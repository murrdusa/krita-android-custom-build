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
#include <atomic>
#include <memory>
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
#include "view_identity_guard.h"

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
        // Android provider canonicalization may dispatch a nested event
        // loop and destroy the original runtime. Capture the root before
        // that callback rather than dereferencing m_runtime afterward.
        const QPointer<MobileKritaStandaloneRuntime> runtime=m_runtime;
        if(!runtime)return false;
        const QPointer<MobileNativeViewStack> originalRoot(
            runtime->documentRoot());
        if(!runtime || !originalRoot)return false;
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(path);
        if(!runtime || !originalRoot ||
           m_runtime!=runtime ||
           runtime->documentRoot()!=originalRoot.data())return false;
        const auto openViews=originalRoot->openViews();
        for(const auto &view:openViews) {
            if(!m_runtime || !originalRoot ||
               m_runtime->documentRoot()!=originalRoot.data())
                return false;
            const QPointer<KisDocument> existing=
                qobject_cast<KisDocument*>(view.document.data());
            if(!existing || !view.nativeView)return false;
            const QString currentPath=existing->path();
            if(!m_runtime || !originalRoot || !existing || !view.nativeView ||
               m_runtime->documentRoot()!=originalRoot.data())return false;
            const QString draft=existing->property(
                "_mobileDraftPath").toString();
            if(!m_runtime || !originalRoot || !existing || !view.nativeView ||
               m_runtime->documentRoot()!=originalRoot.data())return false;
            const bool samePath=currentPath==path || draft==path;
            const bool sameProject=!identity.isEmpty() &&
                (identity==MobileSavedVersionArchive::canonicalIdentity(currentPath) ||
                 identity==MobileSavedVersionArchive::canonicalIdentity(draft));
            if(samePath || sameProject) {
                // A modal callback may have closed/replaced a different
                // canvas while these identities were canonicalized.
                // Do not activate a stale tab or open the same file twice.
                if(!m_runtime || !originalRoot ||
                   m_runtime->documentRoot()!=originalRoot.data() ||
                   !MobileViewIdentityGuard::sameRecoveryWorkspace(
                       openViews,originalRoot->openViews()))
                    return false;
                return originalRoot->activateView(view.nativeView);
            }
        }
        if(!m_runtime || !originalRoot ||
           m_runtime->documentRoot()!=originalRoot.data() ||
           !MobileViewIdentityGuard::sameRecoveryWorkspace(
               openViews,originalRoot->openViews()))return false;

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
                // openPath() may dispatch Android lifecycle callbacks.
                // Do not retry a different checkpoint after its original
                // native window or Krita registry has disappeared.
                if(!m_runtime || !originalRoot ||
                   m_runtime->documentRoot()!=originalRoot.data() ||
                   !KisPart::instance())return false;
                document=KisPart::instance()->createDocument();
                if(!document)return false;
                const QPointer<KisDocument> candidate(document);
                // Creating a Krita document can dispatch plugin/Qt
                // callbacks before we start the expensive KRA read.
                // Never load into an import whose original native host
                // was replaced during construction.
                if(!m_runtime || !originalRoot ||
                   m_runtime->documentRoot()!=originalRoot.data() ||
                   !KisPart::instance()) {
                    if(candidate)delete candidate.data();
                    return false;
                }
                const bool opened=openProjectSource(
                    candidate.data(),checkpoint.absoluteFilePath(),true,
                    originalRoot);
                if(!candidate)return false;
                if(!m_runtime || !originalRoot ||
                   m_runtime->documentRoot()!=originalRoot.data() ||
                   !KisPart::instance()) {
                    delete candidate.data();
                    return false;
                }
                if(opened) {
                    latest=checkpoint.absoluteFilePath();
                    break;
                }
                delete candidate.data();
                document=nullptr;
            }
        }
        if(!document) {
            if(!m_runtime || !originalRoot ||
               m_runtime->documentRoot()!=originalRoot.data() ||
               !KisPart::instance() ||
               !MobileNativeDocumentPathPolicy::mayAttemptOpen(path))
                return false;
            document=KisPart::instance()->createDocument();
            if(!document)return false;
            const QPointer<KisDocument> candidate(document);
            // The host may be closed by an observer of createDocument()
            // even though the new, unregistered KisDocument survives.
            // Dispose of it before openPath rather than reading a large
            // artwork into a stale Android window.
            if(!m_runtime || !originalRoot ||
               m_runtime->documentRoot()!=originalRoot.data() ||
               !KisPart::instance()) {
                if(candidate)delete candidate.data();
                return false;
            }
            const bool opened=openProjectSource(
                candidate.data(),path,privateDraft || savedVersion,
                originalRoot);
            if(!candidate || !opened || !m_runtime || !originalRoot ||
               m_runtime->documentRoot()!=originalRoot.data() ||
               !KisPart::instance()) {
                if(candidate)delete candidate.data();
                return false;
            }
        }
        // openPath() may dispatch callbacks that close or replace the native
        // view stack. Never attach the finished import to a different host
        // from the one which initiated this Open operation.
        if(!m_runtime || !originalRoot ||
           m_runtime->documentRoot()!=originalRoot.data()) {
            if(document)delete document; // Not yet registered with KisPart.
            return false;
        }
        // Property and path changes notify document observers
        // synchronously. A callback may close the document or replace
        // the native host before the next setter runs. Keep the guard
        // across the entire recovery metadata transaction.
        const QPointer<KisDocument> guardedDocument(document);
        const auto stillOpening=[this,guardedDocument,originalRoot]() {
            return guardedDocument && m_runtime && originalRoot &&
                m_runtime->documentRoot()==originalRoot.data();
        };
        const auto abortOpening=[guardedDocument]() {
            // Before addDocument(), this imported document is still
            // ours. Do not leak it if a metadata callback replaces the
            // host; do not delete a document registered by an observer.
            if(guardedDocument && !isRegistered(guardedDocument.data()))
                delete guardedDocument.data();
            return false;
        };
        if(!latest.isEmpty()) {
            // Reopen the latest completed checkpoint as the original project.
            // Keeping the original path lets the next native Save update it.
            document->setProperty("_mobileDraftPath",latest);
            if(!stillOpening())return abortOpening();
            document->setPath(path);
            if(!stillOpening())return abortOpening();
            document->setLocalFilePath(path);
            if(!stillOpening())return abortOpening();
            if(!applyProjectMime(guardedDocument.data(),path) ||
               !stillOpening())return abortOpening();
            document->setReadWrite(true);
            if(!stillOpening())return abortOpening();
            document->setModified(true);
            if(!stillOpening())return abortOpening();
        }
        if(privateDraft) {
            document->setProperty("_mobileDraftPath",path);
            if(!stillOpening())return abortOpening();
        }
        if(privateDraft || savedVersion) {
            document->resetPath();
            if(!stillOpening())return abortOpening();
            document->setRecovered(true);
            if(!stillOpening())return abortOpening();
            document->setModified(true);
            if(!stillOpening())return abortOpening();
        }

        if(!stillOpening() || !KisPart::instance())return abortOpening();
        KisPart::instance()->addDocument(document);
        // Registration notifies Krita observers. A callback may remove
        // the import from KisPart without destroying its QObject.
        // The ordinary failed-attach rollback intentionally removes
        // only registered documents; dispose of a surviving orphan here
        // instead of retaining a loaded KRA with no owner or canvas.
        if(!guardedDocument)return false;
        if(!isRegistered(guardedDocument.data())) {
            delete guardedDocument.data();
            return false;
        }
        if(!m_runtime || !originalRoot ||
           m_runtime->documentRoot()!=originalRoot.data() ||
           !attachCanvas(guardedDocument.data())) {
            MobileFailedDocumentAttach::rollback(
                guardedDocument,
                [](KisDocument *candidate) {
                    return isRegistered(candidate);
                },
                [](KisDocument *candidate) {
                    KisPart::instance()->removeDocument(candidate);
                });
            // Some registry observers unregister a failed import without
            // deleting its QObject. Retire that now-unowned KRA and its
            // pixel buffers rather than leaving it alive without a view.
            if(KisPart::instance())
                MobileFailedDocumentAttach::disposeUnregistered(
                    guardedDocument,
                    [](KisDocument *candidate) {
                        return isRegistered(candidate);
                    });
            return false;
        }
        // Factory activation callbacks may close or replace the root
        // immediately after adoption. An interrupted Open must not
        // register a stale project location with Recent/Android SAF.
        if(!guardedDocument || !m_runtime || !originalRoot ||
           m_runtime->documentRoot()!=originalRoot.data() ||
           !m_runtime->ownsDocument(guardedDocument))
            return false;
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
    static bool applyProjectMime(KisDocument *document,const QString &path)
    {
        const QPointer<KisDocument> guarded(document);
        if(!guarded)return false;
        // Android provider display-name lookups can dispatch callbacks;
        // do not dereference an import destroyed while resolving its MIME.
        const QString name=MobileSavedVersionArchive::projectName(path);
        if(!guarded)return false;
        if(!MobileSavedVersionArchive::isKritaProjectName(name))return true;
        const QString suffix=name.mid(name.lastIndexOf(QLatin1Char('.'))+1);
        const QByteArray mime=suffix.startsWith(QStringLiteral("krz"),Qt::CaseInsensitive)?
            QByteArray("application/x-krita-archive"):guarded->nativeFormatMimeType();
        if(!guarded)return false;
        guarded->setMimeType(mime);
        return bool(guarded);
    }

    bool openProjectSource(
        KisDocument *document,const QString &path,bool privateCopy,
        const QPointer<MobileNativeViewStack> &originalRoot) const
    {
        const QPointer<KisDocument> guarded(document);
        const auto stillOwned=[this,guarded,originalRoot]() {
            return guarded && m_runtime && originalRoot &&
                m_runtime->documentRoot()==originalRoot.data() &&
                KisPart::instance();
        };
        if(!stillOwned() || !applyProjectMime(guarded.data(),path) ||
           !stillOwned())return false;
        // MIME changes or Android provider-name resolution can replace
        // the native window without destroying the unregistered import.
        // Do not spend memory opening a KRA for a stale workspace.
        return guarded->openPath(path,privateCopy?
            KisDocument::DontAddToRecent:KisDocument::None);
    }

    bool attachCanvas(KisDocument *document) const
    {
        if(!available() || m_replacing || m_saving || !document)return false;
        const QPointer<KisDocument> guardedDocument(document);
        const QPointer<MobileKritaStandaloneRuntime> runtime=m_runtime;
        const QPointer<MobileNativeViewStack> root=runtime?
            runtime->documentRoot():nullptr;
        if(!guardedDocument || !runtime || !root)return false;
        // Image and registry queries can run Qt observers. Do not pass
        // a document or native root destroyed during those callbacks to
        // the engine's view factory.
        const auto image=guardedDocument->image();
        if(!image || !guardedDocument || !runtime || !root ||
           runtime->documentRoot()!=root.data())return false;
        const bool registered=isRegistered(guardedDocument.data());
        if(!registered || !guardedDocument || !runtime || !root ||
           runtime->documentRoot()!=root.data() || !m_saver)return false;
        const QScopedValueRollback<bool> attaching(m_replacing,true);
        // New/Open must preserve every other canvas and its unsaved edits.
        // Native tab selection and close ownership live in the view stack.
        return runtime->attachDocument(guardedDocument.data(),m_saver.data())!=nullptr;
    }

    bool writeDocument(KisDocument *document,bool saveAs,bool exporting) const
    {
        if(!available() || m_saving || !document)return false;
        const QPointer<KisDocument> guardedDocument(document);
        const QPointer<MobileKritaStandaloneRuntime> runtime=m_runtime;
        if(!guardedDocument || !runtime)return false;
        // ownsDocument() traverses Krita's native view/document registry.
        // An observer may close the selected canvas while that ownership
        // query runs. Never dereference the old runtime or document to
        // obtain its root after such a reentrant teardown.
        const bool owned=runtime->ownsDocument(guardedDocument.data());
        if(!runtime || !guardedDocument || !owned)return false;
        // Save As and render-wait dialogs can reenter Qt event handling;
        // the runtime may survive while its native view stack is replaced.
        const QPointer<MobileNativeViewStack> originalRoot(
            runtime->documentRoot());
        if(!originalRoot || !runtime || !guardedDocument ||
           runtime->documentRoot()!=originalRoot.data() ||
           originalRoot->currentDocument().data()!=guardedDocument.data())
            return false;
        const QScopedValueRollback<bool> transaction(m_saving,true);
        // Krita registry observers may synchronously close the canvas
        // while ownsDocument() walks engine views. Recheck the guarded
        // root and document AFTER that query, not just before it.
        const auto stillOwnedSelected=[runtime,originalRoot,guardedDocument]() {
            if(!runtime || !originalRoot || !guardedDocument ||
               runtime->documentRoot()!=originalRoot.data())return false;
            const bool owned=runtime->ownsDocument(guardedDocument.data());
            return owned && runtime && originalRoot && guardedDocument &&
                runtime->documentRoot()==originalRoot.data() &&
                originalRoot->currentDocument().data()==guardedDocument.data();
        };

        // Match Krita's native render-completion protection. Refuse
        // Ignore instead of allowing a potentially corrupt overwrite.
        // The Android host may be destroyed while the render-completion
        // dialog processes a nested event loop. A stack-allocated child
        // would otherwise be deleted by Qt's parent teardown.
        const auto image=guardedDocument->image();
        if(!runtime || !guardedDocument || !originalRoot ||
           runtime->documentRoot()!=originalRoot.data() ||
           originalRoot->currentDocument().data()!=guardedDocument.data())
            return false;
        QPointer<KisDelayedSaveDialog> idle(new KisDelayedSaveDialog(
            image,KisDelayedSaveDialog::SaveDialog,0,runtime->window()));
        if(!idle)return false;
        idle->blockIfImageIsBusy();
        const bool renderReady=idle &&
            idle->result()==KisDelayedSaveDialog::Accepted;
        if(idle)delete idle.data();
        if(!renderReady || !stillOwnedSelected())return false;

        // Every Krita document query below may notify Qt observers
        // during a nested save/close workflow. Keep the original view
        // and document selected before preparing any file chooser.
        const auto stillSelected=[this,originalRoot,guardedDocument]() {
            return guardedDocument && m_runtime && originalRoot &&
                m_runtime->documentRoot()==originalRoot.data() &&
                originalRoot->currentDocument().data()==guardedDocument.data();
        };
        const QString currentPath=guardedDocument->path();
        if(!stillSelected())return false;
        const QByteArray currentMime=guardedDocument->mimeType();
        if(!stillSelected())return false;
        const auto formats=KisImportExportManager::supportedMimeTypes(
            KisImportExportManager::Export);
        if(!stillSelected())return false;
        const bool unsupportedMime=!formats.contains(
            QString::fromLatin1(currentMime));
        const bool recovered=guardedDocument->isRecovered();
        if(!stillSelected())return false;
        const bool choosePath=MobileNativeDocumentPathPolicy::needsSaveAs(
            currentPath,saveAs,recovered,unsupportedMime);

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
            if(!stillSelected())return false;
            const QString nativeMime=QString::fromLatin1(
                guardedDocument->nativeFormatMimeType());
            if(!stillSelected())return false;
            const QString preferred=unsupportedMime || currentMime.isEmpty()?
                nativeMime:QString::fromLatin1(currentMime);
            dialog.setMimeTypeFilters(formats,preferred);
            target=MobileNativeDocumentPathPolicy::localFilePath(
                dialog.filename());
            if(target.isEmpty() || !stillOwnedSelected())
                return false; // Cancel, closed canvas, or different active document.
            QString selected=KisMimeDatabase::mimeTypeForFile(target,false);
            if(!formats.contains(selected))
                selected=dialog.selectedMimeType();
            if(!formats.contains(selected))
                return false; // Never encode KRA under an unknown suffix.
            targetMime=selected.toLatin1();
        }

        if(target.isEmpty() || targetMime.isEmpty() ||
           !stillOwnedSelected())return false;
        // With multiple native canvases, a Save As or Export Copy could
        // otherwise overwrite the on-disk project of a different open
        // tab while its dirty in-memory artwork remains unsaved.
        // Compare canonical KRA identities, including equivalent SAF
        // document URI forms, before initiating any native disk write.
        const auto open=originalRoot->openViews();
        // A path or draft-property lookup can synchronously close the
        // other document or replace the native workspace. Do not continue
        // scanning with a stale QObject or skip an unknown destination:
        // failing closed avoids overwriting another open drawing.
        const auto stillSaving=[this,originalRoot,guardedDocument]() {
            return guardedDocument && m_runtime && originalRoot &&
                m_runtime->documentRoot()==originalRoot.data() &&
                originalRoot->currentDocument().data()==guardedDocument.data();
        };
        if(!stillSaving())return false;
        int activeViews=0;
        int ownerViews=0;
        for(const auto &view:open) {
            if(!stillSaving())return false;
            // Do not save while the native tab stack is transitioning or
            // holds a view whose document cannot be inspected. Skipping
            // such a view would bypass the cross-canvas overwrite check.
            const QPointer<KisDocument> other=
                qobject_cast<KisDocument*>(view.document.data());
            if(!view.nativeView || !other)return false;
            if(other==guardedDocument)++ownerViews;
            if(view.active) {
                ++activeViews;
                if(other!=guardedDocument)return false;
            }
            if(other==guardedDocument)continue;
            const QString otherPath=other->path();
            if(!stillSaving() || !other || !view.nativeView)return false;
            const QString otherDraft=other->property(
                "_mobileDraftPath").toString();
            if(!stillSaving() || !other || !view.nativeView)return false;
            if(MobileOpenDocumentDestinationPolicy::conflictsWithBackingFile(
                   target,otherPath,otherDraft)) {
                const QPointer<QWidget> owner=m_runtime->window();
                if(owner)
                    QMessageBox::warning(owner.data(),
                        exporting?QObject::tr("Export a copy"):QObject::tr("Save drawing"),
                        QObject::tr("That file is already open in another canvas. "
                                    "Choose a different destination to avoid overwriting it."));
                return false;
            }
        }
        if(!stillSaving() ||
           !MobileViewIdentityGuard::sameRecoveryWorkspace(
               open,originalRoot->openViews()) ||
           !stillSaving())return false;
        // A save is authorized only by one uniquely selected native
        // canvas. Engine currentDocument alone can lag behind a tab
        // switch and must not authorize writes during that gap.
        if(activeViews!=1 || ownerViews!=1)return false;
        // A recovered canvas can have no visible path even though its
        // private draft/checkpoint is still open. Neither Save As nor
        // Export Copy may overwrite that source recovery file.
        const QString ownDraft=guardedDocument->property(
            "_mobileDraftPath").toString();
        if(!stillSaving())return false;
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
        if(!originalRoot || !m_runtime ||
           m_runtime->documentRoot()!=originalRoot.data() ||
           originalRoot->currentDocument().data()!=guardedDocument.data())
            return false;
        // Krita's return value only says whether background saving started.
        // A Close → Save must keep the canvas open if the write later fails.
        // Queued signals already posted by Krita can still arrive after
        // disconnect(). Never capture this Save call's stack variables
        // by reference in the completion callback.
        const auto completed=std::make_shared<std::atomic_bool>(false);
        const auto connection=QObject::connect(guardedDocument.data(),
            &KisDocument::sigSavingFinished,guardedDocument.data(),
            [completed,target](const QString &path) {
                if(path==target)completed->store(true);
            });
        const bool started=exporting?
            guardedDocument->exportDocument(target,targetMime,false,true):
            choosePath?guardedDocument->saveAs(target,targetMime,true):
                       guardedDocument->save(true,{});
        if(started && guardedDocument)
            guardedDocument->waitForSavingToComplete();
        QObject::disconnect(connection);
        // The write can complete during a nested Qt event loop while
        // Android replaces the native canvas root. Record history and
        // report success only for a document still owned by the same host.
        if(!completed->load() || !stillOwnedSelected())return false;
        // The ownership query and subsequent metadata reads can execute
        // Krita/Qt callbacks. Capture all document-derived information
        // before notifying the recent-files and archive services.
        if(!stillSaving())return false;
        QByteArray nativeMime;
        QString draft;
        if(!exporting) {
            nativeMime=guardedDocument->nativeFormatMimeType();
            if(!stillSaving())return false;
            if(targetMime==nativeMime ||
               targetMime=="application/x-krita-archive") {
                draft=guardedDocument->property(
                    "_mobileDraftPath").toString();
                if(!stillSaving())return false;
            }
        }
        if(!stillSaving())return false;
        if(!exporting) {
            MobileSavedVersionArchive::recordPreviewRevision(target);
            if(!stillSaving())return false;
            rememberPath(target);
            if(!stillSaving())return false;
            if(targetMime==nativeMime ||
               targetMime=="application/x-krita-archive") {
                // Enqueue without re-reading a potentially closed
                // document after recent-files listeners have run.
                MobileSavedVersionArchive::enqueueAfterSave(target,draft);
                if(!stillSaving())return false;
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
        const QPointer<MobileKritaStandaloneRuntime> alive(runtime);
        if(!alive)return services;
        const QPointer<MobileLightweightCanvasHost> host=alive->window();
        const QPointer<MobileNativeViewStack> root=alive->documentRoot();
        if(!alive || !host || !root)return services;

        const auto documents=QSharedPointer<MobileKritaStandaloneDocuments>::
            create(alive.data());
        auto *saver=new MobileKritaNativeSavePortAdapter(documents,alive.data());
        const QPointer<MobileKritaNativeSavePortAdapter> guardedSaver(saver);
        documents->setSaveHost(saver);
        services.documents=documents;
        services.views=host->nativeViewPort();
        if(!alive || !host || !root || !guardedSaver)return {};
        services.commands=alive->commandPort();
        if(!alive || !host || !root || !guardedSaver)return {};
        services.documentRoot=root.data();

        const bool ready=services.ready();
        if(!alive || !host || !root || !guardedSaver || !ready) {
            if(guardedSaver)delete guardedSaver.data();
            return {};
        }
        const bool attached=host->attachServices(services);
        // A nested Qt callback can destroy the runtime and its QObject
        // child saver during attachment. Never delete the stale raw
        // pointer, or return services tied to a destroyed host/root.
        if(!alive || !host || !root || !guardedSaver || !attached ||
           alive->window()!=host.data() ||
           alive->documentRoot()!=root.data()) {
            if(guardedSaver)delete guardedSaver.data();
            return {};
        }
        return services;
    }
};
#endif
