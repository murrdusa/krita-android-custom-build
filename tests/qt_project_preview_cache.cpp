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

    // A legacy cache may contain full-sized image data. Loading it for
    // a 124 px gallery icon must not allocate the full decoded bitmap.
    QImage largePreview(2048,1536,QImage::Format_RGB32);
    largePreview.fill(Qt::yellow);
    cache.store(source,20,largePreview);
    const QImage scaled=cache.load(source,20);
    if(scaled.isNull() || scaled.size()!=QSize(124,93) ||
       scaled.pixelColor(0,0)!=QColor(Qt::yellow))return 12;
    // Small previews must retain their actual dimensions and colors.
    if(cache.load(source,2).size()!=QSize(8,8))return 13;

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
    // A high-entropy PNG can exceed the decoder's 32 MiB disk limit
    // even when its pixel dimensions are otherwise allowed. Avoid
    // publishing a cache entry that load() will always reject.
    QImage noisy(3500,3500,QImage::Format_RGB32);
    if(noisy.isNull())return 10;
    quint32 seed=0x12345678u;
    for(int y=0;y<noisy.height();++y) {
        auto *line=reinterpret_cast<QRgb*>(noisy.scanLine(y));
        for(int x=0;x<noisy.width();++x) {
            seed^=seed<<13;
            seed^=seed>>17;
            seed^=seed<<5;
            line[x]=qRgb(seed&255,(seed>>8)&255,(seed>>16)&255);
        }
    }
    edgeCache.store(source,9,noisy);
    const QByteArray noisyKey=QCryptographicHash::hash(
        source.toUtf8()+'\n'+QByteArray::number(9),
        QCryptographicHash::Sha256).toHex();
    const QString noisyFile=QDir(directory.filePath(QStringLiteral("edge"))).filePath(
        QString::fromLatin1(noisyKey)+QStringLiteral(".png"));
    if(QFile::exists(noisyFile) || !edgeCache.load(source,9).isNull())
        return 11;
    // The cache count limit alone could retain 512 large PNGs. Stage
    // six sparse 28 MiB legacy cache entries, then trigger a routine
    // publication sweep and verify the aggregate on-disk budget.
    const QString quotaFolder=directory.filePath(QStringLiteral("quota"));
    if(!QDir().mkpath(quotaFolder))return 14;
    // Quota eviction must only remove PNG cache entries, never
    // documents accidentally residing in the same directory.
    QFile artwork(QDir(quotaFolder).filePath(QStringLiteral("keep.kra")));
    if(!artwork.open(QIODevice::WriteOnly) || artwork.write("drawing")!=7)
        return 17;
    artwork.close();
    for(int i=0;i<6;++i) {
        QFile large(QDir(quotaFolder).filePath(
            QStringLiteral("legacy-%1.png").arg(i)));
        if(!large.open(QIODevice::WriteOnly) ||
           !large.resize(28LL*1024*1024))return 15;
        large.close();
    }
    const MobileProjectPreviewCache quotaCache(quotaFolder);
    for(int i=0;i<33;++i)
        quotaCache.store(QStringLiteral("quota-project"),i,newImage);
    qint64 retainedBytes=0;
    int oldFiles=0;
    const auto quotaEntries=QDir(quotaFolder).entryInfoList(
        {QStringLiteral("*.png")},QDir::Files|QDir::NoSymLinks);
    for(const auto &entry:quotaEntries) {
        retainedBytes+=entry.size();
        if(entry.fileName().startsWith(QStringLiteral("legacy-")))
            ++oldFiles;
    }
    if(retainedBytes>MobileProjectPreviewCache::maxCacheBytes() ||
       oldFiles>=6 || quotaCache.load(QStringLiteral("quota-project"),32).isNull() ||
       !QFileInfo::exists(artwork.fileName()))
        return 16;

    qInfo("PASS: thumbnail revisions, cancellation, retention, clock skew and invalid images");
}
