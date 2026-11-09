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
#include "deferred_view_command.h"
#include "document_navigation.h"
#include "document_workflow.h"
#include "document_switcher.h"
#include "krita_standalone_documents.h"
#include "krita_standalone_runtime.h"
#include "krita_native_workspace.h"

class MobileKritaStandalonePilot final
{
public:
    static bool launch(QWidget *nativeWindow,
                       std::function<void(bool)> firstCanvasReady={})
    {
        if(!nativeWindow || nativeWindow->window()!=nativeWindow ||
           !nativeWindow->isVisible())return false;
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

        // Persist the latest phase so a crash can be diagnosed after
        // Krita's desktop fallback restarts the Android process.
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
        stage("Creating embedded Qt host");
        auto *window=new MobileLightweightCanvasHost;
        if(!window->embedInExistingWindow(nativeWindow)) {
            delete window;
            return false;
        }
        window->setObjectName(
            QStringLiteral("_mobileStandaloneEnginePilot"));
        window->setWindowTitle(
            QObject::tr("Krita native engine test"));
        window->setAttribute(Qt::WA_DeleteOnClose,true);
        if(!window->createNativeDocumentRoot()) {
            delete window;
            return false;
        }
        stage("Creating independent Krita view manager");
        MobileKritaStandaloneRuntime *runtime=
            MobileKritaStandaloneRuntime::create(window);
        if(!runtime) {
            delete window;
            return false;
        }
        stage("Connecting native Krita services");
        const MobileHostServices services=
            MobileKritaStandaloneDocumentsBootstrap::create(runtime);
        if(!services.ready()) {
            delete window;
            return false;
        }
        auto documents=QSharedPointer<MobileDocumentService>::create(
            services.documents,services.views);
        QPointer<MobileLightweightCanvasHost> alive=window;
        const MobileEngineBridge nativeBridge(services,runtime->viewManager());
        // Native controls now use the independent manager and host ports,
        // not the old desktop MDI window or its docker instances.
        auto *workspace=new MobileKritaNativeWorkspace(
            window,services,runtime->viewManager());

        // Diagnostics, not a new permanent mobile toolbar.
        auto *bar=window->addToolBar(
            QObject::tr("Engine test controls"));
        bar->setMovable(false);
        auto *newAction=bar->addAction(QObject::tr("New"));
        auto *openAction=bar->addAction(QObject::tr("Open"));
        auto *canvasesAction=bar->addAction(QObject::tr("Canvases"));
        auto *previousAction=bar->addAction(QObject::tr("Previous canvas"));
        auto *nextAction=bar->addAction(QObject::tr("Next canvas"));
        auto *undoAction=bar->addAction(QObject::tr("Undo"));
        auto *redoAction=bar->addAction(QObject::tr("Redo"));
        auto *saveAction=bar->addAction(QObject::tr("Save"));
        auto *saveAsAction=bar->addAction(QObject::tr("Save As"));
        auto *closeAction=bar->addAction(QObject::tr("Close view"));
        auto *brushesAction=bar->addAction(QObject::tr("Brushes"));
        auto *layersAction=bar->addAction(QObject::tr("Layers"));
        auto *colorAction=bar->addAction(QObject::tr("Color"));
        auto *toolsAction=bar->addAction(QObject::tr("Tools"));
        auto *exitAction=bar->addAction(QObject::tr("Exit test"));
        QObject::connect(brushesAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleBrushes();});
        QObject::connect(layersAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleLayers();});
        QObject::connect(colorAction,&QAction::triggered,window,
                         [workspace]() {workspace->toggleColor();});
        QObject::connect(toolsAction,&QAction::triggered,window,
                         [workspace]() {workspace->chooseTool();});
        auto *nativeStartup=bar->addAction(
            QObject::tr("Start here next launch"));
        nativeStartup->setCheckable(true);
        {
            QSettings settings;
            nativeStartup->setChecked(MobileNativeStartupPolicy::enabled(settings));
        }
        QObject::connect(nativeStartup,&QAction::toggled,window,[](bool on) {
            QSettings settings;
            MobileNativeStartupPolicy::setEnabled(settings,on);
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
        QObject::connect(exitAction,&QAction::triggered,window,[alive]() {
            if(alive)QTimer::singleShot(0,alive.data(),[alive]() {
                if(alive)alive->close();
            });
        });

        // The real KisCanvas2 must become visible before Qt initializes
        // brush resources and its OpenGL context. Both the initial canvas
        // and later documents use the same native KisPart engine path.
        stage("Showing embedded engine test canvas");
        window->show();
        window->raise();
        QTimer::singleShot(0,window,[alive,documents,stage,firstCanvasReady]() {
            if(!alive)return;
            stage("Creating first native Krita document and view");
            if(!documents->createCanvas(
                       QStringLiteral("Engine test"),512,512,72,true)) {
                stage("Krita refused first native document creation");
                if(firstCanvasReady)firstCanvasReady(false);
                QMessageBox::warning(
                    alive.data(),QObject::tr("Engine test"),
                    QObject::tr("Native view initialization failed. "
                                "The existing Krita interface is unchanged."));
                return;
            }
            stage("First native Krita canvas ready",false);
            if(firstCanvasReady)firstCanvasReady(true);
        });
        return true;
    }
};
#endif
