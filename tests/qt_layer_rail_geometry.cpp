/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mobile_core/layer_rail_geometry.h"
#include "mobile_core/overlay_placement.h"

int main()
{
    using namespace MobileLayerRailGeometry;
    if(tileExtent(82,true,true,false)!=38 || tileExtent(82,true,false,true)!=54 ||
       tileExtent(82,true,false,false)>=82 || tileExtent(82,false,false,false)!=82)
        qFatal("rotated cards must use compact extents");
    const QRect card(5,2,76,38);
    const QRect eye(52,6,26,26);
    const QRect folder(5,6,26,26);
    const auto rotatedCard=rotateTileRect(card,86);
    const auto rotatedEye=rotateTileRect(eye,86);
    const auto rotatedFolder=rotateTileRect(folder,86);
    if(rotatedCard.size()!=QSize(38,76) || !rotatedCard.contains(rotatedEye) ||
       !rotatedCard.contains(rotatedFolder) || rotatedEye.intersects(rotatedFolder) ||
       rotatedEye.top()>=rotatedFolder.top())
        qFatal("folder and visibility targets did not rotate with the card");
    const QRect bounds(2,2,398,596);
    const QRect rail(20,60,82,140);
    const QRect slider(2,400,160,44);
    if(expand(rail,bounds,500,false,false,{slider})!=QRect(20,60,82,338))
        qFatal("vertical rail did not stop before the slider");
    if(expand(rail,bounds,500,false,false,{QRect(110,400,160,44)})!=QRect(20,60,82,500))
        qFatal("a non-overlapping control incorrectly limited the rail");
    if(expand(rail,bounds,120,false,false,{QRect(2,30,160,90)})!=QRect(20,122,82,120))
        qFatal("rail started on top of another control");
    if(expand(QRect(100,20,120,82),bounds,280,true,false,{QRect(300,0,70,180)})!=
       QRect(100,20,198,82))
        qFatal("horizontal rail did not stop before the side toolbar");
    if(expand(QRect(20,420,82,170),bounds,500,false,true,{slider})!=QRect(20,446,82,144))
        qFatal("bottom-anchored rail did not stop above its first blocker");
    if(expand(rail,bounds,500,false,false,{bounds}).height()!=0)
        qFatal("rail overlapped an occupied viewport");
    // Generic phone/fold/tablet layouts: every auto-expanded rail stays
    // inside the available viewport and respects the nearest blocker.
    for(const QSize size:{QSize(320,720),QSize(720,320),
                          QSize(700,700),QSize(1200,800)}) {
        const QRect screen(QPoint(0,0),size);
        for(bool horizontal:{false,true}) {
            for(bool reverse:{false,true}) {
                const QRect start=horizontal?
                    QRect(24,24,90,60):QRect(24,24,60,90);
                const QRect result=expand(start,screen,2000,horizontal,reverse,{});
                if(!result.isValid() || !screen.contains(result))
                    qFatal("auto-expanded rail escaped a device viewport");
                const int extent=horizontal?result.width():result.height();
                if(extent<=0 || extent> (horizontal?size.width():size.height()))
                    qFatal("rail expansion used invalid screen space");
            }
        }
    }
    const QRect toolbar(2,2,398,48);
    const auto panel=MobileOverlayPlacement::nearest(QRect(12,12,150,160),bounds,
                                                     {toolbar},QSize(100,80));
    if(panel.top()!=52 || panel.intersects(toolbar) || panel.size()!=QSize(150,160))
        qFatal("panel did not move off the toolbar after resizing");
    const QRect right(250,2,150,596);
    const auto smaller=MobileOverlayPlacement::nearest(QRect(90,60,300,48),bounds,
                                                       {right},QSize(80,48));
    if(!smaller.isValid() || smaller.intersects(right) || smaller.height()!=48)
        qFatal("toolbar could not shorten on a crowded viewport");
}
