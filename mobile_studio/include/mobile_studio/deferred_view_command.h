/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Queue native document actions outside the initiating Qt button event.
 * Bind the action to the exact active view AND document at tap time:
 * an Android dialog or reentrant Qt callback must not redirect Save
 * or Close to a newly selected painting.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>
#include <functional>
#include <utility>
#include "view_host_port.h"
#include "view_identity_guard.h"

class MobileDeferredViewCommand final
{
public:
    using Command=std::function<void(const QPointer<QObject> &)>;

    static bool schedule(QObject *context,
                         QSharedPointer<MobileViewHostPort> views,
                         Command command)
    {
        if(!context || !views || !command)return false;
        const auto snapshot=views->openViews();
        const QPointer<QObject> view=
            MobileViewIdentityGuard::uniqueActive(snapshot);
        const QPointer<QObject> document=views->currentDocument();
        if(!MobileViewIdentityGuard::maySave(snapshot,view,document))
            return false;
        // Qt cancels the timer if the owning window is destroyed.
        // QPointers also guard deletion of the view or its document.
        QPointer<QObject> owner=context;
        QTimer::singleShot(0,context,
            [owner,views,view,document,command=std::move(command)]() {
                if(!owner || !view || !document || !views ||
                   views->currentDocument()!=document ||
                   !MobileViewIdentityGuard::maySave(
                       views->openViews(),view,document))
                    return;
                command(view);
            });
        return true;
    }
};
#endif
