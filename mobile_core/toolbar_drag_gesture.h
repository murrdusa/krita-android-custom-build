/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QToolButton>
#include <QTimer>
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
    const QString key=QString::fromLatin1(parameter);
    return (key==QStringLiteral("sample")?QStringLiteral("MobileStudio/toolbarDrag/"):
                                          QStringLiteral("MobileStudio/toolbarVerticalSlider/"))+key;
}
inline QString assignment(const char *parameter)
{
    const QString fallback=qstrcmp(parameter,"size")==0?QStringLiteral("paint"):
        qstrcmp(parameter,"opacity")==0?QStringLiteral("color"):QString();
    return QSettings().value(preference(parameter),fallback).toString();
}
enum class Phase { Preview, Commit, Cancel };
inline bool samplesColor(const QString &id)
{
    if(id==QStringLiteral("eyedropper"))return true;
    return id==QStringLiteral("color") &&
        QSettings().value(preference("sample"),true).toBool();
}
inline QString adjustment(const QString &id)
{
    if(id==assignment("opacity"))return QStringLiteral("opacity");
    if(id==assignment("size"))return QStringLiteral("size");
    return {};
}
inline bool usesButton(const QString &id)
{
    return samplesColor(id) || !adjustment(id).isEmpty();
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
    using InlineAdjustment=std::function<void(const QString &,qreal,const QPoint &,MobileToolbarDrag::Phase)>;
    using ColorSample=std::function<void(const QPoint &,MobileToolbarDrag::Phase)>;
    using Hold=std::function<void()>;

    MobileToolbarGestureButton(QWidget *parent,const QString &command,
                               bool vertical,
                               InlineAdjustment adjust,ColorSample sample,Hold hold={})
        : QToolButton(parent),m_command(command),m_vertical(vertical),
          m_adjust(std::move(adjust)),m_sample(std::move(sample)),m_hold(std::move(hold))
    {
        setProperty("_mobileToolbarDragControl",true);
        setAttribute(Qt::WA_AcceptTouchEvents,true);
        m_holdTimer.setSingleShot(true);
        connect(&m_holdTimer,&QTimer::timeout,this,[this]() {
            m_held=true;
            setDown(false);
            m_hold();
        });
    }

protected:
    bool event(QEvent *event) override
    {
        switch(event->type()) {
        case QEvent::Hide:
            finish(m_last,true);
            break;
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            if(event->type()==QEvent::TouchBegin) {
                if(!handlesGesture())return QToolButton::event(event);
            } else if(!m_active) {
                return QToolButton::event(event);
            }
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
            if(points.size()>1)finish(pos,true);
            else if(event->type()==QEvent::TouchBegin)begin(pos);
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
        if(!handlesGesture() ||
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
    bool handlesGesture() const {return bool(m_hold) || MobileToolbarDrag::usesButton(m_command);}

    void begin(const QPoint &pos)
    {
        m_active=true;
        m_dragged=false;
        m_held=false;
        if(m_hold)m_holdTimer.start(430);
        m_axis=0;
        m_sampling=MobileToolbarDrag::samplesColor(m_command);
        m_parameter=MobileToolbarDrag::adjustment(m_command);
        m_start=pos;
        m_last=pos;
        setDown(true);
    }
    void update(const QPoint &pos)
    {
        if(!m_active || m_held)return;
        const QPoint delta=pos-m_start;
        if(!m_dragged &&
           delta.manhattanLength()>=QApplication::startDragDistance()) {
            m_dragged=true;
            m_holdTimer.stop();
            const bool inlineDrag=qAbs(MobileToolbarDrag::along(delta,m_vertical))>=
                                  qAbs(MobileToolbarDrag::away(delta,m_vertical));
            if(m_command==QStringLiteral("eyedropper"))m_axis=2;
            else if(m_command==QStringLiteral("color")) {
                if(!inlineDrag && m_sampling)m_axis=2;
                else if(inlineDrag && !m_parameter.isEmpty()) {
                    m_axis=1;
                    m_adjustVertical=m_vertical;
                }
            } else if(!m_parameter.isEmpty()) {
                m_axis=1;
                m_adjustVertical=qAbs(delta.y())>=qAbs(delta.x());
            }
            setDown(false);
        }
        if(m_dragged)dispatch(pos,MobileToolbarDrag::Phase::Preview);
        m_last=pos;
    }

    void dispatch(const QPoint &pos,MobileToolbarDrag::Phase phase)
    {
        if(m_axis==1 && m_adjust && !m_parameter.isEmpty())
            m_adjust(m_parameter,m_adjustVertical?m_start.y()-pos.y():pos.x()-m_start.x(),
                     pos,phase);
        if(m_axis==2 && m_sample)m_sample(pos,phase);
    }

    void finish(const QPoint &pos,bool cancelled)
    {
        if(!m_active)return;
        m_holdTimer.stop();
        if(!cancelled && !m_dragged && pos!=m_last)update(pos);
        m_active=false;
        setDown(false);
        if(m_held)return;
        if(m_dragged) {
            dispatch(pos,cancelled?MobileToolbarDrag::Phase::Cancel:MobileToolbarDrag::Phase::Commit);
        } else if(!cancelled && rect().contains(mapFromGlobal(pos)) &&
                  defaultAction() && defaultAction()->isEnabled()) {
            defaultAction()->trigger();
        }
    }

    QString m_command,m_parameter;
    bool m_vertical=false,m_sampling=false;
    bool m_active=false,m_adjustVertical=true;
    bool m_dragged=false;
    int m_axis=0;
    QPoint m_start,m_last;
    InlineAdjustment m_adjust;
    ColorSample m_sample;
    Hold m_hold;
    QTimer m_holdTimer;
    bool m_held=false;
};
class MobileToolbarDragPreview final : public QWidget
{
public:
    explicit MobileToolbarDragPreview(QWidget *canvas) : QWidget(canvas)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void value(const QString &label,qreal fraction,const QPoint &global)
    {
        m_color=QColor();
        m_label=label;
        m_fraction=fraction;
        const auto point=parentWidget()->mapFromGlobal(global);
        const auto bounds=parentWidget()->rect();
        const int x=qBound(0,point.x()+22,qMax(0,bounds.width()-152));
        const int y=qBound(0,point.y()-55,qMax(0,bounds.height()-48));
        present(QRect(x,y,152,48));
    }

    void color(const QColor &color,const QPoint &global)
    {
        m_color=color;
        const auto point=parentWidget()->mapFromGlobal(global);
        present(QRect(point-QPoint(40,40),QSize(80,80)));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if(m_color.isValid()) {
            const QRectF ring=QRectF(rect()).adjusted(12,12,-12,-12);
            painter.setPen(QPen(QColor("#f4f4f5"),16));
            painter.drawEllipse(ring);
            painter.setPen(QPen(QColor("#252529"),13));
            painter.drawEllipse(ring);
            painter.setPen(QPen(m_color,10));
            painter.drawEllipse(ring);
            return;
        }
        painter.setPen(QColor("#a0a8b4"));
        painter.setBrush(QColor("#252529"));
        painter.drawRoundedRect(rect().adjusted(1,1,-1,-1),8,8);
        painter.setPen(Qt::white);
        painter.drawText(QRect(9,4,width()-18,23),Qt::AlignCenter,m_label);
        painter.fillRect(QRect(10,32,width()-20,6),QColor("#555962"));
        painter.fillRect(QRect(10,32,qRound((width()-20)*m_fraction),6),QColor("#a7def1"));
    }

private:
    void present(const QRect &geometry)
    {
        setGeometry(geometry);
        show();
        raise();
        update();
    }

    QColor m_color;
    QString m_label;
    qreal m_fraction=0;
};

#endif
