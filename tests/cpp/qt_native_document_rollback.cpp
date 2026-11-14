#include <QCoreApplication>
#include <QPointer>
#include <QDebug>
#include "mobile_studio/native_document_rollback.h"

struct FakeDocument final : QObject {
    using QObject::QObject;
    ~FakeDocument() override {++destroyed;}
    static int destroyed;
};
int FakeDocument::destroyed=0;
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
    qInfo()<<"PASS: attach failure rollback, duplicate prevention and"
              "ownership isolation";
    return 0;
}
