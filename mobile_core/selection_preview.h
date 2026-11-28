/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QPainter>
#include <QPainterPath>

namespace MobileSelectionPreview
{
inline void paint(QPainter &painter,const QPainterPath &path)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setBrush(Qt::NoBrush);
    QPen pen(Qt::black,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawPath(path);
    pen.setColor(Qt::white);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.drawPath(path);
    painter.restore();
}
}
