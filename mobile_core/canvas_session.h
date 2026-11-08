/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio transition controller: startup and mode switching only.
 * The Qt-only transfer service owns the original native canvas widget
 * and MDI tab chrome, while MobileEngineBridge supplies Krita services.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QObject>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QSignalBlocker>
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QTimer>
#include "engine_bridge.h"
#include "canvas_transfer.h"

class MobileAppController final : public QObject
{
public:
    MobileAppController(QMainWindow *backend,
                        QMdiArea *mdi,
                        const MobileEngineBridge &bridge,
                        QAction *entry)
        : QObject(backend),m_transfer(backend,mdi),m_bridge(bridge),
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
            if(!m_entry || !qApp ||
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
                if(!m_transfer.active())
                    verified.setValue(
                        QStringLiteral("MobileStudio/startDirectly"),false);
                verified.sync();
            });
        });
    }
    ~MobileAppController() override
    {
        if(m_transfer.active())leave();
        delete m_host;
    }

private:
    void enter()
    {
        if(m_transfer.active())return;
        if(!m_host) {
            m_host=new MobileStandaloneWindow(m_bridge);
            m_host->setExitCallback([this]() {
                if(m_entry)m_entry->setChecked(false);
            });
        }
        if(!m_transfer.enter(m_host) && m_entry) {
            // Never report Mobile Studio active after a failed transfer.
            const QSignalBlocker blocked(m_entry);
            m_entry->setChecked(false);
        }
    }
    void leave()
    {
        m_transfer.leave();
    }
    MobileCanvasTransfer m_transfer;
    MobileEngineBridge m_bridge;
    QPointer<QAction> m_entry;
    MobileStandaloneWindow *m_host=nullptr;
};

#endif // Q_OS_ANDROID
