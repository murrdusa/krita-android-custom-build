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
#include <QFile>
#include <QFileInfo>
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
        if(!available() || !document || !document->image() ||
           !isRegistered(document))return false;
        return m_runtime->attachDocument(document,m_saver)!=nullptr;
    }

    bool openFile(const QString &path) const override
    {
        if(!available() || m_opening ||
           !MobileNativeDocumentPathPolicy::mayAttemptOpen(path))
            return false;
        const QScopedValueRollback<bool> transaction(m_opening,true);

        KisDocument *document=KisPart::instance()->createDocument();
        if(!document)return false;

        // Native Krita import filters (including KRA, PNG and PSD)
        // are chosen inside openPath. This also presents its existing
        // autosave-recovery prompt, rather than discarding recovery files.
        if(!document->openPath(path,KisDocument::None)) {
            delete document;
            return false;
        }

        KisPart::instance()->addDocument(document);
        QPointer<KisDocument> guardedDocument=document;
        if(!attachNewDocument(document)) {
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
        rememberPath(path);
        return true;
    }

    bool save(KisDocument *document,bool saveAs) const override
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
                                QStringLiteral("MobileStudioNativeSaveAs"));
            dialog.setCaption(QObject::tr("Save drawing"));
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
        // Let KisDocument apply platform SAF providers, backups,
        // native file format conversion, and asynchronous completion.
        const bool saved=choosePath?
            guardedDocument->saveAs(target,targetMime,true):
            guardedDocument->save(true,{});
        if(saved && guardedDocument)
            guardedDocument->waitForSavingToComplete();
        if(saved && guardedDocument && !guardedDocument->isModified())
            rememberPath(target);
        return saved && guardedDocument &&
               m_runtime && m_runtime->ownsDocument(guardedDocument);
    }

private:
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
