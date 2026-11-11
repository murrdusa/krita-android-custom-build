/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QListView>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QAbstractButton>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScroller>
#include <QTimer>
#include <QTouchEvent>

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
            const bool scrollControl=button || (combo && !combo->isEditable());
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

// Item views depend on native pressed-index bookkeeping. Let real mouse
// pairs reach Qt unchanged, and own raw touch so QScroller's recognizer
// cannot consume the only input from which a stationary tap is generated.
class ItemViewPointer final : public QObject
{
public:
    explicit ItemViewPointer(QAbstractItemView *view)
        : QObject(view->viewport()),m_view(view),m_viewport(view->viewport()),
          m_scroller(QScroller::scroller(m_viewport))
    {
        setObjectName(QStringLiteral("_mobileItemViewPointer"));
        m_clock.start();
        m_viewport->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *,QEvent *event) override
    {
        if(event==m_replay)return false;
        switch(event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            return touch(static_cast<QTouchEvent*>(event));
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
            return mouse(static_cast<QMouseEvent*>(event));
        case QEvent::Hide:
            if(m_input==Touch) {
                m_input=None;
                m_scroller->stop();
            }
            Q_FALLTHROUGH();
        case QEvent::UngrabMouse:
            if(m_input==Mouse) {
                m_input=None;
                m_scroller->stop();
                if(!m_dragging && !m_stopTap)cancelNativePress();
            }
            return false;
        default:
            return false;
        }
    }

private:
    enum Input { None, Mouse, Touch };

    void begin(Input input,const QPoint &pos)
    {
        m_input=input;
        m_start=m_last=pos;
        m_pressedAt=m_clock.elapsed()+1;
        m_dragging=false;
        m_cancelled=false;
        m_stopTap=m_scroller->state()==QScroller::Scrolling;
        if(m_stopTap)m_scroller->stop();
    }

    void move(const QPoint &pos)
    {
        if(pos==m_last)return;
        m_last=pos;
        const qint64 now=m_clock.elapsed()+1;
        if(!m_dragging) {
            if((pos-m_start).manhattanLength()<QApplication::startDragDistance())return;
            m_dragging=true;
            if(m_input==Mouse && !m_stopTap)cancelNativePress();
            m_scroller->handleInput(QScroller::InputPress,m_start,m_pressedAt);
        }
        m_scroller->handleInput(QScroller::InputMove,pos,now);
    }

    bool finish(const QPoint &pos)
    {
        if(!m_cancelled)move(pos);
        if(m_dragging)
            m_scroller->handleInput(QScroller::InputRelease,pos,m_clock.elapsed()+1);
        m_input=None;
        return !m_dragging && !m_cancelled && !m_stopTap;
    }

    void sendMouse(QEvent::Type type,const QPoint &pos)
    {
        const auto buttons=type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton;
        QMouseEvent replay(type,pos,m_viewport->mapToGlobal(pos),
                           Qt::LeftButton,buttons,Qt::NoModifier);
        QPointer<ItemViewPointer> alive(this);
        m_replay=&replay;
        QApplication::sendEvent(m_viewport,&replay);
        if(alive)m_replay=nullptr;
    }

    void cancelNativePress()
    {
        // Clear the view's pressed state with a release outside its index
        // area. QAbstractItemView cannot emit clicked for this release.
        sendMouse(QEvent::MouseButtonRelease,QPoint(-QWIDGETSIZE_MAX,-QWIDGETSIZE_MAX));
    }

