/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Completed native KRA saves in app-private storage, including Android
 * document providers. Unsaved edits are handled by private draft recovery.
 */
#pragma once
#include <QRegularExpression>
#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#endif

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

    static bool isProviderUri(const QString &source)
    {
        return source.startsWith(QStringLiteral("content://"),Qt::CaseInsensitive);
    }

    static bool isKritaProjectName(const QString &name)
    {
        static const QRegularExpression extension(
            QStringLiteral(R"(\.kr[az](?: \(\d+\))?$)"),
            QRegularExpression::CaseInsensitiveOption);
        return extension.match(name).hasMatch();
    }

    static QString projectName(const QString &path)
    {
#ifdef Q_OS_ANDROID
        if(isProviderUri(path)) {
            const auto uri=QAndroidJniObject::fromString(path);
            const auto name=QAndroidJniObject::callStaticObjectMethod(
                "org/krita/android/MainActivity","tabletProjectName",
                "(Ljava/lang/String;)Ljava/lang/String;",uri.object<jstring>()).toString();
            if(!name.isEmpty())return name;
        }
#endif
        return QFileInfo(path).fileName();
    }

    static qint64 projectModified(const QString &path)
    {
#ifdef Q_OS_ANDROID
        if(isProviderUri(path)) {
            const auto uri=QAndroidJniObject::fromString(path);
            return QAndroidJniObject::callStaticMethod<jlong>(
                "org/krita/android/MainActivity","tabletProjectModified","(Ljava/lang/String;)J",
                uri.object<jstring>());
        }
#endif
        return QFileInfo(path).lastModified().toMSecsSinceEpoch();
    }

    // Normalizes file and provider identities so that different access methods
    // (such as Android SAF folder tree grants vs direct document URIs) map to
    // the same underlying document and project version history.
    static QString canonicalIdentity(const QString &source)
    {
        if(source.isEmpty())return {};
        if(!isProviderUri(source)) {
            const QFileInfo file(source);
            if(!isKritaProjectName(file.fileName()))return {};
            const QString canonical=file.canonicalFilePath();
            return QDir::cleanPath(canonical.isEmpty()?
                                    file.absoluteFilePath():canonical);
        }
        constexpr int schemeLength=10; // "content://"
        const int slash=source.indexOf(QLatin1Char('/'),schemeLength);
        if(slash==-1)return source;
        const QString authority=source.mid(schemeLength,slash-schemeLength).toLower();
        const QString path=source.mid(slash);
        // Android DocumentsContract: tree document URIs have the form:
        // /tree/<treeDocumentId>/document/<documentId>
        // Direct document URIs have the form:
        // /document/<documentId>
        // Both refer to the exact same underlying document (authority + documentId).
        if(path.startsWith(QStringLiteral("/tree/"))) {
            const int docIndex=path.indexOf(QStringLiteral("/document/"),6);
            if(docIndex!=-1) {
                return QStringLiteral("content://")+authority+path.mid(docIndex);
            }
        }
        return QStringLiteral("content://")+authority+path;
    }

    static QString folderFor(const QString &source,
                             const QString &rootOverride=QString())
    {
        if(source.isEmpty())return {};
        const QString identity=canonicalIdentity(source);
        if(identity.isEmpty())return {};
        const QString base=rootOverride.isEmpty()?
            QDir(QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("saved-versions")):rootOverride;
        if(base.isEmpty())return {};
        const QString digest=QString::fromLatin1(QCryptographicHash::hash(
            identity.toUtf8(),QCryptographicHash::Sha256).toHex());
        const QString canonicalFolder=QDir(base).filePath(digest);
        if(source!=identity) {
            const QString legacyDigest=QString::fromLatin1(QCryptographicHash::hash(
                source.toUtf8(),QCryptographicHash::Sha256).toHex());
            const QString legacyFolder=QDir(base).filePath(legacyDigest);
            if(legacyFolder!=canonicalFolder && QDir(legacyFolder).exists()) {
                migrateFolder(legacyFolder,canonicalFolder);
            }
        }
        return canonicalFolder;
    }

    static QVector<Version> list(const QString &source,
                                const QString &rootOverride=QString())
    {
        return scan(folderFor(source,rootOverride));
    }

    // Also archive the completed private KRA autosave under the original
    // project's identity (or under the private draft path if unsaved).
    // This does not modify the original project or its autosave file.
    static bool snapshotFrom(const QString &source,const QString &identity,
                             const QString &rootOverride=QString(),
                             qint64 minGapMs=0)
    {
        QMutexLocker guard(&lock());
        const QString folder=folderFor(identity,rootOverride);
        if(folder.isEmpty())return false;
        if(minGapMs>0) {
            const auto versions=scan(folder);
            if(!versions.isEmpty()) {
                const qint64 elapsed=versions.first().createdUtc.msecsTo(
                    QDateTime::currentDateTimeUtc());
                if(elapsed>=0 && elapsed<minGapMs)return true;
            }
        }
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
        if(!previous.isEmpty() && previous.first().bytes==copied) {
            QFile sidecar(previous.first().path+QStringLiteral(".sha256"));
            QByteArray oldSha;
            if(sidecar.open(QIODevice::ReadOnly))
                oldSha=sidecar.readAll().trimmed();
            // A checksum sidecar alone is not proof that the archived
            // bytes are intact. If it matches the new source (or is
            // missing), hash the actual previous KRA before deduplicating.
            // A damaged latest version must never suppress a new copy.
            if(oldSha.size()!=64 || oldSha==sha) {
                QFile last(previous.first().path);
                if(last.open(QIODevice::ReadOnly)) {
                    QCryptographicHash actual(QCryptographicHash::Sha256);
                    QByteArray chunk(256*1024,Qt::Uninitialized);
                    qint64 got=0;
                    while((got=last.read(chunk.data(),chunk.size()))>0)
                        actual.addData(chunk.constData(),got);
                    if(got==0 && actual.result().toHex()==sha) {
                        if(oldSha!=sha) {
                            // A prior interrupted publication can leave
                            // the KRA intact but without its checksum.
                            // Repair the sidecar atomically while the
                            // verified content still matches the source.
                            QSaveFile repair(previous.first().path+
                                             QStringLiteral(".sha256"));
                            if(repair.open(QIODevice::WriteOnly)) {
                                if(repair.write(sha)==sha.size())
                                    repair.commit();
                                else
                                    repair.cancelWriting();
                            }
                        }
                        output.cancelWriting(); // unchanged intact save
                        return true;
                    }
                }
            }
        }
        // The caller may have discarded/removed the private draft while
        // we were hashing the previous version. Do not publish the newly
        // copied edits after their source has disappeared or changed.
        if(!provider) {
            original.refresh();
            if(!original.isFile() || original.size()!=expected ||
               original.lastModified()!=before)return false;
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

    // Original completed Save snapshots use the same safe copy path as
    // completed autosave drafts. Different source and identity let an
    // unsaved private draft share history with a later saved project.
    static bool snapshot(const QString &source,
                         const QString &rootOverride=QString())
    {
        return snapshotFrom(source,source,rootOverride);
    }

    static void enqueueDraftCheckpoint(const QString &draft,
                                       const QString &identity,
                                       bool backgroundCheckpoint=false)
    {
        if(folderFor(identity).isEmpty())return;
        QThreadPool::globalInstance()->start(
            [draft,identity,backgroundCheckpoint]() {
                // The latest recovery draft continues updating every
                // 30 seconds. Space full historical copies at least
                // two minutes apart while painting; allow one when
                // backgrounded so an interrupted session has a version.
                snapshotFrom(draft,identity,QString(),
                             backgroundCheckpoint?0:2LL*60*1000);
            });
    }

    // Move history created while an untitled drawing was still a draft
    // into the saved project's history, before taking a completed-save
    // checkpoint. Serialized with draft snapshots by the archive mutex.
    static void adoptHistory(const QString &previousDraft,
                             const QString &savedPath,
                             const QString &rootOverride=QString())
    {
        QMutexLocker guard(&lock());
        const QString from=folderFor(previousDraft,rootOverride);
        const QString to=folderFor(savedPath,rootOverride);
        if(from.isEmpty() || to.isEmpty() || from==to)return;
        migrateFolder(from,to);
    }

    static void enqueueAfterSave(const QString &source,
                                 const QString &previousDraft=QString())
    {
        if(folderFor(source).isEmpty())return;
        QThreadPool::globalInstance()->start([source,previousDraft]() {
            if(!previousDraft.isEmpty())
                adoptHistory(previousDraft,source);
            snapshot(source);
        });
    }

    // Open a separate private copy of a version, not the stored snapshot
    // or the user's original file. Saving a recovered document can
    // therefore never overwrite a historical checkpoint by accident.
    static QString materializeRecovery(const Version &version,
                                       const QString &rootOverride=QString())
    {
        // Snapshot publication, checksum sidecar changes, and retention
        // pruning all share this lock. Recovery must read a consistent
        // archived pair rather than racing deletion of an old version.
        QMutexLocker guard(&lock());
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
        // Reject damaged snapshots before publishing a recovery copy.
        // Older checkpoints may legitimately lack the optional sidecar.
        QByteArray expectedHash;
        QFile digest(version.path+QStringLiteral(".sha256"));
        if(digest.exists()) {
            if(!digest.open(QIODevice::ReadOnly))return {};
            expectedHash=digest.readAll().trimmed().toLower();
            if(expectedHash.size()!=64 ||
               !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                    .match(QString::fromLatin1(expectedHash)).hasMatch())
                return {};
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray block(256*1024,Qt::Uninitialized);
        qint64 copied=0;
        qint64 count=0;
        while((count=source.read(block.data(),block.size()))>0) {
            if(copied==0 &&
               (count<4 || QByteArray(block.constData(),4)!=
                   QByteArray::fromHex("504b0304")))return {};
            copied+=count;
            if(destination.write(block.constData(),count)!=count)return {};
            hash.addData(block.constData(),count);
        }
        if(count!=0 || copied<=0 ||
           (version.bytes>0 && copied!=version.bytes) ||
           (!expectedHash.isEmpty() && hash.result().toHex()!=expectedHash))
            return {};
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
    static QString recoveryDirectory()
    {
        const QString base=QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        return base.isEmpty()?QString():
            QDir(base).filePath(QStringLiteral("recovered-versions"));
    }

    static QMutex &lock()
    {
        static QMutex mutex(QMutex::Recursive);
        return mutex;
    }

    static void migrateFolder(const QString &from, const QString &to)
    {
        if(from.isEmpty() || to.isEmpty() || from==to)return;
        QMutexLocker guard(&lock());
        const QDir fromDir(from);
        if(!fromDir.exists())return;

        if(!QDir(to).exists()) {
            if(QDir().rename(from,to))return;
        }

        if(!QDir().mkpath(to))return;
        // Move complete KRA/sidecar pairs first. Legacy and canonical
        // folders may contain the same checkpoint name; never delete a
        // source version merely because a destination entry exists.
        const auto versions=fromDir.entryInfoList({QStringLiteral("*.kra")},
            QDir::Files|QDir::Readable|QDir::NoSymLinks);
        for(const QFileInfo &entry:versions) {
            QString targetPath=QDir(to).filePath(entry.fileName());
            const QString oldDigest=entry.absoluteFilePath()+
                                    QStringLiteral(".sha256");
            if(QFile::exists(targetPath) ||
               QFile::exists(targetPath+QStringLiteral(".sha256"))) {
                do {
                    targetPath=QDir(to).filePath(entry.completeBaseName()+
                        QLatin1Char('-')+
                        QUuid::createUuid().toString(QUuid::WithoutBraces)+
                        QStringLiteral(".kra"));
                } while(QFile::exists(targetPath) ||
                        QFile::exists(targetPath+QStringLiteral(".sha256")));
            }
            if(!QFile::rename(entry.absoluteFilePath(),targetPath))
                continue; // retain source if moving fails
            if(QFile::exists(oldDigest))
                QFile::rename(oldDigest,targetPath+QStringLiteral(".sha256"));
        }
        // Preserve any other metadata files; never overwrite unrelated
        // destination content, even if migration is retried.
        const auto leftovers=fromDir.entryInfoList(
            QDir::Files|QDir::Readable|QDir::Hidden|QDir::NoSymLinks);
        for(const QFileInfo &entry:leftovers) {
            // A checksum must only travel with its corresponding KRA.
            // Moving an orphaned digest under its old name could attach
            // it to a *different* canonical snapshot after a collision.
            if(entry.fileName().endsWith(QStringLiteral(".kra.sha256")))
                continue;
            const QString targetPath=QDir(to).filePath(entry.fileName());
            if(!QFile::exists(targetPath))
                QFile::rename(entry.absoluteFilePath(),targetPath);
        }
        fromDir.rmdir(from); // succeeds only if fully migrated
        prune(to);
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
