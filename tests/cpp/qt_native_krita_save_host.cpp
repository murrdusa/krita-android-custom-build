/*
 * Native Krita view Save / Discard / Cancel host boundary test.
 * This tests synchronous save forwarding and cancellation guards; it
 * does not instantiate KisView or an Android GL painting surface.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QSharedPointer>
#include <functional>
#include "mobile_studio/native_krita_save_host.h"

class KisDocument {};
static void require(bool ok,const char *why)
{
    if(!ok)qFatal("Native save host test failed: %s",why);
}
struct FakeDocumentPort final : MobileDocumentHostPort {
    bool online=true;
    bool result=true;
    int attempts=0;
    bool lastSaveAs=false;
    std::function<void()> duringSave;
    bool available() const override {return online;}
    bool attachNewDocument(KisDocument *d) const override {return d!=nullptr;}
    bool openFile(const QString &s) const override {return !s.isEmpty();}
    bool save(KisDocument *d,bool as) const override {
        if(!online || !d)return false;
        auto *self=const_cast<FakeDocumentPort*>(this);
        ++self->attempts;
        self->lastSaveAs=as;
        if(self->duringSave)self->duringSave();
        return self->result;
    }
};
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto fake=QSharedPointer<FakeDocumentPort>::create();
    auto *adapter=new MobileKritaNativeSavePortAdapter(fake);
    QPointer<MobileKritaNativeSaveHost> guarded=adapter;
    KisDocument document;
    require(!adapter->save(nullptr,false) && fake->attempts==0,
            "null document passed to backend");
    require(adapter->save(&document,true) && fake->attempts==1 &&
            fake->lastSaveAs && !adapter->saving(),
            "valid Save As did not delegate to document host");
    fake->duringSave=[&]() {
        require(adapter->saving() &&
                !adapter->save(&document,false) &&
                fake->attempts==2,
                "reentrant save was allowed during native transaction");
    };
    require(adapter->save(&document,false) && fake->attempts==2 &&
            !fake->lastSaveAs && !adapter->saving(),
            "reentrant guard did not recover after Save");
    fake->duringSave=nullptr;
    fake->result=false;
    require(!adapter->save(&document,false) && fake->attempts==3,
            "failed native backend Save was reported successful");
    fake->online=false;
    require(!adapter->save(&document,false) && fake->attempts==3,
            "unavailable backend allowed close-time Save");
    delete adapter;
    require(!guarded,"destroyed save callback was not invalidated");
    qInfo() << "PASS: native close-save host, Save As delegation,"
            << "reentrant transaction guard, failure and invalidation";
    return 0;
}
