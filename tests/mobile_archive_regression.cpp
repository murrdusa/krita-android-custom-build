/* SPDX-License-Identifier: GPL-2.0-or-later */
// Standalone desktop Qt regression checks for Android-independent archive paths.
#include "../mobile_core/saved_version_archive.h"
#include "../mobile_core/open_document_destination_policy.h"
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
    // New archives must have a committed SHA-256 sidecar. Earlier
    // versions may lack it, but silent checksum publication failures
    // must not be accepted for newly written recovery history.
    QFile firstDigest(versions.first().path+QStringLiteral(".sha256"));
    if(!require(firstDigest.open(QIODevice::ReadOnly) &&
                firstDigest.readAll().trimmed()==
                    QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex(),
                "published archive checksum matches contents"))return 1;
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

    // Saving an untitled canvas must preserve its earlier unsaved draft
    // history under the final project identity, including multiple versions.
    const QString draft=temp.filePath(QStringLiteral("Untitled.kra"));
    const QString saved=temp.filePath(QStringLiteral("Named.kra"));
    if(!require(writeBytes(draft,initial) &&
                Archive::snapshot(draft,root),"untitled history initial"))return 1;
    if(!require(writeBytes(draft,changed) &&
                Archive::snapshot(draft,root),"untitled history subsequent"))return 1;
    const auto originalHistory=Archive::list(draft,root);
    if(!require(originalHistory.size()==2,"untitled has two versions"))return 1;
    Archive::adoptHistory(draft,saved,root);
    if(!require(Archive::list(draft,root).isEmpty() &&
                Archive::list(saved,root).size()==2,
                "unsaved draft history moves to named project"))return 1;
    if(!require(writeBytes(saved,QByteArray::fromHex("504b0304")+
                QByteArray("named revision")) &&
                Archive::snapshot(saved,root) &&
                Archive::list(saved,root).size()==3,
                "named project continues existing version history"))return 1;

    // Import a second draft into an already populated project history.
    // This follows the per-file migration branch, where checksums must
    // remain paired with their moved KRA files.
    const QString secondDraft=temp.filePath(QStringLiteral("LaterUntitled.kra"));
    const QByteArray imported=QByteArray::fromHex("504b0304")+
        QByteArray("imported draft");
    if(!require(writeBytes(secondDraft,imported) &&
                Archive::snapshot(secondDraft,root),
                "second draft history initial"))return 1;
    Archive::adoptHistory(secondDraft,saved,root);
    const auto merged=Archive::list(saved,root);
    if(!require(Archive::list(secondDraft,root).isEmpty() &&
                merged.size()==4,
                "merge draft history into existing named project"))return 1;
    bool recoveredImported=false;
    for(const auto &version:merged) {
        const QString path=Archive::materializeRecovery(
            version,temp.filePath(QStringLiteral("recovery")));
        if(!require(!path.isEmpty(),
                    "every merged archive retains a valid checksum"))return 1;
        QFile file(path);
        if(!require(file.open(QIODevice::ReadOnly),
                    "open merged recovery copy"))return 1;
        if(file.readAll()==imported)recoveredImported=true;
    }
    if(!require(recoveredImported,
                "migrated draft preserves original bytes"))return 1;

    // A migrated history can collide with a filename already present
    // in the target archive. Both original KRA/checksum pairs must survive
    // with independent, verified recovery copies.
    const QString collisionDraft=temp.filePath(QStringLiteral("CollisionDraft.kra"));
    const QByteArray collisionBytes=QByteArray::fromHex("504b0304")+
        QByteArray("incoming collision content");
    if(!require(writeBytes(collisionDraft,collisionBytes) &&
                Archive::snapshot(collisionDraft,root),
                "create colliding draft"))return 1;
    const auto incoming=Archive::list(collisionDraft,root);
    if(!require(incoming.size()==1,"incoming collision snapshot"))return 1;
    const auto namedBeforeCollision=Archive::list(saved,root);
    const QString duplicateName=QDir(Archive::folderFor(saved,root)).filePath(
        QFileInfo(incoming.first().path).fileName());
    if(!require(QFile::copy(namedBeforeCollision.first().path,duplicateName) &&
                QFile::copy(namedBeforeCollision.first().path+
                            QStringLiteral(".sha256"),
                            duplicateName+QStringLiteral(".sha256")),
                "prepare colliding destination archive"))return 1;
    Archive::adoptHistory(collisionDraft,saved,root);
    const auto afterCollision=Archive::list(saved,root);
    if(!require(afterCollision.size()==namedBeforeCollision.size()+2,
                "both collided archive entries retained"))return 1;
    bool recoveredCollision=false;
    for(const auto &version:afterCollision) {
        const QString path=Archive::materializeRecovery(
            version,temp.filePath(QStringLiteral("recovery")));
        if(!require(!path.isEmpty(),"collided history integrity"))return 1;
        QFile file(path);
        if(!require(file.open(QIODevice::ReadOnly),
                    "read collided recovery"))return 1;
        if(file.readAll()==collisionBytes)recoveredCollision=true;
    }
    if(!require(recoveredCollision,"incoming collision archive recoverable"))
        return 1;

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

    // A stale browser result may point at a file replaced with a
    // symlink after the directory scan. Recovery must not follow it,
    // even if the symlink ultimately contains valid KRA bytes.
    const auto archived=Archive::list(saved,root);
    if(!require(!archived.isEmpty(),"archived entry for symlink check"))return 1;
    const QString linkedArchive=temp.filePath(QStringLiteral("replaced.kra"));
    if(QFile(archived.first().path).link(linkedArchive)) {
        auto stale=archived.first();
        stale.path=linkedArchive;
        if(!require(Archive::materializeRecovery(
                stale,temp.filePath(QStringLiteral("recovery"))).isEmpty(),
                "stale symlink archive rejected"))return 1;
    }
    // Similarly a checksum sidecar swapped for a symlink is not
    // authoritative, even when it resolves to a valid digest.
    const QString checksum=archived.first().path+QStringLiteral(".sha256");
    QFile digestSource(checksum);
    if(!require(digestSource.open(QIODevice::ReadOnly),
                "read checksum for sidecar replacement"))return 1;
    const QByteArray originalDigest=digestSource.readAll();
    digestSource.close();
    const QString safeDigest=temp.filePath(QStringLiteral("safe.sha256"));
    if(!require(writeBytes(safeDigest,originalDigest) &&
                QFile::remove(checksum),"prepare checksum link"))return 1;
    if(QFile(safeDigest).link(checksum)) {
        if(!require(Archive::materializeRecovery(
                archived.first(),temp.filePath(QStringLiteral("recovery"))).isEmpty(),
                "symlink checksum rejected"))return 1;
        if(!require(QFile::remove(checksum) &&
                    writeBytes(checksum,originalDigest),
                    "restore original checksum"))return 1;
    } else {
        if(!require(writeBytes(checksum,originalDigest),
                    "restore original checksum after failed link"))return 1;
    }

    // The Save As/Export guard must recognize aliases, relative paths,
    // and equivalent content provider URIs for another open project.
    using Destination=MobileOpenDocumentDestinationPolicy;
    if(!require(Destination::sameFile(
                    source,temp.filePath(QStringLiteral("./art.kra"))) &&
                Destination::sameFile(tree,direct) &&
                !Destination::sameFile(source,saved) &&
                !Destination::sameFile(QString(),source),
                "multi-document Save As destination identity"))return 1;
    const QString alias=temp.filePath(QStringLiteral("alias.kra"));
    if(QFile(source).link(alias))
        if(!require(Destination::sameFile(alias,source),
                    "symlink alias destination collision"))return 1;
    const QString output=temp.filePath(QStringLiteral("exports/result.png"));
    if(!require(Destination::sameFile(
                    output,temp.filePath(QStringLiteral("exports/./result.png"))),
                "nonexistent export destination normalizes"))return 1;

    qInfo() << "PASS: archive deduplication, recovery integrity, checksum repair,"
               " corruption handling, periodic spacing, forced checkpoints,"
               " retention, merged and collided histories, symlinks and Android identities";
    return 0;
}
