/* SPDX-License-Identifier: GPL-2.0-or-later */
// Standalone desktop Qt regression checks for Android-independent archive paths.
#include "../mobile_core/saved_version_archive.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
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
    // Periodic autosaves must be rate-limited without overwriting an
    // earlier completed historical version.
    const QString spaced=temp.filePath(QStringLiteral("spaced.kra"));
    if(!require(writeBytes(spaced,initial),"create periodic source"))return 1;
    if(!require(Archive::snapshotFrom(spaced,spaced,root,120000),
                "initial periodic checkpoint"))return 1;
    if(!require(writeBytes(spaced,changed),"change periodic source"))return 1;
    if(!require(Archive::snapshotFrom(spaced,spaced,root,120000),
                "periodic checkpoint rate limit"))return 1;
    if(!require(Archive::list(spaced,root).size()==1,
                "periodic checkpoint did not duplicate within minimum gap"))return 1;
    if(!require(Archive::snapshotFrom(spaced,spaced,root,0),
                "foreground-loss checkpoint bypasses spacing"))return 1;
    if(!require(Archive::list(spaced,root).size()==2,
                "forced checkpoint retains new version"))return 1;

    // A removed (explicitly discarded) draft must not appear to have
    // checkpointed successfully merely because a recent version exists.
    if(!require(QFile::remove(spaced),"remove discarded draft"))return 1;
    if(!require(!Archive::snapshotFrom(spaced,spaced,root,120000),
                "discarded source rejected even within minimum gap"))return 1;
    if(!require(writeBytes(spaced,QByteArray("not a KRA archive")),
                "create invalid draft"))return 1;
    if(!require(!Archive::snapshotFrom(spaced,spaced,root,120000),
                "invalid source rejected even within minimum gap"))return 1;
    if(!require(writeBytes(spaced,changed),"restore valid source"))return 1;
    if(!require(Archive::snapshotFrom(spaced,spaced,root,120000),
                "valid source still respects spacing"))return 1;

    // Test retention with separate content for each generation. The oldest
    // archive copies must be pruned without removing the newest version.
    const QString bounded=temp.filePath(QStringLiteral("bounded.kra"));
    for(int i=0;i<Archive::maxVersions()+3;++i) {
        const QByteArray data=QByteArray::fromHex("504b0304")+
            QByteArray("generation-")+QByteArray::number(i);
        if(!require(writeBytes(bounded,data) &&
                    Archive::snapshot(bounded,root),
                    "archive generation"))return 1;
    }
    const auto retained=Archive::list(bounded,root);
    if(!require(retained.size()==Archive::maxVersions(),
                "version retention limit"))return 1;
    const QString lastCopy=Archive::materializeRecovery(
        retained.first(),temp.filePath(QStringLiteral("recovery")));
    QFile lastFile(lastCopy);
    if(!require(!lastCopy.isEmpty() && lastFile.open(QIODevice::ReadOnly) &&
                lastFile.readAll().endsWith(QByteArray("generation-")+
                    QByteArray::number(Archive::maxVersions()+2)),
                "retention preserves newest generation"))return 1;

    // The archive directory can contain unrelated symlinks. Never expose
    // them as recoverable snapshots to the project browser.
    const QString folder=Archive::folderFor(spaced,root);
    const QString link=QDir(folder).filePath(QStringLiteral("99999999T999999999-link.kra"));
    if(QFile(source).link(link))
        if(!require(Archive::list(spaced,root).size()==2,
                    "symlink not included in archive browser"))return 1;
    // Android's tree and direct document URIs identify the same file.
    const QString tree=QStringLiteral(
        "content://com.example.documents/tree/folder%3A1/document/folder%3Aart.kra");
    const QString direct=QStringLiteral(
        "content://com.example.documents/document/folder%3Aart.kra");
    if(!require(Archive::canonicalIdentity(tree)==
                Archive::canonicalIdentity(direct) &&
                Archive::canonicalIdentity(direct+QStringLiteral("?grant=1#part"))==
                Archive::canonicalIdentity(direct),
                "equivalent Android document identities"))return 1;

    // Do not snapshot another file through a symlink in a draft path.
    const QString linkedSource=temp.filePath(QStringLiteral("linked.kra"));
    if(QFile(spaced).link(linkedSource)) {
        if(!require(!Archive::snapshot(linkedSource,root),
                    "symlink source excluded"))return 1;
    }

    qInfo() << "PASS: archive deduplication, recovery integrity, checksum repair,"
               " corruption handling, periodic spacing, forced checkpoints,"
               " retention, discarded drafts, symlink exclusion and Android identities";
    return 0;
}
