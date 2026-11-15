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

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QConicalGradient>
#include <QDialog>
#include "touch_scroll.h"
#include "recent_colors.h"
#include "overlay_placement.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QImage>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QRegion>
#include <QSize>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include <array>
#include <QButtonGroup>
#include <functional>

#include "engine_bridge.h"
#include <KoColor.h>
#include <klocalizedstring.h>

// This is only a visual selector. Krita's KoColor and canvas resource
// provider remain authoritative for the foreground paint color.
class MobileHueWheel final : public QWidget
{
public:
    explicit MobileHueWheel(QWidget *parent=nullptr,int rimInset=15)
        : QWidget(parent),m_rimInset(rimInset)
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
        if(m_sat==sat && m_val==value &&
           (hue<0 || m_hue==hue))return;
        if(hue>=0)m_hue=hue;
        m_sat=sat;
        m_val=value;
        update();
    }

protected:
    bool event(QEvent *event) override
    {
        if((event->type()==QEvent::Hide || event->type()==QEvent::UngrabMouse) &&
           m_dragMode!=None) {
            m_dragMode=None;
            unsetCursor();
            if(onChosen)onChosen(QColor::fromHsv(m_hue,m_sat,m_val),true);
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        const QPointF center=wheelCenter();
        const qreal radius=wheelRadius();
        if(radius<=0)return;
        // One antialiased stroke with a continuous HSV gradient avoids the
        // exposed arc joins and pixelated rim of overlapping hue segments.
        QConicalGradient hues(center,0);
        for(int hue=0;hue<360;hue+=60)
            hues.setColorAt(hue/360.0,QColor::fromHsv(hue,255,255));
        hues.setColorAt(1.0,Qt::red);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(18,19,22,215),20));
        p.drawEllipse(center,radius,radius);
        p.setPen(QPen(QBrush(hues),17));
        p.drawEllipse(center,radius,radius);
        const qreal angle=qDegreesToRadians(qreal(m_hue));
        const QPointF ring(center.x()+radius*qCos(angle),
                           center.y()-radius*qSin(angle));
        p.setPen(QPen(Qt::white,2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring,9,9);
        p.setPen(QPen(Qt::black,1));
        p.drawEllipse(ring,11,11);

        const QRectF square=squareRect();
        if(square.isEmpty())return;
        QLinearGradient horizontal(square.topLeft(),square.topRight());
        horizontal.setColorAt(0,Qt::white);
        horizontal.setColorAt(1,QColor::fromHsv(m_hue,255,255));
        QLinearGradient vertical(square.topLeft(),square.bottomLeft());
        vertical.setColorAt(0,Qt::transparent);
        vertical.setColorAt(1,Qt::black);
        p.setPen(Qt::NoPen);
        p.setBrush(horizontal);
        p.drawRoundedRect(square,3,3);
        p.setBrush(vertical);
        p.drawRoundedRect(square,3,3);
        p.setPen(QPen(QColor(24,25,29,190),1));
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
        const QPointF delta=event->pos()-wheelCenter();
        const qreal dist=qSqrt(delta.x()*delta.x()+delta.y()*delta.y());
        const qreal radius=wheelRadius();
        if(squareRect().contains(event->pos()))m_dragMode=SV;
        else if(radius>0 && dist>=radius-17 && dist<=radius+12)m_dragMode=Hue;
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
    QPointF wheelCenter() const {return QRectF(rect()).center();}
    qreal wheelRadius() const {return qMin(width(),height())/2.0-m_rimInset;}
    QRectF squareRect() const
    {
        const qreal size=qMax(0.0,(wheelRadius()-14.0)*qSqrt(2.0));
        const QPointF center=wheelCenter();
        return QRectF(center.x()-size/2,center.y()-size/2,size,size);
    }
    void applyPosition(const QPointF &point,bool final)
    {
        if(m_dragMode==Hue) {
            const QPointF d=point-wheelCenter();
            const qreal angle=qRadiansToDegrees(qAtan2(-d.y(),d.x()));
            m_hue=(qRound(angle)+360)%360;
        } else if(m_dragMode==SV) {
            const QRectF square=squareRect();
            if(square.isEmpty())return;
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
    int m_rimInset;
};

class MobileLightingSphere final : public QWidget
{
public:
    explicit MobileLightingSphere(QWidget *parent) : QWidget(parent)
    {
        setAccessibleName(tr("Lighting sphere"));
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(450);
        connect(&m_holdTimer,&QTimer::timeout,this,[this]() {
            if(m_press!=WaitingForHold || !isVisible() || !onHold)return;
            m_press=Held;
            onHold();
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
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        const auto area=sphereRect();
        if(area.isEmpty())return;
        if(m_image.isNull()) {
            constexpr int extent=192;
            m_image=QImage(extent,extent,QImage::Format_ARGB32_Premultiplied);
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

    void mousePressEvent(QMouseEvent *event) override
    {
        const auto area=sphereRect();
        if(event->button()!=Qt::LeftButton || area.isEmpty())return;
        const QPointF normal=(event->localPos()-area.center())/(area.width()/2);
        if(QPointF::dotProduct(normal,normal)>1.0) {event->ignore();return;}
        m_start=event->localPos();
        if(onHold) {
            m_press=WaitingForHold;
            m_holdTimer.start();
        } else {
            m_press=Sampling;
            pick(m_start);
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(m_press==Idle)return;
        if(m_press==WaitingForHold &&
           (event->localPos()-m_start).manhattanLength()>=QApplication::startDragDistance()) {
            m_holdTimer.stop();
            m_press=Sampling;
        }
        if(m_press==Sampling)pick(event->localPos());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(m_press==Idle || event->button()!=Qt::LeftButton)return;
        m_holdTimer.stop();
        const bool sample=m_press!=Held;
        m_press=Idle;
        if(sample)pick(event->localPos());
        event->accept();
    }

private:
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
    QPointF m_sample,m_start;
    QPointF m_direction=defaultDirection();
    qreal m_lightZ=qSqrt(1.0-QPointF::dotProduct(m_direction,m_direction));
    bool m_editingDirection=false;
    QTimer m_holdTimer;
    enum Press {Idle,WaitingForHold,Sampling,Held};
    Press m_press=Idle;
    bool m_hasSample=false;
};

class MobileColorPicker final : public QWidget
{
public:
    struct Lighting {
        explicit Lighting(const QSettings &settings)
            : base(readColor(settings,"object",QColor("#c68462"))),
              light(readColor(settings,"highlight",QColor("#fff4dc"))),
              shadow(readColor(settings,"shadow",QColor("#8da9df"))),
              contrast(qBound(0,settings.value(key("hardness"),85).toInt(),100)),
              intensity(qBound(0,settings.value(key("intensity"),100).toInt(),200)),
              followColor(settings.value(key("followColor"),true).toBool()),
              direction(settings.value(key("direction"),MobileLightingSphere::defaultDirection()).toPointF())
        {
            const qreal magnitude=QPointF::dotProduct(direction,direction);
            if(!qIsFinite(magnitude) || magnitude>1.000001)
                direction=MobileLightingSphere::defaultDirection();
        }

        void save() const
        {
            QSettings settings;
            settings.setValue(key("object"),base.name());
            settings.setValue(key("highlight"),light.name());
            settings.setValue(key("shadow"),shadow.name());
            settings.setValue(key("hardness"),contrast);
            settings.setValue(key("intensity"),intensity);
            settings.setValue(key("followColor"),followColor);
            settings.setValue(key("direction"),direction);
        }

        QColor base,light,shadow;
        int contrast,intensity;
        bool followColor;
        QPointF direction;

        void apply(MobileLightingSphere *sphere) const
        {
            if(sphere)sphere->setLighting(base,light,shadow,contrast/100.0,intensity/100.0,direction);
        }

    private:
        static QString key(const char *name)
        {
            return QStringLiteral("MobileStudio/lightingSphere/")+QString::fromLatin1(name);
        }

        static QColor readColor(const QSettings &settings,const char *name,const QColor &fallback)
        {
            const QColor color(settings.value(key(name)).toString());
            return color.isValid()?color:fallback;
        }
    };

    MobileColorPicker(QWidget *parent,KisCanvasResourceProvider *provider,
                      Lighting &lighting,MobileLightingSphere *floating)
        : QWidget(parent),m_provider(provider),m_lighting(lighting),m_floating(floating)
    {
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(6,6,6,6);
        root->setSpacing(4);
        m_tabs=new QTabBar(this);
        auto *tabs=m_tabs;
        tabs->setExpanding(true);
        tabs->setStyleSheet(QStringLiteral(
            "QTabBar::tab{background:#30323b;color:#c3c6d1;min-height:30px;padding:0 7px;}"
            "QTabBar::tab:selected{background:#394459;color:#fff;}"));
        root->addWidget(tabs);
        m_pages=new QStackedWidget(this);
        auto *pages=m_pages;
        pages->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        root->addWidget(pages,1);
        const auto addPage=[tabs,pages](QWidget *page,const QString &name) {
            pages->addWidget(page);
            tabs->addTab(name);
        };
        connect(tabs,&QTabBar::currentChanged,pages,&QStackedWidget::setCurrentIndex);
        connect(pages,&QStackedWidget::currentChanged,this,[this]() {
            if(onPreviewLayoutChanged)onPreviewLayoutChanged();
        });

        auto *wheelPage=new QWidget(pages);
        auto *wheelLayout=new QVBoxLayout(wheelPage);
        wheelLayout->setContentsMargins(0,0,0,0);
        wheelLayout->setSpacing(2);
        m_wheel=new MobileHueWheel(wheelPage);
        m_wheel->setMinimumSize(0,0);
        wheelLayout->addWidget(m_wheel,1);
        auto *comparison=new QVBoxLayout(m_wheel);
        comparison->setContentsMargins(0,0,0,0);
        comparison->setAlignment(Qt::AlignTop|Qt::AlignRight);
        comparison->setSizeConstraint(QLayout::SetNoConstraint);
        m_previous=new QLabel(m_wheel);
        m_current=new QLabel(m_wheel);
        for(auto *chip:{m_previous,m_current}) {
            chip->setFixedSize(23,23);
            comparison->addWidget(chip);
            chip->hide();
        }
        m_previous->setAccessibleName(tr("Previous color"));
        m_current->setAccessibleName(tr("Current color"));
        m_wheel->onChosen=[this](const QColor &color,bool final) {
            if(!final && !m_previous->isVisible() && m_provider) {
                paintChip(m_previous,m_provider->fgColor().toQColor());
                m_previous->show();
                m_current->show();
            }
            select(color);
            if(final) {
                m_previous->hide();
                m_current->hide();
            } else {
                paintChip(m_current,m_provider?m_provider->fgColor().toQColor():color);
            }
        };
        addPage(wheelPage,tr("Wheel"));

        auto *values=new QWidget(pages);
        auto *valuesLayout=new QVBoxLayout(values);
        valuesLayout->setContentsMargins(0,0,0,0);
        auto *form=new QFormLayout;
        valuesLayout->addLayout(form);
        valuesLayout->addStretch();
        form->setContentsMargins(4,0,4,0);
        form->setSpacing(0);
        m_hex=new QLineEdit(values);
        m_hex->setAccessibleName(tr("Hex color"));
        m_hex->setMaxLength(7);
        m_hex->setMinimumHeight(30);
        form->addRow(tr("Hex"),m_hex);
        const QStringList captions{tr("Hue"),tr("Saturation"),tr("Value")};
        for(int i=0;i<3;++i) {
            auto *row=new QHBoxLayout;
            m_values[i]=new MobileTouchSlider(values);
            m_values[i]->setMinimumHeight(30);
            m_values[i]->setAccessibleName(captions.at(i));
            m_values[i]->setRange(0,i==0?359:100);
            m_labels[i]=new QLabel(values);
            m_labels[i]->setMinimumWidth(38);
            row->addWidget(m_values[i],1);
            row->addWidget(m_labels[i]);
            form->addRow(captions.at(i),row);
            connect(m_values[i],&QSlider::valueChanged,this,[this](int) {
                m_hue=m_values[0]->value();
                select(QColor::fromHsvF(m_hue/360.0,m_values[1]->value()/100.0,
                    m_values[2]->value()/100.0));
            });
        }
        connect(m_hex,&QLineEdit::editingFinished,this,[this]() {
            const auto text=m_hex->text().trimmed();
            const QColor color(text.startsWith(QLatin1Char('#'))?text:QStringLiteral("#")+text);
            if(text.size()>=6 && color.isValid())select(color);
            if(m_provider)m_hex->setText(m_provider->fgColor().toQColor().name());
        });
        addPage(values,tr("Sliders"));

        auto *lightingPage=new QWidget(pages);
        auto *lightingLayout=new QVBoxLayout(lightingPage);
        lightingLayout->setContentsMargins(0,0,0,0);
        lightingLayout->setSpacing(2);
        m_sphere=new MobileLightingSphere(lightingPage);
        m_sphere->onColor=[this](const QColor &color) {
            if(onSphereColor)onSphereColor(color);
        };
        lightingLayout->addWidget(m_sphere,1);
        m_lightingScroll=new QScrollArea(lightingPage);
        m_lightingScroll->setWidgetResizable(true);
        m_lightingScroll->setFrameShape(QFrame::NoFrame);
        MobileTouchScroll::configure(m_lightingScroll);
        auto *controls=new QWidget;
        m_lightingScroll->setWidget(controls);
        lightingLayout->addWidget(m_lightingScroll);
        auto *lightingSettings=new QVBoxLayout(controls);
        lightingSettings->setContentsMargins(0,0,0,0);
        lightingSettings->setSpacing(2);
        auto *lightingControls=new QHBoxLayout;
        auto *group=new QButtonGroup(lightingPage);
        const QStringList roles{tr("Object"),tr("Highlight"),tr("Shadow")};
        const std::array<QColor*,3> roleColors{{&m_lighting.base,&m_lighting.light,&m_lighting.shadow}};
        std::array<QPushButton*,3> roleButtons{};
        for(int i=0;i<3;++i) {
            auto *button=new QPushButton(roles.at(i),lightingPage);
            button->setCheckable(true);
            button->setMinimumHeight(40);
            group->addButton(button,i);
            lightingControls->addWidget(button,1);
            roleButtons[i]=button;
        }
        roleButtons[0]->setChecked(true);
        lightingSettings->addLayout(lightingControls);
        auto *lightingForm=new QFormLayout;
        lightingForm->setSpacing(2);
        std::array<MobileTouchSlider*,3> lightingSliders{};
        for(int i=0;i<3;++i) {
            auto *slider=new MobileTouchSlider(lightingPage);
            slider->setMinimumHeight(36);
            slider->setRange(0,i==0?359:100);
            slider->setAccessibleName(captions.at(i));
            lightingForm->addRow(captions.at(i),slider);
            lightingSliders[i]=slider;
        }
        lightingSettings->addLayout(lightingForm);
        m_lightingHues={{qMax(0,m_lighting.base.hsvHue()),
                         qMax(0,m_lighting.light.hsvHue()),
                         qMax(0,m_lighting.shadow.hsvHue())}};
        const auto refreshRole=[group,roleColors,lightingSliders,lightingForm](int hue) {
            const int role=group->checkedId();
            const auto color=*roleColors[role];
            const int current[]{hue,qRound(color.hsvSaturationF()*100),qRound(color.valueF()*100)};
            for(int i=0;i<3;++i) {
                const QSignalBlocker block(lightingSliders[i]);
                lightingSliders[i]->setValue(current[i]);
            }
            auto *value=lightingSliders[2];
            value->setVisible(role==0);
            lightingForm->labelForField(value)->setVisible(role==0);
            paintTracks(lightingSliders.data(),hue,color);
        };
        for(int i=0;i<3;++i) {
            connect(roleButtons[i],&QPushButton::clicked,this,[this,i,refreshRole]() {
                refreshRole(m_lightingHues[i]);
                fitLightingControls();
                if(onPreviewLayoutChanged)onPreviewLayoutChanged();
            });
            connect(lightingSliders[i],&QSlider::valueChanged,this,
                    [this,group,roleColors,lightingSliders,refreshRole](int) {
                const int role=group->checkedId();
                const auto hue=lightingSliders[0]->value();
                m_lightingHues[role]=hue;
                const auto old=*roleColors[role];
                *roleColors[role]=QColor::fromHsvF(hue/360.0,lightingSliders[1]->value()/100.0,
                    role==0?lightingSliders[2]->value()/100.0:old.valueF());
                refreshRole(hue);
                refreshLighting();
            });
        }
        refreshRole(m_lightingHues[0]);
        m_syncBase=[this,group,refreshRole]() {
            if(m_lighting.base.hsvHue()>=0)m_lightingHues[0]=m_lighting.base.hsvHue();
            if(group->checkedId()==0)refreshRole(m_lightingHues[0]);
        };
        auto *levels=new QFormLayout;
        levels->setSpacing(2);
        const auto addLevel=[&](const QString &label,int &value,int maximum) {
            auto *slider=new MobileTouchSlider(lightingPage);
            slider->setMinimumHeight(32);
            slider->setAccessibleName(label);
            slider->setRange(0,maximum);
            slider->setValue(value);
            levels->addRow(label,slider);
            connect(slider,&QSlider::valueChanged,this,[this,&value](int changed) {
                value=changed;
                refreshLighting();
            });
        };
        addLevel(tr("Shadow hardness"),m_lighting.contrast,100);
        addLevel(tr("Light intensity"),m_lighting.intensity,200);
        lightingSettings->addLayout(levels);
        auto *follow=new QCheckBox(tr("Auto-update base color"),lightingPage);
        follow->setAccessibleDescription(tr("Use colors picked outside the sphere as its base."));
        follow->setChecked(m_lighting.followColor);
        lightingSettings->addWidget(follow);
        connect(follow,&QCheckBox::toggled,this,[this](bool enabled) {
            m_lighting.followColor=enabled;
            m_lighting.save();
        });
        auto *direction=new QPushButton(tr("Adjust light direction"),controls);
        direction->setCheckable(true);
        direction->setAccessibleDescription(tr("Turn on, then drag on the sphere to move the light."));
        lightingSettings->addWidget(direction);
        connect(direction,&QPushButton::toggled,this,[this,direction](bool enabled) {
            m_sphere->setEditingDirection(enabled);
            direction->setText(enabled?tr("Done moving light"):tr("Adjust light direction"));
        });
        m_sphere->onLightDirection=[this](const QPointF &point) {
            m_lighting.direction=point;
            refreshLighting();
        };
        refreshLighting();
        addPage(lightingPage,tr("Lighting"));

        const auto colors=MobileRecentColors::load(MobileRecentColors::pickerCapacity);
        if(!colors.isEmpty()) {
            m_historyGrid=new QGridLayout;
            m_historyGrid->setSpacing(3);
            for(int i=0;i<colors.size();++i) {
                constexpr int columns=MobileRecentColors::stripCapacity;
                m_historyGrid->addWidget(swatch(this,QColor(colors.at(i))),i/columns,i%columns);
                m_historyGrid->setColumnStretch(i%columns,1);
            }
            root->addLayout(m_historyGrid);
        }
        connect(provider,&KisCanvasResourceProvider::sigFGColorChanged,this,
                [this](const KoColor &color) {showColor(color.toQColor());});
        showColor(provider->fgColor().toQColor());
    }

    std::function<void()> onPreviewLayoutChanged;
    std::function<void(const QColor &)> onSphereColor;

    void showLighting() {m_tabs->setCurrentIndex(2);}

    void syncLightingBase()
    {
        if(m_syncBase)m_syncBase();
        refreshLighting();
    }

    QSize preferredPopupSize() const
    {
        const int chrome=outerRowsHeight()+4;
        switch(m_tabs->currentIndex()) {
        case 1:return QSize(320,m_pages->currentWidget()->layout()->sizeHint().height()+chrome+10);
        case 2:return QSize(360,470+chrome);
        default:return QSize(344,328+chrome);
        }
    }

    int widthForPopupHeight(int height) const
    {
        if(m_tabs->currentIndex()!=0)return preferredPopupSize().width();
        return qMax(230,height-outerRowsHeight()+12);
    }

    void setFloatingSphere(MobileLightingSphere *sphere)
    {
        m_floating=sphere;
        refreshLighting();
    }

    static void setForeground(KisCanvasResourceProvider *provider,const QColor &color)
    {
        if(!provider || !color.isValid())return;
        const KoColor current=provider->fgColor();
        if(current.colorSpace() && current.toQColor()!=color)
            provider->setFGColor(KoColor(color,current.colorSpace()));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        fitLightingControls();
    }

private:
    int outerRowsHeight() const
    {
        return 12+m_tabs->sizeHint().height()+4+
            (m_historyGrid?m_historyGrid->sizeHint().height()+4:0);
    }

    void fitLightingControls()
    {
        if(!m_lightingScroll)return;
        const int ideal=m_lightingScroll->widget()->sizeHint().height();
        const int available=qMax(0,height()-outerRowsHeight());
        m_lightingScroll->setFixedHeight(qMin(ideal,available*3/5));
    }

    static void paintChip(QLabel *chip,const QColor &color)
    {
        chip->setStyleSheet(QStringLiteral("background:%1;border:1px solid #767681;border-radius:6px;")
                            .arg(color.name()));
    }

    static void paintTracks(MobileTouchSlider *const *sliders,int hue,const QColor &color)
    {
        const auto gradient=[](const QString &first,const QString &last) {
            return QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2)")
                .arg(first,last);
        };
        const QString rainbow=QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 red,stop:0.167 yellow,stop:0.333 lime,stop:0.5 cyan,"
            "stop:0.667 blue,stop:0.833 magenta,stop:1 red)");
        const auto saturation=gradient(QColor::fromHsv(hue,0,color.value()).name(),
                                      QColor::fromHsv(hue,255,color.value()).name());
        const auto value=gradient(QStringLiteral("#000000"),
                                 QColor::fromHsv(hue,color.hsvSaturation(),255).name());
        const QString tracks[]{rainbow,saturation,value};
        for(int i=0;i<3;++i) {
            if(sliders[i]->property("mobileColorTrack").toString()==tracks[i])continue;
            sliders[i]->setProperty("mobileColorTrack",tracks[i]);
            sliders[i]->setTrackColors(tracks[i],QStringLiteral("transparent"));
        }
    }

    QPushButton *swatch(QWidget *parent,const QColor &color)
    {
        auto *button=new QPushButton(parent);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        button->setFixedHeight(24);
        button->setAccessibleName(color.name());
        button->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid #767681;border-radius:6px;"
                                              "min-height:24px;padding:0;}")
                              .arg(color.name()));
        connect(button,&QPushButton::clicked,this,[this,color]() {select(color);});
        return button;
    }

    void select(const QColor &color)
    {
        if(color.hsvHue()>=0)m_hue=color.hsvHue();
        setForeground(m_provider,color);
        if(m_provider)showColor(m_provider->fgColor().toQColor());
    }

    void showColor(const QColor &color)
    {
        if(!color.isValid())return;
        if(color.hsvHue()>=0)m_hue=color.hsvHue();
        m_wheel->showColor(QColor::fromHsv(m_hue,color.hsvSaturation(),color.value()));
        if(!m_hex->hasFocus())m_hex->setText(color.name());
        const int values[]{m_hue,qRound(color.hsvSaturationF()*100),qRound(color.valueF()*100)};
        paintTracks(m_values,m_hue,color);
        for(int i=0;i<3;++i) {
            const QSignalBlocker block(m_values[i]);
            m_values[i]->setValue(values[i]);
            m_labels[i]->setText(QString::number(values[i])+(i==0?QStringLiteral("°"):QStringLiteral("%")));
        }
    }

    void refreshLighting()
    {
        m_lighting.apply(m_sphere);
        m_lighting.apply(m_floating);
    }

    QPointer<KisCanvasResourceProvider> m_provider;
    Lighting &m_lighting;
    QPointer<MobileLightingSphere> m_floating;
    MobileLightingSphere *m_sphere=nullptr;
    MobileHueWheel *m_wheel=nullptr;
    QStackedWidget *m_pages=nullptr;
    QTabBar *m_tabs=nullptr;
    QGridLayout *m_historyGrid=nullptr;
    QScrollArea *m_lightingScroll=nullptr;
    std::function<void()> m_syncBase;
    QLineEdit *m_hex=nullptr;
    QLabel *m_previous=nullptr,*m_current=nullptr;
    std::array<int,3> m_lightingHues{};
    MobileTouchSlider *m_values[3]{};
    QLabel *m_labels[3]{};
    int m_hue=0;
};

class MobileColorWorkspace final : public QFrame
{
public:
    explicit MobileColorWorkspace(const MobileEngineBridge &engine,
                                  QWidget *canvasParent,bool wheelOnly=false)
        : QFrame(canvasParent),m_engine(engine),m_wheelOnly(wheelOnly)
    {
        setObjectName(QStringLiteral("MobileColorWorkspace"));
        setAttribute(Qt::WA_StyledBackground,true);
        if(m_wheelOnly) {
            setAttribute(Qt::WA_TranslucentBackground);
            setStyleSheet(QStringLiteral(
                "QFrame#MobileColorWorkspace{background:transparent;border:0;}"));
            QSettings settings;
            m_sizeLevel=qBound(0,settings.value(QStringLiteral("MobileStudio/colorWheelSize"),1).toInt(),2);
            m_corner=qBound(0,settings.value(QStringLiteral("MobileStudio/colorWheelCorner"),0).toInt(),3);
            m_history=MobileRecentColors::load();
            m_showHistory=settings.value(QStringLiteral("MobileStudio/colorWheelHistory"),false).toBool();
            m_wheel=new MobileHueWheel(this,12);
            m_wheel->setMinimumSize(0,0);
            m_historyRow=createHistoryRow(this);
            refreshHistory();
            m_wheel->onChosen=[this](const QColor &color,bool) {
                setNativeColor(color);
            };
            connectColorUpdates();
            if(canvasParent)canvasParent->installEventFilter(this);
            updateForViewport();
            refresh();
            return;
        }
        setStyleSheet(
            "QFrame#MobileColorWorkspace{background:#252529;"
            "border:1px solid #48484e;border-radius:13px;}"
            "QToolButton,QPushButton{border:1px solid #55555c;"
            "border-radius:6px;min-height:34px;padding:3px;"
            "background:#34343a;color:#f4f4f5;}"
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
        m_resize=new QToolButton(this);
        m_resize->setText(i18n("Size"));
        m_resize->setToolTip(i18n("Cycle color wheel size"));
        m_resize->setMinimumWidth(32);
        top->addWidget(m_resize);
        connect(m_resize,&QToolButton::clicked,this,[this]() {
            m_sizeLevel=(m_sizeLevel+1)%3;
            QSettings().setValue(
                QStringLiteral("MobileStudio/colorWheelSize"),m_sizeLevel);
            updateForViewport();
        });

        m_move=new QToolButton(this);
        m_move->setText(i18n("Drag"));
        m_move->setToolTip(i18n(
            "Drag to reposition; double-tap to return to bottom right"));
        m_move->setMinimumWidth(32);
        m_move->setAttribute(Qt::WA_AcceptTouchEvents,false);
        m_move->installEventFilter(this);
        top->addWidget(m_move);

        m_collapse=new QToolButton(this);
        m_collapse->setText(QStringLiteral("−"));
        m_collapse->setToolTip(i18n("Minimize color wheel"));
        m_collapse->setMinimumWidth(32);
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
        m_wheel->onChosen=[this](const QColor &color,bool) {
            setNativeColor(color);
        };
        inner->addWidget(createHistoryRow(m_contents));
        auto *advanced=new QToolButton(m_contents);
        advanced->setText(i18n("Advanced color…"));
        advanced->setToolTip(i18n("Native color dialog for precise values"));
        connect(advanced,&QToolButton::clicked,this,[this]() {
            auto *provider=m_engine.brushResources();
            if(!provider)return;
            const QColor chosen=QColorDialog::getColor(
                provider->fgColor().toQColor(),this,i18n("Foreground color"),
                QColorDialog::ShowAlphaChannel);
            if(chosen.isValid())setNativeColor(chosen);
        });
        inner->addWidget(advanced);
        root->addWidget(m_contents);
        if(canvasParent)canvasParent->installEventFilter(this);

        QSettings saved;
        m_sizeLevel=qBound(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelSize"),1).toInt(),2);
        m_rightInset=qMax(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelRight"),7).toInt());
        m_bottomInset=qMax(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelBottom"),7).toInt());
        m_history=MobileRecentColors::load();
        connectColorUpdates();
        refreshHistory();
        setExpanded(saved.value(
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
        if(m_wheelOnly)return;
        const QString colorKey=foreground.name(QColor::HexArgb);
        if(colorKey!=m_lastSwatchColor) {
            m_lastSwatchColor=colorKey;
            m_swatch->setStyleSheet(QStringLiteral(
                "QToolButton{background:%1;color:%2;"
                "border:2px solid #a5c4d8;border-radius:7px;"
                "min-height:35px;padding:3px;}")
                .arg(foreground.name(),foreground.lightness()<130?
                                         QStringLiteral("#ffffff"):
                                         QStringLiteral("#202934")));
        }
    }
    void setWheelHistoryVisible(bool visible)
    {
        if(m_showHistory==visible)return;
        m_showHistory=visible;
        updateForViewport();
    }

    void clearWheelGeometry()
    {
        m_manualWheelPlacement={};
        setGeometry(QRect());
    }

    void setManualWheelPlacement(const QRect &geometry)
    {
        if(geometry==m_manualWheelPlacement)return;
        m_manualWheelPlacement=geometry;
        updateForViewport();
    }

    void setWheelArea(const QRect &area,const QList<QRect> &obstacles)
    {
        if(m_wheelArea==area && m_wheelObstacles==obstacles)return;
        m_wheelArea=area;
        m_wheelObstacles=obstacles;
        updateForViewport();
    }

    bool expanded() const {return m_expanded;}
    void setOverlayClearance(int top,int right)
    {
        if(m_avoidTop==top && m_avoidRight==right)return;
        m_avoidTop=top;
        m_avoidRight=right;
        updateForViewport();
    }
    // Reserve space for narrow-screen bottom sheets without changing
    // the user's preferred color wheel size or expanded state.
    void avoidBottom(int pixels)
    {
        const int limit=qMax(0,pixels);
        if(m_avoidBottom==limit)return;
        m_avoidBottom=limit;
        updateForViewport();
    }
    void setExpanded(bool expanded)
    {
        m_wantsExpanded=expanded;
        QSettings().setValue(
            QStringLiteral("MobileStudio/colorWheelExpanded"),expanded);
        updateForViewport();
        raise();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parentWidget() &&
           (event->type()==QEvent::Resize ||
            event->type()==QEvent::Show)) {
            // Recompute for folded/portrait dimensions. If too small,
            // temporarily collapse without altering user preference.
            updateForViewport();
        }
        if(watched==m_move) {
            if(event->type()==QEvent::MouseButtonDblClick) {
                m_rightInset=7;
                m_bottomInset=7;
                savePosition();
                reposition();
                return true;
            }
            if(event->type()==QEvent::MouseButtonPress) {
                auto *mouse=static_cast<QMouseEvent*>(event);
                if(mouse->button()!=Qt::LeftButton)return false;
                m_dragging=true;
                m_dragStart=mouse->globalPos();
                m_positionAtStart=pos();
                m_move->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            if(event->type()==QEvent::MouseMove && m_dragging) {
                auto *mouse=static_cast<QMouseEvent*>(event);
                QWidget *parent=parentWidget();
                if(!parent)return true;
                const QPoint target=m_positionAtStart+
                    mouse->globalPos()-m_dragStart;
                const int x=qBound(0,target.x(),rightmostPosition());
                const int y=qBound(topmostPosition(),target.y(),
                    qMax(0,parent->height()-height()-m_avoidBottom));
                move(x,y);
                m_rightInset=qMax(0,parent->width()-width()-x);
                m_bottomInset=qMax(0,parent->height()-height()-y);
                return true;
            }
            if(event->type()==QEvent::MouseButtonRelease && m_dragging) {
                m_dragging=false;
                m_move->unsetCursor();
                savePosition();
                return true;
            }
        }
        return QFrame::eventFilter(watched,event);
    }

private:
    static QSize colorPanelSize(int level)
    {
        switch(level) {
        case 0:return QSize(192,285);
        case 2:return QSize(292,394);
        default:return QSize(238,330);
        }
    }
    void updateForViewport()
    {
        auto *parent=parentWidget();
        if(!parent)return;
        if(m_wheelOnly) {
            const QRect bounds=(m_wheelArea.isNull()?parent->rect():m_wheelArea)
                .intersected(parent->rect()).adjusted(1,1,-1,-1);
            const bool history=m_showHistory && !m_history.isEmpty();
            if(m_manualWheelPlacement.isValid()) {
                const auto manual=m_manualWheelPlacement.intersected(parent->rect());
                const int historyHeight=history?qBound(18,manual.width()/7,42)+2:0;
                const int side=qMin(manual.width(),manual.height()-historyHeight);
                if(side>0)applyWheelGeometry(
                    QRect(manual.topLeft(),QSize(side,side+historyHeight)),side,history);
                return;
            }
            const int preferred[]={184,212,252};
            const bool left=m_corner%2;
            const bool top=m_corner>=2;
            QRect placement;
            QRect corner;
            for(int level=m_sizeLevel;level>=0 && !placement.isValid();--level) {
                const int historyReserve=history?30:0;
                const int side=qMin(preferred[level],qMin(bounds.width(),bounds.height()-historyReserve));
                if(side<1)continue;
                const int historyHeight=history?qBound(28,side/7,42)+2:0;
                const int height=qMin(bounds.height(),side+historyHeight);
                const int wheelSize=qMin(side,height-historyHeight);
                if(wheelSize<1)continue;
                const int x=left?bounds.left():bounds.right()-wheelSize+1;
                const int y=top?bounds.top():bounds.bottom()-height+1;
                if(!corner.isValid())corner=QRect(x,y,wheelSize,height);
                QList<int> xs{x},ys{y};
                for(const auto &obstacle:m_wheelObstacles) {
                    xs.append(obstacle.left()-wheelSize-2);
                    xs.append(obstacle.right()+3);
                    ys.append(obstacle.top()-height-2);
                    ys.append(obstacle.bottom()+3);
                }
                int distance=bounds.width()+bounds.height()+1;
                for(int candidateX:xs) {
                    for(int candidateY:ys) {
                        const QRect candidate(candidateX,candidateY,wheelSize,height);
                        const int offset=qAbs(candidateX-x)+qAbs(candidateY-y);
                        if(offset>=distance || !bounds.contains(candidate))continue;
                        if(std::any_of(m_wheelObstacles.cbegin(),m_wheelObstacles.cend(),
                            [&candidate](const QRect &obstacle) {
                                return obstacle.intersects(candidate);
                            }))continue;
                        placement=candidate;
                        distance=offset;
                    }
                }
            }
            if(!placement.isValid())
                placement=MobileOverlayPlacement::nearest(corner,bounds,m_wheelObstacles,
                                                          QSize(96,96),true);
            if(!placement.isValid()) {
                clearWheelGeometry();
                return;
            }
            const int historyHeight=history?qBound(18,placement.width()/7,42)+2:0;
            const int wheelSide=qMin(placement.width(),placement.height()-historyHeight);
            if(wheelSide>0)applyWheelGeometry(
                QRect(placement.topLeft(),QSize(wheelSide,wheelSide+historyHeight)),wheelSide,history);
            return;
        }
        // Prefer the requested size, then progressively smaller sizes.
        // When even the smallest wheel cannot fit, keep a swatch visible.
        const int besideLayers=parent->width()-m_avoidRight;
        const int availableWidth=besideLayers>=colorPanelSize(0).width()+6 ?
            besideLayers : parent->width();
        const int availableHeight=qMax(0,parent->height()-m_avoidBottom-m_avoidTop);
        int level=m_sizeLevel;
        while(level>0 &&
              (colorPanelSize(level).width()+6>availableWidth ||
               colorPanelSize(level).height()+8>availableHeight))
            --level;
        const bool fits=colorPanelSize(level).width()+6<=availableWidth &&
                        colorPanelSize(level).height()+8<=availableHeight;
        const bool showWheel=m_wantsExpanded && fits;
        m_expanded=showWheel;
        m_contents->setVisible(showWheel);
        m_resize->setVisible(showWheel);
        m_move->setVisible(showWheel);
        m_collapse->setVisible(showWheel);
        const QSize desired=showWheel?colorPanelSize(level):QSize(72,53);
        if(size()!=desired)setFixedSize(desired);
        reposition();
    }
    void applyWheelGeometry(const QRect &placement,int wheelSide,bool history)
    {
        setGeometry(placement);
        m_wheel->setGeometry(0,0,wheelSide,wheelSide);
        m_historyRow->setVisible(history);
        m_visibleSwatches=qBound(1,wheelSide/32,8);
        refreshHistory();
        QRegion shape(m_wheel->geometry(),QRegion::Ellipse);
        if(history) {
            m_historyRow->setGeometry(0,wheelSide+2,wheelSide,placement.height()-wheelSide-2);
            shape=shape.united(QRegion(m_historyRow->geometry()));
        }
        setMask(shape);
    }

    void reposition()
    {
        auto *parent=parentWidget();
        if(!parent)return;
        if(m_dragging)return;
        const int x=qBound(0,parent->width()-width()-m_rightInset,
                           rightmostPosition());
        const int availableHeight=qMax(0,parent->height()-m_avoidBottom);
        const int y=qBound(topmostPosition(),availableHeight-height()-m_bottomInset,
                           qMax(0,availableHeight-height()));
        move(x,y);
    }
    int rightmostPosition() const
    {
        return qMax(0,parentWidget()->width()-width()-m_avoidRight);
    }
    int topmostPosition() const
    {
        return qMin(m_avoidTop,qMax(0,parentWidget()->height()-height()-m_avoidBottom));
    }
    void savePosition()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/colorWheelRight"),
                          m_rightInset);
        settings.setValue(QStringLiteral("MobileStudio/colorWheelBottom"),
                          m_bottomInset);
    }
    void setNativeColor(const QColor &color)
    {
        auto *provider=m_engine.brushResources();
        if(!provider || !color.isValid())return;
        const KoColor previous=provider->fgColor();
        if(!previous.colorSpace())return;
        // Preserve the active Krita color space, unlike a standalone
        // QColor state or an RGB-only approximation of the document.
        provider->setFGColor(KoColor(color,previous.colorSpace()));
        refresh();
    }
    QWidget *createHistoryRow(QWidget *parent)
    {
        auto *row=new QWidget(parent);
        auto *layout=new QHBoxLayout(row);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(2);
        for(int i=0;i<8;++i) {
            auto *swatch=new QToolButton(row);
            swatch->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
            if(m_wheelOnly)swatch->setMaximumWidth(44);
            else swatch->setMinimumHeight(28);
            layout->addWidget(swatch,1);
            m_recents.append(swatch);
            connect(swatch,&QToolButton::clicked,this,[this,i]() {
                if(i>=m_history.size())return;
                const QColor color(m_history.at(i));
                if(color.isValid())setNativeColor(color);
            });
        }
        return row;
    }

    void connectColorUpdates()
    {
        auto *provider=m_engine.brushResources();
        if(!provider)return;
        connect(provider,&KisCanvasResourceProvider::sigFGColorChanged,
                this,[this](const KoColor &) {refresh();});
        connect(provider,&KisCanvasResourceProvider::sigMobileStrokeColorsChanged,
                this,[this](const QStringList &colors) {
            const bool wasEmpty=m_history.isEmpty();
            m_history=colors;
            refreshHistory();
            if(m_wheelOnly && m_showHistory && wasEmpty)updateForViewport();
        });
    }
    void refreshHistory()
    {
        for(int i=0;i<m_recents.size();++i) {
            auto *button=m_recents.at(i);
            if(m_wheelOnly)button->setVisible(i<m_history.size() && i<m_visibleSwatches);
            if(i>=m_history.size()) {
                button->setEnabled(false);
                button->setStyleSheet(QStringLiteral(
                    "QToolButton{background:#344a5e;}"));
                continue;
            }
            const QColor color(m_history.at(i));
            button->setEnabled(color.isValid());
            button->setToolTip(color.name(QColor::HexArgb));
            button->setAccessibleName(i18n("Recent color %1",color.name()));
            button->setStyleSheet(QStringLiteral(
                "QToolButton{background:%1;border:1px solid #9bb2c4;"
                "border-radius:5px;}").arg(color.name()));
        }
    }
    MobileEngineBridge m_engine;
    QToolButton *m_swatch=nullptr,*m_collapse=nullptr;
    QToolButton *m_resize=nullptr,*m_move=nullptr;
    QWidget *m_contents=nullptr,*m_historyRow=nullptr;
    MobileHueWheel *m_wheel=nullptr;
    QVector<QToolButton*> m_recents;
    QStringList m_history;
    QString m_lastSwatchColor;
    bool m_expanded=true,m_wantsExpanded=true,m_dragging=false;
    int m_sizeLevel=1,m_corner=0,m_rightInset=7,m_bottomInset=7;
    QRect m_wheelArea,m_manualWheelPlacement;
    QList<QRect> m_wheelObstacles;
    int m_avoidBottom=0,m_avoidTop=0,m_avoidRight=0;
    bool m_wheelOnly=false,m_showHistory=false;
    int m_visibleSwatches=8;
    QPoint m_dragStart,m_positionAtStart;
};
#endif // Q_OS_ANDROID
