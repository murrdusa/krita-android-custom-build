// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QImageReader>
#include <QMutex>
#include <QSaveFile>
#include <atomic>

class MobileProjectPreviewCache
{
public:
    explicit MobileProjectPreviewCache(const QString &directory) : m_directory(directory) {}

    QImage load(const QString &source,qint64 revision) const
    {
        const QFileInfo entry(fileName(source,revision));
        if(!entry.isReadable() ||
           (revision==0 && entry.lastModified().secsTo(QDateTime::currentDateTime())>1800))
            return {};
        QImageReader reader(entry.absoluteFilePath(),"png");
        return reader.read();
    }

    void store(const QString &source,qint64 revision,const QImage &image,
               const std::atomic_bool *cancelled=nullptr) const
    {
        if(image.isNull() || !QDir().mkpath(m_directory))return;
        QSaveFile output(fileName(source,revision));
        if(!output.open(QIODevice::WriteOnly) || !image.save(&output,"PNG"))return;
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
