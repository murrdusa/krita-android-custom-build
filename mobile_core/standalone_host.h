/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Experimental independent *window* (not an independent engine executable):
 * the UI owns its application chrome and hosts Krita's real widgetStack.
 * Krita remains the backend service for lifecycle, tools, canvas and saving.
 *
 * The move between native Krita and this host is reversible, with the actual
 * QWidget canvas stack transferred rather than mirrored or imitated.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QSettings>
#include <QCursor>
#include <QVector>
#include <functional>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "KisDocument.h"
#include "kis_canvas_resource_provider.h"
#include "kis_node.h"
#include "kis_node_manager.h"
#include "kis_image.h"
#include <KoColor.h>
#include <KoToolManager.h>
#include <klocalizedstring.h>

#include "engine_bridge.h"
#include "document_workflow.h"
#include "command_browser.h"
#include "brush_workspace.h"
#include "layer_workspace.h"

class MobileStandaloneWindow final : public QMainWindow
{
public:
    MobileStandaloneWindow(const MobileEngineBridge &bridge, QMdiArea *mdi)
        : QMainWindow(nullptr),m_engine(bridge),m_mdi(mdi)
    {
        setObjectName(QStringLiteral("KritaMobileIndependentWindow"));
        setWindowTitle(i18n("Krita Mobile Studio"));
        setAttribute(Qt::WA_DeleteOnClose,false);
        setStyleSheet(
            "QMainWindow#KritaMobileIndependentWindow{background:#202934;}"
            "QFrame#mobileHeader,QFrame#mobileFooter{background:#25313e;"
            "border:0;}"
            "QLabel{color:#edf3f9;font-size:13px;}"
            "QToolButton,QPushButton{background:#405a72;color:#f7faff;"
            "border:1px solid #6c8daa;border-radius:7px;"
            "padding:6px;font-size:13px;}"
            "QToolButton:pressed,QPushButton:pressed{background:#547f9c;}");
        auto *root=new QWidget(this);
        root->setObjectName(QStringLiteral("MobileApplicationRoot"));
        auto *layout=new QVBoxLayout(root);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        setCentralWidget(root);

        auto *header=new QFrame(root);
        header->setObjectName(QStringLiteral("mobileHeader"));
        auto *top=new QHBoxLayout(header);
        top->setContentsMargins(7,4,7,4);
        top->setSpacing(4);

        auto *home=new QToolButton(header);
        home->setText(i18n("Back"));
        home->setToolTip(i18n("Return to the existing Krita interface"));
        home->setMinimumSize(58,43);
        connect(home,&QToolButton::clicked,this,[this](){requestExit();});
        top->addWidget(home);

        auto *title=new QLabel(i18n("Mobile Studio"),header);
        title->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
        top->addWidget(title);

        m_document=new QLabel(i18n("No document"),header);
        m_document->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        m_document->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        top->addWidget(m_document,1);
        layout->addWidget(header,0);

        // The Krita canvas (including its native MDI document view) is
        // transferred into this slot. It is never recreated or resampled.
        // The canvas and layers are sibling views in the mobile window.
        // Unlike a modal dialog, layer operations do not block painting,
        // save, undo or other canvas navigation.
        auto *paintingArea=new QWidget(root);
        auto *paintingLayout=new QHBoxLayout(paintingArea);
        paintingLayout->setContentsMargins(0,0,0,0);
        paintingLayout->setSpacing(0);
        m_canvasSlot=new QWidget(paintingArea);
        m_canvasSlot->setObjectName(QStringLiteral("MobileNativeCanvasSlot"));
        m_canvasSlot->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_canvasSlot->setMinimumSize(0,0);
        m_canvasLayout=new QVBoxLayout(m_canvasSlot);
        m_canvasLayout->setContentsMargins(0,0,0,0);
        m_canvasLayout->setSpacing(0);
        paintingLayout->addWidget(m_canvasSlot,1);
        m_layers=new KritaMobileLayersPanel(m_engine,paintingArea);
        m_layers->onRequestClose=[this]() {
            if(m_layers)m_layers->hide();
            if(m_layersButton)m_layersButton->setChecked(false);
        };
        paintingLayout->addWidget(m_layers,0);
        m_layers->hide();
        layout->addWidget(paintingArea,1);

        m_brushWorkspace=new KritaMobileBrushWorkspace(m_engine,root);
        m_brushWorkspace->setSizePolicy(
            QSizePolicy::Expanding,QSizePolicy::Fixed);
        layout->addWidget(m_brushWorkspace,0);

        auto *footer=new QFrame(root);
        footer->setObjectName(QStringLiteral("mobileFooter"));
        auto *buttons=new QHBoxLayout(footer);
        buttons->setContentsMargins(6,4,6,6);
        buttons->setSpacing(5);
        auto command=[this,buttons,footer](const QString &text,
                                           const char *id) {
            auto *button=new QToolButton(footer);
            button->setText(text);
            button->setMinimumHeight(45);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            connect(button,&QToolButton::clicked,this,[this,id]() {
                // Document commands are mobile-owned workflows; undo/redo
                // still invoke Krita's registered actions directly.
                const QString commandId=QString::fromLatin1(id);
                if(commandId==QStringLiteral("file_new"))
                    MobileDocumentWorkflow::create(m_engine,this);
                else if(commandId==QStringLiteral("file_open"))
                    MobileDocumentWorkflow::open(m_engine,this);
                else if(commandId==QStringLiteral("file_save"))
                    MobileDocumentWorkflow::save(m_engine,this);
                else m_engine.run(id);
                refresh();
            });
            buttons->addWidget(button,1);
            return button;
        };
        command(i18n("New"),"file_new");
        command(i18n("Open"),"file_open");
        m_saveButton=command(i18n("Save"),"file_save");
        command(i18n("Undo"),"edit_undo");
        command(i18n("Redo"),"edit_redo");

        auto custom=[this,buttons,footer](const QString &text,
                                           std::function<void()> callback) {
            auto *button=new QToolButton(footer);
            button->setText(text);
            button->setMinimumHeight(45);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            connect(button,&QToolButton::clicked,this,[callback]() {
                callback();
            });
            buttons->addWidget(button,1);
            return button;
        };
        m_layersButton=custom(i18n("Layers"),[this](){openLayers();});
        m_layersButton->setCheckable(true);
        custom(i18n("Color"),[this](){openColor();});
        m_more=custom(i18n("More"),[this](){openMore();});
        layout->addWidget(footer,0);

        if(m_mdi)
            connect(m_mdi,&QMdiArea::subWindowActivated,this,
                    [this](QMdiSubWindow*){refresh();});
        m_updates=new QTimer(this);
        m_updates->setInterval(450);
        connect(m_updates,&QTimer::timeout,this,[this](){
            if(isVisible())refresh();
        });
        m_updates->start();
    }

