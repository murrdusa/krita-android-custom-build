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
#include "saved_version_archive.h"

class MobileOpenDocumentDestinationPolicy final
{
public:
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
        // A not-yet-existing Save As target must still normalize correctly.
        const QFileInfo a(first),b(second);
        const QString aResolved=a.canonicalFilePath();
        const QString bResolved=b.canonicalFilePath();
        return QDir::cleanPath(aResolved.isEmpty()?a.absoluteFilePath():aResolved)==
               QDir::cleanPath(bResolved.isEmpty()?b.absoluteFilePath():bResolved);
    }
};
#endif
