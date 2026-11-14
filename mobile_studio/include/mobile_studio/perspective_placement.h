// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QLineF>
#include <QVector>

inline bool mobileCanPlaceVanishingPoint(const QVector<QPointF> &points,const QPointF &candidate)
{
    constexpr qreal minSpacing=12;
    for(const auto &point:points)
        if(QLineF(point,candidate).length()<minSpacing)return false;
    if(points.size()!=2)return points.size()<2;
    const QLineF horizon(points[0],points[1]);
    const QPointF offset=candidate-horizon.p1();
    return qAbs(horizon.dx()*offset.y()-horizon.dy()*offset.x())>=minSpacing*horizon.length();
}
