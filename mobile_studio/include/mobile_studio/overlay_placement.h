/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QList>
#include <QRect>
#include <limits>

namespace MobileOverlayPlacement
{
inline QRect nearest(const QRect &preferred,const QRect &bounds,const QList<QRect> &occupied,
                     const QSize &minimum,bool proportional=false)
{
    if(!preferred.isValid() || !bounds.isValid())return {};
    QList<QRect> spaces{bounds};
    for(const auto &obstacle:occupied) {
        if(!obstacle.isValid())continue;
        QList<QRect> next;
        for(const auto &room:spaces) {
            const auto overlap=room.intersected(obstacle.adjusted(-2,-2,2,2));
            if(!overlap.isValid()) {
                next.append(room);
                continue;
            }
            const QRect pieces[]={
                QRect(room.topLeft(),QPoint(overlap.left()-1,room.bottom())),
                QRect(QPoint(overlap.right()+1,room.top()),room.bottomRight()),
                QRect(room.topLeft(),QPoint(room.right(),overlap.top()-1)),
                QRect(QPoint(room.left(),overlap.bottom()+1),room.bottomRight())
            };
            for(const auto &piece:pieces) {
                if(piece.width()<minimum.width() || piece.height()<minimum.height())continue;
                bool contained=false;
                for(const auto &other:next) {
                    if(other.contains(piece)) {contained=true;break;}
                }
                if(!contained) {
                    for(int i=next.size()-1;i>=0;--i)
                        if(piece.contains(next.at(i)))next.removeAt(i);
                    next.append(piece);
                }
            }
        }
        spaces=std::move(next);
    }

    QRect best;
    qint64 bestScore=std::numeric_limits<qint64>::max();
    for(const auto &space:spaces) {
        QSize size=preferred.size().boundedTo(space.size());
        if(proportional && size!=preferred.size())
            size=preferred.size().scaled(space.size(),Qt::KeepAspectRatio);
        if(size.width()<minimum.width() || size.height()<minimum.height())continue;
        const int x=qBound(space.left(),preferred.x(),space.right()-size.width()+1);
        const int y=qBound(space.top(),preferred.y(),space.bottom()-size.height()+1);
        const qint64 shrink=preferred.width()-size.width()+preferred.height()-size.height();
        const qint64 score=4*shrink+(QPoint(x,y)-preferred.topLeft()).manhattanLength();
        if(score<bestScore) {
            best=QRect(x,y,size.width(),size.height());
            bestScore=score;
        }
    }
    return best;
}
}
