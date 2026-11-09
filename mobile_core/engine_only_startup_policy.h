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
        if(enabled) {
            // Only one boot mode may own the Android top-level.
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),false);
            settings.setValue(QStringLiteral("MobileStudio/nativeStartupPending"),false);
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
            settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        }
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
            complete(settings,false);
            return Decision::RecoverDesktop;
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
        settings.setValue(pendingKey(),false);
        // Persist the result explicitly. After a failed launch this
        // false overrides our new default and prevents a boot loop.
        settings.setValue(key(),success);
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
