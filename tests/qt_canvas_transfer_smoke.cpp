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
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "mobile_core/canvas_transfer.h"

static void require(bool condition,const char *description)
{
    if(!condition)qFatal("Canvas transfer test failed: %s",description);
}

class TestSurface final : public QWidget, public MobileCanvasSurface
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

private:
    QPointer<QWidget> m_canvas;
    QVBoxLayout *m_layout=nullptr;
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
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
    // An occupied destination must never steal a native Krita widget.
    TestSurface occupied;
    occupied.attachNativeCanvas(new QWidget);
    require(!transfer.enter(&occupied),"accepted occupied destination");
    require(backend.centralWidget()==canvas,"changed desktop after refused entry");
    require(!transfer.active(),"marked refused entry active");

    for(int cycle=0;cycle<10;++cycle) {
        auto *mobile=new TestSurface;
        require(transfer.enter(mobile),"failed to embed mobile presentation");
        QApplication::processEvents();
        require(transfer.active(),"transfer not active after entering");
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

        require(transfer.leave(),"failed to restore the native canvas");
        QApplication::processEvents();
        require(!transfer.active(),"transfer still active after returning");
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

    qInfo() << "PASS: ten reversible same-window transfers, chrome restore,"
            << "canvas identity, occupied-destination and occupied-backend guards";
    return 0;
}
