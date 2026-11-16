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
#include <QLayout>
#include <QOpenGLWidget>
#include <QPointer>
#include <QTimer>
#include <QMetaObject>
#include <QWindow>
#include <QStackedWidget>
#include <KisDocument.h>
#include <KisPart.h>
#include <KisView.h>
#include <KisViewManager.h>
#include <KoToolManager.h>
#include <KoCanvasSupervisor.h>
#include "kis_canvas2.h"
#include "kis_coordinates_converter.h"
#include "kis_zoom_manager.h"
#include "crash_report.h"
#include "kis_paintop_box.h"
#include "input/kis_input_manager.h"
#include "native_view_stack.h"
#include "native_krita_save_host.h"
#include "canvas_resume_observer.h"
#include "saved_version_archive.h"

class MobileKritaNativeViewFactory final
{
public:
    static void refreshAfterHome(QPointer<KisViewManager> manager,
                                 QPointer<MobileNativeViewStack> root)
    {
        if(!manager || !root)return;
        const QPointer<KisView> selected=qobject_cast<KisView*>(root->currentWidget());
        if(selected)scheduleActivation(manager,root,selected);
    }

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
        // Creating the real Krita view can dispatch Qt callbacks before
        // returning. Guard its dependencies *before* createView(), rather
        // than accessing a manager/document that was destroyed inside it.
        const QPointer<KisDocument> guardedDocument(document);
        const QPointer<KisViewManager> guardedManager(manager);
        const QPointer<MobileNativeViewStack> guardedStack(stack);
        KisView *view=KisPart::instance()->createView(document,manager,nullptr);
        if(!view)return nullptr;
        QPointer<KisView> guarded=view;
        // If attachment fails after Krita created its view, release the
        // still-live unparented view while its manager is also alive.
        // Otherwise the failed first canvas leaks its GL surface.
        bool inputTracked=false;
        const auto abortAttach=[guarded,guardedManager,&inputTracked]() -> KisView* {
            if(guarded && guardedManager) {
                // Tracking precedes setViewManager() during native view
                // initialization. If setup fails in that interval,
                // KisView's destructor cannot deregister the canvas
                // through a manager it was never assigned.
                if(inputTracked && guarded->viewManager()!=guardedManager.data() &&
                   guardedManager->inputManager() && guarded->canvasBase())
                    guardedManager->inputManager()->removeTrackedCanvas(
                        guarded->canvasBase());
                delete guarded.data();
            }
            return nullptr;
        };
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        // Size the hidden view before Krita initializes its zoom and
        // projection. QStackedWidget owns when that view is shown.
        const QSize initialSize=stack->contentsRect().size();
        if(!initialSize.isEmpty())view->resize(initialSize);
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        view->setAttribute(Qt::WA_DeleteOnClose,false);
        view->setMobileNativeSaveHost(saver);
        // Krita's view-manager and loading hooks can run synchronous
        // callbacks that close the document or its native view.
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        // A partially initialized native manager/canvas must not be
        // handed to Krita's input tracker after a reentrant teardown.
        if(!manager->inputManager() || !view->canvasBase())
            return abortAttach();
        manager->inputManager()->addTrackedCanvas(view->canvasBase());
        inputTracked=true;
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        view->setViewManager(manager);
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        if(!view->canvasBase() || !manager->paintOpBox())
            return abortAttach();
        view->canvasBase()->setFavoriteResourceManager(
            manager->paintOpBox()->favoriteResourcesManager());
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        view->slotLoadingFinished();
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();

