/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Future real-Krita view attachment for the independent Qt document root.
 * NOT called by the Android launcher until standalone KisViewManager and
 * Android GL painting are validated. Krita retains all painting engines.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QPointer>
#include <QStackedWidget>
#include <KisDocument.h>
#include <KisPart.h>
#include <KisView.h>
#include <KisViewManager.h>
#include <KoToolManager.h>
#include "kis_canvas2.h"
#include "kis_paintop_box.h"
#include "input/kis_input_manager.h"
#include "native_view_stack.h"
#include "native_krita_save_host.h"

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
           manager->mainWindow())return nullptr;

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
            // closeEvent accepts/cancels first, then delete later.
            if(guarded)guarded->deleteLater();
            return true;
        })) {
            delete view; // KisPart unregisters via KisView destructor.
            return nullptr;
        }

        // One native manager activation router per Qt document root.
        // Connections are scoped to the native manager QObject.
        if(!stack->property("_mobileStudioKritaSelectionWired").toBool()) {
            QPointer<MobileNativeViewStack> root=stack;
            QPointer<KisViewManager> vm=manager;
            QObject::connect(stack,&QStackedWidget::currentChanged,manager,
                [root,vm](int) {
                    if(!root || !vm)return;
                    auto *selected=qobject_cast<KisView*>(root->currentWidget());
                    if(vm->canvasBase()!=(selected?selected->canvasBase():nullptr))
                        vm->setCurrentView(selected);
                });
            stack->setProperty("_mobileStudioKritaSelectionWired",true);
        }

        view->show();
        stack->setCurrentWidget(view);
        // Krita's upstream showView initializes tools *after* show().
        KoToolManager::instance()->initializeCurrentToolForCanvas();
        if(manager->canvasBase()!=view->canvasBase())
            manager->setCurrentView(view);
        return view;
    }
};
#endif
