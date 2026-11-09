/*
 * Qt-only test for Android MotionEvent touch-major/minor input buffer.
 * Native Android Java dispatch is covered separately by APK compilation.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>
#include <cmath>
#include "mobile_core/finger_raw_bridge.h"

static void require(bool valid,const char *message) {
    if(!valid)qFatal("Raw finger touch test failed: %s",message);
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("KritaMobileTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RawContactBridge"));
    QTemporaryDir temp;
    require(temp.isValid(),"isolated settings storage unavailable");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       temp.path());
    QSettings settings;
    settings.setValue(QStringLiteral("MobileStudio/fingerContactPressureV1"),
                      false);
    require(!MobileFingerRawBridge::captureEnabled(1000),
            "raw Android touch sampling enabled without user opt-in");
    settings.setValue(QStringLiteral("MobileStudio/fingerContactPressureV1"),
                      true);
    require(!MobileFingerRawBridge::captureEnabled(1050),
            "cached raw sampling flag ignored its rate limit");
    require(MobileFingerRawBridge::captureEnabled(1220),
            "raw input did not activate after user enabled the experiment");
    settings.setValue(QStringLiteral("MobileStudio/fingerContactPressureV1"),
                      false);
    require(!MobileFingerRawBridge::captureEnabled(1450),
            "raw sampling continued after disable");
    MobileFingerRawBridge::Sample sample;
    // Android major/minor are physical px; density=2 maps to logical px.
    MobileFingerRawBridge::record(2,1000,32,18,2.0,false);
    require(MobileFingerRawBridge::nearest(2,1000,&sample),
            "could not match same pointer and timestamp");
    require(qAbs(sample.diameter-12.0)<0.001 &&
            qAbs(sample.major-16.0)<0.001 &&
            qAbs(sample.minor-9.0)<0.001,
            "raw Android size was not density normalized");
    require(!sample.historical,"current MotionEvent marked historical");
    require(!MobileFingerRawBridge::nearest(3,1000,&sample),
            "raw contact mixed different fingers");
    require(!MobileFingerRawBridge::nearest(2,1100,&sample),
            "raw contact reused a stale sample");
    MobileFingerRawBridge::reset();
    require(!MobileFingerRawBridge::nearest(2,1000,&sample),
            "new gesture reused previous touch size on recycled pointer ID");
    MobileFingerRawBridge::record(2,1000,26,20,2.0,false);
    require(MobileFingerRawBridge::nearest(2,1000,&sample),
            "fresh gesture could not publish its first contact");
    MobileFingerRawBridge::reset();
    require(!MobileFingerRawBridge::nearest(2,1000,&sample),
            "multitouch/cancel reset did not discard single-finger samples");
    require(!MobileFingerRawBridge::nearest(2,0,&sample),
            "raw contact should not guess when Qt has no event time");
    MobileFingerRawBridge::record(2,1010,30,20,2.0,true);
    MobileFingerRawBridge::record(2,1010,40,20,2.0,false);
    require(MobileFingerRawBridge::nearest(2,1010,&sample) &&
            !sample.historical && qAbs(sample.major-20.0)<0.001,
            "current sample did not beat historical sample at same time");
    require(app.property("_mobileFingerRawDiameter").toDouble()>0.0,
            "contact dimensions were not exposed for live diagnostics");
    // Invalid contact dimensions must be ignored.
    MobileFingerRawBridge::record(2,1200,0,0,2.0,false);
    require(!MobileFingerRawBridge::nearest(2,1200,&sample),
            "invalid empty raw contact was accepted");
    // The native ring buffer is bounded; old strokes cannot be matched.
    for(int i=0;i<150;++i)
        MobileFingerRawBridge::record(4,2000+i*8,30,26,2.0,true);
    require(!MobileFingerRawBridge::nearest(2,1000,&sample),
            "raw buffer retained old contacts after its bounded window");
    require(MobileFingerRawBridge::nearest(4,3192,&sample),
            "recent Android historical samples not matched");
    qInfo() << "PASS: opt-in raw capture gate, gesture and multitouch resets, density-adjusted"
            << "raw touch size, pointer identity, historical matching,"
            << "stale/invalid sample rejection and bounded history";
    return 0;
}
