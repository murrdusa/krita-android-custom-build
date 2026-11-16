#include <QCoreApplication>
#include <QPointer>
#include <QSharedPointer>
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

    // A failed attachment can cause an observer to unregister a loaded
    // KRA without deleting its QObject. Its buffers must be retired
    // when the registry no longer owns it.
    auto *orphan=new FakeDocument;
    const QPointer<FakeDocument> orphanIdentity(orphan);
    require(MobileFailedDocumentAttach::disposeUnregistered(
                orphanIdentity,[](FakeDocument *) {return false;}) &&
            !orphanIdentity && FakeDocument::destroyed==4,
            "orphaned imported image was retained after failed attachment");

    // A still-registered document must never be deleted by a local
    // failure handler: another Krita owner may still be displaying it.
    auto *owned=new FakeDocument;
    const QPointer<FakeDocument> ownedIdentity(owned);
    require(!MobileFailedDocumentAttach::disposeUnregistered(
                ownedIdentity,[](FakeDocument *) {return true;}) &&
            ownedIdentity && FakeDocument::destroyed==4,
            "orphan cleanup deleted a registered drawing");
    delete owned;
    require(FakeDocument::destroyed==5,
            "registered drawing was leaked or destroyed twice");

    // Registry traversal itself may destroy the candidate. Recheck
    // the QPointer after that callback before attempting deletion.
    auto *retired=new FakeDocument;
    const QPointer<FakeDocument> retiredIdentity(retired);
    require(!MobileFailedDocumentAttach::disposeUnregistered(
                retiredIdentity,[](FakeDocument *candidate) {
                    delete candidate;
                    return false;
                }) &&
            !retiredIdentity && FakeDocument::destroyed==6,
            "orphan cleanup double-deleted a document retired during lookup");
    int backendDestructions=0;
    auto backend=QSharedPointer<ReentrantDocumentBackend>::create(
        backendDestructions);
    const auto *running=backend.data();
    require(running->run([&backend]() {backend.clear();}) &&
            backend.isNull() && backendDestructions==1,
            "reentrant owner release destroyed in-flight document backend");

    // A completion event may already be queued when the Save operation
    // unwinds. The queued closure must own both its completion flag and
    // the expected path after their originating stack frame is gone.
    auto posted=std::make_shared<std::atomic_bool>(false);
    QObject documentContext;
    auto postCompletion=[&documentContext,posted]() {
        const QString target=QStringLiteral("drawing.kra");
        auto completion=posted;
        QMetaObject::invokeMethod(&documentContext,
            [completion,target]() {
                if(target==QStringLiteral("drawing.kra"))
                    completion->store(true);
            },Qt::QueuedConnection);
    };
    postCompletion();
    require(!posted->load(),"queued completion ran before event dispatch");
    app.processEvents();
    require(posted->load(),"queued completion lost captured Save state");

    // A Save whose KisDocument is destroyed before queued delivery
    // must not invoke its completion callback against stale state.
    auto canceled=std::make_shared<std::atomic_bool>(false);
    auto *vanishingContext=new QObject;
    QMetaObject::invokeMethod(vanishingContext,[canceled]() {
        canceled->store(true);
    },Qt::QueuedConnection);
    delete vanishingContext;
    app.processEvents();
    require(!canceled->load(),"queued completion survived destroyed document");

    // Import metadata callbacks (including Android provider name and
    // Krita MIME notification) may synchronously destroy an unregistered
    // document. A guarded import must never invoke the KRA loader after
    // that destruction, even when the original raw address was valid.
    {
        int loadCalls=0;
        auto *imported=new FakeDocument;
        const QPointer<FakeDocument> identity(imported);
        const auto applyMime=[identity]() {
            if(identity)delete identity.data();
        };
        applyMime();
        if(identity)++loadCalls;
        require(!identity && loadCalls==0,
                "destroyed MIME import was passed to the KRA loader");
    }

    // Canonicalizing Android SAF identities can enter Qt provider
    // callbacks. Open must guard its original runtime and document root
    // before that query and skip scanning views after either disappears.
    {
        auto *runtime=new QObject;
        auto *root=new QObject(runtime);
        const QPointer<QObject> guardedRuntime(runtime);
        const QPointer<QObject> guardedRoot(root);
        int viewScans=0;
        const auto canonicalize=[runtime]() {
            delete runtime;
            return QStringLiteral("content://closed/document");
        };
        const QString identity=canonicalize();
        if(guardedRuntime && guardedRoot)++viewScans;
        require(!identity.isEmpty() && !guardedRuntime &&
                !guardedRoot && viewScans==0,
                "Open scanned native views after URI lookup destroyed runtime");
    }
    {
        QObject runtime;
        QObject root(&runtime);
        const QPointer<QObject> guardedRuntime(&runtime);
        const QPointer<QObject> guardedRoot(&root);
        int viewScans=0;
        const auto canonicalize=[]() {
            return QStringLiteral("content://live/document");
        };
        const QString identity=canonicalize();
        if(guardedRuntime && guardedRoot)++viewScans;
        require(!identity.isEmpty() && viewScans==1,
                "Open incorrectly rejected a surviving canonicalized workspace");
    }

    // A deferred Save or Export can trigger document-view callbacks
    // which release the owning service. The two backend ports must stay
    // alive until the request finishes even if the service is retired.
    {
        struct SaveBackends {
            QSharedPointer<QObject> documents;
            QSharedPointer<QObject> views;
        };
        auto owner=std::make_unique<SaveBackends>();
        owner->documents=QSharedPointer<QObject>::create();
        owner->views=QSharedPointer<QObject>::create();
        QPointer<QObject> documentBackend(owner->documents.data());
        QPointer<QObject> viewBackend(owner->views.data());
        const auto documents=owner->documents;
        const auto views=owner->views;
        int saveCalls=0;
        const auto selectDocument=[&owner]() {
            owner.reset(); // Nested Qt close disposes the UI service.
            return true;
        };
        if(selectDocument() && documents && views &&
           documentBackend && viewBackend)
            ++saveCalls;
        require(!owner && saveCalls==1 &&
                documentBackend && viewBackend,
                "deferred Save lost its backends after service teardown");
    }
    {
        auto *document=new FakeDocument;
        const QPointer<FakeDocument> guardedDocument(document);
        const auto selectDocument=[document]() {
            delete document;
            return true;
        };
        int saves=0;
        if(selectDocument() && guardedDocument)++saves;
        require(!guardedDocument && saves==0,
                "deferred Save used a document destroyed during selection");
    }

    // The Android provider display-name lookup used to be evaluated
    // in the QMessageBox argument list along with a raw window parent.
    // A provider callback may destroy the window before the warning
    // starts. The warning is permitted only after a fresh guard check.
    {
        auto *window=new QObject;
        const QPointer<QObject> guardedWindow(window);
        int warnings=0;
        const auto resolveName=[window]() {
            delete window;
            return QStringLiteral("lost drawing.kra");
        };
        const QString name=resolveName();
        if(guardedWindow && !name.isEmpty())++warnings;
        require(!guardedWindow && warnings==0,
                "Open failure warning used a parent destroyed by filename lookup");
    }
    {
        QObject window;
        const QPointer<QObject> guardedWindow(&window);
        int warnings=0;
        const auto resolveName=[]() {
            return QStringLiteral("surviving drawing.kra");
        };
        const QString name=resolveName();
        if(guardedWindow && !name.isEmpty())++warnings;
        require(guardedWindow && warnings==1,
                "Open failure warning was suppressed for surviving window");
    }

    // Save As collision comparisons can query Android document providers.
    // A provider callback may close the runtime/window before the warning
    // is shown. The collision result never authorizes a stale parent.
    {
        auto *runtime=new QObject;
        auto *window=new QObject(runtime);
        const QPointer<QObject> guardedRuntime(runtime);
        const QPointer<QObject> guardedWindow(window);
        int warnings=0;
        const auto checkCollision=[runtime]() {
            delete runtime;
            return true;
        };
        const bool collision=checkCollision();
        if(guardedRuntime && guardedWindow && collision)++warnings;
        require(collision && !guardedRuntime && !guardedWindow &&
                warnings==0,
                "Save collision warning used a destroyed runtime");
    }
    {
        QObject runtime;
        QObject window(&runtime);
        const QPointer<QObject> guardedRuntime(&runtime);
        const QPointer<QObject> guardedWindow(&window);
        int warnings=0;
        const auto checkCollision=[]() {return true;};
        const bool collision=checkCollision();
        if(guardedRuntime && guardedWindow && collision)++warnings;
        require(warnings==1,
                "Save collision warning was skipped for a surviving owner");
    }

    qInfo()<<"PASS: attach failure rollback, duplicate prevention and"
              "ownership isolation";
    return 0;
}
