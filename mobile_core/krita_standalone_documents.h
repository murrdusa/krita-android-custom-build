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
#include <QEnableSharedFromThis>
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
#include "kis_image.h"
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
#include "open_document_destination_policy.h"

class MobileKritaStandaloneDocuments final : public MobileDocumentHostPort,
                                            public QEnableSharedFromThis<MobileKritaStandaloneDocuments>
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
        // Krita callbacks can tear down the containing window while
        // this method is on the stack. Hold the shared backend until
        // its reentrancy flag and nested operations have unwound.
        const auto keepAlive=sharedFromThis();
        return keepAlive && !m_opening && attachCanvas(document);
    }

    bool openFile(const QString &requestedPath) const override
    {
        const auto keepAlive=sharedFromThis();
        if(!keepAlive)return false;
        // Native Krita openPath expects a filesystem path, not the
        // file:// spelling returned by some Android/Qt file pickers.
        // Keep content:// URIs intact for the SAF bridge.
        const QString path=MobileNativeDocumentPathPolicy::localFilePath(
            requestedPath);
        if(!available() || m_opening || m_replacing || m_saving ||
           path.trimmed().isEmpty())return false;
        const QScopedValueRollback<bool> transaction(m_opening,true);

        // Reopening any already-open project preserves its unsaved edits
        // and activates the existing native tab. Equivalent Android SAF
        // tree/document URIs must also resolve to that same project.
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(path);
        auto *root=m_runtime->documentRoot();
        for(const auto &view:root->openViews()) {
            auto *existing=qobject_cast<KisDocument*>(view.document.data());
            if(!existing || !view.nativeView)continue;
            const QString currentPath=existing->path();
            const QString draft=existing->property("_mobileDraftPath").toString();
            const bool samePath=currentPath==path || draft==path;
            const bool sameProject=!identity.isEmpty() &&
                (identity==MobileSavedVersionArchive::canonicalIdentity(currentPath) ||
                 identity==MobileSavedVersionArchive::canonicalIdentity(draft));
            if(samePath || sameProject)
                return root->activateView(view.nativeView);
        }

        const bool privateDraft=MobileDraftRecovery::isDraft(path);
        const bool savedVersion=MobileSavedVersionArchive::isRecoveryCopy(path);
        KisDocument *document=nullptr;
        QString latest;
        if(!privateDraft && !savedVersion) {
            const auto checkpoints=MobileDraftRecovery::checkpointsFor(
                path,qMax(MobileSavedVersionArchive::projectModified(path),
                    MobileSavedVersionArchive::previewRevisions().value(
                        MobileSavedVersionArchive::canonicalIdentity(path)).toLongLong()));
            for(const auto &checkpoint:checkpoints) {
                document=KisPart::instance()->createDocument();
                if(!document)return false;
                const QPointer<KisDocument> candidate(document);
                const bool opened=openProjectSource(
                    document,checkpoint.absoluteFilePath(),true);
                if(!candidate)return false;
                if(opened) {
                    latest=checkpoint.absoluteFilePath();
                    break;
                }
                delete candidate.data();
                document=nullptr;
            }
        }
        if(!document) {
            if(!MobileNativeDocumentPathPolicy::mayAttemptOpen(path))return false;
            document=KisPart::instance()->createDocument();
            if(!document)return false;
            const QPointer<KisDocument> candidate(document);
            const bool opened=openProjectSource(
                document,path,privateDraft || savedVersion);
            if(!candidate || !opened) {
                if(candidate)delete candidate.data();
                return false;
            }
        }
        if(!latest.isEmpty()) {
            // Reopen the latest completed checkpoint as the original project.
            // Keeping the original path lets the next native Save update it.
            document->setProperty("_mobileDraftPath",latest);
            document->setPath(path);
            document->setLocalFilePath(path);
            applyProjectMime(document,path);
            document->setReadWrite(true);
            document->setModified(true);
        }
        if(privateDraft)document->setProperty("_mobileDraftPath",path);
        if(privateDraft || savedVersion) {
            document->resetPath();
            document->setRecovered(true);
            document->setModified(true);
        }

        const QPointer<KisDocument> guardedDocument(document);
        KisPart::instance()->addDocument(document);
        // Registration notifies Krita observers. A callback can close
        // the imported document before the native view is attached.
        if(!guardedDocument)return false;
        if(!attachCanvas(guardedDocument.data())) {
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
        if(!privateDraft && !savedVersion) {
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
        const auto keepAlive=sharedFromThis();
        return keepAlive && writeDocument(document,saveAs,false);
    }

    bool exportCopy(KisDocument *document) const override
    {
        const auto keepAlive=sharedFromThis();
        return keepAlive && writeDocument(document,true,true);
    }

private:
    static void applyProjectMime(KisDocument *document,const QString &path)
    {
        const QString name=MobileSavedVersionArchive::projectName(path);
        if(!MobileSavedVersionArchive::isKritaProjectName(name))return;
        const QString suffix=name.mid(name.lastIndexOf(QLatin1Char('.'))+1);
        document->setMimeType(suffix.startsWith(QStringLiteral("krz"),Qt::CaseInsensitive)?
            QByteArray("application/x-krita-archive"):document->nativeFormatMimeType());
    }

    static bool openProjectSource(KisDocument *document,const QString &path,bool privateCopy)
    {
        applyProjectMime(document,path);
        return document->openPath(path,privateCopy?KisDocument::DontAddToRecent:KisDocument::None);
    }

    bool attachCanvas(KisDocument *document) const
    {
        if(!available() || m_replacing || m_saving || !document ||
           !document->image() || !isRegistered(document))return false;
        const QScopedValueRollback<bool> attaching(m_replacing,true);
        // New/Open must preserve every other canvas and its unsaved edits.
        // Native tab selection and close ownership live in the view stack.
        return m_runtime->attachDocument(document,m_saver)!=nullptr;
    }

    bool writeDocument(KisDocument *document,bool saveAs,bool exporting) const
    {
        if(!available() || m_saving || !document ||
           !m_runtime->ownsDocument(document) ||
           m_runtime->documentRoot()->currentDocument().data()!=document)
            return false;
        const QScopedValueRollback<bool> transaction(m_saving,true);
        QPointer<KisDocument> guardedDocument(document);

        // Match Krita's native render-completion protection. Refuse
        // Ignore instead of allowing a potentially corrupt overwrite.
        KisDelayedSaveDialog idle(
            document->image(),KisDelayedSaveDialog::SaveDialog,0,
            m_runtime->window());
        idle.blockIfImageIsBusy();
        if(idle.result()!=KisDelayedSaveDialog::Accepted ||
           !guardedDocument || !m_runtime ||
           !m_runtime->ownsDocument(guardedDocument) ||
           m_runtime->documentRoot()->currentDocument().data()!=guardedDocument.data())
            return false;

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
            target=MobileNativeDocumentPathPolicy::localFilePath(
                dialog.filename());
            if(target.isEmpty() || !guardedDocument ||
               !m_runtime || !m_runtime->ownsDocument(guardedDocument) ||
               m_runtime->documentRoot()->currentDocument().data()!=guardedDocument.data())
                return false; // Cancel, closed canvas, or different active document.
            QString selected=KisMimeDatabase::mimeTypeForFile(target,false);
            if(!formats.contains(selected))
                selected=dialog.selectedMimeType();
            if(!formats.contains(selected))
                return false; // Never encode KRA under an unknown suffix.
            targetMime=selected.toLatin1();
        }

        if(!guardedDocument || !m_runtime ||
           !m_runtime->ownsDocument(guardedDocument) ||
           m_runtime->documentRoot()->currentDocument().data()!=guardedDocument.data() ||
           target.isEmpty() || targetMime.isEmpty())return false;
        // With multiple native canvases, a Save As or Export Copy could
        // otherwise overwrite the on-disk project of a different open
        // tab while its dirty in-memory artwork remains unsaved.
        // Compare canonical KRA identities, including equivalent SAF
        // document URI forms, before initiating any native disk write.
        const auto open=m_runtime->documentRoot()->openViews();
        int activeViews=0;
        int ownerViews=0;
        for(const auto &view:open) {
            // Do not save while the native tab stack is transitioning or
            // holds a view whose document cannot be inspected. Skipping
            // such a view would bypass the cross-canvas overwrite check.
            const auto *other=qobject_cast<KisDocument*>(view.document.data());
            if(!view.nativeView || !other)return false;
            if(other==guardedDocument.data())++ownerViews;
            if(view.active) {
                ++activeViews;
                if(other!=guardedDocument.data())return false;
            }
            if(other==guardedDocument.data())continue;
            const QString otherPath=other->path();
            const QString otherDraft=other->property(
                "_mobileDraftPath").toString();
            if(MobileOpenDocumentDestinationPolicy::conflictsWithBackingFile(
                   target,otherPath,otherDraft)) {
                QMessageBox::warning(m_runtime->window(),
                    exporting?QObject::tr("Export a copy"):QObject::tr("Save drawing"),
                    QObject::tr("That file is already open in another canvas. "
                                "Choose a different destination to avoid overwriting it."));
                return false;
            }
        }
        // A save is authorized only by one uniquely selected native
        // canvas. Engine currentDocument alone can lag behind a tab
        // switch and must not authorize writes during that gap.
        if(activeViews!=1 || ownerViews!=1)return false;
        // A recovered canvas can have no visible path even though its
        // private draft/checkpoint is still open. Neither Save As nor
        // Export Copy may overwrite that source recovery file.
        const QString ownDraft=guardedDocument->property(
            "_mobileDraftPath").toString();
        if(MobileOpenDocumentDestinationPolicy::conflictsWithOwnBackingFile(
               target,currentPath,ownDraft,choosePath,exporting)) {
            QMessageBox::warning(m_runtime->window(),
                exporting?QObject::tr("Export a copy"):QObject::tr("Save drawing"),
                QObject::tr("Choose a different file from the open drawing "
                            "or its private recovery copy."));
            return false;
        }
        // A modeless picker or another UI callback may have activated a
        // different tab since collision scanning; refuse to write the
        // old canvas's data when it is no longer the active native view.
        if(!m_runtime->documentRoot() ||
           m_runtime->documentRoot()->currentDocument().data()!=guardedDocument.data())
            return false;
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
            MobileSavedVersionArchive::recordPreviewRevision(target);
            rememberPath(target);
            if(targetMime==guardedDocument->nativeFormatMimeType() ||
               targetMime=="application/x-krita-archive") {
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
        const QUrl url=MobileNativeDocumentPathPolicy::recentFileUrl(path);
        if(url.isValid() && !url.isEmpty())
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
