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
    static constexpr const char *curveKey() {
        return "MobileStudio/fingerPressureCurveGammaV3";
    }
    static constexpr const char *smoothingKey() {
        return "MobileStudio/fingerPressureStabilityV3";
    }
    static constexpr const char *rawEnabledKey() {
        return "MobileStudio/fingerPressureRawAndroidV3";
    }
    static constexpr qreal defaultCurve() {return 1.0;}
    static constexpr qreal defaultStability() {return 0.5;}
    static constexpr qreal defaultMin() {return 8.0;}
    static constexpr qreal defaultMax() {return 24.0;}
    // Give brushes the full Krita pressure domain. Brush presets can
    // still apply their own pressure floor, opacity and size curves.
    static constexpr qreal minPressure() {return 0.0;}
    static constexpr qreal maxPressure() {return 1.0;}

    struct Range {qreal minimum; qreal maximum;};
    struct Response {
        qreal gamma=defaultCurve();
        qreal stability=defaultStability();
        bool useRawAndroid=true;
    };
    static Response readResponse(const QSettings &settings) {
        Response result;
        const qreal gamma=settings.value(
            QString::fromLatin1(curveKey()),defaultCurve()).toDouble();
        const qreal stability=settings.value(
            QString::fromLatin1(smoothingKey()),defaultStability()).toDouble();
        result.gamma=(std::isfinite(gamma) && gamma>=0.3 && gamma<=3.0)?
            gamma:defaultCurve();
        result.stability=(std::isfinite(stability) &&
            stability>=0.0 && stability<=1.0)?
            stability:defaultStability();
        result.useRawAndroid=settings.value(
            QString::fromLatin1(rawEnabledKey()),true).toBool();
        return result;
    }

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

    // Keep default linear for existing settings. An exponent below 1
    // emphasizes small contacts; above 1 emphasizes larger contacts.
    // This remaps existing sensor information; it cannot create raw
    // resolution that the touchscreen does not provide.
    static qreal mapDiameter(qreal diameter,const Range &range,
                             qreal gamma=defaultCurve()) {
        if(!std::isfinite(diameter) ||
           !validRange(range.minimum,range.maximum))return minPressure();
        if(!std::isfinite(gamma) || gamma<0.3 || gamma>3.0)
            gamma=defaultCurve();
        const qreal normalized=qBound(qreal(0.0),
            (diameter-range.minimum)/(range.maximum-range.minimum),qreal(1.0));
        return std::pow(normalized,gamma);
    }

};
