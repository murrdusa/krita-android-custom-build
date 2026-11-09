/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * One-node reorder bounds for a native Krita document. All actual
 * mutations still go through KisNodeManager and Krita's undo adapter.
 * Invalid selections or edge-of-stack moves must never start the
 * asynchronous multi-node juggler with an empty node list.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
class MobileNativeLayerReorderPolicy final
{
public:
    static int targetIndex(int currentIndex,int childCount,bool towardTop,
                           int selectedCount=1)
    {
        if(selectedCount!=1 || childCount<2 ||
           currentIndex<0 || currentIndex>=childCount)return -1;
        const int next=currentIndex+(towardTop?1:-1);
        return next>=0 && next<childCount ? next : -1;
    }
};
#endif
