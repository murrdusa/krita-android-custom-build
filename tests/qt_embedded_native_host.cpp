/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * A real Android app has only one native top-level window. The
 * standalone engine diagnostic must stay under it and never steal
 * Krita's original central drawing canvas.
 */
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QPointer>
#include <QWidget>
#include "mobile_core/lightweight_canvas_host.h"

static void check(bool passed,const char *reason)
{
    if(!passed)qFatal("Embedded engine host test: %s",reason);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMainWindow krita;
    krita.resize(800,600);
    QWidget *original=new QWidget;
    krita.setCentralWidget(original);
    krita.show();
    QApplication::processEvents();

    auto *host=new MobileLightweightCanvasHost;
    check(host->embedInExistingWindow(&krita),
          "could not embed independent QWidget into backend window");
    check(!host->isWindow() && host->window()==&krita &&
          host->parentWidget()==&krita,
          "engine created a second native top-level window");
    auto *root=host->createNativeDocumentRoot();
    check(root && root->parentWidget()==host &&
          root->window()==&krita && host->retainsOriginalCanvas(),
          "native view stack lost embedded host ownership");
    check(krita.centralWidget()==original &&
          original->parentWidget()==&krita,
          "engine overlay moved or replaced the original Krita canvas");
    host->show();
    host->raise();
    QApplication::processEvents();
    check(host->isVisible() && host->geometry()==krita.rect(),
          "embedded engine did not cover the existing window");

    krita.resize(620,940);
    QApplication::processEvents();
    check(host->geometry()==krita.rect(),
          "embedded engine did not follow native window resize");

    auto *document=new QObject(&krita);
    QWidget *nativeView=new QWidget;
    QPointer<QWidget> guardedView=nativeView;
    check(root->adoptView(nativeView,document,QStringLiteral("test.kra"),
                           [nativeView]() {delete nativeView;return true;}),
          "could not adopt native view into embedded stack");
    check(nativeView->window()==&krita &&
          root->openViews().size()==1 &&
          host->retainsOriginalCanvas(),
          "embedded drawing view caused a second top-level window");
    check(host->close(),"embedded host refused disposable view close");
    QApplication::processEvents();
    check(guardedView.isNull() && krita.centralWidget()==original &&
          original->parentWidget()==&krita,
          "closing engine test destroyed or moved old canvas");
    QPointer<MobileLightweightCanvasHost> guardHost=host;
    host->deleteLater();
    QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(guardHost.isNull() && krita.centralWidget()==original,
          "test host teardown altered the desktop canvas");
    qInfo() << "PASS: embedded independent canvas uses one Qt native window,"
            << "tracks rotation, preserves original Krita canvas and exits";
    return 0;
}
