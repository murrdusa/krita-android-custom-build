#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include "mobile_core/native_document_path_policy.h"

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
    qInfo()<<"PASS: provider URI open/save, local path existence,"
            <<"explicit Save As, recovery and MIME guards";
    return 0;
}
