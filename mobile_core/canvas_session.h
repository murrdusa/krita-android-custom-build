/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Temporary Krita-backend canvas session.
 *
 * This is the ONLY mobile controller that transfers Krita's live desktop
 * widget stack to/from our mobile top-level window. When Krita's startup
 * lifecycle becomes independent, this component is replaced; the mobile
 * workspace, document UI, brush and layers are unaffected.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QObject>
#include <QMdiArea>
#include <QPointer>
#include <QSignalBlocker>
#include <QWidget>
#include <QAction>
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "engine_bridge.h"
#include "standalone_host.h"

class MobileAppController final : public QObject
{
public:
    MobileAppController(KisMainWindow *main,
                        QMdiArea *mdi,
                        KisViewManager *manager,
                        QAction *entry)
        : QObject(main),m_main(main),m_mdi(mdi),m_bridge(main,manager,mdi),
          m_entry(entry)
    {
        connect(entry,&QAction::toggled,this,[this](bool value) {
            if(value)enter();else leave();
        });
    }
    ~MobileAppController() override
    {
        if(m_inMobile)leave();
        delete m_host;
    }

private:
    void enter()
    {
        if(m_inMobile || !m_main)return;
        if(!m_host) {
            m_host=new MobileStandaloneWindow(m_bridge);
            m_host->setExitCallback([this]() {
                if(m_entry)m_entry->setChecked(false);
            });
        }
        QWidget *native=m_main->takeCentralWidget();
        if(!native) {
            if(m_entry) {
                const QSignalBlocker block(m_entry);
                m_entry->setChecked(false);
            }
            return;
        }
        m_originalState=m_main->windowState();
        m_originalCentral=native;
        m_host->attachEngineCanvas(native);
        m_inMobile=true;
        m_host->showFullScreen();
        m_host->raise();
        m_host->activateWindow();
        m_main->hide();
        m_host->refresh();
    }
    void leave()
    {
        if(!m_inMobile || !m_main || !m_host)return;
        QWidget *native=m_host->releaseEngineCanvas();
        if(native) {
            m_main->setCentralWidget(native);
            native->show();
        }
        // Main window is a temporary backend only in mobile mode.
        // Preserve the original desktop/compact state on return.
        m_main->setWindowState(m_originalState);
        m_main->show();
        m_main->raise();
        m_host->hide();
        m_main->activateWindow();
        m_inMobile=false;
        m_originalCentral=nullptr;
    }
    QPointer<KisMainWindow> m_main;
    QPointer<QMdiArea> m_mdi;
    MobileEngineBridge m_bridge;
    QPointer<QAction> m_entry;
    MobileStandaloneWindow *m_host=nullptr;
    QPointer<QWidget> m_originalCentral;
    Qt::WindowStates m_originalState;
    bool m_inMobile=false;
};

#endif // Q_OS_ANDROID
