/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Rapid prototype startup: always prefer the independent drawing host.
 * Crashes leave diagnostics but do not switch the app into fallback modes.
 */
#pragma once
#include <QtGlobal>
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QSettings>
#include <QString>

class MobileEngineOnlyStartupPolicy final
{
public:
    enum class Decision { Desktop, Launch };
    static QString key()
    {
        return QStringLiteral("MobileStudio/engineOnlyBootV2");
    }
    static QString pendingKey()
    {
        return QStringLiteral("MobileStudio/engineOnlyBootPendingV2");
    }
    static QString lastResultKey()
    {
        return QStringLiteral("MobileStudio/engineOnlyLastBootOk");
    }
    static bool enabled(const QSettings &settings)
    {
        return settings.value(key(),true).toBool();
    }
    static void setEnabled(QSettings &settings,bool enabled)
    {
        settings.setValue(key(),enabled);
        settings.setValue(pendingKey(),false);
        if(enabled) {
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),false);
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupPending"),false);
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
            settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        }
        settings.sync();
    }
    // Explicit sessions and external file intents still need Krita's
    // existing file routing. They are not crash-recovery fallbacks.
    static Decision begin(QSettings &settings,bool compatibleInvocation)
    {
        if(!enabled(settings) || !compatibleInvocation)
            return Decision::Desktop;
        if(settings.value(pendingKey(),false).toBool())
            settings.setValue(
                QStringLiteral("MobileStudio/previousBootInterrupted"),true);
        settings.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),false);
        settings.setValue(QStringLiteral("MobileStudio/nativeStartupPending"),false);
        settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
        settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        settings.setValue(pendingKey(),true);
        settings.sync();
        return Decision::Launch;
    }
    static void complete(QSettings &settings,bool success)
    {
        settings.setValue(pendingKey(),false);
        settings.setValue(lastResultKey(),success);
        settings.sync();
    }
};
#endif
