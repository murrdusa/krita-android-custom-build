/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt action registry boundary. Krita's native actions are still used
 * (no reimplementation of undo, painting, menus, or brush selection).
 * Mobile command services no longer need a KisMainWindow instance.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QList>
#include <QString>

class MobileCommandRegistryPort
{
public:
    virtual ~MobileCommandRegistryPort() = default;
    virtual QAction *action(const QString &id) const = 0;
    virtual QList<QAction*> availableActions() const = 0;
    virtual QList<QAction*> menuRootActions() const = 0;
};
#endif // Q_OS_ANDROID
