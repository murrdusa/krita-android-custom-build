#include <QCoreApplication>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>
#include <atomic>
#include <memory>
#include <QEnableSharedFromThis>
#include <functional>
#include <QDebug>
#include "mobile_core/native_document_rollback.h"

struct FakeDocument final : QObject {
    using QObject::QObject;
    ~FakeDocument() override {++destroyed;}
    static int destroyed;
};
int FakeDocument::destroyed=0;

// The native document backend uses this same shared-ownership contract:
// synchronous Krita callbacks may release its owning window while an
// Open/Save transaction still has rollback state on the call stack.
struct ReentrantDocumentBackend final :
    QEnableSharedFromThis<ReentrantDocumentBackend> {
    explicit ReentrantDocumentBackend(int &destructions):m_destructions(destructions) {}
    ~ReentrantDocumentBackend() {++m_destructions;}
    bool run(const std::function<void()> &nested) const {
        const auto keepAlive=sharedFromThis();
        if(!keepAlive)return false;
        nested();
        return m_destructions==0;
    }
    int &m_destructions;
};
static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Document rollback regression: %s",reason);
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    int registeredChecks=0,removeCalls=0;
    auto remove=[&](FakeDocument *doc) {++removeCalls;delete doc;};
    auto registered=[&](FakeDocument *) {++registeredChecks;return true;};
    // If the failed native view already triggered KisPart removal, the
    // source document becomes null before rollback is even attempted.
    auto *first=new FakeDocument;
    QPointer<FakeDocument> firstIdentity=first;
    delete first;
    require(!MobileFailedDocumentAttach::rollback(
                firstIdentity,registered,remove),"destroyed document re-removed");
    require(registeredChecks==0 && removeCalls==0,
            "destroyed document passed to registry");
    auto *second=new FakeDocument;
    QPointer<FakeDocument> secondIdentity=second;
    require(MobileFailedDocumentAttach::rollback(
                secondIdentity,registered,remove),"registered orphan not removed");
    require(!secondIdentity && removeCalls==1,
            "orphan cleanup did not destroy document exactly once");
    require(!MobileFailedDocumentAttach::rollback(
                secondIdentity,registered,remove),"repeated rollback succeeded");
    auto *third=new FakeDocument;
    QPointer<FakeDocument> thirdIdentity=third;
    auto missing=[&](FakeDocument *){++registeredChecks;return false;};
    require(!MobileFailedDocumentAttach::rollback(
                thirdIdentity,missing,remove),"unregistered document removed");
    require(thirdIdentity && removeCalls==1,
            "registry guard removed another window's document");
    delete third;
    require(FakeDocument::destroyed==3,"lost or double destroyed document");
    int backendDestructions=0;
    auto backend=QSharedPointer<ReentrantDocumentBackend>::create(
        backendDestructions);
    const auto *running=backend.data();
    require(running->run([&backend]() {backend.clear();}) &&
            backend.isNull() && backendDestructions==1,
            "reentrant owner release destroyed in-flight document backend");

    // A queued completion notification may already be in Qt's event
    // queue when Save disconnects its listener. Its closure must own
    // everything it uses instead of referencing the finished call stack.
    QTimer saveSignal;
    QObject documentContext;
    auto completed=std::make_shared<std::atomic_bool>(false);
    const QString expected=QStringLiteral("drawing.kra");
    const auto connection=QObject::connect(
        &saveSignal,&QTimer::timeout,&documentContext,
        [completed,expected]() {
            if(expected==QStringLiteral("drawing.kra"))
                completed->store(true);
        },Qt::QueuedConnection);
    saveSignal.timeout();
    QObject::disconnect(connection);
    // The signal may be dropped on some Qt versions after disconnect;
    // whether delivered or dropped, it may not retain dangling data.
    app.processEvents();

    qInfo()<<"PASS: attach failure rollback, duplicate prevention and"
              "ownership isolation";
    return 0;
}
