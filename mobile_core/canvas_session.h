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
#include <QApplication>
#include <QSettings>
#include <QTimer>
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
        // Opt-in mobile-first startup: Krita still initializes services
        // before this session takes ownership of its canvas widget.
        // A pending marker prevents repeated launch crashes from trapping
        // users in Mobile Studio; the next launch falls back to Krita.
        QTimer::singleShot(0,this,[this]() {
            if(!m_main || !m_entry || !qApp ||
               qApp->property("_mobileStudioBootHandled").toBool())return;
            qApp->setProperty("_mobileStudioBootHandled",true);
            QSettings settings;
            if(!settings.value(
                    QStringLiteral("MobileStudio/startDirectly"),false)
                    .toBool())return;
            if(settings.value(
                    QStringLiteral("MobileStudio/bootPending"),false)
                    .toBool()) {
                settings.setValue(
                    QStringLiteral("MobileStudio/startDirectly"),false);
                settings.setValue(
                    QStringLiteral("MobileStudio/bootPending"),false);
                settings.sync();
                return;
            }
            settings.setValue(
                QStringLiteral("MobileStudio/bootPending"),true);
            settings.sync();
            m_entry->setChecked(true);
            QTimer::singleShot(4500,this,[this]() {
                QSettings verified;
                verified.setValue(
                    QStringLiteral("MobileStudio/bootPending"),false);
                if(!m_inMobile)
                    verified.setValue(
                        QStringLiteral("MobileStudio/startDirectly"),false);
                verified.sync();
            });
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
