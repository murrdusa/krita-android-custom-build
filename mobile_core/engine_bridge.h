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
#include <brushengine/kis_paintop_preset.h>
#include <KoColor.h>
#include "document_service.h"
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include <KoToolManager.h>
#include <algorithm>

class MobileEngineBridge final {
public:
    MobileEngineBridge(KisMainWindow *mainWindow,KisViewManager *manager,
                       QMdiArea *documents=nullptr)
        : m_mainWindow(mainWindow),m_manager(manager),
          m_documentsService(mainWindow,manager,documents) {}

    using OpenDocument=MobileDocumentService::OpenDocument;
    QVector<OpenDocument> openDocuments() const {
        return m_documentsService.openDocuments();
    }
    bool activateDocument(int nativeIndex) const {
        return m_documentsService.activateDocument(nativeIndex);
    }
    bool closeDocument(int nativeIndex) const {
        return m_documentsService.closeDocument(nativeIndex);
    }
    bool closeActiveDocument() const {
        return m_documentsService.closeActiveDocument();
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
    // Native menu actions are accessed only through the adapter. The
    // mobile menu browser can preserve Krita's real File/Edit/View/etc.
    // hierarchy without referencing the hidden KisMainWindow itself.
    QList<QAction*> menuRootActions() const {
        if(!m_mainWindow || !m_mainWindow->menuBar())return {};
        return m_mainWindow->menuBar()->actions();
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
    // Document lifecycle, native view selection, file format imports and
    // safe save semantics have one owner: MobileDocumentService.
    bool createCanvas(const QString &name,int width,int height,
                      int resolutionDpi,bool whiteBackground) const {
        return m_documentsService.createCanvas(name,width,height,
                                               resolutionDpi,whiteBackground);
    }
    int openFiles(QWidget *parent) const {
        return m_documentsService.openFiles(parent);
    }
    bool saveCurrentDocument(bool saveAs=false) const {
        return m_documentsService.saveCurrentDocument(saveAs);
    }
    QString activeDocumentTitle() const {
        return m_documentsService.activeDocumentTitle();
    }
    bool documentModified() const {
        return m_documentsService.documentModified();
    }
    bool hasDocument() const {
        return m_documentsService.hasDocument();
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
    struct MobileTool {
        QString id;
        QString label;
        QString description;
        QString section;
        QString iconName;
        int priority=0;
    };
    QVector<MobileTool> tools() const
    {
        QVector<MobileTool> result;
        if(!hasDocument())return result;
        KoToolManager *manager=KoToolManager::instance();
        if(!manager)return result;
        for(KoToolAction *action:manager->toolActionList()) {
            if(!action || action->id().isEmpty())continue;
            const QString label=action->iconText().trimmed().isEmpty()?
                action->toolTip():action->iconText();
            result.append({action->id(),label,action->toolTip(),
                           action->section(),action->iconName(),
                           action->priority()});
        }
        std::stable_sort(result.begin(),result.end(),
            [](const MobileTool &a,const MobileTool &b) {
                if(a.section!=b.section)
                    return a.section.localeAwareCompare(b.section)<0;
                if(a.priority!=b.priority)return a.priority<b.priority;
                return a.label.localeAwareCompare(b.label)<0;
            });
        return result;
    }
    QString activeTool() const
    {
        KoToolManager *manager=KoToolManager::instance();
        return manager?manager->activeToolId():QString();
    }
    bool selectTool(const QString &id) const
    {
        if(!hasDocument() || id.isEmpty())return false;
        KoToolManager *manager=KoToolManager::instance();
        if(!manager)return false;
        const auto actions=manager->toolActionList();
        for(KoToolAction *action:actions) {
            if(action && action->id()==id) {
                action->trigger();
                return true;
            }
        }
        return false;
    }
    void freehandBrush() const {
        selectTool(QStringLiteral("KritaShape/KisToolBrush"));
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
    MobileDocumentService m_documentsService;
};
#endif // Q_OS_ANDROID
