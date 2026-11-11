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
#include <QCheckBox>
#include <QCoreApplication>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QSlider>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include "touch_scroll.h"
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

    int distinctLevels() const {return m_distinct.size();}
    int observedSamples() const {return m_history.size();}
    qreal observedNoise() const {
        if(m_history.size()<2)return 0;
        qreal sum=0,sum2=0;
        for(qreal d:m_history) {sum+=d;sum2+=d*d;}
        const qreal n=m_history.size();
        return std::sqrt(qMax(qreal(0),sum2/n-(sum/n)*(sum/n)));
    }
    QString observedSource() const {return m_source;}
    qreal lastHoldSpread() const {return m_holdSpread;}
    int lastHoldLevels() const {return m_holdLevels;}
    int lastHoldSamples() const {return m_holdSamples;}
    void setPreferRaw(bool enabled) {m_preferRaw=enabled;}

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
        qreal diameter=(size.width()>0 && size.height()>0)?
            std::sqrt(size.width()*size.height()):0.0;
        m_source=QStringLiteral("Qt touch ellipse");
        // MainActivity captures MotionEvent contact dimensions before Qt
        // dispatch; the size-only diagnostic is used only when timestamps
        // match. Krita's touch events remain unmodified.
        QCoreApplication *app=QCoreApplication::instance();
        if(app && m_preferRaw) {
            const quint64 rawTime=app->property(
                "_mobileFingerRawTimestamp").toULongLong();
            const quint64 qtTime=touch->timestamp();
            const quint64 delta=rawTime>qtTime?
                rawTime-qtTime:qtTime-rawTime;
            const qreal raw=app->property(
                "_mobileFingerRawDiameter").toDouble();
            if(qtTime>0 && delta<=65 && raw>=1 && raw<=250) {
                diameter=raw;
                m_source=QStringLiteral("Android MotionEvent");
            }
        }
        if(std::isfinite(diameter) && diameter>=1.0 &&
           diameter<=250.0) {
            m_samples.append(diameter);
            if(m_samples.size()>512)m_samples.removeFirst();
            m_history.append(diameter);
            if(m_history.size()>128)m_history.removeFirst();
            m_distinct.insert(qRound(diameter*4));
            update();
            if(onMeasured)onMeasured(diameter);
        }

        if(touch->type()==QEvent::TouchEnd ||
           point.state()==Qt::TouchPointReleased) {
            m_holdSamples=m_samples.size();
            m_holdLevels=0;
            m_holdSpread=0;
            if(!m_samples.isEmpty()) {
                qreal sum=0,sum2=0;
                QSet<int> levels;
                for(qreal d:m_samples) {
                    sum+=d;sum2+=d*d;
                    levels.insert(qRound(d*4));
                }
                m_holdLevels=levels.size();
                const qreal n=m_samples.size();
                m_holdSpread=std::sqrt(qMax(qreal(0),
                    sum2/n-(sum/n)*(sum/n)));
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

    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        if(m_history.size()<2)return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing,true);
        const QRectF graph=rect().adjusted(14,35,-14,-14);
        if(graph.width()<20 || graph.height()<25)return;
        qreal min=1e9,max=0;
        for(qreal d:m_history) {min=qMin(min,d);max=qMax(max,d);}
        const qreal span=qMax(qreal(1.0),max-min);
        QPolygonF line;
        for(int i=0;i<m_history.size();++i) {
            const qreal x=graph.left()+
                graph.width()*i/qMax(1,m_history.size()-1);
            const qreal y=graph.bottom()-
                graph.height()*(m_history.at(i)-min)/span;
            line.append(QPointF(x,y));
        }
        painter.setPen(QPen(QColor(110,220,251),2));
        painter.drawPolyline(line);
        painter.setPen(QColor(240,245,250));
        painter.drawText(rect().adjusted(11,3,-11,-3),
            Qt::AlignTop|Qt::AlignLeft,
            QStringLiteral("%1–%2 px | %3 levels")
            .arg(min,0,'f',1).arg(max,0,'f',1)
            .arg(m_distinct.size()));
    }

private:
    QVector<qreal> m_samples;
    QVector<qreal> m_history;
    QSet<int> m_distinct;
    qreal m_holdSpread=0;
    int m_holdLevels=0,m_holdSamples=0;
    QString m_source=QStringLiteral("Qt");
    bool m_preferRaw=true;
    bool m_tracking=false;
};

