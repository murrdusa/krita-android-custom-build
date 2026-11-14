/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Per-stroke selection between Qt's touch ellipse and Android's raw
 * MotionEvent contact diameter. Different screen-coordinate scales
 * should never cause abrupt brush pressure jumps mid-stroke.
 * This pure Qt helper does not touch event dispatch or pen input.
 */
#pragma once
#include <QtGlobal>
#include <cmath>

class MobileFingerContactSource final
{
public:
    enum Mode {Unselected,QtEllipse,AndroidRaw};
    struct State {
        Mode mode=Unselected;
        qreal rawPerQt=1.0;
        bool hasScale=false;
    };
    struct Reading {
        qreal diameter=0;
        bool raw=false;
        bool correctedQtFallback=false;
        bool valid() const {return diameter>=1.0 && diameter<=250.0;}
    };

    static bool validDiameter(qreal value)
    {
        return std::isfinite(value) && value>=1.0 && value<=250.0;
    }

    static Reading select(State &state,qreal qtDiameter,
                          qreal androidDiameter,bool preferRaw)
    {
        const bool qt=validDiameter(qtDiameter);
        const bool raw=preferRaw && validDiameter(androidDiameter);
        if(state.mode==Unselected) {
            if(raw)state.mode=AndroidRaw;
            else if(qt)state.mode=QtEllipse;
            else return {};
        }

        if(state.mode==QtEllipse) {
            // Stay with Qt for the whole stroke even if the raw JNI
            // bridge becomes available a few samples later.
            return qt?Reading{qtDiameter,false,false}:Reading{};
        }

        if(raw) {
            if(qt) {
                const qreal proposed=androidDiameter/qtDiameter;
                if(std::isfinite(proposed) &&
                   proposed>=0.25 && proposed<=4.0) {
                    if(!state.hasScale) {
                        state.rawPerQt=proposed;
                        state.hasScale=true;
                    } else {
                        // Slow ratio learning rejects noise and does not
                        // repeatedly recalibrate the brush mid-stroke.
                        state.rawPerQt=0.92*state.rawPerQt+
                                       0.08*proposed;
                    }
                }
            }
            return {androidDiameter,true,false};
        }

        if(qt && state.hasScale) {
            const qreal adjusted=qtDiameter*state.rawPerQt;
            if(validDiameter(adjusted))
                return {adjusted,false,true};
        }
        // If the raw stream momentarily disappears and Qt has no
        // usable scale match, hold the previous smoothed pressure.
        return {};
    }
};