        // A path getter can emit synchronous Krita/Qt notifications.
        // Read it once, then verify all original attachment identities
        // before passing the view into the native stack.
        const QString initialPath=guardedDocument->path();
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortAttach();
        const QString title=initialPath.isEmpty()?
            QStringLiteral("Untitled"):
            MobileSavedVersionArchive::projectName(initialPath);
        if(!stack->adoptView(view,document,title,
                            [guarded,manager=QPointer<KisViewManager>(manager)]() {
            if(!guarded || !manager)return false;
            // Closing several tabs can advance the Qt stack before the
            // queued native activation runs. Save must use that same view.
            if(manager->canvasBase()!=guarded->canvasBase())
                manager->setCurrentView(guarded);
            // Activating another native canvas can synchronously close
            // this view or destroy its manager during Qt signal delivery.
            if(!guarded || !manager)return false;
            const bool accepted=guarded->close();
            if(!accepted)return false;
            // Close may synchronously destroy both manager and view.
            // The KisView destructor calls back into its manager, so
            // only destroy a surviving view while that manager is live.
            if(guarded && manager)delete guarded.data();
            return true;
        })) {
            // currentChanged handlers inside adoptView may already have
            // destroyed the candidate or its manager. KisView teardown
            // must not invoke callbacks on a dead view manager.
            if(guarded && guardedManager)delete guarded.data();
            return nullptr;
        }
        // After adoption, a later Qt callback can invalidate the
        // document or manager without removing the view registration.
        // Roll back only our original registration. An independently
        // reparented or re-registered widget belongs to its new owner.
        const auto abortRegistered=[guarded,guardedDocument,guardedManager,
                                    guardedStack]() -> KisView* {
            if(!guarded || !guardedStack)return nullptr;
            bool registered=false;
            for(const auto &entry:guardedStack->openViews()) {
                if(entry.nativeView.data()==guarded.data() &&
                   entry.document==guardedDocument) {
                    registered=true;
                    break;
                }
            }
            if(!registered)return nullptr;
            const QPointer<QObject> identity(guarded.data());
            guardedStack->forgetView(identity);
            // A KisView destructor calls its native manager. Only
            // destroy the detached view if that manager still exists.
            if(guarded && guardedManager && guardedStack &&
               guarded->parentWidget()==guardedStack.data() &&
               guardedStack->indexOf(guarded.data())<0)
                delete guarded.data();
            return nullptr;
        };
        // addWidget() emits currentChanged synchronously. Its listeners
        // can destroy the Krita manager without destroying this root or
        // view; do not wire or display a canvas with a dead manager.
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortRegistered();

        // Krita emits both title/path and dirty-state changes. Keep
        // the native document switcher in sync with the actual engine,
        // without inserting a second document model.
        QPointer<KisDocument> trackedDocument=document;
        QPointer<MobileNativeViewStack> trackedStack=stack;
        auto refresh=[trackedStack,trackedDocument,guarded]() {
            if(!trackedStack || !trackedDocument || !guarded)return;
            const QString path=trackedDocument->path();
            if(!trackedStack || !trackedDocument || !guarded)return;
            const QString title=path.isEmpty()?
                QStringLiteral("Untitled"):
                MobileSavedVersionArchive::projectName(path);
            if(!trackedStack || !trackedDocument || !guarded)return;
            const bool modified=trackedDocument->isModified();
            if(!trackedStack || !trackedDocument || !guarded)return;
            // QPointer is not covariant in the Android Qt version:
            // explicitly convert the live KisView pointer to the
            // QObject identity expected by MobileNativeViewStack.
            const QPointer<QObject> identity(guarded.data());
            trackedStack->updateViewState(identity,title,modified);
        };
        QObject::connect(document,&KisDocument::sigPathChanged,stack,
                         [refresh](const QString &) {refresh();});
        QObject::connect(document,&KisDocument::modified,stack,
                         [refresh](bool) {refresh();});
        refresh();
        // The update can notify native document-switcher observers and
        // synchronously tear down the host. Do not read stack properties
        // or attach activation observers after that teardown.
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortRegistered();

