/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Verify a paused Qt canvas gets another activation on resume,
 * without duplicate activation when both root and window show.
 */
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QWindow>
#include <QVBoxLayout>
#include <QWidget>
#include "mobile_core/canvas_resume_observer.h"

static void require(bool passed,const char *reason)
{
    if(!passed)qFatal("Canvas resume observer: %s",reason);
}
static void drain()
{
    QCoreApplication::sendPostedEvents(nullptr,0);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr,0);
    QCoreApplication::processEvents();
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QWidget window;
    auto *layout=new QVBoxLayout(&window);
    auto *root=new QWidget;
    layout->addWidget(root);
    int activations=0;
    auto *observer=MobileCanvasResumeObserver::attach(
        &window,root,&window,[&activations]() {++activations;});
    require(observer,"could not install native visibility watcher");

    window.show();
    drain();
    require(activations>=1,"first visible canvas was not activated");

    const int initial=activations;
    window.hide();
    drain();
    observer->request();
    drain();
    require(activations==initial,
            "activated an OpenGL canvas while its window was hidden");

    window.show();
    drain();
    require(activations>initial,
            "did not reactivate the canvas when returning to its window");

    const int reopened=activations;
    root->hide();
    drain();
    observer->request();
    drain();
    require(activations==reopened,
            "activated a canvas while its root was hidden");

    root->show();
    drain();
    require(activations>reopened,
            "did not retry activation after the canvas root reappeared");

    // A native surface can be created after the QWidget is first
    // shown, and WinIdChange can occur more than once for the same
    // surface. Rebinding must preserve future expose notifications.
    window.winId();
    QEvent windowHandleChanged(QEvent::WinIdChange);
    QCoreApplication::sendEvent(&window,&windowHandleChanged);
    QCoreApplication::sendEvent(&window,&windowHandleChanged);
    drain();
    QWindow *surface=window.windowHandle();
    require(surface,"native window surface was not created");
    const int beforeExpose=activations;
    QEvent exposed(QEvent::Expose);
    QCoreApplication::sendEvent(surface,&exposed);
    drain();
    require(activations>beforeExpose,
            "window exposure no longer resumes native canvas");

    const int beforeDeletion=activations;
    delete root;
    observer->request();
    drain();
    require(activations==beforeDeletion,
            "reactivated a deleted canvas root");

    qInfo()<<"PASS: native canvas resumes after Android hide/show,"
           <<"skips hidden GL activation and survives deleted roots";
    return 0;
}
