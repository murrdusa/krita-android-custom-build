/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QApplication>
#include <QDebug>
#include <QDockWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QScrollArea>
#include <QStatusBar>
#include <QStackedWidget>
#include <QWidget>
#include "mobile_studio/native_bootstrap_audit.h"

static void check(bool value,const char *message)
{
    if(!value)qFatal("Native bootstrap widget audit: %s",message);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMainWindow desktop;
    auto *desktopStack=new QStackedWidget(&desktop);
    auto *mdi=new QMdiArea(desktopStack);
    auto *welcomeScroll=new QScrollArea(desktopStack);
    desktopStack->addWidget(mdi);
    desktopStack->addWidget(welcomeScroll);
    desktop.setCentralWidget(desktopStack);
    desktop.setProperty("_mobileNativeDesktopDeferred",true);
    desktop.setProperty("_mobileNativeHostActive",true);
    auto *status=desktop.statusBar();
    status->hide();
    desktop.show();
    QApplication::processEvents();
    status->hide();

    const auto bare=MobileNativeBootstrapAudit::inspect(&desktop);
    check(bare.nativeSelected && bare.nativeHostActive,
          "missing reduced-shell startup mode");
    check(bare.mdiAreas==1 && bare.desktopDockers==0 &&
          bare.welcomeScrollAreas==1,
          "retained MDI, welcome scroller or dockers counted incorrectly");
    check(bare.commandPalettes==0 &&
          bare.detachedCanvasWindows==0,
          "legacy desktop auxiliary window falsely reported as present");
    check(!bare.desktopStatusVisible,
          "hidden footer falsely reported visible");
    auto *docker=new QDockWidget(QStringLiteral("Legacy desktop docker"),
                                  &desktop);
    desktop.addDockWidget(Qt::LeftDockWidgetArea,docker);
    check(MobileNativeBootstrapAudit::inspect(&desktop).desktopDockers==1,
          "desktop docker change not detected");

    // A dock inside the independent host must not be mistaken for
    // one created directly by the old desktop bootstrap.
    // The Android independent host is a child of the outer Qt window,
    // not a child of the original desktop MDI area.
    auto *native=new QMainWindow(&desktop);
    auto *nativeDocker=new QDockWidget(QStringLiteral("Brushes"),native);
    native->addDockWidget(Qt::BottomDockWidgetArea,nativeDocker);
    auto *nativeScroller=new QScrollArea(native);
    nativeDocker->setWidget(nativeScroller);
    const auto withNativeControls=MobileNativeBootstrapAudit::inspect(&desktop);
    check(withNativeControls.desktopDockers==1 &&
          withNativeControls.welcomeScrollAreas==1,
          "independent drawing controls counted as desktop widgets");

    desktop.setProperty("_mobileNativeHostActive",false);
    desktop.setProperty("_mobileNativeDesktopDeferred",false);
    const auto normal=MobileNativeBootstrapAudit::inspect(&desktop);
    check(!normal.nativeHostActive && !normal.nativeSelected,
          "native-shell flags stayed enabled after fallback");
    check(normal.display().contains(QStringLiteral("MDI 1")) &&
          normal.display().contains(QStringLiteral("dockers 1")),
          "human-readable startup report omitted important widgets");
    QMainWindow noDesktop;
    noDesktop.setObjectName(QStringLiteral("_mobileEngineOnlyRoot"));
    noDesktop.setProperty("_mobileNativeEngineOnlyRoot",true);
    noDesktop.setProperty("_mobileNativeHostActive",true);
    noDesktop.setCentralWidget(new QWidget);
    const auto independent=MobileNativeBootstrapAudit::inspect(&noDesktop);
    check(independent.engineOnlyRoot && independent.nativeSelected &&
          independent.nativeHostActive && independent.mdiAreas==0 &&
          independent.desktopDockers==0 &&
          independent.detachedCanvasWindows==0,
          "plain engine-only Qt root must have ZERO desktop window UI");
    check(independent.display().contains(QStringLiteral("engine-only yes")),
          "independent bootstrap audit does not distinguish engine-only");
    qInfo()<<"PASS: desktop widget inventory distinguishes native host,"
              "retained MDI and zero-desktop engine-only Qt root";
    return 0;
}
