/*
 * Qt5-only test for the opt-in Android finger-contact pressure prototype.
 * Compiles on Linux without Krita or Android. Synthetic single-finger
 * TouchPoints only; actual device contact-area support must be verified.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QtGlobal>
#include <cmath>

#include "mobile_core/finger_contact_pressure.h"

static void require(bool cond,const char *reason)
{
    if(!cond)qFatal("Finger pressure test failed: %s",reason);
}

static qreal touch(QTouchDevice &device,QEvent::Type type,
                   qreal diameter,ulong timestamp,
                   Qt::TouchPointState state=Qt::TouchPointMoved,
                   int id=1, int count=1,
                   qreal nativePressure=1.0,bool pen=false)
{
    QTouchEvent::TouchPoint p(id);
    p.setState(state);
    if(pen)p.setFlags(QTouchEvent::TouchPoint::Pen);
    p.setEllipseDiameters(QSizeF(diameter,diameter));
    p.setPressure(nativePressure);
    QList<QTouchEvent::TouchPoint> points{p};
    if(count>1) {
        QTouchEvent::TouchPoint second(2);
        second.setState(Qt::TouchPointMoved);
        second.setEllipseDiameters(QSizeF(12,12));
        points.append(second);
    }
    QTouchEvent event(type,&device,Qt::NoModifier,state,points);
    event.setTimestamp(timestamp);
    return MobileFingerContactPressure::pressure(&event,nativePressure);
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("KritaMobileTesting"));
    QCoreApplication::setApplicationName(QStringLiteral("FingerArea"));
    QTemporaryDir temporary;
    require(temporary.isValid(),"failed to create isolated settings store");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       temporary.path());
    QSettings setting;
    const QString key=QString::fromLatin1(
        MobileFingerContactPressure::settingKey());
    QTouchDevice device;
    device.setType(QTouchDevice::TouchScreen);

    // Disabled default: exactly Krita's normal pressure behavior.
    setting.setValue(key,false);
    const qreal native=0.83;
    require(qAbs(touch(device,QEvent::TouchBegin,15,100,
                       Qt::TouchPointPressed,1,1,native)-native)<0.0001,
            "disabled proxy changed original pressure");
    require(qAbs(touch(device,QEvent::TouchEnd,15,130,
                       Qt::TouchPointReleased,1,1,native)-native)<0.0001,
            "disabled proxy changed release pressure");

    setting.setValue(key,true);
    const QString minKey=QString::fromLatin1(
        MobileFingerPressureSettings::minKey());
    const QString maxKey=QString::fromLatin1(
        MobileFingerPressureSettings::maxKey());
    setting.setValue(minKey,8.0);
    setting.setValue(maxKey,24.0);
    const qreal pinky=touch(device,QEvent::TouchBegin,8,600,
                            Qt::TouchPointPressed);
    require(qAbs(pinky-0.16)<0.0001,
            "calibrated minimum did not set initial small-finger pressure");
    touch(device,QEvent::TouchEnd,8,620,Qt::TouchPointReleased);
    const qreal thumb=touch(device,QEvent::TouchBegin,24,700,
                            Qt::TouchPointPressed);
    require(qAbs(thumb-0.96)<0.0001,
            "calibrated maximum did not set initial large-finger pressure");
    require(thumb>pinky+0.75,
            "thumb and pinky incorrectly share the same initial pressure");
    touch(device,QEvent::TouchEnd,24,720,Qt::TouchPointReleased);

    // A new calibration changes the mapping for the next stroke.
    setting.setValue(minKey,10.0);
    setting.setValue(maxKey,30.0);
    require(qAbs(touch(device,QEvent::TouchBegin,15,800,
                       Qt::TouchPointPressed)-0.36)<0.0001,
            "saved custom range not applied to initial dab");
    setting.setValue(minKey,1.0);
    setting.setValue(maxKey,2.0);
    const qreal sameStroke=touch(
        device,QEvent::TouchUpdate,15,810,Qt::TouchPointMoved);
    require(qAbs(sameStroke-0.36)<0.001,
            "changing calibration mid-stroke changed pressure unexpectedly");
    touch(device,QEvent::TouchEnd,15,820,Qt::TouchPointReleased);

    // Restore the normal test range.
    setting.setValue(minKey,8.0);
    setting.setValue(maxKey,24.0);
    const qreal start=touch(device,QEvent::TouchBegin,15,1000,
                            Qt::TouchPointPressed);
    require(start>=0.16 && start<=0.96,"initial pressure outside bounds");
    qreal last=start;
    for(int i=0;i<12;++i) {
        qreal current=touch(device,QEvent::TouchUpdate,23,1010+10*i);
        require(current>=last && current<=last+0.048,
                "expansion did not increase pressure smoothly");
        last=current;
    }
    require(last>start+0.14,"finger expansion had too little response");
    // Identical event samples may be queried more than once by the brush.
    const qreal repeated=touch(device,QEvent::TouchUpdate,23,1120);
    require(qAbs(repeated-last)<0.00001,
            "duplicate sample applied smoothing twice");
    for(int i=0;i<12;++i)last=touch(
        device,QEvent::TouchUpdate,10,1140+10*i);
    require(last<start-0.05,"smaller contact area did not reduce pressure");
    touch(device,QEvent::TouchEnd,10,1280,Qt::TouchPointReleased);
    require(app.property("_mobileFingerAreaLastSampleCount").toInt()>=10,
            "completed stroke did not record valid contact samples");
    require(app.property("_mobileFingerAreaLastMinDiameter").toDouble()<=10,
            "contact diagnostic did not track smallest size");
    require(app.property("_mobileFingerAreaLastMaxDiameter").toDouble()>=23,
            "contact diagnostic did not track largest size");

    // Invalid user settings safely revert to bounded calibration.
    setting.setValue(minKey,25.0);
    setting.setValue(maxKey,15.0);
    const auto fallback=MobileFingerPressureSettings::read(setting);
    require(MobileFingerPressureSettings::validRange(
                fallback.minimum,fallback.maximum),
            "invalid calibration was accepted");
    require(qAbs(fallback.minimum-8.0)<0.001 &&
            qAbs(fallback.maximum-24.0)<0.001,
            "invalid calibration did not use safe defaults");
    setting.setValue(minKey,8.0);
    setting.setValue(maxKey,24.0);

    // Disabling after a stroke must restore the exact native input.
    setting.setValue(key,false);
    require(qAbs(touch(device,QEvent::TouchBegin,15,2000,
                       Qt::TouchPointPressed,1,1,native)-native)<0.00001,
            "toggle OFF did not restore original pressure");
    touch(device,QEvent::TouchEnd,15,2010,Qt::TouchPointReleased);

    // Missing dimensions preserve Krita's native touch pressure.
    setting.setValue(key,true);
    require(qAbs(touch(device,QEvent::TouchBegin,0,3000,
                       Qt::TouchPointPressed,1,1,native)-native)<0.00001,
            "unavailable contact size did not fall back");
    touch(device,QEvent::TouchEnd,0,3010,Qt::TouchPointReleased);
    require(app.property("_mobileFingerAreaLastSampleCount").toInt()==0,
            "unsupported touch dimensions were reported as usable");

    // Even if a device reports a pen through its TouchScreen events,
    // the Qt Pen designation always preserves its original pressure.
    require(qAbs(touch(device,QEvent::TouchBegin,25,3070,
                       Qt::TouchPointPressed,1,1,native,true)-native)<0.00001,
            "pen-type touch was assigned simulated finger pressure");
    touch(device,QEvent::TouchEnd,25,3090,
          Qt::TouchPointReleased,1,1,native,true);

    // Multitouch gestures are never assigned simulated pressure.
    require(qAbs(touch(device,QEvent::TouchUpdate,25,3100,
                       Qt::TouchPointMoved,1,2,native)-native)<0.00001,
            "multitouch gesture pressure was overridden");

    qInfo() << "PASS: absolute thumb/pinky pressures, custom min/max,"
            << "mid-stroke calibration stability, disabled fallback,"
            << "area growth/shrink, smoothing, duplicate idempotence,"
            << "missing size, pen and multitouch safety";
    return 0;
}
