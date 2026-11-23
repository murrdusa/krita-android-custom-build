/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QAction>
#include <QtTest/QTest>
#include "mobile_core/lighting_sphere.h"
#include "mobile_core/idle_fade.h"
#include "mobile_core/toolbar_drag_gesture.h"
static void require(bool ok,const char *reason){if(!ok)qFatal("Sphere/fade regression: %s",reason);}
static void mouse(QWidget *target,QEvent::Type type,QPoint pos,Qt::MouseEventSource source=Qt::MouseEventNotSynthesized)
{
    const bool release=type==QEvent::MouseButtonRelease;
    QMouseEvent event(type,pos,pos,target->mapToGlobal(pos),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                      release?Qt::NoButton:Qt::LeftButton,Qt::NoModifier,source);
    QApplication::sendEvent(target,&event);
}
static void pen(QWidget *target,QEvent::Type type,QPoint pos)
{
    const bool release=type==QEvent::TabletRelease;
    QTabletEvent event(type,pos,target->mapToGlobal(pos),QTabletEvent::Stylus,QTabletEvent::Pen,
        release?0:0.5,0,0,0,0,0,Qt::NoModifier,1,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,release?Qt::NoButton:Qt::LeftButton);
    QApplication::sendEvent(target,&event);
}
static void touch(QWidget *target,QEvent::Type type,QPoint pos)
{
    static QTouchDevice device;
    device.setType(QTouchDevice::TouchScreen);
    const auto state=type==QEvent::TouchBegin?Qt::TouchPointPressed:
                     type==QEvent::TouchEnd?Qt::TouchPointReleased:Qt::TouchPointMoved;
    QTouchEvent::TouchPoint point(0);point.setState(state);point.setPos(pos);point.setScreenPos(target->mapToGlobal(pos));
    QTouchEvent event(type,&device,Qt::NoModifier,state,{point});
    QApplication::sendEvent(target,&event);
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    MobileLightingSphere sphere(nullptr);sphere.resize(160,160);sphere.show();
    int holds=0,samples=0;sphere.onHold=[&](){++holds;};sphere.onColor=[&](const QColor&){++samples;};
    QApplication::processEvents();
    mouse(&sphere,QEvent::MouseButtonPress,{80,80});mouse(&sphere,QEvent::MouseMove,{86,85});QTest::qWait(510);
    mouse(&sphere,QEvent::MouseButtonRelease,{86,85});require(holds==1 && samples==0,"hold jitter became a color pick");
    touch(&sphere,QEvent::TouchBegin,{80,80});QTest::qWait(510);touch(&sphere,QEvent::TouchEnd,{80,80});
    require(holds==2 && samples==0,"finger hold did not fire once");
    pen(&sphere,QEvent::TabletPress,{80,80});pen(&sphere,QEvent::TabletMove,{85,84});QTest::qWait(510);
    pen(&sphere,QEvent::TabletRelease,{85,84});require(holds==3 && samples==0,"pen hold did not fire once");
    mouse(&sphere,QEvent::MouseButtonPress,{80,80});mouse(&sphere,QEvent::MouseMove,{120,80});QTest::qWait(510);
    mouse(&sphere,QEvent::MouseButtonRelease,{120,80});require(holds==3 && samples>0,"intentional sphere drag should sample");
    mouse(&sphere,QEvent::MouseButtonPress,{80,80});sphere.hide();QTest::qWait(510);require(holds==3,"hidden sphere fired hold");
    QAction color("Color",nullptr);int clicks=0,opens=0;QObject::connect(&color,&QAction::triggered,[&](){++clicks;});
    MobileToolbarGestureButton button(nullptr,"color",true,{}, {},[&](){++opens;});button.setDefaultAction(&color);
    button.resize(48,48);button.show();QApplication::processEvents();
    touch(&button,QEvent::TouchBegin,{24,24});touch(&button,QEvent::TouchUpdate,{29,28});QTest::qWait(510);
    touch(&button,QEvent::TouchEnd,{29,28});require(opens==1 && clicks==0,"Color finger hold triggered short tap");
    mouse(&button,QEvent::MouseButtonPress,{24,24},Qt::MouseEventSynthesizedByQt);
    mouse(&button,QEvent::MouseButtonRelease,{24,24},Qt::MouseEventSynthesizedByQt);
    require(clicks==0,"synthesized mouse replay toggled color after hold");
    pen(&button,QEvent::TabletPress,{24,24});QTest::qWait(510);pen(&button,QEvent::TabletRelease,{24,24});
    require(opens==2 && clicks==0,"Color pen hold failed");
    mouse(&button,QEvent::MouseButtonPress,{24,24});mouse(&button,QEvent::MouseButtonRelease,{24,24});
    require(clicks==1,"ordinary Color tap stopped working");
    MobileIdleFade::dim(&button,30);QTest::qWait(260);
    auto *effect=qobject_cast<QGraphicsOpacityEffect*>(button.graphicsEffect());
    require(effect && qAbs(effect->opacity()-0.3)<0.01,"toolbar did not fade");
    MobileIdleFade::wake(&button,true);require(effect->opacity()==1,"input did not restore opacity");
    MobileIdleFade::dim(&button,30);MobileIdleFade::wake(&button,true);QTest::qWait(260);
    require(effect->opacity()==1,"old fade animation continued after input");
    MobileIdleFade::wake(&button,false);require(!button.graphicsEffect(),"disabled fading retained opacity effect");
    qInfo("PASS: sphere and Color holds handle mouse, finger, pen and jitter; fades wake and disable correctly");
}
