/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android's Storage Access Framework returns content:// document
 * identities. A QFile::exists() check alone is not a safe reason to
 * reject an import or force Save As: the original provider URI may
 * remain writable through Krita's platform file bridge.
 *
 * This policy only decides when to invoke Krita's own native file
 * engine; it does not claim provider permissions or write files.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QFile>
#include <QString>
#include <QUrl>

class MobileNativeDocumentPathPolicy final
{
public:
    static bool isProviderUri(const QString &path)
    {
        return path.startsWith(QStringLiteral("content://"),
                               Qt::CaseInsensitive);
    }

    static QString localFilePath(const QString &path)
    {
        if(!path.startsWith(QStringLiteral("file:"),Qt::CaseInsensitive))
            return path;
        const QUrl url(path);
        return url.isValid() && url.isLocalFile()?url.toLocalFile():QString();
    }

    static QUrl recentFileUrl(const QString &path)
    {
        if(path.isEmpty())return {};
        if(isProviderUri(path))return QUrl(path);
        const QString local=localFilePath(path);
        return local.isEmpty()?QUrl():QUrl::fromLocalFile(local);
    }

    static bool mayAttemptOpen(const QString &path)
    {
        return !path.trimmed().isEmpty() &&
               (isProviderUri(path) || QFile::exists(localFilePath(path)));
    }

    static QString saveDialogStartPath(const QString &currentPath,
                                       const QString &picturesFolder)
    {
        // A provider document is a file identity, not a filesystem
        // directory. Keep the picker away from content:// identifiers.
        return currentPath.isEmpty() || isProviderUri(currentPath)?
            picturesFolder:localFilePath(currentPath);
    }

    static bool needsSaveAs(const QString &path,bool saveAs,
                            bool recovered,bool unsupportedMime)
    {
        if(saveAs || path.isEmpty() || recovered || unsupportedMime)
            return true;
        // Provider identity and permissions are Krita's responsibility.
        // Do not force another picker merely because QFile::exists()
        // does not understand a previously granted content URI.
        return !isProviderUri(path) &&
               !QFile::exists(localFilePath(path));
    }
};
#endif
