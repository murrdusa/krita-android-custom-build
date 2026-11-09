/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared Qt-only finger contact-area calibration. Values are measured in
 * the Qt-reported logical-pixel contact diameter sqrt(width * height),
 * NOT physical force or device-independent finger pressure.
 */
#pragma once
#include <QSettings>
#include <QtGlobal>
#include <cmath>

class MobileFingerPressureSettings final
{
public:
    static constexpr const char *enabledKey() {
        return "MobileStudio/fingerContactPressureV1";
    }
    static constexpr const char *minKey() {
        return "MobileStudio/fingerContactMinDiameterV2";
    }
    static constexpr const char *maxKey() {
        return "MobileStudio/fingerContactMaxDiameterV2";
    }
    static constexpr qreal defaultMin() {return 8.0;}
    static constexpr qreal defaultMax() {return 24.0;}
    // Give brushes the full Krita pressure domain. Brush presets can
    // still apply their own pressure floor, opacity and size curves.
    static constexpr qreal minPressure() {return 0.0;}
    static constexpr qreal maxPressure() {return 1.0;}

    struct Range {qreal minimum; qreal maximum;};

    static bool validRange(qreal minimum,qreal maximum) {
        return std::isfinite(minimum) && std::isfinite(maximum) &&
               minimum>=1.0 && maximum<=250.0 &&
               maximum-minimum>=1.0;
    }

    static Range read(const QSettings &settings) {
        const qreal min=settings.value(
            QString::fromLatin1(minKey()),defaultMin()).toDouble();
        const qreal max=settings.value(
            QString::fromLatin1(maxKey()),defaultMax()).toDouble();
        if(!validRange(min,max))
            return {defaultMin(),defaultMax()};
        return {min,max};
    }

    // Linear absolute-size mapping: a thumb may begin a stroke at a higher
    // pressure than a pinky. Clamp outside the user's calibrated range.
    static qreal mapDiameter(qreal diameter,const Range &range) {
        if(!std::isfinite(diameter) ||
           !validRange(range.minimum,range.maximum))return minPressure();
        const qreal normalized=qBound(qreal(0.0),
            (diameter-range.minimum)/(range.maximum-range.minimum),qreal(1.0));
        return minPressure()+(maxPressure()-minPressure())*normalized;
    }
};
