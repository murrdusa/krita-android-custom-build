/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stable Qt MDI view identity for deferred mobile UI commands.
 * Creation-order indices may change while a modal popup is closing.
 * A guarded QObject identity cannot silently resolve to another view.
 * Qt-only; does not include or own a KisView or KisDocument.
 */
#pragma once

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QObject>
#include <QPointer>

class MobileMdiViewIdentity final
{
public:
    static QPointer<QObject> capture(QMdiSubWindow *view)
    {
        return QPointer<QObject>(view);
    }

    static QPointer<QMdiSubWindow> resolve(
        QMdiArea *area,const QPointer<QObject> &identity)
    {
        if(!area || !identity)return {};
        auto *target=qobject_cast<QMdiSubWindow*>(identity.data());
        if(!target ||
           !area->subWindowList(QMdiArea::CreationOrder).contains(target))
            return {};
        return QPointer<QMdiSubWindow>(target);
    }
};
