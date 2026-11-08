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
#include "brush_workspace.h"

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
        m_canvasSlot=new QWidget(root);
        m_canvasSlot->setObjectName(QStringLiteral("MobileNativeCanvasSlot"));
        m_canvasSlot->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_canvasSlot->setMinimumSize(0,0);
        m_canvasLayout=new QVBoxLayout(m_canvasSlot);
        m_canvasLayout->setContentsMargins(0,0,0,0);
        m_canvasLayout->setSpacing(0);
        layout->addWidget(m_canvasSlot,1);

        m_brushWorkspace=new KritaMobileBrushWorkspace(
            m_engine.legacyHost(),m_engine.manager(),root);
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
                m_engine.run(id);
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
        custom(i18n("Layers"),[this](){openLayers();});
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
        m_document->setText(!has?i18n("No document"):
                            node.isEmpty()?i18n("Canvas"):node.left(28));
        if(m_brushWorkspace && has)m_brushWorkspace->refresh();
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
        auto *transform=menu.addAction(i18n("Transform selected pixels/layers"));
        connect(transform,&QAction::triggered,this,[this](){
            m_engine.freehandBrush(); // Switch tool intentionally below.
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
        nativeMenu->setTitle(i18n("All Krita commands"));
        menu.addMenu(nativeMenu);
        menu.exec(m_more->mapToGlobal(QPoint(0,m_more->height())));
    }
    void openLayers()
    {
        auto *manager=m_engine.manager();
        if(!manager || !manager->nodeManager())return;
        const auto image=manager->image().toStrongRef();
        if(!image || !image->root())return;
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Layers"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#283643;color:#eff7ff;}"
            "QListWidget{background:#2c4051;color:white;font-size:14px;}"
            "QListWidget::item{min-height:46px;padding:4px;}"
            "QPushButton{background:#45647e;color:white;min-height:44px;}");
        auto *layout=new QVBoxLayout(&dialog);
        auto *heading=new QLabel(i18n("Layers · Krita image model"),&dialog);
        layout->addWidget(heading);
        auto *list=new QListWidget(&dialog);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        layout->addWidget(list,1);
        QVector<KisNodeSP> nodes;
        std::function<void(KisNodeSP,int)> visit;
        visit=[&](KisNodeSP parent,int depth) {
            if(!parent || depth>12 || nodes.size()>300)return;
            for(KisNodeSP node=parent->lastChild();node;node=node->prevSibling()) {
                nodes.append(node);
                auto *item=new QListWidgetItem(
                    QString(depth*2,QLatin1Char(' '))+node->name(),list);
                item->setData(Qt::UserRole,nodes.size()-1);
                if(node==manager->activeNode())list->setCurrentItem(item);
                if(node->childCount())visit(node,depth+1);
            }
        };
        visit(image->root(),0);
        connect(list,&QListWidget::itemClicked,&dialog,
                [this,&dialog,&nodes](QListWidgetItem *item) {
            const int id=item?item->data(Qt::UserRole).toInt():-1;
            if(id>=0 && id<nodes.size() && m_engine.selectLayer(nodes.at(id)))
                dialog.accept();
        });
        auto *done=new QPushButton(i18n("Close"),&dialog);
        connect(done,&QPushButton::clicked,&dialog,&QDialog::reject);
        layout->addWidget(done);
        dialog.resize(qMax(300,qMin(450,width()-32)),
                      qMax(360,qMin(660,height()-80)));
        dialog.exec();
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
