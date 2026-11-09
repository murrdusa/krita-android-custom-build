/*
 * Qt-hosted canvas lifetime smoke test. Does not initialize Krita, change
 * documents, create an Android APK, or exercise OpenGL drivers.
 *
 * Compile on Linux with Qt5 Widgets and -DKRITA_MOBILE_QT_TEST. This is a
 * structural test of the exact same MobileCanvasTransfer used on Android.
 */
#include <QApplication>
#include <QDebug>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "mobile_core/canvas_transfer.h"

static void require(bool condition,const char *description)
{
    if(!condition)qFatal("Canvas transfer test failed: %s",description);
}

class TestSurface : public QWidget, public MobileCanvasSurface
{
public:
    TestSurface()
    {
        setObjectName(QStringLiteral("TestMobilePresentation"));
        m_layout=new QVBoxLayout(this);
        m_layout->setContentsMargins(0,0,0,0);
    }

    QWidget *canvasSurfaceWindow() override {return this;}
    bool attachNativeCanvas(QWidget *widget) override
    {
        if(!widget || m_canvas)return false;
        m_layout->addWidget(widget);
        m_canvas=widget;
        widget->show();
        return true;
    }
    QWidget *detachNativeCanvas(QWidget *destinationParent=nullptr) override
    {
        QWidget *result=m_canvas.data();
        if(!result)return nullptr;
        m_canvas=nullptr;
        m_layout->removeWidget(result);
        result->hide();
        if(result->parentWidget()!=destinationParent)
            result->setParent(destinationParent);
        return result;
    }
    bool ownsNativeCanvas(QWidget *widget) const override
    {
        return widget && widget==m_canvas;
    }
    bool hasNativeCanvas() const override {return !m_canvas.isNull();}
    void refreshCanvasSurface() override {}
    void setMobilePresentationEnabled(bool enabled) override {
        m_mobilePresentationEnabled=enabled;
    }
    bool mobilePresentationEnabled() const {return m_mobilePresentationEnabled;}

private:
    bool m_mobilePresentationEnabled=true;
    QPointer<QWidget> m_canvas;
    QVBoxLayout *m_layout=nullptr;
};

// A QMainWindow::takeCentralWidget() temporarily sets a native canvas's
// parent to nullptr on Qt 5. This observer distinguishes the still-open
// entry-side risk from the Back path, which should now remain inside the
// original top-level. It deliberately reports the count without treating
// the entry-side risk as solved before a different architecture exists.
class CanvasTopLevelObserver final : public QObject
{
public:
    CanvasTopLevelObserver(QWidget *canvas,QMainWindow *backend)
        : m_canvas(canvas),m_backend(backend)
    {
        canvas->installEventFilter(this);
    }
    ~CanvasTopLevelObserver() override
    {
        if(m_canvas)m_canvas->removeEventFilter(this);
    }
    int escapes=0;
protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_canvas && event->type()==QEvent::ParentChange &&
           m_canvas && m_backend && m_canvas->window()!=m_backend)
            ++escapes;
        return QObject::eventFilter(watched,event);
    }
private:
    QPointer<QWidget> m_canvas;
    QPointer<QMainWindow> m_backend;
};

// Fault injection for the Android path where a surface has detached the
// real painting view but returns an invalid result. The transfer layer
// should reverse that partial mutation without deleting or re-creating
// Krita's native canvas.
class FaultyDetachSurface final : public TestSurface
{
public:
    bool failNextDetach=true;
    QWidget *detachNativeCanvas(QWidget *destinationParent=nullptr) override
    {
        QWidget *original=TestSurface::detachNativeCanvas(destinationParent);
        if(failNextDetach) {
            failNextDetach=false;
            Q_UNUSED(original);
            return nullptr;
        }
        return original;
    }
};

// A destination that cannot accept a native canvas must leave Krita
// exactly as it was. This is an error path that can occur on Android when
// a canvas slot has already been torn down.
class RejectingSurface final : public QWidget, public MobileCanvasSurface
{
public:
    QWidget *canvasSurfaceWindow() override {return this;}
    bool attachNativeCanvas(QWidget *) override {return false;}
    QWidget *detachNativeCanvas(QWidget *destinationParent=nullptr) override {return nullptr;}
    bool ownsNativeCanvas(QWidget *) const override {return false;}
    bool hasNativeCanvas() const override {return false;}
    void refreshCanvasSurface() override {}
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setOrganizationName(QStringLiteral("KritaMobileCanvasTest"));
    app.setApplicationName(QStringLiteral("PersistentCanvas"));
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(),"failed to allocate isolated test settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       settingsDirectory.path());
    QSettings().setValue("MobileStudio/persistentCanvasHostV1",false);
    QMainWindow backend;
    backend.setObjectName(QStringLiteral("TestKritaBackend"));
    backend.resize(820,600);
    QWidget *canvas=new QWidget;
    canvas->setObjectName(QStringLiteral("OriginalNativeCanvas"));
    canvas->setProperty("testDrawingRevision",17);
    auto *layout=new QVBoxLayout(canvas);
    QMdiArea *mdi=new QMdiArea(canvas);
    mdi->setObjectName(QStringLiteral("OriginalNativeMDI"));
    layout->addWidget(mdi);
    backend.setCentralWidget(canvas);

