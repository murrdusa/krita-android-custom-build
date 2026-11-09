/*
 * Qt Widgets smoke test: editable min/max pressure calibration dialog.
 * Compiles and runs offscreen without starting Krita or drawing a stroke.
 */
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QtGlobal>
#include "mobile_core/finger_pressure_calibration.h"

static void require(bool yes,const char *what) {
    if(!yes)qFatal("Finger calibration test failed: %s",what);
}

static void sendContact(MobileFingerCalibrationPad *pad,
                        QTouchDevice *device,QEvent::Type type,
                        qreal diameter,Qt::TouchPointState state)
{
    QTouchEvent::TouchPoint point(1);
    point.setState(state);
    point.setEllipseDiameters(QSizeF(diameter,diameter));
    QTouchEvent event(type,device,Qt::NoModifier,state,{point});
    QApplication::sendEvent(pad,&event);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MobileFingerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Calibration"));
    QTemporaryDir directory;
    require(directory.isValid(),"temporary settings unavailable");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                       directory.path());

    MobileFingerPressureCalibrationDialog dialog;
    auto *minimum=dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMinimum"));
    auto *maximum=dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMaximum"));
    auto *pad=dialog.findChild<MobileFingerCalibrationPad*>(
        QStringLiteral("fingerPressureContactPad"));
    auto *buttons=dialog.findChild<QDialogButtonBox*>();
    require(minimum && maximum && pad && buttons,
            "calibration control missing");
    auto *curve=dialog.findChild<QSlider*>(
        QStringLiteral("fingerPressureCurveGamma"));
    auto *stability=dialog.findChild<QSlider*>(
        QStringLiteral("fingerPressureStability"));
    auto *raw=dialog.findChild<QCheckBox*>(
        QStringLiteral("fingerPressureRawAndroid"));
    auto *scroll=dialog.findChild<QScrollArea*>(
        QStringLiteral("fingerPressureSettingsScroll"));
    require(curve && stability && raw && scroll,
            "response curve / smoothing / raw / scroll controls missing");
    require(curve->value()==100 && stability->value()==50 && raw->isChecked(),
            "incorrect default response settings");
    auto *save=buttons->button(QDialogButtonBox::Save);
    require(save && save->isEnabled(),"default calibration is invalid");
    require(qAbs(minimum->value()-8.0)<0.01 &&
            qAbs(maximum->value()-24.0)<0.01,
            "incorrect default min/max calibration");

    minimum->setValue(35.0);
    maximum->setValue(20.0);
    require(!save->isEnabled(),
            "inverted min/max values must disable Save");
    minimum->setValue(12.0);
    maximum->setValue(35.0);
    require(save->isEnabled(),"valid custom range not accepted");
    curve->setValue(55);
    stability->setValue(80);
    raw->setChecked(false);
    save->click();
    require(dialog.result()==QDialog::Accepted,"Save did not accept dialog");
    const auto calibrated=MobileFingerPressureSettings::read(QSettings());
    const auto response=MobileFingerPressureSettings::readResponse(QSettings());
    require(qAbs(response.gamma-0.55)<0.001 &&
            qAbs(response.stability-0.80)<0.001 && !response.useRawAndroid,
            "curve, stability or raw preference were not saved");
    require(qAbs(calibrated.minimum-12.0)<0.01 &&
            qAbs(calibrated.maximum-35.0)<0.01,
            "calibration changes were not persisted");
    const qreal small=MobileFingerPressureSettings::mapDiameter(
        12.0,calibrated);
    const qreal large=MobileFingerPressureSettings::mapDiameter(
        35.0,calibrated);
    require(qAbs(small-0.0)<0.0001 && qAbs(large-1.0)<0.0001,
            "calibration does not map endpoints to full 0–100% pressure");
    require(qAbs(MobileFingerPressureSettings::mapDiameter(2.0,calibrated))<0.0001 &&
            qAbs(MobileFingerPressureSettings::mapDiameter(100.0,calibrated)-1.0)<0.0001,
            "out-of-range contacts were not clamped at full pressure endpoints");
    require(qAbs(MobileFingerPressureSettings::mapDiameter(23.5,calibrated)-0.5)<0.0001,
            "halfway contact should produce 50% pressure");

    MobileFingerPressureCalibrationDialog reopened;
    auto *remin=reopened.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMinimum"));
    auto *remax=reopened.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMaximum"));
    require(remin && remax &&
            qAbs(remin->value()-12.0)<0.01 &&
            qAbs(remax->value()-35.0)<0.01,
            "saved calibration not restored on reopening");
    require(reopened.findChild<QSlider*>(
            QStringLiteral("fingerPressureCurveGamma"))->value()==55 &&
            reopened.findChild<QSlider*>(
            QStringLiteral("fingerPressureStability"))->value()==80,
            "response and smoothing not restored on reopening");

    // Simulate actual finger contact on the calibration pad, not merely
    // manual spin-box edits: both capture buttons must set the measured
    // median after the finger is released.
    MobileFingerPressureCalibrationDialog touchDialog;
    auto *touchPad=touchDialog.findChild<MobileFingerCalibrationPad*>(
        QStringLiteral("fingerPressureContactPad"));
    auto *captureMin=touchDialog.findChild<QPushButton*>(
        QStringLiteral("fingerCaptureMinimum"));
    auto *captureMax=touchDialog.findChild<QPushButton*>(
        QStringLiteral("fingerCaptureMaximum"));
    auto *captureMedium=touchDialog.findChild<QPushButton*>(
        QStringLiteral("fingerCaptureMedium"));
    auto *touchCurve=touchDialog.findChild<QSlider*>(
        QStringLiteral("fingerPressureCurveGamma"));
    auto *levels=touchDialog.findChild<QLabel*>(
        QStringLiteral("fingerPressureLevels"));
    auto *touchMin=touchDialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMinimum"));
    auto *touchMax=touchDialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("fingerPressureMaximum"));
    require(touchPad && captureMin && captureMax && captureMedium &&
            touchMin && touchMax && touchCurve && levels,
            "touch calibration controls missing");
    QTouchDevice screen;
    screen.setType(QTouchDevice::TouchScreen);
    captureMin->click();
    sendContact(touchPad,&screen,QEvent::TouchBegin,9,
                Qt::TouchPointPressed);
    sendContact(touchPad,&screen,QEvent::TouchUpdate,10,
                Qt::TouchPointMoved);
    sendContact(touchPad,&screen,QEvent::TouchEnd,11,
                Qt::TouchPointReleased);
    require(qAbs(touchMin->value()-10.0)<0.01,
            "minimum touch capture did not save median diameter");
    require(touchPad->lastHoldSamples()==3 &&
            touchPad->lastHoldLevels()==3 &&
            qAbs(touchPad->lastHoldSpread()-std::sqrt(2.0/3.0))<0.01,
            "within-touch raw resolution/spread was not measured");
    captureMax->click();
    sendContact(touchPad,&screen,QEvent::TouchBegin,34,
                Qt::TouchPointPressed);
    sendContact(touchPad,&screen,QEvent::TouchUpdate,35,
                Qt::TouchPointMoved);
    sendContact(touchPad,&screen,QEvent::TouchEnd,36,
                Qt::TouchPointReleased);
    require(qAbs(touchMax->value()-35.0)<0.01,
            "maximum touch capture did not save median diameter");
    captureMedium->click();
    sendContact(touchPad,&screen,QEvent::TouchBegin,14,
                Qt::TouchPointPressed);
    sendContact(touchPad,&screen,QEvent::TouchUpdate,15,
                Qt::TouchPointMoved);
    sendContact(touchPad,&screen,QEvent::TouchEnd,16,
                Qt::TouchPointReleased);
    // Between calibrated 10 and 35px, medium=15px corresponds to
    // position=0.2. Fitted gamma log(.5)/log(.2) ~= 0.43.
    require(touchCurve->value()>=42 && touchCurve->value()<=44,
            "medium contact did not fit the response curve");
    require(touchPad->distinctLevels()>=6 &&
            touchPad->observedSamples()>=9,
            "diagnostics did not count quantized sensor readings");
    require(levels->text().contains("distinct"),
            "sensor level diagnostic label was not refreshed");
    captureMin->click();
    sendContact(touchPad,&screen,QEvent::TouchBegin,0,
                Qt::TouchPointPressed);
    sendContact(touchPad,&screen,QEvent::TouchEnd,0,
                Qt::TouchPointReleased);
    require(qAbs(touchMin->value()-10.0)<0.01,
            "missing contact size corrupted captured calibration");

    qInfo() << "PASS: response curve/stability controls, Android raw toggle,"
            << "live distinct-level graph, three-level calibration,"
            << "touch-pad median capture, unavailable-area safety,"
            << "calibration UI defaults, invalid range validation,"
            << "Save persistence, reopen and full 0–100% pressure endpoints";
    return 0;
}