    bool touch(QTouchEvent *event)
    {
        event->accept(); // Qt must not generate a second mouse tap for this stream.
        if(event->type()==QEvent::TouchCancel) {
            if(m_input==Touch) {
                m_lastTouchEnd=m_clock.elapsed();
                m_lastTouchStart=m_start;
                m_lastTouchPos=m_last;
                m_input=None;
                m_scroller->stop();
            }
            return true;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const auto &points=event->points();
        const QPoint pos=points.isEmpty()?m_last:points.first().position().toPoint();
#else
        const auto &points=event->touchPoints();
        const QPoint pos=points.isEmpty()?m_last:points.first().pos().toPoint();
#endif
        if(event->type()==QEvent::TouchBegin) {
            const bool cancelMouse=m_input==Mouse && !m_dragging && !m_stopTap;
            begin(Touch,pos);
            if(cancelMouse)cancelNativePress();
            m_touchIndex=m_view->indexAt(pos);
            m_touchOnItem=m_touchIndex.isValid();
        }
        if(m_input!=Touch)return true;
        const bool ending=event->type()==QEvent::TouchEnd;
        if(points.size()!=1 && !(ending && points.isEmpty()) && !m_cancelled) {
            m_cancelled=true;
            m_scroller->stop();
        }
        if(ending) {
            m_lastTouchEnd=m_clock.elapsed();
            m_lastTouchPos=pos;
            m_lastTouchStart=m_start;
            const bool tap=finish(pos);
            const bool sameItem=!m_touchOnItem || (m_touchIndex.isValid() &&
                m_touchIndex==m_view->indexAt(m_start));
            m_touchIndex=QModelIndex();
            if(tap && sameItem && m_viewport->rect().contains(pos)) {
                QPointer<ItemViewPointer> alive(this);
                if(m_view->focusPolicy() & Qt::ClickFocus)
                    m_view->setFocus(Qt::MouseFocusReason);
                if(!alive)return true;
                sendMouse(QEvent::MouseButtonPress,m_start);
                if(alive)sendMouse(QEvent::MouseButtonRelease,pos);
            }
        } else if(event->type()==QEvent::TouchUpdate && !m_cancelled) {
            move(pos);
        }
        return true;
    }

    bool mouse(QMouseEvent *event)
    {
        const auto type=event->type();
        if(type!=QEvent::MouseMove && event->button()!=Qt::LeftButton)return false;
        const QPoint pos=event->pos();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick) {
            // Some Android paths deliver an additional synthesized mouse
            // sequence around a touch already handled by this viewport.
            m_ignoreMouse=m_input==Touch ||
                (event->source()!=Qt::MouseEventNotSynthesized &&
                 m_clock.elapsed()-m_lastTouchEnd<200 &&
                 qMin((pos-m_lastTouchPos).manhattanLength(),
                      (pos-m_lastTouchStart).manhattanLength())<QApplication::startDragDistance());
            if(m_ignoreMouse)return true;
            begin(Mouse,pos);
            return m_stopTap;
        }
        if(m_ignoreMouse || m_input==Touch) {
            if(type==QEvent::MouseButtonRelease)m_ignoreMouse=false;
            return true;
        }
        if(m_input!=Mouse)return false;
        if(type==QEvent::MouseMove) {
            if(!(event->buttons() & Qt::LeftButton))return false;
            move(pos);
            return m_dragging || m_stopTap;
        }
        return !finish(pos);
    }

    QAbstractItemView *m_view;
    QWidget *m_viewport;
    QScroller *m_scroller;
    QEvent *m_replay=nullptr;
    QElapsedTimer m_clock;
    Input m_input=None;
    QPoint m_start,m_last,m_lastTouchPos,m_lastTouchStart;
    QPersistentModelIndex m_touchIndex;
    qint64 m_pressedAt=0,m_lastTouchEnd=-200;
    bool m_dragging=false,m_cancelled=false,m_stopTap=false,m_ignoreMouse=false;
    bool m_touchOnItem=false;
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
    if(auto *view=qobject_cast<QAbstractItemView*>(area)) {
        if(!viewport->findChild<QObject*>(QStringLiteral("_mobileItemViewPointer"),
                                         Qt::FindDirectChildrenOnly))
            new ItemViewPointer(view);
    } else {
        QScroller::grabGesture(viewport,QScroller::TouchGesture);
    }
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
