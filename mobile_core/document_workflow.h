/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile document front end. KritaPart/KisDocument remain the sources of
 * truth for painting, file formats, undo and save reliability.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QScrollArea>
#include <QSizePolicy>
#include "touch_scroll.h"
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <klocalizedstring.h>
#include "engine_bridge.h"

class MobileDocumentWorkflow final
{
public:
    static bool create(const MobileEngineBridge &engine,QWidget *owner)
    {
        if(!owner)return false;
        QDialog dialog(owner);
        dialog.setWindowTitle(i18n("New canvas"));
        MobileTouchScroll::styleMenuDialog(dialog,QSize(460,570));
        // Keep actions visible even on short portrait phones and with
        // Android display scaling. Only the form itself scrolls.
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(6);
        auto *heading=new QLabel(i18n("Create canvas"),&dialog);
        heading->setStyleSheet(
            QStringLiteral("font-size:19px;font-weight:600;"));
        layout->addWidget(heading);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumSize(0,0);
        scroll->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        MobileTouchScroll::configure(scroll);
        auto *formContents=new QWidget(scroll);
        auto *formLayout=new QVBoxLayout(formContents);
        formLayout->setContentsMargins(2,2,2,2);
        formLayout->setSpacing(8);
        auto *form=new QFormLayout;
        form->setSpacing(8);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *name=new QLineEdit(&dialog);
        name->setText(i18n("Untitled"));
        form->addRow(i18n("Name"),name);
        auto *preset=new MobileTouchComboBox(&dialog);
        preset->setAnchoredPopup(true);
        preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        const auto addPreset=[preset](const QString &label,const QSize &size,int dpi=0) {
            preset->addItem(size.isValid()?i18n("%1 — %2 × %3",label,size.width(),size.height()):label,size);
            preset->setItemData(preset->count()-1,dpi,Qt::UserRole+1);
        };
        addPreset(i18n("Custom size"),{});
        const auto *window=owner->window()->windowHandle();
        const auto *screen=window?window->screen():QGuiApplication::primaryScreen();
        if(screen) {
            const QSize pixels=screen->geometry().size()*screen->devicePixelRatio();
            if(pixels.isValid() && pixels.width()<=8192 && pixels.height()<=8192 &&
               qint64(pixels.width())*pixels.height()<=40000000LL)
                addPreset(i18n("Match current screen"),pixels);
        }
        addPreset(i18n("Small square"),QSize(1024,1024));
        addPreset(i18n("Square"),QSize(2048,2048));
        addPreset(i18n("Large square"),QSize(4096,4096));
        addPreset(i18n("Phone portrait"),QSize(1080,1920));
        addPreset(i18n("Phone landscape"),QSize(1920,1080));
        addPreset(i18n("Tall phone"),QSize(1080,2400));
        addPreset(i18n("Tablet landscape"),QSize(2560,1600));
        addPreset(i18n("4K landscape"),QSize(3840,2160));
        addPreset(i18n("A4 at 300 DPI"),QSize(2480,3508),300);
        addPreset(i18n("US Letter at 300 DPI"),QSize(2550,3300),300);
        form->addRow(i18n("Preset"),preset);
        auto *width=new QSpinBox(&dialog);
        auto *height=new QSpinBox(&dialog);
        width->setRange(1,8192);
        height->setRange(1,8192);
        width->setSuffix(i18n(" px"));
        height->setSuffix(i18n(" px"));
        QSettings settings;
        width->setValue(settings.value(
            QStringLiteral("MobileStudio/newWidth"),2048).toInt());
        height->setValue(settings.value(
            QStringLiteral("MobileStudio/newHeight"),2048).toInt());
        form->addRow(i18n("Width"),width);
        form->addRow(i18n("Height"),height);
        auto *dpi=new QSpinBox(&dialog);
        dpi->setRange(72,600);
        dpi->setValue(settings.value(
            QStringLiteral("MobileStudio/newDPI"),300).toInt());
        dpi->setSuffix(i18n(" DPI"));
        form->addRow(i18n("Resolution"),dpi);
        auto *background=new MobileTouchComboBox(&dialog);
        background->setAnchoredPopup(true);
        background->addItem(i18n("Transparent"),QColor(Qt::transparent));
        background->addItem(i18n("White"),QColor(Qt::white));
        background->addItem(i18n("Light gray"),QColor("#cccccc"));
        background->addItem(i18n("Mid gray"),QColor("#808080"));
        background->addItem(i18n("Dark gray"),QColor("#333333"));
        background->addItem(i18n("Black"),QColor(Qt::black));
        background->addItem(i18n("Cream"),QColor("#f5eedc"));
        const QColor previous=settings.value(QStringLiteral("MobileStudio/newBackground"),
            QColor(settings.value(QStringLiteral("MobileStudio/newWhite"),true).toBool()?
                   Qt::white:Qt::transparent)).value<QColor>();
        const int index=background->findData(previous);
        background->setCurrentIndex(index<0?1:index);
        form->addRow(i18n("Background"),background);
        formLayout->addLayout(form);
        formLayout->addStretch(1);
        scroll->setWidget(formContents);
        layout->addWidget(scroll,1);
        QObject::connect(preset,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         &dialog,[preset,width,height,dpi](int){
            const QSize size=preset->currentData().toSize();
            if(size.isValid()) {
                width->setValue(size.width());
                height->setValue(size.height());
                const int resolution=preset->currentData(Qt::UserRole+1).toInt();
                if(resolution>0)dpi->setValue(resolution);
            }
        });
        auto *actions=new QDialogButtonBox(
            QDialogButtonBox::Cancel|QDialogButtonBox::Ok,&dialog);
        actions->button(QDialogButtonBox::Ok)->setText(i18n("Create"));
        actions->button(QDialogButtonBox::Ok)->setFocus();
        layout->addWidget(actions);
        QObject::connect(actions,&QDialogButtonBox::rejected,
                         &dialog,&QDialog::reject);
        QObject::connect(actions,&QDialogButtonBox::accepted,&dialog,[&]() {
            if(qint64(width->value())*height->value()>40000000LL) {
                QMessageBox::warning(&dialog,i18n("Canvas too large"),
                    i18n("This preview currently limits new canvases to "
                         "40 million pixels to avoid excessive memory use."));
                return;
            }
            dialog.accept();
        });
        const QScreen *display=QGuiApplication::primaryScreen();
        const QSize available=display?display->availableGeometry().size():
            owner->window()->size();
        // Never impose a 315px minimum width or 440px minimum height:
        // both can exceed the actual app viewport on narrow devices.
        const int usableWidth=qMax(1,qMin(available.width(),
                                         owner->window()->width())-16);
        const int usableHeight=qMax(1,qMin(available.height(),
                                          owner->window()->height())-16);
        dialog.resize(qMin(525,usableWidth),qMin(600,usableHeight));
        dialog.setMaximumSize(usableWidth,usableHeight);
        if(dialog.exec()!=QDialog::Accepted)return false;
        settings.setValue(QStringLiteral("MobileStudio/newWidth"),
                          width->value());
        settings.setValue(QStringLiteral("MobileStudio/newHeight"),
                          height->value());
        settings.setValue(QStringLiteral("MobileStudio/newDPI"),
                          dpi->value());
        const QColor color=background->currentData().value<QColor>();
        settings.setValue(QStringLiteral("MobileStudio/newBackground"),color);
        if(!engine.createCanvas(name->text(),width->value(),height->value(),
                                dpi->value(),color)) {
            QMessageBox::warning(owner,i18n("Canvas not created"),
                i18n("Krita could not allocate this document. "
                     "Try smaller canvas dimensions."));
            return false;
        }
        return true;
    }
    static bool open(const MobileEngineBridge &engine,QWidget *owner)
    {
        if(!owner)return false;
        return engine.openFiles(owner)>0;
    }
    static bool save(const MobileEngineBridge &engine,QWidget *owner,
                     bool saveAs=false)
    {
        if(!engine.hasDocument())return false;
        return engine.saveCurrentDocument(saveAs);
    }
};
#endif // Q_OS_ANDROID
