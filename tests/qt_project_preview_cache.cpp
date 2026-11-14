// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/project_preview_cache.h"
#include <QCoreApplication>
#include <QFile>
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
    const MobileProjectPreviewCache edgeCache(directory.filePath(QStringLiteral("edge")));
    edgeCache.store(source,0,newImage);
    const QByteArray key=QCryptographicHash::hash(
        source.toUtf8()+'\n'+QByteArray::number(0),QCryptographicHash::Sha256).toHex();
    const QString cached=QDir(directory.filePath(QStringLiteral("edge"))).filePath(
        QString::fromLatin1(key)+QStringLiteral(".png"));
    QFile future(cached);
    if(!future.open(QIODevice::ReadWrite) ||
       !future.setFileTime(QDateTime::currentDateTime().addSecs(3600),
                           QFileDevice::FileModificationTime))return 5;
    future.close();
    if(!edgeCache.load(source,0).isNull())return 6;

    QImage oversized(4097,1,QImage::Format_RGB32);
    oversized.fill(Qt::green);
    edgeCache.store(source,7,oversized);
    if(!edgeCache.load(source,7).isNull())return 7;

    edgeCache.store(source,8,newImage);
    const QByteArray corruptKey=QCryptographicHash::hash(
        source.toUtf8()+'\n'+QByteArray::number(8),QCryptographicHash::Sha256).toHex();
    QFile corrupt(QDir(directory.filePath(QStringLiteral("edge"))).filePath(
        QString::fromLatin1(corruptKey)+QStringLiteral(".png")));
    if(!corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate) ||
       corrupt.write("not a png")!=9)return 8;
    corrupt.close();
    if(!edgeCache.load(source,8).isNull())return 9;
    qInfo("PASS: thumbnail revisions, cancellation, retention, clock skew and invalid images");
}
