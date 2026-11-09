/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android engine-only composition boundary: KisApplication knows only
 * this interface, not the native painting controller or its Qt widgets.
 */
#pragma once
#ifdef Q_OS_ANDROID
class MobileEngineOnlyBootstrap final
{
public:
    // Called after Krita resource, tool and paintop registration.
    // Asynchronous canvas success/failure is handled by the V2 policy.
    static bool launch();
};
#endif
