/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QDialog>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScroller>
#include <QTimer>

namespace MobileTouchScroll
{
inline void configure(QAbstractScrollArea *area)
{
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *viewport=area->viewport();
    viewport->setAttribute(Qt::WA_AcceptTouchEvents,true);
    QScroller::ungrabGesture(viewport);
    QScroller::grabGesture(viewport,QScroller::TouchGesture);
}

inline void configureList(QAbstractItemView *view)
{
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    configure(view);
}

}

class MobileTouchComboBox final : public QComboBox
{
public:
    using QComboBox::QComboBox;

    void showPopup() override
    {
        if(count()==0)return;
        QDialog choices(this);
        auto *layout=new QVBoxLayout(&choices);
        auto *list=new QListView(&choices);
        list->setStyleSheet(QStringLiteral("QListView::item{min-height:42px;}"));
        list->setModel(model());
        list->setRootIndex(rootModelIndex());
        list->setModelColumn(modelColumn());
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        const QModelIndex initial=model()->index(currentIndex(),modelColumn(),rootModelIndex());
        list->setCurrentIndex(initial);
        MobileTouchScroll::configureList(list);
        // QComboBox can contain long lists: show the active choice when
        // opened without sacrificing native finger kinetic scrolling.
        QTimer::singleShot(0,&choices,[list,initial]() {
            if(initial.isValid())
                list->scrollTo(initial,QAbstractItemView::PositionAtCenter);
        });
        layout->addWidget(list,1);
        auto *close=new QPushButton(tr("Cancel"),&choices);
        close->setMinimumHeight(42);
        layout->addWidget(close);
        connect(close,&QPushButton::clicked,&choices,&QDialog::reject);
        int selected=-1;
        connect(list,&QListView::clicked,&choices,[&](const QModelIndex &index) {
            if(!index.isValid() ||
               !(index.flags() & Qt::ItemIsEnabled) ||
               !(index.flags() & Qt::ItemIsSelectable))return;
            selected=index.row();
            choices.accept();
        });
        const QSize parentSize=window()->size();
        choices.resize(qMin(qMax(width(),220),parentSize.width()),
                       qMin(66+qMin(count(),7)*42,parentSize.height()));
        if(choices.exec()==QDialog::Accepted && selected>=0 && selected<count()) {
            // A user selection on a regular QComboBox emits activated()
            // even if the choice was already current. Setting the index
            // alone only emits currentIndexChanged().
            setCurrentIndex(selected);
            Q_EMIT activated(selected);
        }
    }
};
