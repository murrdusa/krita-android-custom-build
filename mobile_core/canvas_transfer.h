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
#include <QMdiArea>
#include <QPointer>
#include <QTabBar>
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
            if(m_active)hideNativeTabs();
        });
    }

    ~MobileCanvasTransfer()
    {
        if(m_active)leave();
    }

    bool active() const {return m_active;}

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
        QWidget *native=m_backend->takeCentralWidget();
        if(!native)return false;

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
                surface->detachNativeCanvas();
            native->hide();
            native->setParent(nullptr);
            m_backend->setCentralWidget(native);
            native->show();
            m_backend->setProperty("_mobileStudioCanvasTransferred",false);
            m_surface=nullptr;
            m_canvas=nullptr;
            m_mobileWindow=nullptr;
            return false;
        }

        m_active=true;
        hideNativeTabs();
        m_tabTimer.start();
        mobile->showFullScreen();
        mobile->raise();
        mobile->activateWindow();
        m_backend->hide();
        surface->refreshCanvasSurface();
        return true;
    }

    void leave()
    {
        if(!m_active)return;
        m_tabTimer.stop();

        MobileCanvasSurface *surface=m_mobileWindow?m_surface:nullptr;
        QWidget *detached=surface?surface->detachNativeCanvas():nullptr;
        // The destination is not authoritative about which canvas Krita
        // owns. Never install an unrelated widget as the backend's central
        // widget, even if a future surface implementation returns one.
        QWidget *native=m_canvas.data();
        if(native && detached!=native) {
            // Recovery also covers a surface that reports no widget after
            // having reparented the original native canvas.
            native->hide();
            native->setParent(nullptr);
        }
        if(m_backend && native) {
            m_backend->setCentralWidget(native);
            native->show();
        }
        restoreNativeTabs();

        if(m_backend) {
            m_backend->setProperty("_mobileStudioCanvasTransferred",false);
            m_backend->setWindowState(m_savedWindowState);
            m_backend->show();
            m_backend->raise();
            m_backend->activateWindow();
        }
        if(m_mobileWindow)m_mobileWindow->hide();

        m_surface=nullptr;
        m_mobileWindow=nullptr;
        m_canvas=nullptr;
        m_active=false;
    }

private:
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
    QTimer m_tabTimer;
    bool m_active=false;
};
#endif // Q_OS_ANDROID
