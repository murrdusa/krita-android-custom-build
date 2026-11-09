/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android engine-only composition boundary: KisApplication knows only
 * this interface, not the native painting controller or its Qt widgets.
 */
#pragma once
#include <QtGlobal> // Defines Q_OS_ANDROID before checking the platform.
#ifdef Q_OS_ANDROID
class MobileEngineOnlyBootstrap final
{
public:
    // Called after Krita resource, tool and paintop registration.
    // Failures emit Qt/Android logs without changing startup modes.
    static bool launch();
};
#endif
