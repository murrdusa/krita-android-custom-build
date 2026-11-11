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
#include <QAbstractButton>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScroller>
#include <QTimer>

namespace MobileTouchScroll
{
// Keep native touch inertia. Android also delivers mouse events from
// touch and pen, including when the starting widget is a child control.
class MenuPointerFilter final : public QObject
{
public:
    using QObject::QObject;

    static void install()
    {
        static QPointer<MenuPointerFilter> filter;
        if(!filter) {
            filter=new MenuPointerFilter(qApp);
            qApp->installEventFilter(filter);
        }
    }

protected:
    bool eventFilter(QObject *object,QEvent *event) override
    {
        if(event==m_replay)return false;
        const auto type=event->type();
        if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseButtonDblClick &&
           type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
        auto *mouse=static_cast<QMouseEvent*>(event);
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick) {
            if(mouse->button()!=Qt::LeftButton)return false;
            m_target.clear();
            auto *widget=qobject_cast<QWidget*>(object);
            if(!widget || !widget->rect().contains(mouse->pos()))return false;
            QAbstractScrollArea *area=nullptr;
            for(auto *parent=widget->parentWidget();parent;parent=parent->parentWidget()) {
                area=qobject_cast<QAbstractScrollArea*>(parent);
                if(area && area->property("_mobileMenuScroll").toBool())break;
                area=nullptr;
            }
            const auto *combo=qobject_cast<QComboBox*>(widget);
            const bool mobileCombo=combo && !combo->isEditable() &&
                widget->property("_mobileTapCombo").toBool();
            const bool button=qobject_cast<QAbstractButton*>(widget);
            // For item views, a swipe starting on an item must scroll
            // rather than activate it. Replay *both* halves of a stationary
            // tap into the viewport, so Qt's clicked/itemClicked signals
            // see one complete press/release on the same target.
            const bool itemView=area && area->viewport()==widget &&
                qobject_cast<QAbstractItemView*>(area);
            const bool scrollControl=button || itemView ||
                (combo && !combo->isEditable());
            if(!mobileCombo && (!area || !scrollControl))return false;
            m_target=widget;
            m_area=area;
            m_start=mouse->globalPos();
            m_pressType=type;
            m_dragging=false;
            if(area) {
                m_scrollStart=QPoint(area->horizontalScrollBar()->value(),
                                     area->verticalScrollBar()->value());
            }
            return true;
        }
        if(!m_target)return false;
        if(type==QEvent::MouseMove && !(mouse->buttons() & Qt::LeftButton))return false;
        if(type==QEvent::MouseButtonRelease && mouse->button()!=Qt::LeftButton)return false;
        const QPoint delta=mouse->globalPos()-m_start;
        m_dragging=m_dragging || delta.manhattanLength()>=QApplication::startDragDistance();
        if(m_dragging && m_area && m_area->isVisible() &&
           QScroller::scroller(m_area->viewport())->state()==QScroller::Inactive) {
            m_area->horizontalScrollBar()->setValue(m_scrollStart.x()-delta.x());
            m_area->verticalScrollBar()->setValue(m_scrollStart.y()-delta.y());
        }
        if(type!=QEvent::MouseButtonRelease)return true;
        const auto target=m_target;
        m_target.clear();
        m_area.clear();
        if(m_dragging || !target->isVisible() || !target->isEnabled() ||
           !target->rect().contains(target->mapFromGlobal(mouse->globalPos())))return true;
        QMouseEvent press(m_pressType,target->mapFromGlobal(m_start),m_start,
                          Qt::LeftButton,Qt::LeftButton,mouse->modifiers());
        m_replay=&press;
        QApplication::sendEvent(target,&press);
        m_replay=nullptr;
        if(!target)return true;
        // Do not forward the original release for the same viewport:
        // depending on Android's touch-to-mouse routing its delivery
        // context differs from the synthetic press. Explicitly replay
        // the matching release, and consume the original once.
        QMouseEvent release(type,target->mapFromGlobal(mouse->globalPos()),mouse->globalPos(),
                            Qt::LeftButton,Qt::NoButton,mouse->modifiers());
        m_replay=&release;
        QApplication::sendEvent(target,&release);
        m_replay=nullptr;
        return true;
    }

private:
    QPointer<QWidget> m_target;
    QPointer<QAbstractScrollArea> m_area;
    QEvent *m_replay=nullptr;
    QPoint m_start,m_scrollStart;
    QEvent::Type m_pressType=QEvent::MouseButtonPress;
    bool m_dragging=false;
};

inline void configure(QAbstractScrollArea *area)
{
    MenuPointerFilter::install();
    area->setProperty("_mobileMenuScroll",true);
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
    explicit MobileTouchComboBox(QWidget *parent=nullptr) : QComboBox(parent)
    {
        setProperty("_mobileTapCombo",true);
        MobileTouchScroll::MenuPointerFilter::install();
    }

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
        if(event->button()==Qt::LeftButton && !isEditable()) {
            m_tapPending=true;
            event->accept();
        } else {
            QComboBox::mousePressEvent(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton && !isEditable()) {
            const bool open=m_tapPending && rect().contains(event->pos());
            m_tapPending=false;
            event->accept();
            if(open)showPopup();
        } else {
            QComboBox::mouseReleaseEvent(event);
        }
    }

private:
    bool m_tapPending=false;
};
