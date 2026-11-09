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
                         [this]() {enforceMobileChrome();});
    }

    ~MobileNativeCanvasChrome()
    {
        if(m_active)deactivate();
        // These are owned by Krita's QMainWindow. Do not destroy the
        // native central canvas or unrelated dockers.
        delete m_topDock.data();
        delete m_bottomDock.data();
        delete m_sideDock.data();
    }

    bool install(QWidget *topPanel,QWidget *bottomPanel,
                 QWidget *sidePanel=nullptr)
    {
        if(m_installed || !m_backend || !topPanel || !bottomPanel ||
           topPanel==bottomPanel || !m_backend->centralWidget())
            return false;
        if(topPanel->isAncestorOf(m_backend->centralWidget()) ||
           bottomPanel->isAncestorOf(m_backend->centralWidget()) ||
           (sidePanel &&
            (sidePanel==topPanel || sidePanel==bottomPanel ||
             sidePanel->isAncestorOf(m_backend->centralWidget()))))
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
        if(sidePanel) {
            m_sideDock=new QDockWidget(m_backend);
            m_sideDock->setObjectName(
                QStringLiteral("MobileStudioNativeSideDock"));
            m_sideDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
            m_sideDock->setAllowedAreas(
                Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea|
                Qt::BottomDockWidgetArea);
            m_sideDock->setTitleBarWidget(new QWidget(m_sideDock));
            m_sideDock->setWidget(sidePanel);
            m_backend->addDockWidget(Qt::RightDockWidgetArea,m_sideDock);
            m_sideDock->hide();
        }

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
        if(m_sideDock)m_sideDock->hide();
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
        if(m_sideDock)m_sideDock->hide();
        restoreTabs();
        restoreDesktopChrome();
        m_active=false;
        if(originalCanvasStillPresent)scheduleCanvasUpdate();
        return originalCanvasStillPresent;
    }

    bool setBottomPanelVisible(bool visible)
    {
        if(!m_active || !ownsUnmovedCanvas() || !m_bottomDock)return false;
        if(m_bottomDock->isVisible()!=visible) {
            m_bottomDock->setVisible(visible);
            scheduleCanvasUpdate();
        }
        return true;
    }

    bool bottomPanelVisible() const
    {
        return m_active && m_bottomDock && m_bottomDock->isVisible();
    }

    bool setSidePanelVisible(bool visible)
    {
        if(!m_active || !ownsUnmovedCanvas() || !m_sideDock)return false;
        if(visible)placeSidePanelForViewport();
        m_sideDock->setVisible(visible);
        scheduleCanvasUpdate();
        return true;
    }

    bool sidePanelVisible() const
    {
        return m_active && m_sideDock && m_sideDock->isVisible();
    }

    void enforceMobileChrome()
    {
        if(m_active && ownsUnmovedCanvas()) {
            // Krita can add new dockers or replace its MDI tabbar when
            // opening/closing views. Include late widgets in the original
            // visibility snapshot instead of leaving them on-screen.
            captureAdditionalDesktopChrome();
            captureAdditionalTabs();
            if(m_sideDock && m_sideDock->isVisible())
                placeSidePanelForViewport();
            hideDesktopChrome();
            hideTabs();
        }
    }

private:
    // Portrait phones often have less than 650 logical px of width.
    // Krita's existing layer panel needs ~268 px; docking it at the
    // right would collapse the actual paint area. Move only the mobile
    // layers docker to the bottom on narrow screens. The Krita canvas
    // itself is never reparented, recreated or exchanged.
    void placeSidePanelForViewport()
    {
        if(!m_backend || !m_sideDock || !ownsUnmovedCanvas())return;
        const bool narrow=m_backend->width()<650;
        const Qt::DockWidgetArea desired=narrow?
            Qt::BottomDockWidgetArea:Qt::RightDockWidgetArea;
        if(m_backend->dockWidgetArea(m_sideDock)==desired) {
            if(narrow)
                m_sideDock->setMaximumHeight(
                    qMax(190,qMin(340,m_backend->height()/2)));
            return;
        }
        // A bottom layer panel is capped so it cannot take the whole
        // portrait canvas viewport after the brush dock is laid out.
        if(narrow) {
            m_sideDock->setMaximumHeight(
                qMax(190,qMin(340,m_backend->height()/2)));
        } else {
            m_sideDock->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        m_backend->addDockWidget(desired,m_sideDock);
        scheduleCanvasUpdate();
    }

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
        captureAdditionalDesktopChrome();
    }

    void captureAdditionalDesktopChrome()
    {
        if(!m_backend)return;
        const auto store=[this](QWidget *widget) {
            if(!widget || widget==m_topDock || widget==m_bottomDock ||
               widget==m_sideDock)return;
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
        captureAdditionalTabs();
    }

    void captureAdditionalTabs()
    {
        if(!m_nativeCanvas)return;
        for(QTabBar *bar:m_nativeCanvas->findChildren<QTabBar*>()) {
            bool known=false;
            for(const TabSnapshot &saved:m_tabs) {
                if(saved.widget==bar) {known=true;break;}
            }
            if(!known)
                m_tabs.append({bar,bar->minimumHeight(),
                               bar->maximumHeight(),bar->isVisible()});
        }
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
    QPointer<QDockWidget> m_sideDock;
    QVector<ChromeSnapshot> m_chrome;
    QVector<TabSnapshot> m_tabs;
    QTimer m_guard;
    bool m_installed=false;
    bool m_active=false;
};
#endif // Q_OS_ANDROID || KRITA_MOBILE_QT_TEST
