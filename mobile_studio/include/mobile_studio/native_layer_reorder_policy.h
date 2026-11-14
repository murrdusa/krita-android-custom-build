/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * One-node reorder positioning for Krita's sibling-based move command.
 * Siblings run from bottom (index zero) to top; the move-command
 * anchor is the layer that will be immediately BELOW the moved layer.
 * Never create no-op moves or self-referential anchors.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
class MobileNativeLayerReorderPolicy final
{
public:
    struct Plan {
        bool valid=false;
        int targetIndex=-1;
        // -1 means insert at the bottom, with a null "above" anchor.
        int anchorIndex=-1;
    };

    static int targetIndex(int currentIndex,int childCount,bool towardTop,
                           int selectedCount=1)
    {
        if(selectedCount!=1 || childCount<2 ||
           currentIndex<0 || currentIndex>=childCount)return -1;
        const int next=currentIndex+(towardTop?1:-1);
        return next>=0 && next<childCount ? next : -1;
    }

    static Plan plan(int currentIndex,int childCount,bool towardTop,
                     int selectedCount=1)
    {
        const int next=targetIndex(currentIndex,childCount,towardTop,
                                   selectedCount);
        if(next<0)return {};
        // Krita's moveNode(node,parent,aboveThis) inserts the node
        // above the *existing* sibling (or at bottom if null).
        // For upward moves anchor to the immediate upper sibling.
        // For downward moves anchor to the sibling two positions below
        // the original, not the immediate lower sibling itself.
        return {true,next,towardTop?currentIndex+1:currentIndex-2};
    }
};
#endif
