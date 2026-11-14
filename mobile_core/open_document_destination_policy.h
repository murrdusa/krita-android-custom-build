/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Avoid replacing an open canvas's backing file from another document's
 * Save As / Export. QFileInfo alone does not normalize Android SAF URIs.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QUrl>
#include "saved_version_archive.h"

class MobileOpenDocumentDestinationPolicy final
{
public:
    // A recovered/untitled document may have no visible path but still
    // use a private draft as its backing file. Protect both identities.
    static bool conflictsWithBackingFile(const QString &destination,
                                         const QString &projectPath,
                                         const QString &draftPath)
    {
        return sameFile(destination,projectPath) ||
               sameFile(destination,draftPath);
    }

    static bool conflictsWithOwnBackingFile(
        const QString &destination,const QString &projectPath,
        const QString &draftPath,bool choosingPath,bool exporting)
    {
        return (choosingPath && sameFile(destination,draftPath)) ||
               (exporting && sameFile(destination,projectPath));
    }

    static bool sameFile(const QString &first,const QString &second)
    {
        if(first.isEmpty() || second.isEmpty())return false;
        if(first==second)return true;
        const bool firstProvider=first.startsWith(
            QStringLiteral("content://"),Qt::CaseInsensitive);
        const bool secondProvider=second.startsWith(
            QStringLiteral("content://"),Qt::CaseInsensitive);
        if(firstProvider!=secondProvider)return false;
        if(firstProvider)
            return MobileSavedVersionArchive::canonicalIdentity(first)==
                   MobileSavedVersionArchive::canonicalIdentity(second);
        // Existing files may use symlinks, relative paths or dot segments.
        // File picker results may also use file:// for the exact same
        // local path. Never let that spelling bypass a Save As collision.
        const QString firstPath=resolvedLocalPath(first);
        const QString secondPath=resolvedLocalPath(second);
        return !firstPath.isEmpty() && firstPath==secondPath;
    }

private:
    static QString resolvedLocalPath(const QString &path)
    {
        QString local=path;
        if(path.startsWith(QStringLiteral("file:"),Qt::CaseInsensitive)) {
            const QUrl uri(path);
            if(!uri.isValid() || !uri.isLocalFile() ||
               (!uri.host().isEmpty() &&
                uri.host().compare(QStringLiteral("localhost"),
                                   Qt::CaseInsensitive)!=0))return {};
            local=uri.toLocalFile();
            if(local.isEmpty())return {};
        }
        const QFileInfo file(local);
        const QString canonical=file.canonicalFilePath();
        if(!canonical.isEmpty())return QDir::cleanPath(canonical);

        // A new Save As destination may not exist yet. Canonicalize
        // the closest existing ancestor so aliases through a symlinked
        // folder cannot bypass an open-document collision check.
        const QString absolute=QDir::cleanPath(file.absoluteFilePath());
        QFileInfo parent(absolute);
        QStringList tail;
        while(!parent.exists()) {
            tail.prepend(parent.fileName());
            const QString next=parent.absolutePath();
            if(next==parent.absoluteFilePath())break;
            parent.setFile(next);
        }
        QString base=parent.canonicalFilePath();
        if(base.isEmpty())base=parent.absoluteFilePath();
        for(const QString &segment:tail)base=QDir(base).filePath(segment);
        return QDir::cleanPath(base);
    }
};
#endif
