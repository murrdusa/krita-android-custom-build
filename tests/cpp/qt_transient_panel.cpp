// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_studio/transient_panel.h"
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
    if(panel->isVisible())return 4;

    QToolButton opener(&host);
    opener.setGeometry(90,2,80,44);
    opener.show();
    const auto openerConnection=QObject::connect(&opener,&QToolButton::clicked,[&]() {
        if(MobileTransientPanel::closeForAnchor(&canvas,&opener))return;
        panel=new MobileTransientPanel(&canvas);
        panel->anchorTo(&opener,QSize(220,170));
        panel->present();
    });
    click(&opener);
    if(!panel->isVisible())return 5;
    auto *original=panel;
    // Simulate Android delivering the raw touch to a parent before the
    // synthesized mouse pair reaches the actual opener.
    point.setScreenPos(opener.mapToGlobal(opener.rect().center()));
    QTouchEvent openerTouch(QEvent::TouchBegin,&device,Qt::NoModifier,
                            Qt::TouchPointPressed,{point});
    QApplication::sendEvent(&host,&openerTouch);
    if(!panel->isVisible())return 6;
    click(&opener);
    if(panel!=original || panel->isVisible())return 7;
    click(&opener);
    if(!panel->isVisible())return 8;
    click(&nextMenu);
    if(panel->isVisible() || menuClicks!=2)return 9;

    QObject::disconnect(openerConnection);
    QAction action(&host);
    opener.setDefaultAction(&action);
    QToolButton alias(&host);
    alias.setDefaultAction(&action);
    alias.setGeometry(180,2,80,44);
    alias.show();
    panel=new MobileTransientPanel(&canvas);
    panel->setOpener(nullptr,&action);
    panel->present();
    QObject::connect(&action,&QAction::triggered,[&]() {
        if(panel->isVisible())panel->close();
    });
    QMouseEvent press(QEvent::MouseButtonPress,alias.rect().center(),
                      Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&alias,&press);
    if(!panel->isVisible())return 10;
    QMouseEvent release(QEvent::MouseButtonRelease,alias.rect().center(),
                        Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&alias,&release);
    if(panel->isVisible())return 11;

    panel=new MobileTransientPanel(&canvas);
    panel->present();
    canvas.hide();
    canvas.show();
    return panel->isVisible()?12:0;
}
