/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-only, reversible canvas widget transfer.
 * The native Krita MDI QWidget stack is moved, never copied or
 * recreated. The destination is a generic MobileCanvasSurface.
 * No Krita types or Mobile Studio window implementation are included.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QMainWindow>
#include <QDebug>
#include <QDockWidget>
#include <QMenuBar>
#include <QMdiArea>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include "canvas_surface.h"

class MobileCanvasTransfer final
{
public:
    MobileCanvasTransfer(QMainWindow *backend,QMdiArea *documents)
        : m_backend(backend),m_documents(documents)
    {
        m_tabTimer.setInterval(750);
        QObject::connect(&m_tabTimer,&QTimer::timeout,&m_tabTimer,
                         [this]() {
            if(m_active) {
                hideNativeTabs();
                enforceMobileChrome();
            }
        });
    }

    ~MobileCanvasTransfer()
    {
        if(m_active)leave();
    }

    bool active() const {return m_active;}

    // The native Krita QMainWindow remains the only Android top-level.
    // Called after deferred desktop chrome changes from Krita's old skin.
    void enforceMobileChrome()
    {
        if(!m_active)return;
        for(const ChromeSnapshot &saved:m_chrome) {
            if(saved.widget && saved.widget->isVisible())
                saved.widget->hide();
        }
    }

    bool enter(MobileCanvasSurface *surface)
    {
        if(m_active || !surface || !m_backend || !m_documents ||
           surface->hasNativeCanvas())return false;
        QWidget *mobile=surface->canvasSurfaceWindow();
        if(!mobile || mobile==m_backend)return false;
        // Guard the backend's original central widget before changing
        // ownership. No document or view gets instantiated by this path.
        if(!m_backend->centralWidget())return false;

        m_savedWindowState=m_backend->windowState();
        rememberBackendChrome();
        // Make Mobile Studio a child presentation widget before moving
        // the real painting canvas. Only Krita's existing QMainWindow
        // owns an Android native window / GL compositor surface.
        mobile->hide();
        mobile->setParent(m_backend,Qt::Widget);
        QWidget *native=m_backend->takeCentralWidget();
        if(!native) {
            mobile->setParent(nullptr);
            m_chrome.clear();
            return false;
        }

        m_canvas=native;
        m_surface=surface;
        m_mobileWindow=mobile;

        // Krita's Android refresh workaround observes this Qt property.
        // It must be set before the host exposes the native canvas.
        m_backend->setProperty("_mobileStudioCanvasTransferred",true);

        if(!surface->attachNativeCanvas(native) ||
           !surface->ownsNativeCanvas(native)) {
            // An incomplete attachment must leave desktop Krita usable.
            // Never detach an unrelated canvas from the destination.
            if(surface->ownsNativeCanvas(native))
                surface->detachNativeCanvas(m_backend);
            native->hide();
            if(native->parentWidget()!=m_backend)
                native->setParent(m_backend);
            m_backend->setCentralWidget(native);
            native->show();
            mobile->hide();
            mobile->setParent(nullptr);
            m_chrome.clear();
            m_backend->setProperty("_mobileStudioCanvasTransferred",false);
            m_surface=nullptr;
            m_canvas=nullptr;
            m_mobileWindow=nullptr;
            return false;
        }

        // The native window stays visible throughout. Replacing its
        // central presentation widget avoids Android full-screen window
        // overlap and avoids reparenting the GL canvas across top-levels
        // on every Back/enter cycle.
        m_backend->setCentralWidget(mobile);
        m_active=true;
        hideNativeTabs();
        m_tabTimer.start();
        enforceMobileChrome();
        mobile->show();
        mobile->raise();
        m_backend->show();
        m_backend->activateWindow();
        surface->refreshCanvasSurface();
        // A delayed desktop toolbar restoration may follow the toggle.
        QPointer<QMainWindow> backend=m_backend;
        QTimer::singleShot(320,m_backend,[this,backend]() {
            if(backend && m_active)enforceMobileChrome();
        });
        return true;
    }

