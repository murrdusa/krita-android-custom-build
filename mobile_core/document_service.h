/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Krita-backed document transaction service. The mobile UI depends on
 * swappable document and view ports, not Krita's desktop MDI widget.
 * All view activation, guarded close, and native Save/Discard/Cancel
 * delegation go through the view-host contract. A lightweight Krita
 * host can replace the adapters without rewriting mobile workflows.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QColor>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QString>
#include <QVector>
#include <QWidget>
#include <utility>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoFileDialog.h>

#include "KisDocument.h"
#include "KisImportExportManager.h"
#include "KisPart.h"
#include "kis_config.h"
#include "document_host_port.h"
#include "native_document_rollback.h"
#include "view_host_port.h"
#include "view_identity_guard.h"
#include "saved_version_archive.h"

class MobileDocumentService final
{
public:
    MobileDocumentService(QSharedPointer<MobileDocumentHostPort> host,
                          QSharedPointer<MobileViewHostPort> viewHost)
        : m_host(std::move(host)),m_viewHost(std::move(viewHost)) {}

    using OpenDocument=MobileViewHostPort::ViewSnapshot;

    QVector<OpenDocument> openDocuments() const
    {
        const auto viewsHost=m_viewHost;
        return viewsHost?viewsHost->openViews():QVector<OpenDocument>{};
    }

    // The view provider owns identity validation. Never resolve an
    // outdated creation-order index against a changed MDI document list.
    // Retain that provider across callbacks that can destroy the UI
    // service during a tab switch or native Save/Discard/Cancel prompt.
    bool activateDocumentView(const QPointer<QObject> &identity) const
    {
        const auto viewsHost=m_viewHost;
        return viewsHost && identity && viewsHost->activateView(identity);
    }
    bool activateDocument(int nativeIndex) const
    {
        const auto viewsHost=m_viewHost;
        if(!viewsHost)return false;
        const auto identity=MobileViewIdentityGuard::byIndex(
            viewsHost->openViews(),nativeIndex);
        return identity && viewsHost->activateView(identity);
    }

    // Native Krita still owns the Save / Discard / Cancel close prompt.
    // The view provider is responsible for validating that the captured
    // identity still represents the same live canvas.
    bool closeDocumentView(const QPointer<QObject> &identity) const
    {
        const auto viewsHost=m_viewHost;
        return viewsHost && identity && viewsHost->closeView(identity);
    }
    bool closeDocument(int nativeIndex) const
    {
        const auto viewsHost=m_viewHost;
        if(!viewsHost)return false;
        const auto identity=MobileViewIdentityGuard::byIndex(
            viewsHost->openViews(),nativeIndex);
        return identity && viewsHost->closeView(identity);
    }
    bool closeActiveDocument() const
    {
        const auto viewsHost=m_viewHost;
        if(!viewsHost)return false;
        const auto views=viewsHost->openViews();
        const QPointer<QObject> current=viewsHost->currentDocument();
        const auto target=MobileViewIdentityGuard::safeCloseTarget(
            views,current);
        return target && viewsHost->closeView(target);
    }

