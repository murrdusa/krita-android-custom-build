/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Completed native KRA saves in app-private storage, including Android
 * document providers. Unsaved edits are handled by private draft recovery.
 */
#pragma once
#include <QSettings>
#include <algorithm>
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
#include <QHash>
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

    static QVariantMap previewRevisions()
    {
        return QSettings().value(QStringLiteral("MobileStudio/savedPreviewRevisions")).toMap();
    }

    static void recordPreviewRevision(const QString &path)
    {
        const QString identity=canonicalIdentity(path);
        if(identity.isEmpty())return;
        auto revisions=previewRevisions();
        revisions.insert(identity,QDateTime::currentMSecsSinceEpoch());
        if(revisions.size()>128) {
            const auto oldest=std::min_element(revisions.cbegin(),revisions.cend(),
                [](const QVariant &a,const QVariant &b) {return a.toLongLong()<b.toLongLong();});
            revisions.remove(oldest.key());
        }
        QSettings().setValue(QStringLiteral("MobileStudio/savedPreviewRevisions"),revisions);
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
        // DocumentsContract identifies a document by its authority and
        // encoded document ID. Query parameters and URI fragments are
        // not part of that identity and can vary with a picker grant.
        QString documentUri=source;
        const int fragment=documentUri.indexOf(QLatin1Char('#'));
        if(fragment>=0)documentUri.truncate(fragment);
        const int query=documentUri.indexOf(QLatin1Char('?'));
        if(query>=0)documentUri.truncate(query);
        constexpr int schemeLength=10; // "content://"
        const int slash=documentUri.indexOf(QLatin1Char('/'),schemeLength);
        if(slash==-1)return documentUri.toLower();
        const QString authority=documentUri.mid(schemeLength,slash-schemeLength).toLower();
        const QString path=documentUri.mid(slash);
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
        // Keep the version browser's file list consistent with concurrent
        // background checkpoint commits and retention cleanup.
        QMutexLocker guard(&lock());
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
        // Check the source before acknowledging a rate-limited request.
        // A draft explicitly discarded or removed after its last archive
        // must never be reported as successfully checkpointed.
        constexpr qint64 copyLimit=8LL*1024*1024*1024;
        const bool provider=isProviderUri(source);
        QFileInfo original;
        if(!provider) {
            original.setFile(source);
            if(!original.isFile() || original.isSymLink())return false;
        }
        QFile input(source);
        if(!input.open(QIODevice::ReadOnly) ||
           input.peek(4)!=QByteArray::fromHex("504b0304"))return false;
        const qint64 expected=provider?input.size():original.size();
        if(expected<=0 || expected>copyLimit)return false;
        if(minGapMs>0) {
            const auto versions=scan(folder);
            if(!versions.isEmpty()) {
                const Version &recent=versions.first();
                const qint64 elapsed=recent.createdUtc.msecsTo(
                    QDateTime::currentDateTimeUtc());
                // Rate-limit only against a complete, parseable archive
                // pair. A missing/corrupt SHA sidecar or replaced KRA
                // must not suppress a fresh recoverable checkpoint.
                if(elapsed>=0 && elapsed<minGapMs &&
                   hasPublishedCheckpoint(recent))return true;
            }
        }
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
            // Always verify the archived *bytes*, even if its SHA-256
            // sidecar contains a different valid-looking digest. A bad
            // sidecar must not fork identical versions or permanently
            // render intact history unrecoverable.
            QFile last(previous.first().path);
            if(last.open(QIODevice::ReadOnly)) {
                QCryptographicHash actual(QCryptographicHash::Sha256);
                QByteArray chunk(256*1024,Qt::Uninitialized);
                qint64 got=0;
                qint64 hashed=0;
                while((got=last.read(chunk.data(),chunk.size()))>0) {
                    hashed+=got;
                    actual.addData(chunk.constData(),got);
                }
                if(got==0 && hashed==copied &&
                   actual.result().toHex()==sha) {
                    // A private draft can disappear while we inspect its
                    // prior archive. Never acknowledge a discarded or
                    // concurrently overwritten draft as successfully
                    // checkpointed.
                    if(!provider) {
                        original.refresh();
                        if(!original.isFile() || original.size()!=expected ||
                           original.lastModified()!=before)return false;
                    }
                    if(oldSha.toLower()!=sha) {
                        QSaveFile repair(previous.first().path+
                                         QStringLiteral(".sha256"));
                        if(!repair.open(QIODevice::WriteOnly))return false;
                        if(repair.write(sha)!=sha.size() ||
                           !repair.commit())return false;
                    }
                    output.cancelWriting(); // unchanged intact save
                    return true;
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
        // A freshly published version must carry its checksum. Otherwise
        // a disk-full/permission failure silently leaves an unverifiable
        // entry that recovery may later treat as a legacy archive.
        if(!digestFile.open(QIODevice::WriteOnly) ||
           digestFile.write(sha)!=sha.size() ||
           !digestFile.commit()) {
            digestFile.cancelWriting();
            QFile::remove(target);
            return false;
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
        const QString canonical=canonicalIdentity(identity);
        if(canonical.isEmpty() || draft.isEmpty())return;
        const QString requestKey=draft+QLatin1Char('\n')+canonical;
        {
            // Archive copying holds lock() across file I/O. Never acquire
            // that mutex on the UI thread just to submit another request.
            QMutexLocker guard(&requestLock());
            auto &pending=pendingDraftArchives();
            auto it=pending.find(requestKey);
            if(it!=pending.end()) {
                // The running job must recheck the latest published draft
                // after it finishes. A background request upgrades the
                // following pass so the two-minute historical spacing
                // cannot swallow a background checkpoint.
                ++it->generation;
                it->background |= backgroundCheckpoint;
                return;
            }
            PendingDraftRequest request;
            request.background=backgroundCheckpoint;
            pending.insert(requestKey,request);
        }
        QThreadPool::globalInstance()->start([draft,identity,requestKey]() {
            for(;;) {
                quint64 generation=0;
                bool background=false;
                {
                    QMutexLocker guard(&requestLock());
                    auto it=pendingDraftArchives().find(requestKey);
                    if(it==pendingDraftArchives().end())return;
                    generation=it->generation;
                    background=it->background;
                    it->background=false;
                }
                // Serialize KRA copies through the archive's existing
                // lock. Keep the request mutex free during disk I/O.
                snapshotFrom(draft,identity,QString(),
                             background?0:2LL*60*1000);
                {
                    QMutexLocker guard(&requestLock());
                    auto &pending=pendingDraftArchives();
                    auto it=pending.find(requestKey);
                    if(it==pending.end())return;
                    if(it->generation==generation) {
                        pending.erase(it);
                        return;
                    }
                }
                // A newer completion or app background event occurred
                // during I/O: process that latest state in this worker.
            }
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
        const QString key=canonicalIdentity(source);
        if(key.isEmpty())return;
        {
            // A sequence of explicit Saves must not queue an unbounded
            // sequence of full-sized KRA copies. Preserve adoption from
            // every untitled draft and the newest completed Save request.
            QMutexLocker guard(&requestLock());
            auto &pending=pendingSaves();
            auto it=pending.find(key);
            if(it!=pending.end()) {
                ++it->generation;
                it->source=source;
                if(!previousDraft.isEmpty() &&
                   !it->drafts.contains(previousDraft))
                    it->drafts.append(previousDraft);
                return;
            }
            PendingSaveRequest request;
            request.source=source;
            if(!previousDraft.isEmpty())request.drafts.append(previousDraft);
            pending.insert(key,request);
        }
        QThreadPool::globalInstance()->start([key]() {
            for(;;) {
                quint64 generation=0;
                QString source;
                QStringList drafts;
                {
                    QMutexLocker guard(&requestLock());
                    const auto it=pendingSaves().constFind(key);
                    if(it==pendingSaves().cend())return;
                    generation=it->generation;
                    source=it->source;
                    drafts=it->drafts;
                }
                for(const QString &draft:drafts)
                    adoptHistory(draft,source);
                snapshot(source);
                {
                    QMutexLocker guard(&requestLock());
                    auto &pending=pendingSaves();
                    auto it=pending.find(key);
                    if(it==pending.end())return;
                    if(it->generation==generation) {
                        pending.erase(it);
                        return;
                    }
                    // The completed migrations need not run again on
                    // the next pass; retain only newly received drafts.
                    for(const QString &draft:drafts)
                        it->drafts.removeAll(draft);
                }
            }
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
        // The browser filters symlinks, but recovery may be requested
        // using a previously captured entry after files have changed.
        // Never follow a replaced archive or checksum symlink.
        if(QFileInfo(version.path).isSymLink())return {};
        QFile source(version.path);
        if(!source.open(QIODevice::ReadOnly))return {};
        // An archive chosen from the version browser has a known file
        // length. Reject stale/oversized entries and check destination
        // space before streaming a potentially large recovered KRA.
        constexpr qint64 copyLimit=8LL*1024*1024*1024;
        const qint64 size=source.size();
        if(size<=0 || size>copyLimit ||
           (version.bytes>0 && version.bytes!=size))return {};
        const QStorageInfo disk(base);
        if(disk.isValid() && disk.isReady() &&
           disk.bytesAvailable()>=0 &&
           disk.bytesAvailable()<size+16LL*1024*1024)return {};
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
        if(QFileInfo(digest.fileName()).isSymLink())return {};
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

    struct PendingDraftRequest {
        quint64 generation=1;
        bool background=false;
    };

    static QMutex &requestLock()
    {
        static QMutex mutex;
        return mutex;
    }

    static QHash<QString,PendingDraftRequest> &pendingDraftArchives()
    {
        // Protected only by requestLock(), never by the archive I/O lock.
        static QHash<QString,PendingDraftRequest> requests;
        return requests;
    }

    struct PendingSaveRequest {
        quint64 generation=1;
        QString source;
        QStringList drafts;
    };

    static QHash<QString,PendingSaveRequest> &pendingSaves()
    {
        static QHash<QString,PendingSaveRequest> requests;
        return requests;
    }

    static void migrateFolder(const QString &from, const QString &to)
    {
        if(from.isEmpty() || to.isEmpty() || from==to)return;
        QMutexLocker guard(&lock());
        const QDir fromDir(from);
        if(!fromDir.exists())return;

        if(!QDir(to).exists()) {
            if(QDir().rename(from,to)) {
                prune(to);
                return;
            }
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
            const QString targetDigest=targetPath+QStringLiteral(".sha256");
            const bool hasDigest=QFile::exists(oldDigest);
            // Install the tiny checksum sidecar first. If that copy
            // fails, leave the original KRA/digest pair untouched so
            // history migration can be retried. A failed KRA rename
            // removes only the newly copied destination sidecar.
            if(hasDigest && !QFile::copy(oldDigest,targetDigest))
                continue;
            if(!QFile::rename(entry.absoluteFilePath(),targetPath)) {
                if(hasDigest)QFile::remove(targetDigest);
                continue;
            }
            if(hasDigest)QFile::remove(oldDigest);
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

    static bool hasPublishedCheckpoint(const Version &version)
    {
        if(version.path.isEmpty() || QFileInfo(version.path).isSymLink())
            return false;
        QFile archive(version.path);
        if(!archive.open(QIODevice::ReadOnly) ||
           archive.peek(4)!=QByteArray::fromHex("504b0304") ||
           archive.size()!=version.bytes)return false;
        const QString digestPath=version.path+QStringLiteral(".sha256");
        if(QFileInfo(digestPath).isSymLink())return false;
        QFile digest(digestPath);
        if(!digest.open(QIODevice::ReadOnly))return false;
        const QByteArray sha=digest.read(65).trimmed().toLower();
        return sha.size()==64 &&
            QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                .match(QString::fromLatin1(sha)).hasMatch();
    }

    static QVector<Version> scan(const QString &folder)
    {
        QVector<Version> result;
        if(folder.isEmpty())return result;
        const QDir dir(folder);
        if(!dir.exists())return result;
        const auto files=dir.entryInfoList({QStringLiteral("*.kra")},
                         QDir::Files|QDir::Readable|QDir::NoSymLinks,
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
