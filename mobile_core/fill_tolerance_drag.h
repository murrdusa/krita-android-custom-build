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

inline QRect mobileFillPreviewWindow(const QRect &bounds,const QPoint &seed)
{
    constexpr int side=384;
    return bounds.contains(seed)?
        bounds.intersected(QRect(seed-QPoint(side/2,side/2),QSize(side,side))):QRect();
}