    void setExitCallback(std::function<void()> handler) {
        m_exit=std::move(handler);
    }
    void attachEngineCanvas(QWidget *nativeCanvas)
    {
        if(!nativeCanvas || m_nativeCanvas) return;
        m_nativeCanvas=nativeCanvas;
        m_canvasLayout->addWidget(nativeCanvas,1);
        nativeCanvas->show();
        refresh();
    }
    QWidget *releaseEngineCanvas()
    {
        QWidget *canvas=m_nativeCanvas;
        if(!canvas)return nullptr;
        m_nativeCanvas=nullptr;
        m_canvasLayout->removeWidget(canvas);
        canvas->hide();
        canvas->setParent(nullptr);
        return canvas;
    }
    bool hasEngineCanvas() const {return m_nativeCanvas!=nullptr;}
    void refresh()
    {
        if(!m_document)return;
        const bool has=m_engine.hasDocument();
        if(m_saveButton)m_saveButton->setEnabled(has);
        const QString node=m_engine.activeLayerName();
        if(!has) {
            m_document->setText(i18n("No document"));
        } else {
            const QString title=m_engine.activeDocumentTitle();
            const QString modified=m_engine.documentModified()?
                QStringLiteral(" *"):QString();
            m_document->setText(title+modified+
                (node.isEmpty()?QString():QStringLiteral(" · ")+node.left(18)));
            m_document->setToolTip(i18n("Document · Active layer"));
        }
        if(m_brushWorkspace && has)m_brushWorkspace->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshFromEngine();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        // Never let a system Back gesture silently close an unsaved document.
        event->ignore();
        requestExit();
    }

private:
    void requestExit()
    {
        if(m_exit)m_exit();
    }
    void openMore()
    {
        if(!m_more)return;
        QMenu menu(this);
        auto *saveAs=menu.addAction(i18n("Save As..."));
        connect(saveAs,&QAction::triggered,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                MobileDocumentWorkflow::save(m_engine,this,true);
                refresh();
            });
        });
        menu.addSeparator();
        auto *browse=menu.addAction(i18n("Search all Krita commands"));
        connect(browse,&QAction::triggered,this,[this]() {
            // Let the menu close before opening the independent browser.
            QTimer::singleShot(0,this,[this]() {
                MobileCommandBrowser::open(m_engine,this);
            });
        });
        menu.addSeparator();
        auto *transform=menu.addAction(i18n("Transform selected pixels/layers"));
        connect(transform,&QAction::triggered,this,[this](){
            KoToolManager::instance()->switchToolRequested(
                QStringLiteral("KisToolTransform"));
        });
        auto *move=menu.addAction(i18n("Move tool"));
        connect(move,&QAction::triggered,this,[](){
            KoToolManager::instance()->switchToolRequested(
                QStringLiteral("KritaTransform/KisToolMove"));
        });
        menu.addSeparator();
        auto *nativeMenu=m_engine.nativeMenus(&menu);
        nativeMenu->setTitle(i18n("Legacy menus (compatibility)"));
        menu.addMenu(nativeMenu);
        menu.exec(m_more->mapToGlobal(QPoint(0,m_more->height())));
    }
    void openLayers()
    {
        if(!m_layers)return;
        if(!m_engine.manager() || !m_engine.hasDocument()) {
            if(m_layersButton)m_layersButton->setChecked(false);
            return;
        }
        const bool opening=!m_layers->isVisible();
        m_layers->setVisible(opening);
        if(m_layersButton)m_layersButton->setChecked(opening);
        if(opening)m_layers->refreshNow();
    }
    void openColor()
    {
        auto *provider=m_engine.brushResources();
        if(!provider)return;
        const KoColor initial=provider->fgColor();
        const QColor color=QColorDialog::getColor(
            initial.toQColor(),this,i18n("Brush color"));
        if(color.isValid())
            provider->setFGColor(KoColor(color,initial.colorSpace()));
    }

    MobileEngineBridge m_engine;
    QPointer<QMdiArea> m_mdi;
    QPointer<QWidget> m_nativeCanvas;
    QPointer<QWidget> m_canvasSlot;
    QVBoxLayout *m_canvasLayout=nullptr;
    KritaMobileBrushWorkspace *m_brushWorkspace=nullptr;
    KritaMobileLayersPanel *m_layers=nullptr;
    QToolButton *m_layersButton=nullptr;
    QLabel *m_document=nullptr;
    QToolButton *m_more=nullptr,*m_saveButton=nullptr;
    QTimer *m_updates=nullptr;
    std::function<void()> m_exit;
};

