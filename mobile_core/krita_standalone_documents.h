/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Real Krita document operations for the independent Qt host.
 * Reuses KisPart/KisDocument import/export and Android-aware KoFileDialog;
 * never serializes painting or KRA files in Mobile Studio.
 *
 * The desktop host remains an explicit fallback while native startup
 * and multi-document lifecycle are tested on-device.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStringList>

#include <KoFileDialog.h>
#include <KisMimeDatabase.h>
#include <KisRecentFilesManager.h>
#include "KisDocument.h"
#include "KisImportExportManager.h"
#include "KisPart.h"
#include "dialogs/kis_delayed_save_dialog.h"
#include "document_host_port.h"
#include "native_document_rollback.h"
#include "native_document_path_policy.h"
#include "host_services.h"
#include "krita_standalone_runtime.h"
#include "native_krita_save_host.h"
#include "saved_version_archive.h"

class MobileKritaStandaloneDocuments final : public MobileDocumentHostPort
{
public:
    explicit MobileKritaStandaloneDocuments(
        MobileKritaStandaloneRuntime *runtime)
        : m_runtime(runtime) {}

    void setSaveHost(MobileKritaNativeSaveHost *host)
    {
        m_saver=host;
    }

    bool available() const override
    {
        return m_runtime && m_runtime->viewManager() &&
               m_runtime->window() && m_runtime->documentRoot() &&
               m_saver && KisPart::instance();
    }

    // The shared MobileDocumentService already created and registered
    // this document with KisPart. On failure, leave no live view, so
    // that service can roll back the document registration.
    bool attachNewDocument(KisDocument *document) const override
    {
        return !m_opening && replaceCanvas(document);
    }

    bool openFile(const QString &path) const override
    {
        if(!available() || m_opening || m_replacing || m_saving ||
           !MobileNativeDocumentPathPolicy::mayAttemptOpen(path))
            return false;
        const QScopedValueRollback<bool> transaction(m_opening,true);

        // Opening the current document again must keep its unsaved edits.
        const auto current=m_runtime->documentRoot()->currentDocument();
        if(auto *existing=qobject_cast<KisDocument*>(current.data())) {
            if(existing->path()==path ||
               existing->property("_mobileDraftPath").toString()==path)return true;
            const QString identity=MobileSavedVersionArchive::canonicalIdentity(path);
            if(!identity.isEmpty() &&
               identity==MobileSavedVersionArchive::canonicalIdentity(existing->path()))
                return true;
        }

        KisDocument *document=KisPart::instance()->createDocument();
        if(!document)return false;

        // Native Krita import filters (including KRA, PNG and PSD)
        // are chosen inside openPath. This also presents its existing
        // autosave-recovery prompt, rather than discarding recovery files.
        if(!document->openPath(path,KisDocument::None)) {
            delete document;
            return false;
        }
        const bool draft=MobileDraftRecovery::isDraft(path);
        const bool savedVersion=MobileSavedVersionArchive::isRecoveryCopy(path);
        if(draft)document->setProperty("_mobileDraftPath",path);
        if(draft || savedVersion) {
            document->resetPath();
            document->setRecovered(true);
            document->setModified(true);
        }

        KisPart::instance()->addDocument(document);
        QPointer<KisDocument> guardedDocument=document;
        if(!replaceCanvas(document)) {
            MobileFailedDocumentAttach::rollback(
                guardedDocument,
                [](KisDocument *candidate) {
                    return isRegistered(candidate);
                },
                [](KisDocument *candidate) {
                    KisPart::instance()->removeDocument(candidate);
                });
            return false;
        }
        if(!draft && !savedVersion) {
            rememberPath(path);
            if(path.startsWith(QStringLiteral("content://"))) {
                const auto uri=QAndroidJniObject::fromString(path);
                QAndroidJniObject::callStaticMethod<void>(
                    "org/krita/android/MainActivity","tabletRememberProjectAccess",
                    "(Ljava/lang/String;)V",uri.object<jstring>());
            }
        }
        return true;
    }

    bool save(KisDocument *document,bool saveAs) const override
    {
        return writeDocument(document,saveAs,false);
    }

    bool exportCopy(KisDocument *document) const override
    {
        return writeDocument(document,true,true);
    }

private:
    bool replaceCanvas(KisDocument *document) const
    {
        if(!available() || m_replacing || m_saving || !document ||
           !document->image() || !isRegistered(document))return false;
        const QScopedValueRollback<bool> replacing(m_replacing,true);
        auto *root=m_runtime->documentRoot();
        const auto views=root->openViews();
        for(const auto &view:views) {
            if(!root->closeView(view.nativeView))return false;
        }
        return root->openViews().isEmpty() &&
               m_runtime->attachDocument(document,m_saver)!=nullptr;
    }

