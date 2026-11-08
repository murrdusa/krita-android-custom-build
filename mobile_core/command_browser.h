/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile-first searchable command browser. Labels and handlers come from
 * Krita's live action collection, never a copied desktop menu tree.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScroller>
#include <QScrollBar>
#include <QSize>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <klocalizedstring.h>
#include "engine_bridge.h"

class MobileCommandBrowser final
{
public:
    static void open(const MobileEngineBridge &engine,QWidget *parent)
    {
        if(!parent)return;
        QDialog dialog(parent);
        dialog.setWindowTitle(i18n("Commands"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#263645;color:#ecf4fc;}"
            "QLabel{color:#e9f3fd;font-size:14px;}"
            "QLineEdit{background:#354d61;color:#fff;"
            "border:1px solid #729db9;border-radius:8px;"
            "padding:8px;min-height:44px;font-size:15px;}"
            "QListWidget{background:#2b3d4f;color:white;"
            "border:1px solid #61839c;font-size:13px;}"
            "QListWidget::item{min-height:45px;padding:5px;}"
            "QPushButton{background:#476781;color:#f6fbff;"
            "border:1px solid #8cb5d0;border-radius:7px;"
            "padding:7px;min-height:44px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(7);
        auto *heading=new QLabel(i18n("All Krita commands"),&dialog);
        layout->addWidget(heading);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search command name or identifier"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *results=new QListWidget(&dialog);
        results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        results->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(results->viewport(),
                               QScroller::LeftMouseButtonGesture);
        layout->addWidget(results,1);
        auto *status=new QLabel(&dialog);
        layout->addWidget(status);
        auto *close=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(close);
        QObject::connect(close,&QPushButton::clicked,
                         &dialog,&QDialog::reject);

        QList<QPointer<QAction>> actions;
        for(QAction *native:engine.availableActions()) {
            if(!native || native->isSeparator() || native->menu() ||
               native->text().trimmed().isEmpty() ||
               native->objectName().trimmed().isEmpty())continue;
            actions.append(native);
        }
        const auto refill=[&]() {
            results->clear();
            const QString query=search->text().trimmed();
            int visible=0;
            for(int i=0;i<actions.size();++i) {
                QAction *native=actions.at(i);
                if(!native || !native->isEnabled())continue;
                QString name=native->text();
                name.remove(QLatin1Char('&'));
                const QString id=native->objectName();
                if(!query.isEmpty() &&
                   !name.contains(query,Qt::CaseInsensitive) &&
                   !id.contains(query,Qt::CaseInsensitive))continue;
                auto *item=new QListWidgetItem(name,results);
                item->setData(Qt::UserRole,i);
                item->setToolTip(id);
                ++visible;
            }
            status->setText(i18np("%1 available command",
                                  "%1 available commands",visible));
        };
        QObject::connect(search,&QLineEdit::textChanged,&dialog,
                         [&refill](const QString&){refill();});
        QPointer<QAction> choice;
        QObject::connect(results,&QListWidget::itemClicked,&dialog,
                         [&dialog,&actions,&choice](QListWidgetItem *item){
            if(!item)return;
            const int i=item->data(Qt::UserRole).toInt();
            if(i<0 || i>=actions.size())return;
            choice=actions.at(i);
            dialog.accept();
        });
        refill();
        const QSize bounds=parent->window()->size();
        dialog.resize(qMax(300,qMin(560,bounds.width()-26)),
                      qMax(300,qMin(730,bounds.height()-55)));
        dialog.exec();
        // Run only after modal search is closed. This keeps native document
        // dialogs and tools from being nested in the command chooser.
        if(choice && choice->isEnabled())choice->trigger();
    }
};
#endif // Q_OS_ANDROID
