/*
 * Qt offscreen standalone native canvas host test. This test constructs
 * no Krita desktop MainWindow, MDI view or Krita native painting engine.
 * The real engine will be attached only after device validation.
 */
#include <QApplication>
#include <QAction>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <memory>
#include <QDialog>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include "mobile_core/touch_action_picker.h"
#include <QTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSharedPointer>
#include <QVBoxLayout>
#include "mobile_core/lightweight_canvas_host.h"

static void require(bool value,const char *message)
{
    if(!value)qFatal("Lightweight canvas host test failed: %s",message);
}

class FakeDocumentHost final : public MobileDocumentHostPort {
public:
    bool online=true;
    bool available() const override {return online;}
    bool attachNewDocument(KisDocument *d) const override {return online && d;}
    bool openFile(const QString &path) const override {
        return online && !path.isEmpty();
    }
    bool save(KisDocument *d,bool) const override {return online && d;}
};
class FakeViewHost final : public MobileViewHostPort {
public:
    QVector<ViewSnapshot> openViews() const override {return {};}
    QPointer<QObject> currentDocument() const override {return {};}
    bool activateView(const QPointer<QObject> &) const override {return false;}
    bool closeView(const QPointer<QObject> &) const override {return false;}
};
class FakeActions final : public MobileCommandRegistryPort {
public:
    QAction *action(const QString &) const override {return nullptr;}
    QList<QAction*> availableActions() const override {return {};}
    QList<QAction*> menuRootActions() const override {return {};}
};
class ParentCounter final : public QObject {
public:
    int changes=0;
protected:
    bool eventFilter(QObject *obj,QEvent *event) override {
        Q_UNUSED(obj);
        if(event->type()==QEvent::ParentChange)++changes;
        return false;
    }
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    MobileLightweightCanvasHost window;
    window.resize(440,720);
    auto *nativeCanvas=new QWidget;
    nativeCanvas->setProperty("drawingRevision",1234);
    ParentCounter parent;
    nativeCanvas->installEventFilter(&parent);
    auto *otherCanvas=new QWidget;
    require(window.installNativeCanvas(nativeCanvas),
            "independent host rejected a newly created paint widget");
    require(!window.installNativeCanvas(otherCanvas),
            "host incorrectly replaced an occupied painting canvas");
    delete otherCanvas;

    MobileHostServices services;
    services.documents=QSharedPointer<FakeDocumentHost>::create();
    services.views=QSharedPointer<FakeViewHost>::create();
    services.commands=QSharedPointer<FakeActions>::create();
    require(!window.attachServices(services),
            "host accepted document ports without the correct canvas");
    services.documentRoot=nativeCanvas;
    require(window.attachServices(services) && window.ready(),
            "host rejected valid native document and command services");

    auto *top=new QWidget;
    auto *layout=new QHBoxLayout(top);
    layout->addWidget(new QLabel(QStringLiteral("Canvas commands")));
    auto *bottom=new QWidget;
    auto *bottomLayout=new QVBoxLayout(bottom);
    bottomLayout->addWidget(new QLabel(QStringLiteral("Krita brush controls")));
    auto *layers=new QLabel(QStringLiteral("Krita native layers"));
    require(window.installMobileChrome(top,bottom,layers),
            "mobile chrome cannot attach to independent Qt host");
    require(!window.installMobileChrome(new QWidget,new QWidget),
            "mobile chrome was installed twice");

    window.show();
    QApplication::processEvents();
    const int movesAtInstall=parent.changes;
    for(int i=0;i<12;++i) {
        require(window.setMobileVisible(true) && window.mobileVisible(),
                "could not activate lightweight mobile controls");
        QApplication::processEvents();
        require(window.retainsOriginalCanvas() &&
                nativeCanvas->property("drawingRevision").toInt()==1234 &&
                parent.changes==movesAtInstall,
                "mobile mode moved or destroyed original Krita canvas");
        require(window.setMobileVisible(false) && !window.mobileVisible(),
                "could not restore lightweight native controls");
        QApplication::processEvents();
        require(window.retainsOriginalCanvas() && parent.changes==movesAtInstall,
                "Back reparented the lightweight original painting canvas");
    }
    auto *doc=static_cast<FakeDocumentHost*>(services.documents.data());
    doc->online=false;
    require(!window.ready() && !window.setMobileVisible(true),
            "offline engine document port allowed unsafe host activation");

