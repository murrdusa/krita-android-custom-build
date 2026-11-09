/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * No-canvas-move Mobile Studio architecture prototype.
 *
 * Unlike MobileCanvasTransfer, this adapter NEVER calls setCentralWidget,
 * takeCentralWidget or setParent on Krita's native painting widget.
 * Mobile controls occupy ordinary dock areas around the existing central
 * widget of Krita's one Android QMainWindow. No Krita types are required.
 *
 * The service is intentionally opt-in at the integration layer; this file
 * has no startup, document, painting or touch-input side effects.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QVector>
#include <QWidget>

class MobileNativeCanvasChrome final
{
public:
    explicit MobileNativeCanvasChrome(QMainWindow *backend)
        : m_backend(backend)
    {
        m_guard.setInterval(750);
        QObject::connect(&m_guard,&QTimer::timeout,&m_guard,
                         [this]() {if(m_active)hideDesktopChrome();});
    }

    ~MobileNativeCanvasChrome()
    {
        if(m_active)deactivate();
        // These are owned by Krita's QMainWindow. Do not destroy the
        // native central canvas or unrelated dockers.
        delete m_topDock.data();
        delete m_bottomDock.data();
    }

    bool install(QWidget *topPanel,QWidget *bottomPanel)
    {
        if(m_installed || !m_backend || !topPanel || !bottomPanel ||
           topPanel==bottomPanel || !m_backend->centralWidget())
            return false;
        if(topPanel->isAncestorOf(m_backend->centralWidget()) ||
           bottomPanel->isAncestorOf(m_backend->centralWidget()))
            return false;

        m_nativeCanvas=m_backend->centralWidget();
        // Capture only native controls *before* installing mobile docks.
        // No canvas parenting or central-layout replacement occurs.
        snapshotDesktopChrome();
        m_topDock=new QDockWidget(m_backend);
        m_bottomDock=new QDockWidget(m_backend);
        m_topDock->setObjectName(
            QStringLiteral("MobileStudioNativeTopDock"));
        m_bottomDock->setObjectName(
            QStringLiteral("MobileStudioNativeBottomDock"));
        m_topDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        m_bottomDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        m_topDock->setAllowedAreas(Qt::TopDockWidgetArea);
        m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea);
        // Empty title bars reserve no unnecessary desktop chrome.
        m_topDock->setTitleBarWidget(new QWidget(m_topDock));
        m_bottomDock->setTitleBarWidget(new QWidget(m_bottomDock));
        m_topDock->setWidget(topPanel);
        m_bottomDock->setWidget(bottomPanel);
        m_backend->addDockWidget(Qt::TopDockWidgetArea,m_topDock);
        m_backend->addDockWidget(Qt::BottomDockWidgetArea,m_bottomDock);
        m_topDock->hide();
        m_bottomDock->hide();

        m_installed=true;
        return ownsUnmovedCanvas();
    }

    bool ownsUnmovedCanvas() const
    {
        return m_installed && m_backend && m_nativeCanvas &&
               m_backend->centralWidget()==m_nativeCanvas &&
               m_nativeCanvas->window()==m_backend;
    }

    bool active() const {return m_active;}

    bool activate()
    {
        if(m_active)return ownsUnmovedCanvas();
        if(!ownsUnmovedCanvas() || !m_topDock || !m_bottomDock)
            return false;
        // Capture current visible native chrome, rather than relying on
        // the state from when the app first installed this adapter.
        snapshotDesktopChrome();
        snapshotTabs();
        m_active=true;
        hideDesktopChrome();
        hideTabs();
        m_topDock->show();
        m_bottomDock->show();
        m_guard.start();
        scheduleCanvasUpdate();
        return true;
    }

    bool deactivate()
    {
        if(!m_active)return true;
        const bool originalCanvasStillPresent=ownsUnmovedCanvas();
        // Krita may replace its document stack while Mobile Studio is
        // visible. Never touch or overwrite that replacement. Always
        // restore the surrounding desktop controls and hide our own
        // docks so the user can recover via normal Krita UI.
        m_guard.stop();
        if(m_topDock)m_topDock->hide();
        if(m_bottomDock)m_bottomDock->hide();
        restoreTabs();
        restoreDesktopChrome();
        m_active=false;
        if(originalCanvasStillPresent)scheduleCanvasUpdate();
        return originalCanvasStillPresent;
    }

    void enforceMobileChrome()
    {
        if(m_active && ownsUnmovedCanvas()) {
            hideDesktopChrome();
            hideTabs();
        }
    }

private:
    struct ChromeSnapshot {
        QPointer<QWidget> widget;
        bool visible=false;
    };
    struct TabSnapshot {
        QPointer<QTabBar> widget;
        int minHeight=0;
        int maxHeight=0;
        bool visible=false;
    };

    void snapshotDesktopChrome()
    {
        m_chrome.clear();
        if(!m_backend)return;
        const auto store=[this](QWidget *widget) {
            if(!widget || widget==m_topDock || widget==m_bottomDock)
                return;
            for(const ChromeSnapshot &saved:m_chrome)
                if(saved.widget==widget)return;
            m_chrome.append({widget,widget->isVisible()});
        };
        store(m_backend->menuBar());
        store(m_backend->statusBar());
        for(QToolBar *widget:m_backend->findChildren<QToolBar*>())
            store(widget);
        for(QDockWidget *widget:m_backend->findChildren<QDockWidget*>())
            store(widget);
    }

    void hideDesktopChrome()
    {
        for(const ChromeSnapshot &saved:m_chrome)
            if(saved.widget && saved.widget->isVisible())
                saved.widget->hide();
    }

    void restoreDesktopChrome()
    {
        for(const ChromeSnapshot &saved:m_chrome)
            if(saved.widget)saved.widget->setVisible(saved.visible);
        m_chrome.clear();
    }

    void snapshotTabs()
    {
        m_tabs.clear();
        if(!m_nativeCanvas)return;
        for(QTabBar *bar:m_nativeCanvas->findChildren<QTabBar*>())
            m_tabs.append({bar,bar->minimumHeight(),
                           bar->maximumHeight(),bar->isVisible()});
    }

    void hideTabs()
    {
        for(const TabSnapshot &saved:m_tabs) {
            if(!saved.widget)continue;
            saved.widget->setMinimumHeight(0);
            saved.widget->setMaximumHeight(0);
            saved.widget->hide();
        }
    }

    void restoreTabs()
    {
        for(const TabSnapshot &saved:m_tabs) {
            if(!saved.widget)continue;
            saved.widget->setMaximumHeight(saved.maxHeight);
            saved.widget->setMinimumHeight(saved.minHeight);
            saved.widget->setVisible(saved.visible);
        }
        m_tabs.clear();
    }

    void scheduleCanvasUpdate()
    {
        const QPointer<QMainWindow> backend=m_backend;
        const QPointer<QWidget> canvas=m_nativeCanvas;
        QTimer::singleShot(0,&m_guard,[backend,canvas]() {
            if(!backend || !canvas ||
               backend->centralWidget()!=canvas)return;
            canvas->updateGeometry();
            canvas->update();
        });
    }

    QPointer<QMainWindow> m_backend;
    QPointer<QWidget> m_nativeCanvas;
    QPointer<QDockWidget> m_topDock;
    QPointer<QDockWidget> m_bottomDock;
    QVector<ChromeSnapshot> m_chrome;
    QVector<TabSnapshot> m_tabs;
    QTimer m_guard;
    bool m_installed=false;
    bool m_active=false;
};
#endif // Q_OS_ANDROID || KRITA_MOBILE_QT_TEST
