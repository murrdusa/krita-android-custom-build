/*
 * Qt-only test: native modal actions must run after button dispatch,
 * and may never silently act on another drawing after a view switch.
 */
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>
#include <QDebug>
#include "mobile_core/deferred_view_command.h"

static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Deferred document command: %s",reason);
}
static void pump()
{
    QEventLoop loop;
    QTimer::singleShot(2,&loop,&QEventLoop::quit);
    loop.exec();
}
class FakeViews final : public MobileViewHostPort
{
public:
    QPointer<QObject> a,b,da,db,selected,current;
    bool ambiguous=false;
    mutable bool replaceDocumentOnRead=false;
    QPointer<QObject> replacementDocument;
    QVector<ViewSnapshot> openViews() const override
    {
        return {
            {QStringLiteral("A.kra"),true,
             selected==a || ambiguous,0,a,da},
            {QStringLiteral("B.kra"),true,
             selected==b || ambiguous,1,b,db}
        };
    }
    QPointer<QObject> currentDocument() const override
    {
        if(replaceDocumentOnRead) {
            replaceDocumentOnRead=false;
            const_cast<FakeViews*>(this)->db=replacementDocument;
        }
        return current;
    }
    bool activateView(const QPointer<QObject> &v) const override
    {return v==a || v==b;}
    bool closeView(const QPointer<QObject> &) const override {return true;}
};

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto views=QSharedPointer<FakeViews>::create();
    QObject docA,docB,viewB;
    QPointer<QObject> pointerA(new QObject);
    views->a=pointerA;
    views->b=&viewB;
    views->da=&docA;
    views->db=&docB;
    views->selected=pointerA;
    views->current=&docA;
    QObject window;
    int calls=0;
    QPointer<QObject> last;
    auto command=[&](const QPointer<QObject> &view) {
        ++calls;
        last=view;
    };
    require(MobileDeferredViewCommand::schedule(&window,views,command),
            "valid selected document was refused");
    require(calls==0,"document action ran within toolbar dispatch");
    pump();
    require(calls==1 && last==pointerA,
            "valid command did not target captured canvas");

    // A reentrant engine lookup may detach or replace a different
    // document without changing the currently active one. Dispatch
    // must reject the now-stale workspace even in that case.
    QObject replacedB;
    views->replacementDocument=&replacedB;
    require(MobileDeferredViewCommand::schedule(&window,views,command),
            "reentrant document scenario was refused");
    views->replaceDocumentOnRead=true;
    pump();
    require(calls==1,"reentrant view change allowed stale command");
    views->db=&docB;

    require(MobileDeferredViewCommand::schedule(&window,views,command),
            "second command was refused");
    views->selected=&viewB;
    views->current=&docB;
    pump();
    require(calls==1,"selection switch redirected deferred command");

    views->selected=pointerA;
    views->current=&docA;
    require(MobileDeferredViewCommand::schedule(&window,views,command),
            "third command was refused");
    views->current=&docB;
    pump();
    require(calls==1,"document identity switch redirected command");

    views->current=&docA;
    views->ambiguous=true;
    require(!MobileDeferredViewCommand::schedule(&window,views,command),
            "two active views allowed an unsafe command");
    views->ambiguous=false;

    require(MobileDeferredViewCommand::schedule(&window,views,command),
            "destroyed-view scenario was refused");
    delete pointerA.data();
    pump();
    require(calls==1,"destroyed canvas was used by deferred command");

    // New test owner, to prove QObject timer context cancels on close.
    auto *owner=new QObject;
    views->selected=&viewB;
    views->current=&docB;
    require(MobileDeferredViewCommand::schedule(owner,views,command),
            "destroyed-owner scenario was refused");
    delete owner;
    pump();
    require(calls==1,"action ran after window destruction");
    qInfo() << "PASS: deferred Save/Close identity and lifetime guards";
    return 0;
}
