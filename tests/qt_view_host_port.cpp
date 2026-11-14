/*
 * Qt-only fake document view host: no QMdiArea, KisView, KisMainWindow,
 * Android activity, or painting engine. The same guarded operations
 * must work with a future independent mobile-first view host.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QVector>
#include "mobile_core/view_host_port.h"
#include "mobile_core/view_identity_guard.h"

static void require(bool value,const char *reason)
{
    if(!value)qFatal("Mobile view host port test failed: %s",reason);
}

struct FakeMobileViewHost final : MobileViewHostPort
{
    QVector<ViewSnapshot> snapshots;
    QPointer<QObject> engineCurrent;
    QPointer<QObject> currentDocument() const override {
        return engineCurrent;
    }
    QPointer<QObject> lastActivated;
    QPointer<QObject> lastClosed;
    int closeCalls=0;
    QVector<ViewSnapshot> openViews() const override {return snapshots;}
    bool activateView(const QPointer<QObject> &id) const override {
        for(const auto &view:snapshots) {
            if(view.nativeView && view.nativeView==id) {
                const_cast<FakeMobileViewHost*>(this)->lastActivated=id;
                return true;
            }
        }
        return false;
    }
    bool closeView(const QPointer<QObject> &id) const override {
        if(!id)return false;
        for(const auto &view:snapshots) {
            if(view.nativeView==id) {
                auto *self=const_cast<FakeMobileViewHost*>(this);
                ++self->closeCalls;
                self->lastClosed=id;
                // Real adapter calls Krita's native queryClose()/prompt.
                // The fake intentionally never deletes a document.
                return true;
            }
        }
        return false;
    }
};

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QObject doc1,doc2,viewA,viewB,viewC;
    FakeMobileViewHost host;
    host.engineCurrent=&doc1;
    require(host.currentDocument()==&doc1,
            "injected host did not expose engine-selected document");
    using Guard=MobileViewIdentityGuard;
    host.snapshots={
        {QStringLiteral("A.kra"),true,true,0,&viewA,&doc1},
        {QStringLiteral("B.kra"),false,false,1,&viewB,&doc2}
    };
    const auto before=host.openViews();
    const QPointer<QObject> selected=Guard::uniqueActive(before);
    require(selected==&viewA && Guard::maySave(before,selected,&doc1),
            "selected document was not retained for native save");
    require(!Guard::maySave(before,selected,&doc2),
            "deferred Save could write the wrong current document");
    require(Guard::maySave(before,Guard::uniqueActive(before),&doc1),
            "current Save rejected the one active matching document");
    require(!Guard::maySave(before,Guard::uniqueActive(before),&doc2),
            "current Save ignored an engine document switch");
    require(Guard::byIndex(before,1)==&viewB,
            "live creation-order lookup incorrect");

    // Qt closes a tab while a confirmation dialog is open. The old
    // index now points at another view; only the captured pointer is safe.
    host.snapshots.removeFirst();
    host.engineCurrent=&doc2;
    require(host.currentDocument()==&doc2,
            "new engine document was not selected after view switch");
    host.snapshots[0].nativeIndex=0;
    host.snapshots[0].active=true;
    require(!Guard::maySave(host.openViews(),selected,&doc1),
            "stale selected view was accepted after the document switched");
    require(!host.closeView(selected),
            "missing native view was resolved to a different document");
    require(Guard::byIndex(host.openViews(),0)==&viewB,
            "current index did not reflect native reordering");

    // Even if a malicious/buggy host labels two rows with the same
    // legacy index, index-based commands must refuse ambiguity.
    host.snapshots.append({QStringLiteral("C.kra"),false,false,0,
                           &viewC,&doc1});
    require(!Guard::byIndex(host.openViews(),0),
            "duplicated index was allowed to select an arbitrary view");
    host.snapshots[1].nativeIndex=1;

    // Closing with no active native tab is unsafe even if the engine
    // still exposes a unique selected document from the previous tab.
    host.snapshots[0].active=false;
    host.snapshots[1].active=false;
    host.snapshots[1].document=&doc1;
    require(!Guard::safeCloseTarget(host.openViews(),&doc2),
            "inactive view unexpectedly became closable");
    host.snapshots[1].document=&doc2;
    require(!Guard::safeCloseTarget(host.openViews(),&doc2),
            "inactive document was closed without native selection");
    // A duplicate backing document or view must also disallow Save and
    // Close when one of the conflicting rows is active.
    host.snapshots[0].active=true;
    host.snapshots[1].document=&doc1;
    host.snapshots[0].document=&doc1;
    require(!Guard::safeCloseTarget(host.openViews(),&doc1) &&
            !Guard::maySave(host.openViews(),&viewB,&doc1),
            "duplicate document ownership allowed destructive action");

    host.snapshots[0].active=true;
    host.snapshots[1].active=true;
    require(!Guard::uniqueActive(host.openViews()) &&
            !Guard::safeCloseTarget(host.openViews(),&doc1) &&
            !Guard::maySave(host.openViews(),&viewB,&doc1) &&
            !Guard::maySave(host.openViews(),
                            Guard::uniqueActive(host.openViews()),&doc1),
            "ambiguous active views enabled save/close");
    host.snapshots[1].active=false;
    require(host.activateView(&viewB) && host.closeView(&viewB) &&
            host.lastActivated==&viewB && host.lastClosed==&viewB &&
            host.closeCalls==1,
            "injected lightweight host did not handle exact view identity");

    auto *temporary=new QObject;
    host.snapshots={{QStringLiteral("Temporary.kra"),true,true,0,
                     temporary,&doc1}};
    QPointer<QObject> pending=Guard::uniqueActive(host.openViews());
    require(pending==temporary,"temporary live view not captured");
    delete temporary;
    require(!pending && !Guard::maySave(host.openViews(),pending,&doc1) &&
            !Guard::safeCloseTarget(host.openViews(),&doc1),
            "destroyed view remained eligible for document operations");

    qInfo() << "PASS: host-neutral view port, stable IDs, reused indices,"
            << "deferred Save safety, ambiguous same-document views,"
            << "real native close delegation and invalidated QPointers";
    return 0;
}
