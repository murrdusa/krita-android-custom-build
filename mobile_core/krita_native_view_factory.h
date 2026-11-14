/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Real Krita view attachment for the independent Qt document root.
 * Opt-in native engine launch and basic pen painting are device-validated;
 * Krita retains all original painting and rendering engines.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QMetaObject>
#include <QStackedWidget>
#include <KisDocument.h>
#include <KisPart.h>
#include <KisView.h>
#include <KisViewManager.h>
#include <KoToolManager.h>
#include <KoCanvasSupervisor.h>
#include "kis_canvas2.h"
#include "kis_paintop_box.h"
#include "input/kis_input_manager.h"
#include "native_view_stack.h"
#include "native_krita_save_host.h"
#include "canvas_resume_observer.h"
#include "saved_version_archive.h"

class MobileKritaNativeViewFactory final
{
public:
    static KisView *attach(KisDocument *document,KisViewManager *manager,
                           MobileNativeViewStack *stack,
                           MobileKritaNativeSaveHost *saver)
    {
        if(!document || !document->image() || !manager || !stack ||
           !saver || !KisPart::instance() || !manager->qtMainWindow() ||
           stack->parentWidget()!=manager->qtMainWindow() ||
           stack->window()!=manager->qtMainWindow()->window() ||
           manager->mainWindow() ||
           !dynamic_cast<KoCanvasSupervisor*>(manager->qtMainWindow()))
            return nullptr;

        // Create and register the REAL KisView in Krita's KisPart.
        // Its QWidget must be new, unattached and never borrowed from MDI.
        KisView *view=KisPart::instance()->createView(document,manager,nullptr);
        if(!view)return nullptr;
        QPointer<KisView> guarded=view;
        view->setAttribute(Qt::WA_DeleteOnClose,false);
        view->setMobileNativeSaveHost(saver);
        // Krita's view-manager and loading hooks can run synchronous
        // callbacks that close the document or its native view.
        const QPointer<KisViewManager> guardedManager(manager);
        const QPointer<MobileNativeViewStack> guardedStack(stack);
        manager->inputManager()->addTrackedCanvas(view->canvasBase());
        if(!guarded || !guardedManager || !guardedStack)return nullptr;
        view->setViewManager(manager);
        if(!guarded || !guardedManager || !guardedStack)return nullptr;
        if(!view->canvasBase() || !manager->paintOpBox())return nullptr;
        view->canvasBase()->setFavoriteResourceManager(
            manager->paintOpBox()->favoriteResourcesManager());
        if(!guarded || !guardedManager || !guardedStack)return nullptr;
        view->slotLoadingFinished();
        if(!guarded || !guardedManager || !guardedStack)return nullptr;

        const QString title=document->path().isEmpty()?
            QStringLiteral("Untitled"):
            MobileSavedVersionArchive::projectName(document->path());
        if(!stack->adoptView(view,document,title,
                            [guarded,manager=QPointer<KisViewManager>(manager)]() {
            if(!guarded || !manager)return false;
            // Closing several tabs can advance the Qt stack before the
            // queued native activation runs. Save must use that same view.
            if(manager->canvasBase()!=guarded->canvasBase())
                manager->setCurrentView(guarded);
            if(!guarded || !guarded->close())return false;
            // Close may be canceled. Once accepted, destroy the native
            // view synchronously while its Krita view manager is alive.
            // Deferring deletion could outlive the manager during quit.
            if(guarded)delete guarded.data();
            return true;
        })) {
            // currentChanged handlers inside adoptView may already have
            // destroyed the candidate. Never delete a dangling pointer.
            if(guarded)delete guarded.data();
            return nullptr;
        }

        // Krita emits both title/path and dirty-state changes. Keep
        // the native document switcher in sync with the actual engine,
        // without inserting a second document model.
        QPointer<KisDocument> trackedDocument=document;
        QPointer<MobileNativeViewStack> trackedStack=stack;
        auto refresh=[trackedStack,trackedDocument,guarded]() {
            if(!trackedStack || !trackedDocument || !guarded)return;
            const QString path=trackedDocument->path();
            const QString title=path.isEmpty()?
                QStringLiteral("Untitled"):
                MobileSavedVersionArchive::projectName(path);
            // QPointer is not covariant in the Android Qt version:
            // explicitly convert the live KisView pointer to the
            // QObject identity expected by MobileNativeViewStack.
            const QPointer<QObject> identity(guarded.data());
            trackedStack->updateViewState(identity,title,
                                          trackedDocument->isModified());
        };
        QObject::connect(document,&KisDocument::sigPathChanged,stack,
                         [refresh](const QString &) {refresh();});
        QObject::connect(document,&KisDocument::modified,stack,
                         [refresh](bool) {refresh();});
        refresh();

        // One native manager activation router per Qt document root.
        // Connections are scoped to the native manager QObject.
        const bool selectionAlreadyWired=
            stack->property("_mobileStudioKritaSelectionWired").toBool();
        if(!selectionAlreadyWired) {
            QPointer<MobileNativeViewStack> root=stack;
            QPointer<KisViewManager> vm=manager;
            QObject::connect(stack,&QStackedWidget::currentChanged,manager,
                [root,vm](int) {
                    if(!root || !vm)return;
                    scheduleActivation(vm,root,
                        qobject_cast<KisView*>(root->currentWidget()));
                });
            // A Qt timer may fire while Android has hidden this
            // window. An early visibility guard is correct for GL,
            // but without a Show/Activate hook the canvas would never
            // be bootstrapped after the user returns to the app.
            MobileCanvasResumeObserver::attach(
                manager->qtMainWindow(),stack,manager,
                [root,vm]() {
                    if(!root || !vm)return;
                    scheduleActivation(vm,root,
                        qobject_cast<KisView*>(root->currentWidget()));
                });
            stack->setProperty("_mobileStudioKritaSelectionWired",true);
        }

        if(!guarded || !guardedStack || !guardedManager)return nullptr;
        view->show();
        if(!guarded || !guardedStack || !guardedManager)return nullptr;
        stack->setCurrentWidget(view);
        if(!guarded || !guardedStack || !guardedManager)return nullptr;
        // First addWidget() emits currentChanged before the router is
        // wired. Only that first canvas needs explicit activation.
        // Later changes are already routed through currentChanged.
        if(!selectionAlreadyWired)
            scheduleActivation(manager,stack,guarded);
        return guarded.data();
    }

private:
    static void scheduleActivation(
        QPointer<KisViewManager> manager,
        QPointer<MobileNativeViewStack> root,
        QPointer<KisView> selected)
    {
        if(!manager || !root)return;
        QTimer::singleShot(0,manager,[manager,root,selected]() {
            if(!manager || !root)return;
            if(selected && (root->currentWidget()!=selected ||
                            !selected->isVisible() ||
                            !selected->document() ||
                            root->currentDocument()!=selected->document()))
                return; // Switch was superseded before the GL show.
            if(!selected && root->currentWidget())return;
            // The canvas resource provider and active brush must first
            // refer to the CURRENT view, not the previously selected tab.
            const bool changedView=
                manager->canvasBase()!=(selected?selected->canvasBase():nullptr);
            if(changedView)manager->setCurrentView(selected);
            if(selected) {
                if(!root || !manager || root->currentWidget()!=selected ||
                   !selected->document() || root->currentDocument()!=selected->document())return;
                // Native guiLoadingFinished() only fires once on
                // desktop Krita. Never re-bootstrap on each tab switch.
                const bool firstBootstrap=!selected->property(
                    "_mobileStudioKritaCanvasBootstrapped").toBool();
                if(firstBootstrap) {
                    const bool bootstrapped=QMetaObject::invokeMethod(
                        selected->canvasBase(),"bootstrapFinished",
                        Qt::DirectConnection);
                    if(bootstrapped)
                        selected->setProperty(
                            "_mobileStudioKritaCanvasBootstrapped",true);
                    else
                        qWarning() << "Mobile Studio: canvas bootstrap"
                                   << "method unavailable";
                }
                // WindowActivate/Expose also fire when a popup closes.
                // A full reconnect deactivates Transform (committing its
                // stroke) and activates it again in Free mode. Repaint an
                // unchanged canvas without restarting the current tool.
                if(changedView || firstBootstrap) {
                    qInfo() << "Mobile canvas tool initialization"
                            << selected.data() << "changed view" << changedView;
                    KoToolManager::instance()->initializeCurrentToolForCanvas();
                    // Tool setup can synchronously tear down the active
                    // canvas; do not dereference a deleted selected view.
                    if(!selected || !root || !manager ||
                       root->currentWidget()!=selected.data())return;
                }
                // Android can show the native QWidget before its
                // QWindow/GL surface receives the first Expose event.
                // Force a real engine projection/GL paint when activated,
                // including on first exposure. No background polling.
                if(auto *canvas=selected->canvasBase()) {
                    canvas->updateCanvas();
                    if(QWidget *surface=canvas->canvasWidget())
                        surface->update();
                }
            }
        });
    }
};
#endif
