// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <klocalizedstring.h>

inline int mobileConfirmDocumentClose(QWidget *parent,const QString &name)
{
    constexpr int holdDuration=2000;
    QDialog dialog(parent);
    dialog.setWindowTitle(i18n("Unsaved changes"));
    QVBoxLayout layout(&dialog);
    QLabel explanation(i18n("Save changes to %1 before closing?",name),&dialog);
    explanation.setTextFormat(Qt::PlainText);
    explanation.setWordWrap(true);
    layout.addWidget(&explanation);

    QPushButton save(i18n("Save"),&dialog);
    QPushButton discard(i18n("Hold to discard"),&dialog);
    QPushButton cancel(i18n("Cancel"),&dialog);
    QProgressBar progress(&dialog);
    progress.setRange(0,holdDuration);
    progress.setValue(0);
    progress.setTextVisible(false);
    progress.setFixedHeight(6);
    for(auto *button:{&save,&discard,&cancel})button->setMinimumHeight(44);
    layout.addWidget(&save);
    layout.addWidget(&discard);
    layout.addWidget(&progress);
    layout.addWidget(&cancel);
    cancel.setFocus();

    QElapsedTimer held;
    QTimer timer;
    timer.setInterval(25);
    QObject::connect(&save,&QPushButton::clicked,&dialog,[&]() {dialog.done(QMessageBox::Yes);});
    QObject::connect(&cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    QObject::connect(&discard,&QPushButton::pressed,&dialog,[&]() {
        held.start();
        timer.start();
    });
    QObject::connect(&discard,&QPushButton::released,&dialog,[&]() {
        timer.stop();
        progress.reset();
        progress.setValue(0);
    });
    QObject::connect(&timer,&QTimer::timeout,&dialog,[&]() {
        if(!discard.isDown() || !dialog.isActiveWindow()) {
            discard.setDown(false);
            timer.stop();
            progress.setValue(0);
            return;
        }
        const auto elapsed=held.elapsed();
        progress.setValue(qMin<qint64>(holdDuration,elapsed));
        if(elapsed>=holdDuration)dialog.done(QMessageBox::No);
    });
    return dialog.exec();
}
