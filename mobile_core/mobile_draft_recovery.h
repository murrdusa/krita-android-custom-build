/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Private locations for Krita's native autosave files. Saved project
 * versions are managed separately by MobileSavedVersionArchive.
 */
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <cstdio>
#include "saved_version_archive.h"
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <KoStore.h>
#include <QScopedPointer>
#endif

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
        const QFileInfo entry(path);
        constexpr qint64 maxArchiveBytes=8LL*1024*1024*1024;
        if(!entry.isFile() || entry.isSymLink() ||
           entry.size()<4 || entry.size()>maxArchiveBytes)return false;
        QFile file(path);
        return file.open(QIODevice::ReadOnly) &&
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

    static QFileInfoList list()
    {
        const QString folder=directory();
        if(folder.isEmpty())return {};
        // If the process terminated between KRA export and publication,
        // a complete .pending.kra would otherwise never appear in the
        // recovery browser. Only rescue old, readable staging files:
        // recent files may belong to an export still in progress.
        const QDateTime now=QDateTime::currentDateTimeUtc();
        const QString suffix=QStringLiteral(".pending.kra");
        for(const QFileInfo &pending:QDir(folder).entryInfoList(
                {QStringLiteral("*.pending.kra")},
                QDir::Files|QDir::Readable|QDir::NoSymLinks)) {
            const qint64 age=pending.lastModified().toUTC().secsTo(now);
            if(age<300)continue;
            const QString path=pending.absoluteFilePath();
            const QString draft=path.left(path.size()-suffix.size());
            if(!isDraftPath(draft) || !hasReadableProject(path))continue;
            // Opening the KRA with KoStore may take time. An exporter
            // that resumed writing meanwhile must retain ownership of
            // its staging file; never rescue a file whose size or mtime
            // changed during validation.
            if(!unchangedStaging(pending))continue;
            // False explicitly prevents replacing the previous published
            // draft; publish() moves the orphan to a separate recovery ID.
            publish(draft,false);
        }
        QFileInfoList drafts;
        for(const auto &file:QDir(folder).entryInfoList({QStringLiteral("*.kra")},
                    QDir::Files|QDir::Readable|QDir::NoSymLinks,QDir::Time)) {
            if(isDraft(file.absoluteFilePath()))drafts.append(file);
        }
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
        if(QFileInfo(staging).isSymLink())return false;
        // The exporter can report failure after writing an otherwise
        // complete KRA (for example if its final flush failed). Never
        // replace the last published draft when exportSucceeded is false.
        // A readable staging file may still be the only recoverable copy.
        if(!hasReadableProject(staging)) {
            // A failed export may have written partial user data before
            // returning false. Retain its staging bytes for inspection or
            // future recovery rather than deleting them in the error path.
            // The next export owns replacing its own staging pathname.
            if(exportSucceeded)QFile::remove(staging);
            return false;
        }
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
        if(!file.open(QIODevice::ReadOnly) || file.size()<0 ||
           file.size()>16384)return {};
        return QString::fromUtf8(file.readAll());
    }

    static QFileInfoList checkpointsFor(const QString &project,qint64 savedModified)
    {
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(project);
        if(identity.isEmpty())return {};
        QFileInfoList result;
        for(const auto &draft:list()) {
            if(draft.lastModified().toMSecsSinceEpoch()<=savedModified)break;
            if(MobileSavedVersionArchive::canonicalIdentity(
                   projectFor(draft.absoluteFilePath()))==identity &&
               hasReadableProject(draft.absoluteFilePath()))
                result.append(draft);
        }
        return result;
    }

    // A named project with newer, still-recoverable private edits must
    // not be deleted from the gallery before the draft is explicitly
    // discarded or recovered. Compare normalized local/SAF identities.
    static bool hasProjectDraft(const QString &project)
    {
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(project);
        if(identity.isEmpty())return false;
        for(const QFileInfo &draft:list()) {
            if(MobileSavedVersionArchive::canonicalIdentity(
                   projectFor(draft.absoluteFilePath()))==identity)
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