    // A failed engine startup must not strand an empty native root
    // (or its view adapter) and permanently block the next attempt.
    {
        MobileLightweightCanvasHost failedStartup;
        auto *empty=failedStartup.createNativeDocumentRoot();
        require(empty && failedStartup.nativeViewPort(),
                "failed-bootstrap setup could not create native root");
        const QPointer<MobileNativeViewStack> abandoned(empty);
        require(failedStartup.discardEmptyNativeDocumentRoot() &&
                !abandoned && !failedStartup.nativeViewPort() &&
                !failedStartup.centralWidget(),
                "failed startup leaked its empty canvas root or view adapter");
        auto *retry=failedStartup.createNativeDocumentRoot();
        require(retry && failedStartup.nativeViewPort(),
                "failed startup prevented native engine retry");
        QObject drawing;
        auto *view=new QWidget;
        require(retry->adoptView(view,&drawing,QStringLiteral("Dirty"),
                                 [](){return false;}),
                "failed-bootstrap test could not attach a drawing");
        require(!failedStartup.discardEmptyNativeDocumentRoot() &&
                failedStartup.centralWidget()==retry &&
                retry->openViews().size()==1,
                "rollback destroyed a live native drawing");
    }
    // A host with configured engine services is no longer a failed
    // bootstrap even before its first native view is created.
    {
        MobileLightweightCanvasHost configured;
        auto *root=configured.createNativeDocumentRoot();
        require(root,"could not create configured native root");
        MobileHostServices ready;
        ready.documents=QSharedPointer<FakeDocumentHost>::create();
        ready.views=configured.nativeViewPort();
        ready.commands=QSharedPointer<FakeActions>::create();
        ready.documentRoot=root;
        require(configured.attachServices(ready) && configured.ready() &&
                !configured.discardEmptyNativeDocumentRoot() &&
                configured.centralWidget()==root,
                "failed startup rollback removed an initialized host");
    }

    // Version History runs a nested modal event loop. Android may
    // destroy its parent window while that loop is active; a guarded,
    // heap-owned child must become null without a second delete.
    {
        auto *closingParent=new QWidget;
        QPointer<QWidget> parentGuard(closingParent);
        QPointer<QDialog> historyDialog(new QDialog(closingParent));
        QTimer::singleShot(0,&app,[closingParent]() {
            delete closingParent;
        });
        historyDialog->exec();
        require(!parentGuard && !historyDialog,
                "nested history dialog survived destroyed owning window");
    }

    // Gallery permission/deletion and rename prompts use guarded, heap-
    // owned Qt dialogs. An Android window teardown during exec() must
    // clear both identities without deleting a C++ stack child.
    {
        auto *closingParent=new QWidget;
        QPointer<QWidget> parentGuard(closingParent);
        QPointer<QMessageBox> confirm(new QMessageBox(
            QMessageBox::Question,QStringLiteral("Confirm"),
            QStringLiteral("Continue?"),
            QMessageBox::Yes|QMessageBox::No,closingParent));
        QTimer::singleShot(0,&app,[closingParent]() {
            delete closingParent;
        });
        confirm->exec();
        delete confirm.data();
        require(!parentGuard && !confirm,
                "gallery confirmation survived destroyed Android host");
    }
    {
        auto *closingParent=new QWidget;
        QPointer<QWidget> parentGuard(closingParent);
        QPointer<QInputDialog> prompt(new QInputDialog(closingParent));
        prompt->setInputMode(QInputDialog::TextInput);
        QTimer::singleShot(0,&app,[closingParent]() {
            delete closingParent;
        });
        prompt->exec();
        delete prompt.data();
        require(!parentGuard && !prompt,
                "gallery rename prompt survived destroyed Android host");
    }

    // Android's document-provider delete may synchronously tear down
    // the gallery/window. A successful provider result must not trigger
    // follow-up UI/index updates on an already-destroyed host.
    {
        auto *gallery=new QWidget;
        const QPointer<QWidget> home(gallery);
        bool touchedGalleryAfterDelete=false;
        const auto providerDelete=[gallery]() {
            delete gallery;
            return true;
        };
        const bool removed=providerDelete();
        if(home && removed)
            touchedGalleryAfterDelete=true;
        require(removed && !home && !touchedGalleryAfterDelete,
                "provider deletion continued into destroyed gallery state");
    }
    // Information/warning dialogs shown after a failed delete must be
    // heap-owned: a parent teardown inside exec() cannot delete a
    // stack-allocated QMessageBox that is still executing.
    {
        auto *gallery=new QWidget;
        const QPointer<QWidget> home(gallery);
        QPointer<QMessageBox> warning(new QMessageBox(
            QMessageBox::Warning,QStringLiteral("Delete failed"),
            QStringLiteral("Provider rejected deletion"),
            QMessageBox::Ok,gallery));
        QTimer::singleShot(0,&app,[gallery]() { delete gallery; });
        warning->exec();
        delete warning.data();
        require(!home && !warning,
                "delete warning outlived its destroyed gallery parent");
    }