    auto *toolbar=new QToolBar(QStringLiteral("OriginalToolbar"),&backend);
    backend.addToolBar(toolbar);
    auto *dock=new QDockWidget(QStringLiteral("OriginalDock"),&backend);
    dock->setWidget(new QLabel(QStringLiteral("Original native panel")));
    backend.addDockWidget(Qt::RightDockWidgetArea,dock);
    backend.menuBar()->addMenu(QStringLiteral("File"));
    backend.statusBar()->showMessage(QStringLiteral("OriginalStatus"));
    backend.show();
    QApplication::processEvents();

    const bool menuVisible=backend.menuBar()->isVisible();
    const bool statusVisible=backend.statusBar()->isVisible();
    const bool toolbarVisible=toolbar->isVisible();
    const bool dockVisible=dock->isVisible();

    MobileCanvasTransfer transfer(&backend,mdi);
    CanvasTopLevelObserver parentObserver(canvas,&backend);
    // An occupied destination must never steal a native Krita widget.
    TestSurface occupied;
    occupied.attachNativeCanvas(new QWidget);
    require(!transfer.enter(&occupied),"accepted occupied destination");
    require(backend.centralWidget()==canvas,"changed desktop after refused entry");
    require(!transfer.active(),"marked refused entry active");

    RejectingSurface refusing;
    require(!transfer.enter(&refusing),"accepted rejected canvas attachment");
    require(!transfer.active(),"marked failed attachment active");
    require(backend.centralWidget()==canvas,
            "native canvas was lost during failed attachment");
    require(canvas->property("testDrawingRevision").toInt()==17,
            "failed attachment altered document state");

    for(int cycle=0;cycle<10;++cycle) {
        TestSurface *mobile=(cycle==3)?
            static_cast<TestSurface*>(new FaultyDetachSurface):
            new TestSurface;
        require(transfer.enter(mobile),"failed to embed mobile presentation");
        QApplication::processEvents();
        require(transfer.active(),"transfer not active after entering");
        require(backend.property("_mobileStudioCanvasTransferred").toBool() &&
                backend.property("_mobileStudioPresentationActive").toBool(),
                "default transfer did not set canvas/presentation flags");
        require(backend.centralWidget()==mobile,
                "mobile presentation is not native central widget");
        require(mobile->ownsNativeCanvas(canvas),
                "native canvas no longer owned by embedded mobile surface");
        require(canvas->window()==&backend,
                "canvas moved to a separate top-level Android-style window");
        require(!toolbar->isVisible() && !dock->isVisible(),
                "desktop chrome visible over mobile presentation");
        require(!backend.menuBar()->isVisible() &&
                !backend.statusBar()->isVisible(),
                "desktop menu or status bar visible in embedded mobile mode");

        if(cycle==2) {
            // Wrong desktop central widget must be rejected without
            // deleting an unrelated widget or the original painting view.
            QWidget *removed=backend.takeCentralWidget();
            require(removed==mobile,"could not isolate embedded presentation");
            QWidget *other=new QWidget;
            backend.setCentralWidget(other);
            require(!transfer.leave(),"accepted occupied backend central slot");
            require(backend.centralWidget()==other,
                    "overwrote unrelated desktop central widget");
            require(mobile->ownsNativeCanvas(canvas),
                    "discarded original canvas on rejected return");
            require(backend.takeCentralWidget()==other,
                    "failed to remove deliberate test replacement");
            delete other;
            backend.setCentralWidget(mobile);
        }

        if(cycle==3) {
            // A failed detach must leave the mobile host usable, retain
            // the original canvas, and allow a subsequent safe retry.
            require(!transfer.leave(),"accepted invalid detached widget");
            require(transfer.active(),"invalid detach cleared active state");
            require(backend.centralWidget()==mobile,
                    "invalid detach replaced central mobile presentation");
            require(mobile->ownsNativeCanvas(canvas),
                    "invalid detach orphaned original Krita canvas");
            require(canvas->property("testDrawingRevision").toInt()==17,
                    "invalid detach discarded document state");
        }
        const int escapesBeforeBack=parentObserver.escapes;
        require(transfer.leave(),"failed to restore the native canvas");
        require(parentObserver.escapes==escapesBeforeBack,
                "Back temporarily unparented the original native canvas");
        QApplication::processEvents();
        require(!transfer.active(),"transfer still active after returning");
        require(!backend.property("_mobileStudioCanvasTransferred").toBool() &&
                !backend.property("_mobileStudioPresentationActive").toBool(),
                "default transfer did not reset canvas/presentation flags");
        require(backend.centralWidget()==canvas,
                "desktop did not recover the identical native canvas");
        require(canvas->property("testDrawingRevision").toInt()==17,
                "document state changed while toggling the UI");
        require(canvas->window()==&backend,
                "return created another top-level window");
        require(backend.menuBar()->isVisible()==menuVisible &&
                backend.statusBar()->isVisible()==statusVisible,
                "desktop menu/status visibility not restored");
        require(toolbar->isVisible()==toolbarVisible &&
                dock->isVisible()==dockVisible,
                "desktop toolbar/dock visibility not restored");
        require(!mobile->hasNativeCanvas(),
                "empty mobile presentation still owns original painting canvas");
        delete mobile;
    }

