/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Opt-in migration from Krita's desktop-backed Android startup to the
 * independently hosted engine. Crash recovery must happen before any
 * second native launch attempt; legacy Krita remains the fallback.
 *
 * Qt-only, so it can be regression-tested without the Krita libraries.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QSettings>
#include <QCoreApplication>
#include <QVariant>
#include <QString>

class MobileNativeStartupPolicy final
{
public:
    enum class Decision { Skip, Launch, RecoverDesktop };

    static QString preferenceKey()
    {
        return QStringLiteral("MobileStudio/nativeStartupV1");
    }
    static QString pendingKey()
    {
        return QStringLiteral("MobileStudio/nativeStartupPending");
    }

    static bool enabled(const QSettings &settings)
    {
        return settings.value(preferenceKey(),false).toBool();
    }

    // The full legacy UI is required on recovery boots, before begin()
    // clears a stale interrupted-startup marker. The independent engine
    // can safely defer legacy dockers and duplicate mobile controllers
    // ONLY on a fresh, explicitly enabled native-startup attempt.
    static bool mayDeferDesktopUi(const QSettings &settings)
    {
        return enabled(settings) &&
               !settings.value(pendingKey(),false).toBool();
    }

    static void setEnabled(QSettings &settings,bool enabled)
    {
        settings.setValue(preferenceKey(),enabled);
        settings.setValue(pendingKey(),false);
        if(enabled) {
            // Never race the older Mobile Studio startup controller;
            // after a crashed independent launch, recover to desktop.
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
            settings.setValue(QStringLiteral("MobileStudio/bootPending"),false);
        }
        settings.sync();
    }

    // Android normally has one native window, but session restore may
    // construct another KisMainWindow in the same process. A second
    // constructor must not retry or mistake the current process'
    // pending flag for a previous crash. Only the FIRST native window
    // may construct the independent drawing host.
    static const char *processDecisionKey()
    {
        return "_mobileNativeBootDecisionOnce";
    }

    static Decision beginOncePerProcess(QSettings &settings)
    {
        QObject *app=QCoreApplication::instance();
        if(!app)return begin(settings);
        const QVariant cached=app->property(processDecisionKey());
        if(cached.isValid())return Decision::Skip; // No second engine host.
        const Decision decision=begin(settings);
        app->setProperty(processDecisionKey(),static_cast<int>(decision));
        return decision;
    }

    static Decision begin(QSettings &settings)
    {
        if(!enabled(settings))return Decision::Skip;
        if(settings.value(pendingKey(),false).toBool()) {
            fail(settings);
            return Decision::RecoverDesktop;
        }
        settings.setValue(pendingKey(),true);
        settings.sync();
        return Decision::Launch;
    }

    static void complete(QSettings &settings,bool success)
    {
        settings.setValue(pendingKey(),false);
        if(!success) {
            settings.setValue(preferenceKey(),false);
            settings.setValue(QStringLiteral("MobileStudio/startDirectly"),false);
        }
        settings.sync();
    }

    static void fail(QSettings &settings)
    {
        complete(settings,false);
    }
};
#endif
