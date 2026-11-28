/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QImage>
#include "mobile_core/selection_preview.h"
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    for(const QColor background:{QColor(Qt::black),QColor(Qt::white),QColor(Qt::gray),QColor(Qt::red)}) {
        QImage image(96,96,QImage::Format_ARGB32_Premultiplied);image.fill(background);
        QPainter painter(&image);painter.scale(2,2);painter.setPen(Qt::green);
        const QPen before=painter.pen();const auto transform=painter.transform();
        QPainterPath path;path.moveTo(10,20);path.lineTo(40,20);path.lineTo(35,35);
        MobileSelectionPreview::paint(painter,path);
        if(painter.pen()!=before || painter.transform()!=transform)qFatal("selection preview leaked painter state");
        painter.end();
        int changedRows=0;
        for(int y=0;y<96;++y)if(image.pixelColor(50,y)!=background)++changedRows;
        if(changedRows<1 || changedRows>5)qFatal("lasso preview invisible or incorrectly scaled");
        if(image.pixelColor(45,50)!=background)qFatal("lasso preview unexpectedly filled image content");
    }
    qInfo("PASS: lasso preview visible on dark/light/color backgrounds and maintains zoom-independent line width");
}
