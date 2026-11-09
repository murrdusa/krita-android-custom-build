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
#include <QKeyEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QTimer>
#include <memory>
#include "engine_bridge.h"
#include "mobile_startup_policy.h"
#include "android_back_policy.h"
#include "canvas_transfer.h"
#include "native_canvas_chrome.h" // Qt-only stationary canvas UI architecture
#include "native_canvas_pilot.h"  // Optional no-reparent Mobile Studio UI
#include "standalone_host.h"

class MobileAppController final : public QObject
{
public:
    MobileAppController(QMainWindow *backend,
                        QWidget *documentRoot,
                        const MobileEngineBridge &bridge,
                        QAction *entry)
        : QObject(backend),m_transfer(backend,documentRoot),m_backend(backend),
          m_bridge(bridge),m_entry(entry),
          m_nativeCanvasMode(QSettings().value(
              QStringLiteral("MobileStudio/nativeDockCanvasV1"),
              false).toBool())
    {
        // QApplication receives key events sent to focused children of
        // Krita's main window. Filtering only QMainWindow misses Android
        // Back when focus is inside a mobile toolbar, layer or canvas.
        if(qApp)qApp->installEventFilter(this);
        connect(entry,&QAction::toggled,this,[this](bool value) {
            if(value)enter();else leave();
        });
        // Wait until Krita has a visible canvas before requesting
        // Mobile Studio. A previous interrupted startup still falls
        // back safely to desktop Krita, with a manual Settings entry.
        QTimer::singleShot(0,this,[this]() {
            if(!m_entry || !qApp ||
               qApp->property("_mobileStudioBootHandled").toBool())return;
            qApp->setProperty("_mobileStudioBootHandled",true);
            QSettings settings;
            const auto decision=MobileStartupPolicy::begin(settings);
            if(decision==MobileStartupPolicy::Decision::NoAutostart)
                return;
            if(decision==MobileStartupPolicy::Decision::RecoverNativeKrita) {
                qWarning() << "Mobile Studio: incomplete previous boot;"
                           << "manual recovery available in Settings";
                return;
            }
            QTimer::singleShot(250,this,[this]() {
                attemptAutomaticStartup(12);
            });
        });
    }
    ~MobileAppController() override
    {
        if(qApp)qApp->removeEventFilter(this);
        // Native dock mode owns only its mobile chrome, not the canvas.
        // Destruction of the pilot never destroys Krita's central widget.
        if(m_nativePilot) {
            m_nativePilot->leave();
            m_nativePilot.reset();
        }
        if(m_transfer.persistentHostInstalled()) {
            // The persistent central widget and native Krita canvas are
            // children of the backend QMainWindow. Leave teardown to
            // Krita's normal window lifecycle and save prompts.
            if(m_transfer.active())m_transfer.leave();
            m_host=nullptr;
            return;
        }
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
        // Android system Back (Qt::Key_Back) can arrive at any focused
        // child, not necessarily Krita's main window. Preserve native
        // Save/Discard/Cancel dialogs and menus: they handle their own
        // Back events. Never intercept Escape (used by Krita tools).
        if(event && (event->type()==QEvent::KeyPress ||
                     event->type()==QEvent::KeyRelease)) {
            auto *key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Back) {
                if(m_consumeBackRelease &&
                   event->type()==QEvent::KeyRelease) {
                    m_consumeBackRelease=false;
                    key->accept();
                    return true;
                }
                if(MobileAndroidBackPolicy::mayDismiss(
                       m_backend,watched,event->type(),key->key(),
                       key->isAutoRepeat(),sessionActive(),m_transitioning,
                       qApp && qApp->activeModalWidget(),
                       qApp && qApp->activePopupWidget())) {
                    bool dismissed=false;
                    if(m_nativePilot && m_nativePilot->active())
                        dismissed=m_nativePilot->dismissTransientControls();
                    else if(m_host)
                        dismissed=m_host->dismissTransientControls();
                    if(!dismissed && m_entry)
                        m_entry->setChecked(false);
                    m_consumeBackRelease=true;
                    key->accept();
                    return true;
                }
            }
        }
        // Mobile Studio is now a child presentation inside Krita's only
        // Android native window. Do NOT hide the backend here: doing so
        // would hide the canvas and mobile controls together. Reassert
        // only the hidden desktop toolbars/status after deferred shows.
        if(watched==m_backend && sessionActive() &&
           event->type()==QEvent::Show) {
            QTimer::singleShot(0,this,[this]() {
                if(!sessionActive() || !m_backend)return;
                if(m_nativePilot && m_nativePilot->active()) {
                    m_nativePilot->enforceMobileChrome();
                } else {
                    m_transfer.enforceMobileChrome();
                    if(m_host && !m_host->isVisible())m_host->show();
                }
            });
        }
        return QObject::eventFilter(watched,event);
    }

