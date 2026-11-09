/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Explicit opt-in, minimal real-engine Android smoke window.
 * This does not alter the existing Mobile Studio launcher or migrate
 * any live Krita document. The temporary toolbar is diagnostic only.
 *
 * Launch from Settings > Standalone Engine Test (experimental).
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QPointer>
#include <QSharedPointer>
#include <QSettings>
#include <functional>
#include "native_startup_policy.h"
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
#include "engine_only_startup_policy.h"

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
        auto *exitAction=makeAction("Exit test");
        auto *engineOnlyAction=makeAction(
            "Engine-only on next launch (experimental)");
        auto *architectureAction=makeAction("Engine separation status");
        QObject::connect(brushesAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes();});
        QObject::connect(layersAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleLayers();});
        QObject::connect(colorAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleColor();});
        QObject::connect(toolsAction,&QAction::triggered,window,
                         [workspace]() {workspace->chooseTool();});
        auto *nativeStartup=makeAction("Start here next launch");
        nativeStartup->setCheckable(true);
        {
            QSettings settings;
            nativeStartup->setChecked(MobileNativeStartupPolicy::enabled(settings));
        }
        QObject::connect(nativeStartup,&QAction::toggled,window,
                         [engineOnlyAction](bool on) {
            QSettings settings;
            MobileNativeStartupPolicy::setEnabled(settings,on);
            if(on)engineOnlyAction->setChecked(false);
        });
        engineOnlyAction->setCheckable(true);
        {
            QSettings settings;
            engineOnlyAction->setChecked(
                MobileEngineOnlyStartupPolicy::enabled(settings));
        }
        engineOnlyAction->setToolTip(QObject::tr(
            "Launch directly into the independent native drawing window."));
        QObject::connect(engineOnlyAction,&QAction::toggled,window,
                         [nativeStartup](bool on) {
            QSettings settings;
            MobileEngineOnlyStartupPolicy::setEnabled(settings,on);
            if(on)nativeStartup->setChecked(false);
        });
        // Avoid an overflow chevron hiding the primary drawing tools.
        // Less-common operations stay in an explicit, labeled menu.
        if(!MobileNativeCommandLayout::install(
            window,
            {canvasesAction,undoAction,redoAction,saveAction},
            {brushesAction,layersAction,colorAction,toolsAction},
            {newAction,openAction,previousAction,nextAction,
             saveAsAction,closeAction,nativeStartup,
             engineOnlyAction,architectureAction,exitAction})) {
            qWarning() << "Independent Krita command layout unavailable";
        }

        QObject::connect(architectureAction,&QAction::triggered,window,
                         [alive,desktopNative]() {
            if(!alive || !desktopNative)return;
            QSettings settings;
            const int legacyWindows=KisPart::instance() ?
                KisPart::instance()->mainwindowCount() : -1;
            const bool engineOnly=desktopNative->property(
                "_mobileNativeEngineOnlyRoot").toBool();
            QMessageBox::information(
                alive.data(),QObject::tr("Engine architecture"),
                QObject::tr("Engine-only startup: %1\n"
                            "KisMainWindow instances: %2\n"
                            "Desktop widget audit: %3\n"
                            "Direct engine root: %4")
                    .arg(engineOnly?QObject::tr("Yes"):QObject::tr("No"))
                    .arg(legacyWindows)
                    .arg(settings.value(
                        QStringLiteral("MobileStudio/nativeDesktopWidgetAudit"),
                        QStringLiteral("Not recorded")).toString())
                    .arg(dynamic_cast<MobileLightweightCanvasHost*>(
                        desktopNative.data())?
                        QObject::tr("Yes"):QObject::tr("No")));
        });

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

        // Exit returns to the original canvas without closing it.
        // The child host's closeEvent still enforces Krita's
        // Save/Discard/Cancel for any experimental drawings.
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
