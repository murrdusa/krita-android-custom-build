// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QPushButton>
#include <QVBoxLayout>

#include "touch_scroll.h"

inline QAction *mobilePickAction(QWidget *parent,const QMenu &menu)
{
    if(!parent)return nullptr;
    const QPointer<QWidget> host(parent);
    // The parent may be destroyed during exec(); a stack child
    // must not be deleted by Qt's parent ownership machinery.
    QPointer<QDialog> dialog(new QDialog(parent));
    const auto cleanup=qScopeGuard([&dialog]() {delete dialog.data();});
    dialog->setWindowTitle(menu.title());
    MobileTouchScroll::styleMenuDialog(*dialog,QSize(320,440));
    auto *layout=new QVBoxLayout(dialog.data());
    auto *list=new QListWidget(dialog.data());
    MobileTouchScroll::configureList(list);
    QVector<QPointer<QAction>> actions;
    for(auto *action:menu.actions()) {
        if(!action || !action->isVisible() || action->isSeparator())continue;
        QString text=action->text();
        text.remove(QLatin1Char('&'));
        if(action->menu())text+=QStringLiteral("  ›");
        if(action->isCheckable())
            text=(action->isChecked()?QStringLiteral("✓  "):QStringLiteral("    "))+text;
        auto *item=new QListWidgetItem(action->icon(),text,list);
        item->setData(Qt::UserRole,actions.size());
        if(!action->isEnabled())item->setFlags(Qt::NoItemFlags);
        actions.append(action);
    }
    if(actions.isEmpty())return nullptr;
    layout->addWidget(list,1);
    auto *close=new QPushButton(QObject::tr("Cancel"),dialog.data());
    QObject::connect(close,&QPushButton::clicked,dialog.data(),&QDialog::reject);
    layout->addWidget(close);
    QPointer<QAction> selected;
    QObject::connect(list,&QListWidget::itemClicked,dialog.data(),[&](QListWidgetItem *item) {
        if(!item || !(item->flags()&Qt::ItemIsEnabled))return;
        selected=actions.value(item->data(Qt::UserRole).toInt());
        if(selected && selected->isEnabled())dialog->accept();
    });
    const QSize area=parent->window()->size();
    dialog->resize(qMin(320,qMax(1,area.width()-24)),
                  qMin(84+44*qMin(8,actions.size()),qMax(1,area.height()-24)));
    const int result=dialog->exec();
    if(!dialog || !host || result!=QDialog::Accepted ||
       !selected || !selected->isEnabled())return nullptr;
    if(auto *submenu=selected->menu())return mobilePickAction(host.data(),*submenu);
    return selected.data();
}


// Contextual options use a compact list over the canvas. The QMenu
// supplies action metadata and can be destroyed as soon as this returns.
#include "transient_panel.h"

inline MobileTransientPanel *mobileShowActionList(QWidget *host,QWidget *anchor,const QMenu &menu,
                                 QObject *receiver,std::function<void(const QVariant &)> picked)
{
    if(!host || !receiver || !picked ||
       MobileTransientPanel::closeForAnchor(host,anchor))return nullptr;
    auto *panel=new MobileTransientPanel(host);
    panel->setObjectName(QStringLiteral("mobileContextOptionList"));
    panel->setAccessibleName(menu.title());
    auto *layout=new QVBoxLayout(panel);
    layout->setContentsMargins(4,4,4,4);
    auto *list=new QListWidget(panel);
    const int rowHeight=qMax(40,list->fontMetrics().height()+12);
    for(auto *action:menu.actions()) {
        if(!action->isVisible() || action->isSeparator())continue;
        if(action->property("mobileSectionHeading").toBool()) {
            auto *heading=new QListWidgetItem(action->text(),list);
            QFont font=heading->font();
            font.setBold(true);
            heading->setFont(font);
            heading->setForeground(QColor("#bfc9dc"));
            heading->setFlags(Qt::NoItemFlags);
            heading->setSizeHint(QSize(0,rowHeight));
            continue;
        }
        auto *item=new QListWidgetItem(action->icon(),
            (action->isCheckable() && action->isChecked()?QStringLiteral("✓  "):QString())+
            QString(action->text()).remove(QLatin1Char('&'))+
            (action->menu()?QStringLiteral("  ›"):QString()),list);
        item->setData(Qt::UserRole,action->data());
        item->setSizeHint(QSize(0,rowHeight));
        if(!action->isEnabled())item->setFlags(Qt::NoItemFlags);
    }
    if(!list->count()) {panel->deleteLater();return nullptr;}
    MobileTouchScroll::configureList(list);
    layout->addWidget(list);
    const QPointer<QObject> alive=receiver;
    QObject::connect(list,&QListWidget::itemClicked,panel,[panel,alive,picked](QListWidgetItem *item) {
        if(!alive || !item || !(item->flags()&Qt::ItemIsEnabled))return;
        const QVariant value=item->data(Qt::UserRole);
        panel->close();
        if(alive)QTimer::singleShot(0,alive.data(),[picked,value]() {picked(value);});
    });
    panel->anchorTo(anchor,QSize(300,12+rowHeight*list->count()),true);
    panel->present();
    return panel;
}

// Expose native QAction trees through the same touch list as contextual menus.
inline void mobileShowMenu(QWidget *host,QWidget *anchor,QMenu *source,QVector<QPointer<QMenu>> history={})
{
    if(!host || !source || MobileTransientPanel::closeForAnchor(host,anchor))return;
    const QPointer<QWidget> root(host),opener(anchor);
    const QPointer<QMenu> menu(source);
    // Dynamic native menus populate their actions before presentation.
    Q_EMIT menu->aboutToShow();
    if(!root || !menu)return;
    QMenu rows(menu->title());
    if(!menu->title().isEmpty()) {
        auto *heading=rows.addAction(menu->title());
        heading->setProperty("mobileSectionHeading",true);
    }
    if(!history.isEmpty())rows.addAction(QObject::tr("‹ Back"))->setData(-1);
    QVector<QPointer<QAction>> actions;
    for(auto *action:menu->actions()) {
        if(!action || !action->isVisible() || action->isSeparator())continue;
        auto *row=rows.addAction(action->icon(),action->text());
        row->setData(actions.size());
        row->setEnabled(action->isEnabled());
        row->setCheckable(action->isCheckable());
        row->setChecked(action->isChecked());
        if(action->menu())row->setText(row->text()+QStringLiteral("  ›"));
        actions.append(action);
    }
    if(actions.isEmpty() && history.isEmpty())return;
    mobileShowActionList(root,opener,rows,menu,[root,opener,menu,history,actions](const QVariant &value) mutable {
        if(!root || !root->isVisible())return;
        if(value.toInt()==-1) {
            if(!history.isEmpty()) {
                const auto previous=history.takeLast();
                if(previous)mobileShowMenu(root,opener,previous,history);
            }
            return;
        }
        const auto chosen=actions.value(value.toInt());
        if(!chosen || !chosen->isEnabled() || !chosen->isVisible())return;
        if(auto *submenu=chosen->menu()) {
            history.append(menu);
            mobileShowMenu(root,opener,submenu,history);
        } else chosen->trigger();
    });
}
