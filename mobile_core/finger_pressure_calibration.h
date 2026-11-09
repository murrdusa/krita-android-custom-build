/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-only finger contact-pressure calibration UI. No Krita painting,
 * tool or tablet event code is modified by this dialog.
 */
#pragma once
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSizeF>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <functional>

#include "finger_pressure_settings.h"

class MobileFingerCalibrationPad final : public QFrame
{
public:
    std::function<void(qreal)> onMeasured;
    std::function<void(qreal)> onCompleted;

    explicit MobileFingerCalibrationPad(QWidget *parent=nullptr)
        : QFrame(parent)
    {
        setAttribute(Qt::WA_AcceptTouchEvents,true);
        setMinimumHeight(138);
        setFrameShape(QFrame::StyledPanel);
        setStyleSheet(QStringLiteral(
            "QFrame{background:#34485a;border:2px dashed #7da4c4;"
            "border-radius:10px;}"));
    }

protected:
    bool event(QEvent *incoming) override
    {
        if(incoming->type()!=QEvent::TouchBegin &&
           incoming->type()!=QEvent::TouchUpdate &&
           incoming->type()!=QEvent::TouchEnd &&
           incoming->type()!=QEvent::TouchCancel)
            return QFrame::event(incoming);

        auto *touch=static_cast<QTouchEvent*>(incoming);
        const auto &points=touch->touchPoints();
        if(touch->type()==QEvent::TouchCancel || points.size()!=1 ||
           (touch->device() &&
            touch->device()->type()!=QTouchDevice::TouchScreen) ||
           points.first().flags().testFlag(QTouchEvent::TouchPoint::Pen)) {
            m_samples.clear();
            m_tracking=false;
            incoming->accept();
            return true;
        }

        const auto &point=points.first();
        if(touch->type()==QEvent::TouchBegin ||
           point.state()==Qt::TouchPointPressed) {
            m_samples.clear();
            m_tracking=true;
        }
        if(!m_tracking) {
            incoming->accept();
            return true;
        }

        const QSizeF size=point.ellipseDiameters();
        if(std::isfinite(size.width()) && std::isfinite(size.height()) &&
           size.width()>0 && size.height()>0) {
            const qreal diameter=std::sqrt(size.width()*size.height());
            if(std::isfinite(diameter) && diameter>=1.0 &&
               diameter<=250.0) {
                m_samples.append(diameter);
                if(onMeasured)onMeasured(diameter);
            }
        }

        if(touch->type()==QEvent::TouchEnd ||
           point.state()==Qt::TouchPointReleased) {
            if(!m_samples.isEmpty()) {
                std::sort(m_samples.begin(),m_samples.end());
                // Median rejects spikes from noisy Android touch ellipses.
                const int middle=m_samples.size()/2;
                const qreal measured=m_samples.size()%2?
                    m_samples.at(middle):
                    (m_samples.at(middle-1)+m_samples.at(middle))*0.5;
                if(onCompleted)onCompleted(measured);
            } else if(onCompleted) {
                onCompleted(0.0);
            }
            m_samples.clear();
            m_tracking=false;
        }

        incoming->accept();
        return true;
    }

private:
    QVector<qreal> m_samples;
    bool m_tracking=false;
};

class MobileFingerPressureCalibrationDialog final : public QDialog
{
public:
    explicit MobileFingerPressureCalibrationDialog(QWidget *parent=nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("Calibrate finger pressure"));
        setMinimumWidth(310);
        auto *layout=new QVBoxLayout(this);
        layout->setSpacing(9);

        auto *description=new QLabel(tr(
            "Finger contact size controls simulated pressure. A smaller "
            "contact starts thinner; a larger contact starts thicker. "
            "The minimum maps to 0%, and the maximum to 100% pressure. "
            "Some brushes may make no mark at 0%."),this);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto *minRow=new QHBoxLayout;
        minRow->addWidget(new QLabel(tr("Minimum contact"),this));
        m_min=new QDoubleSpinBox(this);
        m_min->setObjectName(QStringLiteral("fingerPressureMinimum"));
        m_min->setDecimals(1);
        m_min->setRange(1.0,249.0);
        m_min->setSingleStep(0.5);
        m_min->setSuffix(tr(" px"));
        minRow->addWidget(m_min);
        layout->addLayout(minRow);

        auto *maxRow=new QHBoxLayout;
        maxRow->addWidget(new QLabel(tr("Maximum contact"),this));
        m_max=new QDoubleSpinBox(this);
        m_max->setObjectName(QStringLiteral("fingerPressureMaximum"));
        m_max->setDecimals(1);
        m_max->setRange(2.0,250.0);
        m_max->setSingleStep(0.5);
        m_max->setSuffix(tr(" px"));
        maxRow->addWidget(m_max);
        layout->addLayout(maxRow);

