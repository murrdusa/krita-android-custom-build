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
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include "kis_paint_layer.h"
#include "kis_layer_properties_icons.h"
#include <KoToolManager.h>

class MobileEngineBridge final {
public:
    MobileEngineBridge(KisMainWindow *mainWindow,KisViewManager *manager)
        : m_mainWindow(mainWindow),m_manager(manager) {}

    KisMainWindow *legacyHost() const {return m_mainWindow;}
    KisViewManager *manager() const {return m_manager;}
    KisCanvasResourceProvider *brushResources() const {
        return m_manager?m_manager->canvasResourceProvider():nullptr;
    }
    QAction *action(const char *id) const {
        return m_mainWindow && m_mainWindow->actionCollection()?
            m_mainWindow->actionCollection()->action(
                QString::fromLatin1(id)):nullptr;
    }
    bool run(const char *id) const {
        if(QAction *a=action(id)) {
            if(a->isEnabled()){a->trigger();return true;}
        }
        return false;
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
    // Native commands and native undo. UI widgets never mutate the
    // node tree directly or create their own shadow layer state.
    bool isLiveNode(KisNodeSP node) const {
        return node && node->graphListener() && node->parent();
    }
    bool setNodeVisibility(KisNodeSP node,bool visible) const {
        if(!isLiveNode(node) || !m_manager)return false;
        const auto image=m_manager->image().toStrongRef();
        if(!image)return false;
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(
            node,KisLayerPropertiesIcons::visible,visible,image);
        return true;
    }
    bool setNodeLocked(KisNodeSP node,bool locked) const {
        if(!isLiveNode(node) || !m_manager)return false;
        const auto image=m_manager->image().toStrongRef();
        if(!image)return false;
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(
            node,KisLayerPropertiesIcons::locked,locked,image);
        return true;
    }
    bool setNodeAlphaLocked(KisNodeSP node,bool locked) const {
        if(!isLiveNode(node) || !m_manager ||
           !node->inherits("KisPaintLayer"))return false;
        const auto image=m_manager->image().toStrongRef();
        if(!image)return false;
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(
            node,KisLayerPropertiesIcons::alphaLocked,locked,image);
        return true;
    }
    bool renameNode(KisNodeSP node,const QString &name) const {
        if(!isLiveNode(node) || !m_manager || !m_manager->nodeManager() ||
           name.trimmed().isEmpty())return false;
        m_manager->nodeManager()->setNodeName(node,name.trimmed());
        return true;
    }
    bool setNodeOpacity(KisNodeSP node,int percent) const {
        if(!isLiveNode(node) || !m_manager || !m_manager->nodeManager())
            return false;
        m_manager->nodeManager()->setNodeOpacity(
            node,qRound(255.0*qBound(0,percent,100)/100.0));
        return true;
    }
    bool addPaintLayer() const {
        return m_manager && m_manager->nodeManager() &&
            bool(m_manager->nodeManager()->createPaintLayer());
    }
    void duplicateSelected() const {
        if(m_manager && m_manager->nodeManager())
            m_manager->nodeManager()->duplicateActiveNode();
    }
    void deleteSelected() const {
        if(m_manager && m_manager->nodeManager())
            m_manager->nodeManager()->removeNode();
    }
    void moveSelectedUp() const {
        if(m_manager && m_manager->nodeManager())
            m_manager->nodeManager()->raiseNode();
    }
    void moveSelectedDown() const {
        if(m_manager && m_manager->nodeManager())
            m_manager->nodeManager()->lowerNode();
    }
    void groupSelected(bool clipping=false) const {
        if(!m_manager || !m_manager->nodeManager())return;
        if(clipping)m_manager->nodeManager()->createQuickClippingGroup();
        else m_manager->nodeManager()->createQuickGroup();
    }
    void ungroup() const {
        if(m_manager && m_manager->nodeManager())
            m_manager->nodeManager()->quickUngroup();
    }
    KisNodeList selectedNodes() const {
        return m_manager && m_manager->nodeManager()?
            m_manager->nodeManager()->selectedNodes():KisNodeList();
    }
    void setSelectedNodes(const KisNodeList &nodes) const {
        if(!m_manager || !m_manager->nodeManager() || nodes.isEmpty())return;
        auto *manager=m_manager->nodeManager();
        // Keep current active node included in the selection when possible.
        KisNodeSP active=m_manager->activeNode();
        if(!nodes.contains(active))
            manager->slotUiActivatedNode(nodes.last());
        manager->slotSetSelectedNodes(nodes);
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
};
#endif // Q_OS_ANDROID
