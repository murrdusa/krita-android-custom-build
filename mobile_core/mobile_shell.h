/*
 * KritaMobilePilot: opt-in mobile-first shell around Krita's real canvas.
 * Copyright 2026 contributors.
 *
 * Experimental UI integration proof; Krita's brush, image, selection,
 * transform, undo and document subsystems remain authoritative.
 * No Q_OBJECT or MOC required.
 */
#pragma once

#ifdef Q_OS_ANDROID

#include <QAction>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <functional>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "KisDocument.h"
#include "kis_canvas_resource_provider.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include <KoColor.h>
#include <KoToolManager.h>
#include <klocalizedstring.h>

class KritaMobilePilot final : public QObject
{
public:
    KritaMobilePilot(KisMainWindow *window,
                     QMdiArea *mdi,
                     KisViewManager *viewManager,
                     QAction *toggle)
        : QObject(window),
          m_window(window),
          m_mdi(mdi),
          m_viewManager(viewManager),
          m_toggle(toggle)
    {
        // The existing QMdiArea keeps its real canvas, full Krita input
        // system and ongoing document state. This only adds controls.
        m_surface = new QFrame(m_mdi->viewport());
        m_surface->setObjectName(QStringLiteral("kritaMobilePilot"));
        m_surface->setAttribute(Qt::WA_StyledBackground,true);
        m_surface->setStyleSheet(
            "QFrame#kritaMobilePilot{background:rgba(32,39,47,244);"
            "border:1px solid #708da5;border-radius:12px;}"
            "QToolButton{background:#40566b;color:#f4f7fa;"
            "border:1px solid #718ea4;border-radius:8px;"
            "font-size:13px;font-weight:500;padding:5px;}"
            "QToolButton:pressed{background:#537b9b;}"
            "QLabel{color:#f2f5fa;}");
        auto *outer=new QVBoxLayout(m_surface);
        outer->setContentsMargins(9,8,9,8);
        outer->setSpacing(6);

        auto *heading=new QHBoxLayout;
        heading->setSpacing(8);
        auto *mark=new QLabel(i18n("Krita Core · mobile pilot"),m_surface);
        mark->setStyleSheet("font-weight:600;font-size:12px;");
        heading->addWidget(mark);
        m_documentLabel=new QLabel(i18n("No document"),m_surface);
        m_documentLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        heading->addWidget(m_documentLabel,1);
        auto *exit=makeButton(i18n("Exit"),[toggle]() {
            if(toggle)toggle->setChecked(false);
        });
        exit->setToolTip(i18n("Restore Krita's original interface"));
        heading->addWidget(exit);
        outer->addLayout(heading);

        auto *files=new QHBoxLayout;
        files->setSpacing(5);
        addButton(files,i18n("Open"),[this](){trigger(QStringLiteral("file_open"));});
        addButton(files,i18n("Save"),[this](){trigger(QStringLiteral("file_save"));});
        addButton(files,i18n("Undo"),[this](){trigger(QStringLiteral("edit_undo"));});
        addButton(files,i18n("Redo"),[this](){trigger(QStringLiteral("edit_redo"));});
        outer->addLayout(files);

        auto *tools=new QHBoxLayout;
        tools->setSpacing(5);
        addButton(tools,i18n("Paint"),[this](){activatePaint(false);});
        addButton(tools,i18n("Erase"),[this](){activatePaint(true);});
        addButton(tools,i18n("Layers"),[this](){openLayers();});
        addButton(tools,i18n("Color"),[this](){openColor();});
        addButton(tools,i18n("Menus"),[this](){openMenus();});
        outer->addLayout(tools);

        m_surface->hide();
        m_mdi->viewport()->installEventFilter(this);
        connect(m_mdi,&QMdiArea::subWindowActivated,this,
                [this](QMdiSubWindow*){refreshDocumentLabel();});
        if(m_viewManager && m_viewManager->nodeManager()) {
            connect(m_viewManager->nodeManager(),
                    &KisNodeManager::sigNodeActivated,this,
                    [this](KisNodeSP){refreshDocumentLabel();});
        }
    }

