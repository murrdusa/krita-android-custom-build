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

    std::atomic_bool cancelled{true};
    cache.store(source,2,oldImage,&cancelled);
    if(cache.load(source,2).pixelColor(0,0)!=QColor(Qt::blue))return 3;
    // A gallery close can cancel before a decoded preview is allocated.
    // An uncancelled subsequent visit must still retrieve the same PNG.
    if(!cache.load(source,2,&cancelled).isNull())return 40;
    cancelled.store(false);
    if(cache.load(source,2,&cancelled).pixelColor(0,0)!=QColor(Qt::blue))
        return 41;

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

    // A previously interrupted app may restart with an over-quota
    // cache but no new preview writes. Startup pruning must reclaim it
    // without touching artwork or waiting for 32 later publications.
    const QString startupFolder=directory.filePath(QStringLiteral("startup"));
    if(!QDir().mkpath(startupFolder))return 18;
    QFile original(QDir(startupFolder).filePath(QStringLiteral("artwork.kra")));
    if(!original.open(QIODevice::WriteOnly) || original.write("safe",4)!=4)
        return 19;
    original.close();
    for(int i=0;i<6;++i) {
        QFile stale(QDir(startupFolder).filePath(
            QStringLiteral("oversize-%1.png").arg(i)));
        if(!stale.open(QIODevice::WriteOnly) ||
           !stale.resize(28LL*1024*1024))return 20;
        stale.close();
    }
    const MobileProjectPreviewCache startupCache(startupFolder);
    startupCache.prune();
    qint64 startupBytes=0;
    for(const auto &entry:QDir(startupFolder).entryInfoList(
            {QStringLiteral("*.png")},QDir::Files|QDir::NoSymLinks))
        startupBytes+=entry.size();
    if(startupBytes>MobileProjectPreviewCache::maxCacheBytes() ||
       !QFileInfo::exists(original.fileName()))return 21;
    // A previously interrupted cache may have hundreds more files
    // than the retention limit. The bounded heap must choose the 512
    // newest by modification time, not filesystem traversal order.
    const QString backlogFolder=directory.filePath(QStringLiteral("backlog"));
    if(!QDir().mkpath(backlogFolder))return 22;
    const QDateTime base=QDateTime::currentDateTime().addDays(-30);
    for(int i=0;i<620;++i) {
        const QString name=QDir(backlogFolder).filePath(
            QStringLiteral("preview-%1.png").arg(i,4,10,QLatin1Char('0')));
        QFile entry(name);
        const QByteArray signature=QByteArray::fromHex("89504e470d0a1a0a");
        if(!entry.open(QIODevice::WriteOnly) ||
           entry.write(signature)!=signature.size() ||
           !entry.setFileTime(base.addSecs(i),
                              QFileDevice::FileModificationTime))
            return 23;
        entry.close();
    }
    const MobileProjectPreviewCache backlogCache(backlogFolder);
    backlogCache.prune();
    const auto remaining=QDir(backlogFolder).entryList(
        {QStringLiteral("*.png")},QDir::Files);
    if(remaining.size()!=512 ||
       QFile::exists(QDir(backlogFolder).filePath(
           QStringLiteral("preview-0000.png"))) ||
       !QFile::exists(QDir(backlogFolder).filePath(
           QStringLiteral("preview-0619.png"))))
        return 24;

    // Newer corrupt/empty PNGs must not evict older usable previews.
    // They are deleted, but should not occupy any of the 512 slots.
    const QString invalidFolder=directory.filePath(QStringLiteral("invalid-backlog"));
    if(!QDir().mkpath(invalidFolder))return 25;
    const MobileProjectPreviewCache invalidCache(invalidFolder);
    invalidCache.store(source,41,newImage);
    const QByteArray validKey=QCryptographicHash::hash(
        source.toUtf8()+'\n'+QByteArray::number(41),
        QCryptographicHash::Sha256).toHex();
    const QString validPath=QDir(invalidFolder).filePath(
        QString::fromLatin1(validKey)+QStringLiteral(".png"));
    {
        QFile valid(validPath);
        if(!valid.open(QIODevice::ReadWrite) ||
           !valid.setFileTime(QDateTime::currentDateTime().addDays(-7),
                              QFileDevice::FileModificationTime))
            return 26;
    }
    for(int i=0;i<520;++i) {
        QFile invalid(QDir(invalidFolder).filePath(
            QStringLiteral("empty-%1.png").arg(i,4,10,QLatin1Char('0'))));
        if(!invalid.open(QIODevice::WriteOnly))return 27;
        invalid.close();
    }
    invalidCache.prune();
    if(!QFileInfo::exists(validPath) ||
       invalidCache.load(source,41).isNull() ||
       QDir(invalidFolder).entryList({QStringLiteral("empty-*.png")},
                                    QDir::Files).size()!=0)
        return 28;

    // Nonempty corrupt files also used to occupy the newest 512 slots,
    // pushing old valid thumbnails out before they could be decoded.
    // A PNG signature check must reject those files during pruning.
    const QString corruptFolder=directory.filePath(QStringLiteral("corrupt-backlog"));
    if(!QDir().mkpath(corruptFolder))return 29;
    const MobileProjectPreviewCache corruptCache(corruptFolder);
    corruptCache.store(source,42,newImage);
    const QByteArray preservedKey=QCryptographicHash::hash(
        source.toUtf8()+'\n'+QByteArray::number(42),
        QCryptographicHash::Sha256).toHex();
    const QString preservedPath=QDir(corruptFolder).filePath(
        QString::fromLatin1(preservedKey)+QStringLiteral(".png"));
    {
        QFile preserved(preservedPath);
        if(!preserved.open(QIODevice::ReadWrite) ||
           !preserved.setFileTime(QDateTime::currentDateTime().addDays(-7),
                                  QFileDevice::FileModificationTime))
            return 30;
    }
    for(int i=0;i<520;++i) {
        QFile broken(QDir(corruptFolder).filePath(
            QStringLiteral("broken-%1.png").arg(i,4,10,QLatin1Char('0'))));
        if(!broken.open(QIODevice::WriteOnly) ||
           broken.write("not-a-png",9)!=9)return 31;
    }
    corruptCache.prune();
    if(!QFileInfo::exists(preservedPath) ||
       corruptCache.load(source,42).isNull() ||
       !QDir(corruptFolder).entryList({QStringLiteral("broken-*.png")},
                                     QDir::Files).isEmpty())
        return 32;

    // Gallery must decline malformed or unexpectedly large native KRA
    // inputs before KoStore begins reading ZIP directory metadata.
    const QString kraFixture=directory.filePath(QStringLiteral("preview-input.kra"));
    QFile native(kraFixture);
    if(!native.open(QIODevice::WriteOnly) ||
       native.write(QByteArray::fromHex("504b0304"))!=4)return 33;
    native.close();
    if(!MobileProjectPreviewCache::safeLocalKraSource(kraFixture))return 34;
    const QString linkedKra=directory.filePath(QStringLiteral("linked-input.kra"));
    if(QFile::link(kraFixture,linkedKra) &&
       MobileProjectPreviewCache::safeLocalKraSource(linkedKra))return 35;
    const QString damagedKra=directory.filePath(QStringLiteral("damaged-input.kra"));
    QFile damaged(damagedKra);
    if(!damaged.open(QIODevice::WriteOnly) ||
       damaged.write("not a zip",9)!=9)return 36;
    damaged.close();
    if(MobileProjectPreviewCache::safeLocalKraSource(damagedKra))return 37;
    if(!native.open(QIODevice::ReadWrite) ||
       !native.resize(8LL*1024*1024*1024+1))return 38;
    native.close();
    if(MobileProjectPreviewCache::safeLocalKraSource(kraFixture))return 39;

    qInfo("PASS: thumbnail revisions, cancellation, retention, clock skew and invalid images");
}
