/* SPDX-License-Identifier: GPL-2.0-or-later
 * Qt-only checks for the independent mobile toolbar gesture contract.
 */
#include <QApplication>
#include <QAction>
#include <QMouseEvent>
#include <QSettings>
#include "mobile_core/toolbar_drag_gesture.h"

static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Toolbar gesture regression: %s",reason);
}

static void mouse(QWidget *widget,QEvent::Type type,QPoint local)
{
    const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;
    const auto held=type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton;
    const QPoint global=widget->mapToGlobal(local);
    QMouseEvent event(type,local,global,button,held,Qt::NoModifier);
    QApplication::sendEvent(widget,&event);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QSettings().setValue(MobileToolbarDrag::preference("opacity"),
                         QStringLiteral("color"));
    QSettings().setValue(MobileToolbarDrag::preference("size"),
                         QStringLiteral("tool-settings"));
    QSettings().setValue(MobileToolbarDrag::preference("sample"),true);
    int opacityMoves=0, sizeMoves=0, sampled=0, taps=0;
    qreal opacityDelta=0,sizeDelta=0;
    QPoint lastSample;
    const auto adjust=[&](const QString &button,qreal delta) {
        if(button==QStringLiteral("color")) {
            ++opacityMoves;
            opacityDelta+=delta;
        } else if(button==QStringLiteral("tool-settings")) {
            ++sizeMoves;
            sizeDelta+=delta;
        }
    };
    const auto sample=[&](const QPoint &pos) {
        ++sampled;
        lastSample=pos;
    };
    MobileToolbarGestureButton color(nullptr,QStringLiteral("color"),
                                     false,adjust,sample);
    QAction colorAction(QStringLiteral("Color"),&color);
    QObject::connect(&colorAction,&QAction::triggered,&color,[&]() {++taps;});
    color.setDefaultAction(&colorAction);
    color.resize(48,48);
    color.show();
    QApplication::processEvents();

    mouse(&color,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&color,QEvent::MouseButtonRelease,QPoint(20,20));
    require(taps==1 && opacityMoves==0 && sampled==0,
            "stationary tap must open color normally");

    mouse(&color,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&color,QEvent::MouseMove,QPoint(75,20));
    mouse(&color,QEvent::MouseButtonRelease,QPoint(90,20));
    require(taps==1 && opacityMoves>0 && opacityDelta>0 && sampled==0,
            "horizontal color-button swipe must adjust opacity, not sample");

    mouse(&color,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&color,QEvent::MouseMove,QPoint(20,85));
    mouse(&color,QEvent::MouseButtonRelease,QPoint(20,110));
    require(taps==1 && sampled==1 &&
            lastSample==color.mapToGlobal(QPoint(20,110)),
            "perpendicular color swipe must sample at release, not trigger tap");

    MobileToolbarGestureButton settings(nullptr,QStringLiteral("tool-settings"),
                                        true,adjust,sample);
    QAction settingsAction(QStringLiteral("Tool Settings"),&settings);
    settings.setDefaultAction(&settingsAction);
    settings.resize(48,48);
    settings.show();
    QApplication::processEvents();
    mouse(&settings,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&settings,QEvent::MouseMove,QPoint(20,80));
    mouse(&settings,QEvent::MouseButtonRelease,QPoint(20,100));
    require(sizeMoves>0 && sizeDelta>0,
            "vertical toolbar must use vertical displacement for brush size");
    require(MobileToolbarDrag::along(QPoint(5,30),true)==30 &&
            MobileToolbarDrag::along(QPoint(5,30),false)==5,
            "toolbar drag direction must follow bar orientation");
}
