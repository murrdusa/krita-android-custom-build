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
#include <QEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QMdiArea>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QTimer>
#include "engine_bridge.h"
#include "canvas_transfer.h"
#include "standalone_host.h"

class MobileAppController final : public QObject
{
public:
    MobileAppController(QMainWindow *backend,
                        QMdiArea *mdi,
                        const MobileEngineBridge &bridge,
                        QAction *entry)
        : QObject(backend),m_transfer(backend,mdi),m_backend(backend),
          m_bridge(bridge),m_entry(entry)
    {
        if(m_backend)m_backend->installEventFilter(this);
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
        // Do not destroy the mobile top-level (and its child canvas) if
        // the desktop handoff refused to replace an occupied widget.
        // During shutdown it is safer to let the OS reclaim this window
        // than to silently delete a modified KisDocument's view.
        if(m_transfer.active() && !m_transfer.leave()) {
            // The embedded host is a child of Krita's native window.
            // If an unsafe handoff is rejected, detach that host from the
            // QObject parent tree before the backend destroys its children;
            // otherwise shutdown would still delete the live native canvas.
            // No document is closed or silently discarded by this path.
            if(m_host)m_host->setParent(nullptr);
            m_host=nullptr;
            return;
        }
        delete m_host.data();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        // Mobile Studio is now a child presentation inside Krita's only
        // Android native window. Do NOT hide the backend here: doing so
        // would hide the canvas and mobile controls together. Reassert
        // only the hidden desktop toolbars/status after deferred shows.
        if(watched==m_backend && m_transfer.active() &&
           event->type()==QEvent::Show) {
            QTimer::singleShot(0,this,[this]() {
                if(!m_transfer.active() || !m_backend)return;
                m_transfer.enforceMobileChrome();
                if(m_host && !m_host->isVisible())m_host->show();
            });
        }
        return QObject::eventFilter(watched,event);
    }

private:
    void enter()
    {
        // Modal file dialogs and deferred Compact UI callbacks can toggle
        // the launcher while the native central widget is being moved.
        // Never run two ownership transactions on the same event loop.
        if(m_transitioning) {
            if(m_entry) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(m_transfer.active());
            }
            return;
        }
        if(m_transfer.active())return;
        const QScopedValueRollback<bool> transition(m_transitioning,true);
        if(!m_host) {
            m_host=new MobileStandaloneWindow(m_bridge);
            m_host->setExitCallback([this]() {
                if(m_entry)m_entry->setChecked(false);
            });
        }
        if(!m_transfer.enter(m_host)) {
            // Never leave the launcher checked after a rejected transfer;
            // discard any temporary host that owns no native canvas.
            if(m_entry) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(false);
            }
            if(m_host && !m_host->hasNativeCanvas()) {
                m_host->hide();
                m_host->deleteLater();
                m_host=nullptr;
            }
        }
    }
    void leave()
    {
        if(m_transitioning) {
            if(m_entry) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(m_transfer.active());
            }
            return;
        }
        const QScopedValueRollback<bool> transition(m_transitioning,true);
        if(m_transfer.leave()) {
            // Recreate the UI host on the next entry. A former independent
            // Android window must not remain behind the Krita surface and
            // expose an old toolbar after Back. defer deletion because Back
            // may have been invoked from a child QToolButton's click slot.
            if(m_host) {
                m_host->hide();
                m_host->deleteLater();
                m_host=nullptr;
            }
            return;
        }
        // Leave the working mobile canvas in place on a rejected
        // transfer. Keep the launcher state in sync without recursively
        // reentering the mode-switch handler.
        if(m_entry) {
            const QSignalBlocker blocked(m_entry);
            m_entry->setChecked(true);
        }
        if(m_host) {
            QMessageBox::warning(m_host,
                QObject::tr("Canvas still in Mobile Studio"),
                QObject::tr("Krita could not safely restore the original "
                            "canvas because its desktop window changed. "
                            "Your drawing remains in Mobile Studio. "
                            "Save it before trying to switch again."));
        }
    }
    MobileCanvasTransfer m_transfer;
    QPointer<QMainWindow> m_backend;
    MobileEngineBridge m_bridge;
    QPointer<QAction> m_entry;
    // QMainWindow owns this while embedded as the central widget. A
    // deferred legacy layout change may destroy that widget without the
    // controller being notified; QPointer prevents a stale Back handler
    // from dereferencing the destroyed mobile presentation.
    QPointer<MobileStandaloneWindow> m_host;
    bool m_transitioning=false;
};

#endif // Q_OS_ANDROID
