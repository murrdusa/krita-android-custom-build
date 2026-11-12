/* SPDX-License-Identifier: GPL-2.0-or-later */
// Standalone desktop Qt regression checks for Android-independent archive paths.
#include "../mobile_core/saved_version_archive.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>

static bool writeBytes(const QString &path,const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data)==data.size();
}
static bool require(bool ok,const char *message)
{
    if(!ok)qCritical() << "FAIL:" << message;
    return ok;
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    if(!require(temp.isValid(),"temporary directory"))return 1;
    const QString root=temp.filePath(QStringLiteral("history"));
    const QString source=temp.filePath(QStringLiteral("art.kra"));
    using Archive=MobileSavedVersionArchive;
    const QByteArray initial=QByteArray::fromHex("504b0304")+QByteArray("first version");
    if(!require(writeBytes(source,initial),"create source"))return 1;
    if(!require(Archive::snapshot(source,root),"initial archive"))return 1;
    auto versions=Archive::list(source,root);
    if(!require(versions.size()==1,"one version created"))return 1;
    if(!require(Archive::snapshot(source,root),"deduplicated archive"))return 1;
    if(!require(Archive::list(source,root).size()==1,"duplicate not retained"))return 1;

    const QString recovered=Archive::materializeRecovery(
        versions.first(),temp.filePath(QStringLiteral("recovery")));
    QFile recoveredFile(recovered);
    if(!require(!recovered.isEmpty() &&
                recoveredFile.open(QIODevice::ReadOnly) &&
                recoveredFile.readAll()==initial,
                "recover byte-for-byte"))return 1;

    const QByteArray changed=QByteArray::fromHex("504b0304")+QByteArray("second version");
    if(!require(writeBytes(source,changed),"change source"))return 1;
    if(!require(Archive::snapshot(source,root),"second archive"))return 1;
    versions=Archive::list(source,root);
    if(!require(versions.size()==2,"distinct versions retained"))return 1;

    const auto newest=versions.first();
    if(!require(writeBytes(newest.path+QStringLiteral(".sha256"),
                           QByteArray(64,'0')),"corrupt digest sidecar"))return 1;
    if(!require(Archive::materializeRecovery(
                    newest,temp.filePath(QStringLiteral("recovery"))).isEmpty(),
                "corrupt sidecar rejected"))return 1;
    if(!require(Archive::snapshot(source,root),"repair digest"))return 1;
    if(!require(Archive::list(source,root).size()==2,
                "repair did not fork history"))return 1;
    if(!require(!Archive::materializeRecovery(
                    newest,temp.filePath(QStringLiteral("recovery"))).isEmpty(),
                "recovery succeeds after digest repair"))return 1;

    if(!require(writeBytes(newest.path,QByteArray::fromHex("504b0304")+
                           QByteArray("tampered content")),"tamper archived KRA"))return 1;
    if(!require(Archive::materializeRecovery(
                    newest,temp.filePath(QStringLiteral("recovery"))).isEmpty(),
                "modified archive rejected"))return 1;
    if(!require(Archive::snapshot(source,root),"archive after corruption"))return 1;
    if(!require(Archive::list(source,root).size()==3,
                "corrupt archive preserved for diagnosis"))return 1;
    if(!require(Archive::folderFor(source,root)==
                Archive::folderFor(temp.filePath(QStringLiteral("./art.kra")),root),
                "canonical file identity"))return 1;
    qInfo() << "PASS: archive deduplication, recovery integrity, checksum repair,"
               " corruption handling, and canonical file identity";
    return 0;
}
