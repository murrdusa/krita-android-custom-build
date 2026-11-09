/*
 * Qt Widgets smoke test: editable min/max pressure calibration dialog.
 * Compiles and runs offscreen without starting Krita or drawing a stroke.
 */
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>
#include "mobile_core/finger_pressure_calibration.h"

static void require(bool yes,const char *what) {
    if(!yes)qFatal("Finger calibration test failed: %s",what);
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
    save->click();
    require(dialog.result()==QDialog::Accepted,"Save did not accept dialog");
    const auto calibrated=MobileFingerPressureSettings::read(QSettings());
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

    qInfo() << "PASS: calibration UI defaults, invalid range validation,"
            << "Save persistence, reopen and absolute pressure endpoints";
    return 0;
}
