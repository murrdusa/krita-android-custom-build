/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Production Android drawing session. Uses Krita's real engine and
 * native document services; the desktop launch-mode selector is gone.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QPointer>
#include <QSharedPointer>
#include <QSettings>
#include <functional>
#include <initializer_list>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include "document_service.h"
#include "android_document_intents.h"
#include "deferred_view_command.h"
#include "document_navigation.h"
#include "document_workflow.h"
#include "document_switcher.h"
#include "krita_engine_session.h"
#include "krita_native_workspace.h"
#include "native_command_layout.h"
#include "native_bootstrap_audit.h"
#include "engine_only_root.h"

class MobileKritaStandalonePilot final
{
public:
    static bool launch(QWidget *nativeWindow,
                       std::function<void(bool)> firstCanvasReady={})
    {
        if(!nativeWindow || nativeWindow->window()!=nativeWindow ||
           !nativeWindow->isVisible())return false;
        // Opt-in V3 uses this painting host as the actual Android root.
        // Do not reparent it into itself or create a second QMainWindow.
        auto *directRoot=dynamic_cast<MobileLightweightCanvasHost*>(
            nativeWindow);
        if(directRoot && directRoot->nativeViewPort()) {
            const bool ready=!directRoot->nativeViewPort()->
                openViews().isEmpty();
            if(firstCanvasReady)firstCanvasReady(ready);
            return ready;
        }
        // Android forbids the desktop-style second native window used
        // by the first test. A second instance instead focuses the
        // existing CHILD overlay; the engine and document views are
        // still independent of Krita's MDI.
        if(auto *existing=nativeWindow->findChild<MobileLightweightCanvasHost*>(
               QStringLiteral("_mobileStandaloneEnginePilot"),
               Qt::FindDirectChildrenOnly)) {
            existing->show();
            existing->raise();
            existing->setFocus(Qt::OtherFocusReason);
            if(firstCanvasReady)firstCanvasReady(existing->nativeViewPort() &&
                         !existing->nativeViewPort()->openViews().isEmpty());
            return true;
        }

        // Persist the last startup phase and emit logcat checkpoints.
        auto stage=[](const char *name,bool pending=true) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/standaloneLastPhase"),
                QString::fromLatin1(name));
            settings.setValue(
                QStringLiteral("MobileStudio/standaloneLaunchPending"),pending);
            settings.sync();
            qWarning() << "Standalone engine checkpoint:" << name;
        };
        stage("Creating native drawing host");
        auto *window=directRoot ? directRoot :
            new MobileLightweightCanvasHost;
        if(!directRoot && !window->embedInExistingWindow(nativeWindow)) {
            delete window;
            return false;
        }
        if(!directRoot) {
            window->setObjectName(
                QStringLiteral("_mobileStandaloneEnginePilot"));
            window->setWindowTitle(
                QObject::tr("Krita native engine test"));
        }
        window->setAttribute(Qt::WA_DeleteOnClose,true);
        // In V2 the parent is a plain QMainWindow, not KisMainWindow.
        // Android keeps a single native top-level in both boot modes.
        if(auto *independent=dynamic_cast<MobileEngineOnlyRoot*>(nativeWindow))
            independent->setDrawingHost(window);
        stage("Creating independent Krita engine session");
        MobileKritaEngineSession *engineSession=
            MobileKritaEngineSession::attach(window);
        if(!engineSession) {
            delete window;
            return false;
        }
        // KisStatusBar::setup() still creates the Qt footer for engine
        // bookkeeping. Keep it live but hide desktop presentation.
        if(!window->suppressNativeStatusChrome())
            qWarning() << "Independent status footer was not found";
        stage("Native Krita services connected");
        const MobileHostServices services=engineSession->services();
        auto documents=QSharedPointer<MobileDocumentService>::create(
            services.documents,services.views);
        QPointer<MobileLightweightCanvasHost> alive=window;
        QPointer<QWidget> desktopNative=nativeWindow;
        const MobileEngineBridge nativeBridge(services,engineSession->viewManager());
        // Native controls now use the independent manager and host ports,
        // not the old desktop MDI window or its docker instances.
        auto *workspace=new MobileKritaNativeWorkspace(
            window,services,engineSession->viewManager());

        // Reuse every real QAction callback from the validated engine,
        // but present the controls in short native rows, not one
        // overflowing desktop-style toolbar.
        auto makeAction=[window](const char *label) {
            return new QAction(QObject::tr(label),window);
        };
        auto *newAction=makeAction("New");
        auto *openAction=makeAction("Open");
        auto *canvasesAction=makeAction("Canvases");
        auto *previousAction=makeAction("Previous canvas");
        auto *nextAction=makeAction("Next canvas");
        auto *undoAction=makeAction("Undo");
        auto *redoAction=makeAction("Redo");
        auto *saveAction=makeAction("Save");
        auto *saveAsAction=makeAction("Save As");
        auto *closeAction=makeAction("Close view");
        auto *brushesAction=makeAction("Brushes");
        auto *layersAction=makeAction("Layers");
        auto *colorAction=makeAction("Color");
        auto *toolsAction=makeAction("Tools");
        auto *optionsAction=makeAction("Tool Options");
        auto *commandsAction=makeAction("All Krita Commands");
        auto *exitAction=makeAction("Close app");
        QObject::connect(brushesAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes();});
        QObject::connect(layersAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleLayers();});
        QObject::connect(colorAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleColor();});
        QObject::connect(toolsAction,&QAction::triggered,window,
                         [workspace]() {workspace->chooseTool();});
        QObject::connect(optionsAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleToolOptions();});
        QObject::connect(commandsAction,&QAction::triggered,window,
                         [workspace]() {workspace->browseCommands();});
        // Krita's selection manager already provides these native,
        // undoable actions. Expose them directly rather than inventing
        // mobile selection commands or simulating keyboard shortcuts.
        auto *selectionMenu=new QMenu(QObject::tr("Selection"),window);
        for(const char *name : {"select_all","deselect","reselect",
                                "invert_selection","edit_selection",
                                "convert_to_vector_selection",
                                "convert_to_raster_selection",
                                "selectopaque","selectopaque_add",
                                "selectopaque_subtract",
                                "selectopaque_intersect",
                                "colorrange","featherselection",
                                "growselection","shrinkselection",
                                "borderselection","smoothselection"}) {
            if(QAction *action=nativeBridge.action(name))
                selectionMenu->addAction(action);
        }
        // General drawing commands unavailable in the trimmed mobile
        // toolbar remain usable through Krita's real QAction registry.
        // A selected operation stays owned by Krita (including document
        // validation, undo and tool-specific prompts); no painting logic
        // or second command history is implemented here.
        auto *drawingMenu=new QMenu(QObject::tr("Drawing"),window);
        const auto addDrawingGroup=[&nativeBridge,drawingMenu](
            const char *heading,std::initializer_list<const char*> ids) {
            auto *group=new QMenu(QObject::tr(heading),drawingMenu);
            for(const char *id:ids) {
                if(QAction *native=nativeBridge.action(id))
                    group->addAction(native);
            }
            if(group->isEmpty())delete group;
            else drawingMenu->addMenu(group);
        };
        addDrawingGroup("Fill and stroke",{
            "fill_selection_foreground_color",
            "fill_selection_background_color",
            "fill_selection_pattern",
            "stroke_selection",
            "stroke_shapes"
        });
        addDrawingGroup("Brush smoothing",{
            "set_no_brush_smoothing",
            "set_simple_brush_smoothing",
            "set_weighted_brush_smoothing",
            "set_stabilizer_brush_smoothing",
            "set_pixel_perfect_smoothing"
        });
        addDrawingGroup("Brush symmetry",{
            "hmirror_action",
            "vmirror_action"
        });
        addDrawingGroup("Clipboard and canvas edits",{
            "edit_cut",
            "edit_copy",
            "copy_merged",
            "edit_paste",
            "paste_at",
            "paste_into",
            "clear",
            "cut_selection_to_new_layer",
            "copy_selection_to_new_layer"
        });
        addDrawingGroup("Assistants and guides",{
            "view_toggle_painting_assistants",
            "view_toggle_assistant_previews",
            "view_show_guides",
            "view_lock_guides",
            "view_grid",
            "view_pixel_grid"
        });
        addDrawingGroup("Snap to",{
            "view_snap_to_guides",
            "view_snap_to_grid",
            "view_snap_to_pixel",
            "view_snap_orthogonal",
            "view_snap_node",
            "view_snap_extension",
            "view_snap_intersection",
            "view_snap_bounding_box",
            "view_snap_image_bounds",
            "view_snap_image_center"
        });
        // These commands manipulate Krita's current view/document,
        // never the mobile QWidget or its GL surface directly.
        // Actions absent from the standalone native registry are
        // intentionally hidden rather than replaced by shortcuts.
        addDrawingGroup("Canvas view",{
            "view_zoom_in",
            "view_zoom_out",
            "zoom_to_100pct",
            "zoom_to_fit",
            "rotate_canvas_left",
            "rotate_canvas_right",
            "reset_canvas_rotation",
            "mirror_canvas",
            "reset_display"
        });
        addDrawingGroup("Image and layer transforms",{
            "imagesize",
            "canvassize",
            "rotateimage",
            "rotateImageCW90",
            "rotateImageCCW90",
            "mirrorImageHorizontal",
            "mirrorImageVertical",
            "layersize",
            "rotatelayer",
            "rotateLayerCW90",
            "rotateLayerCCW90",
            "mirrorNodeX",
            "mirrorNodeY",
            "offsetlayer"
        });
        QList<QAction*> moreActions{
            newAction,openAction,previousAction,nextAction,
            saveAsAction,optionsAction,commandsAction
        };
        if(!selectionMenu->isEmpty())
            moreActions.append(selectionMenu->menuAction());
        if(!drawingMenu->isEmpty())
            moreActions.append(drawingMenu->menuAction());
        moreActions.append(closeAction);
        moreActions.append(exitAction);
        // Avoid an overflow chevron hiding the primary drawing tools.
        // Less-common operations stay in an explicit, labeled menu.
        if(!MobileNativeCommandLayout::install(
            window,
            {canvasesAction,undoAction,redoAction,saveAction},
            {brushesAction,layersAction,colorAction,toolsAction},
            moreActions)) {
            qWarning() << "Independent Krita command layout unavailable";
        }

        QObject::connect(newAction,&QAction::triggered,window,
                         [alive,nativeBridge]() {
            // New canvas now uses the same configurable native document
            // workflow as the earlier mobile UI: actual Krita documents,
            // configurable dimensions, no second serializer.
            if(!alive)return;
            QTimer::singleShot(0,alive.data(),[alive,nativeBridge]() {
                if(alive)MobileDocumentWorkflow::create(
                    nativeBridge,alive.data());
            });
        });
        QObject::connect(canvasesAction,&QAction::triggered,window,
                         [alive,nativeBridge]() {
            if(!alive)return;
            QTimer::singleShot(0,alive.data(),[alive,nativeBridge]() {
                if(alive)MobileDocumentSwitcher::open(
                    nativeBridge,alive.data());
            });
        });
        QObject::connect(openAction,&QAction::triggered,window,
                         [alive,documents]() {
            // Android native file pickers should not appear while Qt
            // is still dispatching the initiating toolbar action.
            if(!alive)return;
            QTimer::singleShot(0,alive.data(),[alive,documents]() {
                if(alive)documents->openFiles(alive.data());
            });
        });
        // Independent host must be able to switch among real KisViews
        // without using the desktop MDI tab index.
        QObject::connect(previousAction,&QAction::triggered,window,
                         [views=services.views]() {
            MobileDocumentNavigation::cycle(views,-1);
        });
        QObject::connect(nextAction,&QAction::triggered,window,
                         [views=services.views]() {
            MobileDocumentNavigation::cycle(views,1);
        });
        // Exercise Krita's own undo stack, never a mobile shadow
        // history. Defer outside the toolbar callback, just like Save,
        // and require the SAME active canvas/document before dispatch:
        // switching documents must never undo another drawing.
        auto history=[alive,views=services.views](bool redo) {
            if(!alive || !views)return;
            MobileDeferredViewCommand::schedule(
                alive.data(),views,
                [views,redo](const QPointer<QObject> &selected) {
                    if(!views)return;
                    const auto current=views->currentDocument();
                    if(!MobileViewIdentityGuard::maySave(
                           views->openViews(),selected,current))return;
                    auto *doc=qobject_cast<KisDocument*>(current.data());
                    if(!doc || !doc->undoStack())return;
                    if(redo)doc->undoStack()->redo();
                    else doc->undoStack()->undo();
                });
        };
        QObject::connect(undoAction,&QAction::triggered,window,
                         [history](){history(false);});
        QObject::connect(redoAction,&QAction::triggered,window,
                         [history](){history(true);});
        // Qt timer dispatch prevents native Android dialog stacking.
        // Capture both view and document before deferring, and recheck
        // identity before Save / Save As / Close can touch artwork.
        auto deferViewCommand=[alive,views=services.views](
            MobileDeferredViewCommand::Command command) {
            return alive && MobileDeferredViewCommand::schedule(
                alive.data(),views,std::move(command));
        };
        QObject::connect(saveAction,&QAction::triggered,window,
                         [alive,documents,deferViewCommand]() {
            deferViewCommand([alive,documents](
                    const QPointer<QObject> &selected) {
                if(alive && !documents->saveDocumentView(selected))
                    qWarning() << "Independent Krita Save canceled or rejected";
            });
        });
        QObject::connect(saveAsAction,&QAction::triggered,window,
                         [alive,documents,deferViewCommand]() {
            deferViewCommand([alive,documents](
                    const QPointer<QObject> &selected) {
                if(alive && !documents->saveDocumentView(selected,true))
                    qWarning() << "Independent Krita Save As canceled or rejected";
            });
        });
        QObject::connect(closeAction,&QAction::triggered,window,
                         [alive,documents,deferViewCommand]() {
            deferViewCommand([alive,documents](
                    const QPointer<QObject> &selected) {
                if(alive && !documents->closeDocumentView(selected))
                    qWarning() << "Independent Krita close canceled or rejected";
            });
        });

        // Close uses the existing Krita Save/Discard/Cancel transaction.
        QPointer<QWidget> topLevel=nativeWindow;
        QObject::connect(exitAction,&QAction::triggered,window,
                         [alive,topLevel]() {
            if(!alive)return;
            QTimer::singleShot(0,alive.data(),[alive,topLevel]() {
                if(!alive)return;
                // In engine-only mode there is no desktop to return to.
                // Close the SINGLE native top-level; its closeEvent
                // enforces the native Save/Discard/Cancel transaction.
                if(topLevel && topLevel->property(
                       "_mobileNativeEngineOnlyRoot").toBool())
                    topLevel->close();
                else
                    alive->close();
            });
        });

        // The real KisCanvas2 must become visible before Qt initializes
        // brush resources and its OpenGL context. Both the initial canvas
        // and later documents use the same native KisPart engine path.
        stage("Showing native drawing canvas");
        window->show();
        window->raise();
        window->suppressNativeStatusChrome();
        QTimer::singleShot(0,window,
                           [alive,documents,stage,firstCanvasReady,
                            desktopNative,intentHost=services.documents]() {
            if(!alive)return;
            stage("Creating first native Krita document and view");
            // Cold-start Android file intents open before a blank document
            // is created. Shares/reopens do not accumulate untitled tabs.
            const int opened=MobileAndroidDocumentIntents::attachAndOpen(
                alive.data(),intentHost);
            if(opened==0 && !documents->createCanvas(
                       QStringLiteral("Untitled"),512,512,72,true)) {
                stage("Krita refused first native document creation");
                if(firstCanvasReady)firstCanvasReady(false);
                qCritical() << "Mobile Studio: first document creation failed";
                return;
            }
            // KisStatusBar::setView() attaches the zoom control once
            // the first native canvas is active; keep footer hidden.
            alive->suppressNativeStatusChrome();
            stage("First native Krita canvas ready",false);
            if(auto *desktop=qobject_cast<QMainWindow*>(desktopNative.data()))
                MobileNativeBootstrapAudit::record(desktop);
            if(firstCanvasReady)firstCanvasReady(true);
            // The Qt Android top-level was exposed before its first
            // QOpenGLWidget existed. On some phones that leaves the
            // initial SurfaceView composition grey until the Activity
            // is backgrounded/restored. An update() or GL projection
            // refresh alone did not resolve it on the Galaxy Z Fold.
            // Re-expose this ONE already-initialized top-level once,
            // after the first document has attached, to follow the
            // same native surface lifecycle as the proven resume path.
            // No retry timer, no alternate UI mode, no extra window.
            if(alive->property("_mobileNativeEngineOnlyRoot").toBool()) {
                QPointer<MobileLightweightCanvasHost> direct=alive;
                QTimer::singleShot(0,alive.data(),[direct]() {
                    if(!direct || !direct->isVisible())return;
                    qInfo() << "Mobile Studio: first document native surface refresh";
                    direct->hide();
                    QTimer::singleShot(50,direct.data(),[direct]() {
                        if(!direct)return;
                        direct->show();
                        direct->raise();
                        direct->suppressNativeStatusChrome();
                        if(QWidget *canvas=direct->nativeCanvas())
                            canvas->update();
                    });
                });
            }
        });
        return true;
    }
};
#endif