class MobileAppController final : public QObject
{
public:
    MobileAppController(KisMainWindow *main,
                        QMdiArea *mdi,
                        KisViewManager *manager,
                        QAction *entry)
        : QObject(main),m_main(main),m_mdi(mdi),m_bridge(main,manager),
          m_entry(entry)
    {
        connect(entry,&QAction::toggled,this,[this](bool value) {
            if(value)enter();else leave();
        });
    }
    ~MobileAppController() override
    {
        if(m_inMobile)leave();
        delete m_host;
    }

private:
    void enter()
    {
        if(m_inMobile || !m_main)return;
        if(!m_host) {
            m_host=new MobileStandaloneWindow(m_bridge,m_mdi);
            m_host->setExitCallback([this]() {
                if(m_entry)m_entry->setChecked(false);
            });
        }
        QWidget *native=m_main->takeCentralWidget();
        if(!native) {
            if(m_entry) {
                const QSignalBlocker block(m_entry);
                m_entry->setChecked(false);
            }
            return;
        }
        m_originalState=m_main->windowState();
        m_originalCentral=native;
        m_host->attachEngineCanvas(native);
        m_inMobile=true;
        m_host->showFullScreen();
        m_host->raise();
        m_host->activateWindow();
        m_main->hide();
        m_host->refresh();
    }
    void leave()
    {
        if(!m_inMobile || !m_main || !m_host)return;
        QWidget *native=m_host->releaseEngineCanvas();
        if(native) {
            m_main->setCentralWidget(native);
            native->show();
        }
        // Main window is a temporary backend only in mobile mode.
        // Preserve the original desktop/compact state on return.
        m_main->setWindowState(m_originalState);
        m_main->show();
        m_main->raise();
        m_host->hide();
        m_main->activateWindow();
        m_inMobile=false;
        m_originalCentral=nullptr;
    }
    QPointer<KisMainWindow> m_main;
    QPointer<QMdiArea> m_mdi;
    MobileEngineBridge m_bridge;
    QPointer<QAction> m_entry;
    MobileStandaloneWindow *m_host=nullptr;
    QPointer<QWidget> m_originalCentral;
    Qt::WindowStates m_originalState;
    bool m_inMobile=false;
};
#endif // Q_OS_ANDROID
