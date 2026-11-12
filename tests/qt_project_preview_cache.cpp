// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/project_preview_cache.h"
#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir directory;
    if(!directory.isValid())return 1;
    const MobileProjectPreviewCache cache(directory.path());
    const QString source=QStringLiteral("drawing.kra");
    QImage oldImage(8,8,QImage::Format_RGB32),newImage(8,8,QImage::Format_RGB32);
    oldImage.fill(Qt::red);
    newImage.fill(Qt::blue);
    cache.store(source,2,newImage);
    cache.store(source,1,oldImage);
    if(cache.load(source,2).pixelColor(0,0)!=QColor(Qt::blue) ||
       cache.load(source,1).pixelColor(0,0)!=QColor(Qt::red) ||
       !cache.load(source,3).isNull())return 2;

    const std::atomic_bool cancelled{true};
    cache.store(source,2,oldImage,&cancelled);
    if(cache.load(source,2).pixelColor(0,0)!=QColor(Qt::blue))return 3;

    for(int i=3;i<550;++i)cache.store(source,i,newImage);
    const auto entries=QDir(directory.path()).entryList({QStringLiteral("*.png")},QDir::Files);
    if(entries.size()>543)return 4;
    qInfo("PASS: preview revisions, cancelled publication and bounded disk cache");
}
