// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QApplication>
#include <QAction>
#include <QFrame>
#include <QKeyEvent>
#include <QPointer>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QTouchEvent>
#include <QWindow>
#include <functional>

class MobileTransientPanel final : public QFrame
{
public:
    explicit MobileTransientPanel(QWidget *host) : QFrame(host)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_NoMousePropagation);
        qApp->installEventFilter(this);
    }

    std::function<void()> onRefresh;

    void requestRefresh()
    {
        if(!onRefresh || m_refreshPending)return;
        m_refreshPending=true;
        QTimer::singleShot(0,this,[this]() {
            m_refreshPending=false;
            if(isVisible() && onRefresh)onRefresh();
        });
    }

    void anchorTo(QWidget *anchor,const QSize &preferred,bool allowSide=false)
    {
        m_anchor=anchor;
        m_preferred=preferred;
        m_allowSide=allowSide;
    }

    void setOpener(QWidget *widget,QAction *action=nullptr)
    {
        m_opener=widget;
        m_action=action;
    }

    static bool closeForAnchor(QWidget *host,QWidget *anchor)
    {
        if(!host || !anchor)return false;
        for(auto *child:host->children()) {
            auto *panel=dynamic_cast<MobileTransientPanel*>(child);
            if(panel && panel->isVisible() && panel->m_anchor==anchor) {
                panel->close();
                return true;
            }
        }
        return false;
    }

    void present()
    {
        position();
        show();
        raise();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(!isVisible())return false;
        auto *target=qobject_cast<QWidget*>(watched);
        if(target==parentWidget() && event->type()==QEvent::Resize) {
            position();
            requestRefresh();
        }

        const auto type=event->type();
        const bool sameWindow=target?target->window()==window():watched==window()->windowHandle();
        if(sameWindow && (type==QEvent::KeyPress || type==QEvent::ShortcutOverride)) {
            const int key=static_cast<QKeyEvent*>(event)->key();
            if(key==Qt::Key_Escape || key==Qt::Key_Back) {
                if(type==QEvent::KeyPress)close();
                event->accept();
                return true;
            }
        }
        if(!target || target==this || isAncestorOf(target))return false;
        if(type==QEvent::Hide && target==parentWidget()) {
            close();
            return false;
        }
        if(type==QEvent::Show && target->isWindow() && target->parentWidget() &&
           target->parentWidget()->window()==window()) {
            close();
        } else if(sameWindow && (type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick ||
                               type==QEvent::TabletPress || type==QEvent::TouchBegin)) {
            // Native touch or pen may reach a parent before Qt synthesizes
            // the button's click. Keep the opener exempt on that delivery too.
            if(type==QEvent::TouchBegin) {
                const auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                const auto &points=touch->points();
                if(!points.isEmpty())
                    if(auto *hit=QApplication::widgetAt(points.first().globalPosition().toPoint()))target=hit;
#else
                const auto &points=touch->touchPoints();
                if(!points.isEmpty())
                    if(auto *hit=QApplication::widgetAt(points.first().screenPos().toPoint()))target=hit;
#endif
            } else if(type==QEvent::TabletPress) {
                const auto *tablet=static_cast<QTabletEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                const auto global=tablet->globalPosition().toPoint();
#else
                const auto global=tablet->globalPos();
#endif
                if(auto *hit=QApplication::widgetAt(global))target=hit;
            }
            if(target==this || isAncestorOf(target))return false;
            for(auto *widget=target;widget;widget=widget->parentWidget()) {
                if(widget==m_anchor || widget==m_opener)return false;
                const auto *button=qobject_cast<QToolButton*>(widget);
                if(m_action && button && button->defaultAction()==m_action)return false;
            }
            close();
        }
        return false;
    }

private:
    bool m_refreshPending=false;
    bool m_allowSide=false;
    QPointer<QWidget> m_anchor,m_opener;
    QPointer<QAction> m_action;
    QSize m_preferred;

    void position()
    {
        const QSize bounds=parentWidget()->size();
        const QSize preferred=m_preferred.isValid()?m_preferred:
            QSize(450,qMin(440,qMax(1,bounds.height()*3/5)));
        const QSize size(qMin(preferred.width(),qMax(1,bounds.width()-16)),
                         qMin(preferred.height(),qMax(1,bounds.height()-16)));
        QPoint origin((bounds.width()-size.width())/2,(bounds.height()-size.height())/2);
        if(m_anchor) {
            const QPoint anchor=parentWidget()->mapFromGlobal(m_anchor->mapToGlobal(QPoint()));
            const int below=anchor.y()+m_anchor->height()+4;
            const int above=anchor.y()-size.height()-4;
            const bool useAbove=below+size.height()>bounds.height()-8 &&
                                anchor.y()>bounds.height()-below;
            origin=QPoint(anchor.x()+(m_anchor->width()-size.width())/2,useAbove?above:below);
            if(m_allowSide) {
                const int right=anchor.x()+m_anchor->width()+4;
                const int left=anchor.x()-size.width()-4;
                if(anchor.x()<56 && right+size.width()<=bounds.width())
                    origin=QPoint(right,anchor.y()+(m_anchor->height()-size.height())/2);
                else if(anchor.x()+m_anchor->width()>bounds.width()-56 && left>=0)
                    origin=QPoint(left,anchor.y()+(m_anchor->height()-size.height())/2);
            }
            origin.setX(qBound(0,origin.x(),qMax(0,bounds.width()-size.width())));
            origin.setY(qBound(0,origin.y(),qMax(0,bounds.height()-size.height())));
        }
        setGeometry(QRect(origin,size));
    }
};
