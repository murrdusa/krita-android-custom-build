// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/transient_panel.h"
#include <QMouseEvent>
#include <QPushButton>
#include <QTouchDevice>
#include <QTouchEvent>

class Canvas final : public QWidget
{
public:
    using QWidget::QWidget;
    int touchStarts=0;
protected:
    bool event(QEvent *event) override
    {
        if(event->type()==QEvent::TouchBegin) {
            ++touchStarts;
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }
};

static void click(QWidget *target)
{
    const QPoint point=target->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(target,&press);
    QApplication::sendEvent(target,&release);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QWidget host;
    host.resize(400,700);
    Canvas canvas(&host);
    canvas.setAttribute(Qt::WA_AcceptTouchEvents);
    canvas.setGeometry(host.rect());
    QPushButton nextMenu(&host);
    nextMenu.setGeometry(2,2,80,44);
    host.show();
    auto *panel=new MobileTransientPanel(&canvas);
    auto *star=new QPushButton(panel);
    star->setGeometry(12,12,60,44);
    panel->present();
    int starClicks=0,menuClicks=0;
    QObject::connect(star,&QPushButton::clicked,[&]() {++starClicks;});
    QObject::connect(&nextMenu,&QPushButton::clicked,[&]() {++menuClicks;});
    click(star);
    click(panel);
    if(!panel->isVisible() || starClicks!=1)return 1;
    click(&nextMenu);
    if(panel->isVisible() || menuClicks!=1)return 2;

    panel=new MobileTransientPanel(&canvas);
    panel->present();
    QTouchDevice device;
    device.setType(QTouchDevice::TouchScreen);
    QTouchEvent::TouchPoint point(1);
    point.setState(Qt::TouchPointPressed);
    point.setPos(QPointF(2,2));
    point.setScreenPos(canvas.mapToGlobal(QPoint(2,2)));
    QTouchEvent touch(QEvent::TouchBegin,&device,Qt::NoModifier,
                      Qt::TouchPointPressed,{point});
    QApplication::sendEvent(&canvas,&touch);
    if(panel->isVisible() || canvas.touchStarts!=1)return 3;

    panel=new MobileTransientPanel(&canvas);
    panel->present();
    auto *inside=new QPushButton(panel);
    inside->show();
    QKeyEvent back(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(inside,&back);
    return panel->isVisible()?4:0;
}