// Visual preview of the calibration curve, not a second brush engine.
class MobileFingerResponsePreview final : public QFrame
{
public:
    explicit MobileFingerResponsePreview(QWidget *parent=nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("fingerPressureResponsePreview"));
        setMinimumHeight(92);
        setFrameShape(QFrame::StyledPanel);
    }
    void setGamma(qreal gamma) {
        m_gamma=qBound(qreal(0.3),gamma,qreal(3.0));
        update();
    }
    qreal gamma() const {return m_gamma;}
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing,true);
        const QRectF graph=rect().adjusted(22,9,-12,-17);
        if(graph.width()<40 || graph.height()<40)return;
        painter.setPen(QPen(QColor(130,144,157),1));
        painter.drawLine(graph.bottomLeft(),graph.topLeft());
        painter.drawLine(graph.bottomLeft(),graph.bottomRight());
        painter.setPen(QPen(QColor(120,135,150,110),1,
                            Qt::DashLine));
        painter.drawLine(graph.bottomLeft(),graph.topRight());
        QPolygonF line;
        for(int i=0;i<=64;++i) {
            const qreal x=qreal(i)/64.0;
            const qreal p=std::pow(x,m_gamma);
            line.append(QPointF(graph.left()+x*graph.width(),
                graph.bottom()-p*graph.height()));
        }
        painter.setPen(QPen(QColor(81,192,237),2.5));
        painter.drawPolyline(line);
        painter.setPen(QColor(200,210,221));
        painter.drawText(QRectF(0,0,width(),height()),
            Qt::AlignBottom|Qt::AlignHCenter,
            QStringLiteral("Contact size → simulated pressure"));
    }
private:
    qreal m_gamma=1.0;
};

