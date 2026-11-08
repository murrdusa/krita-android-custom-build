/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A mobile color selector presented over the native Krita canvas. This
 * widget chooses a QColor, then passes it to Krita's existing foreground
 * resource provider in the current color space. No custom paint engine or
 * separate persistent document-color state is created.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>
#include <functional>

#include "engine_bridge.h"
#include <KoColor.h>
#include <klocalizedstring.h>

// This is only a visual selector. Krita's KoColor and canvas resource
// provider remain authoritative for the foreground paint color.
class MobileHueWheel final : public QWidget
{
public:
    explicit MobileHueWheel(QWidget *parent=nullptr) : QWidget(parent)
    {
        setMinimumSize(170,170);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        setAttribute(Qt::WA_AcceptTouchEvents,false);
    }

    std::function<void(const QColor&,bool)> onChosen;

    void showColor(const QColor &color)
    {
        if(!color.isValid() || m_dragMode!=None)return;
        int hue=0,sat=0,value=0,alpha=0;
        color.getHsv(&hue,&sat,&value,&alpha);
        if(hue>=0)m_hue=hue;
        m_sat=sat;
        m_val=value;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        const QPointF center=rect().center();
        const qreal radius=qMin(width(),height())/2.0-11.0;
        const QRectF circle(center.x()-radius,center.y()-radius,
                            radius*2,radius*2);
        // Hue is expressed as an angle in Krita's familiar HSV model,
        // with 0/red at the right and increasing counter-clockwise.
        for(int hue=0;hue<360;hue+=3) {
            QPen pen(QColor::fromHsv(hue,255,255),17,Qt::SolidLine,Qt::FlatCap);
            p.setPen(pen);
            p.drawArc(circle,hue*16,3*16+2);
        }
        const qreal angle=qDegreesToRadians(qreal(m_hue));
        const QPointF ring(center.x()+radius*qCos(angle),
                           center.y()-radius*qSin(angle));
        p.setPen(QPen(Qt::white,2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring,9,9);
        p.setPen(QPen(Qt::black,1));
        p.drawEllipse(ring,11,11);

        const QRectF square=squareRect();
        QPainterPath rounded;
        rounded.addRoundedRect(square,3,3);
        p.save();
        p.setClipPath(rounded);
        QLinearGradient horizontal(square.topLeft(),square.topRight());
        horizontal.setColorAt(0,Qt::white);
        horizontal.setColorAt(1,QColor::fromHsv(m_hue,255,255));
        p.fillRect(square,horizontal);
        QLinearGradient vertical(square.topLeft(),square.bottomLeft());
        vertical.setColorAt(0,QColor(0,0,0,0));
        vertical.setColorAt(1,Qt::black);
        p.fillRect(square,vertical);
        p.restore();
        p.setPen(QPen(QColor(255,255,255,220),1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(square,3,3);
        const QPointF mark(square.left()+m_sat/255.0*square.width(),
                           square.top()+(1.0-m_val/255.0)*square.height());
        p.setPen(QPen(Qt::black,3));
        p.drawEllipse(mark,5,5);
        p.setPen(QPen(Qt::white,1.5));
        p.drawEllipse(mark,6,6);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()!=Qt::LeftButton)return;
        const QPointF delta=event->pos()-rect().center();
        const qreal dist=qSqrt(delta.x()*delta.x()+delta.y()*delta.y());
        const qreal radius=qMin(width(),height())/2.0-11.0;
        if(dist>=radius-17 && dist<=radius+12)m_dragMode=Hue;
        else if(squareRect().contains(event->pos()))m_dragMode=SV;
        else {event->ignore();return;}
        setCursor(Qt::CrossCursor);
        applyPosition(event->pos(),false);
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(m_dragMode==None || !(event->buttons()&Qt::LeftButton))return;
        applyPosition(event->pos(),false);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(m_dragMode==None)return;
        applyPosition(event->pos(),true);
        m_dragMode=None;
        unsetCursor();
        event->accept();
    }
private:
    enum DragMode {None,Hue,SV};
    QRectF squareRect() const
    {
        const qreal radius=qMin(width(),height())/2.0-11.0;
        const qreal size=qMax(35.0,(radius-23.0)*1.37);
        const QPointF center=rect().center();
        return QRectF(center.x()-size/2,center.y()-size/2,size,size);
    }
    void applyPosition(const QPointF &point,bool final)
    {
        if(m_dragMode==Hue) {
            const QPointF d=point-rect().center();
            const qreal angle=qRadiansToDegrees(qAtan2(-d.y(),d.x()));
            m_hue=(qRound(angle)+360)%360;
        } else if(m_dragMode==SV) {
            const QRectF square=squareRect();
            const qreal x=qBound(0.0,(point.x()-square.left())/
                                      square.width(),1.0);
            const qreal y=qBound(0.0,(point.y()-square.top())/
                                      square.height(),1.0);
            m_sat=qRound(255*x);
            m_val=qRound(255*(1-y));
        }
        update();
        if(onChosen)onChosen(QColor::fromHsv(m_hue,m_sat,m_val),final);
    }
    int m_hue=0,m_sat=255,m_val=255;
    DragMode m_dragMode=None;
};

class MobileColorWorkspace final : public QFrame
{
public:
    explicit MobileColorWorkspace(const MobileEngineBridge &engine,
                                  QWidget *canvasParent)
        : QFrame(canvasParent),m_engine(engine)
    {
        setObjectName(QStringLiteral("MobileColorWorkspace"));
        setAttribute(Qt::WA_StyledBackground,true);
        setStyleSheet(
            "QFrame#MobileColorWorkspace{background:#27394a;"
            "border:1px solid #7e9eb5;border-radius:10px;}"
            "QToolButton,QPushButton{border:1px solid #7899b2;"
            "border-radius:6px;min-height:34px;padding:3px;"
            "background:#435c72;color:#f4f9ff;}"
            "QLabel{color:#eef5fb;font-size:12px;}");
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(6,6,6,6);
        root->setSpacing(4);
        auto *top=new QHBoxLayout;
        top->setContentsMargins(0,0,0,0);
        top->setSpacing(4);
        m_swatch=new QToolButton(this);
        m_swatch->setText(i18n("Color"));
        m_swatch->setToolTip(i18n("Expand or collapse the color wheel"));
        m_swatch->setMinimumWidth(57);
        top->addWidget(m_swatch,1);
        connect(m_swatch,&QToolButton::clicked,this,[this]() {
            setExpanded(!m_expanded);
        });
        m_collapse=new QToolButton(this);
        m_collapse->setText(QStringLiteral("−"));
        m_collapse->setToolTip(i18n("Minimize color wheel"));
        top->addWidget(m_collapse);
        connect(m_collapse,&QToolButton::clicked,this,[this]() {
            setExpanded(false);
        });
        root->addLayout(top);

        m_contents=new QWidget(this);
        auto *inner=new QVBoxLayout(m_contents);
        inner->setContentsMargins(0,0,0,0);
        inner->setSpacing(4);
        m_wheel=new MobileHueWheel(m_contents);
        inner->addWidget(m_wheel,1);
        m_wheel->onChosen=[this](const QColor &color,bool final) {
            setNativeColor(color,final);
        };
        auto *bottom=new QHBoxLayout;
        bottom->setContentsMargins(0,0,0,0);
        bottom->setSpacing(2);
        for(int i=0;i<8;++i) {
            auto *swatch=new QToolButton(m_contents);
            swatch->setMinimumSize(22,28);
            swatch->setMaximumHeight(32);
            swatch->setToolTip(i18n("Recent color"));
            bottom->addWidget(swatch,1);
            m_recents.append(swatch);
            connect(swatch,&QToolButton::clicked,this,[this,i]() {
                if(i>=m_history.size())return;
                const QColor color(m_history.at(i));
                if(color.isValid())setNativeColor(color,true);
            });
        }
        inner->addLayout(bottom);
        auto *advanced=new QToolButton(m_contents);
        advanced->setText(i18n("Advanced color…"));
        advanced->setToolTip(i18n("Native color dialog for precise values"));
        connect(advanced,&QToolButton::clicked,this,[this]() {
            auto *provider=m_engine.brushResources();
            if(!provider)return;
            const QColor chosen=QColorDialog::getColor(
                provider->fgColor().toQColor(),this,i18n("Foreground color"),
                QColorDialog::ShowAlphaChannel);
            if(chosen.isValid())setNativeColor(chosen,true);
        });
        inner->addWidget(advanced);
        root->addWidget(m_contents);
        if(canvasParent)canvasParent->installEventFilter(this);

        m_history=QSettings().value(
            QStringLiteral("MobileStudio/recentColors")).toStringList();
        while(m_history.size()>8)m_history.removeLast();
        refreshHistory();
        setExpanded(QSettings().value(
            QStringLiteral("MobileStudio/colorWheelExpanded"),true).toBool());
        refresh();
    }

    void refresh()
    {
        const auto *provider=m_engine.brushResources();
        if(!provider)return;
        const QColor foreground=provider->fgColor().toQColor();
        if(!foreground.isValid())return;
        m_wheel->showColor(foreground);
        m_swatch->setStyleSheet(QStringLiteral(
            "QToolButton{background:%1;color:%2;border:2px solid #a5c4d8;"
            "border-radius:7px;min-height:35px;padding:3px;}")
            .arg(foreground.name(),foreground.lightness()<130?
                                     QStringLiteral("#ffffff"):
                                     QStringLiteral("#202934")));
    }
    void setExpanded(bool expanded)
    {
        m_expanded=expanded;
        m_contents->setVisible(expanded);
        m_collapse->setVisible(expanded);
        setFixedSize(expanded?238:72,expanded?316:53);
        QSettings().setValue(
            QStringLiteral("MobileStudio/colorWheelExpanded"),expanded);
        reposition();
        raise();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parentWidget() &&
           (event->type()==QEvent::Resize || event->type()==QEvent::Show))
            reposition();
        return QFrame::eventFilter(watched,event);
    }

private:
    void reposition()
    {
        auto *parent=parentWidget();
        if(!parent)return;
        const int x=qMax(0,parent->width()-width()-7);
        const int y=qMax(0,parent->height()-height()-7);
        move(x,y);
    }
    void setNativeColor(const QColor &color,bool final)
    {
        auto *provider=m_engine.brushResources();
        if(!provider || !color.isValid())return;
        const KoColor previous=provider->fgColor();
        if(!previous.colorSpace())return;
        // Preserve the active Krita color space, unlike a standalone
        // QColor state or an RGB-only approximation of the document.
        provider->setFGColor(KoColor(color,previous.colorSpace()));
        if(final)remember(color);
        refresh();
    }
    void remember(const QColor &color)
    {
        const QString code=color.name(QColor::HexArgb);
        m_history.removeAll(code);
        m_history.prepend(code);
        while(m_history.size()>8)m_history.removeLast();
        QSettings().setValue(QStringLiteral("MobileStudio/recentColors"),
                             m_history);
        refreshHistory();
    }
    void refreshHistory()
    {
        for(int i=0;i<m_recents.size();++i) {
            auto *button=m_recents.at(i);
            if(i>=m_history.size()) {
                button->setEnabled(false);
                button->setStyleSheet(QStringLiteral(
                    "QToolButton{background:#344a5e;}"));
                continue;
            }
            const QColor color(m_history.at(i));
            button->setEnabled(color.isValid());
            button->setToolTip(color.name(QColor::HexArgb));
            button->setStyleSheet(QStringLiteral(
                "QToolButton{background:%1;border:1px solid #9bb2c4;"
                "border-radius:5px;}").arg(color.name()));
        }
    }
    MobileEngineBridge m_engine;
    QToolButton *m_swatch=nullptr,*m_collapse=nullptr;
    QWidget *m_contents=nullptr;
    MobileHueWheel *m_wheel=nullptr;
    QVector<QToolButton*> m_recents;
    QStringList m_history;
    bool m_expanded=true;
};
#endif // Q_OS_ANDROID