        // One native manager activation router per Qt document root.
        // Connections are scoped to the native manager QObject.
        const bool selectionAlreadyWired=
            stack->property("_mobileStudioKritaSelectionWired").toBool();
        // Qt dynamic-property observers can synchronously tear down
        // this view, manager or root before connections are installed.
        if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
            return abortRegistered();
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
            // Installing the window/surface observer can synchronously
            // deliver Qt events; do not mark a destroyed root as wired.
            // If the original view remains registered, release that
            // partial attachment just like the other late failures.
            if(!guarded || !guardedDocument || !guardedManager || !guardedStack)
                return abortRegistered();
            stack->setProperty("_mobileStudioKritaSelectionWired",true);
            if(!guarded || !guardedDocument || !guardedStack || !guardedManager)
                return abortRegistered();
        }

        if(!guarded || !guardedDocument || !guardedStack || !guardedManager)
            return abortRegistered();
        stack->setCurrentWidget(view);
        if(!guarded || !guardedDocument || !guardedStack || !guardedManager)
            return abortRegistered();
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
            if(!manager || !root)return;
            if(selected && (root->currentWidget()!=selected.data() ||
                            !selected->document()))return;
            if(selected) {
                if(!root || !manager || root->currentWidget()!=selected ||
                   !selected->document() || root->currentDocument()!=selected->document())return;
                // Native guiLoadingFinished() only fires once on
                // desktop Krita. Never re-bootstrap on each tab switch.
                const bool firstBootstrap=!selected->property(
                    "_mobileStudioKritaCanvasBootstrapped").toBool();
                // Dynamic-property reads can dispatch Qt callbacks. The
                // view may have been closed or switched before touching
                // its painting canvas or invoking bootstrapFinished().
                if(!selected || !manager || !root ||
                   root->currentWidget()!=selected.data() ||
                   !selected->document() ||
                   root->currentDocument()!=selected->document())return;
                if(firstBootstrap) {
                    const QPointer<QObject> bootstrapCanvas=
                        selected->canvasBase();
                    if(!bootstrapCanvas)return;
                    const bool bootstrapped=QMetaObject::invokeMethod(
                        bootstrapCanvas.data(),"bootstrapFinished",
                        Qt::DirectConnection);
                    if(!selected || !manager || !root ||
                       root->currentWidget()!=selected.data() ||
                       !selected->document() ||
                       root->currentDocument()!=selected->document())return;
                    if(bootstrapped) {
                        selected->setProperty(
                            "_mobileStudioKritaCanvasBootstrapped",true);
                        if(!selected || !manager || !root ||
                           root->currentWidget()!=selected.data() ||
                           !selected->document() ||
                           root->currentDocument()!=selected->document())return;
                    } else {
                        qWarning() << "Mobile Studio: canvas bootstrap"
                                   << "method unavailable";
                    }
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
                       root->currentWidget()!=selected.data() ||
                       !selected->document() ||
                       root->currentDocument()!=selected->document())return;
                }
                const auto isCurrent=[manager,root,selected]() {
                    return manager && root && selected && selected->isVisible() &&
                        root->currentWidget()==selected && selected->document() &&
                        root->currentDocument()==selected->document() &&
                        selected->canvasBase()==manager->canvasBase();
                };
                if(!isCurrent())return;
                QPointer<KisCanvas2> canvas=selected->canvasBase();
                if(!canvas)return;
                // Home/IME transitions can leave a paint queued against the
                // pre-layout viewport. On the first visible pass, lay out the
                // real view and repopulate the projection, then paint it.
                const bool firstProjection=!selected->property("_mobileInitialProjectionReady").toBool();
                if(firstProjection) {
                    if(auto *layout=root->layout())layout->activate();
                    if(!isCurrent() || !canvas)return;
                    if(auto *layout=selected->layout())layout->activate();
                    if(!isCurrent() || !canvas)return;
                    QPointer<QWidget> surface=canvas->canvasWidget();
                    if(!surface || surface->size().isEmpty() || !surface->isVisible())return;
                    if(auto *gl=qobject_cast<QOpenGLWidget*>(surface.data())) {
                        if(!gl->isValid()) {
                            // A first swap can arrive after Qt has made the
                            // surface visible and initialized its GL context.
                            if(!surface->property("_mobileWaitingForFirstSwap").toBool()) {
                                surface->setProperty("_mobileWaitingForFirstSwap",true);
                                // Qt dynamic-property observers may tear down or
                                // replace the GL canvas synchronously. No swap
                                // receiver or deadline has been installed yet:
                                // if setup is interrupted, release the flag
                                // rather than leaving this surface stuck.
                                if(!isCurrent() || !canvas || !surface ||
                                   canvas->canvasWidget()!=surface) {
                                    if(surface)
                                        surface->setProperty(
                                            "_mobileWaitingForFirstSwap",false);
                                    return;
                                }
                                // This is a startup-only retry. Leaving a frameSwapped
                                // observer on the view makes every later painting frame
                                // execute a callback even after initialization succeeds.
                                // A child receiver owns the connection and disconnects
                                // before requesting the deferred activation.
                                auto *firstSwap=new QObject(selected.data());
                                // Name the receiver for opt-in native memory
                                // diagnostics. Closed views should retire
                                // these observers along with their canvases.
                                firstSwap->setObjectName(
                                    QStringLiteral("_mobileFirstSwapObserver"));
                                const QPointer<QObject> receiver(firstSwap);
                                const QPointer<QOpenGLWidget> watched(
                                    qobject_cast<QOpenGLWidget*>(surface.data()));
                                if(!watched) {
                                    firstSwap->deleteLater();
                                    if(surface)
                                        surface->setProperty(
                                            "_mobileWaitingForFirstSwap",false);
                                    return;
                                }
                                // Android can replace the GL QWidget before
                                // its first swap. Without clearing this flag,
                                // a new surface will never install a retry.
                                QObject::connect(watched.data(),&QObject::destroyed,firstSwap,
                                    [receiver]() {
                                        // Waiting-for-swap is stored on the
                                        // surface, not the KisView. A destroyed
                                        // surface discards that flag itself; a
                                        // replacement surface starts unarmed.
                                        if(receiver)receiver->deleteLater();
                                    });
                                // Android can suspend or lose the GL context
                                // before any frameSwapped signal occurs.
                                // Repeated two-second retries would otherwise
                                // allocate a new QObject and queue a surface
                                // update indefinitely while the driver is
                                // unavailable. Back off per GL surface (2, 4,
                                // 8, then 16 seconds) without abandoning
                                // recovery when its context becomes usable.
                                const int attempts=watched->property(
                                    "_mobileFirstSwapRetryCount").toInt();
                                const int retryDelayMs=
                                    MobileFirstSwapRetryPolicy::timeoutMs(attempts);
                                watched->setProperty(
                                    "_mobileFirstSwapRetryCount",
                                    MobileFirstSwapRetryPolicy::afterTimeoutScheduled(attempts));
                                if(!receiver || !watched || !selected ||
                                   !manager || !root) {
                                    if(watched)
                                        watched->setProperty(
                                            "_mobileWaitingForFirstSwap",false);
                                    if(receiver)receiver->deleteLater();
                                    return;
                                }
                                QTimer::singleShot(retryDelayMs,firstSwap,
                                    [manager,root,selected,watched,receiver]() {
                                        if(!receiver)return;
                                        if(watched)
                                            QObject::disconnect(
                                                watched.data(),nullptr,
                                                receiver.data(),nullptr);
                                        receiver->deleteLater();
                                        if(watched)
                                            watched->setProperty(
                                                "_mobileWaitingForFirstSwap",false);
                                        if(!manager || !root || !selected ||
                                           !watched || !selected->isVisible() ||
                                           root->currentWidget()!=selected.data() ||
                                           !selected->document() ||
                                           root->currentDocument()!=selected->document())
                                            return;
                                        // The root's existing activation
                                        // timer rechecks GL readiness. Do not
                                        // touch the canvas from this timeout.
                                        scheduleActivation(manager,root,selected);
                                    });
                                QObject::connect(watched.data(),&QOpenGLWidget::frameSwapped,firstSwap,
                                    [manager,root,selected,receiver,watched]() {
                                        if(receiver && watched)
                                            QObject::disconnect(watched.data(),nullptr,
                                                                receiver.data(),nullptr);
                                        if(receiver)receiver->deleteLater();
                                        // The one-shot observer is now gone.
                                        // Always disarm its flag on the
                                        // owning GL surface, including for a
                                        // valid swap. The deferred activation
                                        // can be skipped while Android hides
                                        // the app; leaving the flag set would
                                        // permanently prevent another retry.
                                        if(watched) {
                                            watched->setProperty(
                                                "_mobileWaitingForFirstSwap",false);
                                            if(watched->isValid())
                                                watched->setProperty(
                                                    "_mobileFirstSwapRetryCount",0);
                                        }
                                        if(!selected || !manager || !root)return;
                                        const bool projectionReady=selected->property(
                                            "_mobileInitialProjectionReady").toBool();
                                        if(!selected || !manager || !root ||
                                           projectionReady)return;
                                        // A swap does not guarantee a usable
                                        // context if Android is tearing it down.
                                        // Let the ordinary activation path
                                        // decide whether to arm a new retry.
                                        scheduleActivation(manager,root,selected);
                                    });
                            }
                            if(!isCurrent() || !canvas || !surface ||
                               canvas->canvasWidget()!=surface)return;
                            surface->update();
                            return;
                        }
                        // The GL widget may become valid on resume before
                        // frameSwapped fires. Do not carry a stale 16-second
                        // timeout into an unrelated future context loss.
                        if(gl->property("_mobileFirstSwapRetryCount").toInt()!=0) {
                            gl->setProperty("_mobileFirstSwapRetryCount",
                                            MobileFirstSwapRetryPolicy::afterSurfaceReady());
                            if(!isCurrent() || !canvas || !surface ||
                               canvas->canvasWidget()!=surface)return;
                        }
                    }
                    const QRectF viewport(surface->rect());
                    const QRectF image=selected->viewConverter()->imageRectInWidgetPixels();
                    // Krita zoom/view notifications may replace the active
                    // view even while the first canvas is being laid out.
                    if(!isCurrent() || !canvas || !surface ||
                       canvas->canvasWidget()!=surface)return;
                    const bool fitNeeded=!viewport.intersects(image);
                    if(fitNeeded)selected->zoomManager()->slotZoomToFit();
                    if(!isCurrent() || !canvas || canvas->canvasWidget()!=surface)return;
                    selected->setProperty("_mobileInitialProjectionReady",true);
                    if(!isCurrent() || !canvas || !surface ||
                       canvas->canvasWidget()!=surface)return;
                    const QPointer<KisDocument> currentDocument=selected->document();
                    if(!currentDocument)return;
                    const bool blank=currentDocument->property("_mobileBlankCanvas").toBool();
                    if(!isCurrent() || !canvas || !surface ||
                       canvas->canvasWidget()!=surface)return;
                    const bool waitingForSwap=surface->property(
                        "_mobileWaitingForFirstSwap").toBool();
                    if(!isCurrent() || !canvas || !surface ||
                       canvas->canvasWidget()!=surface)return;
                    const bool needsReload=!blank || fitNeeded || waitingForSwap;
                    // Native canvas initialization already queues the projection.
                    // A new blank image can use that upload on a ready surface.
                    if(needsReload) {
                        MobileCrashReport::checkpoint(QStringLiteral("Populate initial canvas projection"));
                        canvas->refetchDataFromImage();
                        if(!isCurrent() || !canvas || canvas->canvasWidget()!=surface)return;
                        MobileCrashReport::checkpoint(QStringLiteral("Initial canvas projection queued"));
                    }
                }
                canvas->updateCanvas();
                if(!isCurrent() || !canvas)return;
                if(auto *surface=canvas->canvasWidget())surface->update();
                // update() can synchronously dispatch a surface-lifecycle
                // callback which removes the root or its current view.
                if(!isCurrent() || !canvas)return;
                if(firstProjection) {
                    const QPointer<QWidget> hostWindow=root->window();
                    if(!isCurrent() || !hostWindow)return;
                    const QPointer<QWindow> window=hostWindow->windowHandle();
                    if(window && window->isExposed())window->requestUpdate();
                }
            }
        });
    }
};
#endif