    void setActive(bool enabled)
    {
        if(m_active==enabled || !m_surface)return;
        if(enabled) {
            rememberAndHideNativeChrome();
            m_active=true;
            refreshDocumentLabel();
            reposition();
            m_surface->show();
            m_surface->raise();
        } else {
            m_active=false;
            m_surface->hide();
            restoreNativeChrome();
        }
    }

    bool active() const {return m_active;}

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_mdi->viewport() && m_active &&
           (event->type()==QEvent::Resize || event->type()==QEvent::Show)) {
            QTimer::singleShot(0,this,[this](){
                if(m_active)reposition();
            });
        }
        return QObject::eventFilter(watched,event);
    }

private:
    struct SavedWidget {
        QPointer<QWidget> widget;
    };

    QToolButton *makeButton(const QString &label,std::function<void()> callback)
    {
        auto *button=new QToolButton(m_surface);
        button->setText(label);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setMinimumHeight(46);
        button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        connect(button,&QToolButton::clicked,this,
                [callback](){callback();});
        return button;
    }
    void addButton(QHBoxLayout *row,const QString &label,
                   std::function<void()> callback)
    {
        row->addWidget(makeButton(label,callback),1);
    }
    void trigger(const QString &actionId)
    {
        if(!m_window)return;
        if(QAction *action=m_window->actionCollection()->action(actionId)) {
            if(action->isEnabled())action->trigger();
        }
    }
    void activatePaint(bool erasing)
    {
        // No new brush engine: the same registered Krita freehand tool and
        // native Erase toggle used by the desktop UI.
        KoToolManager::instance()->switchToolRequested(
            QStringLiteral("KritaShape/KisToolBrush"));
        QAction *eraser=m_window->actionCollection()->action(
            QStringLiteral("erase_action"));
        if(eraser && eraser->isEnabled() &&
           (!eraser->isCheckable() || eraser->isChecked()!=erasing))
            eraser->trigger();
    }
    void refreshDocumentLabel()
    {
        if(!m_documentLabel || !m_viewManager)return;
        KisDocument *document=m_viewManager->document();
        if(!document) {
            m_documentLabel->setText(i18n("No document"));
            return;
        }
        const KisNodeSP node=m_viewManager->activeNode();
        m_documentLabel->setText(
            node ? node->name().left(22) : i18n("Canvas ready"));
    }
    void openLayers()
    {
        if(!m_viewManager || !m_viewManager->nodeManager())return;
        const auto image=m_viewManager->image().toStrongRef();
        if(!image || !image->root())return;
        QDialog dialog(m_window);
        dialog.setWindowTitle(i18n("Layers · native engine"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        auto *outer=new QVBoxLayout(&dialog);
        auto *title=new QLabel(i18n("Choose a layer"),&dialog);
        outer->addWidget(title);
        auto *list=new QListWidget(&dialog);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setStyleSheet(
            "QListWidget{background:#2f3742;color:#fafbfc;"
            "font-size:15px;border:1px solid #768b9f;}"
            "QListWidget::item{min-height:45px;padding:4px;}");
        outer->addWidget(list,1);
        QVector<KisNodeSP> candidates;
        std::function<void(KisNodeSP,int)> collect;
        collect=[&](KisNodeSP parent,int depth) {
            if(!parent || depth>12 || candidates.size()>=200)return;
            for(KisNodeSP node=parent->lastChild();node;
                node=node->prevSibling()) {
                if(node->inherits("KisReferenceImagesLayer"))continue;
                candidates.append(node);
                QString prefix(depth*2,QLatin1Char(' '));
                auto *item=new QListWidgetItem(prefix+node->name(),list);
                item->setData(Qt::UserRole,candidates.size()-1);
                if(node==m_viewManager->activeNode())list->setCurrentItem(item);
                if(node->childCount())collect(node,depth+1);
            }
        };
        collect(image->root(),0);
        connect(list,&QListWidget::itemClicked,&dialog,
                [this,&dialog,&candidates](QListWidgetItem *item) {
            const int index=item?item->data(Qt::UserRole).toInt():-1;
            if(index<0 || index>=candidates.size())return;
            const KisNodeSP node=candidates.at(index);
            auto *manager=m_viewManager->nodeManager();
            manager->slotUiActivatedNode(node);
            manager->slotSetSelectedNodes(KisNodeList{node});
            dialog.accept();
        });
        auto *close=new QPushButton(i18n("Close"),&dialog);
        close->setMinimumHeight(48);
        outer->addWidget(close);
        connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        const QSize viewport=m_mdi->viewport()->size();
        dialog.resize(qMax(260,qMin(390,viewport.width()-24)),
                      qMax(290,qMin(570,viewport.height()-40)));
        dialog.exec();
    }
    void openColor()
    {
        if(!m_viewManager || !m_viewManager->canvasResourceProvider())return;
        auto *provider=m_viewManager->canvasResourceProvider();
        const KoColor original=provider->fgColor();
        // Placeholder until the native wheel and history model are exposed
        // through a dedicated mobile color-selector component.
        const QColor choice=QColorDialog::getColor(
            original.toQColor(),m_window,i18n("Foreground color"));
        if(choice.isValid())
            provider->setFGColor(KoColor(choice,original.colorSpace()));
    }
    void openMenus()
    {
        QMenu native(m_window);
        if(QMenuBar *bar=m_window->menuBar()) {
            for(QAction *top:bar->actions())
                if(top && top->menu())native.addMenu(top->menu());
        }
        native.exec(QCursor::pos());
    }
    void rememberAndHideNativeChrome()
    {
        m_wasMenuVisible=m_window->menuBar() &&
                         m_window->menuBar()->isVisible();
        m_wasStatusVisible=m_window->statusBar() &&
                           m_window->statusBar()->isVisible();
        m_hidden.clear();
        // Hide only currently visible dockers/toolbars. The main window's
        // own canvas, tabs and tool state are never recreated or reparented.
        for(QDockWidget *dock:m_window->findChildren<QDockWidget*>())
            if(dock->isVisible()) {
                m_hidden.append({dock});
                dock->hide();
            }
        for(QToolBar *toolbar:m_window->findChildren<QToolBar*>())
            if(toolbar->isVisible()) {
                m_hidden.append({toolbar});
                toolbar->hide();
            }
        if(m_window->menuBar())m_window->menuBar()->hide();
        if(m_window->statusBar())m_window->statusBar()->hide();
    }
    void restoreNativeChrome()
    {
        for(const SavedWidget &saved:m_hidden)
            if(saved.widget)saved.widget->show();
        m_hidden.clear();
        if(m_window->menuBar())
            m_window->menuBar()->setVisible(m_wasMenuVisible);
        if(m_window->statusBar())
            m_window->statusBar()->setVisible(m_wasStatusVisible);
    }
    void reposition()
    {
        if(!m_surface || !m_mdi)return;
        QWidget *viewport=m_mdi->viewport();
        const int width=qMin(595,qMax(250,viewport->width()-12));
        const int height=m_surface->sizeHint().height();
        m_surface->setGeometry(qMax(0,(viewport->width()-width)/2),
                               qMax(0,viewport->height()-height-10),
                               width,height);
        m_surface->raise();
    }

    QPointer<KisMainWindow> m_window;
    QPointer<QMdiArea> m_mdi;
    QPointer<QFrame> m_surface;
    KisViewManager *m_viewManager=nullptr;
    QPointer<QAction> m_toggle;
    QLabel *m_documentLabel=nullptr;
    bool m_active=false;
    bool m_wasMenuVisible=false;
    bool m_wasStatusVisible=false;
    QVector<SavedWidget> m_hidden;
};

#endif // Q_OS_ANDROID
