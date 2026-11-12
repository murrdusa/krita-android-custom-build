// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/fill_tolerance_drag.h"

int main()
{
    if(mobileFillTolerance(30,0)!=30 ||
       mobileFillTolerance(30,6)!=30 ||
       mobileFillTolerance(30,-8)!=30)return 1;
    if(mobileFillTolerance(30,36)!=40 ||
       mobileFillTolerance(30,-36)!=20)return 2;
    if(mobileFillTolerance(70,1000)!=100 ||
       mobileFillTolerance(15,-1000)!=1)return 3;
    const QRect canvas(0,0,2000,1600);
    if(mobileFillPreviewWindow(canvas,QPoint(500,500))!=QRect(308,308,384,384) ||
       mobileFillPreviewWindow(canvas,QPoint(0,0))!=QRect(0,0,192,192) ||
       !mobileFillPreviewWindow(canvas,QPoint(-1,0)).isEmpty())return 4;
}
