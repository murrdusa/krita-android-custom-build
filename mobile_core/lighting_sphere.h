/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QApplication>
#include <QWidget>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QTabletEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <QtMath>
#include <functional>
class MobileLightingSphere final : public QWidget
{
public:
    explicit MobileLightingSphere(QWidget *parent) : QWidget(parent)
    {
        setAccessibleName(tr("Lighting sphere"));
        setAttribute(Qt::WA_AcceptTouchEvents);
        setAttribute(Qt::WA_TabletTracking);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(450);
        connect(&m_holdTimer,&QTimer::timeout,this,[this]() {
            if(m_press!=WaitingForHold || !isVisible() || !onHold)return;
            m_press=Held;
            const auto hold=onHold;
            hold();
        });
    }

    std::function<void(const QColor&)> onColor;
    std::function<void()> onHold;
    std::function<void(const QPointF&)> onLightDirection;

    static QPointF defaultDirection() {return QPointF(-0.47,-0.61);}

    void setEditingDirection(bool editing)
    {
        m_editingDirection=editing;
        update();
    }

    void setLighting(const QColor &base,const QColor &light,const QColor &shadow,
                     qreal contrast,qreal intensity,const QPointF &direction)
    {
        if(m_base==base && m_light==light && m_shadow==shadow &&
           m_contrast==contrast && m_intensity==intensity && m_direction==direction)return;
        m_base=base;
        m_light=light;
        m_shadow=shadow;
        m_contrast=contrast;
        m_intensity=intensity;
        m_direction=direction;
        m_lightZ=qSqrt(qMax(0.0,1.0-QPointF::dotProduct(direction,direction)));
        m_image=QImage();
        update();
    }

protected:
    bool event(QEvent *event) override
    {
        if(event->type()==QEvent::Hide || event->type()==QEvent::UngrabMouse) {
            m_holdTimer.stop();
            m_press=Idle;
        }
        if(event->type()==QEvent::TouchBegin || event->type()==QEvent::TouchUpdate ||
           event->type()==QEvent::TouchEnd || event->type()==QEvent::TouchCancel) {
            m_nativeInput.restart();
            const auto *touch=static_cast<QTouchEvent*>(event);
            const auto points=touch->touchPoints();
            if(event->type()==QEvent::TouchCancel || points.size()>1) {
                m_holdTimer.stop();
                m_press=Idle;
            } else {
                const QPointF pos=points.isEmpty()?m_last:points.first().pos();
                if(event->type()==QEvent::TouchBegin)beginPress(pos);
                else if(event->type()==QEvent::TouchUpdate)movePress(pos);
                else endPress(pos);
            }
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        const auto area=sphereRect();
        if(area.isEmpty())return;
        if(m_image.isNull()) {
            constexpr int extent=192;
            m_image=QImage(extent,extent,QImage::Format_ARGB32_Premultiplied);
            // Android can refuse even this small raster allocation when
            // Krita canvases have exhausted the process memory budget.
            // A null QImage has no writable scan lines.
            if(m_image.isNull())return;
            for(int y=0;y<extent;++y) {
                auto *row=reinterpret_cast<QRgb*>(m_image.scanLine(y));
                for(int x=0;x<extent;++x) {
                    const QPointF normal((2*x+1.0)/extent-1,(2*y+1.0)/extent-1);
                    const qreal coverage=qBound(0.0,(1.0-qSqrt(QPointF::dotProduct(normal,normal)))*extent/2+0.5,1.0);
                    QColor color=shade(normal);
                    color.setAlphaF(coverage);
                    row[x]=qPremultiply(color.rgba());
                }
            }
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(area,m_image);
        if(m_editingDirection || m_hasSample) {
            const QPointF point=area.center()+(m_editingDirection?m_direction:m_sample)*area.width()/2;
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(Qt::black,3));
            painter.drawEllipse(point,6,6);
            painter.setPen(QPen(m_editingDirection?QColor("#ffd26a"):Qt::white,1.5));
            painter.drawEllipse(point,6,6);
        }
    }

    void tabletEvent(QTabletEvent *event) override
    {
        m_nativeInput.restart();
        if(event->type()==QEvent::TabletPress)beginPress(event->posF());
        else if(event->type()==QEvent::TabletMove)movePress(event->posF());
        else if(event->type()==QEvent::TabletRelease)endPress(event->posF());
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()!=Qt::LeftButton)return;
        if(!duplicateMouse(event))beginPress(event->localPos());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(!duplicateMouse(event))movePress(event->localPos());
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()!=Qt::LeftButton)return;
        if(!duplicateMouse(event))endPress(event->localPos());
        event->accept();
    }

