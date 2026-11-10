/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Non-destructive v1 of Mobile Studio's project version store.
 * This stores COMPLETED native KRA saves in app-private storage. It does
 * not claim to capture unsaved strokes, background exits or SAF URIs yet.
 * Never modifies the drawing, the native save path or Krita's undo stack.
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

    // Existing filesystem-backed KRA documents only. The Android SAF
    // content URI stream and new unsaved drawings require a native Krita
    // export/checkpoint integration, not an unsafe QFile copy attempt.
    static QString folderFor(const QString &source,
                             const QString &rootOverride=QString())
    {
        if(source.isEmpty() || source.startsWith(
              QStringLiteral("content:"),Qt::CaseInsensitive))
            return {};
        const QFileInfo file(source);
        if(file.suffix().compare(QStringLiteral("kra"),Qt::CaseInsensitive)!=0)
            return {};
        const QString identity=QDir::cleanPath(
            file.canonicalFilePath().isEmpty()?
                file.absoluteFilePath():file.canonicalFilePath());
        if(identity.isEmpty())return {};
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
        QMutexLocker guard(&lock());
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
        QFileInfo original(source);
        if(!original.exists() || !original.isFile() ||
           original.size()<=0 || original.size()>qint64(8)*1024*1024*1024)
            return false;
        if(!QDir().mkpath(folder))return false;
        const QStorageInfo disk(folder);
        if(disk.isValid() && disk.isReady() &&
           disk.bytesAvailable()>=0 &&
           disk.bytesAvailable()<original.size()+16LL*1024*1024)
            return false;

        QFile input(source);
        if(!input.open(QIODevice::ReadOnly))return false;
        // Krita's native KRA serialization is ZIP-based. A truncated or
        // unrelated file should never be exposed as a recoverable version.
        if(input.peek(4)!=QByteArray::fromHex("504b0304"))
            return false;
        const QDateTime before=original.lastModified();
        const qint64 expected=original.size();
        const QString stem=QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMddTHHmmsszzz"))+QStringLiteral("-")+
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString target=QDir(folder).filePath(stem+QStringLiteral(".kra"));
        QSaveFile output(target);
        if(!output.open(QIODevice::WriteOnly))return false;

        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 copied=0;
        while(!input.atEnd()) {
            const QByteArray block=input.read(256*1024);
            if(block.isEmpty()) {
                output.cancelWriting();
                return false;
            }
            if(output.write(block)!=block.size()) {
                output.cancelWriting();
                return false;
            }
            hash.addData(block);
            copied+=block.size();
        }
        input.close();
        original.refresh();
        if(copied!=expected || original.size()!=expected ||
           original.lastModified()!=before) {
            output.cancelWriting(); // source changed during the copy
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
        const QString base=rootOverride.isEmpty()?
            QDir(QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("recovered-versions")):rootOverride;
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

private:
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
