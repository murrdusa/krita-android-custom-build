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
    // A tolerance may return to an earlier value while a queued worker
    // is still calculating that older request (ABA). Never accept it.
    if(!mobileFillPreviewResultCurrent(20,20,4,4) ||
       mobileFillPreviewResultCurrent(20,20,4,6) ||
       mobileFillPreviewResultCurrent(20,30,6,6))return 7;
    // Fill preview workers must never allocate an unbounded sample
    // when canvas geometry or a colorspace stride is malformed.
    if(mobileFillPreviewBufferBytes(QSize(384,384),4)!=589824 ||
       mobileFillPreviewBufferBytes(QSize(384,384),16)!=2359296 ||
       mobileFillPreviewBufferBytes(QSize(0,384),4)!=0 ||
       mobileFillPreviewBufferBytes(QSize(385,384),4)!=0 ||
       mobileFillPreviewBufferBytes(QSize(384,384),0)!=0 ||
       mobileFillPreviewBufferBytes(QSize(384,384),1000000)!=0)
        return 8;
    // Both the input selection and computed flood mask are read into
    // one-byte-per-pixel buffers. Reject an unexpected engine stride
    // before either readBytes() can overwrite its destination.
    if(mobileFillPreviewSelectionBytes(QSize(384,384),1)!=147456 ||
       mobileFillPreviewSelectionBytes(QSize(384,384),2)!=0 ||
       mobileFillPreviewSelectionBytes(QSize(384,384),4)!=0 ||
       mobileFillPreviewSelectionBytes(QSize(0,384),1)!=0 ||
       mobileFillPreviewSelectionBytes(QSize(385,384),1)!=0)
        return 9;
    const QRect canvas(0,0,2000,1600);
    if(mobileFillPreviewWindow(canvas,QPoint(500,500))!=QRect(308,308,384,384) ||
       mobileFillPreviewWindow(canvas,QPoint(0,0))!=QRect(0,0,384,384) ||
       mobileFillPreviewWindow(canvas,QPoint(1999,1599))!=QRect(1616,1216,384,384) ||
       !mobileFillPreviewWindow(canvas,QPoint(-1,0)).isEmpty())return 4;
    // Small canvases must remain within their real image bounds.
    const QRect small(12,34,120,180);
    if(mobileFillPreviewWindow(small,QPoint(12,34))!=small ||
       mobileFillPreviewWindow(small,QPoint(131,213))!=small)return 5;
    const QRect narrow(40,60,250,1000);
    if(mobileFillPreviewWindow(narrow,QPoint(40,1059))!=
       QRect(40,676,250,384))return 6;
}
