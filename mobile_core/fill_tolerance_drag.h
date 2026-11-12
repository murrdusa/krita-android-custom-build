// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QtGlobal>

inline int mobileFillTolerance(int initial,int delta)
{
    constexpr int slop=6;
    const int movement=delta>slop?delta-slop:qMin(0,delta+slop);
    return qBound(1,initial+movement/3,100);
}
