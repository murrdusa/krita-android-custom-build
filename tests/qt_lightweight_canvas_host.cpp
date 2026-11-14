/*
 * Qt offscreen standalone native canvas host test. This test constructs
 * no Krita desktop MainWindow, MDI view or Krita native painting engine.
 * The real engine will be attached only after device validation.
 */
#include <QApplication>
#include <QAction>
#include <QDebug>
#include <QDialog>
#include <QMenu>
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
