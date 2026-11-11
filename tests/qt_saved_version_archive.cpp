/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-only regression checks for completed-save version storage.
 * Synthetic KRA-like ZIP headers exercise file safety; the native
 * Android build separately validates the actual Krita integration.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include "mobile_core/saved_version_archive.h"
#include "mobile_core/mobile_draft_recovery.h"

static void require(bool pass,const char *reason)
{
    if(!pass)qFatal("Saved-version archive regression: %s",reason);
}
static QByteArray load(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),"cannot read test file");
    return file.readAll();
}
static void save(const QString &path,const QByteArray &data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),
            "cannot create test KRA");
    require(file.write(data)==data.size(),"test file incomplete");
    file.close();
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    require(temp.isValid(),"temporary test directory unavailable");
    const QString source=QDir(temp.path()).filePath(
        QStringLiteral("My Drawing.kra"));
    const QString store=QDir(temp.path()).filePath(
        QStringLiteral("private-history"));
    const QString recover=QDir(temp.path()).filePath(
        QStringLiteral("private-recovery"));
    const QByteArray initial=QByteArray::fromHex("504b0304")+"saved-A";
    save(source,initial);
    const QString uri=QStringLiteral("content://provider/document/42");
    const QString uriFolder=MobileSavedVersionArchive::folderFor(uri,store);
    require(!uriFolder.isEmpty() &&
            uriFolder!=MobileSavedVersionArchive::folderFor(source,store) &&
            uriFolder!=MobileSavedVersionArchive::folderFor(uri+QStringLiteral("0"),store),
            "opaque provider URI needs a distinct project history");
    require(!MobileSavedVersionArchive::snapshot(uri,store) &&
            MobileSavedVersionArchive::list(uri,store).isEmpty(),
            "unreadable provider must not produce a version");
    require(MobileSavedVersionArchive::snapshot(source,store),
            "first completed save snapshot failed");
    auto versions=MobileSavedVersionArchive::list(source,store);
    require(versions.size()==1,"missing first snapshot");
    require(load(versions.first().path)==initial,
            "snapshot does not reproduce saved content");
    require(MobileSavedVersionArchive::snapshot(source,store) &&
            MobileSavedVersionArchive::list(source,store).size()==1,
            "repeated unchanged save should not create a duplicate");
    const QString firstSnapshot=versions.first().path;
    QThread::msleep(3);
    const QByteArray second=QByteArray::fromHex("504b0304")+"saved-B";
    save(source,second);
    require(MobileSavedVersionArchive::snapshot(source,store),
            "changed save snapshot failed");
    versions=MobileSavedVersionArchive::list(source,store);
    require(versions.size()==2,"changed save not added to version history");
    require(load(firstSnapshot)==initial,
            "saving an update changed an older snapshot");
    const QString copy=MobileSavedVersionArchive::materializeRecovery(
        versions.last(),recover);
    require(!copy.isEmpty() && QFileInfo(copy).isFile(),
            "recovery copy was not created");
    require(copy!=source && copy!=firstSnapshot &&
            load(copy)==initial && load(source)==second,
            "recovery touched the original or historical copy");

    // Draft autosave snapshots are immutable and keyed by the intended
    // project, not the temporary draft file's location.
    const QString draftProject=QDir(temp.path()).filePath(
        QStringLiteral("Later Saved.kra"));
    const QString privateDraft=QDir(temp.path()).filePath(
        QStringLiteral("Staging.kra"));
    const QByteArray draftA=QByteArray::fromHex("504b0304")+"draft-first";
    const QByteArray draftB=QByteArray::fromHex("504b0304")+"draft-second";
    save(privateDraft,draftA);
    require(MobileSavedVersionArchive::snapshotFrom(privateDraft,privateDraft,store),
            "first unsaved draft checkpoint failed");
    QThread::msleep(3);
    save(privateDraft,draftB);
    require(MobileSavedVersionArchive::snapshotFrom(privateDraft,privateDraft,store),
            "second unsaved draft checkpoint failed");
    require(MobileSavedVersionArchive::list(privateDraft,store).size()==2,
            "completed unsaved drafts should create two historical versions");
    MobileSavedVersionArchive::adoptHistory(privateDraft,draftProject,store);
    require(MobileSavedVersionArchive::list(privateDraft,store).isEmpty() &&
            MobileSavedVersionArchive::list(draftProject,store).size()==2,
            "save-as should transfer private draft history to the project");
    save(draftProject,draftB);
    require(MobileSavedVersionArchive::snapshot(draftProject,store) &&
            MobileSavedVersionArchive::list(draftProject,store).size()==2,
            "unchanged first save should not duplicate the last draft version");
    QThread::msleep(3);
    save(draftProject,QByteArray::fromHex("504b0304")+"saved-third");
    require(MobileSavedVersionArchive::snapshot(draftProject,store) &&
            MobileSavedVersionArchive::list(draftProject,store).size()==3,
            "subsequent saved changes must join the draft version history");
    save(privateDraft,QByteArray::fromHex("504b0304")+"other-draft");
    require(MobileSavedVersionArchive::snapshotFrom(privateDraft,draftProject,store),
            "named document autosave checkpoint failed");
    require(MobileSavedVersionArchive::list(draftProject,store).size()==4 &&
            MobileSavedVersionArchive::list(privateDraft,store).isEmpty(),
            "named autosave should join the project timeline, not fork it");
    // Routine drafts keep updating independently while full historical
    // copies are spaced apart. Background checkpoints may bypass the gap.
    const int historicalCount=MobileSavedVersionArchive::list(draftProject,store).size();
    save(privateDraft,QByteArray::fromHex("504b0304")+"too-soon");
    require(MobileSavedVersionArchive::snapshotFrom(
                privateDraft,draftProject,store,2LL*60*1000) &&
            MobileSavedVersionArchive::list(draftProject,store).size()==historicalCount,
            "routine historical snapshot ignored the time spacing");
    require(MobileSavedVersionArchive::snapshotFrom(privateDraft,draftProject,store) &&
            MobileSavedVersionArchive::list(draftProject,store).size()==historicalCount+1,
            "forced background checkpoint should bypass spacing");


    // Bound the number and total size of backups, keeping the latest
    // completed save even if it alone exceeds the usual byte budget.
    for(int i=0;i<20;++i) {
        QThread::msleep(2);
        save(source,QByteArray::fromHex("504b0304")+
            QByteArray::number(i)+QByteArray(1024,char('a'+i%26)));
        require(MobileSavedVersionArchive::snapshot(source,store),
                "snapshot during retention loop failed");
    }
    versions=MobileSavedVersionArchive::list(source,store);
    require(versions.size()<=MobileSavedVersionArchive::maxVersions(),
            "old snapshots were not pruned");
    require(!versions.isEmpty() &&
            load(versions.first().path)==load(source),
            "newest saved snapshot was pruned");
    const QString unsupported=QDir(temp.path()).filePath(
        QStringLiteral("not-a-KRA.kra"));
    save(unsupported,"not-a-valid-zip");
    require(!MobileSavedVersionArchive::snapshot(unsupported,store),
            "non-KRA bytes were archived");
    qputenv("XDG_DATA_HOME",temp.path().toUtf8());
    const QString restored=MobileSavedVersionArchive::materializeRecovery(versions.first());
    require(MobileSavedVersionArchive::isRecoveryCopy(restored) &&
            !MobileSavedVersionArchive::isRecoveryCopy(source),
            "only private restored copies should require Save As");
    const QString draft=MobileDraftRecovery::allocate(QStringLiteral("a/b"));
    require(!draft.isEmpty() && draft!=MobileDraftRecovery::allocate(QStringLiteral("a/b")),
            "draft paths must be private and unique");
    save(draft,QByteArray::fromHex("504b0304")+"draft");
    save(draft+QStringLiteral(".pending.kra"),"incomplete");
    const auto drafts=MobileDraftRecovery::list();
    require(drafts.size()==1 && drafts.first().absoluteFilePath()==draft &&
            MobileDraftRecovery::title(drafts.first())==QStringLiteral("a_b") &&
            !MobileDraftRecovery::isDraft(source),
            "draft listing exposed staging or external files");
    // Android SAF folder grant (tree URI) vs direct Browse Files (document URI)
    const QString docUri=QStringLiteral(
        "content://com.android.externalstorage.documents/document/primary%3ADrawings%2Fart.kra");
    const QString treeUri1=QStringLiteral(
        "content://com.android.externalstorage.documents/tree/primary%3ADrawings/document/primary%3ADrawings%2Fart.kra");
    const QString treeUri2=QStringLiteral(
        "content://com.android.externalstorage.documents/tree/primary%3A/document/primary%3ADrawings%2Fart.kra");
    require(MobileSavedVersionArchive::canonicalIdentity(treeUri1)==docUri,
            "treeUri1 must normalize to canonical document URI");
    require(MobileSavedVersionArchive::canonicalIdentity(treeUri2)==docUri,
            "treeUri2 must normalize to canonical document URI");
    require(MobileSavedVersionArchive::canonicalIdentity(docUri)==docUri,
            "direct docUri must remain canonical identity");
    const QString docFolder=MobileSavedVersionArchive::folderFor(docUri,store);
    const QString treeFolder1=MobileSavedVersionArchive::folderFor(treeUri1,store);
    const QString treeFolder2=MobileSavedVersionArchive::folderFor(treeUri2,store);
    require(!docFolder.isEmpty(), "document version folder must not be empty");
    require(docFolder==treeFolder1 && docFolder==treeFolder2,
            "SAF tree grants and direct document access must share the same version history");
    const QString otherDocUri=QStringLiteral(
        "content://com.android.externalstorage.documents/document/primary%3ADrawings%2Fother.kra");
    require(MobileSavedVersionArchive::folderFor(otherDocUri,store)!=docFolder,
            "distinct documents in the same provider must have separate version histories");

    // Existing version folder preservation: migrate legacy folder created with raw tree URI hash
    const QString legacyStore=QDir(temp.path()).filePath(QStringLiteral("legacy-store"));
    const QString legacyDigest=QString::fromLatin1(QCryptographicHash::hash(
        treeUri1.toUtf8(),QCryptographicHash::Sha256).toHex());
    const QString legacyFolder=QDir(legacyStore).filePath(legacyDigest);
    require(QDir().mkpath(legacyFolder),"failed to create test legacy folder");
    const QString legacyKra=QDir(legacyFolder).filePath(
        QStringLiteral("20261110T000000000-00000000000000000000000000000001.kra"));
    save(legacyKra,initial);
    save(legacyKra+QStringLiteral(".sha256"),
         QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
    const QString canonicalMigratedFolder=MobileSavedVersionArchive::folderFor(treeUri1,legacyStore);
    require(QDir(canonicalMigratedFolder).exists(),
            "migrated canonical folder must exist");
    require(!QDir(legacyFolder).exists(),
            "legacy folder should be cleaned up after migration");
    const auto migratedDocVersions=MobileSavedVersionArchive::list(docUri,legacyStore);
    require(migratedDocVersions.size()==1 && load(migratedDocVersions.first().path)==initial,
            "migrated version must be accessible via direct document URI");
    const auto migratedTreeVersions=MobileSavedVersionArchive::list(treeUri1,legacyStore);
    require(migratedTreeVersions.size()==1 &&
            migratedTreeVersions.first().path==migratedDocVersions.first().path,
            "migrated version must match across tree and document listing");

    qInfo() << "PASS: snapshot, dedup, atomic recovery, retention,"
               " provider identity, SAF tree normalization and legacy migration";
    return 0;
}