private:
    bool duplicateMouse(const QMouseEvent *event) const
    {
        return event->source()!=Qt::MouseEventNotSynthesized &&
               m_nativeInput.isValid() && m_nativeInput.elapsed()<400;
    }
    void beginPress(const QPointF &pos)
    {
        const auto area=sphereRect();
        if(area.isEmpty())return;
        const QPointF normal=(pos-area.center())/(area.width()/2);
        if(QPointF::dotProduct(normal,normal)>1.0)return;
        m_start=m_last=pos;
        if(onHold) {m_press=WaitingForHold;m_holdTimer.start();}
        else {m_press=Sampling;pick(pos);}
    }
    void movePress(const QPointF &pos)
    {
        if(m_press==Idle || m_press==Held)return;
        m_last=pos;
        const QPointF delta=pos-m_start;
        const qreal slop=qMax(16,QApplication::startDragDistance()*2);
        if(m_press==WaitingForHold && QPointF::dotProduct(delta,delta)>=slop*slop) {
            m_holdTimer.stop();
            m_press=Sampling;
        }
        if(m_press==Sampling)pick(pos);
    }
    void endPress(const QPointF &pos)
    {
        if(m_press==Idle)return;
        m_holdTimer.stop();
        const bool sample=m_press!=Held;
        m_press=Idle;
        if(sample)pick(pos);
    }
    QRectF sphereRect() const
    {
        const qreal size=qMax(0,qMin(width(),height())-16);
        return QRectF((width()-size)/2,(height()-size)/2,size,size);
    }

    QColor shade(const QPointF &normal) const
    {
        const qreal z=qSqrt(qMax(0.0,1.0-QPointF::dotProduct(normal,normal)));
        const qreal light=QPointF::dotProduct(m_direction,normal)+m_lightZ*z;
        const qreal diffuse=qPow(qMax(0.0,light),0.55+2.1*m_contrast);
        const qreal specular=qPow(diffuse,18.0)*0.16;
        const qreal ambient=0.32+0.12*qMax(0.0,normal.y());
        const auto channel=[&](qreal base,qreal highlight,qreal shadow) {
            return qBound(0.0,m_intensity*highlight*(0.68*base*diffuse+specular)+base*shadow*ambient,1.0);
        };
        return QColor::fromRgbF(channel(m_base.redF(),m_light.redF(),m_shadow.redF()),
                                channel(m_base.greenF(),m_light.greenF(),m_shadow.greenF()),
                                channel(m_base.blueF(),m_light.blueF(),m_shadow.blueF()));
    }

    void pick(const QPointF &position)
    {
        const auto area=sphereRect();
        if(area.isEmpty())return;
        QPointF point=(position-area.center())/(area.width()/2);
        const qreal length=qSqrt(QPointF::dotProduct(point,point));
        if(length>1)point/=length;
        if(m_editingDirection) {
            if(onLightDirection)onLightDirection(point);
            return;
        }
        m_sample=point;
        m_hasSample=true;
        update();
        if(onColor)onColor(shade(m_sample));
    }

    QColor m_base=QColor("#c68462"),m_light=QColor("#fff4dc"),m_shadow=QColor("#8da9df");
    qreal m_contrast=0.85,m_intensity=1.0;
    QImage m_image;
    QPointF m_sample,m_start,m_last;
    QElapsedTimer m_nativeInput;
    QPointF m_direction=defaultDirection();
    qreal m_lightZ=qSqrt(1.0-QPointF::dotProduct(m_direction,m_direction));
    bool m_editingDirection=false;
    QTimer m_holdTimer;
    enum Press {Idle,WaitingForHold,Sampling,Held};
    Press m_press=Idle;
    bool m_hasSample=false;
};


#endif
