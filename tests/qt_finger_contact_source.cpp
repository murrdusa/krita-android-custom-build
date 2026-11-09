/*
 * Qt-only deterministic contact-source regression. Tests intermittent
 * Android raw touch data and Qt fallback without the painting engine.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>
#include "mobile_core/finger_contact_source.h"

static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Finger contact source test failed: %s",reason);
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using Source=MobileFingerContactSource;
    Source::State source;
    // Raw Android reports a contact twice the Qt logical diameter.
    const auto initial=Source::select(source,10.0,20.0,true);
    require(initial.valid() && initial.raw &&
            qAbs(initial.diameter-20.0)<0.0001,
            "raw source was not selected at the start of the stroke");
    require(source.mode==Source::AndroidRaw && source.hasScale &&
            qAbs(source.rawPerQt-2.0)<0.0001,
            "Qt-to-Android contact scale was not learned");
    // Android's next raw sample may miss Qt's current timestamp. The
    // corrected Qt reading must remain on the original raw scale.
    const auto fallback=Source::select(source,11.0,0.0,true);
    require(fallback.valid() && fallback.correctedQtFallback &&
            qAbs(fallback.diameter-22.0)<0.0001,
            "raw dropout caused an uncalibrated source-scale jump");
    const auto resumed=Source::select(source,12.0,24.0,true);
    require(resumed.valid() && resumed.raw &&
            qAbs(resumed.diameter-24.0)<0.0001,
            "raw contact stream did not recover");
    require(qAbs(source.rawPerQt-2.0)<0.0001,
            "stable Qt/raw size scale drifted unexpectedly");

    Source::State qtFirst;
    const auto qtStart=Source::select(qtFirst,10.0,0.0,true);
    const auto lateRaw=Source::select(qtFirst,10.5,26.0,true);
    require(qtStart.valid() && lateRaw.valid() && !lateRaw.raw &&
            qAbs(lateRaw.diameter-10.5)<0.0001 &&
            qtFirst.mode==Source::QtEllipse,
            "raw availability mid-stroke changed an already-selected Qt source");

    Source::State forcedQt;
    const auto qtOnly=Source::select(forcedQt,12.0,24.0,false);
    require(qtOnly.valid() && !qtOnly.raw &&
            qAbs(qtOnly.diameter-12.0)<0.0001,
            "disabling raw preference did not select Qt");

    Source::State delayed;
    require(!Source::select(delayed,0.0,0.0,true).valid() &&
            delayed.mode==Source::Unselected,
            "invalid initial samples incorrectly selected a source");
    const auto firstRaw=Source::select(delayed,0.0,15.0,true);
    require(firstRaw.valid() && firstRaw.raw &&
            delayed.mode==Source::AndroidRaw,
            "raw-only device could not initialize on first valid sample");
    require(!Source::select(delayed,12.0,0.0,true).valid(),
            "raw-only source invented an uncalibrated Qt fallback");
    require(!Source::select(delayed,-1.0,-1.0,true).valid(),
            "invalid contact axes became valid sensor measurements");

    qInfo() << "PASS: per-stroke Qt/raw source lock, density-scale"
            << "alignment on missed Android samples, native data"
            << "recovery, Qt-only preference and invalid data fallback";
    return 0;
}
