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
#include <QDialog>
#include <QDialogButtonBox>
#include <QMap>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSharedPointer>
#include <QSettings>
#include <functional>
#include <initializer_list>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "document_service.h"
#include "document_home.h"
#include "android_document_intents.h"
#include "deferred_view_command.h"
#include "document_workflow.h"
#include "krita_engine_session.h"
#include "krita_native_workspace.h"
#include "native_command_layout.h"
#include <filter/kis_filter.h>
#include <filter/kis_filter_registry.h>

class MobileKritaStandalonePilot final
{
public:
    static bool launch(MobileLightweightCanvasHost *window,
                       std::function<void(bool)> firstCanvasReady={})
    {
        if(!window || !window->isWindow() || !window->isVisible() ||
           window->nativeViewPort())return false;

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
        auto *homeAction=makeAction("Home");
        auto *shortcutSlot=makeAction("Tool Options");
        auto *undoAction=makeAction("Undo");
        auto *redoAction=makeAction("Redo");
        auto *saveAction=makeAction("Save");
        auto *saveAsAction=makeAction("Save As");
        auto *exportAction=makeAction("Export Copy…");
        auto *versionsAction=makeAction("Saved versions…");
        auto *closeAction=makeAction("Close view");
        auto *brushesAction=makeAction("Paint");
        auto *eraserAction=makeAction("Erase");
        auto *blendBrushAction=makeAction("Blend");
        auto *layersAction=makeAction("Layers");
        auto *colorAction=makeAction("Color");
        auto *toolsAction=makeAction("Tools");
        auto *optionsAction=makeAction("Tool Settings");
        auto *colorOptionsAction=makeAction("Color options…");
        auto *commandsAction=makeAction("All Krita Commands");
        commandsAction->setObjectName(QStringLiteral("mobileBrowseCommands"));
        auto *settingsAction=makeAction("Workspace Settings");
        auto *inputAction=makeAction("Touch, Pressure & Buttons");
        const auto studioIcon=[](const char *name) {
            return QIcon(QStringLiteral(":/light_%1.svg").arg(QString::fromLatin1(name)));
        };
        const auto setStudioIcon=[&studioIcon](QAction *action,const char *name) {
            action->setIcon(studioIcon(name));
        };
        setStudioIcon(homeAction,"go-home");
        setStudioIcon(undoAction,"edit-undo");
        setStudioIcon(redoAction,"edit-redo");
        setStudioIcon(saveAction,"document-save");
        setStudioIcon(brushesAction,"krita_tool_freehand");
        setStudioIcon(layersAction,"all-layers");
        setStudioIcon(toolsAction,"tools-wizard");
        setStudioIcon(eraserAction,"draw-eraser");
        setStudioIcon(blendBrushAction,"colorsmudge");
        if(auto *provider=nativeBridge.brushResources()) {
            const auto updateSwatch=[colorAction](const KoColor &foreground) {
                QPixmap swatch(46,46);
                swatch.fill(Qt::transparent);
                QPainter paint(&swatch);
                paint.setRenderHint(QPainter::Antialiasing);
                paint.setPen(QPen(QColor(QStringLiteral("#e2e2e7")),2));
                paint.setBrush(foreground.toQColor());
                paint.drawEllipse(QRectF(4,4,38,38));
                paint.end();
                colorAction->setIcon(QIcon(swatch));
            };
            updateSwatch(provider->fgColor());
            QObject::connect(provider,&KisCanvasResourceProvider::sigFGColorChanged,
                             colorAction,updateSwatch);
        }
        auto *canvasOnlyAction=makeAction("Canvas Only");
        canvasOnlyAction->setCheckable(true);
        if(auto *collection=engineSession->viewManager()->actionCollection()) {
            collection->addAction(QStringLiteral("mobile_gesture_undo"),undoAction);
            collection->addAction(QStringLiteral("mobile_gesture_redo"),redoAction);
            collection->addAction(QStringLiteral("mobile_canvas_only"),canvasOnlyAction);
        }
        auto *cloneSourceAction=makeAction("Set Clone Source");
        cloneSourceAction->setIcon(studioIcon("krita_tool_color_sampler"));
        QObject::connect(cloneSourceAction,&QAction::triggered,workspace,[workspace]() {
            workspace->pickCloneSource();
        });
        workspace->configureToolbarShortcut(shortcutSlot,{
            {QStringLiteral("options"),optionsAction,studioIcon("configure")},
            {QStringLiteral("brush-editor"),nativeBridge.action("show_brush_editor"),
             studioIcon("paintop_settings_02")},
            {QStringLiteral("clone-source"),cloneSourceAction,cloneSourceAction->icon()},
            {QStringLiteral("fit"),nativeBridge.action("zoom_to_fit"),studioIcon("zoom-fit")},
            {QStringLiteral("mirror"),nativeBridge.action("mirror_canvas"),studioIcon("mirror-view")},
            {QStringLiteral("assistants"),nativeBridge.action("view_toggle_painting_assistants"),
             studioIcon("krita_tool_assistant")},
            {QStringLiteral("save-as"),saveAsAction,studioIcon("document-save-as")},
            {QStringLiteral("canvas-only"),canvasOnlyAction,studioIcon("view-fullscreen")}
        });
        auto *exitAction=makeAction("Close app");
        QObject::connect(canvasOnlyAction,&QAction::toggled,workspace,
                         &MobileKritaNativeWorkspace::setCanvasOnly);
        workspace->bindBrushModes(brushesAction,eraserAction,blendBrushAction);
        workspace->bindPanelActions(layersAction,colorAction,optionsAction);
        QObject::connect(brushesAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes(0);});
        QObject::connect(eraserAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes(1);});
        QObject::connect(blendBrushAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes(2);});
        QObject::connect(layersAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleLayers();});
        QObject::connect(colorAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleColor();});
        QObject::connect(toolsAction,&QAction::triggered,window,
                         [workspace]() {workspace->chooseTool();});
        QObject::connect(optionsAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleToolSettings();});
        QObject::connect(colorOptionsAction,&QAction::triggered,window,[workspace]() {
            QTimer::singleShot(0,workspace,[workspace]() {workspace->openColorOptions();});
        });
        QObject::connect(commandsAction,&QAction::triggered,window,
                         [workspace]() {workspace->browseCommands();});
        QObject::connect(settingsAction,&QAction::triggered,window,
                         [workspace]() {workspace->openSettings();});
        QObject::connect(inputAction,&QAction::triggered,window,
                         [workspace]() {workspace->openInputSettings();});
        workspace->bindVolumeHistory(undoAction,redoAction);
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
        auto *fillResourcesMenu=new QMenu(QObject::tr("Gradients and Patterns"),drawingMenu);
        for(bool gradient:{true,false}) {
            auto *action=fillResourcesMenu->addAction(
                gradient?QObject::tr("Choose gradient…"):QObject::tr("Choose pattern…"));
            QObject::connect(action,&QAction::triggered,workspace,[workspace,gradient]() {
                workspace->chooseFillResource(gradient);
            });
        }
        drawingMenu->addMenu(fillResourcesMenu);
        drawingMenu->addAction(cloneSourceAction);
        if(auto *editor=nativeBridge.action("show_brush_editor"))
            drawingMenu->addAction(editor);
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
        auto *viewMenu=new QMenu(QObject::tr("Canvas View"),window);
        for(const char *id:{
            "view_zoom_in",
            "view_zoom_out",
            "zoom_to_100pct",
            "zoom_to_fit",
            "rotate_canvas_left",
            "rotate_canvas_right",
            "reset_canvas_rotation",
            "mirror_canvas",
            "reset_display"}) {
            if(QAction *native=nativeBridge.action(id))
                viewMenu->addAction(native);
        }
        auto *wrapMenu=new QMenu(QObject::tr("Seamless Tiling"),viewMenu);
        for(const char *id:{"wrap_around_mode","wrap_around_hv_axis",
                            "wrap_around_h_axis","wrap_around_v_axis"}) {
            if(QAction *native=nativeBridge.action(id))wrapMenu->addAction(native);
        }
        if(!wrapMenu->isEmpty())viewMenu->addMenu(wrapMenu);

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
        auto *filtersMenu=new QMenu(QObject::tr("Filters"),window);
        for(const char *id:{"filter_apply_again","filter_apply_reprompt"}) {
            if(QAction *native=nativeBridge.action(id))filtersMenu->addAction(native);
        }
        QMap<QString,QMenu*> filterGroups;
        auto *filterRegistry=KisFilterRegistry::instance();
        QStringList filterIds=filterRegistry->keys();
        filterIds.sort();
        for(const QString &id:filterIds) {
            const auto filter=filterRegistry->value(id);
            QAction *native=nativeBridge.action(QStringLiteral("krita_filter_")+id);
            if(!filter || !native)continue;
            const QString name=filter->menuCategory().name();
            auto *&group=filterGroups[name];
            if(!group)group=new QMenu(name,filtersMenu);
            group->addAction(native);
        }
        if(!filterGroups.isEmpty() && !filtersMenu->isEmpty())filtersMenu->addSeparator();
        for(auto *group:filterGroups)filtersMenu->addMenu(group);

        auto *documentMenu=new QMenu(QObject::tr("Documents"),window);
        documentMenu->addActions({newAction,openAction,saveAsAction,exportAction,
                                  versionsAction});
        documentMenu->addSeparator();
        documentMenu->addAction(closeAction);
        QList<QAction*> moreActions{
            documentMenu->menuAction(),optionsAction,canvasOnlyAction
        };
        if(!viewMenu->isEmpty())
            moreActions.append(viewMenu->menuAction());
        if(!selectionMenu->isEmpty())
            moreActions.append(selectionMenu->menuAction());
        if(!drawingMenu->isEmpty())
            moreActions.append(drawingMenu->menuAction());
        if(!filtersMenu->isEmpty())
            moreActions.append(filtersMenu->menuAction());
        moreActions.append(commandsAction);
        moreActions.append(settingsAction);
        moreActions.append(colorOptionsAction);
        moreActions.append(inputAction);
        moreActions.append(exitAction);
        MobileNativeCommandLayout::Commands toolbarCommands;
        for(auto *action:nativeBridge.availableActions()) {
            if(!action || action->objectName().isEmpty() || action->menu() ||
               action->text().isEmpty() || action->isSeparator() ||
               action->objectName().startsWith(QStringLiteral("mobile_")))continue;
            toolbarCommands.insert(QStringLiteral("krita/")+action->objectName(),action);
        }
        const QList<QPair<const char*,QAction*>> studioCommands{
            {"paint",brushesAction},{"erase",eraserAction},{"blend",blendBrushAction},
            {"tool-settings",optionsAction},{"color",colorAction},{"layers",layersAction},
            {"tools",toolsAction},{"undo",undoAction},{"redo",redoAction},{"save",saveAction},
            {"shortcut",shortcutSlot},{"new",newAction},{"open",openAction},
            {"save-as",saveAsAction},{"export",exportAction},{"versions",versionsAction},
            {"close",closeAction},{"canvas-only",canvasOnlyAction},
            {"clone-source",cloneSourceAction},{"input-settings",inputAction},
            {"workspace-settings",settingsAction},{"color-options",colorOptionsAction},
            {"commands",commandsAction}
        };
        for(const auto &entry:studioCommands)
            toolbarCommands.insert(QString::fromLatin1(entry.first),entry.second);
        setStudioIcon(optionsAction,"paintop_settings_02");
        if(!MobileNativeCommandLayout::install(window,homeAction,studioIcon("configure"),
            toolbarCommands,{"paint","erase","blend","tool-settings","color","layers",
                             "tools","undo","redo","save","shortcut"},
            moreActions,canvasOnlyAction)) {
            qWarning() << "Independent Krita command layout unavailable";
        }

        auto *home=new MobileDocumentHome(nativeBridge,services.documents,window);
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
        QObject::connect(openAction,&QAction::triggered,window,
                         [alive,documents]() {
            // Android native file pickers should not appear while Qt
            // is still dispatching the initiating toolbar action.
            if(!alive)return;
            QTimer::singleShot(0,alive.data(),[alive,documents]() {
                if(alive)documents->openFiles(alive.data());
            });
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
        QObject::connect(exportAction,&QAction::triggered,window,
                         [documents,deferViewCommand]() {
            deferViewCommand([documents](const QPointer<QObject> &selected) {
                documents->exportDocumentView(selected);
            });
        });

        QObject::connect(versionsAction,&QAction::triggered,window,
                         [alive,home,views=services.views]() {
            if(!alive || !views)return;
            const auto *doc=qobject_cast<KisDocument*>(views->currentDocument().data());
            if(!doc)return;
            const QString path=doc->path();
            QTimer::singleShot(0,home,[home,path]() {home->showSavedVersions(path);});
        });

        QObject::connect(closeAction,&QAction::triggered,window,
                         [alive,documents,deferViewCommand]() {
            deferViewCommand([alive,documents](
                    const QPointer<QObject> &selected) {
                if(alive && !documents->closeDocumentView(selected))
                    qWarning() << "Independent Krita close canceled or rejected";
            });
        });
        QObject::connect(homeAction,&QAction::triggered,window,
                         [alive,home,documents,deferViewCommand]() {
            if(!alive)return;
            if(documents->openDocuments().isEmpty()) {
                home->showHome();
                return;
            }
            deferViewCommand([home,documents](const QPointer<QObject> &selected) {
                if(documents->closeDocumentView(selected))home->showHome();
            });
        });

        // Close uses the existing Krita Save/Discard/Cancel transaction.
        QPointer<QWidget> topLevel=window;
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

        // Re-expose the first real canvas only after Krita has attached
        // and activated it. Home can precede that by any amount of time.
        const auto firstCanvas=[alive,stage]() {
            if(!alive || !alive->nativeViewPort()->currentDocument() ||
               alive->property("_mobileFirstCanvasAttached").toBool())return;
            alive->setProperty("_mobileFirstCanvasAttached",true);
            alive->suppressNativeStatusChrome();
            stage("First native Krita canvas ready",false);
            if(!alive->property("_mobileNativeEngineOnlyRoot").toBool())return;
            QTimer::singleShot(0,alive.data(),[alive]() {
                if(!alive || !alive->isVisible() ||
                   !alive->nativeViewPort()->currentDocument())return;
                alive->hide();
                QTimer::singleShot(50,alive.data(),[alive]() {
                    if(!alive)return;
                    alive->show();
                    alive->raise();
                    alive->suppressNativeStatusChrome();
                    if(QWidget *canvas=alive->nativeCanvas())canvas->update();
                });
            });
        };
        if(auto *stack=qobject_cast<QStackedWidget*>(window->nativeCanvas())) {
            QObject::connect(stack,&QStackedWidget::currentChanged,window,
                             [alive,home,firstCanvas](int) {
                if(!alive)return;
                const bool wasHome=home->isVisible();
                QTimer::singleShot(0,alive.data(),[alive,home,firstCanvas,wasHome]() {
                    if(!alive)return;
                    if(alive->nativeViewPort()->currentDocument()) {
                        if(wasHome)home->hide();
                        firstCanvas();
                    } else if(alive->nativeViewPort()->openViews().isEmpty()) {
                        home->showHome();
                    }
                });
            });
        }

        stage("Showing native Studio window");
        window->show();
        window->raise();
        window->suppressNativeStatusChrome();
        QTimer::singleShot(0,window,
                           [alive,home,stage,firstCanvas,firstCanvasReady,
                            intentHost=services.documents]() {
            if(!alive)return;
            const int opened=MobileAndroidDocumentIntents::attachAndOpen(
                alive.data(),intentHost);
            if(opened==0 && !home->restoreSingleDraft()) {
                home->showHome();
                stage("Document Home ready",false);
            } else {
                firstCanvas();
            }
            if(firstCanvasReady)firstCanvasReady(true);
        });
        return true;
    }
};
#endif
