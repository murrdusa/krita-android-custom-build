/* SPDX-License-Identifier: GPL-2.0-or-later
 * Qt-only checks for the independent mobile toolbar gesture contract.
 */
#include <QApplication>
#include <QAction>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QTest>
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
    require(MobileToolbarDrag::assignment("opacity")==QStringLiteral("color") &&
            MobileToolbarDrag::assignment("size")==QStringLiteral("tool-settings"),
            "new workspaces should enable distinct opacity and size drags");
    QSettings().setValue(MobileToolbarDrag::preference("size"),QStringLiteral("tools"));
    MobileToolbarDrag::migrateSizeDefault();
    require(MobileToolbarDrag::assignment("size")==QStringLiteral("tool-settings"),
            "saved legacy default must move to Tool Settings");
    QSettings().setValue(MobileToolbarDrag::preference("size"),QStringLiteral("paint"));
    MobileToolbarDrag::migrateSizeDefault();
    require(MobileToolbarDrag::assignment("size")==QStringLiteral("paint"),
            "a later explicit assignment must survive");
    QSettings().setValue(MobileToolbarDrag::preference("opacity"),QString());
    require(MobileToolbarDrag::assignment("opacity").isEmpty(),"explicit Off was lost");
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

    QSettings().setValue(MobileToolbarDrag::preference("opacity"),QStringLiteral("color"));
    mouse(&color,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&color,QEvent::MouseMove,QPoint(70,20));
    mouse(&color,QEvent::MouseButtonRelease,QPoint(80,20));
    require(commits==1 && parameter==QStringLiteral("opacity") && adjustment==60 && sampled==1,
            "Color must adjust inline while sampling stays perpendicular");
    for(bool vertical:{false,true}) {
        color.setVertical(vertical);
        const int oldPreviews=previews,oldSamples=sampled;
        const int distance=QApplication::startDragDistance();
        const auto point=[vertical](int along,int away) {
            return QPoint(20,20)+(vertical?QPoint(away,along):QPoint(along,away));
        };
        mouse(&color,QEvent::MouseButtonPress,point(0,0));
        mouse(&color,QEvent::MouseMove,point(distance+1,0));
        require(previews==oldPreviews,"small inline wobble changed opacity");
        mouse(&color,QEvent::MouseMove,point(2*distance,3*distance));
        mouse(&color,QEvent::MouseButtonRelease,point(2*distance,5*distance));
        require(previews==oldPreviews && sampled==oldSamples+1,
                "color sampling was stolen by the inline slider");
    }
    color.setVertical(false);
    QSettings().setValue(MobileToolbarDrag::preference("opacity"),QStringLiteral("krita/custom-action"));
    previews=commits=0;

    MobileToolbarGestureButton paint(nullptr,QStringLiteral("krita/custom-action"),false,adjust,sample);
    QAction paintAction(QStringLiteral("Paint"),&paint);
    paint.setDefaultAction(&paintAction);
    paint.resize(48,48);
    paint.show();
    QApplication::processEvents();
    mouse(&paint,QEvent::MouseButtonPress,QPoint(20,20));
    mouse(&paint,QEvent::MouseMove,QPoint(64,20));
    require(previews==1 && commits==0,"slider should preview without committing during drag");
    mouse(&paint,QEvent::MouseButtonRelease,QPoint(104,20));
    require(commits==1 && parameter==QStringLiteral("opacity") && adjustment==84,
            "custom command must adjust along the horizontal toolbar");

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
    int holds=0,arms=0;
    QSettings().setValue(MobileToolbarDrag::preference("sample"),false);
    MobileToolbarGestureButton eyedropper(nullptr,QStringLiteral("eyedropper"),false,adjust,sample,
                                        [&]() {++holds;});
    QAction eyedropperAction(QStringLiteral("Eyedropper"),&eyedropper);
    QObject::connect(&eyedropperAction,&QAction::triggered,&eyedropper,[&]() {++arms;});
    eyedropper.setDefaultAction(&eyedropperAction);
    eyedropper.resize(48,48);
    eyedropper.show();
    const QPoint center(20,20);
    mouse(&eyedropper,QEvent::MouseButtonPress,center);
    mouse(&eyedropper,QEvent::MouseButtonRelease,center);
    require(arms==1 && holds==0,"eyedropper tap must arm once");

    mouse(&eyedropper,QEvent::MouseButtonPress,center);
    QTest::qWait(520);
    mouse(&eyedropper,QEvent::MouseButtonRelease,center);
    require(holds==1 && arms==1,"holding must switch source without arming");

    const int beforeDrag=sampled;
    mouse(&eyedropper,QEvent::MouseButtonPress,center);
    mouse(&eyedropper,QEvent::MouseMove,QPoint(85,20));
    QTest::qWait(520);
    mouse(&eyedropper,QEvent::MouseButtonRelease,QPoint(105,20));
    require(holds==1 && arms==1 && sampled==beforeDrag+1 &&
            lastSample==eyedropper.mapToGlobal(QPoint(105,20)),
            "eyedropper drag must cancel the hold and sample in any direction");

    mouse(&eyedropper,QEvent::MouseButtonPress,center);
    mouse(&eyedropper,QEvent::MouseMove,QPoint(85,20));
    eyedropper.cancelGesture();
    mouse(&eyedropper,QEvent::MouseButtonRelease,QPoint(105,20));
    require(arms==1 && sampled==beforeDrag+1,
            "release after cancellation must not sample or trigger the button");

    // Inline preview can synchronously rebuild the toolbar, deleting
    // the button while update() is still handling this mouse movement.
    QPointer<MobileToolbarGestureButton> previewButton;
    int previewTeardowns=0;
    previewButton=new MobileToolbarGestureButton(nullptr,
        QStringLiteral("tool-settings"),false,
        [&](const QString &,qreal,const QPoint &,Phase phase) {
            if(phase==Phase::Preview) {
                ++previewTeardowns;
                delete previewButton.data();
            }
        },{});
    previewButton->resize(48,48);
    previewButton->show();
    QApplication::processEvents();
    mouse(previewButton.data(),QEvent::MouseButtonPress,QPoint(20,20));
    mouse(previewButton.data(),QEvent::MouseMove,QPoint(90,20));
    require(!previewButton && previewTeardowns==1,
            "toolbar preview deletion must not access a destroyed gesture button");

    // The release/commit path has the same callback lifetime contract.
    QPointer<MobileToolbarGestureButton> commitButton;
    int commitTeardowns=0;
    commitButton=new MobileToolbarGestureButton(nullptr,
        QStringLiteral("tool-settings"),false,
        [&](const QString &,qreal,const QPoint &,Phase phase) {
            if(phase==Phase::Commit) {
                ++commitTeardowns;
                delete commitButton.data();
            }
        },{});
    commitButton->resize(48,48);
    commitButton->show();
    QApplication::processEvents();
    mouse(commitButton.data(),QEvent::MouseButtonPress,QPoint(20,20));
    mouse(commitButton.data(),QEvent::MouseMove,QPoint(90,20));
    mouse(commitButton.data(),QEvent::MouseButtonRelease,QPoint(100,20));
    require(!commitButton && commitTeardowns==1,
            "toolbar commit deletion must not use a freed callback");

    // Holding a button may close the tool panel before the timer's
    // callback completes.
    QPointer<MobileToolbarGestureButton> holdButton;
    int holdTeardowns=0;
    holdButton=new MobileToolbarGestureButton(nullptr,
        QStringLiteral("eyedropper"),false,adjust,sample,[&]() {
            ++holdTeardowns;
            delete holdButton.data();
        });
    holdButton->resize(48,48);
    holdButton->show();
    QApplication::processEvents();
    mouse(holdButton.data(),QEvent::MouseButtonPress,QPoint(20,20));
    QTest::qWait(520);
    require(!holdButton && holdTeardowns==1,
            "toolbar hold deletion must not use a freed callback");

    // Android can hide or deactivate the window during an inline drag.
    // Cancellation may rebuild the workspace and destroy the active
    // button before QWidget's normal event handler is reached.
    QPointer<MobileToolbarGestureButton> cancelButton;
    int cancelTeardowns=0;
    cancelButton=new MobileToolbarGestureButton(nullptr,
        QStringLiteral("tool-settings"),false,
        [&](const QString &,qreal,const QPoint &,Phase phase) {
            if(phase==Phase::Cancel) {
                ++cancelTeardowns;
                delete cancelButton.data();
            }
        },{});
    cancelButton->resize(48,48);
    cancelButton->show();
    QApplication::processEvents();
    mouse(cancelButton.data(),QEvent::MouseButtonPress,QPoint(20,20));
    mouse(cancelButton.data(),QEvent::MouseMove,QPoint(90,20));
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(cancelButton.data(),&deactivate);
    require(!cancelButton && cancelTeardowns==1,
            "window deactivation must not forward an event to a deleted toolbar button");
}
