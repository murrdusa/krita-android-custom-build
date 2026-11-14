// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QtGlobal>

inline int mobileFillTolerance(int initial,int delta)
{
    constexpr int slop=6;
    const int movement=delta>slop?delta-slop:qMin(0,delta+slop);
    return qBound(1,initial+movement/3,100);
}

#include <QRect>

// An older worker can finish after a drag returns to its earlier value.
// Preview acceptance requires both tolerance and request identity.
inline bool mobileFillPreviewResultCurrent(int value,int currentValue,
                                           int request,int currentRequest)
{
    return value==currentValue && request==currentRequest;
}

inline QRect mobileFillPreviewWindow(const QRect &bounds,const QPoint &seed)
{
    constexpr int side=384;
    if(bounds.isEmpty() || !bounds.contains(seed))return {};
    // Maintain a full bounded preview when filling near a canvas edge.
    // Intersecting a centered square would unnecessarily shrink the
    // sampled region to a quarter-size preview at a corner.
    const int width=qMin(side,bounds.width());
    const int height=qMin(side,bounds.height());
    const int x=qBound(bounds.left(),seed.x()-width/2,
                       bounds.right()-width+1);
    const int y=qBound(bounds.top(),seed.y()-height/2,
                       bounds.bottom()-height+1);
    return QRect(x,y,width,height);
}
