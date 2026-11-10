/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Private locations for Krita's native autosave files. Saved project
 * versions are managed separately by MobileSavedVersionArchive.
 */
#pragma once

#include <QDir>
#include <QFileInfo>
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

    static QString title(const QFileInfo &draft)
    {
        return draft.completeBaseName().mid(37);
    }
};
