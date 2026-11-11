/* SPDX-License-Identifier: GPL-2.0-or-later
 * Qt-only checks for the independent mobile toolbar gesture contract.
 */
#include <QApplication>
#include <QAction>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
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
    QTemporaryDir config;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,config.path());
    app.setOrganizationName(QStringLiteral("KritaMobileToolbarDragTest"));
    app.setApplicationName(QStringLiteral("KritaMobileToolbarDragTest"));
    QSettings().clear();
    require(MobileToolbarDrag::assignment("opacity").isEmpty() &&
            MobileToolbarDrag::assignment("size").isEmpty(),"sliders must default to off");
    QSettings().setValue(MobileToolbarDrag::preference("opacity"),QStringLiteral("paint"));
    QSettings().setValue(MobileToolbarDrag::preference("size"),QStringLiteral("tool-settings"));
    using MobileToolbarDrag::Phase;
    int previews=0,commits=0,colorPreviews=0,sampled=0,taps=0;
    qreal adjustment=0;
    QPoint lastSample;
    QString parameter;
    const auto adjust=[&](const QString &name,qreal delta,const QPoint &,Phase phase) {
        if(phase==Phase::Preview)++previews;
        if(phase==Phase::Commit) {
            ++commits;
            adjustment=delta;
            parameter=name;
        }
    };
    const auto sample=[&](const QPoint &pos,Phase phase) {
        if(phase==Phase::Preview)++colorPreviews;
        if(phase==Phase::Commit) {
            ++sampled;
            lastSample=pos;
        }
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
    require(taps==1 && previews==0 && sampled==0,
            "stationary tap must open color normally");

    mouse(&color,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&color,QEvent::MouseMove,QPoint(20,85));
    require(colorPreviews==1 && sampled==0,"color swipe should preview before committing");
    mouse(&color,QEvent::MouseButtonRelease,QPoint(20,110));
    require(taps==1 && sampled==1 && lastSample==color.mapToGlobal(QPoint(20,110)),
            "perpendicular color swipe must select the release pixel");

    MobileToolbarGestureButton paint(nullptr,QStringLiteral("paint"),false,adjust,sample);
    QAction paintAction(QStringLiteral("Paint"),&paint);
    paint.setDefaultAction(&paintAction);
    paint.resize(48,48);
    paint.show();
    QApplication::processEvents();
    mouse(&paint,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&paint,QEvent::MouseMove,QPoint(20,-24));
    require(previews==1 && commits==0,"slider should preview without committing during drag");
    mouse(&paint,QEvent::MouseButtonRelease,QPoint(20,-64));
    require(commits==1 && parameter==QStringLiteral("opacity") && adjustment==84,
            "horizontal toolbar opacity control must use upward displacement");

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
    require(commits==2 && parameter==QStringLiteral("size") && adjustment==-80,
            "vertical toolbar brush size must decrease on downward drag");
}
