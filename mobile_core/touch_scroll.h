/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QListView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QToolButton>
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

class TapDrag
{
public:
    void press(QWidget *widget,const QPoint &global)
    {
        m_start=global;
        m_dragged=false;
        m_area=nullptr;
        for(auto *parent=widget->parentWidget();parent;parent=parent->parentWidget()) {
            if((m_area=qobject_cast<QAbstractScrollArea*>(parent)))break;
        }
        if(m_area)m_scrollStart=m_area->verticalScrollBar()->value();
    }

    bool move(const QPoint &global)
    {
        const QPoint delta=global-m_start;
        m_dragged=m_dragged || delta.manhattanLength()>=QApplication::startDragDistance();
        if(m_dragged && m_area &&
           QScroller::scroller(m_area->viewport())->state()==QScroller::Inactive)
            m_area->verticalScrollBar()->setValue(m_scrollStart-delta.y());
        return m_dragged;
    }

private:
    QAbstractScrollArea *m_area=nullptr;
    QPoint m_start;
    int m_scrollStart=0;
    bool m_dragged=false;
};

class ToolTile final : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton)m_drag.press(this,event->globalPos());
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if((event->buttons() & Qt::LeftButton) && m_drag.move(event->globalPos())) {
            setDown(false);
            event->accept();
            return;
        }
        QToolButton::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton && m_drag.move(event->globalPos()))
            setDown(false);
        QToolButton::mouseReleaseEvent(event);
    }

private:
    TapDrag m_drag;
};

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

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()!=Qt::LeftButton || isEditable()) {
            QComboBox::mousePressEvent(event);
            return;
        }
        m_pressed=true;
        m_drag.press(this,event->globalPos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(m_pressed && (event->buttons() & Qt::LeftButton)) {
            m_drag.move(event->globalPos());
            event->accept();
            return;
        }
        QComboBox::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(m_pressed && event->button()==Qt::LeftButton) {
            m_pressed=false;
            if(!m_drag.move(event->globalPos()) && rect().contains(event->pos()))
                showPopup();
            event->accept();
            return;
        }
        QComboBox::mouseReleaseEvent(event);
    }

private:
    MobileTouchScroll::TapDrag m_drag;
    bool m_pressed=false;

};
