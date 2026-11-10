/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * An intentionally thin bridge between the independent mobile window and
 * Krita's existing document/painting systems. No duplicate painting logic.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QMenu>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QList>
#include <QSharedPointer>
#include <utility>
#include <brushengine/kis_paintop_preset.h>
#include "host_services.h"
#include "document_service.h"
#include "command_service.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_node_manager.h"
#include "kis_node.h"

class MobileEngineBridge final {
public:
    // The mobile app receives only reusable Krita services and host
    // ports. No KisMainWindow type or desktop constructor is required.
    // Both ports are shared because document dialogs copy this facade
    // into deferred Qt callbacks; destroyed hosts are QPointer-guarded
    // by the concrete adapter instead of leaving dangling pointers.
    MobileEngineBridge(const MobileHostServices &services,
                       KisViewManager *manager)
        : m_manager(manager),
          m_documentsService(services.documents,services.views),
          m_commands(services.commands,manager) {}

    using OpenDocument=MobileDocumentService::OpenDocument;
    QVector<OpenDocument> openDocuments() const {
        return m_documentsService.openDocuments();
    }
    bool activateDocument(int nativeIndex) const {
        return m_documentsService.activateDocument(nativeIndex);
    }
    bool activateDocumentView(const QPointer<QObject> &view) const {
        return m_documentsService.activateDocumentView(view);
    }
    bool closeDocument(int nativeIndex) const {
        return m_documentsService.closeDocument(nativeIndex);
    }
    bool closeDocumentView(const QPointer<QObject> &view) const {
        return m_documentsService.closeDocumentView(view);
    }
    bool closeActiveDocument() const {
        return m_documentsService.closeActiveDocument();
    }

    KisViewManager *manager() const {return m_manager;}
    KisCanvasResourceProvider *brushResources() const {
        return m_manager?m_manager->canvasResourceProvider():nullptr;
    }
    // Native Krita actions, tool registry, menus and brush selection are
    // provided by one command service; this facade owns no desktop chrome.
    QAction *action(const QString &id) const { return m_commands.action(id); }
    QAction *action(const char *id) const { return m_commands.action(id); }
    QList<QAction*> availableActions() const {
        return m_commands.availableActions();
    }
    QList<QAction*> menuRootActions() const {
        return m_commands.menuRootActions();
    }
    bool canRun(const char *id) const { return m_commands.canRun(id); }
    bool selectBrushPreset(const KisPaintOpPresetSP &preset) const {
        return m_commands.selectBrushPreset(preset);
    }
    void setEraser(bool enabled) const { m_commands.setEraser(enabled); }
    bool run(const char *id) const { return m_commands.run(id); }
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
    bool saveDocumentView(const QPointer<QObject> &view,
                          bool saveAs=false) const {
        return m_documentsService.saveDocumentView(view,saveAs);
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
    using MobileTool=MobileCommandService::MobileTool;
    QVector<MobileTool> tools() const { return m_commands.tools(); }
    QString activeTool() const { return m_commands.activeTool(); }
    bool selectTool(const QString &id) const {
        // Krita's Transform tool snapshots the native selected-node
        // list *when it activates*. The global canvas selection is
        // internally a root-level KisSelectionMask; if it slipped into
        // that list while making a marquee, the transform could
        // incorrectly report "Cannot transform empty layer" even
        // though the visibly selected paint layer contains content.
        //
        // Normalize before activating Transform, not while its image
        // stroke is running. Preserve intentional multi-layer targets.
        if(m_manager && (id==QStringLiteral("KisToolTransform") ||
                         id==QStringLiteral("KritaTransform/KisToolMove"))) {
            auto *nodes=m_manager->nodeManager();
            const KisImageSP image=m_manager->image().toStrongRef();
            if(nodes && image && image->root()) {
                const auto belongsToImage=[&image](KisNodeSP node) {
                    for(KisNodeSP p=node;p;p=p->parent())
                        if(p==image->root())return true;
                    return false;
                };
                const KisNodeList existing=nodes->selectedNodes();
                KisNodeList targets;
                for(const KisNodeSP &node:existing) {
                    if(belongsToImage(node) &&
                       !KisNodeManager::isNodeHidden(node,true) &&
                       !targets.contains(node))
                        targets.append(node);
                }
                const KisNodeSP active=m_manager->activeNode();
                if(belongsToImage(active) &&
                   !KisNodeManager::isNodeHidden(active,true) &&
                   targets.size()<=1) {
                    targets=KisNodeList{active};
                }
                if(!targets.isEmpty() && targets!=existing)
                    nodes->slotSetSelectedNodes(targets);
            }
        }
        return m_commands.selectTool(id);
    }
    void freehandBrush() const { m_commands.freehandBrush(); }
    QMenu *nativeMenus(QWidget *parent) const {
        return m_commands.nativeMenus(parent);
    }
private:
    KisViewManager *m_manager=nullptr; // Native layer/resource adapter
    MobileDocumentService m_documentsService;
    MobileCommandService m_commands;
};
#endif // Q_OS_ANDROID