private:
    void attemptAutomaticStartup(int remaining)
    {
        if(sessionActive()) {
            QSettings settings;
            MobileStartupPolicy::complete(settings,true);
            return;
        }
        const bool isReady=m_entry && m_backend && qApp &&
            MobileStartupPolicy::ready(
                m_backend->isVisible(),
                m_backend->centralWidget()!=nullptr,
                m_transitioning,
                qApp->activeModalWidget()!=nullptr);
        // Qt can delay Krita's native document root. Retry only this
        // harmless readiness check, never an actual rejected canvas
        // handoff (which could be a failed GL ownership transaction).
        if(!isReady && remaining>0) {
            QTimer::singleShot(300,this,[this,remaining]() {
                attemptAutomaticStartup(remaining-1);
            });
            return;
        }
        if(isReady) {
            if(m_entry->isChecked()) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(false);
            }
            m_entry->setChecked(true); // enter() runs synchronously.
        }
        QSettings settings;
        MobileStartupPolicy::complete(settings,sessionActive());
        if(!sessionActive())
            qWarning() << "Mobile Studio: automatic entry failed;"
                       << "open Settings > Mobile Studio (experimental)";
    }

    bool sessionActive() const
    {
        return (m_nativePilot && m_nativePilot->active()) ||
               m_transfer.active();
    }

    void enter()
    {
        // Modal file dialogs and deferred Compact UI callbacks can toggle
        // the launcher while the native central widget is being moved.
        // Never run two ownership transactions on the same event loop.
        if(m_transitioning) {
            if(m_entry) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(sessionActive());
            }
            return;
        }
        if(sessionActive())return;
        const QScopedValueRollback<bool> transition(m_transitioning,true);
        if(m_nativeCanvasMode) {
            if(m_nativePilot && !m_nativePilot->ready()) {
                // A native document/layout operation may legitimately
                // replace Krita's central widget. The stationary pilot
                // must never force its old canvas back into that slot.
                // Tear down only the obsolete mobile docks and create
                // fresh controls around Krita's *current* central view.
                m_nativePilot->leave();
                m_nativePilot.reset();
            }
            if(!m_nativePilot) {
                m_nativePilot.reset(
                    new MobileNativeCanvasPilot(m_backend,m_bridge));
                m_nativePilot->setExitCallback([this]() {
                    if(m_entry)m_entry->setChecked(false);
                });
            }
            if(!m_nativePilot->enter() && m_entry) {
                const QSignalBlocker blocked(m_entry);
                m_entry->setChecked(false);
            }
            return;
        }
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
            if(m_host && !m_host->hasNativeCanvas() &&
               !m_transfer.persistentHostInstalled()) {
                // An embedded persistent host remains Krita's central
                // widget even if Qt unexpectedly loses its canvas child.
                // Never delete that host on failed reentry: doing so would
                // discard the last chance to recover the original view.
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
                m_entry->setChecked(sessionActive());
            }
            return;
        }
        const QScopedValueRollback<bool> transition(m_transitioning,true);
        if(m_nativeCanvasMode) {
            // The original Krita central canvas never moves here. Even if
            // Qt replaced it unexpectedly, the dock adapter restores
            // desktop chrome before refusing a new mobile entry.
            if(m_nativePilot)m_nativePilot->leave();
            return;
        }
        if(m_transfer.leave()) {
            if(m_transfer.persistentHostInstalled()) {
                // Its paint canvas remains parented to this central host.
                // Never hide or delete it on Back; only mobile chrome was
                // disabled by MobileCanvasTransfer::leave().
                return;
            }
            // Default transfer mode still discards its empty mobile UI host
            // after native Krita has recovered its original canvas.
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
    std::unique_ptr<MobileNativeCanvasPilot> m_nativePilot;
    const bool m_nativeCanvasMode;
    bool m_transitioning=false;
    bool m_consumeBackRelease=false;
};

#endif // Q_OS_ANDROID