    // Explicit opt-in stable host: the first entry may still reparent
    // once, but every subsequent Back/enter must change only the controls.
    // This test must not force experimental mode into the Android default.
    QSettings().setValue("MobileStudio/persistentCanvasHostV1",true);
    MobileCanvasTransfer persistent(&backend,mdi);
    auto *stableSurface=new TestSurface;
    require(persistent.enter(stableSurface),
            "failed initial persistent-host canvas attachment");
    require(persistent.persistentHostInstalled(),
            "persistent opt-in was not honored");
    require(persistent.active(),"persistent host did not enter mobile mode");
    require(backend.property("_mobileStudioCanvasTransferred").toBool() &&
            backend.property("_mobileStudioPresentationActive").toBool(),
            "persistent entry did not set canvas/presentation flags");
    const int parentChangesAfterInstall=parentObserver.escapes;
    for(int i=0;i<10;++i) {
        require(persistent.leave(),"persistent host refused desktop mode");
        QApplication::processEvents();
        require(!persistent.active(),"persistent host still in mobile mode");
        require(backend.property("_mobileStudioCanvasTransferred").toBool() &&
                !backend.property("_mobileStudioPresentationActive").toBool(),
                "persistent Back lost ownership or left mobile mode active");
        require(backend.centralWidget()==stableSurface,
                "persistent mode replaced its original central host");
        require(stableSurface->ownsNativeCanvas(canvas),
                "persistent mode detached the original Krita canvas");
        require(!stableSurface->mobilePresentationEnabled(),
                "persistent mode left mobile controls visible on Back");
        require(backend.menuBar()->isVisible()==menuVisible &&
                backend.statusBar()->isVisible()==statusVisible,
                "persistent mode did not restore native desktop menu/status");
        require(parentObserver.escapes==parentChangesAfterInstall,
                "persistent Back detached the native canvas");
        require(persistent.enter(stableSurface),
                "persistent mode failed to reenter Mobile Studio");
        QApplication::processEvents();
        require(stableSurface->mobilePresentationEnabled(),
                "persistent mode did not restore mobile controls");
        require(backend.property("_mobileStudioCanvasTransferred").toBool() &&
                backend.property("_mobileStudioPresentationActive").toBool(),
                "persistent reentry did not restore presentation-active flag");
        require(!backend.menuBar()->isVisible() &&
                !backend.statusBar()->isVisible(),
                "persistent reentry exposed desktop chrome");
        require(parentObserver.escapes==parentChangesAfterInstall,
                "persistent reentry detached the native canvas");
        require(canvas->property("testDrawingRevision").toInt()==17,
                "persistent mode changed document identity");
    }
    require(persistent.leave(),"persistent final desktop return failed");
    require(stableSurface->ownsNativeCanvas(canvas),
            "persistent host lost its canvas on final Back");

    qInfo() << "Entry-side transient top-level parent changes:"
            << parentObserver.escapes
            << "(separately tracked; the Back path has zero)";
    qInfo() << "PASS: ten reversible same-window transfers, chrome restore,"
            << "canvas identity, occupied destination, rejected attachment,"
            << "failed detach rollback, occupied backend guards,"
            << "and ten persistent-host chrome-only mode switches";
    return 0;
}
