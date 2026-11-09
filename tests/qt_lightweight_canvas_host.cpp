/*
 * Qt offscreen standalone native canvas host test. This test constructs
 * no Krita desktop MainWindow, MDI view or Krita native painting engine.
 * The real engine will be attached only after device validation.
 */
#include <QApplication>
#include <QAction>
#include <QDebug>
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
    qInfo() << "PASS: Krita-independent Qt window host, immutable canvas,"
            << "injected real-engine-compatible backend ports, 12 mode"
            << "transitions, Back restoration and offline service refusal";
    return 0;
}
