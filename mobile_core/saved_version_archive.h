/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Completed native KRA saves in app-private storage, including Android
 * document providers. Unsaved edits are handled by private draft recovery.
 */
#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QString>
#include <QThreadPool>
#include <QUuid>
#include <QVector>

class MobileSavedVersionArchive final
{
public:
    struct Version {
        QString path;
        QDateTime createdUtc;
        qint64 bytes=0;
    };
    static constexpr int maxVersions() {return 12;}
    static constexpr qint64 maxBytes() {return 512LL*1024*1024;}

    static QString folderFor(const QString &source,
                             const QString &rootOverride=QString())
    {
        if(source.isEmpty())return {};
        QString identity=source;
        if(!isProviderUri(source)) {
            const QFileInfo file(source);
            if(file.suffix().compare(QStringLiteral("kra"),Qt::CaseInsensitive)!=0)
                return {};
            const QString canonical=file.canonicalFilePath();
            identity=QDir::cleanPath(canonical.isEmpty()?
                                    file.absoluteFilePath():canonical);
        }
        const QString base=rootOverride.isEmpty()?
            QDir(QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("saved-versions")):rootOverride;
        if(base.isEmpty())return {};
        const QString digest=QString::fromLatin1(QCryptographicHash::hash(
            identity.toUtf8(),QCryptographicHash::Sha256).toHex());
        return QDir(base).filePath(digest);
    }

    static QVector<Version> list(const QString &source,
                                const QString &rootOverride=QString())
    {
        return scan(folderFor(source,rootOverride));
    }

    // Safe to invoke from a background worker; call enqueueAfterSave() from
    // the Krita GUI thread after waitForSavingToComplete() returns.
    static bool snapshot(const QString &source,
                         const QString &rootOverride=QString())
    {
        QMutexLocker guard(&lock());
        const QString folder=folderFor(source,rootOverride);
        if(folder.isEmpty())return false;
        constexpr qint64 copyLimit=8LL*1024*1024*1024;
        const bool provider=isProviderUri(source);
        QFileInfo original;
        if(!provider) {
            original.setFile(source);
            if(!original.isFile())return false;
        }

        QFile input(source);
        if(!input.open(QIODevice::ReadOnly) ||
           input.peek(4)!=QByteArray::fromHex("504b0304"))return false;
        const qint64 expected=provider?input.size():original.size();
        if(expected>copyLimit || (!provider && expected<=0))return false;
        if(!QDir().mkpath(folder))return false;
        const QStorageInfo disk(folder);
        if(disk.isValid() && disk.isReady() &&
           disk.bytesAvailable()>=0 &&
           disk.bytesAvailable()<qMax(qint64(0),expected)+16LL*1024*1024)
            return false;
        const QDateTime before=original.lastModified();
        const QString stem=QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMddTHHmmsszzz"))+QStringLiteral("-")+
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString target=QDir(folder).filePath(stem+QStringLiteral(".kra"));
        QSaveFile output(target);
        if(!output.open(QIODevice::WriteOnly))return false;

        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 copied=0;
        QByteArray block(256*1024,Qt::Uninitialized);
        qint64 length=0;
        while((length=input.read(block.data(),block.size()))>0) {
            copied+=length;
            if(copied>copyLimit || output.write(block.constData(),length)!=length)
                return false;
            hash.addData(block.constData(),length);
        }
        if(length<0 || (expected>0 && copied!=expected))return false;
        if(!provider) {
            original.refresh();
            if(original.size()!=expected || original.lastModified()!=before)
                return false;
        }
        const QByteArray sha=hash.result().toHex();
        const auto previous=scan(folder);
        if(!previous.isEmpty()) {
            QFile sidecar(previous.first().path+QStringLiteral(".sha256"));
            if(sidecar.open(QIODevice::ReadOnly) &&
               sidecar.readAll().trimmed()==sha) {
                output.cancelWriting(); // unchanged save, no duplicate
                return true;
            }
        }
        if(!output.commit())return false;
        QSaveFile digestFile(target+QStringLiteral(".sha256"));
        if(digestFile.open(QIODevice::WriteOnly)) {
            if(digestFile.write(sha)==sha.size())
                digestFile.commit();
            else digestFile.cancelWriting();
        }
        prune(folder);
        return true;
    }

    static void enqueueAfterSave(const QString &source)
    {
        if(folderFor(source).isEmpty())return;
        QThreadPool::globalInstance()->start([source]() {
            snapshot(source);
        });
    }

    // Open a separate private copy of a version, not the stored snapshot
    // or the user's original file. Saving a recovered document can
    // therefore never overwrite a historical checkpoint by accident.
    static QString materializeRecovery(const Version &version,
                                       const QString &rootOverride=QString())
    {
        const QString base=rootOverride.isEmpty()?recoveryDirectory():rootOverride;
        if(base.isEmpty() || !QDir().mkpath(base))return {};
        QFile source(version.path);
        if(!source.open(QIODevice::ReadOnly))return {};
        const QString path=QDir(base).filePath(
            QStringLiteral("Recovered-")+
            QUuid::createUuid().toString(QUuid::WithoutBraces)+
            QStringLiteral(".kra"));
        QSaveFile destination(path);
        if(!destination.open(QIODevice::WriteOnly))return {};
        while(!source.atEnd()) {
            const QByteArray block=source.read(256*1024);
            if(block.isEmpty() || destination.write(block)!=block.size()) {
                destination.cancelWriting();
                return {};
            }
        }
        return destination.commit()?path:QString();
    }

    static bool isRecoveryCopy(const QString &path)
    {
        if(isProviderUri(path))return false;
        const QFileInfo file(path);
        const QString name=file.completeBaseName();
        const QString folder=recoveryDirectory();
        return !folder.isEmpty() && file.absolutePath()==folder &&
            !file.isSymLink() && file.isFile() &&
            file.suffix()==QStringLiteral("kra") &&
            name.startsWith(QStringLiteral("Recovered-")) &&
            !QUuid(name.mid(10)).isNull();
    }

private:
    static bool isProviderUri(const QString &source)
    {
        return source.startsWith(QStringLiteral("content://"),Qt::CaseInsensitive);
    }

    static QString recoveryDirectory()
    {
        const QString base=QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        return base.isEmpty()?QString():
            QDir(base).filePath(QStringLiteral("recovered-versions"));
    }

    static QMutex &lock()
    {
        static QMutex mutex;
        return mutex;
    }

    static QVector<Version> scan(const QString &folder)
    {
        QVector<Version> result;
        if(folder.isEmpty())return result;
        const QDir dir(folder);
        if(!dir.exists())return result;
        const auto files=dir.entryInfoList({QStringLiteral("*.kra")},
                         QDir::Files|QDir::Readable,
                         QDir::Name|QDir::Reversed);
        for(const QFileInfo &entry:files)
            if(entry.size()>0)
                result.append({entry.absoluteFilePath(),
                    entry.lastModified().toUTC(),entry.size()});
        return result;
    }

    static void prune(const QString &folder)
    {
        const QVector<Version> versions=scan(folder);
        qint64 keptBytes=0;
        int kept=0;
        for(const auto &entry:versions) {
            if(kept==0 || (kept<maxVersions() &&
               keptBytes+entry.bytes<=maxBytes())) {
                keptBytes+=entry.bytes;
                ++kept;
                continue;
            }
            QFile::remove(entry.path);
            QFile::remove(entry.path+QStringLiteral(".sha256"));
        }
    }
};
