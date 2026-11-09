/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Qt-only startup policy for Mobile Studio's real Krita canvas host.
 * Failed handoffs must never be retried in a loop.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QSettings>
class MobileStartupPolicy final {
public:
    enum class Decision {NoAutostart, Start, RecoverNativeKrita};
    static Decision begin(QSettings &settings) {
        // Missing setting means a fresh install; explicit false is honored.
        if(!settings.value(QStringLiteral("MobileStudio/startDirectly"),
                           true).toBool())
            return Decision::NoAutostart;
        if(settings.value(QStringLiteral("MobileStudio/bootPending"),
                          false).toBool()) {
            complete(settings,false);
            return Decision::RecoverNativeKrita;
        }
        settings.setValue(QStringLiteral("MobileStudio/bootPending"),true);
        settings.sync();
        return Decision::Start;
    }
    static bool ready(bool visible,bool canvas,bool transitioning,bool modal) {
        return visible && canvas && !transitioning && !modal;
    }
    static void complete(QSettings &settings,bool success) {
        settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        if(!success)
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
        settings.sync();
    }
};
#endif