    bool leave()
    {
        if(!m_active)return true;
        // A second central widget can be installed by deferred native
        // startup or an unexpected Krita window rebuild. QMainWindow's
        // setCentralWidget() would DELETE it. Refuse to discard another
        // canvas, even when the UI is asking to return to desktop.
        if(!m_backend || !m_canvas || !m_mobileWindow || !m_surface ||
           m_backend->centralWidget()!=m_mobileWindow) {
            qWarning() << "Mobile Studio: rejected embedded handoff; canvas"
                       << "or mobile host missing, or backend central widget replaced";
            return false;
        }
        m_tabTimer.stop();

        MobileCanvasSurface *surface=m_surface;
        QWidget *mobile=m_mobileWindow;
        // Never let Qt delete Mobile Studio while it still owns the native
        // painting canvas. The canvas is reparented directly to the shared
        // Krita top-level before replacing the central presentation widget.
        QWidget *native=m_canvas.data();
        mobile->hide();
        QWidget *detached=surface->detachNativeCanvas(m_backend);
        if(detached!=native || surface->ownsNativeCanvas(native)) {
            qWarning() << "Mobile Studio: incorrect canvas returned; keeping host";
            if(native && native->parentWidget()==m_backend)
                surface->attachNativeCanvas(native);
            mobile->show();
            m_tabTimer.start();
            return false;
        }
        QWidget *removed=m_backend->takeCentralWidget();
        if(removed!=mobile) {
            qWarning() << "Mobile Studio: backend presentation changed mid-handoff";
            if(removed)m_backend->setCentralWidget(removed);
            surface->attachNativeCanvas(native);
            mobile->show();
            m_tabTimer.start();
            return false;
        }
        m_backend->setCentralWidget(native);
        restoreNativeTabs();
        restoreBackendChrome();

        if(m_backend) {
            m_backend->setProperty("_mobileStudioCanvasTransferred",false);
            m_backend->show();
            native->show();
            m_backend->activateWindow();
            // Returning the native widget changes its top-level window.
            // Let Qt finish the Android surface/MDI layout pass before
            // requesting its repaint. This does not create a view.
            const QPointer<QWidget> restored=native;
            const QPointer<QMainWindow> backend=m_backend;
            const QPointer<QMdiArea> documents=m_documents;
            QTimer::singleShot(0,m_backend,[restored,backend,documents]() {
                // A rapid second toggle may already have moved this
                // widget back into Mobile Studio. Never paint an old
                // desktop surface after ownership changed again.
                if(!restored || !backend ||
                   backend->centralWidget()!=restored)return;
                restored->show();
                restored->updateGeometry();
                restored->update();
                if(documents) {
                    documents->updateGeometry();
                    documents->update();
                    if(documents->viewport())
                        documents->viewport()->update();
                }
            });
        }

        m_surface=nullptr;
        m_mobileWindow=nullptr;
        m_canvas=nullptr;
        m_active=false;
        return true;
    }

private:
    struct ChromeSnapshot {
        QPointer<QWidget> widget;
        bool wasVisible=false;
    };

    void rememberBackendChrome()
    {
        m_chrome.clear();
        if(!m_backend)return;
        const auto remember=[this](QWidget *widget) {
            if(!widget)return;
            for(const ChromeSnapshot &entry:m_chrome)
                if(entry.widget==widget)return;
            m_chrome.append({widget,widget->isVisible()});
        };
        remember(m_backend->menuBar());
        remember(m_backend->statusBar());
        // Snapshot before embedding the mobile host so nested mobile
        // controls are never confused with Krita's legacy desktop chrome.
        for(QToolBar *bar:m_backend->findChildren<QToolBar*>())
            remember(bar);
        for(QDockWidget *dock:m_backend->findChildren<QDockWidget*>())
            remember(dock);
    }

    void restoreBackendChrome()
    {
        for(const ChromeSnapshot &entry:m_chrome)
            if(entry.widget)entry.widget->setVisible(entry.wasVisible);
        m_chrome.clear();
    }

    struct TabSnapshot {
        QPointer<QTabBar> bar;
        int minimumHeight=0;
        int maximumHeight=0;
        bool wasVisible=false;
    };

    void hideNativeTabs()
    {
        if(!m_documents)return;
        for(QTabBar *bar:m_documents->findChildren<QTabBar*>()) {
            if(!bar)continue;
            bool seen=false;
            for(const TabSnapshot &saved:m_tabs)
                if(saved.bar==bar){seen=true;break;}
            if(!seen)
                m_tabs.append({bar,bar->minimumHeight(),
                               bar->maximumHeight(),bar->isVisible()});
            if(bar->minimumHeight()!=0)bar->setMinimumHeight(0);
            if(bar->maximumHeight()!=0)bar->setMaximumHeight(0);
            if(bar->isVisible())bar->hide();
        }
    }
    void restoreNativeTabs()
    {
        for(const TabSnapshot &saved:m_tabs) {
            if(!saved.bar)continue;
            saved.bar->setMaximumHeight(saved.maximumHeight);
            saved.bar->setMinimumHeight(saved.minimumHeight);
            saved.bar->setVisible(saved.wasVisible);
        }
        m_tabs.clear();
    }

    QPointer<QMainWindow> m_backend;
    QPointer<QMdiArea> m_documents;
    QPointer<QWidget> m_mobileWindow;
    QPointer<QWidget> m_canvas;
    MobileCanvasSurface *m_surface=nullptr; // Lifetime guarded by m_mobileWindow
    Qt::WindowStates m_savedWindowState=Qt::WindowNoState;
    QVector<TabSnapshot> m_tabs;
    QVector<ChromeSnapshot> m_chrome;
    QTimer m_tabTimer;
    bool m_active=false;
};
#endif // Q_OS_ANDROID
