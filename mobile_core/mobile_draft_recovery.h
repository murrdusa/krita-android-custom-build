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
        QFileInfoList drafts;
        for(const auto &file:QDir(folder).entryInfoList({QStringLiteral("*.kra")},
                    QDir::Files|QDir::Readable|QDir::NoSymLinks,QDir::Time)) {
            if(isDraft(file.absoluteFilePath()))drafts.append(file);
        }
        return drafts;
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
        if(!exportSucceeded || !hasReadableProject(staging)) {
            QFile::remove(staging);
            return false;
        }
        // A completed staging KRA may contain the only copy of the
        // newest edits. If atomic replacement fails (permissions,
        // transient storage error, busy destination), preserve it for
        // recovery or a subsequent publication attempt instead of
        // deleting the successful export.
        return std::rename(QFile::encodeName(staging).constData(),
                           QFile::encodeName(draft).constData())==0;
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
            if(titleFile.open(QIODevice::ReadOnly) && titleFile.size()<=384) {
                const QString saved=QString::fromUtf8(titleFile.readAll()).trimmed();
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
