/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)

#include <QApplication>
#include <QMouseEvent>
#include <QSettings>
#include <QToolButton>
#include <QTouchEvent>
#include <functional>

// Use the *toolbar's* axis, not the screen's horizontal axis. A
// custom toolbar moved to the left/right edge therefore automatically
// changes its slider direction. The perpendicular Color swipe remains
// a canvas-color sampler regardless of that orientation.
namespace MobileToolbarDrag
{
inline QString preference(const char *parameter)
{
    return QStringLiteral("MobileStudio/toolbarDrag/")+
        QString::fromLatin1(parameter);
}
inline QString assignment(const char *parameter)
{
    return QSettings().value(preference(parameter),
        QString::fromLatin1(parameter)==QStringLiteral("opacity")?
            QStringLiteral("color"):QStringLiteral("tool-settings")).toString();
}
inline bool usesButton(const QString &id)
{
    return id==assignment("opacity") || id==assignment("size") ||
        (id==QStringLiteral("color") &&
         QSettings().value(preference("sample"),true).toBool());
}
inline qreal along(const QPoint &delta,bool vertical)
{
    return vertical?delta.y():delta.x();
}
inline qreal away(const QPoint &delta,bool vertical)
{
    return vertical?delta.x():delta.y();
}
}

class MobileToolbarGestureButton final : public QToolButton
{
public:
    using InlineAdjustment=std::function<void(const QString &,qreal)>;
    using ColorSample=std::function<void(const QPoint &)>;

    MobileToolbarGestureButton(QWidget *parent,const QString &command,
                               bool vertical,
                               InlineAdjustment adjust,ColorSample sample)
        : QToolButton(parent),m_command(command),m_vertical(vertical),
          m_adjust(std::move(adjust)),m_sample(std::move(sample))
    {
        setProperty("_mobileToolbarDragControl",true);
        setAttribute(Qt::WA_AcceptTouchEvents,true);
    }

protected:
    bool event(QEvent *event) override
    {
        if(!MobileToolbarDrag::usesButton(m_command))
            return QToolButton::event(event);
        switch(event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            const auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            const auto points=touch->points();
            const QPoint pos=points.isEmpty()?m_last:
                points.first().globalPosition().toPoint();
#else
            const auto points=touch->touchPoints();
            const QPoint pos=points.isEmpty()?m_last:
                points.first().screenPos().toPoint();
#endif
            if(event->type()==QEvent::TouchBegin)begin(pos);
            else if(event->type()==QEvent::TouchUpdate)update(pos);
            else finish(pos,event->type()==QEvent::TouchCancel);
            event->accept();
            return true;
        }
        default:break;
        }
        return QToolButton::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if(!MobileToolbarDrag::usesButton(m_command) ||
           event->button()!=Qt::LeftButton) {
            QToolButton::mousePressEvent(event);
            return;
        }
        begin(event->globalPos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(!m_active) {
            QToolButton::mouseMoveEvent(event);
            return;
        }
        update(event->globalPos());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(!m_active) {
            QToolButton::mouseReleaseEvent(event);
            return;
        }
        finish(event->globalPos(),false);
        event->accept();
    }

private:
    void begin(const QPoint &pos)
    {
        m_active=true;
        m_dragged=false;
        m_axis=0;
        m_start=pos;
        m_last=pos;
        setDown(true);
    }
    void update(const QPoint &pos)
    {
        if(!m_active)return;
        const QPoint delta=pos-m_start;
        if(!m_dragged &&
           delta.manhattanLength()>=QApplication::startDragDistance()) {
            m_dragged=true;
            // Lock the gesture to its first intentional direction:
            // along=slider, perpendicular=canvas color selection.
            m_axis=qAbs(MobileToolbarDrag::along(delta,m_vertical)) >=
                qAbs(MobileToolbarDrag::away(delta,m_vertical))?1:2;
            setDown(false);
        }
        if(m_dragged && m_axis==1 && m_adjust) {
            const QPoint step=pos-m_last;
            m_adjust(m_command,MobileToolbarDrag::along(step,m_vertical));
        }
        m_last=pos;
    }
    void finish(const QPoint &pos,bool cancelled)
    {
        if(!m_active)return;
        update(pos);
        m_active=false;
        setDown(false);
        if(cancelled)return;
        if(!m_dragged) {
            if(rect().contains(mapFromGlobal(pos)) &&
               defaultAction() && defaultAction()->isEnabled())
                defaultAction()->trigger();
        } else if(m_axis==2 && m_command==QStringLiteral("color") &&
                  QSettings().value(MobileToolbarDrag::preference("sample"),
                                    true).toBool() && m_sample) {
            m_sample(pos);
        }
    }

    QString m_command;
    bool m_vertical=false;
    bool m_active=false;
    bool m_dragged=false;
    int m_axis=0;
    QPoint m_start,m_last;
    InlineAdjustment m_adjust;
    ColorSample m_sample;
};
#endif
