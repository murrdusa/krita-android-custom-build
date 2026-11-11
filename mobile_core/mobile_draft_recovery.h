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
#include "saved_version_archive.h"
#include <QStandardPaths>
#include <QUuid>

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

    static bool isDraft(const QString &path)
    {
        const QString folder=directory();
        const QFileInfo file(path);
        const QString name=file.completeBaseName();
        return !folder.isEmpty() && !file.isSymLink() && file.isFile() &&
            file.size()>0 && file.absolutePath()==folder &&
            !path.endsWith(QStringLiteral(".pending.kra")) &&
            file.suffix()==QStringLiteral("kra") && name.size()>37 &&
            name.at(36)==QLatin1Char('_') && !QUuid(name.left(36)).isNull();
    }

    // A cheap startup guard, not full ZIP validation. Manual recovery
    // retains damaged drafts so users can still inspect them.
    static bool hasKraHeader(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) &&
            file.peek(4)==QByteArray::fromHex("504b0304");
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

    static void setProject(const QString &draft,const QString &project)
    {
        if(draft.isEmpty())return;
        const QString metadata=draft+QStringLiteral(".project");
        if(MobileSavedVersionArchive::canonicalIdentity(project).isEmpty()) {
            QFile::remove(metadata);
            return;
        }
        if(projectFor(draft)==project)return;
        QSaveFile file(metadata);
        const QByteArray data=project.toUtf8();
        if(file.open(QIODevice::WriteOnly) && file.write(data)==data.size())
            file.commit();
    }

    static QString projectFor(const QString &draft)
    {
        QFile file(draft+QStringLiteral(".project"));
        if(!file.open(QIODevice::ReadOnly) || file.size()>16384)return {};
        return QString::fromUtf8(file.readAll());
    }

    static QFileInfo latestFor(const QString &project,qint64 savedModified)
    {
        const QString identity=MobileSavedVersionArchive::canonicalIdentity(project);
        if(identity.isEmpty())return {};
        for(const auto &draft:list()) {
            if(draft.lastModified().toMSecsSinceEpoch()<=savedModified)break;
            if(MobileSavedVersionArchive::canonicalIdentity(
                   projectFor(draft.absoluteFilePath()))==identity)
                return draft;
        }
        return {};
    }

    static QString title(const QFileInfo &draft)
    {
        return draft.completeBaseName().mid(37);
    }
};
