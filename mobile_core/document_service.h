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
        return m_viewHost?m_viewHost->openViews():QVector<OpenDocument>{};
    }

    // The view provider owns identity validation. Never resolve an
    // outdated creation-order index against a changed MDI document list.
    bool activateDocumentView(const QPointer<QObject> &identity) const
    {
        return m_viewHost && identity && m_viewHost->activateView(identity);
    }
    bool activateDocument(int nativeIndex) const
    {
        return activateDocumentView(
            MobileViewIdentityGuard::byIndex(openDocuments(),nativeIndex));
    }

    // Native Krita still owns the Save / Discard / Cancel close prompt.
    // The view provider is responsible for validating that the captured
    // identity still represents the same live canvas.
    bool closeDocumentView(const QPointer<QObject> &identity) const
    {
        return m_viewHost && identity && m_viewHost->closeView(identity);
    }
    bool closeDocument(int nativeIndex) const
    {
        return closeDocumentView(
            MobileViewIdentityGuard::byIndex(openDocuments(),nativeIndex));
    }
    bool closeActiveDocument() const
    {
        const auto views=openDocuments();
        const QPointer<QObject> current=m_viewHost?
            m_viewHost->currentDocument():QPointer<QObject>();
        return closeDocumentView(
            MobileViewIdentityGuard::safeCloseTarget(views,current));
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
        guardedDocument->setProperty("_mobileBlankCanvas",true);
        if(!guardedDocument || !KisPart::instance()) {
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
            QMessageBox::warning(window.data(),QObject::tr("Drawing not opened"),
                QObject::tr("Could not open %1.").arg(
                    MobileSavedVersionArchive::projectName(path)));
        }
        return opened?1:0;
    }
    bool saveCurrentDocument(bool saveAs=false) const
    {
        if(!m_host || !m_host->available() || !m_viewHost)return false;
        const QPointer<QObject> current=m_viewHost->currentDocument();
        const auto views=m_viewHost->openViews();
        const QPointer<QObject> selected=
            MobileViewIdentityGuard::uniqueActive(views);
        // A focus/selection change while the save dialog is opening
        // must never redirect Save to a different document. Require one
        // unambiguous live view for Krita's currently selected document.
        if(!MobileViewIdentityGuard::maySave(views,selected,current))
            return false;
        auto *document=qobject_cast<KisDocument*>(current.data());
        if(!document)return false;
        // Native Krita save path retains its format handling,
        // render-completion checks, backups and Android file provider.
        return m_host->save(document,saveAs);
    }

    // A deferred UI Save must never silently target a different drawing
    // if document selection changes while a modal menu is closing.
    // Capture the selected MDI identity at click time and revalidate it
    // immediately before invoking Krita's own native save operation.
    bool saveDocumentView(const QPointer<QObject> &identity,
                          bool saveAs=false) const
    {
        auto *document=documentForView(identity);
        return document && m_host->save(document,saveAs);
    }

    bool exportDocumentView(const QPointer<QObject> &identity) const
    {
        auto *document=documentForView(identity);
        return document && m_host->exportCopy(document);
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
    KisDocument *documentForView(const QPointer<QObject> &identity) const
    {
        if(!m_host || !m_host->available() ||
           !m_viewHost || !identity)return nullptr;
        // Capture no raw KisView: the provider can replace a view while
        // Qt dispatches a deferred menu callback. A current snapshot
        // must still contain exactly this selected identity and the
        // same live KisDocument currently owned by Krita's manager.
        const auto views=m_viewHost->openViews();
        const QPointer<QObject> current=m_viewHost->currentDocument();
        auto *document=qobject_cast<KisDocument*>(current.data());
        if(!document || !MobileViewIdentityGuard::maySave(
               views,identity,current))
            return nullptr;
        return document;
    }
    QSharedPointer<MobileDocumentHostPort> m_host;
    QSharedPointer<MobileViewHostPort> m_viewHost;

};
#endif // Q_OS_ANDROID
