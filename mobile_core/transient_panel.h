// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
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

    void anchorTo(QWidget *anchor,const QSize &preferred)
    {
        m_anchor=anchor;
        m_preferred=preferred;
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
        if(type==QEvent::Show && target->isWindow() && target->parentWidget() &&
           target->parentWidget()->window()==window()) {
            close();
        } else if(sameWindow && (type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick ||
                               type==QEvent::TabletPress || type==QEvent::TouchBegin)) {
            // Keep the original event available to the canvas or next control.
            close();
        }
        return false;
    }

private:
    bool m_refreshPending=false;
    QPointer<QWidget> m_anchor;
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
            origin.setX(qBound(0,origin.x(),qMax(0,bounds.width()-size.width())));
            origin.setY(qBound(0,origin.y(),qMax(0,bounds.height()-size.height())));
        }
        setGeometry(QRect(origin,size));
    }
};
