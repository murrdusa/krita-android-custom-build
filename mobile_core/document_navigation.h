/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host-independent selection of adjacent live document views.
 * This operates on guarded view identities rather than MDI tab indices,
 * so closing or inserting a canvas cannot silently select a different one.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QPointer>
#include <QSharedPointer>
#include <QVector>
#include "view_host_port.h"

class MobileDocumentNavigation final
{
public:
    using Snapshot=MobileViewHostPort::ViewSnapshot;

    static QPointer<QObject> neighbor(
        const QVector<Snapshot> &views,
        const QPointer<QObject> &currentDocument,
        int step)
    {
        if((step!=1 && step!=-1) || !currentDocument)return {};
        QVector<QPointer<QObject>> live;
        int activeIndex=-1;
        int activeCount=0;
        for(const Snapshot &view:views) {
            if(view.active) {
                ++activeCount;
                if(!view.nativeView || !view.document ||
                   view.document!=currentDocument)
                    return {};
            }
            if(!view.nativeView || !view.document)continue;
            // Duplicate view identities make the navigation ambiguous.
            if(live.contains(view.nativeView))return {};
            if(view.active)activeIndex=live.size();
            live.append(view.nativeView);
        }
        if(activeCount!=1 || activeIndex<0 || live.size()<2)return {};
        const int next=(activeIndex+step+live.size())%live.size();
        return live.at(next);
    }

    static bool cycle(const QSharedPointer<MobileViewHostPort> &host,
                      int step)
    {
        if(!host)return false;
        const auto snapshot=host->openViews();
        const QPointer<QObject> current=host->currentDocument();
        const auto target=neighbor(snapshot,current,step);
        // Revalidate the selected document before activating the target:
        // callers may be invoked from a menu after a document was closed.
        if(!target || host->currentDocument()!=current)return false;
        return host->activateView(target);
    }
};
#endif
