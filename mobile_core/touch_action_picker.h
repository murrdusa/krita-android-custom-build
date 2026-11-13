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


// Contextual options use a compact list over the canvas. The QMenu
// supplies action metadata and can be destroyed as soon as this returns.
#include "transient_panel.h"

inline void mobileShowActionList(QWidget *host,QWidget *anchor,const QMenu &menu,
                                 QObject *receiver,std::function<void(const QVariant &)> picked)
{
    if(MobileTransientPanel::closeForAnchor(host,anchor))return;
    auto *panel=new MobileTransientPanel(host);
    panel->setObjectName(QStringLiteral("mobileContextOptionList"));
    panel->setAccessibleName(menu.title());
    panel->setStyleSheet(QStringLiteral(
        "QFrame#mobileContextOptionList,QListWidget{background:#252529;color:#f4f4f5;"
        "border:1px solid #55555c;border-radius:8px;}"
        "QListWidget::item{min-height:40px;padding:0 9px;}"
        "QListWidget::item:disabled{color:#909097;}"
        "QListWidget::item:selected{background:#3b586f;color:white;}"));
    auto *layout=new QVBoxLayout(panel);
    layout->setContentsMargins(4,4,4,4);
    auto *list=new QListWidget(panel);
    for(auto *action:menu.actions()) {
        if(!action->isVisible() || action->isSeparator())continue;
        if(action->property("mobileSectionHeading").toBool()) {
            auto *heading=new QListWidgetItem(action->text(),list);
            QFont font=heading->font();
            font.setBold(true);
            heading->setFont(font);
            heading->setForeground(QColor("#bfc9dc"));
            heading->setFlags(Qt::NoItemFlags);
            continue;
        }
        auto *item=new QListWidgetItem(action->icon(),
            (action->isCheckable() && action->isChecked()?QStringLiteral("✓  "):QString())+
            action->text(),list);
        item->setData(Qt::UserRole,action->data());
        if(!action->isEnabled())item->setFlags(Qt::NoItemFlags);
    }
    if(!list->count()) {panel->deleteLater();return;}
    MobileTouchScroll::configureList(list);
    layout->addWidget(list);
    const QPointer<QObject> alive=receiver;
    QObject::connect(list,&QListWidget::itemClicked,panel,[panel,alive,picked](QListWidgetItem *item) {
        if(!alive || !item || !(item->flags()&Qt::ItemIsEnabled))return;
        const QVariant value=item->data(Qt::UserRole);
        panel->close();
        QTimer::singleShot(0,alive,[picked,value]() {picked(value);});
    });
    panel->anchorTo(anchor,QSize(245,qMin(330,10+41*list->count())));
    panel->present();
}
