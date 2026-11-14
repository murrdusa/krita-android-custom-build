/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Verify a paused Qt canvas gets another activation on resume,
 * without duplicate activation when both root and window show.
 */
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>
#include "mobile_studio/canvas_resume_observer.h"

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
