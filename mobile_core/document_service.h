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
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_config.h"
#include "mdi_view_identity.h"
#include "document_host_port.h"

class MobileDocumentService final
{
public:
    MobileDocumentService(QSharedPointer<MobileDocumentHostPort> host,
                          KisViewManager *manager,
                          QMdiArea *documents)
        : m_host(std::move(host)),m_manager(manager),
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
        const KisDocument *current=m_manager?m_manager->document():nullptr;
        for(int i=0;i<windows.size();++i) {
            QMdiSubWindow *sub=windows.at(i);
            auto *view=sub?qobject_cast<KisView*>(sub->widget()):nullptr;
            KisDocument *doc=view?view->document():nullptr;
            if(!doc)continue;
            const QString path=doc->path();
            QString title=path.isEmpty()?QStringLiteral("Untitled"):
                          QFileInfo(path).fileName();
            if(title.isEmpty())title=QStringLiteral("Untitled");
            items.append({title,doc->isModified(),false,i,
                          MobileMdiViewIdentity::capture(sub)});
        }

        // A document can have multiple KisView windows. Comparing each
        // view's document to the manager's current document would label
        // several rows active and Close Current could target the wrong
        // view. Select exactly one valid view, or none if ambiguous.
        int selected=-1;
        QMdiSubWindow *mdiActive=m_documents->activeSubWindow();
        const auto *mdiView=mdiActive?
            qobject_cast<KisView*>(mdiActive->widget()):nullptr;
        if(mdiView && (!current || mdiView->document()==current)) {
            for(int i=0;i<items.size();++i) {
                if(items[i].nativeView==mdiActive) {
                    selected=i;
                    break;
                }
            }
        }

        if(selected<0 && current) {
            int candidate=-1;
            for(int i=0;i<items.size();++i) {
                const auto *sub=windows.at(items.at(i).nativeIndex);
                const auto *view=sub?
                    qobject_cast<KisView*>(sub->widget()):nullptr;
                if(view && view->document()==current) {
                    if(candidate>=0) {
                        // Multiple views of the same document but no
                        // reliable active MDI selection: refuse to guess.
                        candidate=-1;
                        break;
                    }
                    candidate=i;
                }
            }
            selected=candidate;
        }

        // The lone view is unambiguous even when the view manager has
        // briefly cleared its active document during a Qt reparent.
        if(selected<0 && items.size()==1)selected=0;
        if(selected>=0)items[selected].active=true;
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
        if(!m_host || !m_host->available() || !m_documents ||
           !m_manager || !identity)return false;
        const auto items=openDocuments();
        int activeCount=0;
        bool identityWasCurrent=false;
        for(const auto &item:items) {
            if(!item.active)continue;
            ++activeCount;
            identityWasCurrent=item.nativeView==identity;
        }
        if(activeCount!=1 || !identityWasCurrent)return false;
        const auto sub=MobileMdiViewIdentity::resolve(m_documents,identity);
        const auto *view=sub?qobject_cast<KisView*>(sub->widget()):nullptr;
        KisDocument *document=view?view->document():nullptr;
        if(!document || document!=m_manager->document())return false;
        return m_host->save(document,saveAs);
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
    QSharedPointer<MobileDocumentHostPort> m_host;
    KisViewManager *m_manager=nullptr;  // Owned by native Krita backend
    QPointer<QMdiArea> m_documents;
};
#endif // Q_OS_ANDROID
