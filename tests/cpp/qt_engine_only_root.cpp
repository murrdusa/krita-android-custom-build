/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>
#include "mobile_studio/engine_only_startup_policy.h"

static void check(bool ok,const char *why)
{
    if(!ok)qFatal("Android engine startup: %s",why);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir dir;
    check(dir.isValid(),"missing test settings folder");
    QSettings settings(dir.filePath(QStringLiteral("boot.ini")),
                       QSettings::IniFormat);
    using Policy=MobileEngineOnlyStartupPolicy;
    using Decision=Policy::Decision;
    check(Policy::enabled(settings) &&
          Policy::begin(settings,true)==Decision::Launch,
          "new install must start independent engine");
    check(settings.value(Policy::pendingKey()).toBool(),
          "startup diagnostic marker missing");
    QSettings afterCrash(settings.fileName(),QSettings::IniFormat);
    check(Policy::begin(afterCrash,true)==Decision::Launch &&
          Policy::enabled(afterCrash),
          "interrupted launch must not enable fallback mode");
    check(afterCrash.value(
              QStringLiteral("MobileStudio/previousBootInterrupted")).toBool(),
          "interrupted launch was not recorded");
    Policy::complete(afterCrash,false);
    check(!afterCrash.value(Policy::lastResultKey(),true).toBool() &&
          Policy::enabled(afterCrash),
          "startup failure should be logged but not alter default");
    check(Policy::begin(afterCrash,true)==Decision::Launch,
          "next launch unexpectedly changed modes");
    Policy::complete(afterCrash,true);
    check(afterCrash.value(Policy::lastResultKey()).toBool() &&
          !afterCrash.value(Policy::pendingKey()).toBool(),
          "success diagnostic was not recorded");
    check(Policy::begin(afterCrash,false)==Decision::Desktop,
          "external file intent must keep Krita compatibility routing");
    Policy::setEnabled(afterCrash,false);
    check(!Policy::enabled(afterCrash) &&
          Policy::begin(afterCrash,true)==Decision::Desktop,
          "explicit desktop preference ignored");
    Policy::setEnabled(afterCrash,true);
    check(Policy::begin(afterCrash,true)==Decision::Launch &&
          !afterCrash.value(
              QStringLiteral("MobileStudio/nativeStartupV1")).toBool(),
          "enabling independent engine also enabled legacy host");
    qInfo() << "PASS: direct startup and diagnostic-only failure handling";
    return 0;
}
