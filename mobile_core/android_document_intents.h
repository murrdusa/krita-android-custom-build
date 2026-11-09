/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android file-open intents for the native Mobile Studio document host.
 * Receives provider URIs before the first canvas exists, then opens
 * them through Krita's existing document import implementation.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
#include <QSharedPointer>
#include <QString>
class QWidget;
class MobileDocumentHostPort;

class MobileAndroidDocumentIntents final
{
public:
    static void activate();
    static bool active();
    static bool enqueue(const QString &path);
    // Binds the live document service and opens queued launch files.
    // Returns number of successfully opened files (zero -> create blank).
    static int attachAndOpen(QWidget *window,
                             const QSharedPointer<MobileDocumentHostPort> &documents);
};
#endif
