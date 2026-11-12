// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include "touch_scroll.h"

inline QAction *mobilePickAction(QWidget *parent,const QMenu &menu)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(menu.title());
    dialog.setStyleSheet(QStringLiteral(
        "QDialog,QListWidget{background:#252529;color:#f4f4f5;}"
        "QListWidget::item{min-height:44px;padding:0 9px;}"
        "QListWidget::item:disabled{color:#909097;}"
        "QListWidget::item:selected{background:#3b586f;color:white;}"
        "QPushButton{background:#34343a;color:#f4f4f5;min-height:44px;}"));
    auto *layout=new QVBoxLayout(&dialog);
    auto *list=new QListWidget(&dialog);
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
    auto *close=new QPushButton(QObject::tr("Cancel"),&dialog);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
    layout->addWidget(close);
    QPointer<QAction> selected;
    QObject::connect(list,&QListWidget::itemClicked,&dialog,[&](QListWidgetItem *item) {
        if(!item || !(item->flags()&Qt::ItemIsEnabled))return;
        selected=actions.value(item->data(Qt::UserRole).toInt());
        if(selected && selected->isEnabled())dialog.accept();
    });
    const QSize area=parent->window()->size();
    dialog.resize(qMin(320,qMax(1,area.width()-24)),
                  qMin(84+44*qMin(8,actions.size()),qMax(1,area.height()-24)));
    if(dialog.exec()!=QDialog::Accepted || !selected || !selected->isEnabled())return nullptr;
    if(auto *submenu=selected->menu())return mobilePickAction(parent,*submenu);
    return selected.data();
}
