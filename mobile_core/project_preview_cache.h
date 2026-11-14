// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QSaveFile>
#include <atomic>

class MobileProjectPreviewCache
{
public:
    static constexpr qint64 maxEncodedBytes() {return 32LL*1024*1024;}
    static constexpr qint64 maxCacheBytes() {return 128LL*1024*1024;}
    explicit MobileProjectPreviewCache(const QString &directory) : m_directory(directory) {}

    QImage load(const QString &source,qint64 revision) const
    {
        const QFileInfo entry(fileName(source,revision));
        if(!entry.isFile() || entry.isSymLink() || !entry.isReadable() ||
           entry.size()<=0 || entry.size()>maxEncodedBytes())
            return {};
        if(revision==0) {
            const qint64 age=entry.lastModified().secsTo(QDateTime::currentDateTime());
            // Unsaved projects have no stable revision. Clock skew must not
            // make a future-dated thumbnail valid indefinitely.
            if(age>1800 || age < -300)return {};
        }
        QImageReader reader(entry.absoluteFilePath(),"png");
        const QSize size=reader.size();
        // Cache files are untrusted disk input. Reject malformed or very
        // large images before allocating their decoded pixel buffers.
        if(!size.isValid() || size.width()>4096 || size.height()>4096 ||
           qint64(size.width())*size.height()>4096*4096)
            return {};
        // Thumbnails are only displayed at 124 px. A cache entry may
        // still contain a much larger legacy or damaged-but-decodable
        // PNG; decoding the full image can allocate tens of MiB per
        // queued worker. Ask the decoder to downscale before allocation.
        const QSize displaySize=size.scaled(124,124,Qt::KeepAspectRatio);
        if(displaySize!=size)reader.setScaledSize(displaySize);
        return reader.read();
    }

    // An abrupt process kill can leave a legacy or over-quota thumbnail
    // directory without any subsequent writes to trigger retention.
    // The gallery runs this off the UI thread on startup.
    void prune() const
    {
        QMutexLocker guard(&publicationLock());
        pruneLocked();
    }

    void store(const QString &source,qint64 revision,const QImage &image,
               const std::atomic_bool *cancelled=nullptr) const
    {
        // A queued thumbnail may have been superseded while waiting for
        // its worker. Avoid expensive encoding and filesystem work then.
        if((cancelled && cancelled->load()) || image.isNull() ||
           image.width()>4096 || image.height()>4096 ||
           qint64(image.width())*image.height()>4096*4096 ||
           !QDir().mkpath(m_directory))return;
        QSaveFile output(fileName(source,revision));
        if(!output.open(QIODevice::WriteOnly) || !image.save(&output,"PNG"))
            return;
        // Keep writer and reader in agreement: large incompressible
        // thumbnails should not consume disk only to be rejected on
        // every subsequent gallery refresh.
        if(output.size()<=0 || output.size()>maxEncodedBytes()) {
            output.cancelWriting();
            return;
        }
        const qint64 encodedBytes=output.size();
        QMutexLocker guard(&publicationLock());
        if((cancelled && cancelled->load()) || !output.commit())return;
        static unsigned writes=0;
        // A large, valid preview can fill the quota in just a few
        // writes. Sweep immediately for those; small thumbnails still
        // amortize their directory enumeration over 32 publications.
        if(writes++%32!=0 && encodedBytes<=1024*1024)return;
        pruneLocked();
    }

private:
    static QMutex &publicationLock()
    {
        static QMutex lock;
        return lock;
    }

    void pruneLocked() const
    {
        // Keep only metadata for the newest 512 files. Materializing and
        // sorting every PNG in an interrupted/overfull cache can spike
        // gallery startup memory before a single thumbnail is loaded.
        constexpr int maxEntries=512;
        const auto newer=[](const QFileInfo &a,const QFileInfo &b) {
            if(a.lastModified()!=b.lastModified())
                return a.lastModified()>b.lastModified();
            return a.fileName()>b.fileName();
        };
        QVector<QFileInfo> newest;
        newest.reserve(maxEntries);
        QDirIterator files(m_directory,{QStringLiteral("*.png")},
                           QDir::Files|QDir::NoSymLinks);
        while(files.hasNext()) {
            files.next();
            const QFileInfo entry=files.fileInfo();
            // Invalid legacy cache files must not crowd out readable
            // older previews from the newest-512 retention window.
            // The second pass deletes these invalid files as usual.
            if(entry.size()<8 || entry.size()>maxEncodedBytes())continue;
            // A small corrupt file may still occupy a newest slot and
            // evict a real preview. Reject the cheap PNG-signature
            // failure before it enters the bounded retention heap.
            QFile candidate(entry.absoluteFilePath());
            if(!candidate.open(QIODevice::ReadOnly) ||
               candidate.peek(8)!=QByteArray::fromHex("89504e470d0a1a0a"))
                continue;
            if(newest.size()<maxEntries) {
                newest.append(entry);
                std::push_heap(newest.begin(),newest.end(),newer);
            } else if(newer(entry,newest.front())) {
                std::pop_heap(newest.begin(),newest.end(),newer);
                newest.last()=entry;
                std::push_heap(newest.begin(),newest.end(),newer);
            }
        }
        std::sort(newest.begin(),newest.end(),newer);
        QSet<QString> retained;
        qint64 retainedBytes=0;
        for(const QFileInfo &entry:newest) {
            const qint64 size=entry.size();
            if(size>0 && size<=maxEncodedBytes() &&
               retainedBytes<=maxCacheBytes()-size) {
                retained.insert(entry.absoluteFilePath());
                retainedBytes+=size;
            }
        }
        // A second streaming pass removes older and over-budget files
        // without retaining their names or decoded contents in RAM.
        QDirIterator obsolete(m_directory,{QStringLiteral("*.png")},
                              QDir::Files|QDir::NoSymLinks);
        while(obsolete.hasNext()) {
            const QString path=obsolete.next();
            if(!retained.contains(path))QFile::remove(path);
        }
    }

    QString fileName(const QString &source,qint64 revision) const
    {
        const QByteArray key=QCryptographicHash::hash(
            source.toUtf8()+'\n'+QByteArray::number(revision),QCryptographicHash::Sha256).toHex();
        return QDir(m_directory).filePath(QString::fromLatin1(key)+QStringLiteral(".png"));
    }

    QString m_directory;
};
