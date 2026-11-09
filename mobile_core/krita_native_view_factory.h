/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Future real-Krita view attachment for the independent Qt document root.
 * NOT called by the Android launcher until standalone KisViewManager and
 * Android GL painting are validated. Krita retains all painting engines.
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

class MobileKritaNativeViewFactory final
{
public:
    static KisView *attach(KisDocument *document,KisViewManager *manager,
                           MobileNativeViewStack *stack,
                           MobileKritaNativeSaveHost *saver)
    {
        if(!document || !document->image() || !manager || !stack ||
           !saver || !KisPart::instance() || !manager->qtMainWindow() ||
           stack->window()!=manager->qtMainWindow() ||
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
        manager->inputManager()->addTrackedCanvas(view->canvasBase());
        view->setViewManager(manager);
        view->canvasBase()->setFavoriteResourceManager(
            manager->paintOpBox()->favoriteResourcesManager());
        view->slotLoadingFinished();

        const QString title=document->path().isEmpty()?
            QStringLiteral("Untitled"):document->path();
        if(!stack->adoptView(view,document,title,[guarded]() {
            if(!guarded || !guarded->close())return false;
            // Close may be canceled. Once accepted, destroy the native
            // view synchronously while its Krita view manager is alive.
            // Deferring deletion could outlive the manager during quit.
            if(guarded)delete guarded.data();
            return true;
        })) {
            delete view; // KisPart unregisters via KisView destructor.
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
                QStringLiteral("Untitled"):QFileInfo(path).fileName();
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

        view->show();
        stack->setCurrentWidget(view);
        // First addWidget() emits currentChanged before the router is
        // wired. Only that first canvas needs explicit activation.
        // Later changes are already routed through currentChanged.
        if(!selectionAlreadyWired)
            scheduleActivation(manager,stack,guarded);
        return view;
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
                            !selected->isVisible()))
                return; // Switch was superseded before the GL show.
            if(!selected && root->currentWidget())return;
            if(selected) {
                // Native guiLoadingFinished() only fires once on
                // desktop Krita. Never re-bootstrap on each tab switch.
                if(!selected->property(
                       "_mobileStudioKritaCanvasBootstrapped").toBool()) {
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
                KoToolManager::instance()->initializeCurrentToolForCanvas();
            }
            if(manager->canvasBase()!=(selected?selected->canvasBase():nullptr))
                manager->setCurrentView(selected);
        });
    }
};
#endif
