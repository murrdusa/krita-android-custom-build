/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Verify a paused Qt canvas gets another activation on resume,
 * without duplicate activation when both root and window show.
 */
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QExposeEvent>
#include <QPointer>
#include <QWindow>
#include <QVBoxLayout>
#include <QWidget>
#include "mobile_core/canvas_resume_observer.h"

// Model Android/Qt listeners that synchronously retire a GL surface
// when its first-frame waiting flag is cleared.
class RetireOnFirstSwapDisarm final : public QObject
{
protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(event->type()==QEvent::DynamicPropertyChange) {
            auto *change=static_cast<QDynamicPropertyChangeEvent*>(event);
            if(change->propertyName()=="_mobileWaitingForFirstSwap" &&
               !watched->property("_mobileWaitingForFirstSwap").toBool()) {
                delete watched;
                return true;
            }
        }
        return QObject::eventFilter(watched,event);
    }
};

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
    // Validate the same retry policy used by the Android GL surface,
    // including saturated counts from malformed/stale Qt properties.
    require(MobileFirstSwapRetryPolicy::timeoutMs(0)==2000 &&
            MobileFirstSwapRetryPolicy::timeoutMs(1)==4000 &&
            MobileFirstSwapRetryPolicy::timeoutMs(2)==8000 &&
            MobileFirstSwapRetryPolicy::timeoutMs(3)==16000 &&
            MobileFirstSwapRetryPolicy::timeoutMs(1000000)==16000 &&
            MobileFirstSwapRetryPolicy::timeoutMs(-100)==2000,
            "first-frame retry delays exceeded the bounded policy");
    int attempt=0;
    const int expectedDelays[]={2000,4000,8000,16000,16000};
    for(int delay:expectedDelays) {
        require(MobileFirstSwapRetryPolicy::timeoutMs(attempt)==delay,
                "GL retry progression used an incorrect delay");
        attempt=MobileFirstSwapRetryPolicy::afterTimeoutScheduled(attempt);
    }
    require(attempt==3 &&
            MobileFirstSwapRetryPolicy::afterTimeoutScheduled(-1)==1 &&
            MobileFirstSwapRetryPolicy::afterTimeoutScheduled(999)==3,
            "GL retry progression did not clamp an out-of-range count");
    // Successful context restoration can precede frameSwapped. Starting
    // another context-loss episode must not inherit saturated backoff.
    attempt=MobileFirstSwapRetryPolicy::afterSurfaceReady();
    require(attempt==0 &&
            MobileFirstSwapRetryPolicy::timeoutMs(attempt)==2000,
            "restored GL surface retained stale first-frame retry backoff");
    // First-swap completion clears a surface property. An observer
    // may synchronously destroy that surface; subsequent GL access
    // must check the QPointer again after setProperty() returns.
    {
        RetireOnFirstSwapDisarm retire;
        auto *surface=new QObject;
        surface->setProperty("_mobileWaitingForFirstSwap",true);
        surface->installEventFilter(&retire);
        const QPointer<QObject> watched(surface);
        watched->setProperty("_mobileWaitingForFirstSwap",false);
        int retriesReset=0;
        if(watched) {
            watched->setProperty("_mobileFirstSwapRetryCount",0);
            ++retriesReset;
        }
        require(!watched && retriesReset==0,
                "first-swap disarm accessed a surface destroyed by a callback");
    }
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

    // Layout can settle after the initial activation while the window stays visible.
    const int beforeLayout=activations;
    QEvent layoutReady(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(root,&layoutReady);
    drain();
    require(activations>beforeLayout,"settled canvas layout did not retry activation");

    // A newly selected child view must wake without hiding/showing the window.
    auto *secondView=new QWidget(root);
    secondView->hide();
    int secondActivations=0;
    MobileCanvasResumeObserver::attach(&window,secondView,secondView,[&]() {++secondActivations;});
    drain();
    require(secondActivations==0,"activated a hidden new view");
    secondView->show();
    drain();
    require(secondActivations>0,"new view waited for a window refresh");
    delete secondView;
    drain();

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
    QExposeEvent exposed(QRegion(window.rect()));
    QCoreApplication::sendEvent(surface,&exposed);
    drain();
    require(activations>beforeExpose,
            "window exposure no longer resumes native canvas");

    const int beforeDeletion=activations;
    const QPointer<MobileCanvasResumeObserver> observerGuard(observer);
    delete root;
    drain();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    drain();
    require(activations==beforeDeletion && !observerGuard,
            "retained a canvas observer after its root was destroyed");

    // The owner can remain alive after a root is replaced. A stale
    // surface observer must not linger until the owner is deleted.
    {
        QWidget survivingOwner;
        QWidget separateWindow;
        auto *separateRoot=new QWidget(&separateWindow);
        auto *detached=MobileCanvasResumeObserver::attach(
            &separateWindow,separateRoot,&survivingOwner,[](){});
        const QPointer<MobileCanvasResumeObserver> detachedGuard(detached);
        separateWindow.show();
        drain();
        delete separateRoot;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        drain();
        require(!detachedGuard,
                "old canvas watcher survived replacement under a live manager");
    }

    // An activation can close the final native canvas and destroy the
    // observer synchronously. Its callback must remain valid until
    // invocation returns, without a use-after-free of the member
    // std::function being executed.
    {
        QWidget dyingWindow;
        auto *dyingRoot=new QWidget(&dyingWindow);
        dyingWindow.show();
        drain();
        int teardownCalls=0;
        QPointer<MobileCanvasResumeObserver> dyingObserver;
        dyingObserver=MobileCanvasResumeObserver::attach(
            &dyingWindow,dyingRoot,&dyingWindow,[&]() {
                ++teardownCalls;
                delete dyingObserver.data();
            });
        require(dyingObserver,"could not create teardown observer");
        drain();
        require(teardownCalls==1 && !dyingObserver,
                "canvas resume callback could not destroy its own observer");
        dyingWindow.hide();
        drain();
        require(teardownCalls==1,
                "destroyed canvas observer resumed after teardown");
    }

    // An Android GL-context replacement can leave the old QWidget
    // alive while a first-swap deadline is pending. Its timeout must
    // not trigger activation of the new surface or clear its flags.
    {
        auto *oldSurface=new QWidget;
        auto *newSurface=new QWidget;
        QPointer<QWidget> watched(oldSurface);
        QPointer<QWidget> current(newSurface);
        int activations=0;
        const auto expireFirstSwap=[&]() {
            if(watched)
                watched->setProperty("_mobileWaitingForFirstSwap",false);
            if(watched && current && watched==current)
                ++activations;
        };
        oldSurface->setProperty("_mobileWaitingForFirstSwap",true);
        newSurface->setProperty("_mobileWaitingForFirstSwap",true);
        expireFirstSwap();
        require(activations==0 &&
                !oldSurface->property("_mobileWaitingForFirstSwap").toBool() &&
                newSurface->property("_mobileWaitingForFirstSwap").toBool(),
                "old GL timeout restarted a replacement canvas surface");
        watched=newSurface;
        expireFirstSwap();
        require(activations==1 &&
                !newSurface->property("_mobileWaitingForFirstSwap").toBool(),
                "current GL timeout failed to resume the matching surface");
        delete oldSurface;
        delete newSurface;
    }

    qInfo()<<"PASS: native canvas resumes after Android hide/show,"
           <<"skips hidden GL activation and survives deleted roots";
    return 0;
}
