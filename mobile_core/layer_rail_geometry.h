/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QList>
#include <QRect>
#include <algorithm>

namespace MobileLayerRailGeometry
{
inline QRect expand(const QRect &anchor,const QRect &bounds,int desired,
                    bool horizontal,bool reverse,const QList<QRect> &obstacles)
{
    constexpr int gap=2;
    const int low=horizontal?bounds.left():bounds.top();
    const int high=(horizontal?bounds.right():bounds.bottom())+1;
    const int crossLow=horizontal?bounds.top():bounds.left();
    const int crossHigh=(horizontal?bounds.bottom():bounds.right())+1;
    const int thickness=qMin(horizontal?anchor.height():anchor.width(),crossHigh-crossLow);
    const int cross=qBound(crossLow,horizontal?anchor.top():anchor.left(),crossHigh-thickness);
    const int leading=horizontal?anchor.left():anchor.top();
    const int trailing=(horizontal?anchor.right():anchor.bottom())+1;
    const auto coordinate=[=](int position) {return reverse?low+high-position:position;};
    int start=qBound(low,coordinate(reverse?trailing:leading),high);
    QList<QPair<int,int>> blocked;
    for(const auto &obstacle:obstacles) {
        const int firstCross=horizontal?obstacle.top():obstacle.left();
        const int lastCross=(horizontal?obstacle.bottom():obstacle.right())+1;
        if(firstCross>=cross+thickness || lastCross<=cross)continue;
        const int first=(horizontal?obstacle.left():obstacle.top())-gap;
        const int last=(horizontal?obstacle.right():obstacle.bottom())+1+gap;
        blocked.append({coordinate(reverse?last:first),coordinate(reverse?first:last)});
    }
    std::sort(blocked.begin(),blocked.end());
    int end=high;
    for(const auto &interval:blocked) {
        if(interval.second<=start)continue;
        if(interval.first>start) {
            end=qMin(end,interval.first);
            break;
        }
        start=qMin(high,interval.second);
    }
    const int length=qMin(qMax(0,desired),qMax(0,end-start));
    const int position=coordinate(reverse?start+length:start);
    return horizontal?QRect(position,cross,length,thickness):
                      QRect(cross,position,thickness,length);
}
}
