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
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QStandardPaths>
#include <QString>
#include <QVector>
#include <QWidget>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoFileDialog.h>

#include "KisDocument.h"
#include "KisImportExportManager.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_config.h"
#include "mdi_view_identity.h"

class MobileDocumentService final
{
public:
    MobileDocumentService(KisMainWindow *mainWindow,
                          KisViewManager *manager,
                          QMdiArea *documents)
        : m_mainWindow(mainWindow),m_manager(manager),
          m_documents(documents) {}

    struct OpenDocument {
        QString title;
        bool modified=false;
        bool active=false;
        int nativeIndex=-1;
        // Opaque guarded identity: delayed actions must never resolve an
        // old index against a different MDI ordering. Do not expose
        // Krita's document or MDI implementation to mobile UI widgets.
        QPointer<QObject> nativeView;
    };

    QVector<OpenDocument> openDocuments() const
    {
        QVector<OpenDocument> items;
        if(!m_documents)return items;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        items.reserve(windows.size());
        for(int i=0;i<windows.size();++i) {
            QMdiSubWindow *sub=windows.at(i);
            if(!sub || !sub->widget())continue;
            auto *view=qobject_cast<KisView*>(sub->widget());
            KisDocument *doc=view?view->document():nullptr;
            if(!doc)continue;
            const QString path=doc->path();
            QString title=path.isEmpty()?QStringLiteral("Untitled"):
                          QFileInfo(path).fileName();
            if(title.isEmpty())title=QStringLiteral("Untitled");
            // A transferred canvas can have no active Qt MDI subwindow
            // even while Krita's view manager still has a current document.
            // Use the engine's actual active document as a fallback.
            const KisDocument *current=m_manager?m_manager->document():nullptr;
            const bool isActive=current?doc==current:
                                        sub==m_documents->activeSubWindow();
            items.append({title,doc->isModified(),isActive,i,
                          MobileMdiViewIdentity::capture(sub)});
        }
        // If the view manager temporarily has no matching active document,
        // retain the MDI selection (or the lone open canvas) as the visual
        // current item instead of leaving every row without an active mark.
        bool hasActive=false;
        for(const OpenDocument &item:items)
            if(item.active){hasActive=true;break;}
        if(!hasActive) {
            const int mdiIndex=windows.indexOf(m_documents->activeSubWindow());
            for(OpenDocument &item:items) {
                if(item.nativeIndex==mdiIndex) {
                    item.active=true;
                    hasActive=true;
                    break;
                }
            }
            if(!hasActive && items.size()==1)
                items[0].active=true;
        }
        return items;
    }
    // Resolves against the live MDI list, never the index captured when
    // a popup opened. A destroyed view automatically invalidates QPointer.
    bool activateDocumentView(const QPointer<QObject> &identity) const
    {
        if(!m_documents || !identity)return false;
        QPointer<QMdiSubWindow> target=
            MobileMdiViewIdentity::resolve(m_documents,identity);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        m_documents->setActiveSubWindow(target);
        return true;
    }
    bool activateDocument(int nativeIndex) const
    {
        if(!m_documents || nativeIndex<0)return false;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        if(nativeIndex>=windows.size())return false;
        return activateDocumentView(windows.at(nativeIndex));
    }

    // Close through Krita's existing QMdiSubWindow/KisView path.
    // KisView::closeEvent calls queryClose() on the last view, waits for
    // active saves and offers Save / Discard / Cancel for modified files.
    // The mobile UI must not destroy KisDocument or mutate its dirty flag.
    bool closeDocumentView(const QPointer<QObject> &identity) const
    {
        if(!m_documents || !identity)return false;
        QPointer<QMdiSubWindow> target=
            MobileMdiViewIdentity::resolve(m_documents,identity);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        // Native Krita still owns Save/Discard/Cancel and the document.
        m_documents->setActiveSubWindow(target);
        // Signal handlers may remove a view during the selection change.
        return target && target->close();
    }
    bool closeDocument(int nativeIndex) const
    {
        if(!m_documents || nativeIndex<0)return false;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        if(nativeIndex>=windows.size())return false;
        return closeDocumentView(windows.at(nativeIndex));
    }
    bool closeActiveDocument() const
    {
        if(!m_documents)return false;
        // On Android the native QMdiArea is physically hosted in a second
        // top-level window. Qt can clear activeSubWindow() during reparenting
        // even though the Krita view manager still owns the active document.
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        const KisDocument *current=m_manager?m_manager->document():nullptr;
        QMdiSubWindow *active=m_documents->activeSubWindow();
        if(active && qobject_cast<KisView*>(active->widget()) &&
           (!current ||
            qobject_cast<KisView*>(active->widget())->document()==current))
            return closeDocument(windows.indexOf(active));

        // Prefer the view backed by the engine's active document; do not
        // arbitrarily close a different document when several are open.
        if(current) {
            for(int i=0;i<windows.size();++i) {
                const auto *view=windows.at(i)?
                    qobject_cast<KisView*>(windows.at(i)->widget()):nullptr;
                if(view && view->document()==current)
                    return closeDocument(i);
            }
        }

        // If the manager still refers to a just-closed document, trust an
        // existing valid active MDI subwindow rather than refusing to close
        // every other document in a multi-document session.
        if(active && windows.contains(active) &&
           qobject_cast<KisView*>(active->widget()))
            return closeDocument(windows.indexOf(active));

        // A lone valid view is unambiguous even if no MDI tab is active.
        int only=-1;
        for(int i=0;i<windows.size();++i) {
            if(!windows.at(i) ||
               !qobject_cast<KisView*>(windows.at(i)->widget()))continue;
            if(only>=0)return false;
            only=i;
        }
        return only>=0 && closeDocument(only);
    }


    // Document I/O and image construction are owned by Krita's engine.
    // The mobile shell supplies the dialogs and initiates transactions,
    // but must not implement its own native file serializer.
    bool createCanvas(const QString &name,int width,int height,
                      int resolutionDpi,bool whiteBackground) const
    {
        if(!m_mainWindow || width<1 || height<1 || resolutionDpi<1 ||
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
        m_mainWindow->addViewAndNotifyLoadingCompleted(document);
        return true;
    }
    int openFiles(QWidget *parent) const
    {
        if(!m_mainWindow || !parent)return 0;
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
            if(m_mainWindow->openDocument(path,KisMainWindow::None))
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
        if(!m_mainWindow || !m_manager || !m_manager->document())return false;
        // Native Krita save path retains its format handling,
        // render-completion checks, backups and Android file provider.
        return m_mainWindow->saveDocument(m_manager->document(),
                                    saveAs,false,false);
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
    bool hasDocument() const {
        // A stale active-document pointer can survive briefly after closing
        // the last MDI subwindow. The live native views are authoritative for
        // the mobile empty-workspace state and availability of commands.
        if(m_documents) {
            for(QMdiSubWindow *sub:m_documents->subWindowList(
                    QMdiArea::CreationOrder)) {
                if(sub && qobject_cast<KisView*>(sub->widget()) &&
                   qobject_cast<KisView*>(sub->widget())->document())
                    return true;
            }
            return false;
        }
        return m_manager && m_manager->document();
    }

private:
    QPointer<KisMainWindow> m_mainWindow;
    KisViewManager *m_manager=nullptr;  // Owned by native Krita backend
    QPointer<QMdiArea> m_documents;
};
#endif // Q_OS_ANDROID
