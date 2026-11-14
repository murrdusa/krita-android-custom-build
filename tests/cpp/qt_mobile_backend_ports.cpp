/*
 * Qt-only backend contract regression. Does not load Krita, an Android
 * activity, or a KisMainWindow. A lightweight host can implement the
 * same document and command ports used by Mobile Studio.
 */
#include <QApplication>
#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>
#include "mobile_studio/document_host_port.h"
#include "mobile_studio/command_registry_port.h"
#include "mobile_studio/host_services.h"

static void require(bool valid,const char *msg)
{
    if(!valid)qFatal("Mobile backend port test failed: %s",msg);
}

struct FakeDocumentHost final : MobileDocumentHostPort
{
    bool online=true;
    int attached=0,opened=0,saved=0;
    QStringList files;
    bool available() const override {return online;}
    bool attachNewDocument(KisDocument *document) const override
    {
        if(!online || !document)return false;
        ++const_cast<FakeDocumentHost*>(this)->attached;
        return true;
    }
    bool openFile(const QString &name) const override
    {
        if(!online || name.isEmpty())return false;
        auto *self=const_cast<FakeDocumentHost*>(this);
        ++self->opened;
        self->files.append(name);
        return true;
    }
    bool save(KisDocument *document,bool saveAs) const override
    {
        if(!online || !document)return false;
        auto *self=const_cast<FakeDocumentHost*>(this);
        self->saved+=saveAs?10:1;
        return true;
    }
};

struct FakeCommandRegistry final : MobileCommandRegistryPort
{
    QPointer<QAction> undo;
    QPointer<QAction> redo;
    QPointer<QAction> file;
    QAction *action(const QString &id) const override
    {
        if(id==QStringLiteral("edit_undo"))return undo;
        if(id==QStringLiteral("edit_redo"))return redo;
        return nullptr;
    }
    QList<QAction*> availableActions() const override
    {
        return {undo.data(),redo.data()};
    }
    QList<QAction*> menuRootActions() const override
    {
        return {file.data()};
    }
};

struct FakeViewHost final : MobileViewHostPort
{
    QVector<ViewSnapshot> openViews() const override {return {};}
    QPointer<QObject> currentDocument() const override {return {};}
    bool activateView(const QPointer<QObject> &) const override {return false;}
    bool closeView(const QPointer<QObject> &) const override {return false;}
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    // The fake engine pointer is an identity token only. The Qt-only
    // test never dereferences, constructs or deletes a KisDocument.
    char marker=0;
    auto *document=reinterpret_cast<KisDocument*>(&marker);
    auto writer=QSharedPointer<FakeDocumentHost>::create();
    QSharedPointer<MobileDocumentHostPort> host=writer;
    require(host->available(),"lightweight document host unavailable");
    require(host->attachNewDocument(document),"host cannot attach a view");
    require(host->openFile(QStringLiteral("drawing.kra")),"host cannot open");
    require(host->save(document,false) && host->save(document,true),
            "host cannot use native save / save as");
    require(writer->attached==1 && writer->opened==1 && writer->saved==11,
            "document operations did not reach the injected host");
    {
        // Modal document choosers copy the facade into deferred callbacks.
        // Shared host ownership must keep their backend alive.
        QSharedPointer<MobileDocumentHostPort> deferred=host;
        host.clear();
        require(deferred->available() &&
                deferred->openFile(QStringLiteral("second.kra")),
                "deferred copied backend lost its document service");
        require(writer->opened==2 &&
                writer->files.last()==QStringLiteral("second.kra"),
                "the deferred open request changed its target");
        host=deferred;
    }
    writer->online=false;
    require(!host->available() &&
            !host->attachNewDocument(document) &&
            !host->save(document,false),
            "a destroyed desktop-equivalent host accepted document writes");

    QAction undo(QStringLiteral("Undo"),&app);
    QAction redo(QStringLiteral("Redo"),&app);
    QAction file(QStringLiteral("File"),&app);
    int calls=0;
    QObject::connect(&undo,&QAction::triggered,&app,[&calls](){++calls;});
    auto registry=QSharedPointer<FakeCommandRegistry>::create();
    registry->undo=&undo;
    registry->redo=&redo;
    registry->file=&file;
    QSharedPointer<MobileCommandRegistryPort> actions=registry;
    require(actions->action(QStringLiteral("edit_undo"))==&undo &&
            actions->availableActions().size()==2 &&
            actions->menuRootActions().first()==&file,
            "native action/menu discovery did not reach injected port");
    actions->action(QStringLiteral("edit_undo"))->trigger();
    require(calls==1,"host action was not forwarded to the real QAction");
    require(!actions->action(QStringLiteral("invalid_action")),
            "missing native actions should not be invented");

    // Lightweight hosts can wire all three ports and their document
    // widget without constructing any Krita desktop QMainWindow.
    auto *root=new QWidget;
    MobileHostServices services;
    require(!services.ready(),"empty host incorrectly marked ready");
    writer->online=true;
    services.documents=host;
    services.views=QSharedPointer<FakeViewHost>::create();
    services.commands=actions;
    require(!services.ready(),"host without a canvas root marked ready");
    services.documentRoot=root;
    require(services.ready(),"independent Qt host port bundle not ready");
    const MobileHostServices copied=services;
    require(copied.ready() && copied.documents==services.documents &&
            copied.views==services.views &&
            copied.commands==services.commands,
            "copying a host bundle changed service ownership");
    delete root;
    require(!services.ready() && !copied.ready(),
            "destroyed canvas root remained eligible for mobile startup");
    writer->online=false;
    require(!services.ready(),
            "offline document provider incorrectly marked host ready");

    qInfo() << "PASS: host-independent document attach/open/save,"
            << "safe shared callback lifetime, unavailable-host refusal,"
            << "native QAction dispatch and menu registry";
    return 0;
}