    // Document I/O and image construction are owned by Krita's engine.
    // The mobile shell supplies the dialogs and initiates transactions,
    // but must not implement its own native file serializer.
    bool createCanvas(const QString &name,int width,int height,
                      int resolutionDpi,const QColor &backgroundColor) const
    {
        // Creating/registering a document emits Krita callbacks which
        // may close the Android window and destroy this service. Keep
        // the backend alive locally throughout the attachment transaction.
        const auto host=m_host;
        if(!host || !host->available() ||
           width<1 || height<1 || resolutionDpi<1 ||
           qint64(width)*height>40000000LL || !KisPart::instance())return false;
        const KoColorSpace *space=KoColorSpaceRegistry::instance()->rgb8();
        if(!space)return false;
        KisDocument *document=KisPart::instance()->createDocument();
        if(!document)return false;
        const QPointer<KisDocument> guardedDocument(document);
        const bool backgroundLayer=backgroundColor.isValid() && backgroundColor.alpha()>0;
        const KoColor background(backgroundLayer?backgroundColor:QColor(Qt::transparent),space);
        const bool created=document->newImage(
            name.trimmed().isEmpty()?QStringLiteral("Unnamed"):name.trimmed(),
            width,height,space,background,
            backgroundLayer?KisConfig::RASTER_LAYER:KisConfig::CANVAS_COLOR,
            backgroundLayer?2:1,QString(),qreal(resolutionDpi)/72.0);
        if(!guardedDocument)return false;
        if(!created) {
            delete guardedDocument.data();
            return false;
        }
        if(!KisPart::instance()) {
            if(guardedDocument)delete guardedDocument.data();
            return false;
        }
        KisPart::instance()->addDocument(guardedDocument.data());
        // Registration callbacks can remove the new document from
        // KisPart without destroying its QObject. Attach must never
        // proceed with an unregistered image or leak that orphan.
        if(!guardedDocument)return false;
        bool registered=false;
        if(KisPart::instance()) {
            for(const auto &entry:KisPart::instance()->documents()) {
                if(entry==guardedDocument.data()) {
                    registered=true;
                    break;
                }
            }
        }
        if(!registered) {
            delete guardedDocument.data();
            return false;
        }
        // A failed host attach must not leave an orphaned, registered
        // Krita document with no view. The host port's false result
        // explicitly guarantees that it did not retain a native view.
        if(!host->attachNewDocument(guardedDocument.data())) {
            // The last native view may destroy its document when
            // adoption fails. Do not remove that document twice.
            MobileFailedDocumentAttach::rollback(
                guardedDocument,
                [](KisDocument *candidate) {
                    if(!KisPart::instance())return false;
                    for(const auto &registered:KisPart::instance()->documents())
                        if(registered==candidate)return true;
                    return false;
                },
                [](KisDocument *candidate) {
                    KisPart::instance()->removeDocument(candidate);
                });
            // A document can survive unregistering during a failed
            // attachment. It has no canvas owner, but still retains
            // its image and layer allocations unless disposed here.
            if(KisPart::instance())
                MobileFailedDocumentAttach::disposeUnregistered(
                    guardedDocument,[](KisDocument *candidate) {
                        if(!KisPart::instance())return true;
                        for(const auto &entry:KisPart::instance()->documents())
                            if(entry==candidate)return true;
                        return false;
                    });
            return false;
        }
        return true;
    }
    int openFiles(QWidget *parent) const
    {
        // Android's file picker may run a nested Qt event loop and close
        // the originating window or its document-service owner. Retain
        // the backend independently of this service's member storage.
        const auto host=m_host;
        const QPointer<QWidget> window(parent);
        if(!host || !host->available() || !window)return 0;
        KoFileDialog dialog(parent,KoFileDialog::ImportFile,
                            QStringLiteral("MobileOpenDocument"));
        dialog.setDefaultDir(QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation));
        // Providers may advertise KRA documents as ZIP or generic binary.
        dialog.setNameFilter(QObject::tr("All files (*)"));
        dialog.setCaption(QObject::tr("Open drawing"));
        const QString path=dialog.filename();
        if(path.isEmpty() || !window || !host->available())return 0;
        const bool opened=host->openFile(path);
        if(!opened && window) {
            // Resolving an Android SAF display name can deliver Qt
            // lifecycle callbacks and destroy the originating window.
            // Resolve the name first; never pass a stale parent to a
            // modal warning after the provider has run.
            const QString name=MobileSavedVersionArchive::projectName(path);
            if(window)
                QMessageBox::warning(window.data(),
                    QObject::tr("Drawing not opened"),
                    QObject::tr("Could not open %1.").arg(name));
        }
        return opened?1:0;
    }
    bool saveCurrentDocument(bool saveAs=false) const
    {
        // A provider may synchronously destroy this service when a
        // document closes during selection. Keep both backends alive
        // independently of the service's member storage.
        const auto host=m_host;
        const auto viewsHost=m_viewHost;
        if(!host || !viewsHost || !host->available())return false;
        const QPointer<QObject> current=viewsHost->currentDocument();
        const auto views=viewsHost->openViews();
        const QPointer<QObject> selected=
            MobileViewIdentityGuard::uniqueActive(views);
        // A focus/selection change while the save dialog is opening
        // must never redirect Save to a different document. Require one
        // unambiguous live view for Krita's currently selected document.
        if(!current || !MobileViewIdentityGuard::maySave(
               views,selected,current))return false;
        const QPointer<KisDocument> document=
            qobject_cast<KisDocument*>(current.data());
        if(!document)return false;
        // Native Krita save path retains its format handling,
        // render-completion checks, backups and Android file provider.
        return host->save(document.data(),saveAs);
    }

    // A deferred UI Save must never silently target a different drawing
    // if document selection changes while a modal menu is closing.
    // Capture the selected MDI identity at click time and revalidate it
    // immediately before invoking Krita's own native save operation.
    bool saveDocumentView(const QPointer<QObject> &identity,
                          bool saveAs=false) const
    {
        const auto host=m_host;
        const auto viewsHost=m_viewHost;
        const QPointer<KisDocument> document=
            documentForView(identity,host,viewsHost);
        return host && document && host->save(document.data(),saveAs);
    }

    bool exportDocumentView(const QPointer<QObject> &identity) const
    {
        const auto host=m_host;
        const auto viewsHost=m_viewHost;
        const QPointer<KisDocument> document=
            documentForView(identity,host,viewsHost);
        return host && document && host->exportCopy(document.data());
    }

    QString activeDocumentTitle() const
    {
        const QPointer<QObject> current=m_viewHost?
            m_viewHost->currentDocument():QPointer<QObject>();
        const auto *document=qobject_cast<KisDocument*>(current.data());
        if(!document)return {};
        const QString path=document->path();
        return path.isEmpty()?QStringLiteral("Untitled"):
            MobileSavedVersionArchive::projectName(path);
    }
    bool documentModified() const
    {
        const QPointer<QObject> current=m_viewHost?
            m_viewHost->currentDocument():QPointer<QObject>();
        const auto *document=qobject_cast<KisDocument*>(current.data());
        return document && document->isModified();
    }
    bool hasDocument() const
    {
        // A stale manager pointer can survive the closing of the last
        // view. Live view-host state is authoritative.
        if(m_viewHost) {
            for(const auto &view:m_viewHost->openViews()) {
                if(view.nativeView && view.document)return true;
            }
            return false;
        }
        return false;
    }

private:
    static QPointer<KisDocument> documentForView(
        const QPointer<QObject> &identity,
        const QSharedPointer<MobileDocumentHostPort> &host,
        const QSharedPointer<MobileViewHostPort> &viewsHost)
    {
        if(!host || !viewsHost || !identity || !host->available())return {};
        // A current snapshot must still contain exactly this selected
        // identity and the same live document owned by Krita. Both
        // backends stay alive if a nested callback destroys the service.
        const auto views=viewsHost->openViews();
        const QPointer<QObject> current=viewsHost->currentDocument();
        if(!current || !MobileViewIdentityGuard::maySave(
               views,identity,current))return {};
        return qobject_cast<KisDocument*>(current.data());
    }
    QSharedPointer<MobileDocumentHostPort> m_host;
    QSharedPointer<MobileViewHostPort> m_viewHost;

};
#endif // Q_OS_ANDROID
