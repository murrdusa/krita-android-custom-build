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
#include <QSizeF>
#include <QThread>
#include <QTouchEvent>
#include <QtGlobal>
#include <cmath>
#include "finger_pressure_settings.h"

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
        }
        stroke.lastTimestamp=eventTime;
        if(!stroke.enabled)return originalPressure;

        const QSizeF ellipse=point.ellipseDiameters();
        const qreal width=ellipse.width();
        const qreal height=ellipse.height();
        if(!std::isfinite(width) || !std::isfinite(height) ||
           width<=0 || height<=0) {
            // Some Android drivers return zero / unavailable touch size.
            // Preserve the previous sample instead of injecting a spike.
            return stroke.hasSample?stroke.smoothed:originalPressure;
        }
        const qreal diameter=std::sqrt(width*height);
        if(!std::isfinite(diameter) || diameter<1 || diameter>250)
            return stroke.hasSample?stroke.smoothed:originalPressure;

        if(!stroke.hasSample) {
            // Absolute area calibration. The first dab now reflects the
            // actual finger contact size rather than a fixed 52% baseline.
            stroke.smoothed=MobileFingerPressureSettings::mapDiameter(
                diameter,stroke.range);
            stroke.lastDiameter=diameter;
            stroke.lastSampleTime=eventTime;
            stroke.minDiameter=diameter;
            stroke.maxDiameter=diameter;
            stroke.samples=1;
            stroke.hasSample=true;
            return stroke.smoothed;
        }
        // Absolute contact size relative to the user's calibrated minimum
        // and maximum. Unlike per-stroke normalization, a thumb starts at
        // higher pressure than a pinky when its contact is larger.
        const qreal target=MobileFingerPressureSettings::mapDiameter(
            diameter,stroke.range);

        // Small measurement jitter should not fluctuate brush opacity.
        // Cap sudden jumps, then low-pass filter at ~25% per input sample.
        // Repeated use of the same touch sample stays idempotent.
        if(eventTime==stroke.lastSampleTime &&
           qAbs(diameter-stroke.lastDiameter)<0.001)
            return stroke.smoothed;
        stroke.lastSampleTime=eventTime;
        stroke.lastDiameter=diameter;
        stroke.minDiameter=qMin(stroke.minDiameter,diameter);
        stroke.maxDiameter=qMax(stroke.maxDiameter,diameter);
        ++stroke.samples;
        const qreal error=target-stroke.smoothed;
        if(qAbs(error)>0.015)
            stroke.smoothed=qBound(
                MobileFingerPressureSettings::minPressure(),
                stroke.smoothed+qBound(qreal(-0.18),error,qreal(0.18))*0.26,
                MobileFingerPressureSettings::maxPressure());
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
        qreal lastDiameter=0;
        qreal minDiameter=0;
        qreal maxDiameter=0;
        qreal smoothed=MobileFingerPressureSettings::minPressure();
        int samples=0;
    };
};