class MobileFingerPressureCalibrationDialog final : public QDialog
{
public:
    explicit MobileFingerPressureCalibrationDialog(QWidget *parent=nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("Calibrate finger pressure"));
        // Calibration must still open on narrow Android displays.
        const int usable=parent?parent->window()->width()-24:280;
        setMinimumWidth(qMin(280,qMax(1,usable)));
        setStyleSheet(QStringLiteral(
            "QPushButton,QDoubleSpinBox,QCheckBox{min-height:44px;}"
            "QDoubleSpinBox{padding-left:8px;padding-right:40px;}"
            "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button{width:38px;}"
            "QSlider{min-height:44px;}"
            "QSlider::groove:horizontal{height:6px;background:#647180;border-radius:3px;}"
            "QSlider::handle:horizontal{width:26px;margin:-10px 0;"
            "background:#58a8d1;border-radius:13px;}"
            "QCheckBox{spacing:9px;}"
            "QCheckBox::indicator{width:22px;height:22px;}"));
        auto *outer=new QVBoxLayout(this);
        outer->setContentsMargins(7,7,7,7);
        auto *scroll=new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("fingerPressureSettingsScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        MobileTouchScroll::configure(scroll);
        auto *body=new QWidget(scroll);
        auto *layout=new QVBoxLayout(body);
        layout->setSpacing(9);
        outer->addWidget(scroll,1);

        auto *description=new QLabel(tr(
            "Capture light and firm contacts to set the pressure range, "
            "or enter the values below."),this);
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
        const auto response=MobileFingerPressureSettings::readResponse(
            QSettings());
        m_min->setValue(initial.minimum);
        m_max->setValue(initial.maximum);

        auto *curveRow=new QHBoxLayout;
        curveRow->addWidget(new QLabel(tr("Response curve"),this));
        m_curve=new QSlider(Qt::Horizontal,this);
        m_curve->setObjectName(QStringLiteral("fingerPressureCurveGamma"));
        m_curve->setRange(30,300);
        m_curve->setValue(qRound(response.gamma*100.0));
        curveRow->addWidget(m_curve,1);
        m_curveValue=new QLabel(this);
        curveRow->addWidget(m_curveValue);
        layout->addLayout(curveRow);
        m_preview=new MobileFingerResponsePreview(this);
        m_preview->setGamma(m_curve->value()/100.0);
        layout->addWidget(m_preview);
        auto *stabilityRow=new QHBoxLayout;
        stabilityRow->addWidget(new QLabel(tr("Stability"),this));
        m_stability=new QSlider(Qt::Horizontal,this);
        m_stability->setObjectName(QStringLiteral("fingerPressureStability"));
        m_stability->setRange(0,100);
        m_stability->setValue(qRound(response.stability*100.0));
        stabilityRow->addWidget(m_stability,1);
        m_stabilityValue=new QLabel(this);
        stabilityRow->addWidget(m_stabilityValue);
        layout->addLayout(stabilityRow);
        m_useRaw=new QCheckBox(
            tr("Prefer Android raw touch size when available"),this);
        m_useRaw->setObjectName(QStringLiteral("fingerPressureRawAndroid"));
        m_useRaw->setChecked(response.useRawAndroid);
        layout->addWidget(m_useRaw);
        auto updateControls=[this]() {
            m_curveValue->setText(QString::number(
                m_curve->value()/100.0,'f',2));
            m_stabilityValue->setText(
                QStringLiteral("%1%").arg(m_stability->value()));
        };
        connect(m_curve,&QSlider::valueChanged,this,
                [this,updateControls](int value){
                    updateControls();
                    if(m_preview)m_preview->setGamma(value/100.0);
                });
        connect(m_stability,&QSlider::valueChanged,this,
                [updateControls](int){updateControls();});
        updateControls();

        auto *calibration=new QVBoxLayout;
        layout->insertLayout(1,calibration);
        auto *capture=new QHBoxLayout;
        auto *captureMin=new QPushButton(tr("Capture minimum"),this);
        auto *captureMax=new QPushButton(tr("Capture maximum"),this);
        captureMin->setObjectName(QStringLiteral("fingerCaptureMinimum"));
        captureMax->setObjectName(QStringLiteral("fingerCaptureMaximum"));
        captureMin->setMinimumHeight(45);
        captureMax->setMinimumHeight(45);
        capture->addWidget(captureMin);
        capture->addWidget(captureMax);
        calibration->addLayout(capture);
        auto *captureMedium=new QPushButton(
            tr("Capture medium contact (50%)"),this);
        captureMedium->setObjectName(
            QStringLiteral("fingerCaptureMedium"));
        captureMedium->setMinimumHeight(42);
        calibration->addWidget(captureMedium);

        m_instruction=new QLabel(tr(
            "Select a calibration button, then touch the pad below. "
            "You can also edit the numbers manually."),this);
        m_instruction->setWordWrap(true);
        calibration->addWidget(m_instruction);

        m_pad=new MobileFingerCalibrationPad(this);
        m_pad->setPreferRaw(m_useRaw->isChecked());
        connect(m_useRaw,&QCheckBox::toggled,m_pad,
                &MobileFingerCalibrationPad::setPreferRaw);
        m_pad->setObjectName(QStringLiteral("fingerPressureContactPad"));
        auto *padLayout=new QVBoxLayout(m_pad);
        auto *padText=new QLabel(tr("TOUCH HERE"),m_pad);
        // Leave the entire pad as the touch event target. QLabel should
        // never grab the calibration gesture from its parent.
        padText->setAttribute(Qt::WA_TransparentForMouseEvents,true);
        padText->setAlignment(Qt::AlignCenter);
        padLayout->addWidget(padText);
        calibration->addWidget(m_pad);

        m_observed=new QLabel(tr("Contact reading: none yet"),this);
        m_observed->setWordWrap(true);
        calibration->addWidget(m_observed);
        m_levels=new QLabel(tr(
            "Sensor levels: no readings yet. Touch and vary your "
            "finger contact on the pad."),this);
        m_levels->setObjectName(QStringLiteral("fingerPressureLevels"));
        m_levels->setWordWrap(true);
        layout->addWidget(m_levels);
        m_validation=new QLabel(this);
        m_validation->setWordWrap(true);
        layout->addWidget(m_validation);

        auto *buttons=new QDialogButtonBox(
            QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);
        auto *defaults=buttons->addButton(tr("Defaults"),QDialogButtonBox::ResetRole);
        connect(defaults,&QPushButton::clicked,this,[this]() {
            m_min->setValue(MobileFingerPressureSettings::defaultMin());
            m_max->setValue(MobileFingerPressureSettings::defaultMax());
            m_curve->setValue(qRound(MobileFingerPressureSettings::defaultCurve()*100));
            m_stability->setValue(qRound(MobileFingerPressureSettings::defaultStability()*100));
            m_useRaw->setChecked(true);
        });
        layout->addStretch(1);
        scroll->setWidget(body);
        outer->addWidget(buttons);
        // Save/Cancel remain accessible on short phone screens even
        // when the live diagnostic graph and calibration controls scroll.
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
            settings.setValue(
                QString::fromLatin1(MobileFingerPressureSettings::curveKey()),
                m_curve->value()/100.0);
            settings.setValue(
                QString::fromLatin1(MobileFingerPressureSettings::smoothingKey()),
                m_stability->value()/100.0);
            settings.setValue(
                QString::fromLatin1(MobileFingerPressureSettings::rawEnabledKey()),
                m_useRaw->isChecked());
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
        connect(captureMedium,&QPushButton::clicked,this,[this]() {
            m_mode=Mode::Medium;
            m_instruction->setText(tr(
                "Make a medium-sized contact and lift. The measured "
                "median will adjust the response curve so this contact "
                "maps to about 50% pressure. Capture min/max first."));
        });
        connect(captureMax,&QPushButton::clicked,this,[this]() {
            m_mode=Mode::Maximum;
            m_instruction->setText(tr(
                "Touch the pad with a large/flattened finger contact, then "
                "lift your finger. The measured median becomes the maximum."));
        });

        m_pad->onMeasured=[this](qreal diameter) {
            m_observed->setText(tr(
                "Live contact: %1 logical px (%2); mapped pressure: %3%")
                .arg(diameter,0,'f',1)
                .arg(m_pad->observedSource())
                .arg(MobileFingerPressureSettings::mapDiameter(diameter,
                    {m_min->value(),m_max->value()},
                    m_curve->value()/100.0)*100.0,0,'f',0));
            m_levels->setText(tr(
                "%1 observed samples, %2 distinct 0.25px contact levels; "
                "overall spread %3px. More levels permit finer control.")
                .arg(m_pad->observedSamples())
                .arg(m_pad->distinctLevels())
                .arg(m_pad->observedNoise(),0,'f',2));
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
            else if(m_mode==Mode::Medium) {
                const qreal position=(diameter-m_min->value())/
                    (m_max->value()-m_min->value());
                if(position<=0.02 || position>=0.98) {
                    m_observed->setText(tr(
                        "Medium contact must fall between your captured "
                        "minimum and maximum. Try again."));
                    m_mode=Mode::None;
                    return;
                }
                const qreal fitted=std::log(0.5)/std::log(position);
                if(std::isfinite(fitted))
                    m_curve->setValue(qRound(qBound(qreal(0.3),
                        fitted,qreal(3.0))*100.0));
            } else {
                m_observed->setText(tr(
                    "Measured %1 px. Select Capture minimum or "
                    "Capture maximum to use it.")
                    .arg(diameter,0,'f',1));
                return;
            }
            const QString label=m_mode==Mode::Minimum?tr("minimum"):
                m_mode==Mode::Maximum?tr("maximum"):
                tr("medium / response curve");
            m_observed->setText(tr("Captured %1: %2 logical pixels.")
                .arg(label).arg(diameter,0,'f',1));
            m_levels->setText(tr(
                "Last touch: %1 samples, %2 distinct 0.25px levels; "
                "within-touch spread %3px (includes intentional motion).")
                .arg(m_pad->lastHoldSamples())
                .arg(m_pad->lastHoldLevels())
                .arg(m_pad->lastHoldSpread(),0,'f',2));
            m_mode=Mode::None;
            m_instruction->setText(tr(
                "Capture the other end of the range, adjust the numbers "
                "if needed, then Save."));
        };
        updateValidation();
    }

private:
    enum class Mode {None,Minimum,Medium,Maximum};
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
    QSlider *m_curve=nullptr;
    QSlider *m_stability=nullptr;
    QCheckBox *m_useRaw=nullptr;
    QLabel *m_curveValue=nullptr;
    QLabel *m_stabilityValue=nullptr;
    QLabel *m_levels=nullptr;
    MobileFingerResponsePreview *m_preview=nullptr;
    MobileFingerCalibrationPad *m_pad=nullptr;
    QLabel *m_instruction=nullptr;
    QLabel *m_observed=nullptr;
    QLabel *m_validation=nullptr;
    QPushButton *m_save=nullptr;
};
