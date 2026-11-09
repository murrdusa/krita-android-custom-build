/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include "mobile_core/native_startup_policy.h"

static void require(bool success,const char *why)
{
    if(!success)qFatal("Native startup policy: %s",why);
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;
    require(dir.isValid(),"temporary settings directory unavailable");
    QSettings s(dir.filePath(QStringLiteral("native.ini")),QSettings::IniFormat);
    using Decision=MobileNativeStartupPolicy::Decision;
    require(!MobileNativeStartupPolicy::mayDeferDesktopUi(s),
            "desktop UI deferred on a fresh default installation");
    require(MobileNativeStartupPolicy::begin(s)==Decision::Skip,
            "independent mode must be off on initial installation");
    require(!s.value(MobileNativeStartupPolicy::pendingKey()).toBool(),
            "skipped mode must not leave a pending launch");

    s.setValue(QStringLiteral("MobileStudio/startDirectly"),true);
    MobileNativeStartupPolicy::setEnabled(s,true);
    require(MobileNativeStartupPolicy::enabled(s),
            "explicit independent-host opt-in not persisted");
    require(MobileNativeStartupPolicy::mayDeferDesktopUi(s),
            "explicit opt-in did not enable reduced desktop setup");
    require(!s.value(QStringLiteral("MobileStudio/startDirectly")).toBool(),
            "old mobile overlay would race independent startup");
    require(MobileNativeStartupPolicy::begin(s)==Decision::Launch,
            "opt-in did not request a real engine launch");
    require(s.value(MobileNativeStartupPolicy::pendingKey()).toBool(),
            "failure recovery marker was not flushed");
    require(!MobileNativeStartupPolicy::mayDeferDesktopUi(s),
            "interrupted launch would omit desktop recovery UI");

    // Simulate Android terminating halfway through the new engine's
    // initialization, followed by the next app process.
    QSettings reboot(dir.filePath(QStringLiteral("native.ini")),
                     QSettings::IniFormat);
    require(MobileNativeStartupPolicy::begin(reboot)==Decision::RecoverDesktop,
            "interrupted native launch did not enter desktop recovery");
    require(!MobileNativeStartupPolicy::enabled(reboot) &&
            !reboot.value(MobileNativeStartupPolicy::pendingKey()).toBool() &&
            !MobileNativeStartupPolicy::mayDeferDesktopUi(reboot),
            "crash recovery left a relaunch loop enabled");
    require(MobileNativeStartupPolicy::begin(reboot)==Decision::Skip,
            "recovery reattempted native startup");

    MobileNativeStartupPolicy::setEnabled(s,true);
    require(MobileNativeStartupPolicy::begin(s)==Decision::Launch,
            "reenabling native startup was rejected");
    MobileNativeStartupPolicy::complete(s,true);
    require(MobileNativeStartupPolicy::enabled(s) &&
            !s.value(MobileNativeStartupPolicy::pendingKey()).toBool() &&
            MobileNativeStartupPolicy::mayDeferDesktopUi(s),
            "successful canvas did not retain user opt-in");

    require(MobileNativeStartupPolicy::begin(s)==Decision::Launch,
            "successful startup did not persist to next launch");
    MobileNativeStartupPolicy::complete(s,false);
    require(!MobileNativeStartupPolicy::enabled(s) &&
            !s.value(MobileNativeStartupPolicy::pendingKey()).toBool(),
            "failed host launch did not revert to desktop");

    // A second main window in the SAME Android process is not an
    // interrupted process and must not clear the native opt-in.
    MobileNativeStartupPolicy::setEnabled(s,true);
    const Decision first=MobileNativeStartupPolicy::beginOncePerProcess(s);
    const Decision second=MobileNativeStartupPolicy::beginOncePerProcess(s);
    require(first==Decision::Launch && second==Decision::Skip &&
            MobileNativeStartupPolicy::enabled(s),
            "second same-process main window duplicated engine or recovery");
    app.setProperty(MobileNativeStartupPolicy::processDecisionKey(),QVariant());
    QSettings nextProcess(dir.filePath(QStringLiteral("native.ini")),
                          QSettings::IniFormat);
    require(MobileNativeStartupPolicy::beginOncePerProcess(nextProcess)==
                Decision::RecoverDesktop &&
            !MobileNativeStartupPolicy::enabled(nextProcess),
            "interrupted NEXT process did not activate desktop fallback");
    app.setProperty(MobileNativeStartupPolicy::processDecisionKey(),QVariant());

    MobileNativeStartupPolicy::setEnabled(s,true);
    MobileNativeStartupPolicy::setEnabled(s,false);
    require(!MobileNativeStartupPolicy::mayDeferDesktopUi(s),
            "desktop UI was deferred after the user disabled native startup");
    require(MobileNativeStartupPolicy::begin(s)==Decision::Skip,
            "user opt-out ignored");
    qInfo() << "PASS: independent Android boot, opt-in, recovery, undo opt-in";
    return 0;
}
