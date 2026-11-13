/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Pure Qt safeguards for asynchronous mobile document commands.
 * Never select a different document merely because a native tab index
 * was reused after a close, or because multiple views share a document.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QPointer>
#include <QVector>
#include "view_host_port.h"

class MobileViewIdentityGuard final
{
public:
    using View=MobileViewHostPort::ViewSnapshot;

    static QPointer<QObject> byIndex(const QVector<View> &views,int index)
    {
        if(index<0)return {};
        QPointer<QObject> found;
        for(const View &v:views) {
            if(v.nativeIndex!=index)continue;
            if(!v.nativeView || found)return {};
            found=v.nativeView;
        }
        return found;
    }

    static QPointer<QObject> uniqueActive(const QVector<View> &views)
    {
        QPointer<QObject> chosen;
        for(const View &v:views) {
            if(!v.active)continue;
            if(!v.nativeView || chosen)return {};
            chosen=v.nativeView;
        }
        return chosen;
    }

    // Delayed recovery must not open into a workspace whose native views
    // or backing documents changed during archive verification. View
    // identity alone is insufficient if a host reuses a view widget.
    static bool sameRecoveryWorkspace(const QVector<View> &expected,
                                      const QVector<View> &current)
    {
        if(expected.size()!=current.size())return false;
        int expectedActive=0;
        int currentActive=0;
        for(const View &old:expected) {
            if(!old.nativeView || !old.document)return false;
            if(old.active)++expectedActive;
            int matches=0;
            for(const View &now:current) {
                if(now.nativeView!=old.nativeView)continue;
                ++matches;
                if(!now.document || now.document!=old.document ||
                   now.active!=old.active)return false;
            }
            if(matches!=1)return false;
        }
        for(const View &now:current) {
            if(!now.nativeView || !now.document)return false;
            if(now.active)++currentActive;
        }
        return expectedActive<=1 && currentActive==expectedActive;
    }

    static bool maySave(const QVector<View> &views,
                        const QPointer<QObject> &view,
                        const QPointer<QObject> &document)
    {
        if(!view || !document)return false;
        int count=0;
        bool selected=false;
        for(const View &v:views) {
            if(!v.active)continue;
            ++count;
            selected=v.nativeView==view && v.document==document;
        }
        return count==1 && selected;
    }

    static QPointer<QObject> safeCloseTarget(
        const QVector<View> &views,const QPointer<QObject> &currentDocument)
    {
        int activeCount=0;
        for(const View &v:views)if(v.active)++activeCount;
        if(activeCount>1)return {};
        if(activeCount==1) {
            // The native Qt stack can switch its current widget before
            // KisViewManager updates its active document. During that
            // transition, never close whichever view happens to be
            // visually selected if the engine still reports another
            // canvas (or no document) as current.
            if(!currentDocument)return {};
            const auto active=uniqueActive(views);
            for(const View &v:views) {
                if(v.active && v.nativeView==active &&
                   v.document==currentDocument)return active;
            }
            return {};
        }
        // No active native view means the stack is in transition or
        // the last canvas is being detached. An engine document left
        // selected from the previous tab is not sufficient authority to
        // close an inactive canvas, even if it is the only match.
        return {};
    }
};
#endif
