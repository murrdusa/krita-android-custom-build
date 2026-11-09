/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The Android app always opens its own Krita-backed drawing host.
 */
#include "krita_application_entry.h"
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QSettings>
#include "engine_only_startup_policy.h"
#include "engine_only_bootstrap.h"

bool MobileKritaApplicationEntry::start()
{
    // Preserve crash diagnostics without providing or selecting another
    // application interface. Canvas success is reported asynchronously.
    QSettings settings;
    const QString pending=MobileEngineOnlyStartupPolicy::pendingKey();
    if(settings.value(pending,false).toBool())
        settings.setValue(
            QStringLiteral("MobileStudio/previousBootInterrupted"),true);
    settings.setValue(pending,true);
    settings.sync();

    if(MobileEngineOnlyBootstrap::launch())
        return true;

    MobileEngineOnlyStartupPolicy::complete(settings,false);
    qCritical() << "Mobile Studio: native drawing window failed";
    return false;
}
#endif
