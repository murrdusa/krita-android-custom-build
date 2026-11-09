/*
 * Qt-only transfer smoke test with NO QMdiArea. A future Android Krita
 * host can provide a simple native QWidget stack while retaining the
 * original painting canvas, document identities and Back restoration.
 */
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include "mobile_core/canvas_transfer.h"

static void require(bool passed,const char *reason)
{
    if(!passed)qFatal("Generic document-root test failed: %s",reason);
}

class GenericMobileSurface final : public QWidget,public MobileCanvasSurface
{
public:
    GenericMobileSurface()
    {
        m_layout=new QVBoxLayout(this);
        m_layout->setContentsMargins(0,0,0,0);
    }
    QWidget *canvasSurfaceWindow() override {return this;}
    bool attachNativeCanvas(QWidget *canvas) override
    {
        if(!canvas || m_canvas)return false;
        m_layout->addWidget(canvas);
        m_canvas=canvas;
        canvas->show();
        return true;
    }
    QWidget *detachNativeCanvas(QWidget *parent=nullptr) override
    {
        QWidget *canvas=m_canvas.data();
        if(!canvas)return nullptr;
        m_canvas=nullptr;
        m_layout->removeWidget(canvas);
        canvas->hide();
        if(canvas->parentWidget()!=parent)canvas->setParent(parent);
        return canvas;
    }
    bool hasNativeCanvas() const override {return !m_canvas.isNull();}
    bool ownsNativeCanvas(QWidget *canvas) const override {
        return canvas && m_canvas==canvas;
    }
    void refreshCanvasSurface() override {if(m_canvas)m_canvas->update();}
private:
    QPointer<QWidget> m_canvas;
    QVBoxLayout *m_layout=nullptr;
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setOrganizationName(QStringLiteral("MobileStudioViewPortTest"));
    app.setApplicationName(QStringLiteral("GenericCanvasRoot"));
    QTemporaryDir settings;
    require(settings.isValid(),"test preferences path unavailable");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       settings.path());
    QSettings().setValue(QStringLiteral(
        "MobileStudio/persistentCanvasHostV1"),false);
    QMainWindow backend;
    backend.resize(740,530);
    auto *canvas=new QWidget;
    canvas->setProperty("nativeImageRevision",84);
    auto *rootLayout=new QVBoxLayout(canvas);
    auto *documentRoot=new QWidget(canvas);
    rootLayout->addWidget(documentRoot);
    auto *tab=new QTabBar(documentRoot);
    tab->addTab(QStringLiteral("Krita drawing"));
    auto *docsLayout=new QVBoxLayout(documentRoot);
    docsLayout->addWidget(tab);
    backend.setCentralWidget(canvas);
    backend.show();
    QApplication::processEvents();

    const bool oldTabVisible=tab->isVisible();
    const int tabMin=tab->minimumHeight(),tabMax=tab->maximumHeight();
    // Constructor accepts a generic QWidget document root, not MDI.
    MobileCanvasTransfer transfer(&backend,documentRoot);
    for(int i=0;i<8;++i) {
        auto *mobile=new GenericMobileSurface;
        require(transfer.enter(mobile),"could not attach generic document root");
        QApplication::processEvents();
        require(mobile->ownsNativeCanvas(canvas) && canvas->window()==&backend,
                "generic document host moved the canvas to another top-level");
        if(tab->isVisible() || tab->maximumHeight()!=0)
            qWarning() << "Generic root tab state:" << "visible="
                       << tab->isVisible() << "minimum=" << tab->minimumHeight()
                       << "maximum=" << tab->maximumHeight()
                       << "parent=" << tab->parentWidget();
        require(!tab->isVisible() && tab->maximumHeight()==0,
                "generic host did not hide native document tabs");
        require(transfer.leave(),"could not return to generic document host");
        QApplication::processEvents();
        require(backend.centralWidget()==canvas &&
                canvas->property("nativeImageRevision").toInt()==84,
                "generic host lost native widget or its drawing data");
        require(tab->isVisible()==oldTabVisible &&
                tab->minimumHeight()==tabMin && tab->maximumHeight()==tabMax,
                "generic host did not restore document tab state");
        require(!mobile->hasNativeCanvas(),
                "generic mobile UI still owns the original painting canvas");
        delete mobile;
    }
    qInfo() << "PASS: eight Qt QWidget document-root handoffs without"
            << "QMdiArea, preserved original canvas, tab restoration"
            << "and unchanged drawing state";
    return 0;
}
