// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSettings>
#include <QSysInfo>
#include <QThreadPool>

namespace MobileCrashReport
{
inline void rememberPreviousSession(QSettings &settings)
{
    const auto phase=settings.value(QStringLiteral("MobileStudio/standaloneLastPhase"));
    const auto step=settings.value(QStringLiteral("MobileStudio/crashCheckpoint"));
    settings.setValue(QStringLiteral("MobileStudio/previousCrashPhase"),phase);
    settings.setValue(QStringLiteral("MobileStudio/previousCrashCheckpoint"),step);
    settings.remove(QStringLiteral("MobileStudio/crashCheckpoint"));
}

inline void checkpoint(const QString &step)
{
    QSettings settings;
    settings.setValue(QStringLiteral("MobileStudio/crashCheckpoint"),step);
    settings.sync();
}

inline void copy()
{
    const QSettings settings;
    const QString header=QStringLiteral(
        "Krita Mobile Studio crash report\nVersion: %1\nDevice: %2, %3\n"
        "Previous launch startup phase: %4\nPrevious launch canvas/tool checkpoint: %5\n"
        "Current canvas/tool checkpoint: %6\n\n")
        .arg(QCoreApplication::applicationVersion(),QSysInfo::currentCpuArchitecture(),
             QSysInfo::prettyProductName(),
             settings.value(QStringLiteral("MobileStudio/previousCrashPhase")).toString(),
             settings.value(QStringLiteral("MobileStudio/previousCrashCheckpoint")).toString(),
             settings.value(QStringLiteral("MobileStudio/crashCheckpoint")).toString());
    QThreadPool::globalInstance()->start([header]() {
        const auto result=QAndroidJniObject::callStaticObjectMethod(
            "org/krita/android/MainActivity","studioProcessExitReport","()Ljava/lang/String;");
        const QString report=header+(result.isValid()?result.toString():
            QStringLiteral("Android exit history unavailable.\n"));
        QMetaObject::invokeMethod(qApp,[report]() {
            if(auto *clipboard=QGuiApplication::clipboard()) {
                clipboard->setText(report);
                QMessageBox::information(QApplication::activeWindow(),
                    QObject::tr("Crash report copied"),
                    QObject::tr("Paste the report into your chat or message."));
            }
        },Qt::QueuedConnection);
    });
}
}
#endif
