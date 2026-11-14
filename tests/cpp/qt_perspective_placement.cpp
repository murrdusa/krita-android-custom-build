// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_studio/perspective_placement.h"

int main()
{
    if(!mobileCanPlaceVanishingPoint({},QPointF(0,0)))return 1;
    if(mobileCanPlaceVanishingPoint({QPointF(0,0)},QPointF(8,0)))return 2;
    if(!mobileCanPlaceVanishingPoint({QPointF(0,0)},QPointF(30,0)))return 3;
    const QVector<QPointF> horizon{QPointF(0,10),QPointF(100,10)};
    if(mobileCanPlaceVanishingPoint(horizon,QPointF(50,10)) ||
       mobileCanPlaceVanishingPoint(horizon,QPointF(50,17)))return 4;
    if(!mobileCanPlaceVanishingPoint(horizon,QPointF(50,-25)) ||
       !mobileCanPlaceVanishingPoint(horizon,QPointF(50,45)))return 5;
}
