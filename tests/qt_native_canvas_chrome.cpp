/*
 * Qt5 offscreen regression for no-reparent Mobile Studio dock chrome.
 * Tests the architecture separately from Krita's painting engine.
 */
#include <QApplication>
#include <QDebug>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QVBoxLayout>
#include "mobile_core/native_canvas_chrome.h"

static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Native canvas chrome test failed: %s",reason);
}

class ParentChangeCounter final : public QObject
{
public:
    int changes=0;
protected:
    bool eventFilter(QObject *object,QEvent *event) override
    {
        Q_UNUSED(object);
        if(event->type()==QEvent::ParentChange)++changes;
        return false;
    }
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMainWindow backend;
    backend.setObjectName(QStringLiteral("KritaNativeBackend"));
    backend.resize(900,650);

    auto *canvas=new QWidget;
    canvas->setObjectName(QStringLiteral("OriginalKritaCanvasWidget"));
    canvas->setProperty("syntheticDocumentRevision",12345);
    auto *nativeLayout=new QVBoxLayout(canvas);
    auto *nativeTabs=new QTabBar(canvas);
    nativeTabs->addTab(QStringLiteral("Drawing 1"));
    nativeTabs->addTab(QStringLiteral("Drawing 2"));
    nativeLayout->addWidget(nativeTabs);
    nativeLayout->addWidget(new QLabel(QStringLiteral("Canvas remains here"),canvas));
    backend.setCentralWidget(canvas);

    auto *originalToolbar=new QToolBar(QStringLiteral("Native Tools"),&backend);
    backend.addToolBar(originalToolbar);
    auto *originalDock=new QDockWidget(QStringLiteral("Native Layers"),&backend);
    originalDock->setWidget(new QLabel(QStringLiteral("Original layer list")));
    backend.addDockWidget(Qt::RightDockWidgetArea,originalDock);
    backend.menuBar()->addMenu(QStringLiteral("File"));
    backend.statusBar()->showMessage(QStringLiteral("Native document status"));
    backend.show();
    QApplication::processEvents();

    const bool initiallyMenu=backend.menuBar()->isVisible();
    const bool initiallyStatus=backend.statusBar()->isVisible();
    const bool initiallyToolbar=originalToolbar->isVisible();
    const bool initiallyDock=originalDock->isVisible();
    const bool initiallyTabs=nativeTabs->isVisible();
    const int initiallyTabMin=nativeTabs->minimumHeight();
    const int initiallyTabMax=nativeTabs->maximumHeight();
    QWidget *const originalCanvas=backend.centralWidget();

    ParentChangeCounter observer;
    canvas->installEventFilter(&observer);

    MobileNativeCanvasChrome mobile(&backend);
    auto *top=new QWidget;
    auto *topLayout=new QHBoxLayout(top);
    topLayout->addWidget(new QLabel(QStringLiteral("Mobile Back / Canvases")));
    auto *bottom=new QWidget;
    auto *bottomLayout=new QVBoxLayout(bottom);
    bottomLayout->addWidget(new QLabel(QStringLiteral("Mobile brushes and quick actions")));
    auto *side=new QLabel(QStringLiteral("Mobile native layers"));
    require(mobile.install(top,bottom,side),"could not install mobile chrome");
    require(mobile.ownsUnmovedCanvas(),"installation changed native central widget");
    const int parentChangesAtInstall=observer.changes;
    QPointer<QToolBar> lateToolbar;
    QPointer<QDockWidget> lateDocker;
    QPointer<QTabBar> lateTab;
    int lateTabOriginalMin=0;
    int lateTabOriginalMax=0;

