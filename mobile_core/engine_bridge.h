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
#include <brushengine/kis_paintop_preset.h>
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
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
};
#endif // Q_OS_ANDROID