        const auto initial=MobileFingerPressureSettings::read(QSettings());
        m_min->setValue(initial.minimum);
        m_max->setValue(initial.maximum);

        auto *capture=new QHBoxLayout;
        auto *captureMin=new QPushButton(tr("Capture minimum"),this);
        auto *captureMax=new QPushButton(tr("Capture maximum"),this);
        captureMin->setMinimumHeight(45);
        captureMax->setMinimumHeight(45);
        capture->addWidget(captureMin);
        capture->addWidget(captureMax);
        layout->addLayout(capture);

        m_instruction=new QLabel(tr(
            "Select a calibration button, then touch the pad below. "
            "You can also edit the numbers manually."),this);
        m_instruction->setWordWrap(true);
        layout->addWidget(m_instruction);

        m_pad=new MobileFingerCalibrationPad(this);
        m_pad->setObjectName(QStringLiteral("fingerPressureContactPad"));
        auto *padLayout=new QVBoxLayout(m_pad);
        auto *padText=new QLabel(tr("TOUCH HERE"),m_pad);
        padText->setAlignment(Qt::AlignCenter);
        padLayout->addWidget(padText);
        layout->addWidget(m_pad);

        m_observed=new QLabel(tr("Contact reading: none yet"),this);
        m_observed->setWordWrap(true);
        layout->addWidget(m_observed);
        m_validation=new QLabel(this);
        m_validation->setWordWrap(true);
        layout->addWidget(m_validation);

        auto *buttons=new QDialogButtonBox(
            QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);
        layout->addWidget(buttons);
        m_save=buttons->button(QDialogButtonBox::Save);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,this,[this]() {
            if(!validCalibration())return;
            QSettings settings;
            settings.setValue(
                QString::fromLatin1(MobileFingerPressureSettings::minKey()),
                m_min->value());
            settings.setValue(
                QString::fromLatin1(MobileFingerPressureSettings::maxKey()),
                m_max->value());
            settings.sync();
            accept();
        });
        connect(m_min,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,[this](double){updateValidation();});
        connect(m_max,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,[this](double){updateValidation();});

        connect(captureMin,&QPushButton::clicked,this,[this]() {
            m_mode=Mode::Minimum;
            m_instruction->setText(tr(
                "Touch the pad with a small/light finger contact, then lift "
                "your finger. The measured median becomes the minimum."));
        });
        connect(captureMax,&QPushButton::clicked,this,[this]() {
            m_mode=Mode::Maximum;
            m_instruction->setText(tr(
                "Touch the pad with a large/flattened finger contact, then "
                "lift your finger. The measured median becomes the maximum."));
        });

        m_pad->onMeasured=[this](qreal diameter) {
            m_observed->setText(tr("Live contact: %1 logical pixels; "
                "estimated pressure: %2%")
                .arg(diameter,0,'f',1)
                .arg(MobileFingerPressureSettings::mapDiameter(diameter,
                    {m_min->value(),m_max->value()})*100.0,0,'f',0));
        };
        m_pad->onCompleted=[this](qreal diameter) {
            if(diameter<1.0) {
                m_observed->setText(tr(
                    "No contact area reported. Enter the min/max values "
                    "manually or use the last-stroke diagnostics."));
                return;
            }
            if(m_mode==Mode::Minimum)
                m_min->setValue(diameter);
            else if(m_mode==Mode::Maximum)
                m_max->setValue(diameter);
            else {
                m_observed->setText(tr(
                    "Measured %1 px. Select Capture minimum or "
                    "Capture maximum to use it.")
                    .arg(diameter,0,'f',1));
                return;
            }
            m_observed->setText(tr("Captured %1: %2 logical pixels.")
                .arg(m_mode==Mode::Minimum?tr("minimum"):tr("maximum"))
                .arg(diameter,0,'f',1));
            m_mode=Mode::None;
            m_instruction->setText(tr(
                "Capture the other end of the range, adjust the numbers "
                "if needed, then Save."));
        };
        updateValidation();
    }

private:
    enum class Mode {None,Minimum,Maximum};
    bool validCalibration() const {
        return MobileFingerPressureSettings::validRange(
            m_min->value(),m_max->value());
    }
    void updateValidation() {
        const bool valid=validCalibration();
        if(m_save)m_save->setEnabled(valid);
        m_validation->setText(valid?
            tr("Calibration applies to the next finger stroke. "
               "Stylus pressure is unchanged."):
            tr("Maximum must be at least 1 logical pixel larger "
               "than minimum."));
    }

    Mode m_mode=Mode::None;
    QDoubleSpinBox *m_min=nullptr;
    QDoubleSpinBox *m_max=nullptr;
    MobileFingerCalibrationPad *m_pad=nullptr;
    QLabel *m_instruction=nullptr;
    QLabel *m_observed=nullptr;
    QLabel *m_validation=nullptr;
    QPushButton *m_save=nullptr;
};
