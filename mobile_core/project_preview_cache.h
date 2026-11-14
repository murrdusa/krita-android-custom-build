// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QSaveFile>
#include <atomic>

class MobileProjectPreviewCache
{
public:
    static constexpr qint64 maxEncodedBytes() {return 32LL*1024*1024;}
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
        static QMutex publication;
        QMutexLocker guard(&publication);
        if((cancelled && cancelled->load()) || !output.commit())return;
        static unsigned writes=0;
        if(writes++%32!=0)return;
        const auto entries=QDir(m_directory).entryInfoList({QStringLiteral("*.png")},
            QDir::Files|QDir::NoSymLinks,QDir::Time);
        for(int i=512;i<entries.size();++i)QFile::remove(entries.at(i).absoluteFilePath());
    }

private:
    QString fileName(const QString &source,qint64 revision) const
    {
        const QByteArray key=QCryptographicHash::hash(
            source.toUtf8()+'\n'+QByteArray::number(revision),QCryptographicHash::Sha256).toHex();
        return QDir(m_directory).filePath(QString::fromLatin1(key)+QStringLiteral(".png"));
    }

    QString m_directory;
};
