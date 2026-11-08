/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * An intentionally thin bridge between the independent mobile window and
 * Krita's existing document/painting systems. No duplicate painting logic.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QPointer>
#include <QMenu>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QList>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMessageBox>
#include <brushengine/kis_paintop_preset.h>
#include <KoFileDialog.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include "KisDocument.h"
#include "KisPart.h"
#include "KisImportExportManager.h"
#include "kis_config.h"
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "KisView.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include <KoToolManager.h>

class MobileEngineBridge final {
public:
    MobileEngineBridge(KisMainWindow *mainWindow,KisViewManager *manager,
                       QMdiArea *documents=nullptr)
        : m_mainWindow(mainWindow),m_manager(manager),
          m_documents(documents) {}

    struct OpenDocument {
        QString title;
        bool modified=false;
        bool active=false;
        int nativeIndex=-1;
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
            items.append({title,doc->isModified(),
                          sub==m_documents->activeSubWindow(),i});
        }
        return items;
    }
    bool activateDocument(int nativeIndex) const
    {
        if(!m_documents || nativeIndex<0)return false;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        if(nativeIndex>=windows.size())return false;
        QMdiSubWindow *target=windows.at(nativeIndex);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        m_documents->setActiveSubWindow(target);
        return true;
    }

    // Close through Krita's existing QMdiSubWindow/KisView path.
    // KisView::closeEvent calls queryClose() on the last view, waits for
    // active saves and offers Save / Discard / Cancel for modified files.
    // The mobile UI must not destroy KisDocument or mutate its dirty flag.
    bool closeDocument(int nativeIndex) const
    {
        if(!m_documents || nativeIndex<0)return false;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        if(nativeIndex>=windows.size())return false;
        QPointer<QMdiSubWindow> target=windows.at(nativeIndex);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        // The native Save prompt belongs to this document view. Make it
        // active before querying, so Krita's save handlers target it.
        m_documents->setActiveSubWindow(target);
        return target->close();
    }
    bool closeActiveDocument() const
    {
        if(!m_documents)return false;
        QMdiSubWindow *active=m_documents->activeSubWindow();
        if(!active)return false;
        const auto windows=m_documents->subWindowList(QMdiArea::CreationOrder);
        const int index=windows.indexOf(active);
        return closeDocument(index);
    }

    KisMainWindow *legacyHost() const {return m_mainWindow;}
    KisViewManager *manager() const {return m_manager;}
    KisCanvasResourceProvider *brushResources() const {
        return m_manager?m_manager->canvasResourceProvider():nullptr;
    }
    // Only this adapter knows the temporary desktop backend. Client
    // workspaces see Krita services, not KisMainWindow or its toolbars.
    QAction *action(const QString &id) const {
        return m_mainWindow && m_mainWindow->actionCollection()?
            m_mainWindow->actionCollection()->action(id):nullptr;
    }
    QAction *action(const char *id) const {
        return action(QString::fromLatin1(id));
    }
    QList<QAction*> availableActions() const {
        if(!m_mainWindow || !m_mainWindow->actionCollection())return {};
        return m_mainWindow->actionCollection()->actions();
    }
    bool canRun(const char *id) const {
        const QAction *a=action(id);
        return a && a->isEnabled();
    }
    bool selectBrushPreset(const KisPaintOpPresetSP &preset) const {
        if(!preset || !m_manager || !m_manager->paintOpBox())return false;
        m_manager->paintOpBox()->resourceSelected(preset);
        return true;
    }
    void setEraser(bool enabled) const {
        if(QAction *eraser=action("erase_action"))
            if(eraser->isCheckable() && eraser->isChecked()!=enabled &&
               eraser->isEnabled())
                eraser->trigger();
    }
    bool run(const char *id) const {
        if(QAction *a=action(id)) {
            if(a->isEnabled()){a->trigger();return true;}
        }
        return false;
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
        return m_manager && m_manager->document();
    }
    QString activeLayerName() const {
        if(!m_manager)return {};
        KisNodeSP node=m_manager->activeNode();
        return node?node->name():QString();
    }
    bool selectLayer(KisNodeSP node) const {
        if(!node || !m_manager || !m_manager->nodeManager())return false;
        auto *nodes=m_manager->nodeManager();
        nodes->slotUiActivatedNode(node);
        nodes->slotSetSelectedNodes(KisNodeList{node});
        return true;
    }
    void freehandBrush() const {
        KoToolManager::instance()->switchToolRequested(
            QStringLiteral("KritaShape/KisToolBrush"));
    }
    QMenu *nativeMenus(QWidget *parent) const {
        auto *menu=new QMenu(parent);
        if(!m_mainWindow || !m_mainWindow->menuBar())return menu;
        for(QAction *top:m_mainWindow->menuBar()->actions())
            if(top && top->menu())menu->addMenu(top->menu());
        return menu;
    }
private:
    QPointer<KisMainWindow> m_mainWindow;
    KisViewManager *m_manager=nullptr;
    QPointer<QMdiArea> m_documents;
};
#endif // Q_OS_ANDROID
