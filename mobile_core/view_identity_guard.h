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

    // Opening a recovered version after asynchronous KRA validation is
    // safe if the complete set of native canvases and the active canvas
    // are unchanged. Native tab order may change without replacing views.
    static bool sameRecoveryWorkspace(
        const QVector<QPointer<QObject>> &expectedViews,
        const QPointer<QObject> &expectedActive,
        const QVector<View> &current)
    {
        if(expectedViews.size()!=current.size())return false;
        for(const auto &expected:expectedViews) {
            if(!expected)return false;
            int matches=0;
            for(const auto &entry:current)
                if(entry.nativeView==expected)++matches;
            if(matches!=1)return false;
        }
        bool activeMatches=!expectedActive;
        int activeCount=0;
        for(const auto &entry:current) {
            if(!entry.nativeView)return false;
            if(entry.active) {
                ++activeCount;
                if(entry.nativeView!=expectedActive)return false;
                activeMatches=true;
            }
        }
        return activeCount<=1 && activeMatches;
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
