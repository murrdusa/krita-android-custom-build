/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Opt-in Krita engine bootstrap for a QMainWindow without KisMainWindow.
 * Device-validated for basic painting, Undo/Redo and KRA save/reopen.
 * Optional native-startup and controller interactions require further testing.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QMenuBar>
#include <QPointer>
#include <QSharedPointer>
#include <kactioncollection.h>
#include <KoToolFactoryBase.h>
#include <KoToolRegistry.h>
#include "KisPart.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "lightweight_canvas_host.h"
#include "krita_native_view_factory.h"

class MobileStandaloneKritaActions final : public MobileCommandRegistryPort
{
public:
    MobileStandaloneKritaActions(KisKActionCollection *actions,QMainWindow *host)
        : m_actions(actions),m_host(host) {}
    QAction *action(const QString &id) const override
    {
        return m_actions ? m_actions->action(id) : nullptr;
    }
    QList<QAction*> availableActions() const override
    {
        return m_actions ? m_actions->actions() : QList<QAction*>{};
    }
    QList<QAction*> menuRootActions() const override
    {
        if(!m_host)return {};
        // Do not create a legacy desktop menu bar by querying menuBar().
        const auto *bar=m_host->findChild<QMenuBar*>(
            QString(),Qt::FindDirectChildrenOnly);
        return bar ? bar->actions() : QList<QAction*>{};
    }
private:
    QPointer<KisKActionCollection> m_actions;
    QPointer<QMainWindow> m_host;
};

class MobileKritaStandaloneRuntime final : public QObject
{
public:
    static MobileKritaStandaloneRuntime *create(MobileLightweightCanvasHost *host)
    {
        if(!host || !KisPart::instance() || !host->nativeViewPort())
            return nullptr;
        auto *root=dynamic_cast<MobileNativeViewStack*>(host->nativeCanvas());
        if(!root || root->parentWidget()!=host ||
           root->window()!=host->window() ||
           !root->openViews().isEmpty() ||
           host->findChild<QObject*>(
               QStringLiteral("_mobileKritaRuntime"),Qt::FindDirectChildrenOnly))
            return nullptr;

        auto *runtime=new MobileKritaStandaloneRuntime(host,root);
        runtime->m_actions=new KisKActionCollection(
            runtime,QStringLiteral("krita-mobile-native"));
        runtime->m_actions->setComponentDisplayName(
            QStringLiteral("Krita Mobile Studio"));
        runtime->m_manager=new KisViewManager(host,runtime->m_actions);
        // KisViewManager(QWidget*,...) does not set its QObject parent.
        runtime->m_manager->setParent(runtime);
        runtime->m_manager->setQtMainWindow(host);
        // Krita's KisMainWindow registers plugin tool actions before
        // KisView::setViewManager() requests the active brush. Without
        // this, KoToolManager can select a tool whose actions were never
        // created for the independent host.
        for(KoToolFactoryBase *factory:KoToolRegistry::instance()->values()) {
            if(factory)factory->createActions(runtime->m_actions.data());
        }
        runtime->m_actions->addAssociatedWidget(host);
        runtime->m_commands=QSharedPointer<MobileStandaloneKritaActions>::create(
            runtime->m_actions,host);
        return runtime;
    }

    ~MobileKritaStandaloneRuntime() override
    {
        // KisView::~KisView() calls back into KisViewManager; never free
        // the manager or its action collection while a live native view
        // is still attached. The host's central widget may already be gone.
        if(m_root && m_manager) {
            // Include hidden/removed native views as well as those
            // with a still-live document identity. An unregistered
            // QWidget must not outlive the view manager either.
            const auto views=m_root->findChildren<KisView*>();
            for(KisView *view:views) {
                if(view && view->viewManager()==m_manager)
                    delete view;
            }
        }
        delete m_manager.data();
        delete m_actions.data();
    }

    KisViewManager *viewManager() const {return m_manager.data();}
    QSharedPointer<MobileCommandRegistryPort> commandPort() const
    {
        return m_commands;
    }
    MobileLightweightCanvasHost *window() const {return m_host.data();}
    MobileNativeViewStack *documentRoot() const {return m_root.data();}

    bool ownsDocument(KisDocument *document) const
    {
        if(!document || !m_root || !m_manager ||
           !KisPart::instance())return false;
        bool registered=false;
        for(const auto &candidate:KisPart::instance()->documents()) {
            if(candidate==document) {registered=true;break;}
        }
        if(!registered)return false;
        // A registered document may belong to a desktop fallback or a
        // second Qt window. Validate ownership against native view IDs.
        for(const auto &view:m_root->openViews()) {
            if(view.document==document && view.nativeView)return true;
        }
        return false;
    }

    KisView *attachDocument(KisDocument *document,
                            MobileKritaNativeSaveHost *saver)
    {
        if(!m_host || !m_root || !m_manager || !m_actions ||
           !document || !saver || !m_host->isVisible() ||
           !KisPart::instance())return nullptr;
        bool inPart=false;
        for(const auto &registered : KisPart::instance()->documents()) {
            if(registered==document) {inPart=true;break;}
        }
        if(!inPart)return nullptr;
        return MobileKritaNativeViewFactory::attach(
            document,m_manager,m_root,saver);
    }

private:
    MobileKritaStandaloneRuntime(MobileLightweightCanvasHost *host,
                                 MobileNativeViewStack *root)
        : QObject(host),m_host(host),m_root(root)
    {
        setObjectName(QStringLiteral("_mobileKritaRuntime"));
    }

    QPointer<MobileLightweightCanvasHost> m_host;
    QPointer<MobileNativeViewStack> m_root;
    QPointer<KisKActionCollection> m_actions;
    QPointer<KisViewManager> m_manager;
    QSharedPointer<MobileCommandRegistryPort> m_commands;
};
#endif