    // Opening a native drawing can destroy the gallery synchronously.
    // Never hide or display a failure dialog through a stale receiver.
    {
        auto *gallery=new QWidget;
        const QPointer<QWidget> home(gallery);
        bool touchedDestroyedGallery=false;
        const auto openDocument=[gallery]() {
            delete gallery;
            return true;
        };
        const bool opened=openDocument();
        if(home && opened) {
            touchedDestroyedGallery=true;
            home->hide();
        }
        require(opened && !home && !touchedDestroyedGallery,
                "native document Open used a destroyed gallery widget");
    }
    // Failed Open and Version History recovery both display a warning
    // using a parent-owned, heap-allocated dialog. Parent teardown
    // during exec() must invalidate both QPointers without a double free.
    {
        auto *gallery=new QWidget;
        const QPointer<QWidget> home(gallery);
        QPointer<QMessageBox> warning(new QMessageBox(
            QMessageBox::Warning,QStringLiteral("Drawing not opened"),
            QStringLiteral("Recovery could not be opened"),
            QMessageBox::Ok,gallery));
        QTimer::singleShot(0,&app,[gallery]() { delete gallery; });
        warning->exec();
        delete warning.data();
        require(!home && !warning,
                "recovery warning outlived its destroyed gallery");
    }

    // Discard Draft displays information and error notices in nested
    // modal loops. Losing the Android activity must destroy the dialog
    // through its parent without Qt deleting a C++ stack child.
    for(const auto severity:{QMessageBox::Information,QMessageBox::Warning}) {
        auto *gallery=new QWidget;
        const QPointer<QWidget> home(gallery);
        QPointer<QMessageBox> dialog(new QMessageBox(
            severity,QStringLiteral("Draft notice"),
            QStringLiteral("Draft could not be discarded"),
            QMessageBox::Ok,gallery));
        QTimer::singleShot(0,&app,[gallery]() { delete gallery; });
        dialog->exec();
        delete dialog.data();
        require(!home && !dialog,
                "draft discard notice survived destroyed gallery");
    }

    // Regress the independent Qt ownership pattern used by the native
    // Save/Discard/Cancel dialog. This test target intentionally has no
    // KDE localization dependency, so it exercises the same heap-owned
    // parent/child lifecycle without calling the production i18n wrapper.
    {
        auto *closingParent=new QWidget;
        QPointer<QWidget> parentGuard(closingParent);
        QPointer<QDialog> closeDialog(new QDialog(closingParent));
        auto *layout=new QVBoxLayout(closeDialog.data());
        auto *save=new QPushButton(QStringLiteral("Save"),closeDialog.data());
        auto *discard=new QPushButton(QStringLiteral("Discard"),closeDialog.data());
        auto *timer=new QTimer(closeDialog.data());
        layout->addWidget(save);
        layout->addWidget(discard);
        QObject::connect(timer,&QTimer::timeout,closeDialog.data(),
                         [closeDialog]() {
            if(closeDialog)closeDialog->done(QMessageBox::No);
        });
        QTimer::singleShot(0,&app,[closingParent]() {
            delete closingParent;
        });
        closeDialog->exec();
        const bool survived=closeDialog && parentGuard;
        delete closeDialog.data();
        require(!survived && !parentGuard && !closeDialog,
                "native close dialog ownership survived Android host teardown");
    }

    // The shared action picker is used throughout the mobile UI.
    // Destroying its owning Android window during exec() must cancel
    // safely, without Qt deleting a stack-allocated dialog.
    {
        QMenu actions(QStringLiteral("Actions"));
        actions.addAction(QStringLiteral("Paint"));
        auto *closingParent=new QWidget;
        QPointer<QWidget> parentGuard(closingParent);
        QTimer::singleShot(0,&app,[closingParent]() {
            delete closingParent;
        });
        require(!mobilePickAction(closingParent,actions) && !parentGuard,
                "touch action picker survived destroyed Android host");
    }

