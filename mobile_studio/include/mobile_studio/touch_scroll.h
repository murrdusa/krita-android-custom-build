/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QListView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QStylePainter>
#include <QAbstractButton>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
#include <QScroller>
#include <QTimer>
#include <QTouchEvent>
#include <QTabletEvent>

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
        if(type==QEvent::TabletPress || type==QEvent::TabletMove || type==QEvent::TabletRelease)
            return tabletGap(object,static_cast<QTabletEvent*>(event));
        if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseButtonDblClick &&
           type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
        auto *mouse=static_cast<QMouseEvent*>(event);
        if(m_tabletArea && mouse->source()!=Qt::MouseEventNotSynthesized)return true;
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick) {
            if(mouse->button()!=Qt::LeftButton)return false;
            m_target.clear();
            auto *widget=qobject_cast<QWidget*>(object);
            if(!widget || !widget->rect().contains(mouse->pos()))return false;
            // Dedicated toolbar drag buttons own their complete
            // touch/mouse transaction (tap, inline slider and color
            // sampler). Never delay or replay their events here.
            if(widget->property("_mobileToolbarDragControl").toBool())
                return false;
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
            const bool gap=area && area->property("_mobileScrollGaps").toBool() &&
                !qobject_cast<QAbstractSlider*>(widget) && !qobject_cast<QLineEdit*>(widget);
            if(!mobileCombo && (!area || (!scrollControl && !gap)))return false;
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
        const bool gap=m_area && m_area->property("_mobileScrollGaps").toBool() &&
            !qobject_cast<QAbstractButton*>(target.data()) && !qobject_cast<QComboBox*>(target.data());
        m_area.clear();
        if(gap || m_dragging || !target->isVisible() || !target->isEnabled() ||
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
    bool tabletGap(QObject *object,QTabletEvent *event)
    {
        const auto type=event->type();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const QPoint global=event->globalPosition().toPoint();
#else
        const QPoint global=event->globalPos();
#endif
        if(type==QEvent::TabletPress) {
            m_tabletArea.clear();
            auto *widget=QApplication::widgetAt(global);
            if(!widget)widget=qobject_cast<QWidget*>(object);
            for(auto *parent=widget;parent;parent=parent->parentWidget()) {
                if(qobject_cast<QAbstractButton*>(parent) || qobject_cast<QAbstractSlider*>(parent) ||
                   qobject_cast<QLineEdit*>(parent) || qobject_cast<QComboBox*>(parent))return false;
                auto *area=qobject_cast<QAbstractScrollArea*>(parent);
                if(!area)continue;
                if(!area->property("_mobileScrollGaps").toBool())return false;
                m_tabletArea=area;
                m_tabletStart=global;
                m_tabletOffset=area->verticalScrollBar()->value();
                break;
            }
        }
        if(!m_tabletArea)return false;
        m_tabletArea->verticalScrollBar()->setValue(m_tabletOffset+m_tabletStart.y()-global.y());
        if(type==QEvent::TabletRelease)m_tabletArea.clear();
        event->accept();
        return true;
    }

    QPointer<QAbstractScrollArea> m_tabletArea;
    QPoint m_tabletStart;
    int m_tabletOffset=0;

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
        m_holdTimer.setSingleShot(true);
        connect(&m_holdTimer,&QTimer::timeout,this,[this]() {
            if(!m_holdIndex.isValid() || m_holdIndex!=m_view->indexAt(m_last))return;
            m_held=true;
            if(m_input==Mouse)cancelNativePress();
            m_holdAction(m_holdIndex);
        });
        m_viewport->installEventFilter(this);
    }

    void setHoldAction(std::function<void(const QModelIndex &)> action)
    {
        m_holdTimer.stop();
        m_holdAction=std::move(action);
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
            m_holdTimer.stop();
            if(m_input==Touch) {
                m_input=None;
                m_scroller->stop();
            }
            Q_FALLTHROUGH();
        case QEvent::UngrabMouse:
            m_holdTimer.stop();
            if(m_input==Mouse) {
                m_input=None;
                m_scroller->stop();
                if(!m_dragging && !m_stopTap && !m_held)cancelNativePress();
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
        m_held=false;
        m_holdTimer.stop();
        if(m_holdAction && !m_stopTap) {
            m_holdIndex=m_view->indexAt(pos);
            if(m_holdIndex.isValid())m_holdTimer.start(480);
        }
    }

    void move(const QPoint &pos)
    {
        if(m_held || pos==m_last)return;
        m_last=pos;
        const qint64 now=m_clock.elapsed()+1;
        if(!m_dragging) {
            if((pos-m_start).manhattanLength()<QApplication::startDragDistance())return;
            m_dragging=true;
            m_holdTimer.stop();
            if(m_input==Mouse && !m_stopTap)cancelNativePress();
            m_scroller->handleInput(QScroller::InputPress,m_start,m_pressedAt);
        }
        m_scroller->handleInput(QScroller::InputMove,pos,now);
    }

    bool finish(const QPoint &pos)
    {
        m_holdTimer.stop();
        if(!m_cancelled)move(pos);
        if(m_dragging)
            m_scroller->handleInput(QScroller::InputRelease,pos,m_clock.elapsed()+1);
        m_input=None;
        return !m_dragging && !m_cancelled && !m_stopTap && !m_held;
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
            m_holdTimer.stop();
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
            const bool cancelMouse=m_input==Mouse && !m_dragging && !m_stopTap && !m_held;
            begin(Touch,pos);
            if(cancelMouse)cancelNativePress();
            m_touchIndex=m_view->indexAt(pos);
            m_touchOnItem=m_touchIndex.isValid();
        }
        if(m_input!=Touch)return true;
        const bool ending=event->type()==QEvent::TouchEnd;
        if(points.size()!=1 && !(ending && points.isEmpty()) && !m_cancelled) {
            m_cancelled=true;
            m_holdTimer.stop();
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
            return m_dragging || m_stopTap || m_held;
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
    QTimer m_holdTimer;
    QPersistentModelIndex m_holdIndex;
    std::function<void(const QModelIndex &)> m_holdAction;
    bool m_held=false;
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

inline void styleMenuDialog(QDialog &dialog,const QSize &preferred)
{
    dialog.setWindowFlag(Qt::FramelessWindowHint,true);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog{background:#25262b;color:#f1f2f5;}"
        "QScrollArea,QListWidget,QListView{background:#25262b;color:#f1f2f5;border:0;}"
        "QLabel,QCheckBox{color:#f1f2f5;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit,QPushButton{background:#30323b;color:#f1f2f5;"
        "border:1px solid #44464f;border-radius:7px;padding:4px 8px;min-height:36px;}"
        "QSpinBox QLineEdit,QDoubleSpinBox QLineEdit{border:0;padding:0;min-height:0;}"
        "QPushButton:pressed,QListView::item:selected{background:#394459;}"
        "QPushButton:disabled{color:#989da9;}"
        "QTabBar::tab{background:#30323b;color:#bfc3ce;padding:0 10px;min-height:40px;}"
        "QTabBar::tab:selected{background:#394459;color:#f1f2f5;}"));
    if(auto *parent=dialog.parentWidget()) {
        const QSize room=parent->window()->size()-QSize(20,20);
        dialog.resize(preferred.boundedTo(room.expandedTo(QSize(1,1))));
    }
}

inline void configureList(QAbstractItemView *view,
                          std::function<void(const QModelIndex &)> hold={})
{
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    configure(view);
    if(hold) {
        auto *filter=view->viewport()->findChild<QObject*>(
            QStringLiteral("_mobileItemViewPointer"),Qt::FindDirectChildrenOnly);
        static_cast<ItemViewPointer*>(filter)->setHoldAction(std::move(hold));
    }
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

    void setAnchoredPopup(bool anchored) {m_anchored=anchored;}

    void showPopup() override
    {
        if(count()==0)return;
        QDialog choices(this);
        MobileTouchScroll::styleMenuDialog(choices,QSize(qMax(width(),240),360));
        if(m_anchored)choices.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
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
        if(m_anchored)close->hide();
        else layout->addWidget(close);
        connect(close,&QPushButton::clicked,&choices,&QDialog::reject);
        int selected=-1;
        connect(list,&QListView::clicked,&choices,[&](const QModelIndex &index) {
            if(!index.isValid() ||
               !(index.flags() & Qt::ItemIsEnabled) ||
               !(index.flags() & Qt::ItemIsSelectable))return;
            selected=index.row();
            choices.accept();
        });
        const QSize room=window()->size()-QSize(16,16);
        choices.resize(QSize(qMax(width(),240),66+qMin(count(),8)*42)
                       .boundedTo(room.expandedTo(QSize(1,1))));
        if(m_anchored) {
            const QRect bounds(window()->mapToGlobal(QPoint()),window()->size());
            const QPoint top=mapToGlobal(QPoint());
            const int below=top.y()+height();
            const int above=top.y()-choices.height();
            const int y=below+choices.height()<=bounds.bottom()+1?below:above;
            choices.move(qBound(bounds.left(),top.x(),bounds.right()+1-choices.width()),
                         qBound(bounds.top(),y,bounds.bottom()+1-choices.height()));
        }
        if(choices.exec()==QDialog::Accepted && selected>=0 && selected<count()) {
            // A user selection on a regular QComboBox emits activated()
            // even if the choice was already current. Setting the index
            // alone only emits currentIndexChanged().
            setCurrentIndex(selected);
            Q_EMIT activated(selected);
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if(isEditable()) {
            QComboBox::paintEvent(event);
            return;
        }
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        painter.drawComplexControl(QStyle::CC_ComboBox,option);
        const QRect field=style()->subControlRect(QStyle::CC_ComboBox,&option,
                                                   QStyle::SC_ComboBoxEditField,this);
        option.currentText=fontMetrics().elidedText(currentText(),Qt::ElideRight,
                                                     qMax(0,field.width()-4));
        painter.drawControl(QStyle::CE_ComboBoxLabel,option);
    }

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
    bool m_tapPending=false,m_anchored=false;
};

class MobileTouchSlider final : public QSlider
{
public:
    explicit MobileTouchSlider(QWidget *parent=nullptr) : QSlider(Qt::Horizontal,parent)
    {
        setAttribute(Qt::WA_AcceptTouchEvents);
        setMinimumHeight(44);
        setTrackColors(QStringLiteral("#555965"),QStringLiteral("#6ca9c9"));
    }

    void setTrackColors(const QString &track,const QString &fill)
    {
        setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal{height:8px;background:%1;border-radius:4px;}"
            "QSlider::sub-page:horizontal{background:%2;border-radius:4px;}"
            "QSlider::handle:horizontal{width:28px;margin:-11px 0;"
            "background:#f1f2f5;border:1px solid #343740;border-radius:14px;}"
            "QSlider::handle:horizontal:disabled{background:#82848d;}").arg(track,fill));
    }

protected:
    bool event(QEvent *event) override
    {
        const auto type=event->type();
        if(type!=QEvent::TouchBegin && type!=QEvent::TouchUpdate &&
           type!=QEvent::TouchEnd && type!=QEvent::TouchCancel)
            return QSlider::event(event);
        const auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const auto &points=touch->points();
        if(!points.isEmpty())m_touchPosition=points.first().position().toPoint();
#else
        const auto &points=touch->touchPoints();
        if(!points.isEmpty())m_touchPosition=points.first().pos().toPoint();
#endif
        if(type==QEvent::TouchBegin && points.size()==1) {
            QMouseEvent press(QEvent::MouseButtonPress,m_touchPosition,mapToGlobal(m_touchPosition),
                              Qt::LeftButton,Qt::LeftButton,touch->modifiers());
            mousePressEvent(&press);
        } else if(isSliderDown()) {
            if(points.size()<=1 && type!=QEvent::TouchCancel) {
                QMouseEvent move(QEvent::MouseMove,m_touchPosition,mapToGlobal(m_touchPosition),
                                 Qt::NoButton,Qt::LeftButton,touch->modifiers());
                QSlider::mouseMoveEvent(&move);
            }
            if(type!=QEvent::TouchUpdate || points.size()>1) {
                QMouseEvent release(QEvent::MouseButtonRelease,m_touchPosition,mapToGlobal(m_touchPosition),
                                    Qt::LeftButton,Qt::NoButton,touch->modifiers());
                QSlider::mouseReleaseEvent(&release);
            }
        }
        event->accept();
        return true;
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const auto handleRect=[&]() {
            return style()->subControlRect(QStyle::CC_Slider,&option,
                                          QStyle::SC_SliderHandle,this);
        };
        QRect handle=handleRect();
        if(event->button()!=Qt::LeftButton || handle.contains(event->pos())) {
            QSlider::mousePressEvent(event);
            return;
        }
        const QRect groove=style()->subControlRect(QStyle::CC_Slider,&option,
                                                  QStyle::SC_SliderGroove,this);
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(),maximum(),
            event->pos().x()-groove.left()-handle.width()/2,
            qMax(1,groove.width()-handle.width()),option.upsideDown));
        initStyleOption(&option);
        const QPoint center=handleRect().center();
        QMouseEvent press(QEvent::MouseButtonPress,center,mapToGlobal(center),
                          event->button(),event->buttons(),event->modifiers());
        QSlider::mousePressEvent(&press);
        event->accept();
    }

private:
    QPoint m_touchPosition;
};
