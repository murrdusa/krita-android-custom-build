// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>
#include <QStringList>
#include <QThreadPool>

namespace MobileCrashReport
{
// Read-only process memory counters supplied by Linux/Android. These
// complement the previous-exit RSS: a single on-demand snapshot is
// cheap, does not request permissions, and never reads drawing data.
// Values are kernel-reported KiB; they are not allocations attributable
// specifically to Krita's canvas or a particular tool.
inline QString memorySnapshot()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if(!status.open(QIODevice::ReadOnly))
        return QStringLiteral("Process memory: unavailable");
    const QByteArray contents=status.read(16*1024);
    QStringList fields;
    for(const QByteArray &line:contents.split('\\n')) {
        const int colon=line.indexOf(':');
        if(colon<0)continue;
        const QByteArray key=line.left(colon);
        if(key==QByteArrayLiteral("VmRSS") ||
           key==QByteArrayLiteral("VmHWM") ||
           key==QByteArrayLiteral("VmSwap")) {
            fields.append(QString::fromLatin1(key)+QStringLiteral(": ")+
                          QString::fromLatin1(line.mid(colon+1).trimmed()));
        }
    }
    return fields.isEmpty()?QStringLiteral("Process memory: unavailable"):
        QStringLiteral("Process memory (kernel counters): ")+
            fields.join(QStringLiteral("; "));
}

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
        "Current canvas/tool checkpoint: %6\n%7\n\n")
        .arg(QCoreApplication::applicationVersion(),QSysInfo::currentCpuArchitecture(),
             QSysInfo::prettyProductName(),
             settings.value(QStringLiteral("MobileStudio/previousCrashPhase")).toString(),
             settings.value(QStringLiteral("MobileStudio/previousCrashCheckpoint")).toString(),
             settings.value(QStringLiteral("MobileStudio/crashCheckpoint")).toString(),
             memorySnapshot());
    // Exit-history retrieval can finish after Android destroys the
    // QApplication. Never post to qApp by a stale global pointer.
    const QPointer<QCoreApplication> application(qApp);
    if(!application)return;
    QThreadPool::globalInstance()->start([header,application]() {
        if(!application)return;
        const auto result=QAndroidJniObject::callStaticObjectMethod(
            "org/krita/android/MainActivity","studioProcessExitReport","()Ljava/lang/String;");
        const QString report=header+(result.isValid()?result.toString():
            QStringLiteral("Android exit history unavailable.\n"));
        if(!application)return;
        QMetaObject::invokeMethod(application.data(),[report]() {
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
