/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Experimental Android finger-contact-area pressure proxy (v1).
 * Krita's tablet events are not changed. This code only runs for a
 * single touchscreen contact when explicitly enabled in Mobile Studio.
 *
 * Contact area is NOT a calibrated measure of force. Android devices can
 * omit ellipse dimensions or report noisy sizes. Never synthesize pen
 * events, override multitouch gestures, or modify incoming touch points.
 */
#pragma once

#include <QCoreApplication>
#include <QSettings>
#include <QSet>
#include <QSizeF>
#include <QThread>
#include <QTouchEvent>
#include <QtGlobal>
#include <cmath>
#include "finger_pressure_settings.h"
#include "finger_contact_source.h"
#ifdef Q_OS_ANDROID
#include "finger_raw_bridge.h"
#endif

class MobileFingerContactPressure final
{
public:
    static constexpr const char *settingKey()
    {
        return MobileFingerPressureSettings::enabledKey();
    }

    static qreal pressure(const QTouchEvent *event,qreal originalPressure)
    {
        // One independent state per dispatch thread. This function is
        // invoked by Krita's KoPointerEvent when a brush consumes input.
        // It never alters QTouchEvent or QTabletEvent event routing.
        static thread_local Stroke stroke;

        if(!event || event->touchPoints().size()!=1 ||
           (event->device() &&
            event->device()->type()!=QTouchDevice::TouchScreen)) {
            stroke=Stroke{};
            return originalPressure;
        }

        const QTouchEvent::TouchPoint &point=event->touchPoints().first();
        // Some Android drivers send pens through their touchscreen source.
        // Respect the Qt Pen flag as well as the QTabletEvent separation.
        if(point.flags().testFlag(QTouchEvent::TouchPoint::Pen)) {
            stroke=Stroke{};
            return originalPressure;
        }
        const bool released=event->type()==QEvent::TouchEnd ||
            event->type()==QEvent::TouchCancel ||
            point.state()==Qt::TouchPointReleased;
        if(released) {
            const qreal finalPressure=stroke.active && stroke.enabled &&
                stroke.hasSample && stroke.pointId==point.id()?
                stroke.smoothed:originalPressure;
            // One in-memory diagnostic per completed stroke. This avoids
            // per-event settings writes and never records coordinates,
            // images, paths or stylus pressure. The UI can report whether
            // the device/Qt platform actually supplied a contact ellipse.
            if(stroke.active && stroke.enabled &&
               stroke.pointId==point.id()) {
                QCoreApplication *app=QCoreApplication::instance();
                if(app && QThread::currentThread()==app->thread()) {
                    app->setProperty("_mobileFingerAreaLastSampleCount",
                                     stroke.samples);
                    app->setProperty("_mobileFingerAreaLastMinDiameter",
                                     stroke.minDiameter);
                    app->setProperty("_mobileFingerAreaLastMaxDiameter",
                                     stroke.maxDiameter);
                    app->setProperty("_mobileFingerAreaLastPressure",
                                     finalPressure);
                    app->setProperty("_mobileFingerAreaLastUniqueSteps",
                                     stroke.uniqueBins.size());
                    app->setProperty("_mobileFingerAreaLastRawSamples",
                                     stroke.rawSamples);
                    app->setProperty("_mobileFingerAreaLastQtSamples",
                                     stroke.qtSamples);
                    app->setProperty("_mobileFingerAreaLastAlignedFallbacks",
                                     stroke.alignedFallbacks);
                    const QString source=stroke.source.mode==
                        MobileFingerContactSource::AndroidRaw?
                        (stroke.usedCorrectedQt?
                         QStringLiteral("Android raw / aligned Qt fallback"):
                         QStringLiteral("Android raw touch")):
                        QStringLiteral("Qt touch ellipse");
                    app->setProperty("_mobileFingerAreaLastSource",source);
                }
            }
            stroke=Stroke{};
            return finalPressure;
        }

        const ulong eventTime=event->timestamp();
        const bool timedOut=stroke.active && eventTime>stroke.lastTimestamp &&
                            eventTime-stroke.lastTimestamp>500;
        if(!stroke.active || stroke.pointId!=point.id() ||
           point.state()==Qt::TouchPointPressed || timedOut) {
            stroke=Stroke{};
            stroke.active=true;
            stroke.pointId=point.id();
            // Load once per stroke, never on every high-rate brush sample.
            // The user can toggle the experiment between strokes.
            // Both enabled state and calibration are snapshotted at the
            // start of each stroke. Changes during a stroke cannot cause
            // a pressure jump half-way through the painted line.
            QSettings settings;
            stroke.enabled=settings.value(
                QString::fromLatin1(settingKey()),false).toBool();
            stroke.range=MobileFingerPressureSettings::read(settings);
            stroke.response=MobileFingerPressureSettings::readResponse(settings);
        }
        stroke.lastTimestamp=eventTime;
        if(!stroke.enabled)return originalPressure;

        const QSizeF ellipse=point.ellipseDiameters();
        // Qt may use (-1,-1) for unavailable dimensions. Their product
        // is positive, so reject each axis before taking the square root.
        const qreal qtDiameter=(ellipse.width()>0 && ellipse.height()>0)?
            std::sqrt(ellipse.width()*ellipse.height()):0.0;
        qreal rawDiameter=0;
#ifdef Q_OS_ANDROID
        // MotionEvent history and raw touch-major/minor are captured in
        // MainActivity before Qt dispatch. Match pointer ID and event
        // uptime; stale/multi-finger data must never affect a brush.
        if(stroke.response.useRawAndroid) {
            MobileFingerRawBridge::Sample sample;
            if(MobileFingerRawBridge::nearest(point.id(),
                static_cast<quint64>(eventTime),&sample)) {
                rawDiameter=sample.diameter;
            }
        }
#endif
        const auto measurement=MobileFingerContactSource::select(
            stroke.source,qtDiameter,rawDiameter,
            stroke.response.useRawAndroid);
        if(!measurement.valid()) {
            // Missing or mismatched sources must not inject false brush
            // pressure. Keep the last valid smoothed result, if any.
            return stroke.hasSample?stroke.smoothed:originalPressure;
        }
        const qreal diameter=measurement.diameter;
        if(measurement.correctedQtFallback) {
            stroke.usedCorrectedQt=true;
            ++stroke.alignedFallbacks;
        }
        if(measurement.raw)++stroke.rawSamples;
        else ++stroke.qtSamples;

        if(!stroke.hasSample) {
            // Absolute area calibration. The first dab now reflects the
            // actual finger contact size rather than a fixed 52% baseline.
            stroke.smoothed=MobileFingerPressureSettings::mapDiameter(
                diameter,stroke.range,stroke.response.gamma);
            stroke.lastTarget=stroke.smoothed;
            stroke.previousRaw=stroke.smoothed;
            stroke.lastRaw=stroke.smoothed;
            stroke.lastDiameter=diameter;
            stroke.lastSampleTime=eventTime;
            stroke.minDiameter=diameter;
            stroke.maxDiameter=diameter;
            stroke.samples=1;
            stroke.uniqueBins.insert(qRound(diameter*4.0));
            stroke.hasSample=true;
            return stroke.smoothed;
        }
        // Absolute contact size relative to the user's calibrated minimum
        // and maximum. Unlike per-stroke normalization, a thumb starts at
        // higher pressure than a pinky when its contact is larger.
        const qreal raw=MobileFingerPressureSettings::mapDiameter(
            diameter,stroke.range,stroke.response.gamma);

        // Repeated use of the same touch sample stays idempotent.
        if(eventTime==stroke.lastSampleTime &&
           qAbs(diameter-stroke.lastDiameter)<0.001)
            return stroke.smoothed;
        const qreal elapsed=qBound(qreal(4.0),
            eventTime>stroke.lastSampleTime?
                qreal(eventTime-stroke.lastSampleTime):qreal(16.0),
            qreal(50.0));
        stroke.lastSampleTime=eventTime;
        stroke.lastDiameter=diameter;
        stroke.minDiameter=qMin(stroke.minDiameter,diameter);
        stroke.maxDiameter=qMax(stroke.maxDiameter,diameter);
        stroke.uniqueBins.insert(qRound(diameter*4.0));
        ++stroke.samples;
        // Reject isolated contact-size jumps before the adaptive filter.
        const qreal target=qMax(qMin(stroke.previousRaw,stroke.lastRaw),
            qMin(qMax(stroke.previousRaw,stroke.lastRaw),raw));
        stroke.previousRaw=stroke.lastRaw;
        stroke.lastRaw=raw;
        const qreal motion=qAbs(target-stroke.lastTarget);
        if(motion<0.06)
            stroke.noise=qBound(qreal(0.002),
                stroke.noise*0.9+motion*0.1,qreal(0.04));
        else
            stroke.noise=qMax(qreal(0.002),stroke.noise*0.98);
        stroke.lastTarget=target;
        const qreal error=target-stroke.smoothed;
        const qreal alpha=MobileFingerPressureSettings::adaptiveAlpha(
            error,stroke.noise,stroke.response.stability,elapsed);
        // Limit how far adjacent samples can move even when quantized
        // contact sizes make the adaptive filter see a large change.
        const qreal maxStep=elapsed*(0.009-0.006*stroke.response.stability);
        stroke.smoothed+=qBound(-maxStep,error*alpha,maxStep);
        if((target==0.0 || target==1.0) &&
           qAbs(target-stroke.smoothed)<0.002)
            stroke.smoothed=target;
        return stroke.smoothed;
    }

private:
    struct Stroke {
        bool active=false;
        bool enabled=false;
        bool hasSample=false;
        int pointId=-1;
        ulong lastTimestamp=0;
        ulong lastSampleTime=0;
        MobileFingerPressureSettings::Range range{
            MobileFingerPressureSettings::defaultMin(),
            MobileFingerPressureSettings::defaultMax()};
        MobileFingerPressureSettings::Response response;
        QSet<int> uniqueBins;
        qreal noise=0.006;
        qreal lastTarget=0;
        qreal previousRaw=0;
        qreal lastRaw=0;
        MobileFingerContactSource::State source;
        bool usedCorrectedQt=false;
        int rawSamples=0;
        int qtSamples=0;
        int alignedFallbacks=0;
        qreal lastDiameter=0;
        qreal minDiameter=0;
        qreal maxDiameter=0;
        qreal smoothed=MobileFingerPressureSettings::minPressure();
        int samples=0;
    };
};
