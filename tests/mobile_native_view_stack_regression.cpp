/* SPDX-License-Identifier: GPL-2.0-or-later */
// Desktop Qt checks for multiple independent native document views.
// Does not initialize Krita or the Android drawing engine.
#define KRITA_MOBILE_QT_TEST
#include "../mobile_core/native_view_stack.h"
#include "../mobile_core/view_identity_guard.h"
#include "../mobile_core/deferred_view_command.h"
#include <QSharedPointer>
#include <QApplication>
#include <QWidget>
#include <QObject>
#include <QDebug>
#include <algorithm>

static bool require(bool ok,const char *message)
{
    if(!ok)qCritical()<<"FAIL:"<<message;
    return ok;
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    MobileNativeViewStack stack;
    QObject firstDocument,secondDocument;
    auto *first=new QWidget;
    auto *second=new QWidget;
    auto *duplicate=new QWidget;
    bool allowClose=false;
    int closed=0;
    const bool firstAdded=stack.adoptView(first,&firstDocument,
        QStringLiteral("Untitled"),[&]() {++closed;return allowClose;});
    const bool secondAdded=stack.adoptView(second,&secondDocument,
        QStringLiteral("Second"),[&]() {++closed;return allowClose;});
    if(!require(firstAdded && secondAdded,"two distinct documents attached"))
        return 1;
    if(!require(!stack.adoptView(duplicate,&firstDocument,
                    QStringLiteral("Duplicate"),[](){return true;}),
                "duplicate document rejected"))return 1;
    delete duplicate;
    auto views=stack.openViews();
    if(!require(views.size()==2 &&
                views.at(0).document==&firstDocument &&
                views.at(1).document==&secondDocument,
                "both document identities retained"))return 1;
    const QPointer<QObject> firstIdentity(first);
    const QPointer<QObject> secondIdentity(second);
    // Recovery is asynchronous. Multiple stable canvases are valid,
    // but tab activation, closure and replacement invalidate the open.
    using Identity=MobileViewIdentityGuard;
    auto recoveryBefore=stack.openViews();
    QVector<QPointer<QObject>> recoveryViews{firstIdentity,secondIdentity};
    const auto originalActive=Identity::uniqueActive(recoveryBefore);
    if(!require(Identity::sameRecoveryWorkspace(
                    recoveryViews,originalActive,recoveryBefore),
                "recovery accepts an unchanged two-canvas workspace"))
        return 1;
    auto reordered=recoveryBefore;
    std::reverse(reordered.begin(),reordered.end());
    if(!require(Identity::sameRecoveryWorkspace(
                    recoveryViews,originalActive,reordered),
                "recovery accepts reordered unchanged native tabs"))
        return 1;
    auto changedActive=reordered;
    for(auto &entry:changedActive)
        entry.active=entry.nativeView!=originalActive;
    if(!require(!Identity::sameRecoveryWorkspace(
                    recoveryViews,originalActive,changedActive),
                "recovery refuses a switched active canvas"))
        return 1;
    auto removedView=recoveryBefore;
    removedView.removeLast();
    if(!require(!Identity::sameRecoveryWorkspace(
                    recoveryViews,originalActive,removedView),
                "recovery refuses a closed canvas"))
        return 1;
    auto replacedView=recoveryBefore;
    QObject replacement;
    replacedView.last().nativeView=&replacement;
    if(!require(!Identity::sameRecoveryWorkspace(
                    recoveryViews,originalActive,replacedView),
                "recovery refuses a replaced native canvas"))
        return 1;
    if(!require(Identity::sameRecoveryWorkspace(
                    {},{},{}),
                "recovery accepts an unchanged empty workspace"))
        return 1;
    // Qt changes the visible stack index synchronously, while Krita
    // updates its active document on the next queued event-loop turn.
    // Close/Save must never hit the previous canvas in that interval.
    if(!require(MobileViewIdentityGuard::safeCloseTarget(
                    views,QPointer<QObject>(&firstDocument)).isNull(),
                "reject close while selected and engine document disagree"))
        return 1;
    if(!require(stack.activateView(firstIdentity) &&
                stack.currentDocument()==&firstDocument,
                "switch to first document"))return 1;
    // Neither a stale engine document nor a reused native tab index
    // authorizes Close while the Qt stack reports no active canvas.
    auto noActive=stack.openViews();
    for(auto &row:noActive)row.active=false;
    if(!require(!MobileViewIdentityGuard::safeCloseTarget(
                    noActive,QPointer<QObject>(&firstDocument)) &&
                !MobileViewIdentityGuard::safeCloseTarget(
                    noActive,QPointer<QObject>(&secondDocument)),
                "reject close when no native canvas is active"))return 1;
    auto ambiguous=noActive;
    for(auto &row:ambiguous)row.active=true;
    if(!require(!MobileViewIdentityGuard::safeCloseTarget(
                    ambiguous,QPointer<QObject>(&firstDocument)),
                "reject close with ambiguous active canvases"))return 1;
    auto firstSnapshot=stack.openViews();
    if(!require(MobileViewIdentityGuard::safeCloseTarget(
                    firstSnapshot,QPointer<QObject>(&firstDocument))==firstIdentity &&
                !MobileViewIdentityGuard::safeCloseTarget(
                    firstSnapshot,QPointer<QObject>(&secondDocument)) &&
                !MobileViewIdentityGuard::safeCloseTarget(
                    firstSnapshot,QPointer<QObject>()),
                "close target requires matching engine-selected document"))
        return 1;
    if(!require(stack.activateView(secondIdentity) &&
                stack.currentDocument()==&secondDocument,
                "switch to second document"))return 1;
    // A click is bound to the selected document at dispatch time, not
    // whichever canvas becomes current before Qt's queued callback runs.
    QSharedPointer<MobileViewHostPort> port(
        new MobileNativeViewPortAdapter(&stack));
    int dispatched=0;
    if(!require(MobileDeferredViewCommand::schedule(&stack,port,
                    [&dispatched](const QPointer<QObject> &) {++dispatched;}),
                "schedule deferred command"))return 1;
    if(!require(stack.activateView(firstIdentity),
                "switch before deferred dispatch"))return 1;
    app.processEvents();
    if(!require(dispatched==0,"stale deferred command rejected"))return 1;
    if(!require(stack.activateView(secondIdentity) &&
                MobileDeferredViewCommand::schedule(&stack,port,
                    [&dispatched,secondIdentity](const QPointer<QObject> &id) {
                        if(id==secondIdentity)++dispatched;
                    }),
                "schedule correct current document command"))return 1;
    app.processEvents();
    if(!require(dispatched==1,"valid deferred command dispatched once"))
        return 1;
    if(!require(stack.updateViewState(secondIdentity,
                    QStringLiteral("Second edited"),true),
                "update document state"))return 1;
    views=stack.openViews();
    if(!require(views.at(1).modified &&
                views.at(1).title==QStringLiteral("Second edited"),
                "modified flag and title reflect real document"))return 1;
    // A delayed Close from another canvas must not prompt for, save,
    // or destroy the document which is now in the background.
    if(!require(stack.activateView(firstIdentity) &&
                !stack.closeView(secondIdentity) &&
                closed==0 && stack.openViews().size()==2 &&
                stack.currentDocument()==&firstDocument,
                "background canvas must reject stale close"))return 1;
    if(!require(stack.activateView(secondIdentity),
                "restore selected document for close"))return 1;
    if(!require(!stack.closeView(secondIdentity) &&
                stack.openViews().size()==2 && closed==1,
                "canceling close keeps document attached"))return 1;
    allowClose=true;
    if(!require(stack.closeView(secondIdentity) &&
                stack.openViews().size()==1 &&
                stack.currentDocument()==&firstDocument,
                "closing second document preserves first"))return 1;
    delete second;
    const auto lone=stack.openViews();
    if(!require(lone.size()==1 &&
                !MobileViewIdentityGuard::safeCloseTarget(
                    lone,QPointer<QObject>()) &&
                MobileViewIdentityGuard::safeCloseTarget(
                    lone,QPointer<QObject>(&firstDocument))==firstIdentity,
                "lone canvas close still requires engine document ownership"))
        return 1;
    if(!require(stack.closeView(firstIdentity) &&
                stack.openViews().isEmpty(),
                "close remaining document"))return 1;
    delete first;
    // The native engine can destroy a view without a close command
    // (for example, document teardown after a failed import). The
    // stack must forget its identity and permit reopening that document.
    MobileNativeViewStack externalStack;
    QObject externalDocument;
    auto *externalView=new QWidget;
    if(!require(externalStack.adoptView(
                    externalView,&externalDocument,QStringLiteral("External"),
                    [](){return true;}),
                "external view registered"))return 1;
    delete externalView;
    if(!require(externalStack.openViews().isEmpty(),
                "destroyed native view removed from stack"))return 1;
    auto *replacement=new QWidget;
    if(!require(externalStack.adoptView(
                    replacement,&externalDocument,QStringLiteral("Reattached"),
                    [](){return true;}) &&
                externalStack.currentDocument()==&externalDocument,
                "document can attach again after view destruction"))return 1;
    delete replacement;
    if(!require(externalStack.openViews().isEmpty(),
                "replaced view teardown cleans registry"))return 1;

    qInfo()<<"PASS: multi-document native view ownership, duplicate rejection,"
             " activation, state changes and cancelable closing";
    return 0;
}
