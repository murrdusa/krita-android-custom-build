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
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#endif

class MobileNativeDocumentPathPolicy final
{
public:
    static bool needsKritaCopyOnHome(const QByteArray &mime,const QByteArray &nativeMime)
    {
        return !mime.isEmpty() && mime!=nativeMime && mime!="application/x-krita-archive";
    }

    static QString kritaCopyFileName(const QString &originalName)
    {
        const QString base=QFileInfo(originalName).completeBaseName();
        return (base.isEmpty()?QStringLiteral("Untitled"):base)+QStringLiteral(".kra");
    }

    static bool isPhotoshopDocumentName(const QString &name)
    {
        return !photoshopMimeForName(name).isEmpty();
    }

    static QString photoshopMimeForName(const QString &name)
    {
        static const QRegularExpression extension(
            QStringLiteral(R"(\.(psd|psb)(?: \(\d+\))?$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match=extension.match(name);
        if(!match.hasMatch())return {};
        return match.captured(1).compare(QLatin1String("psb"),Qt::CaseInsensitive)==0?
            QStringLiteral("image/x-psb"):QStringLiteral("image/vnd.adobe.photoshop");
    }

    static bool isPhotoshopMime(const QString &mime)
    {
        return mime==QLatin1String("image/vnd.adobe.photoshop") ||
            mime==QLatin1String("image/x-photoshop") ||
            mime==QLatin1String("image/photoshop") ||
            mime==QLatin1String("image/x-psd") || mime==QLatin1String("image/x-psb");
    }

    static bool canModifyOriginal(const QString &path,bool deleting)
    {
#ifdef Q_OS_ANDROID
        if(isProviderUri(path)) {
            const auto uri=QAndroidJniObject::fromString(path);
            return QAndroidJniObject::callStaticMethod<jboolean>(
                "org/krita/android/MainActivity","tabletCanModifyProject",
                "(Ljava/lang/String;Z)Z",uri.object<jstring>(),jboolean(deleting));
        }
#endif
        const QFileInfo file(localFilePath(path));
        return file.isFile() && QFileInfo(file.absolutePath()).isWritable() &&
            (deleting || file.isWritable());
    }

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
        return url.isValid() && url.isLocalFile() &&
               (url.host().isEmpty() ||
                url.host().compare(QStringLiteral("localhost"),Qt::CaseInsensitive)==0)?
            url.toLocalFile():QString();
    }

    static QUrl recentFileUrl(const QString &path)
    {
        if(path.isEmpty())return {};
        if(isProviderUri(path))return QUrl(path);
        const QString local=localFilePath(path);
        return local.isEmpty() || !QFileInfo(local).isFile()?
            QUrl():QUrl::fromLocalFile(local);
    }

    static bool mayAttemptOpen(const QString &path)
    {
        if(path.trimmed().isEmpty())return false;
        if(isProviderUri(path))return true;
        const QString local=localFilePath(path);
        return !local.isEmpty() && QFileInfo(local).isFile();
    }

    static QString saveDialogStartPath(const QString &currentPath,
                                       const QString &picturesFolder)
    {
        // A provider document is a file identity, not a filesystem
        // directory. Keep the picker away from content:// identifiers.
        if(currentPath.isEmpty() || isProviderUri(currentPath))
            return picturesFolder;
        const QString local=localFilePath(currentPath);
        return local.isEmpty()?picturesFolder:local;
    }

    static bool needsSaveAs(const QString &path,bool saveAs,
                            bool recovered,bool unsupportedMime)
    {
        if(saveAs || path.isEmpty() || recovered || unsupportedMime)
            return true;
        // Provider identity and permissions are Krita's responsibility.
        // Do not force another picker merely because QFile::exists()
        // does not understand a previously granted content URI.
        if(isProviderUri(path))return false;
        const QString local=localFilePath(path);
        return local.isEmpty() || !QFileInfo(local).isFile();
    }
};
#endif