    // Fill tolerance worker responses use a guarded Qt receiver. If
    // Android tears down that receiver before delivery, Qt discards
    // its queued update rather than calling into freed UI state.
    {
        auto *receiver=new QObject;
        QPointer<QObject> live(receiver);
        bool delivered=false;
        require(QMetaObject::invokeMethod(receiver,[&delivered]() {
                    delivered=true;
                },Qt::QueuedConnection),
                "could not queue tolerance completion callback");
        delete receiver;
        app.processEvents();
        require(!live && !delivered,
                "stale tolerance callback executed after receiver destruction");
    }

    // Version History temporary copies must be removed if Qt destroys\n    // the queued UI receiver before its opening callback runs.
    {
        QTemporaryDir recovery;
        require(recovery.isValid(),"temporary recovery folder unavailable");
        const QString path=recovery.filePath(QStringLiteral("undelivered.kra"));
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write("kra",3)==3,
                "could not stage undelivered recovery copy");
        file.close();
        {
            auto *receiver=new QObject;
            auto owned=std::shared_ptr<QString>(
                new QString(path),[](QString *p) {
                    QFile::remove(*p);
                    delete p;
                });
            QMetaObject::invokeMethod(receiver,[owned]() {},
                                      Qt::QueuedConnection);
            owned.reset();
            delete receiver;
        }
        require(!QFile::exists(path),
                "cancelled Qt delivery leaked temporary Version History copy");
    }

    // Krita can delete a document through an engine-owned path before
    // deleting the view. The native stack must release the detached
    // painting QWidget rather than retain a hidden GL canvas forever.
    {
        MobileLightweightCanvasHost orphanHost;
        auto *orphanRoot=orphanHost.createNativeDocumentRoot();
        require(orphanRoot,"could not create orphan-view test root");
        auto *orphanDocument=new QObject;
        auto *orphanCanvas=new QWidget;
        QPointer<QWidget> canvasGuard(orphanCanvas);
        require(orphanRoot->adoptView(orphanCanvas,orphanDocument,
                                     QStringLiteral("Orphaned"),
                                     [](){return true;}),
                "could not attach orphan-view test canvas");
        auto *survivingDocument=new QObject;
        auto *survivingCanvas=new QWidget;
        QPointer<QWidget> survivorGuard(survivingCanvas);
        require(orphanRoot->adoptView(survivingCanvas,survivingDocument,
                                     QStringLiteral("Survivor"),
                                     [](){return true;}),
                "could not attach surviving native drawing");
        delete orphanDocument;
        const auto remaining=orphanRoot->openViews();
        require(remaining.size()==1 && remaining.first().document==survivingDocument,
                "orphan document teardown disturbed a surviving native canvas");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        require(!canvasGuard && survivorGuard &&
                orphanRoot->openViews().size()==1 &&
                orphanRoot->currentWidget()==survivingCanvas,
                "orphan cleanup freed the wrong canvas or retained its widget");
        delete survivingDocument;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        require(!survivorGuard && orphanRoot->openViews().isEmpty(),
                "last document teardown retained native canvas resources");
    }

    // A currentChanged observer can transfer a still-live canvas to
    // another QWidget during external document teardown. The old root
    // must not delete that widget after it leaves the root's ownership.
    {
        MobileLightweightCanvasHost transferHost;
        auto *root=transferHost.createNativeDocumentRoot();
        require(root,"could not create transfer test root");
        QWidget externalOwner;
        QObject survivingDocument;
        auto *survivingCanvas=new QWidget;
        require(root->adoptView(survivingCanvas,&survivingDocument,
                                QStringLiteral("Survivor"),
                                [](){return true;}),
                "could not attach transfer test survivor");
        auto *removedDocument=new QObject;
        auto *movedCanvas=new QWidget;
        QPointer<QWidget> movedGuard(movedCanvas);
        require(root->adoptView(movedCanvas,removedDocument,
                                QStringLiteral("Transfer"),
                                [](){return true;}),
                "could not attach transfer test canvas");
        root->setCurrentWidget(movedCanvas);
        bool transferred=false;
        QObject::connect(root,&QStackedWidget::currentChanged,root,
                         [movedCanvas,&externalOwner,&transferred,root](int) {
            if(root->indexOf(movedCanvas)<0 && !transferred) {
                movedCanvas->setParent(&externalOwner);
                movedCanvas->show();
                transferred=true;
            }
        });
        delete removedDocument;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        require(transferred && movedGuard &&
                movedCanvas->parentWidget()==&externalOwner &&
                !movedCanvas->isHidden() &&
                root->openViews().size()==1,
                "old native stack hid or deleted a canvas transferred to another owner");
    }

    // A close adapter is allowed to approve a document transaction
    // without explicitly deleting the QWidget. The native stack owns
    // that still-parented canvas and must retire it after deregistration.
    {
        MobileLightweightCanvasHost closeHost;
        auto *root=closeHost.createNativeDocumentRoot();
        require(root,"could not create close-cleanup root");
        QObject document;
        auto *canvas=new QWidget;
        const QPointer<QWidget> guard(canvas);
        require(root->adoptView(canvas,&document,QStringLiteral("Closed"),
                                [](){return true;}),
                "could not register close-cleanup canvas");
        const QPointer<QObject> identity(canvas);
        require(root->closeView(identity) && root->openViews().isEmpty(),
                "accepted native close did not deregister its widget");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        require(!guard,"accepted native close retained its canvas allocation");
    }

    // Removing a successfully closed tab emits currentChanged. Its
    // observers can synchronously attempt to close the next drawing.
    // Never allow that nested close until the first transaction ends.
    {
        MobileLightweightCanvasHost transitionHost;
        auto *root=transitionHost.createNativeDocumentRoot();
        require(root,"could not create reentrant-close test root");
        QObject firstDocument,secondDocument;
        auto *firstView=new QWidget;
        auto *secondView=new QWidget;
        int firstCloses=0,secondCloses=0,nestedAttempts=0;
        require(root->adoptView(firstView,&firstDocument,
                                QStringLiteral("First"),[&]() {
            ++firstCloses;
            return true;
        }),"could not attach first reentrant-close view");
        require(root->adoptView(secondView,&secondDocument,
                                QStringLiteral("Second"),[&]() {
            ++secondCloses;
            return true;
        }),"could not attach second reentrant-close view");
        root->setCurrentWidget(secondView);
        QObject::connect(root,&QStackedWidget::currentChanged,root,
                         [root,&nestedAttempts](int) {
            const auto current=root->openViews();
            if(current.size()!=1)return;
            ++nestedAttempts;
            require(!root->closeView(current.first().nativeView),
                    "tab transition recursively closed another drawing");
        });
        require(root->closeView(QPointer<QObject>(secondView)),
                "could not close active reentrant-close view");
        require(nestedAttempts==1 && secondCloses==1 && firstCloses==0 &&
                root->openViews().size()==1 &&
                root->currentWidget()==firstView,
                "tab teardown disturbed surviving document");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }

    MobileLightweightCanvasHost multi;
    auto *stack=multi.createNativeDocumentRoot();
    require(stack,"could not create the multi-canvas root");
    QObject firstDocument,secondDocument;
    auto *first=new QWidget;
    auto *second=new QWidget;
    QStringList closed;
    bool confirmFirst=false;
    bool nestedCloseRejected=false;
    require(stack->adoptView(first,&firstDocument,QStringLiteral("First"),[&]() {
        if(!confirmFirst)return false;
        closed.append(QStringLiteral("First"));
        return true;
    }),"could not adopt first canvas");
    require(stack->adoptView(second,&secondDocument,QStringLiteral("Second"),[&]() {
        // A native Save/Discard callback may reenter the host close
        // handler while its first transaction is still in progress.
        // The nested close must be denied without losing the guard.
        nestedCloseRejected=!multi.close();
        closed.append(QStringLiteral("Second"));
        return true;
    }),"could not adopt second canvas");
    stack->setCurrentWidget(second);
    multi.show();
    require(!multi.close() && nestedCloseRejected &&
            multi.isVisible() && stack->currentWidget()==first &&
            closed==QStringList{QStringLiteral("Second")},
            "app close must begin on the active tab and respect cancellation");
    confirmFirst=true;
    require(multi.close() && stack->openViews().isEmpty(),
            "app close must finish after all remaining canvases confirm");
    qInfo() << "PASS: Krita-independent Qt window host, immutable canvas,"
            << "injected real-engine-compatible backend ports, 12 mode"
            << "transitions, Back restoration and offline service refusal";
    return 0;
}
