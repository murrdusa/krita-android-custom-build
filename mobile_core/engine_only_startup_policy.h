/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Experimental Android startup WITHOUT creating KisMainWindow.
 * Krita's KisApplication/registries/plugins and KisPart engine remain;
 * only the presentation bootstrap is replaced.
 *
 * Engine-only is the normal Android startup, unless explicitly
 * disabled or a previous attempt failed. A crash before the first
 * canvas is ready forces the original desktop recovery on the NEXT
 * process and persists that fallback until manually re-enabled.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QSettings>
#include <QString>

class MobileEngineOnlyStartupPolicy final
{
public:
    enum class Decision { Desktop, Launch, RecoverDesktop };

    static QString key()
    {
        return QStringLiteral("MobileStudio/engineOnlyBootV2");
    }
    static QString pendingKey()
    {
        return QStringLiteral("MobileStudio/engineOnlyBootPendingV2");
    }
    // V3 tests this native painting host as the Android top-level.
    // V2 remains the device-verified default until V3 passes tests.
    static QString directHostKey()
    {
        return QStringLiteral("MobileStudio/directEngineHostV3");
    }
    static bool directHostEnabled(const QSettings &settings)
    {
        return settings.value(directHostKey(),false).toBool();
    }
    static bool enabled(const QSettings &settings)
    {
        // New installations and existing V1 installations migrate to
        // the now device-verified independent engine on first launch.
        // An explicit false remains authoritative for manual opt-out
        // or previous failed/crashed startup; never retry indefinitely.
        return settings.value(key(),true).toBool();
    }
    static void setEnabled(QSettings &settings,bool enabled)
    {
        settings.setValue(key(),enabled);
        settings.setValue(pendingKey(),false);
        if(!enabled)settings.setValue(directHostKey(),false);
        if(enabled) {
            // Only one boot mode may own the Android top-level.
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),false);
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupPending"),false);
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
            settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        }
        settings.sync();
    }
    static void setDirectHostEnabled(QSettings &settings,bool enabled)
    {
        if(enabled)setEnabled(settings,true);
        settings.setValue(directHostKey(),enabled);
        settings.sync();
    }
    // Some Android share/Open intents require desktop integration until
    // native file-intent routing exists. Refuse engine-only startup
    // rather than dropping such a request or dereferencing mainWindow.
    static Decision begin(QSettings &settings,bool compatibleInvocation)
    {
        if(!enabled(settings) || !compatibleInvocation)
            return Decision::Desktop;
        if(settings.value(pendingKey(),false).toBool()) {
            if(directHostEnabled(settings)) {
                // Interrupted V3 launch: retry the verified V2 wrapper,
                // not another copy of the potentially broken direct host.
                settings.setValue(directHostKey(),false);
                settings.setValue(pendingKey(),false);
                settings.sync();
            } else {
                complete(settings,false);
                return Decision::RecoverDesktop;
            }
        }
        // The old native host and compact launcher cannot race V2:
        // neither should create its own window while we boot the
        // Krita-independent Qt root.
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
        const bool recoverDirectHost=!success && directHostEnabled(settings);
        settings.setValue(pendingKey(),false);
        // A V3 failure falls back to verified V2. A V2 failure still
        // disables engine-only to restore original desktop Krita.
        settings.setValue(key(),success || recoverDirectHost);
        if(recoverDirectHost)settings.setValue(directHostKey(),false);
        if(!success) {
            // On failure, use Krita's complete original desktop shell.
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),false);
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupPending"),false);
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
            settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        }
        settings.sync();
    }
};
#endif
