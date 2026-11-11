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
#include <QConicalGradient>
#include <QDialog>
#include "touch_scroll.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
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
        if(m_sat==sat && m_val==value &&
           (hue<0 || m_hue==hue))return;
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
    qreal wheelRadius() const {return qMin(width(),height())/2.0-15.0;}
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
            m_history=settings.value(QStringLiteral("MobileStudio/recentColors")).toStringList();
            while(m_history.size()>8)m_history.removeLast();
            auto *layout=new QVBoxLayout(this);
            layout->setContentsMargins(0,0,0,0);
            m_wheel=new MobileHueWheel(this);
            m_wheel->setMinimumSize(0,0);
            layout->addWidget(m_wheel);
            m_wheel->onChosen=[this](const QColor &color,bool final) {
                setNativeColor(color,final);
            };
            if(auto *provider=m_engine.brushResources()) {
                connect(provider,&KisCanvasResourceProvider::sigFGColorChanged,
                        this,[this](const KoColor &) {refresh();});
            }
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
        m_wheel->onChosen=[this](const QColor &color,bool final) {
            setNativeColor(color,final);
        };
        auto *bottom=new QHBoxLayout;
        bottom->setContentsMargins(0,0,0,0);
        bottom->setSpacing(2);
        for(int i=0;i<8;++i) {
            auto *swatch=new QToolButton(m_contents);
            swatch->setMinimumSize(18,28);
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

        QSettings saved;
        m_sizeLevel=qBound(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelSize"),1).toInt(),2);
        m_rightInset=qMax(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelRight"),7).toInt());
        m_bottomInset=qMax(0,saved.value(
            QStringLiteral("MobileStudio/colorWheelBottom"),7).toInt());
        m_history=saved.value(
            QStringLiteral("MobileStudio/recentColors")).toStringList();
        while(m_history.size()>8)m_history.removeLast();
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
    void openWheelOptions()
    {
        QDialog dialog(window());
        dialog.setWindowTitle(i18n("Color options"));
        auto *layout=new QVBoxLayout(&dialog);
        if(!m_history.isEmpty()) {
            layout->addWidget(new QLabel(i18n("Recent colors"),&dialog));
            auto *swatches=new QGridLayout;
            for(int i=0;i<m_history.size();++i) {
                const QColor color(m_history.at(i));
                if(!color.isValid())continue;
                auto *swatch=new QToolButton(&dialog);
                swatch->setMinimumSize(44,44);
                swatch->setAccessibleName(color.name());
                swatch->setStyleSheet(QStringLiteral(
                    "QToolButton{background:%1;border:1px solid #7e8995;border-radius:4px;}")
                    .arg(color.name()));
                swatches->addWidget(swatch,i/4,i%4);
                connect(swatch,&QToolButton::clicked,&dialog,[this,color,&dialog]() {
                    setNativeColor(color,true);
                    dialog.accept();
                });
            }
            layout->addLayout(swatches);
        }
        layout->addWidget(new QLabel(i18n("Wheel size"),&dialog));
        auto *size=new MobileTouchComboBox(&dialog);
        size->addItems({i18n("Small"),i18n("Medium"),i18n("Large")});
        size->setMinimumHeight(44);
        size->setCurrentIndex(m_sizeLevel);
        layout->addWidget(size);
        connect(size,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[this](int value) {
            m_sizeLevel=value;
            QSettings().setValue(QStringLiteral("MobileStudio/colorWheelSize"),value);
            updateForViewport();
        });
        layout->addWidget(new QLabel(i18n("Wheel position"),&dialog));
        auto *position=new MobileTouchComboBox(&dialog);
        position->addItems({i18n("Bottom right"),i18n("Bottom left"),
                            i18n("Top right"),i18n("Top left")});
        position->setMinimumHeight(44);
        position->setCurrentIndex(m_corner);
        layout->addWidget(position);
        connect(position,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[this](int value) {
            m_corner=value;
            QSettings().setValue(QStringLiteral("MobileStudio/colorWheelCorner"),value);
            updateForViewport();
        });
        auto *advanced=new QPushButton(i18n("Advanced color…"),&dialog);
        advanced->setMinimumHeight(44);
        layout->addWidget(advanced);
        connect(advanced,&QPushButton::clicked,&dialog,[this,&dialog]() {
            if(auto *provider=m_engine.brushResources()) {
                const QColor color=QColorDialog::getColor(provider->fgColor().toQColor(),
                    &dialog,i18n("Foreground color"),QColorDialog::ShowAlphaChannel);
                if(color.isValid()) {
                    setNativeColor(color,true);
                    dialog.accept();
                }
            }
        });
        auto *done=new QPushButton(i18n("Done"),&dialog);
        done->setMinimumHeight(44);
        layout->addWidget(done);
        connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
        dialog.exec();
    }

    void setWheelArea(const QRect &area)
    {
        if(m_wheelArea==area)return;
        m_wheelArea=area;
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
                .intersected(parent->rect()).adjusted(2,2,-2,-2);
            const int preferred[]={184,212,252};
            const int side=qMax(0,qMin(preferred[m_sizeLevel],qMin(bounds.width(),bounds.height())));
            if(size()!=QSize(side,side)) {
                setFixedSize(side,side);
                setMask(QRegion(rect(),QRegion::Ellipse));
            }
            const int x=(m_corner%2)?bounds.left():bounds.right()-side+1;
            const int y=m_corner>=2?bounds.top():bounds.bottom()-side+1;
            move(x,y);
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
    QToolButton *m_resize=nullptr,*m_move=nullptr;
    QWidget *m_contents=nullptr;
    MobileHueWheel *m_wheel=nullptr;
    QVector<QToolButton*> m_recents;
    QStringList m_history;
    QString m_lastSwatchColor;
    bool m_expanded=true,m_wantsExpanded=true,m_dragging=false;
    int m_sizeLevel=1,m_corner=0,m_rightInset=7,m_bottomInset=7;
    QRect m_wheelArea;
    int m_avoidBottom=0,m_avoidTop=0,m_avoidRight=0;
    bool m_wheelOnly=false;
    QPoint m_dragStart,m_positionAtStart;
};
#endif // Q_OS_ANDROID
