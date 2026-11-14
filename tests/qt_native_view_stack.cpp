/*
 * Qt-only regression for a lightweight real-widget document stack.
 * No KisView or Krita engine is instantiated by this smoke test.
 */
#include <QApplication>
#include <QAction>
#include <QDebug>
#include <QDialog>
#include <QTimer>
#include <QLabel>
#include <QPointer>
#include <QSharedPointer>
#include <QVBoxLayout>
#include "mobile_core/native_view_stack.h"
#include "mobile_core/lightweight_canvas_host.h"
#include "mobile_core/view_identity_guard.h"

static void require(bool passed,const char *why)
{
    if(!passed)qFatal("Native view stack test failed: %s",why);
}
struct DummyDocuments final : MobileDocumentHostPort {
    bool available() const override {return true;}
    bool attachNewDocument(KisDocument *d) const override {return d!=nullptr;}
    bool openFile(const QString &s) const override {return !s.isEmpty();}
    bool save(KisDocument *d,bool) const override {return d!=nullptr;}
};
struct DummyActions final : MobileCommandRegistryPort {
    QAction *action(const QString &) const override {return nullptr;}
    QList<QAction*> availableActions() const override {return {};}
    QList<QAction*> menuRootActions() const override {return {};}
};
struct ParentCounter final : QObject {
    int changes=0;
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::ParentChange)++changes;
        return false;
    }
};
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QSharedPointer<MobileViewHostPort> retainedPort;
    QPointer<QObject> expiredView;
    {
    MobileLightweightCanvasHost host;
    host.resize(440,750);
    auto *root=host.createNativeDocumentRoot();
    require(root && host.nativeViewPort() && host.retainsOriginalCanvas(),
            "lightweight application could not create a native view root");
    require(!host.createNativeDocumentRoot(),
            "host replaced existing native canvas root");
    int selectionChanges=0;
    QObject::connect(root,&QStackedWidget::currentChanged,&host,
                     [&selectionChanges](int){++selectionChanges;});
    auto *doc1=new QObject(&host),*doc2=new QObject(&host);
    auto *a=new QWidget,*b=new QWidget;
    a->setProperty("drawingRevision",17);
    b->setProperty("drawingRevision",29);
    ParentCounter parentA,parentB;
    a->installEventFilter(&parentA);
    b->installEventFilter(&parentB);
    bool approve=false;
    int closeCalls=0;
    require(root->adoptView(a,doc1,QStringLiteral("A.kra"),
                             [&]() {++closeCalls;return approve;}),
            "failed to adopt first native widget");
    require(root->adoptView(b,doc2,QStringLiteral("B.kra"),
                             [&]() {++closeCalls;return true;}),
            "failed to adopt second native widget");
    require(!root->adoptView(a,doc1,QStringLiteral("duplicate"),
                              [](){return true;}),
            "adopted one native painting view twice");
    require(selectionChanges==1 && root->currentWidget()==a,
            "first QWidget adopt did not emit exactly one selection event");
    // Later view adoption must leave the original current until the
    // caller explicitly switches. Redundant selection must not trigger
    // another brush bootstrap.
    require(root->activateView(QPointer<QObject>(b)) &&
            selectionChanges==2 &&
            root->activateView(QPointer<QObject>(b)) &&
            selectionChanges==2 &&
            root->activateView(QPointer<QObject>(a)) &&
            selectionChanges==3,
            "native document selection sent duplicate activation events");
    QPointer<QObject> aid=a,bid=b;
    MobileHostServices services;
    services.documents=QSharedPointer<DummyDocuments>::create();
    services.commands=QSharedPointer<DummyActions>::create();
    // The application-created view port uses a QPointer to its
    // native document root; a deferred copy can outlive the window.
    services.views=host.nativeViewPort();
    services.documentRoot=root;
    require(host.attachServices(services),"could not attach view port");
    auto *top=new QWidget,*bottom=new QWidget;
    auto *topLayout=new QVBoxLayout(top),*bottomLayout=new QVBoxLayout(bottom);
    topLayout->addWidget(new QLabel(QStringLiteral("Documents")));
    bottomLayout->addWidget(new QLabel(QStringLiteral("Brush controls")));
    require(host.installMobileChrome(top,bottom),
            "could not install lightweight host chrome");
    host.show();
    QApplication::processEvents();
    const int oldA=parentA.changes,oldB=parentB.changes;
    for(int i=0;i<12;++i) {
        require(host.setMobileVisible(true),"enter failed");
        require(root->activateView(i%2?aid:bid),"native view switch failed");
        QApplication::processEvents();
        require(root->currentDocument()==(i%2?doc1:doc2) &&
                root->openViews().size()==2,"wrong active document");
        require(host.setMobileVisible(false),"Back failed");
        QApplication::processEvents();
        require(host.retainsOriginalCanvas() &&
                parentA.changes==oldA && parentB.changes==oldB &&
                a->property("drawingRevision").toInt()==17 &&
                b->property("drawingRevision").toInt()==29,
                "mode transition moved or replaced painting widgets");
    }
    require(root->activateView(aid) &&
            root->updateViewState(aid,QStringLiteral("A renamed.kra"),true),
            "view state update failed");
    require(MobileViewIdentityGuard::maySave(root->openViews(),aid,doc1),
            "selected native view rejected for save");
    require(!MobileViewIdentityGuard::maySave(root->openViews(),bid,doc2),
            "inactive view incorrectly allowed to save");
    require(!host.close() && host.isVisible() &&
            root->openViews().size()==2 && closeCalls==1,
            "Qt window close bypassed canceled native document prompt");
    require(!root->closeView(aid) && root->openViews().size()==2 &&
            root->currentDocument()==doc1 && closeCalls==2,
            "canceled native close destroyed document");
    approve=true;
    require(root->closeView(aid) && root->openViews().size()==1 &&
            root->currentDocument()==doc2 && closeCalls==3 &&
            a->property("drawingRevision").toInt()==17,
            "accepted close targeted wrong view or destroyed canvas");
    require(!root->closeView(aid) && !root->activateView(aid),
            "stale closed view remained actionable");
    delete doc2;
    require(root->openViews().isEmpty() && !root->currentDocument() &&
            !root->activateView(bid) && !root->closeView(bid) &&
            host.retainsOriginalCanvas(),
            "destroyed engine document left dangerous view operations");
    // A native view is allowed to delete itself synchronously from
    // its close callback, just as the real Krita factory does. That
    // destroys the QObject while closeImpl is still on the stack.
    auto *doc3=new QObject(&host);
    auto *c=new QWidget;
    QPointer<QObject> cid=c;
    QPointer<QWidget> guardedC=c;
    require(root->adoptView(c,doc3,QStringLiteral("Close now"),
                            [c](){delete c;return true;}),
            "could not adopt synchronous-close test widget");
    require(root->closeView(cid) && !guardedC &&
            root->openViews().isEmpty(),
            "synchronous native widget destruction invalidated stack");
    retainedPort=services.views;
    expiredView=bid;
    } // Destroy the real Qt host, its document root and child views.
    require(retainedPort && retainedPort->openViews().isEmpty() &&
            !retainedPort->currentDocument() &&
            !retainedPort->activateView(expiredView) &&
            !retainedPort->closeView(expiredView),
            "view port accessed freed native host after destruction");
    // Removing the active view emits currentChanged, and listeners may
    // synchronously destroy the stack (e.g. closing the last window).
    // Its forgetToken cleanup must not touch the now-destroyed host.
    {
        QPointer<MobileNativeViewStack> closingStack=new MobileNativeViewStack;
        auto *closingDoc=new QObject;
        auto *closingWidget=new QWidget;
        require(closingStack->adoptView(closingWidget,closingDoc,
                    QStringLiteral("last tab"),[](){return true;}),
                "could not set up last-view teardown test");
        const QPointer<QObject> closingIdentity(closingWidget);
        QObject::connect(closingStack.data(),&QStackedWidget::currentChanged,
                         closingStack.data(),[&closingStack](int) {
            if(closingStack)delete closingStack.data();
        });
        require(closingStack->forgetView(closingIdentity) && !closingStack,
                "native stack removal used destroyed host after selection signal");
        delete closingDoc;
    }
    // A native Save As dialog can process queued activity events while
    // the runtime stays alive but its old view stack is replaced.
    // Never authorize a save using a pointer captured before the modal.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(new MobileNativeViewStack(&owner));
        MobileNativeViewStack *replacement=nullptr;
        QDialog picker(&owner);
        QTimer::singleShot(0,&app,[&]() {
            delete original.data();
            replacement=new MobileNativeViewStack(&owner);
            picker.accept();
        });
        require(picker.exec()==QDialog::Accepted,
                "test modal did not resume after replacing document root");
        require(!original && replacement &&
                original.data()!=replacement,
                "stale native document root was not invalidated by Qt");
    }
    // Save completion can arrive after a nested Qt loop replaces the
    // original native root. A successful I/O notification alone must
    // not authorize post-save history publication on that stale host.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(new MobileNativeViewStack(&owner));
        MobileNativeViewStack *runtimeRoot=original.data();
        QDialog saveCompletion(&owner);
        bool ioCompleted=false;
        QTimer::singleShot(0,&app,[&]() {
            ioCompleted=true;
            delete original.data();
            runtimeRoot=new MobileNativeViewStack(&owner);
            saveCompletion.accept();
        });
        require(saveCompletion.exec()==QDialog::Accepted && ioCompleted,
                "test save completion was not delivered");
        const bool publishHistory=ioCompleted && original &&
                                  runtimeRoot==original.data();
        require(!publishHistory && runtimeRoot &&
                runtimeRoot->openViews().isEmpty(),
                "late save completion published history for replaced root");
    }
    // Krita openPath may reenter the event loop before registration.
    // A pending import must not be attached to a replacement native root.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(new MobileNativeViewStack(&owner));
        auto *pendingDocument=new QObject;
        QPointer<QObject> pending(pendingDocument);
        QDialog importer(&owner);
        MobileNativeViewStack *replacement=nullptr;
        QTimer::singleShot(0,&app,[&]() {
            delete original.data();
            replacement=new MobileNativeViewStack(&owner);
            importer.accept();
        });
        require(importer.exec()==QDialog::Accepted,
                "test import dialog did not resume");
        if(!original || original.data()!=replacement)
            delete pendingDocument;
        require(!original && replacement && !pending &&
                replacement->openViews().isEmpty(),
                "stale import leaked document into a replacement root");
    }
    // Recovery Open may try several checkpoints before the original
    // project. A failed first load which tears down the native root
    // must abort retries rather than construct a second stale document.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(new MobileNativeViewStack(&owner));
        MobileNativeViewStack *replacement=nullptr;
        int attempts=0;
        for(int checkpoint=0;checkpoint<3;++checkpoint) {
            if(!original || replacement)break;
            ++attempts;
            if(checkpoint==0) {
                delete original.data(); // Reentrant failed openPath callback.
                replacement=new MobileNativeViewStack(&owner);
            }
            if(!original || replacement)break;
        }
        require(attempts==1 && !original && replacement &&
                replacement->openViews().isEmpty(),
                "failed KRA recovery retried after native root replacement");
    }
    // Autosave iterates a snapshot of native canvases. A reentrant
    // checkpoint callback can replace the root while that snapshot
    // still contains another document; do not visit that stale canvas.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(new MobileNativeViewStack(&owner));
        auto *firstDoc=new QObject(&owner);
        auto *secondDoc=new QObject(&owner);
        require(original->adoptView(new QWidget,firstDoc,
                    QStringLiteral("first"),[](){return true;}) &&
                original->adoptView(new QWidget,secondDoc,
                    QStringLiteral("second"),[](){return true;}),
                "could not set up two-canvas autosave traversal");
        const auto views=original->openViews();
        MobileNativeViewStack *currentRoot=original.data();
        int visited=0;
        for(const auto &view:views) {
            if(!original || original.data()!=currentRoot)break;
            require(view.document,"autosave snapshot lost document identity");
            ++visited;
            if(visited==1) {
                delete original.data();
                currentRoot=new MobileNativeViewStack(&owner);
            }
        }
        require(visited==1 && !original &&
                currentRoot->openViews().isEmpty(),
                "autosave traversed stale canvases after root replacement");
    }
    // Krita's createView() can synchronously invoke destruction callbacks
    // before its caller registers input tracking. The caller must check
    // its original manager/document/root identities first.
    {
        auto *owner=new QWidget;
        QPointer<QWidget> manager(owner);
        auto *document=new QObject;
        QPointer<QObject> guardedDocument(document);
        QPointer<MobileNativeViewStack> root(new MobileNativeViewStack(owner));
        QWidget *created=nullptr;
        const auto createView=[&]() {
            created=new QWidget; // A native view that was never adopted.
            delete owner; // Reentrant Android host/manager teardown.
            return created;
        };
        QPointer<QWidget> view(createView());
        const bool mayInitialize=view && manager &&
            guardedDocument && root;
        require(!mayInitialize && !manager && !root && view,
                "native view creation did not invalidate destroyed dependencies");
        delete view.data();
        delete document;
    }
    // Adding the first canvas emits currentChanged synchronously.
    // A Krita selection listener can destroy the view manager while
    // the document and stack remain live. Factory initialization must
    // treat that manager identity as invalid before wiring callbacks.
    {
        MobileNativeViewStack stack;
        auto *manager=new QObject;
        const QPointer<QObject> guardedManager(manager);
        auto *document=new QObject;
        auto *view=new QWidget;
        QObject::connect(&stack,&QStackedWidget::currentChanged,
                         &stack,[manager](int) {delete manager;});
        require(stack.adoptView(view,document,QStringLiteral("activate"),
                                [](){return true;}),
                "could not exercise synchronous manager teardown on registration");
        require(!guardedManager && stack.openViews().size()==1,
                "registration did not expose a stale manager after activation");
        delete document;
    }
    // A native view can emit Show/Activate callbacks that close its
    // document while keeping the QWidget and stack alive. Treat the
    // original document identity as part of successful final attachment.
    {
        QWidget owner;
        MobileNativeViewStack stack(&owner);
        auto *document=new QObject;
        QPointer<QObject> guardedDocument(document);
        auto *view=new QWidget;
        QPointer<QWidget> guardedView(view);
        require(stack.adoptView(view,document,QStringLiteral("show"),
                                [](){return true;}),
                "failed to register show-time teardown canvas");
        // This models a synchronous application callback dispatched by
        // view->show(), not a real Krita event listener.
        const auto showCallback=[document]() {delete document;};
        showCallback();
        const bool mayFinishAttach=guardedView && guardedDocument &&
                                   stack.openViews().size()==1;
        require(!mayFinishAttach && !guardedDocument && guardedView &&
                stack.openViews().isEmpty(),
                "destroyed document remained eligible for final attachment");
    }
    qInfo() << "PASS: MDI-free view stack, Qt host close cancellation,"
            << "synchronous native destruction, twelve no-reparent"
            << "transitions and destroyed-host safety";
    return 0;
}
