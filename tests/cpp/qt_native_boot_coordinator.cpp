/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QDebug>
#include "mobile_studio/native_boot_coordinator.h"

static void ensure(bool ok,const char *message)
{
    if(!ok)qFatal("Native Qt boot coordinator: %s",message);
}
static void drain()
{
    for(int i=0;i<12;++i) {
        QCoreApplication::sendPostedEvents(nullptr,0);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir temp;
    ensure(temp.isValid(),"temporary test directory unavailable");
    QCoreApplication::setOrganizationName("KritaMobileTest");
    QCoreApplication::setApplicationName("NativeBootCoordinator");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       temp.path());
    QSettings prefs;
    using Policy=MobileNativeStartupPolicy;
    using Decision=Policy::Decision;
    using Coordinator=MobileNativeBootCoordinator;

    QMainWindow window;
    window.setCentralWidget(new QWidget);
    window.resize(480,720);
    window.show();
    drain();
    Policy::setEnabled(prefs,true);
    auto selected=Policy::begin(prefs);
    ensure(selected==Decision::Launch,
           "independent boot was not opted in");
    int launchCount=0;
    ensure(Coordinator::schedule(
        &window,selected,
        [&launchCount](QMainWindow *host,Coordinator::Completion complete) {
            ensure(host && host->centralWidget(),
                   "coordinator lost generic Qt window ownership");
            ++launchCount;
            complete(true);
            return true;
        },0,3), "valid host failed to schedule");
    drain();
    prefs.sync();
    ensure(launchCount==1 && Policy::enabled(prefs) &&
           !prefs.value(Policy::pendingKey()).toBool(),
           "successful native launch did not complete exactly once");

    // A hidden host must not attempt any native GL/view construction.
    window.hide();
    Policy::setEnabled(prefs,true);
    selected=Policy::begin(prefs);
    int hiddenLaunchCount=0;
    ensure(Coordinator::schedule(
        &window,selected,
        [&hiddenLaunchCount](QMainWindow*,Coordinator::Completion) {
            ++hiddenLaunchCount;
            return true;
        },0,2),"hidden host was rejected instead of deferred");
    drain();
    prefs.sync();
    ensure(hiddenLaunchCount==0 && !Policy::enabled(prefs) &&
           !prefs.value(Policy::pendingKey()).toBool(),
           "unready host created a canvas or failed to restore fallback");

    window.show();
    drain();
    Policy::setEnabled(prefs,true);
    selected=Policy::begin(prefs);
    int failedLaunchCount=0;
    ensure(Coordinator::schedule(
        &window,selected,
        [&failedLaunchCount](QMainWindow*,Coordinator::Completion) {
            ++failedLaunchCount;
            return false;
        },0,2),"failed factory wasn't scheduled");
    drain();
    prefs.sync();
    ensure(failedLaunchCount==1 && !Policy::enabled(prefs),
           "failed factory did not return to desktop fallback");

    ensure(Coordinator::schedule(
        &window,Decision::Skip,
        [](QMainWindow*,Coordinator::Completion) {return false;}),
        "disabled native host should not schedule");
    ensure(!Coordinator::schedule(
        nullptr,Decision::Launch,
        [](QMainWindow*,Coordinator::Completion) {return true;}),
        "missing Android Qt window accepted");
    qInfo()<<"PASS: Android native engine Qt boot, retries,"
              " failure recovery and MDI-independent host";
    return 0;
}
