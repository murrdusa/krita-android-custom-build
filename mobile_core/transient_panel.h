// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
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

    void position()
    {
        const QSize bounds=parentWidget()->size();
        const QSize size(qMin(450,qMax(1,bounds.width()-28)),
                         qMin(440,qMax(1,bounds.height()*3/5)));
        setGeometry(QRect(QPoint((bounds.width()-size.width())/2,
                                 (bounds.height()-size.height())/2),size));
    }
};
