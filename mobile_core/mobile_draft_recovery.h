/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Private locations for Krita's native autosave files. Saved project
 * versions are managed separately by MobileSavedVersionArchive.
 */
#pragma once

#include <QDir>
#include <QDirIterator>
#include <QMutex>
#include <memory>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <cstdio>
#include <atomic>
#include <algorithm>
#include "saved_version_archive.h"
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <KoStore.h>
#include <QScopedPointer>
#endif

// The background checkpoint watchdog runs only while a document remains
// busy saving. Back off on long saves rather than waking Android every
// two seconds indefinitely; normal save-completion signals still trigger
// the immediate continuation.
struct MobilePendingDraftRetryPolicy final
{
    static int timeoutMs(int attempts)
    {
        const int level=qBound(0,attempts,3);
        return 2000 << level;
    }
    static int afterTimeoutScheduled(int attempts)
    {
        return attempts<0?1:(attempts<3?attempts+1:3);
    }
};

class MobileDraftRecovery final
{
public:
    static QString directory()
    {
        const QString appData=QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        return appData.isEmpty()?QString():
            QDir(appData).filePath(QStringLiteral("mobile-drafts"));
    }

    static QString allocate(const QString &title)
    {
        const QString folder=directory();
        if(folder.isEmpty() || !QDir().mkpath(folder))return {};
        QString safe=title.simplified().left(48);
        for(int i=0;i<safe.size();++i) {
            const QChar ch=safe.at(i);
            if(!ch.isLetterOrNumber() && ch!=QLatin1Char(' ') &&
               ch!=QLatin1Char('-') && ch!=QLatin1Char('_'))
                safe[i]=QLatin1Char('_');
        }
        if(safe.isEmpty())safe=QStringLiteral("Untitled");
        return QDir(folder).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+
                                    QLatin1Char('_')+safe+QStringLiteral(".kra"));
    }

    // A candidate can be validated before its first checkpoint exists.
    // Never allow a draft publication or sidecar write to escape the
    // private recovery directory, even if a caller supplies a bad path.
    static bool isDraftPath(const QString &path)
    {
        const QString folder=directory();
        if(folder.isEmpty() || path.isEmpty())return false;
        const QFileInfo file(path);
        const QString name=file.completeBaseName();
        return QDir::cleanPath(file.absolutePath())==QDir::cleanPath(folder) &&
            !file.isSymLink() &&
            file.suffix()==QStringLiteral("kra") && name.size()>37 &&
            name.at(36)==QLatin1Char('_') && !QUuid(name.left(36)).isNull() &&
            !path.endsWith(QStringLiteral(".pending.kra"));
    }

    static bool isDraft(const QString &path)
    {
        const QFileInfo file(path);
        return isDraftPath(path) && file.isFile() && file.size()>0;
    }

    // A cheap startup guard, not full ZIP validation. Manual recovery
    // retains damaged drafts so users can still inspect them.
    static bool hasKraHeader(const QString &path)
    {
        // Do not feed an oversized, replaced, or special filesystem entry
        // to KoStore during background recovery discovery.
        constexpr qint64 maxArchiveBytes=8LL*1024*1024*1024;
        const QFileInfo entry(path);
        if(!entry.isFile() || entry.isSymLink() ||
           entry.size()<4 || entry.size()>maxArchiveBytes)return false;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))return false;
        // The path can be replaced between QFileInfo and open(). Check
        // the opened handle as well, so a newly oversized or special
        // file does not pass the cheap recovery discovery guard.
        const QFileInfo opened(file);
        return opened.isFile() && !opened.isSymLink() &&
            file.size()>=4 && file.size()<=maxArchiveBytes &&
            file.peek(4)==QByteArray::fromHex("504b0304");
    }

    static bool hasReadableProject(const QString &path)
    {
        if(!hasKraHeader(path))return false;
#ifdef Q_OS_ANDROID
        QScopedPointer<KoStore> store(KoStore::createStore(path,KoStore::Read));
        return store && !store->bad() && store->hasFile(QStringLiteral("maindoc.xml"));
#else
        return true;
#endif
    }

    // maxResults limits the retained gallery snapshot. An optional
    // modified-time cutoff skips drafts that cannot supersede the saved
    // project, without losing older drafts from unrestricted recovery
    // browsing or deleting any files.
    // Rescue only a small batch of old complete staging files. Keep this
    // separate from published-draft enumeration: project deletion needs
    // to check whether a draft exists, not build a sorted gallery list.
    static void rescueOrphanStaging(const QString &folder)
    {
        if(folder.isEmpty())return;
        // If the process terminated between KRA export and publication,
        // a complete .pending.kra would otherwise never appear in the
        // recovery browser. Only rescue old, readable staging files:
        // recent files may belong to an export still in progress.
        const QDateTime now=QDateTime::currentDateTimeUtc();
        const QString suffix=QStringLiteral(".pending.kra");
        // Opening a large KRA to validate maindoc.xml can block the
        // gallery's UI thread. Process only a small batch per listing;
        // subsequent refreshes continue rescuing the remaining files.
        constexpr int maxOrphanChecksPerScan=2;
        // Walk at most 128 directory entries per gallery visit, but
        // stop *as soon as* two eligible files are selected. Advancing
        // the iterator by 128 and then checking only two used to skip
        // dozens of recoverable drafts until the cursor wrapped around.
        constexpr int maxOrphanCandidatesPerScan=128;
        QFileInfoList candidates;
        candidates.reserve(maxOrphanChecksPerScan);
        {
            static QMutex cursorLock;
            static std::unique_ptr<QDirIterator> cursor;
            static QString cursorFolder;
            QMutexLocker guard(&cursorLock);
            if(folder!=cursorFolder) {
                cursor.reset();
                cursorFolder=folder;
            }
            if(!cursor)
                cursor.reset(new QDirIterator(folder,
                    {QStringLiteral("*.pending.kra")},
                    QDir::Files|QDir::Readable|QDir::NoSymLinks));
            for(int i=0;i<maxOrphanCandidatesPerScan &&
                            candidates.size()<maxOrphanChecksPerScan;++i) {
                if(!cursor->hasNext()) {
                    // Resume from the beginning on the next gallery visit,
                    // not repeatedly within one scan of a small directory.
                    cursor.reset();
                    break;
                }
                cursor->next();
                const QFileInfo pending=cursor->fileInfo();
                if(pending.lastModified().toUTC().secsTo(now)<300)continue;
                const QString path=pending.absoluteFilePath();
                const QString draft=path.left(path.size()-suffix.size());
                if(!isDraftPath(draft))continue;
                candidates.append(pending);
            }
        }
        for(const QFileInfo &pending:candidates) {
            const QString path=pending.absoluteFilePath();
            const QString draft=path.left(path.size()-suffix.size());
            if(!hasReadableProject(path))continue;
            // An exporter that resumed writing during validation must
            // retain ownership of its staging file.
            if(!unchangedStaging(pending))continue;
            // False prevents replacing the previous published draft.
            publish(draft,false);
        }
    }

    static QFileInfoList list(int maxResults=0,qint64 newerThanMs=0)
    {
        const QString folder=directory();
        if(folder.isEmpty())return {};
        rescueOrphanStaging(folder);
        // For the gallery, retain only the newest maxResults entries
        // while streaming directory contents. A damaged recovery folder
        // may contain far more files than the UI's 20k-project limit.
        // Other recovery callers keep the complete newest-first list.
        QFileInfoList drafts;
        const auto newer=[](const QFileInfo &a,const QFileInfo &b) {
            if(a.lastModified()!=b.lastModified())
                return a.lastModified()>b.lastModified();
            return a.fileName()>b.fileName();
        };
        QDirIterator completed(folder,{QStringLiteral("*.kra")},
                               QDir::Files|QDir::Readable|QDir::NoSymLinks);
        while(completed.hasNext()) {
            completed.next();
            const QFileInfo file=completed.fileInfo();
            if(newerThanMs>0 &&
               file.lastModified().toMSecsSinceEpoch()<=newerThanMs)
                continue;
            if(!isDraft(file.absoluteFilePath()))continue;
            if(maxResults<=0 || drafts.size()<maxResults) {
                drafts.append(file);
                if(maxResults>0)
                    std::push_heap(drafts.begin(),drafts.end(),newer);
            } else if(newer(file,drafts.front())) {
                std::pop_heap(drafts.begin(),drafts.end(),newer);
                drafts.last()=file;
                std::push_heap(drafts.begin(),drafts.end(),newer);
            }
        }
        // checkpointsFor() relies on newest-first order to stop once
        // entries are older than the saved project's modification time.
        std::sort(drafts.begin(),drafts.end(),newer);
        return drafts;
    }

    // Recheck a saved directory entry after slow ZIP verification.
    // QFileInfo caches its initial attributes, so a fresh instance is
    // required to detect a concurrent exporter updating the same path.
    static bool unchangedStaging(const QFileInfo &before)
    {
        const QFileInfo current(before.absoluteFilePath());
        return current.isFile() && !current.isSymLink() &&
            before.size()==current.size() &&
            before.lastModified()==current.lastModified();
    }

    static bool setProject(const QString &draft,const QString &project)
    {
        if(!isDraftPath(draft))return false;
        const QString metadata=draft+QStringLiteral(".project");
        if(MobileSavedVersionArchive::canonicalIdentity(project).isEmpty()) {
            return !QFile::exists(metadata) || QFile::remove(metadata);
        }
        if(projectFor(draft)==project)return true;
        QSaveFile file(metadata);
        const QByteArray data=project.toUtf8();
        return data.size()<=16384 && file.open(QIODevice::WriteOnly) &&
            file.write(data)==data.size() && file.commit();
    }

    static bool publish(const QString &draft,bool exportSucceeded)
    {
        if(!isDraftPath(draft))return false;
        const QString staging=draft+QStringLiteral(".pending.kra");
        const QFileInfo before(staging);
        if(!before.isFile() || before.isSymLink())return false;
        // The exporter can report failure after writing an otherwise
        // complete KRA (for example if its final flush failed). Never
        // replace the last published draft when exportSucceeded is false.
        // A readable staging file may still be the only recoverable copy.
        if(!hasReadableProject(staging)) {
            // Even when the exporter reports success, a failed validation
            // can mean it raced another writer or observed a partially
            // flushed archive. Do not delete bytes that may contain the
            // only surviving unsaved strokes. A later export may replace
            // this staging pathname; the last published draft stays intact.
            return false;
        }
        // ZIP validation can take significant time for a large draft.
        // If an exporter resumed or replaced this staging file while it
        // was checked, do not overwrite the previous completed draft
        // with bytes that have not been verified.
        if(!unchangedStaging(before))return false;
        if(exportSucceeded &&
           std::rename(QFile::encodeName(staging).constData(),
                       QFile::encodeName(draft).constData())==0)return true;
        // A .pending.kra is excluded from the recovery browser. Rescue
        // any readable but unpublished result under a separate UUID, so
        // failure does not erase previously published edits or this copy.
        const QString rescue=allocate(title(QFileInfo(draft)));
        if(!rescue.isEmpty() && !QFileInfo(rescue).exists() &&
           std::rename(QFile::encodeName(staging).constData(),
                       QFile::encodeName(rescue).constData())==0) {
            const QString originalProject=projectFor(draft);
            if(!originalProject.isEmpty())setProject(rescue,originalProject);
            // Keep a user-supplied draft title with the recovered copy.
            // Do not follow linked or unexpectedly large sidecars.
            const QString titleSidecar=draft+QStringLiteral(".title");
            const QFileInfo titleEntry(titleSidecar);
            if(titleEntry.isFile() && !titleEntry.isSymLink() &&
               titleEntry.size()>0 && titleEntry.size()<=384) {
                QFile titleFile(titleSidecar);
                if(titleFile.open(QIODevice::ReadOnly)) {
                    const QByteArray titleData=titleFile.read(385);
                    if(!titleData.isEmpty() && titleData.size()<=384) {
                        QSaveFile output(rescue+QStringLiteral(".title"));
                        if(output.open(QIODevice::WriteOnly) &&
                           output.write(titleData)==titleData.size())
                            output.commit();
                    }
                }
            }
        }
        return false;
    }

    static QString projectFor(const QString &draft)
    {
        // Recovery associations must come only from private draft
        // metadata, not a substituted link to another project's file.
        if(!isDraftPath(draft))return {};
        const QString metadata=draft+QStringLiteral(".project");
        if(QFileInfo(metadata).isSymLink())return {};
        QFile file(metadata);
        constexpr qint64 maxAssociationBytes=16384;
        if(!file.open(QIODevice::ReadOnly) || file.size()<0 ||
           file.size()>maxAssociationBytes)return {};
        // The sidecar can grow after the size check (for example during
        // an interrupted or external metadata update). Never let an
        // unbounded readAll() allocate memory based on a moving file.
        const QByteArray association=file.read(maxAssociationBytes+1);
        if(association.size()>maxAssociationBytes)return {};
        return QString::fromUtf8(association);
    }

    static QFileInfoList checkpointsFor(const QString &project,qint64 savedModified)
    {
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(project);
        const QString folder=directory();
        if(identity.isEmpty() || folder.isEmpty())return {};
        // Preserve bounded recovery of interrupted .pending.kra exports,
        // but do not materialize and sort every unrelated draft merely
        // to find checkpoints for one named project.
        rescueOrphanStaging(folder);
        QFileInfoList result;
        QDirIterator completed(folder,{QStringLiteral("*.kra")},
                               QDir::Files|QDir::Readable|QDir::NoSymLinks);
        while(completed.hasNext()) {
            completed.next();
            const QFileInfo draft=completed.fileInfo();
            if(draft.lastModified().toMSecsSinceEpoch()<=savedModified ||
               !isDraft(draft.absoluteFilePath()))
                continue;
            if(MobileSavedVersionArchive::canonicalIdentity(
                   projectFor(draft.absoluteFilePath()))==identity &&
               hasReadableProject(draft.absoluteFilePath()))
                result.append(draft);
        }
        // Recovery offers the newest valid project checkpoint first,
        // breaking timestamp ties deterministically like list().
        std::sort(result.begin(),result.end(),
                  [](const QFileInfo &a,const QFileInfo &b) {
            if(a.lastModified()!=b.lastModified())
                return a.lastModified()>b.lastModified();
            return a.fileName()>b.fileName();
        });
        return result;
    }

    // A named project with newer, still-recoverable private edits must
    // not be deleted from the gallery before the draft is explicitly
    // discarded or recovered. Compare normalized local/SAF identities.
    static bool hasProjectDraft(const QString &project)
    {
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(project);
        if(identity.isEmpty())return false;
        // Keep the same bounded rescue of interrupted exports without
        // paying for list(1)'s redundant published-draft directory pass.
        // A single streaming pass below answers this existence query.
        const QString folder=directory();
        if(folder.isEmpty())return false;
        rescueOrphanStaging(folder);
        QDirIterator completed(folder,{QStringLiteral("*.kra")},
                               QDir::Files|QDir::Readable|QDir::NoSymLinks);
        while(completed.hasNext()) {
            completed.next();
            const QString path=completed.fileInfo().absoluteFilePath();
            if(!isDraft(path))continue;
            if(MobileSavedVersionArchive::canonicalIdentity(
                   projectFor(path))==identity)
                return true;
        }
        return false;
    }

    static bool rename(const QString &path,const QString &name)
    {
        const QByteArray data=name.trimmed().left(96).toUtf8();
        if(data.isEmpty() || !isDraft(path))return false;
        QSaveFile file(path+QStringLiteral(".title"));
        return file.open(QIODevice::WriteOnly) && file.write(data)==data.size() &&
            file.commit();
    }

    static QString displayTitle(const QFileInfo &draft,const QString &generated)
    {
        const QString path=draft.absoluteFilePath();
        // Display labels are metadata too: a replaced .title symlink
        // must not read an unrelated file or impersonate another draft.
        const QString titlePath=path+QStringLiteral(".title");
        if(isDraftPath(path) && !QFileInfo(titlePath).isSymLink()) {
            QFile titleFile(titlePath);
            if(titleFile.open(QIODevice::ReadOnly) &&
               titleFile.size()>0 && titleFile.size()<=384) {
                const QString saved=QString::fromUtf8(titleFile.read(385)).trimmed();
                if(!saved.isEmpty())return saved;
            }
        }
        if(isDraftPath(path))rename(path,generated);
        return generated;
    }

    static QString title(const QFileInfo &draft)
    {
        return draft.completeBaseName().mid(37);
    }
};
