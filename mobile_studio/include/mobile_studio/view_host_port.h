/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-only document-view lifecycle interface. No Krita desktop MDI,
 * window, brush, or file-format dependencies. A replacement mobile
 * application host can implement view ownership without QMdiArea.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class MobileViewHostPort
{
public:
    struct ViewSnapshot {
        QString title;
        bool modified=false;
        bool active=false;
        int nativeIndex=-1; // Compatibility only; never defer by index.
        QPointer<QObject> nativeView; // Guarded, immutable identity.
        QPointer<QObject> document;   // Guarded Krita document identity.
    };

    virtual ~MobileViewHostPort() = default;
    virtual QVector<ViewSnapshot> openViews() const = 0;
    // Krita's engine-selected document. A guarded QObject identity keeps
    // this interface independent of both KisViewManager and KisDocument.
    virtual QPointer<QObject> currentDocument() const = 0;
    virtual bool activateView(const QPointer<QObject> &identity) const = 0;
    // Must preserve the underlying Krita Save / Discard / Cancel prompt.
    virtual bool closeView(const QPointer<QObject> &identity) const = 0;
};
#endif