    for(int cycle=0;cycle<25;++cycle) {
        require(mobile.activate(),"failed to enter native-host mode");
        QApplication::processEvents();
        require(mobile.active(),"native-host mode did not become active");
        require(backend.centralWidget()==originalCanvas,
                "enter replaced the central canvas");
        require(canvas->window()==&backend && observer.changes==parentChangesAtInstall,
                "enter changed the canvas native parent/window");
        require(canvas->property("syntheticDocumentRevision").toInt()==12345,
                "enter changed the synthetic drawing");
        require(!backend.menuBar()->isVisible() &&
                !backend.statusBar()->isVisible() &&
                !originalToolbar->isVisible() &&
                !originalDock->isVisible(),"desktop controls leaked into mobile mode");
        require(!nativeTabs->isVisible() && nativeTabs->maximumHeight()==0,
                "desktop MDI tabs leaked into mobile mode");
        require(top->isVisible() && bottom->isVisible(),
                "mobile docks hidden after activation");
        if(cycle==2) {
            // Zero-document welcome screen: hide the empty brush strip
            // without moving or replacing Krita's native central widget.
            require(mobile.setBottomPanelVisible(false),
                    "could not collapse empty mobile brush dock");
            QApplication::processEvents();
            require(!mobile.bottomPanelVisible() && !bottom->isVisible(),
                    "empty mobile brush dock still occupies canvas space");
            require(canvas->window()==&backend &&
                    backend.centralWidget()==originalCanvas &&
                    observer.changes==parentChangesAtInstall,
                    "collapsing empty brush dock reparented native canvas");
            require(mobile.setBottomPanelVisible(true),
                    "could not restore brush dock on document creation");
            QApplication::processEvents();
            require(mobile.bottomPanelVisible() && bottom->isVisible(),
                    "creating a document failed to restore mobile brushes");
        }
        require(!mobile.sidePanelVisible(),
                "mobile layers dock should start closed");
        require(mobile.setSidePanelVisible(true),
                "cannot open native-host mobile layers dock");
        QApplication::processEvents();
        require(mobile.sidePanelVisible() && side->isVisible(),
                "side dock failed to expose mobile layer controls");
        require(backend.centralWidget()==originalCanvas &&
                observer.changes==parentChangesAtInstall,
                "opening native-host layers moved original canvas");
        require(mobile.setSidePanelVisible(false) &&
                !mobile.sidePanelVisible(),
                "mobile layer dock did not close");

        if(cycle==4) {
            // Opening another Krita document while mobile chrome is up
            // may dynamically create tabs and native toolbar actions.
            auto *newToolbar=new QToolBar(QStringLiteral("Late native tools"));
            backend.addToolBar(newToolbar);
            newToolbar->show();
            lateToolbar=newToolbar;
            auto *newDocker=new QDockWidget(
                QStringLiteral("New native docker"),&backend);
            newDocker->setWidget(new QLabel(
                QStringLiteral("New native panel"),newDocker));
            backend.addDockWidget(Qt::RightDockWidgetArea,newDocker);
            newDocker->show();
            lateDocker=newDocker;
            auto *newTab=new QTabBar(canvas);
            newTab->addTab(QStringLiteral("Newly opened drawing"));
            nativeLayout->insertWidget(1,newTab);
            newTab->show();
            lateTab=newTab;
            QApplication::processEvents();
            lateTabOriginalMin=newTab->minimumHeight();
            lateTabOriginalMax=newTab->maximumHeight();
        }
        mobile.enforceMobileChrome();
        if(lateToolbar)
            require(!lateToolbar->isVisible(),
                    "new Krita toolbar remained visible in mobile mode");
        if(lateDocker)
            require(!lateDocker->isVisible(),
                    "new Krita docker remained visible in mobile mode");
        if(lateTab)
            require(!lateTab->isVisible() && lateTab->maximumHeight()==0,
                    "new Krita tabbar remained visible in mobile mode");
        require(mobile.deactivate(),"could not return to desktop");
        QApplication::processEvents();
        require(!mobile.active(),"mode remained active after Back");
        require(canvas->window()==&backend &&
                backend.centralWidget()==originalCanvas &&
                observer.changes==parentChangesAtInstall,
                "Back reparented or replaced the original canvas");
        require(backend.menuBar()->isVisible()==initiallyMenu &&
                backend.statusBar()->isVisible()==initiallyStatus &&
                originalToolbar->isVisible()==initiallyToolbar &&
                originalDock->isVisible()==initiallyDock,
                "Back failed to restore native controls");
        require(nativeTabs->isVisible()==initiallyTabs &&
                nativeTabs->minimumHeight()==initiallyTabMin &&
                nativeTabs->maximumHeight()==initiallyTabMax,
                "Back did not restore original native tab geometry");
        if(lateToolbar)
            require(lateToolbar->isVisible(),
                    "Back did not restore toolbar created during mobile mode");
        if(lateDocker)
            require(lateDocker->isVisible(),
                    "Back did not restore docker created during mobile mode");
        if(lateTab)
            require(lateTab->isVisible() &&
                    lateTab->minimumHeight()==lateTabOriginalMin &&
                    lateTab->maximumHeight()==lateTabOriginalMax,
                    "Back did not restore dynamically added Krita tab");
        require(!top->isVisible() && !bottom->isVisible() &&
                !side->isVisible(),
                "Back left mobile controls above Krita canvas");
        require(canvas->property("syntheticDocumentRevision").toInt()==12345,
                "Back changed synthetic drawing state");
    }

    // Simulate an unexpected replacement while already in mobile mode.
    // Restore the desktop controls even though the original native canvas
    // is no longer in the central slot; never delete an unrelated canvas.
    require(mobile.activate(),"could not prepare replacement safety test");
    QWidget *displaced=backend.takeCentralWidget();
    require(displaced==canvas,"unexpected central canvas identity");
    QWidget *replacement=new QWidget;
    backend.setCentralWidget(replacement);
    require(!mobile.deactivate(),
            "native host incorrectly reported a valid canvas after replacement");
    QApplication::processEvents();
    require(backend.centralWidget()==replacement,
            "native-host adapter overwrote an unrelated central widget");
    require(!top->isVisible() && !bottom->isVisible() &&
            !side->isVisible(),
            "mobile docks remained visible after unexpected native replacement");
    require(backend.menuBar()->isVisible()==initiallyMenu &&
            backend.statusBar()->isVisible()==initiallyStatus &&
            originalToolbar->isVisible()==initiallyToolbar &&
            originalDock->isVisible()==initiallyDock,
            "desktop controls were not restored after native canvas replacement");
    require(!mobile.activate(),
            "native-host adapter accepted a replaced Krita canvas");
    require(observer.changes>=parentChangesAtInstall,
            "parent observer failed unexpectedly");

    qInfo() << "PASS: 25 chrome-only switches, identical canvas identity,"
            << "no reparenting during switches, tab and desktop restoration,"
            << "empty-document dock recovery, late toolbar/tab/docker restoration,"
            << "synthetic document retention,"
            << "and desktop recovery when"
            << "Krita unexpectedly replaces its canvas";
    return 0;
}
