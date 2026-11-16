/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android launch/onNewIntent document delivery, native engine path.
 * No desktop MDI, KisMainWindow, or second import implementation.
 */
#include "android_document_intents.h"
#ifdef Q_OS_ANDROID
#include <atomic>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QScopedValueRollback>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include "document_host_port.h"

namespace {
std::atomic_bool s_active{false};
QPointer<QWidget> s_window;
QSharedPointer<MobileDocumentHostPort> s_documentHost;
// A previous Android window can be destroyed after a replacement host
// has attached. Only the latest registration may clear global host state.
quint64 s_attachmentGeneration=0;
QStringList s_pending;
bool s_opening=false;

int drain()
{
    if(!s_window || !s_documentHost || !s_documentHost->available() ||
       s_opening)return 0;
    const QScopedValueRollback<bool> opening(s_opening,true);
    int opened=0;
    while(s_window && s_documentHost && !s_pending.isEmpty()) {
        const QString path=s_pending.takeFirst();
        // Opening a document can synchronously close its old Android
        // window. Retain this host until its own openFile() returns.
        const QSharedPointer<MobileDocumentHostPort> host=s_documentHost;
        if(host->openFile(path)) {
            ++opened;
            qInfo() << "Mobile Studio: opened Android file intent" << path;
        } else {
            qWarning() << "Mobile Studio: could not open Android file intent"
                       << path;
        }
    }
    return opened;
}
}

void MobileAndroidDocumentIntents::activate()
{
    s_active.store(true);
}

bool MobileAndroidDocumentIntents::active()
{
    return s_active.load();
}

bool MobileAndroidDocumentIntents::enqueue(const QString &input)
{
    if(!active())return false;
    const QString raw=input.trimmed();
    if(raw.isEmpty())return false;
    // Android may call a native file-open callback from its Java thread.
    // Process document views only on Qt's GUI thread.
    auto *app=QCoreApplication::instance();
    if(app && QThread::currentThread()!=app->thread()) {
        QMetaObject::invokeMethod(app,[raw]() {
            MobileAndroidDocumentIntents::enqueue(raw);
        },Qt::QueuedConnection);
        return true;
    }
    const QUrl url(raw);
    const QString path=url.isLocalFile()?url.toLocalFile():raw;
    if(!s_pending.contains(path))
        s_pending.append(path);
    if(s_window && s_documentHost) {
        QTimer::singleShot(0,s_window.data(),[](){drain();});
    }
    return true;
}

int MobileAndroidDocumentIntents::attachAndOpen(
    QWidget *window,const QSharedPointer<MobileDocumentHostPort> &documents)
{
    if(!active() || !window || !documents || !documents->available())
        return 0;
    if(s_window!=window) {
        const quint64 generation=++s_attachmentGeneration;
        QObject::connect(window,&QObject::destroyed,[generation](QObject*) {
            // A stale window must not clear a newer window's document
            // host during Android activity recreation.
            if(generation!=s_attachmentGeneration)return;
            s_window=nullptr;
            s_documentHost.clear();
        });
    }
    s_window=window;
    s_documentHost=documents;
    return drain();
}
#endif
