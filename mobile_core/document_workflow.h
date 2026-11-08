/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile document front end. KritaPart/KisDocument remain the sources of
 * truth for painting, file formats, undo and save reliability.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#263746;color:#f4f8fd;}"
            "QLabel{color:#e9f3fa;font-size:14px;}"
            "QLineEdit,QComboBox,QSpinBox{background:#364c5f;color:#fff;"
            "border:1px solid #769bb8;border-radius:7px;padding:6px;"
            "min-height:41px;font-size:14px;}"
            "QPushButton{background:#456883;color:white;"
            "border:1px solid #8aafc9;border-radius:7px;"
            "min-height:46px;padding:5px;}"
            "QCheckBox{color:#e9f3fa;font-size:14px;min-height:42px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(12,12,12,12);
        layout->setSpacing(9);
        auto *heading=new QLabel(i18n("Create canvas"),&dialog);
        heading->setStyleSheet(
            QStringLiteral("font-size:19px;font-weight:600;"));
        layout->addWidget(heading);
        auto *description=new QLabel(
            i18n("Native Krita document · RGB 8-bit"),&dialog);
        layout->addWidget(description);
        auto *form=new QFormLayout;
        form->setSpacing(10);
        auto *name=new QLineEdit(&dialog);
        name->setText(i18n("Untitled"));
        form->addRow(i18n("Name"),name);
        auto *preset=new QComboBox(&dialog);
        preset->addItem(i18n("Custom size"),QSize());
        preset->addItem(i18n("Square — 2048"),QSize(2048,2048));
        preset->addItem(i18n("Phone portrait — 1080 × 1920"),QSize(1080,1920));
        preset->addItem(i18n("Phone landscape — 1920 × 1080"),QSize(1920,1080));
        preset->addItem(i18n("A4 at 300 DPI — 2480 × 3508"),QSize(2480,3508));
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
        auto *white=new QCheckBox(i18n("White background layer"),&dialog);
        white->setChecked(settings.value(
            QStringLiteral("MobileStudio/newWhite"),true).toBool());
        form->addRow(i18n("Background"),white);
        layout->addLayout(form);
        auto *note=new QLabel(
            i18n("White creates a locked background and an empty paint layer. "
                 "Turning it off creates one transparent paint layer."),
            &dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        QObject::connect(preset,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         &dialog,[preset,width,height](int){
            const QSize size=preset->currentData().toSize();
            if(size.isValid()) {
                width->setValue(size.width());
                height->setValue(size.height());
            }
        });
        auto *actions=new QDialogButtonBox(
            QDialogButtonBox::Cancel|QDialogButtonBox::Ok,&dialog);
        actions->button(QDialogButtonBox::Ok)->setText(i18n("Create"));
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
        const QSize screen=owner->window()->size();
        dialog.resize(qMax(315,qMin(525,screen.width()-20)),
                      qMax(440,qMin(600,screen.height()-35)));
        if(dialog.exec()!=QDialog::Accepted)return false;
        settings.setValue(QStringLiteral("MobileStudio/newWidth"),
                          width->value());
        settings.setValue(QStringLiteral("MobileStudio/newHeight"),
                          height->value());
        settings.setValue(QStringLiteral("MobileStudio/newDPI"),
                          dpi->value());
        settings.setValue(QStringLiteral("MobileStudio/newWhite"),
                          white->isChecked());
        if(!engine.createCanvas(name->text(),width->value(),height->value(),
                                dpi->value(),white->isChecked())) {
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
