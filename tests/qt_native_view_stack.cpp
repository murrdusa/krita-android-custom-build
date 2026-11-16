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
    // A reentrant save-ownership query can destroy the selected canvas
    // and original root before the render-wait dialog is constructed.
    // Guard both identities before touching document->image().
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> root(
            new MobileNativeViewStack(&owner));
        auto *document=new QObject;
        QPointer<QObject> guardedDocument(document);
        int renderImageReads=0;
        const auto ownsDocument=[&]() {
            delete document;
            delete root.data();
            return true; // A stale result must never authorize saving.
        };
        const bool owns=ownsDocument();
        if(owns && guardedDocument && root)
            ++renderImageReads;
        require(owns && !guardedDocument && !root &&
                renderImageReads==0,
                "save ownership callback read a destroyed document image");
    }
    // Publishing successful Save metadata can notify Recent Files
    // listeners which synchronously close the document/root. Capture
    // the draft identity first and never query the document again after
    // a reentrant publication callback.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> root(new MobileNativeViewStack(&owner));
        auto *document=new QObject;
        const QPointer<QObject> guardedDocument(document);
        const QString savedDraft=QStringLiteral("private-draft.kra");
        int documentReadsAfterPublication=0;
        bool publishedHistory=false;
        const auto publishPreview=[&]() {
            delete document;
            delete root.data();
        };
        publishPreview();
        if(guardedDocument && root)
            ++documentReadsAfterPublication;
        if(guardedDocument && root)
            publishedHistory=!savedDraft.isEmpty();
        require(!guardedDocument && !root &&
                documentReadsAfterPublication==0 && !publishedHistory,
                "save metadata callback continued after document/root teardown");
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
    // Creating an import can invoke an engine/plugin callback before
    // openPath() begins. If that callback replaces the native root,
    // dispose of the unregistered document without allocating a KRA
    // image or proceeding into any recovery attempt.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> initial(new MobileNativeViewStack(&owner));
        MobileNativeViewStack *activeRoot=initial.data();
        QPointer<QObject> created;
        bool startedLoad=false;
        const auto createDocument=[&]() -> QObject* {
            auto *document=new QObject;
            created=document;
            delete initial.data();
            activeRoot=new MobileNativeViewStack(&owner);
            return document;
        };
        QObject *candidate=createDocument();
        QPointer<QObject> pending(candidate);
        const bool validImport=initial && activeRoot==initial.data() &&
                               pending;
        if(!validImport)delete pending.data();
        else startedLoad=true;
        require(!initial && !created && !startedLoad &&
                activeRoot && activeRoot->openViews().isEmpty(),
                "document constructor teardown allowed a stale KRA load");
    }
    // MIME metadata resolution may replace the native root without
    // destroying an in-flight import. The loader must recheck both
    // identities before reading a KRA, not only the document pointer.
    {
        QWidget owner;
        QPointer<MobileNativeViewStack> original(
            new MobileNativeViewStack(&owner));
        MobileNativeViewStack *runtimeRoot=original.data();
        auto *imported=new QObject;
        const QPointer<QObject> guardedImport(imported);
        bool startedLoad=false;
        const auto resolveMime=[&]() {
            delete original.data();
            runtimeRoot=new MobileNativeViewStack(&owner);
        };
        resolveMime();
        if(guardedImport && original &&
           runtimeRoot==original.data())
            startedLoad=true;
        require(!original && guardedImport && !startedLoad &&
                runtimeRoot && runtimeRoot->openViews().isEmpty(),
                "MIME callback allowed import into a replaced native root");
        delete imported;
    }
    // A first-frame GL retry checks the selected document before
    // asking the view host for its active document. That host lookup
    // can synchronously close the document. A QPointer identity must
    // cancel activation rather than dereference the stale document.
    {
        QWidget owner;
        MobileNativeViewStack root(&owner);
        auto *document=new QObject;
        const QPointer<QObject> selectedDocument(document);
        auto *view=new QWidget;
        require(root.adoptView(view,document,QStringLiteral("GL retry"),
                               [](){return true;}),
                "could not register GL retry identity fixture");
        const QPointer<QObject> retryDocument(selectedDocument.data());
        QObject::connect(&root,&QStackedWidget::currentChanged,
                         &root,[](int){});
        // Model a reentrant document-close notification while evaluating
        // the active document; the view remains but is no longer owned.
        const auto queryActive=[&]() -> QPointer<QObject> {
            const auto active=root.currentDocument();
            delete document;
            return active;
        };
        const auto active=queryActive();
        int activations=0;
        if(retryDocument && selectedDocument && active==retryDocument &&
           root.currentDocument()==retryDocument)
            ++activations;
        require(!retryDocument && !selectedDocument && activations==0,
                "GL retry activated a document destroyed during ownership query");
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
                         &stack,[guardedManager](int index) {
            if(index>=0 && guardedManager)delete guardedManager.data();
        });
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
    // A closed Krita document can die before its QWidget painting view.
    // Keep the native stack alive across many such closures and verify
    // each orphan is actually retired after its queued Qt cleanup.
    // A retained view holds a GL surface and canvas resources on Android.
    {
        MobileNativeViewStack churn;
        int destroyedViews=0;
        constexpr int cycles=128;
        for(int iteration=0;iteration<cycles;++iteration) {
            auto *document=new QObject;
            auto *canvas=new QWidget;
            QPointer<QWidget> guardedCanvas(canvas);
            QObject::connect(canvas,&QObject::destroyed,&churn,
                             [&destroyedViews]() {++destroyedViews;});
            require(churn.adoptView(canvas,document,
                        QStringLiteral("churn-%1").arg(iteration),
                        [](){return true;}),
                    "could not register churn canvas");
            require(churn.openViews().size()==1 &&
                    churn.currentDocument()==document,
                    "churn canvas not registered as selected");
            delete document;
            require(churn.openViews().isEmpty() &&
                    !churn.currentDocument(),
                    "document teardown retained a stale canvas registration");
            // retireDetachedView schedules deletion only after Qt's
            // removeWidget/currentChanged handlers have unwound.
            QCoreApplication::sendPostedEvents(nullptr,0);
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            QApplication::processEvents();
            require(!guardedCanvas && destroyedViews==iteration+1,
                    "closed document retained an orphaned canvas widget");
        }
        require(churn.findChildren<QWidget*>().isEmpty() &&
                destroyedViews==cycles,
                "canvas widgets accumulated across native document churn");
    }

    // Some save/close adapters accept a close without immediately
    // destroying the QWidget. The stack must retire only that detached
    // canvas, and a canceled close must preserve the same live view.
    {
        MobileNativeViewStack stack;
        int deleted=0;
        constexpr int cycles=128;
        for(int iteration=0;iteration<cycles;++iteration) {
            auto *document=new QObject;
            auto *view=new QWidget;
            QPointer<QWidget> watched(view);
            const QPointer<QObject> identity(view);
            bool approve=false;
            QObject::connect(view,&QObject::destroyed,&stack,
                             [&deleted]() {++deleted;});
            require(stack.adoptView(view,document,
                        QStringLiteral("close-%1").arg(iteration),
                        [&approve]() {return approve;}),
                    "could not register deferred-close canvas");
            require(!stack.closeView(identity) && watched &&
                    stack.openViews().size()==1 &&
                    stack.currentDocument()==document,
                    "canceled close lost a live painting widget");
            approve=true;
            require(stack.closeView(identity) &&
                    stack.openViews().isEmpty() &&
                    !stack.currentDocument() &&
                    !stack.activateView(identity),
                    "accepted close retained a registered native view");
            QCoreApplication::sendPostedEvents(nullptr,0);
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            QApplication::processEvents();
            require(!watched && deleted==iteration+1,
                    "accepted close retained a detached painting widget");
            delete document;
        }
        require(stack.findChildren<QWidget*>().isEmpty() &&
                deleted==cycles,
                "native close cycles accumulated orphaned painting widgets");
    }

    // A native view factory can fail after adoptView() already returned
    // success, for example when a resume observer closes the manager.
    // Roll back only that view's registration, preserving other canvases
    // and their document identities. This is the Qt-side contract used
    // by the Krita-specific late-attachment failure handler.
    {
        MobileNativeViewStack stack;
        auto *firstDocument=new QObject;
        auto *secondDocument=new QObject;
        auto *failedView=new QWidget;
        auto *survivingView=new QWidget;
        QPointer<QWidget> failed(failedView);
        QPointer<QWidget> surviving(survivingView);
        require(stack.adoptView(failedView,firstDocument,
                    QStringLiteral("failed"),[](){return true;}) &&
                stack.adoptView(survivingView,secondDocument,
                    QStringLiteral("surviving"),[](){return true;}),
                "could not register two canvases for late attach rollback");
        // Model the exact view identity inspected by abortRegistered.
        bool registered=false;
        for(const auto &entry:stack.openViews()) {
            if(entry.nativeView.data()==failed.data() &&
               entry.document.data()==firstDocument)
                registered=true;
        }
        require(registered && stack.forgetView(QPointer<QObject>(failed.data())),
                "failed canvas was not removable by its original identity");
        if(failed && failed->parentWidget()==&stack && stack.indexOf(failed.data())<0)
            delete failed.data();
        require(!failed && surviving && stack.openViews().size()==1 &&
                stack.currentDocument().data()==secondDocument &&
                stack.openViews().first().nativeView.data()==surviving.data(),
                "late attachment rollback removed a different live canvas");
        delete firstDocument;
        delete secondDocument;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }

    // A detached canvas may be transferred into another native host
    // before the old host's queued retirement runs. It must not be
    // destroyed by the old host after reparenting/re-registration.
    {
        MobileNativeViewStack original;
        MobileNativeViewStack receiving;
        auto *oldDocument=new QObject;
        auto *newDocument=new QObject;
        auto *view=new QWidget;
        QPointer<QWidget> surviving(view);
        require(original.adoptView(view,oldDocument,
                    QStringLiteral("original"),[](){return true;}),
                "could not register view before transfer");
        delete oldDocument; // Queues retirement outside removeWidget.
        require(original.openViews().isEmpty() && surviving,
                "document destruction did not detach transferable view");
        surviving->setParent(nullptr);
        require(receiving.adoptView(surviving.data(),newDocument,
                    QStringLiteral("transferred"),[](){return false;}),
                "receiving host could not adopt detached native canvas");
        QCoreApplication::sendPostedEvents(nullptr,0);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QApplication::processEvents();
        require(surviving && receiving.openViews().size()==1 &&
                receiving.currentDocument()==newDocument,
                "old host deleted a canvas after ownership transferred");
        delete newDocument;
        QCoreApplication::sendPostedEvents(nullptr,0);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QApplication::processEvents();
        require(!surviving && receiving.openViews().isEmpty(),
                "receiving host retained orphan after final document close");
    }

    // Duplicate-file Open scans the path and draft metadata of other
    // canvases. If a document closes during that work, opening or
    // activating another file must fail instead of using a stale row.
    {
        MobileNativeViewStack stack;
        auto *firstDocument=new QObject;
        auto *secondDocument=new QObject;
        require(stack.adoptView(new QWidget,firstDocument,
                    QStringLiteral("existing"),[](){return true;}) &&
                stack.adoptView(new QWidget,secondDocument,
                    QStringLiteral("other"),[](){return true;}),
                "could not set up duplicate-open document scan");
        const auto beforeScan=stack.openViews();
        // A synchronous document path observer destroys the second
        // document after Open captured its original tab snapshot.
        delete secondDocument;
        require(stack.openViews().size()==1 &&
                !MobileViewIdentityGuard::sameRecoveryWorkspace(
                    beforeScan,stack.openViews()),
                "duplicate-open scan accepted a changed native workspace");
        delete firstDocument;
    }

    // Save As must reject a destination scan whose set of open drawings
    // changes during a nested callback, even when the selected canvas
    // and original native stack remain alive.
    {
        MobileNativeViewStack stack;
        auto *activeDocument=new QObject;
        auto *otherDocument=new QObject;
        auto *activeCanvas=new QWidget;
        auto *otherCanvas=new QWidget;
        require(stack.adoptView(activeCanvas,activeDocument,
                    QStringLiteral("active.kra"),[](){return true;}) &&
                stack.adoptView(otherCanvas,otherDocument,
                    QStringLiteral("other.kra"),[](){return true;}),
                "could not create two canvases for Save As scan");
        const auto beforeScan=stack.openViews();
        const QPointer<QObject> originalActive=stack.currentDocument();
        delete otherDocument;
        require(stack.currentDocument()==originalActive &&
                stack.openViews().size()==1 &&
                !MobileViewIdentityGuard::sameRecoveryWorkspace(
                    beforeScan,stack.openViews()),
                "Save As scan accepted a workspace changed by document teardown");
        delete activeDocument;
    }

    // Recovery metadata updates emit synchronous QObject change
    // notifications before an imported document is registered with
    // Krita. The first notification may destroy the document, so the
    // subsequent path setters must not use the original raw pointer.
    {
        QPointer<QObject> imported(new QObject);
        QObject::connect(imported.data(),&QObject::objectNameChanged,
                         &app,[&imported](const QString &) {
            if(imported)delete imported.data();
        },Qt::DirectConnection);
        int subsequentWrites=0;
        imported->setObjectName(QStringLiteral("recovered-project"));
        if(imported) {
            ++subsequentWrites;
            imported->setProperty("_mobileDraftPath",QStringLiteral("draft.kra"));
        }
        require(!imported && subsequentWrites==0,
                "reentrant recovery metadata destroyed document before next write");
    }

    // A background autosave request can be deferred while its document
    // is still saving. A timer owned by the runtime must retry only
    // while the owner and document survive, then release the pending
    // request when that save has finished.
    {
        auto *owner=new QObject;
        auto *document=new QObject(owner);
        const QPointer<QObject> ownerGuard(owner);
        const QPointer<QObject> documentGuard(document);
        bool saving=true;
        bool pending=true;
        int checkpoints=0;
        const auto retry=[ownerGuard,documentGuard,&saving,&pending,&checkpoints]() {
            if(!ownerGuard || !documentGuard || !pending || saving)return;
            pending=false;
            ++checkpoints;
        };
        QTimer::singleShot(0,owner,retry);
        QCoreApplication::processEvents();
        require(pending && checkpoints==0,
                "deferred checkpoint ran while document was still saving");
        saving=false;
        QTimer::singleShot(0,owner,retry);
        QCoreApplication::processEvents();
        require(!pending && checkpoints==1,
                "pending checkpoint was not resumed after save completion");
        pending=true;
        QTimer::singleShot(0,owner,retry);
        delete owner;
        QCoreApplication::processEvents();
        require(!ownerGuard && !documentGuard && checkpoints==1,
                "deferred checkpoint survived runtime destruction");
    }

    // A document may remain a live QObject after its native view is
    // detached. Pending background retries must retire this identity
    // before checking its stale "saving" flag, or the retry watchdog
    // continues waking indefinitely for a foreign document.
    {
        MobileNativeViewStack stack;
        auto *document=new QObject;
        auto *canvas=new QWidget;
        const QPointer<QObject> identity(document);
        require(stack.adoptView(canvas,document,QStringLiteral("handoff"),
                                [](){return true;}),
                "failed to create deferred-save handoff fixture");
        const auto registered=stack.openViews();
        require(registered.size()==1 && registered.first().document==identity,
                "handoff fixture lost its initial document identity");
        stack.forgetView(QPointer<QObject>(canvas));
        require(identity && stack.openViews().isEmpty(),
                "detached native view unexpectedly destroyed document");
        QList<QPointer<QObject>> pending{identity};
        const auto stillOwned=[&stack](const QPointer<QObject> &candidate) {
            for(const auto &row:stack.openViews())
                if(row.document==candidate)return true;
            return false;
        };
        bool staleSaving=true;
        int retries=0;
        const auto snapshot=pending;
        for(const auto &candidate:snapshot) {
            if(!candidate || !stillOwned(candidate)) {
                pending.removeAll(candidate);
                continue;
            }
            if(staleSaving)++retries;
        }
        require(pending.isEmpty() && retries==0 && staleSaving,
                "detached document stayed in pending save retry queue");
        delete document;
    }

    // Engine tool-factory initialization is a synchronous callback
    // boundary. If it closes the host, bootstrap must not register
    // another action using the deleted runtime or native root.
    {
        auto *host=new QObject;
        auto *runtime=new QObject(host);
        auto *root=new QObject(host);
        const QPointer<QObject> hostGuard(host);
        const QPointer<QObject> runtimeGuard(runtime);
        const QPointer<QObject> rootGuard(root);
        int laterRegistrations=0;
        const auto initializeFactory=[host]() {delete host;};
        initializeFactory();
        if(runtimeGuard && hostGuard && rootGuard)
            ++laterRegistrations;
        require(!hostGuard && !runtimeGuard && !rootGuard &&
                laterRegistrations==0,
                "startup continued after tool factory destroyed native host");
    }
    {
        QObject host;
        QObject runtime(&host);
        QObject root(&host);
        const QPointer<QObject> hostGuard(&host);
        const QPointer<QObject> runtimeGuard(&runtime);
        const QPointer<QObject> rootGuard(&root);
        int registrations=0;
        const auto initializeFactory=[]() {};
        initializeFactory();
        if(runtimeGuard && hostGuard && rootGuard)
            ++registrations;
        require(registrations==1,
                "live native bootstrap skipped normal tool registration");
    }

    // Save ownership checks traverse engine registry listeners. Model a
    // synchronous document-ownership observer replacing the canvas root:
    // a live document and runtime alone do not authorize accessing the
    // original root after the callback.
    {
        auto *runtime=new QObject;
        auto *root=new QObject(runtime);
        auto *document=new QObject(runtime);
        const QPointer<QObject> guardedRuntime(runtime);
        const QPointer<QObject> guardedRoot(root);
        const QPointer<QObject> guardedDocument(document);
        int rootReads=0;
        const auto ownsDocument=[&]() {
            delete root;
            return true;
        };
        const auto stillOwnedSelected=[&]() {
            if(!guardedRuntime || !guardedRoot || !guardedDocument)
                return false;
            const bool owned=ownsDocument();
            if(!owned || !guardedRuntime || !guardedRoot ||
               !guardedDocument)return false;
            ++rootReads;
            return true;
        };
        require(!stillOwnedSelected() && rootReads==0 &&
                guardedRuntime && guardedDocument && !guardedRoot,
                "save inspected destroyed root after reentrant ownership query");
        delete runtime;
    }
    {
        QObject runtime;
        QObject root(&runtime);
        QObject document(&runtime);
        const QPointer<QObject> guardedRuntime(&runtime);
        const QPointer<QObject> guardedRoot(&root);
        const QPointer<QObject> guardedDocument(&document);
        int rootReads=0;
        const auto stillOwnedSelected=[&]() {
            if(!guardedRuntime || !guardedRoot || !guardedDocument)
                return false;
            const bool owned=true;
            if(!owned || !guardedRuntime || !guardedRoot ||
               !guardedDocument)return false;
            ++rootReads;
            return true;
        };
        require(stillOwnedSelected() && rootReads==1,
                "save guard rejected live native canvas");
    }

    // Attaching a newly opened document must not hand its stale pointer
    // to the native view factory if a document-image or registry query
    // synchronously closes the document or its original canvas root.
    {
        auto *host=new QObject;
        auto *root=new QObject(host);
        auto *document=new QObject(host);
        const QPointer<QObject> hostGuard(host);
        const QPointer<QObject> rootGuard(root);
        const QPointer<QObject> documentGuard(document);
        int attachCalls=0;
        const auto queryImage=[document]() {delete document;return true;};
        const bool imageAvailable=queryImage();
        if(imageAvailable && hostGuard && rootGuard && documentGuard)
            ++attachCalls;
        require(!documentGuard && attachCalls==0,
                "native view attached after image query destroyed document");
        delete host;
    }
    {
        auto *host=new QObject;
        auto *root=new QObject(host);
        auto *document=new QObject(host);
        const QPointer<QObject> hostGuard(host);
        const QPointer<QObject> rootGuard(root);
        const QPointer<QObject> documentGuard(document);
        int attachCalls=0;
        const auto queryRegistration=[root]() {delete root;return true;};
        const bool registered=queryRegistration();
        if(registered && hostGuard && rootGuard && documentGuard)
            ++attachCalls;
        require(!rootGuard && documentGuard && attachCalls==0,
                "native view attached after registry query destroyed root");
        delete host;
    }
    {
        QObject host;
        QObject root(&host);
        QObject document(&host);
        const QPointer<QObject> hostGuard(&host);
        const QPointer<QObject> rootGuard(&root);
        const QPointer<QObject> documentGuard(&document);
        int attachCalls=0;
        if(hostGuard && rootGuard && documentGuard)
            ++attachCalls;
        require(attachCalls==1,
                "native view attachment rejected live document and root");
    }

    // Native project display-name resolution can enter Android/Qt
    // callbacks before the view is registered. A destroyed dependency
    // must prevent adoption, while an intact title lookup still works.
    {
        auto *host=new QWidget;
        auto *stack=new MobileNativeViewStack(host);
        auto *document=new QObject(host);
        auto *view=new QWidget;
        const QPointer<MobileNativeViewStack> guardedStack(stack);
        const QPointer<QObject> guardedDocument(document);
        const QPointer<QWidget> guardedView(view);
        int adoptions=0;
        const auto resolveTitle=[host]() {
            delete host;
            return QStringLiteral("Artwork.kra");
        };
        const QString title=resolveTitle();
        if(guardedStack && guardedDocument && guardedView)
            ++adoptions;
        require(title==QStringLiteral("Artwork.kra") &&
                !guardedStack && !guardedDocument && adoptions==0,
                "native attachment survived host destruction during title lookup");
        delete view;
    }
    {
        MobileNativeViewStack stack;
        QObject document;
        auto *view=new QWidget;
        const QPointer<QWidget> guardedView(view);
        const QPointer<QObject> guardedDocument(&document);
        const QPointer<MobileNativeViewStack> guardedStack(&stack);
        const QString title=QStringLiteral("Artwork.kra");
        const bool accepted=guardedStack && guardedDocument && guardedView &&
            guardedStack->adoptView(guardedView.data(),guardedDocument.data(),
                                    title,[](){return true;});
        require(accepted && stack.openViews().size()==1,
                "native title-lookup guard rejected intact attachment");
    }

    // The native close hook may query the active manager canvas before
    // switching to the view being closed. If the query synchronously
    // destroys the closing canvas, it must not call setCurrentView()
    // using the stale view identity.
    {
        QObject manager;
        QObject view;
        auto *canvas=new QObject(&view);
        const QPointer<QObject> closingView(&view);
        const QPointer<QObject> closingManager(&manager);
        const QPointer<QObject> closingCanvas(canvas);
        int switches=0;
        const auto managerCanvas=[canvas,&manager]() -> QObject* {
            delete canvas;
            return &manager;
        };
        const bool needsActivation=managerCanvas()!=closingCanvas.data();
        if(closingView && closingManager && closingCanvas && needsActivation)
            ++switches;
        require(!closingCanvas && switches==0,
                "close hook switched to a view with a destroyed canvas");
    }
    {
        QObject manager;
        QObject view;
        QObject canvas(&view);
        const QPointer<QObject> closingView(&view);
        const QPointer<QObject> closingManager(&manager);
        const QPointer<QObject> closingCanvas(&canvas);
        int switches=0;
        const bool needsActivation=true;
        if(closingView && closingManager && closingCanvas && needsActivation)
            ++switches;
        require(switches==1,
                "close hook failed to activate a surviving native canvas");
    }

    // A deferred activation must not inspect the selected view's
    // canvas after the manager canvas getter synchronously tears down
    // that view. Preserve normal switching for a surviving selection.
    {
        MobileNativeViewStack stack;
        auto *document=new QObject;
        auto *selected=new QWidget;
        require(stack.adoptView(selected,document,QStringLiteral("active"),
                                [](){return true;}),
                "could not register activation teardown fixture");
        const QPointer<QWidget> selectedGuard(selected);
        const QPointer<QObject> documentGuard(document);
        const QPointer<MobileNativeViewStack> rootGuard(&stack);
        int switches=0;
        // Model the selected canvas identity without Krita classes.
        auto *selectedCanvas=new QObject(selected);
        const QPointer<QObject> selectedCanvasGuard(selectedCanvas);
        const auto queryManagerCanvas=[selected]() -> QObject* {
            delete selected;
            return nullptr;
        };
        const QPointer<QObject> managerCanvas(queryManagerCanvas());
        if(rootGuard && selectedGuard && selectedCanvasGuard &&
           rootGuard->currentWidget()==selectedGuard.data() &&
           managerCanvas!=selectedCanvasGuard)
            ++switches;
        require(!selectedGuard && !selectedCanvasGuard &&
                switches==0 && documentGuard,
                "deferred activation switched after selected view teardown");
        delete document;
    }
    {
        MobileNativeViewStack stack;
        QObject document;
        auto *selected=new QWidget;
        require(stack.adoptView(selected,&document,QStringLiteral("active"),
                                [](){return true;}),
                "could not register intact activation fixture");
        auto *selectedCanvas=new QObject(selected);
        QObject managerCanvas;
        const QPointer<QWidget> selectedGuard(selected);
        const QPointer<QObject> selectedCanvasGuard(selectedCanvas);
        const QPointer<QObject> managerCanvasGuard(&managerCanvas);
        int switches=0;
        if(selectedGuard && selectedCanvasGuard &&
           stack.currentWidget()==selectedGuard.data() &&
           managerCanvasGuard!=selectedCanvasGuard)
            ++switches;
        require(switches==1,
                "deferred activation rejected surviving view selection");
    }

    // Home's deferred close handler must not use a child widget after
    // Save/Discard callbacks synchronously destroy the containing host.
    {
        auto *window=new QWidget;
        auto *home=new QWidget(window);
        const QPointer<QWidget> guardedWindow(window);
        const QPointer<QWidget> guardedHome(home);
        int shows=0;
        const auto closeDocument=[window]() {
            delete window;
            return true;
        };
        const bool closed=closeDocument();
        if(closed && guardedWindow && guardedHome)++shows;
        require(closed && !guardedWindow && !guardedHome && shows==0,
                "Home callback accessed widget destroyed during document close");
    }
    {
        QWidget window;
        QWidget home(&window);
        const QPointer<QWidget> guardedWindow(&window);
        const QPointer<QWidget> guardedHome(&home);
        int shows=0;
        const auto closeDocument=[]() {return true;};
        const bool closed=closeDocument();
        if(closed && guardedWindow && guardedHome)++shows;
        require(shows==1,
                "Home callback suppressed surviving widget after successful close");
    }
    // Android intent import can likewise destroy the host before the
    // first Home display or boot-complete notification.
    {
        auto *window=new QWidget;
        auto *home=new QWidget(window);
        const QPointer<QWidget> guardedWindow(window);
        const QPointer<QWidget> guardedHome(home);
        int readyCallbacks=0;
        const auto attachIntent=[window]() {
            delete window;
            return 0;
        };
        const int opened=attachIntent();
        if(guardedWindow && guardedHome && opened==0)
            ++readyCallbacks;
        require(!guardedWindow && !guardedHome && readyCallbacks==0,
                "Android intent completion used a destroyed Home widget");
    }

    // Saved-versions toolbar resolves the current document path before
    // scheduling the Home gallery. A provider callback may destroy the
    // document and Home synchronously; neither may be reused afterward.
    {
        auto *window=new QWidget;
        auto *home=new QWidget(window);
        auto *document=new QObject(window);
        const QPointer<QWidget> guardedWindow(window);
        const QPointer<QWidget> guardedHome(home);
        const QPointer<QObject> guardedDocument(document);
        int galleryOpens=0;
        const auto resolvePath=[window]() {
            delete window;
            return QStringLiteral("closed.kra");
        };
        const QString path=resolvePath();
        if(guardedWindow && guardedHome && guardedDocument)
            ++galleryOpens;
        require(path==QStringLiteral("closed.kra") &&
                !guardedWindow && !guardedHome && !guardedDocument &&
                galleryOpens==0,
                "saved-version gallery used document or Home after path lookup teardown");
    }
    {
        QWidget window;
        auto *home=new QWidget(&window);
        const QPointer<QWidget> guardedHome(home);
        int galleryOpens=0;
        QTimer::singleShot(0,home,[guardedHome,&galleryOpens]() {
            if(guardedHome)++galleryOpens;
        });
        delete home;
        QCoreApplication::processEvents();
        require(!guardedHome && galleryOpens==0,
                "queued saved-version gallery reopened after Home destruction");
    }
    {
        QWidget window;
        QWidget home(&window);
        const QPointer<QWidget> guardedHome(&home);
        int galleryOpens=0;
        QTimer::singleShot(0,&home,[guardedHome,&galleryOpens]() {
            if(guardedHome)++galleryOpens;
        });
        QCoreApplication::processEvents();
        require(galleryOpens==1,
                "live Home lost its scheduled saved-version gallery action");
    }

    // A startup intent callback can outlive rollback of an empty
    // native document root. No first-canvas readiness notification
    // may dereference the discarded view port.
    {
        MobileLightweightCanvasHost host;
        auto *root=host.createNativeDocumentRoot();
        require(root && host.nativeViewPort(),
                "could not create first-canvas rollback fixture");
        int readyNotifications=0;
        QTimer::singleShot(0,&host,[&host,&readyNotifications]() {
            const auto views=host.nativeViewPort();
            if(!views)return;
            if(!views->currentDocument())return;
            ++readyNotifications;
        });
        require(host.discardEmptyNativeDocumentRoot() &&
                !host.nativeViewPort(),
                "empty native root was not discarded for startup rollback");
        QCoreApplication::processEvents();
        require(readyNotifications==0,
                "first-canvas callback used a discarded native view port");
    }
    {
        MobileLightweightCanvasHost host;
        auto *root=host.createNativeDocumentRoot();
        auto *document=new QObject;
        auto *canvas=new QWidget;
        require(root && root->adoptView(canvas,document,
                  QStringLiteral("Ready"),[](){return true;}),
                "could not create surviving first-canvas fixture");
        const auto views=host.nativeViewPort();
        int readyNotifications=0;
        QTimer::singleShot(0,&host,[&host,&readyNotifications]() {
            const auto port=host.nativeViewPort();
            if(port && port->currentDocument())
                ++readyNotifications;
        });
        QCoreApplication::processEvents();
        require(views && readyNotifications==1,
                "first-canvas callback skipped surviving native view");
        delete document;
    }

    // Engine-session bootstrap may synchronously delete its Qt host
    // before returning failure. The pilot must not delete the stale
    // raw host a second time; a surviving failed host is still cleaned.
    {
        auto *host=new MobileLightweightCanvasHost;
        const QPointer<MobileLightweightCanvasHost> guardedHost(host);
        int destructions=0;
        QObject::connect(host,&QObject::destroyed,
                         [&destructions]() {++destructions;});
        const auto attachSession=[host]() -> bool {
            delete host;
            return false;
        };
        const bool attached=attachSession();
        if(!attached && guardedHost)delete guardedHost.data();
        require(!attached && !guardedHost && destructions==1,
                "failed engine bootstrap deleted destroyed host twice");
    }
    {
        auto *host=new MobileLightweightCanvasHost;
        const QPointer<MobileLightweightCanvasHost> guardedHost(host);
        int destructions=0;
        QObject::connect(host,&QObject::destroyed,
                         [&destructions]() {++destructions;});
        const auto attachSession=[]() {return false;};
        const bool attached=attachSession();
        if(!attached && guardedHost)delete guardedHost.data();
        require(!attached && !guardedHost && destructions==1,
                "failed engine bootstrap leaked a surviving host");
    }

    // Bootstrap's native Save adapter is parented to its runtime.
    // An attachment callback can destroy that parent and the adapter
    // before the failure branch runs: never double-delete the adapter.
    {
        auto *runtime=new QObject;
        auto *saver=new QObject(runtime);
        const QPointer<QObject> alive(runtime);
        const QPointer<QObject> guardedSaver(saver);
        int saverDestructions=0;
        QObject::connect(saver,&QObject::destroyed,
                         [&saverDestructions]() {++saverDestructions;});
        const auto attachServices=[runtime]() {
            delete runtime;
            return false;
        };
        const bool attached=attachServices();
        if(!attached && guardedSaver)delete guardedSaver.data();
        require(!attached && !alive && !guardedSaver &&
                saverDestructions==1,
                "bootstrap failure double-deleted a parent-owned Save adapter");
    }
    {
        QObject runtime;
        auto *saver=new QObject(&runtime);
        const QPointer<QObject> guardedSaver(saver);
        int saverDestructions=0;
        QObject::connect(saver,&QObject::destroyed,
                         [&saverDestructions]() {++saverDestructions;});
        const auto attachServices=[]() {return false;};
        const bool attached=attachServices();
        if(!attached && guardedSaver)delete guardedSaver.data();
        require(!attached && !guardedSaver && saverDestructions==1,
                "bootstrap failure retained an unattached Save adapter");
    }

    // The pilot must not construct mobile UI after a Qt notification
    // destroys the window (and its child engine session) while status
    // chrome is being suppressed.
    {
        auto *window=new MobileLightweightCanvasHost;
        auto *session=new QObject(window);
        const QPointer<MobileLightweightCanvasHost> guardedWindow(window);
        const QPointer<QObject> guardedSession(session);
        int workspaceCreations=0;
        const auto suppressStatus=[window]() {
            delete window;
            return true;
        };
        const bool hidden=suppressStatus();
        if(hidden && guardedWindow && guardedSession)
            ++workspaceCreations;
        require(hidden && !guardedWindow && !guardedSession &&
                workspaceCreations==0,
                "native pilot created workspace after Qt host teardown");
    }
    {
        MobileLightweightCanvasHost window;
        QObject session(&window);
        const QPointer<MobileLightweightCanvasHost> guardedWindow(&window);
        const QPointer<QObject> guardedSession(&session);
        int workspaceCreations=0;
        const auto suppressStatus=[]() {return true;};
        const bool hidden=suppressStatus();
        if(hidden && guardedWindow && guardedSession)
            ++workspaceCreations;
        require(workspaceCreations==1,
                "native pilot suppressed surviving workspace setup");
    }

    // Root installation may synchronously destroy the native window.
    // Engine-session startup must capture the QPointer before creating
    // the root, and skip runtime construction if either was deleted.
    {
        auto *host=new MobileLightweightCanvasHost;
        const QPointer<MobileLightweightCanvasHost> guardedHost(host);
        const auto installRoot=[host]() -> MobileNativeViewStack* {
            delete host;
            return nullptr;
        };
        const QPointer<MobileNativeViewStack> root(installRoot());
        int runtimeCreations=0;
        if(guardedHost && root)++runtimeCreations;
        require(!guardedHost && !root && runtimeCreations==0,
                "engine session started after root installation destroyed host");
    }
    {
        MobileLightweightCanvasHost host;
        const QPointer<MobileLightweightCanvasHost> guardedHost(&host);
        const QPointer<MobileNativeViewStack> root(
            host.createNativeDocumentRoot());
        int runtimeCreations=0;
        if(guardedHost && root)++runtimeCreations;
        require(guardedHost && root && runtimeCreations==1,
                "engine session rejected surviving root installation");
        require(host.discardEmptyNativeDocumentRoot(),
                "could not clean up intact engine startup root fixture");
    }

    qInfo() << "PASS: MDI-free view stack, Qt host close cancellation,"
            << "synchronous native destruction, twelve no-reparent"
            << "transitions and destroyed-host safety";
    return 0;
}
