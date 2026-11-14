// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>
#include "touch_scroll.h"
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <klocalizedstring.h>

inline int mobileConfirmDocumentClose(QWidget *parent,const QString &name)
{
    constexpr int holdDuration=2000;
    if(!parent)return QDialog::Rejected;
    // Android may destroy the owning window during exec(). Parent-owned
    // stack widgets would then be deleted by Qt before their stack
    // frames unwind, potentially corrupting memory.
    const QPointer<QWidget> host(parent);
    QPointer<QDialog> dialog(new QDialog(parent));
    MobileTouchScroll::styleMenuDialog(*dialog,QSize(420,240));
    dialog->setWindowTitle(i18n("Unsaved changes"));
    auto *layout=new QVBoxLayout(dialog.data());
    auto *explanation=new QLabel(i18n("Save changes to %1 before closing?",name),
                                 dialog.data());
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto *save=new QPushButton(i18n("Save"),dialog.data());
    auto *discard=new QPushButton(i18n("Hold to discard"),dialog.data());
    auto *cancel=new QPushButton(i18n("Cancel"),dialog.data());
    auto *progress=new QProgressBar(dialog.data());
    progress->setRange(0,holdDuration);
    progress->setValue(0);
    progress->setTextVisible(false);
    progress->setFixedHeight(6);
    for(auto *button:{save,discard,cancel})button->setMinimumHeight(44);
    layout->addWidget(save);
    layout->addWidget(discard);
    layout->addWidget(progress);
    layout->addWidget(cancel);
    cancel->setFocus();

    // The timer belongs to the dialog, and its callbacks disconnect
    // automatically if the window is destroyed during the hold.
    QElapsedTimer held;
    auto *timer=new QTimer(dialog.data());
    timer->setInterval(25);
    QObject::connect(save,&QPushButton::clicked,dialog.data(),
                     [dialog]() {if(dialog)dialog->done(QMessageBox::Yes);});
    QObject::connect(cancel,&QPushButton::clicked,dialog.data(),&QDialog::reject);
    QObject::connect(discard,&QPushButton::pressed,dialog.data(),
                     [&held,timer]() {
        held.start();
        timer->start();
    });
    QObject::connect(discard,&QPushButton::released,dialog.data(),
                     [timer,progress]() {
        timer->stop();
        progress->reset();
        progress->setValue(0);
    });
    QObject::connect(timer,&QTimer::timeout,dialog.data(),
                     [dialog,discard,timer,progress,&held]() {
        if(!dialog || !discard->isDown() || !dialog->isActiveWindow()) {
            discard->setDown(false);
            timer->stop();
            progress->setValue(0);
            return;
        }
        const auto elapsed=held.elapsed();
        progress->setValue(qMin<qint64>(holdDuration,elapsed));
        if(elapsed>=holdDuration)dialog->done(QMessageBox::No);
    });
    const int result=dialog->exec();
    const bool survived=dialog && host;
    delete dialog.data();
    // Destroyed windows must never be interpreted as Save or Discard.
    return survived?result:QDialog::Rejected;
}
