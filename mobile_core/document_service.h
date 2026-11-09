/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Krita-backed document and native canvas-view lifecycle service.
 * Mobile UI never constructs KisMainWindow or QMdiArea. This adapter
 * owns the final, explicit dependency on those legacy operations:
 * creation, open, safe save, active view selection and close.
 *
 * Moving from a legacy host to a future lightweight Krita engine host
 * should require replacing this service rather than changing mobile UI.
 */
#pragma once
#ifdef Q_OS_ANDROID

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
#include "KisViewManager.h"
#include "kis_config.h"
#include "document_host_port.h"
#include "view_host_port.h"
#include "view_identity_guard.h"

class MobileDocumentService final
{
public:
    MobileDocumentService(QSharedPointer<MobileDocumentHostPort> host,
                          QSharedPointer<MobileViewHostPort> viewHost,
                          KisViewManager *manager)
        : m_host(std::move(host)),m_viewHost(std::move(viewHost)),
          m_manager(manager) {}

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
        const QPointer<QObject> current=m_manager?
            QPointer<QObject>(m_manager->document()):QPointer<QObject>();
        return closeDocumentView(
            MobileViewIdentityGuard::safeCloseTarget(views,current));
    }

    // Document I/O and image construction are owned by Krita's engine.
    // The mobile shell supplies the dialogs and initiates transactions,
    // but must not implement its own native file serializer.
    bool createCanvas(const QString &name,int width,int height,
                      int resolutionDpi,bool whiteBackground) const
    {
        if(!m_host || !m_host->available() ||
           width<1 || height<1 || resolutionDpi<1 ||
           qint64(width)*height>40000000LL)return false;
        const KoColorSpace *space=KoColorSpaceRegistry::instance()->rgb8();
        if(!space)return false;
        KisDocument *document=KisPart::instance()->createDocument();
        if(!document)return false;
        const KoColor background(
            whiteBackground?QColor(Qt::white):QColor(Qt::transparent),space);
        const bool created=document->newImage(
            name.trimmed().isEmpty()?QStringLiteral("Unnamed"):name.trimmed(),
            width,height,space,background,
            whiteBackground?KisConfig::RASTER_LAYER:KisConfig::CANVAS_COLOR,
            whiteBackground?2:1,QString(),qreal(resolutionDpi)/72.0);
        if(!created) {
            delete document;
            return false;
        }
        KisPart::instance()->addDocument(document);
        // Only the host port may attach a Krita native document view.
        // Never substitute a detached mobile-owned KisView here.
        return m_host->attachNewDocument(document);
    }
    int openFiles(QWidget *parent) const
    {
        if(!m_host || !m_host->available() || !parent)return 0;
        KoFileDialog dialog(parent,KoFileDialog::ImportFiles,
                            QStringLiteral("MobileOpenDocument"));
        dialog.setDefaultDir(QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation));
        dialog.setMimeTypeFilters(
            KisImportExportManager::supportedMimeTypes(
                KisImportExportManager::Import));
        dialog.setCaption(QObject::tr("Open drawings"));
        QStringList paths=dialog.filenames();
        paths.sort();
        int successful=0;
        for(const QString &path:paths) {
            if(path.isEmpty())continue;
            if(m_host->openFile(path))
                ++successful;
            else
                QMessageBox::warning(parent,QObject::tr("Open failed"),
                    QObject::tr("Could not open %1").arg(
                        QFileInfo(path).fileName()));
        }
        return successful;
    }
    bool saveCurrentDocument(bool saveAs=false) const
    {
        if(!m_host || !m_host->available() ||
           !m_manager || !m_manager->document())return false;
        // Native Krita save path retains its format handling,
        // render-completion checks, backups and Android file provider.
        return m_host->save(m_manager->document(),saveAs);
    }

    // A deferred UI Save must never silently target a different drawing
    // if document selection changes while a modal menu is closing.
    // Capture the selected MDI identity at click time and revalidate it
    // immediately before invoking Krita's own native save operation.
    bool saveDocumentView(const QPointer<QObject> &identity,
                          bool saveAs=false) const
    {
        if(!m_host || !m_host->available() || !m_manager ||
           !m_viewHost || !identity)return false;
        // Capture no raw KisView: the provider can replace a view while
        // Qt dispatches a deferred menu callback. A current snapshot
        // must still contain exactly this selected identity and the
        // same live KisDocument currently owned by Krita's manager.
        const auto views=m_viewHost->openViews();
        if(!MobileViewIdentityGuard::maySave(
               views,identity,
               QPointer<QObject>(m_manager->document())))
            return false;
        return m_host->save(m_manager->document(),saveAs);
    }
    QString activeDocumentTitle() const
    {
        if(!m_manager || !m_manager->document())return {};
        const KisDocument *document=m_manager->document();
        const QString path=document->path();
        return path.isEmpty()?QStringLiteral("Untitled"):
            QFileInfo(path).fileName();
    }
    bool documentModified() const
    {
        return m_manager && m_manager->document() &&
               m_manager->document()->isModified();
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
        return m_manager && m_manager->document();
    }

private:
    QSharedPointer<MobileDocumentHostPort> m_host;
    QSharedPointer<MobileViewHostPort> m_viewHost;
    KisViewManager *m_manager=nullptr; // Owned by native Krita backend
};
#endif // Q_OS_ANDROID
