/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded, non-persistent native Android MotionEvent contact samples.
 * The Java Activity records ONLY tool type, pointer ID, event uptime,
 * major/minor finger contact dimensions and display density; never
 * coordinates, gestures, images or a history of painted paths.
 *
 * Called on Android's Activity thread before Qt dispatches its event,
 * read later from Krita's KoPointerEvent. Mutex protects both threads.
 */
#pragma once
#include <QCoreApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>
#include <QVector>
#include <QtGlobal>
#include <cmath>
#include "finger_pressure_settings.h"

class MobileFingerRawBridge final
{
public:
    struct Sample {
        int pointerId=-1;
        quint64 uptimeMs=0;
        qreal diameter=0;
        qreal major=0;
        qreal minor=0;
        bool historical=false;
    };

    // Every new Android gesture, multitouch transition or cancellation
    // invalidates the previous single-finger contact history. Android
    // pointer IDs can be reused immediately for the next stroke.
    static void reset()
    {
        Cache &state=cache();
        QMutexLocker locked(&state.mutex);
        state.samples.clear();
        state.lastPublished=0;
    }

    // Called from Android's Java touch dispatcher before recording size
    // samples. Query the same setting as Krita's brush converter,
    // caching it briefly to avoid a filesystem/settings lookup per frame.
    static bool captureEnabled(quint64 uptimeMs)
    {
        struct Gate {
            QMutex mutex;
            bool allowed=false;
            quint64 lastChecked=0;
        };
        static Gate gate;
        QMutexLocker locked(&gate.mutex);
        if(gate.lastChecked==0 || uptimeMs<gate.lastChecked ||
           uptimeMs-gate.lastChecked>=200) {
            gate.allowed=QSettings().value(
                QString::fromLatin1(MobileFingerPressureSettings::enabledKey()),
                MobileFingerPressureSettings::defaultEnabled()).toBool();
            gate.lastChecked=uptimeMs;
        }
        return gate.allowed;
    }

    static void record(int pointerId,quint64 uptimeMs,
                       qreal majorPixels,qreal minorPixels,qreal density,
                       bool historical)
    {
        if(pointerId<0 || !std::isfinite(majorPixels) ||
           !std::isfinite(minorPixels) || majorPixels<=0 ||
           minorPixels<=0 || majorPixels>1500 || minorPixels>1500)
            return;
        if(!std::isfinite(density) || density<0.25 || density>8.0)
            density=1.0;
        const qreal major=majorPixels/density;
        const qreal minor=minorPixels/density;
        const qreal diameter=std::sqrt(major*minor);
        if(!std::isfinite(diameter) || diameter<1 || diameter>250)return;

        bool publish=false;
        {
            Cache &state=cache();
            QMutexLocker locked(&state.mutex);
            state.samples.append({pointerId,uptimeMs,diameter,major,minor,
                                  historical});
            // A short bounded history lets us match Qt event timestamps
            // without holding unbounded personal input history in memory.
            if(state.samples.size()>96)
                state.samples.remove(0,state.samples.size()-96);
            if(!historical && (uptimeMs>=state.lastPublished+40 ||
                               uptimeMs<state.lastPublished)) {
                state.lastPublished=uptimeMs;
                publish=true;
            }
        }
        if(!publish)return;
        QCoreApplication *app=QCoreApplication::instance();
        if(!app)return;
        // Do not mutate QObject properties from Android's Java UI thread.
        // The preview reads these privacy-safe, size-only diagnostics.
        const auto update=[app,major,minor,diameter,uptimeMs]() {
            app->setProperty("_mobileFingerRawMajor",major);
            app->setProperty("_mobileFingerRawMinor",minor);
            app->setProperty("_mobileFingerRawDiameter",diameter);
            app->setProperty("_mobileFingerRawTimestamp",
                             static_cast<qulonglong>(uptimeMs));
        };
        if(QThread::currentThread()==app->thread())
            update();
        else
            QMetaObject::invokeMethod(app,update,Qt::QueuedConnection);
    }

    static bool nearest(int pointerId,quint64 qtTimeMs,Sample *out)
    {
        if(!out || pointerId<0 || qtTimeMs==0)return false;
        Cache &state=cache();
        QMutexLocker locked(&state.mutex);
        quint64 closest=51;
        bool found=false;
        for(int i=state.samples.size()-1;i>=0;--i) {
            const Sample &sample=state.samples.at(i);
            if(sample.pointerId!=pointerId)continue;
            const quint64 delta=sample.uptimeMs>qtTimeMs?
                sample.uptimeMs-qtTimeMs:qtTimeMs-sample.uptimeMs;
            if(delta>50 || delta>closest)continue;
            if(delta==closest && sample.historical)continue;
            closest=delta;
            *out=sample;
            found=true;
            if(delta==0 && !sample.historical)break;
        }
        return found;
    }

private:
    struct Cache {
        QMutex mutex;
        QVector<Sample> samples;
        quint64 lastPublished=0;
    };
    static Cache &cache() {
        static Cache state;
        return state;
    }
};
