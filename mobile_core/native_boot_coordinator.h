/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Independent engine startup scheduling is a pure Qt host concern.
 * It must not require KisMainWindow, QMdiArea, desktop canvas
 * transfer, or a second Android native window.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <utility>
#include "native_startup_policy.h"

class MobileNativeBootCoordinator final : public QObject
{
public:
    using Completion=std::function<void(bool)>;
    using Launch=std::function<bool(QMainWindow *,Completion)>;

    // begin() is executed by the bootstrap BEFORE desktop dockers are
    // considered. This coordinator only consumes the decision.
    static bool schedule(QMainWindow *window,
                         MobileNativeStartupPolicy::Decision decision,
                         Launch launch,
                         int retryMs=250,int maxAttempts=24)
    {
        using Decision=MobileNativeStartupPolicy::Decision;
        if(decision==Decision::Skip ||
           decision==Decision::RecoverDesktop)
            return true;
        if(!window || !launch) {
            QSettings settings;
            MobileNativeStartupPolicy::fail(settings);
            return false;
        }
        auto *task=new MobileNativeBootCoordinator(
            window,std::move(launch),qMax(0,retryMs),
            qMax(1,maxAttempts));
        QTimer::singleShot(0,task,[task]() {task->poll();});
        return true;
    }

private:
    MobileNativeBootCoordinator(QMainWindow *window,Launch launch,
                                int retryMs,int maxAttempts)
        : QObject(window),m_window(window),m_launch(std::move(launch)),
          m_retryMs(retryMs),m_maxAttempts(maxAttempts) {}

    void poll()
    {
        if(m_finished || !m_window)return;
        // Readiness checks may dispatch QWidget/Qt callbacks that
        // destroy this coordinator's parent window. Track the original
        // identities before checking visibility or the central widget.
        const QPointer<MobileNativeBootCoordinator> alive(this);
        const QPointer<QMainWindow> originalWindow=m_window;
        const bool visible=originalWindow->isVisible();
        if(!alive || !originalWindow)return;
        const QPointer<QWidget> canvas=originalWindow->centralWidget();
        if(!alive || !originalWindow)return;
        const bool modal=QApplication::activeModalWidget()!=nullptr;
        if(!alive || !originalWindow)return;
        if(!visible || !canvas || modal) {
            if(++m_attempts>=m_maxAttempts) {
                qWarning()<<"Independent Qt host startup was not ready";
                finish(false);
            } else {
                QTimer::singleShot(m_retryMs,this,[this]() {poll();});
            }
            return;
        }
        const QPointer<QWidget> currentCanvas=originalWindow->centralWidget();
        if(!alive || !originalWindow)return;
        if(!currentCanvas || currentCanvas!=canvas ||
           m_window!=originalWindow) {
            // The original root was removed or replaced while readiness
            // was being checked. Silently returning here leaves the
            // persisted boot attempt pending with no scheduled retry.
            // Fail explicitly so the next launch uses desktop fallback.
            finish(false);
            return;
        }
        // A native launch can synchronously destroy the Qt window and
        // this child coordinator. Never execute the std::function stored
        // in the coordinator: its storage may be freed mid-invocation.
        const Launch launch=m_launch;
        const QPointer<QMainWindow> window=m_window;
        const bool launched=launch(
            window.data(),[alive](bool successful) {
                if(alive)alive->finish(successful);
            });
        if(!launched && alive)alive->finish(false);
    }

    void finish(bool success)
    {
        if(m_finished)return;
        m_finished=true;
        QSettings settings;
        MobileNativeStartupPolicy::complete(settings,success);
        if(!success)
            qWarning()<<"Independent Krita startup failed; "
                       <<"next launch uses full Krita desktop fallback";
        deleteLater();
    }

    QPointer<QMainWindow> m_window;
    Launch m_launch;
    int m_retryMs=250;
    int m_maxAttempts=24;
    int m_attempts=0;
    bool m_finished=false;
};
#endif
