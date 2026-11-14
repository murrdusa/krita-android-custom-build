#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include "mobile_core/native_document_path_policy.h"
#include "mobile_core/open_document_destination_policy.h"

static void require(bool ok,const char *reason)
{
    if(!ok)qFatal("Native path policy regression: %s",reason);
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir tmp;
    require(tmp.isValid(),"temporary directory");
    const QString realFile=tmp.filePath(QStringLiteral("drawing.kra"));
    {
        QFile f(realFile);
        require(f.open(QFile::WriteOnly),"create local drawing fixture");
        require(f.write("drawing")==7,"write local drawing fixture");
    }
    const QString provider=QStringLiteral(
        "content://com.android.documents/document/primary%3Aart%2Fdrawing.kra");
    const QString missing=tmp.filePath(QStringLiteral("missing.kra"));
    using P=MobileNativeDocumentPathPolicy;
    require(P::mayAttemptOpen(realFile),"existing local document rejected");
    require(P::mayAttemptOpen(provider),"Android provider document pre-rejected");
    require(!P::mayAttemptOpen(missing),"missing local document accepted");
    const QString folder=tmp.filePath(QStringLiteral("project-folder.kra"));
    require(QDir().mkpath(folder),"create directory masquerading as KRA");
    const QString folderUrl=QUrl::fromLocalFile(folder).toString();
    require(!P::mayAttemptOpen(folder) &&
            !P::mayAttemptOpen(folderUrl) &&
            P::needsSaveAs(folder,false,false,false) &&
            P::needsSaveAs(folderUrl,false,false,false) &&
            P::recentFileUrl(folder).isEmpty() &&
            P::recentFileUrl(folderUrl).isEmpty(),
            "filesystem directory was accepted as a native drawing");

    require(!P::mayAttemptOpen(QStringLiteral("  ")),"blank path accepted");
    require(P::isProviderUri(provider),"provider identity undetected");
    require(!P::isProviderUri(realFile),"normal file treated as provider");
    require(!P::needsSaveAs(provider,false,false,false),
            "provider document unexpectedly forced into Save As");
    require(!P::needsSaveAs(realFile,false,false,false),
            "existing local file unexpectedly forced into Save As");
    require(P::needsSaveAs(missing,false,false,false),
            "missing local destination treated as writable");
    require(P::needsSaveAs(provider,true,false,false),
            "explicit Save As on provider URI ignored");
    require(P::needsSaveAs(provider,false,true,false),
            "recovered provider document bypassed Save As");
    require(P::needsSaveAs(provider,false,false,true),
            "unsupported provider MIME bypassed Save As");
    require(P::needsSaveAs(QString(),false,false,false),
            "unsaved document had no Save As picker");
    const QString pictures=tmp.filePath(QStringLiteral("Pictures"));
    require(P::saveDialogStartPath(provider,pictures)==pictures,
            "provider file was passed as a Save As directory");
    require(P::saveDialogStartPath(QString(),pictures)==pictures,
            "untitled document lacked a Save As directory");
    require(P::saveDialogStartPath(realFile,pictures)==realFile,
            "local Save As failed to retain its suggested file path");
    // The file picker may return a local file URL while an open canvas
    // is registered under its filesystem path. Both are the same backing
    // file, including percent-escaped names and a not-yet-existing
    // Save As destination.
    using Guard=MobileOpenDocumentDestinationPolicy;
    const QString fileUrl=QUrl::fromLocalFile(realFile).toString();
    require(Guard::sameFile(realFile,fileUrl) &&
            Guard::sameFile(fileUrl,realFile) &&
            Guard::conflictsWithBackingFile(fileUrl,realFile,QString()),
            "file URL alias bypassed open document Save As protection");
    const QString spaced=tmp.filePath(QStringLiteral("new drawing.kra"));
    const QString spacedUrl=QUrl::fromLocalFile(spaced).toString();
    require(P::localFilePath(fileUrl)==realFile &&
            P::recentFileUrl(fileUrl)==QUrl::fromLocalFile(realFile) &&
            P::recentFileUrl(realFile)==QUrl::fromLocalFile(realFile) &&
            P::recentFileUrl(provider)==QUrl(provider),
            "native open/recent paths differ for the same drawing");
    const QString remoteUrl=QStringLiteral("file://remote-host/drawing.kra");
    require(P::recentFileUrl(remoteUrl).isEmpty() &&
            P::localFilePath(remoteUrl).isEmpty() &&
            !P::mayAttemptOpen(remoteUrl) &&
            P::saveDialogStartPath(remoteUrl,pictures)==pictures &&
            P::needsSaveAs(remoteUrl,false,false,false) &&
            MobileSavedVersionArchive::canonicalIdentity(remoteUrl).isEmpty() &&
            !Guard::sameFile(remoteUrl,realFile),
            "nonlocal file URL was treated as an Android local project");
    require(P::mayAttemptOpen(fileUrl) &&
            !P::needsSaveAs(fileUrl,false,false,false) &&
            P::saveDialogStartPath(fileUrl,pictures)==realFile,
            "existing file URL was treated as a missing local document");
    require(!P::mayAttemptOpen(spacedUrl) &&
            P::needsSaveAs(spacedUrl,false,false,false),
            "nonexistent local file URL was treated as openable");

    require(Guard::sameFile(spacedUrl,spaced) &&
            Guard::conflictsWithOwnBackingFile(
                spacedUrl,QString(),spaced,true,false),
            "escaped URL of new draft destination bypassed collision guard");
    require(!Guard::sameFile(fileUrl,missing) &&
            !Guard::sameFile(fileUrl,provider),
            "file URL alias collided with unrelated backing document");
    qInfo()<<"PASS: provider URI open/save, local path existence,"
            <<"explicit Save As, recovery and MIME guards";
    return 0;
}