    bool writeDocument(KisDocument *document,bool saveAs,bool exporting) const
    {
        if(!available() || m_saving || !document ||
           !m_runtime->ownsDocument(document))return false;
        const QScopedValueRollback<bool> transaction(m_saving,true);
        QPointer<KisDocument> guardedDocument(document);

        // Match Krita's native render-completion protection. Refuse
        // Ignore instead of allowing a potentially corrupt overwrite.
        KisDelayedSaveDialog idle(
            document->image(),KisDelayedSaveDialog::SaveDialog,0,
            m_runtime->window());
        idle.blockIfImageIsBusy();
        if(idle.result()!=KisDelayedSaveDialog::Accepted ||
           !guardedDocument ||
           !m_runtime->ownsDocument(guardedDocument))return false;

        const QString currentPath=guardedDocument->path();
        const QByteArray currentMime=guardedDocument->mimeType();
        const auto formats=KisImportExportManager::supportedMimeTypes(
            KisImportExportManager::Export);
        const bool unsupportedMime=!formats.contains(
            QString::fromLatin1(currentMime));
        const bool choosePath=MobileNativeDocumentPathPolicy::needsSaveAs(
            currentPath,saveAs,guardedDocument->isRecovered(),
            unsupportedMime);

        QString target=currentPath;
        QByteArray targetMime=currentMime;
        if(choosePath) {
            KoFileDialog dialog(m_runtime->window(),
                                KoFileDialog::SaveFile,
                                exporting?QStringLiteral("MobileStudioExportCopy"):
                                          QStringLiteral("MobileStudioNativeSaveAs"));
            dialog.setCaption(exporting?QObject::tr("Export a copy"):
                                        QObject::tr("Save drawing"));
            const QString proposed=
                MobileNativeDocumentPathPolicy::saveDialogStartPath(
                    currentPath,
                    QStandardPaths::writableLocation(
                        QStandardPaths::PicturesLocation));
            if(!proposed.isEmpty())dialog.setDefaultDir(proposed,true);
            const QString nativeMime=QString::fromLatin1(
                guardedDocument->nativeFormatMimeType());
            const QString preferred=unsupportedMime || currentMime.isEmpty()?
                nativeMime:QString::fromLatin1(currentMime);
            dialog.setMimeTypeFilters(formats,preferred);
            target=dialog.filename();
            if(target.isEmpty() || !guardedDocument ||
               !m_runtime || !m_runtime->ownsDocument(guardedDocument))
                return false; // Cancel or different document.
            QString selected=KisMimeDatabase::mimeTypeForFile(target,false);
            if(!formats.contains(selected))
                selected=dialog.selectedMimeType();
            if(!formats.contains(selected))
                return false; // Never encode KRA under an unknown suffix.
            targetMime=selected.toLatin1();
        }

        if(!guardedDocument || !m_runtime ||
           !m_runtime->ownsDocument(guardedDocument) ||
           target.isEmpty() || targetMime.isEmpty())return false;
        if(exporting) {
            const QString canonical=QFileInfo(target).canonicalFilePath();
            if(target==currentPath ||
               (!canonical.isEmpty() && canonical==QFileInfo(currentPath).canonicalFilePath())) {
                QMessageBox::warning(m_runtime->window(),QObject::tr("Export a copy"),
                    QObject::tr("Choose a different file from the open drawing."));
                return false;
            }
        }
        // Krita's return value only says whether background saving started.
        // A Close → Save must keep the canvas open if the write later fails.
        bool completed=false;
        const auto connection=QObject::connect(guardedDocument.data(),
            &KisDocument::sigSavingFinished,guardedDocument.data(),
            [&completed,&target](const QString &path) {
                if(path==target)completed=true;
            });
        const bool started=exporting?
            guardedDocument->exportDocument(target,targetMime,false,true):
            choosePath?guardedDocument->saveAs(target,targetMime,true):
                       guardedDocument->save(true,{});
        if(started && guardedDocument)
            guardedDocument->waitForSavingToComplete();
        QObject::disconnect(connection);
        if(!completed || !guardedDocument || !m_runtime ||
           !m_runtime->ownsDocument(guardedDocument))return false;

        if(!exporting) {
            rememberPath(target);
            if(targetMime==guardedDocument->nativeFormatMimeType()) {
                // An unsaved drawing already has historical private draft
                // checkpoints. Keep them when it first receives a name.
                const QString draft=guardedDocument->property(
                    "_mobileDraftPath").toString();
                MobileSavedVersionArchive::enqueueAfterSave(target,draft);
            }
        }
        return true;
    }

    static void rememberPath(const QString &path)
    {
        if(path.isEmpty())return;
        const QUrl url=path.startsWith(QStringLiteral("content:"))?
            QUrl(path):QUrl::fromLocalFile(path);
        KisRecentFilesManager::instance()->add(url);
    }

    static bool isRegistered(KisDocument *document)
    {
        if(!document || !KisPart::instance())return false;
        for(const auto &entry:KisPart::instance()->documents()) {
            if(entry==document)return true;
        }
        return false;
    }

    QPointer<MobileKritaStandaloneRuntime> m_runtime;
    QPointer<MobileKritaNativeSaveHost> m_saver;
    mutable bool m_opening=false;
    mutable bool m_replacing=false;
    mutable bool m_saving=false;
};

class MobileKritaStandaloneDocumentsBootstrap final
{
public:
    static MobileHostServices create(MobileKritaStandaloneRuntime *runtime)
    {
        MobileHostServices services;
        if(!runtime || !runtime->window() || !runtime->documentRoot())
            return services;

        const auto documents=QSharedPointer<MobileKritaStandaloneDocuments>::
            create(runtime);
        auto *saver=new MobileKritaNativeSavePortAdapter(documents,runtime);
        documents->setSaveHost(saver);
        services.documents=documents;
        services.views=runtime->window()->nativeViewPort();
        services.commands=runtime->commandPort();
        services.documentRoot=runtime->documentRoot();

        if(!services.ready() ||
           !runtime->window()->attachServices(services)) {
            delete saver;
            return {};
        }
        return services;
    }
};
#endif
