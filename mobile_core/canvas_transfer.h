/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-only, reversible canvas widget transfer for Mobile Studio.
 * The native Krita MDI stack is never recreated. This module knows
 * QMainWindow/QMdiArea and the mobile destination, but does not depend
 * on KisMainWindow, KisDocument or the Krita painting engine.
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
#include "standalone_host.h"

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
        // The mobile host must not delete Krita's QWidget stack on exit.
        // A still-alive backend takes it back before the host is deleted.
        if(m_active)leave();
    }

    bool active() const {return m_active;}

    bool enter(MobileStandaloneWindow *mobile)
    {
        if(m_active || !m_backend || !mobile ||
           mobile->hasEngineCanvas())return false;
        QWidget *native=m_backend->takeCentralWidget();
        if(!native)return false;
        m_savedWindowState=m_backend->windowState();
        m_canvas=native;
        m_mobile=mobile;

        // Krita's Android refresh hack observes this property. Set it
        // BEFORE presenting the mobile host so queued load completions
        // cannot raise the now-empty desktop window over the canvas.
        m_backend->setProperty("_mobileStudioCanvasTransferred",true);
        mobile->attachEngineCanvas(native);
        if(!mobile->hasEngineCanvas()) {
            // No partial takeover: recover the exact original widget.
            m_backend->setCentralWidget(native);
            native->show();
            m_backend->setProperty("_mobileStudioCanvasTransferred",false);
            m_canvas=nullptr;
            m_mobile=nullptr;
            return false;
        }

        m_active=true;
        hideNativeTabs();
        m_tabTimer.start();
        mobile->showFullScreen();
        mobile->raise();
        mobile->activateWindow();
        m_backend->hide();
        mobile->refresh();
        return true;
    }

    void leave()
    {
        if(!m_active)return;
        m_tabTimer.stop();
        MobileStandaloneWindow *mobile=m_mobile;
        QWidget *native=mobile?mobile->releaseEngineCanvas():nullptr;
        // If the mobile slot unexpectedly lost its widget, recover the
        // original Qt object if it still exists. Never create a new
        // canvas or silently discard unsaved Krita documents here.
        if(!native && m_canvas) {
            native=m_canvas;
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
        if(mobile)mobile->hide();

        m_mobile=nullptr;
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
    QPointer<MobileStandaloneWindow> m_mobile;
    QPointer<QWidget> m_canvas;
    Qt::WindowStates m_savedWindowState=Qt::WindowNoState;
    QVector<TabSnapshot> m_tabs;
    QTimer m_tabTimer;
    bool m_active=false;
};
#endif // Q_OS_ANDROID
