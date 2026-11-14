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
    // A completed KRA archive survives even when power loss occurs
    // before the optional checksum sidecar is published.
    require(QFile::remove(firstSnapshot+QStringLiteral(".sha256")),
            "cannot simulate a missing checksum sidecar");
    require(MobileSavedVersionArchive::snapshot(source,store) &&
            MobileSavedVersionArchive::list(source,store).size()==1,
            "sidecar loss must not evict older history with duplicates");
    require(load(firstSnapshot+QStringLiteral(".sha256"))==
            QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex(),
            "unchanged archive should recreate a missing checksum sidecar");
    // A syntactically valid but incorrect sidecar is not evidence that
    // the KRA changed. Verify stored bytes and repair metadata rather
    // than duplicating or losing an otherwise intact saved version.
    save(firstSnapshot+QStringLiteral(".sha256"),QByteArray(64,'0'));
    require(MobileSavedVersionArchive::snapshot(source,store) &&
            MobileSavedVersionArchive::list(source,store).size()==1,
            "wrong checksum metadata must not fork an unchanged version");
    require(load(firstSnapshot+QStringLiteral(".sha256"))==
            QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex() &&
            !MobileSavedVersionArchive::materializeRecovery(
                MobileSavedVersionArchive::list(source,store).first(),recover).isEmpty(),
            "intact archive with bad metadata should recover after repair");
    // Deduplication must treat a large replaced digest as missing and
    // repair it from the verified archive rather than reading it all.
    save(firstSnapshot+QStringLiteral(".sha256"),QByteArray(1024*1024,'x'));
    require(MobileSavedVersionArchive::snapshot(source,store) &&
            MobileSavedVersionArchive::list(source,store).size()==1 &&
            load(firstSnapshot+QStringLiteral(".sha256"))==
                QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex(),
            "oversized dedup checksum is repaired without forking history");
    // A linked sidecar must not be trusted or read from outside history.
    const QString foreignDigest=temp.filePath(QStringLiteral("foreign-digest.txt"));
    save(foreignDigest,QByteArray(256,'f'));
    require(QFile::remove(firstSnapshot+QStringLiteral(".sha256")),
            "clear checksum before symlink test");
    if(QFile::link(foreignDigest,firstSnapshot+QStringLiteral(".sha256"))) {
        require(MobileSavedVersionArchive::snapshot(source,store) &&
                MobileSavedVersionArchive::list(source,store).size()==1 &&
                load(foreignDigest)==QByteArray(256,'f') &&
                load(firstSnapshot+QStringLiteral(".sha256"))==
                    QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex(),
                "linked checksum must not be read or modify its original");
    } else {
        save(firstSnapshot+QStringLiteral(".sha256"),
             QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
    }
    // An oversized or malformed sidecar must be rejected without
    // replacing the immutable checkpoint or opening a recovered copy.
    save(firstSnapshot+QStringLiteral(".sha256"),QByteArray(129,'0'));
    require(MobileSavedVersionArchive::materializeRecovery(
                MobileSavedVersionArchive::list(source,store).first(),recover).isEmpty(),
            "oversized checksum metadata must fail recovery");
    save(firstSnapshot+QStringLiteral(".sha256"),
         QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
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
    auto staleVersion=versions.last();
    ++staleVersion.bytes;
    require(MobileSavedVersionArchive::materializeRecovery(
                staleVersion,recover).isEmpty(),
            "stale version metadata must not start an invalid recovery copy");
    // A checksum sidecar may be missing after interrupted publication,
    // but a present checksum must prevent damaged data being restored.
    require(QFile::remove(firstSnapshot+QStringLiteral(".sha256")) &&
            !MobileSavedVersionArchive::materializeRecovery(
                versions.last(),recover).isEmpty(),
            "legacy checkpoint without checksum should still restore");
    auto legacyNoSize=versions.last();
    legacyNoSize.bytes=0; // Older callers did not retain file-size metadata.
    const QString legacyCopy=MobileSavedVersionArchive::materializeRecovery(
        legacyNoSize,recover);
    require(!legacyCopy.isEmpty() && load(legacyCopy)==initial,
            "legacy recovery without size metadata must preserve full KRA");
    QFile::remove(legacyCopy);
    const QString newestSnapshot=versions.first().path;
    const QByteArray newestOriginal=load(newestSnapshot);
    QByteArray checksumMismatch=newestOriginal;
    checksumMismatch[checksumMismatch.size()-1]='X';
    require(checksumMismatch.size()==newestOriginal.size(),
            "checksum corruption test must preserve archive size");
    save(newestSnapshot,checksumMismatch);
    require(MobileSavedVersionArchive::materializeRecovery(
                versions.first(),recover).isEmpty(),
            "damaged snapshot must fail checksum validation");
    save(newestSnapshot,newestOriginal);
    save(newestSnapshot,"broken header");
    require(MobileSavedVersionArchive::materializeRecovery(
                versions.first(),recover).isEmpty(),
            "invalid snapshot header must not be recovered");
    save(newestSnapshot,newestOriginal);
    // An existing sidecar must not cause a damaged latest KRA to be
    // treated as identical to the current good saved file.
    save(newestSnapshot,checksumMismatch);
    require(MobileSavedVersionArchive::snapshot(source,store),
            "failed to repair history after a damaged checkpoint");
    versions=MobileSavedVersionArchive::list(source,store);
    require(versions.size()==3 && load(versions.first().path)==second &&
            !MobileSavedVersionArchive::materializeRecovery(
                versions.first(),recover).isEmpty(),
            "damaged latest archive incorrectly suppressed a fresh save");

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
    const auto newestDraftCheckpoint=MobileSavedVersionArchive::list(
        draftProject,store).first().path;
    save(newestDraftCheckpoint+QStringLiteral(".sha256"),QByteArray(129,'0'));
    save(privateDraft,QByteArray::fromHex("504b0304")+"after-bad-checksum");
    require(MobileSavedVersionArchive::snapshotFrom(
                privateDraft,draftProject,store,2LL*60*1000) &&
            MobileSavedVersionArchive::list(draftProject,store).size()==historicalCount+2,
            "oversized latest checksum must not suppress a fresh checkpoint");


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
    // Retention must not evict the last recoverable older checkpoint
    // just because the newest filenames still exist but fail SHA-256.
    // Migrating an older donor triggers prune without publishing a new
    // valid latest snapshot, reproducing interrupted/corrupt history.
    const QString safeRoot=temp.filePath(QStringLiteral("fallback-history"));
    const QString safeProject=temp.filePath(QStringLiteral("fallback.kra"));
    const QString donorProject=temp.filePath(QStringLiteral("fallback-donor.kra"));
    const QString secondDonor=temp.filePath(QStringLiteral("fallback-donor-2.kra"));
    const QByteArray lastGood=QByteArray::fromHex("504b0304")+"last-good";
    const QByteArray secondGood=QByteArray::fromHex("504b0304")+"another-good";
    save(donorProject,lastGood);
    save(secondDonor,secondGood);
    require(MobileSavedVersionArchive::snapshot(donorProject,safeRoot) &&
            MobileSavedVersionArchive::snapshot(secondDonor,safeRoot),
            "create older healthy checkpoints");
    for(int i=0;i<MobileSavedVersionArchive::maxVersions();++i) {
        QThread::msleep(2);
        save(safeProject,QByteArray::fromHex("504b0304")+
            QByteArray::number(i)+QByteArray("history"));
        require(MobileSavedVersionArchive::snapshot(safeProject,safeRoot),
                "create bounded fallback archive");
    }
    const auto damagedVersions=MobileSavedVersionArchive::list(
        safeProject,safeRoot);
    require(damagedVersions.size()==MobileSavedVersionArchive::maxVersions(),
            "fill retention quota");
    for(const auto &candidate:damagedVersions)
        save(candidate.path,QByteArray("corrupt"));
    const QString corruptNewest=damagedVersions.first().path;
    MobileSavedVersionArchive::adoptHistory(
        donorProject,safeProject,safeRoot);
    MobileSavedVersionArchive::adoptHistory(
        secondDonor,safeProject,safeRoot);
    const auto retained=MobileSavedVersionArchive::list(safeProject,safeRoot);
    require(retained.size()<=MobileSavedVersionArchive::maxVersions() &&
            QFile::exists(corruptNewest),
            "retain damaged newest for inspection within quota");
    bool recoveredGood=false;
    bool recoveredSecond=false;
    for(const auto &candidate:retained) {
        const QString restored=MobileSavedVersionArchive::materializeRecovery(
            candidate,recover);
        if(restored.isEmpty())continue;
        const QByteArray payload=load(restored);
        if(payload==lastGood)recoveredGood=true;
        if(payload==secondGood)recoveredSecond=true;
        QFile::remove(restored);
    }
    require(recoveredGood && recoveredSecond,
            "retention must prioritize both verified older KRAs over corrupt intermediates");
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
    // A mistaken Save As target must never become a private draft
    // publication destination. In particular, a successful staging
    // export cannot overwrite a real project outside app-private drafts.
    const QString external=QDir(temp.path()).filePath(
        QStringLiteral("external-art.kra"));
    const QByteArray externalOriginal=QByteArray::fromHex("504b0304")+"original-art";
    save(external,externalOriginal);
    save(external+QStringLiteral(".pending.kra"),
         QByteArray::fromHex("504b0304")+"replacement-art");
    require(!MobileDraftRecovery::isDraftPath(external) &&
            !MobileDraftRecovery::publish(external,true) &&
            !MobileDraftRecovery::setProject(external,source) &&
            load(external)==externalOriginal,
            "draft publication must refuse external project paths");
    QFile::remove(external+QStringLiteral(".pending.kra"));
    const QString draft=MobileDraftRecovery::allocate(QStringLiteral("a/b"));
    require(!draft.isEmpty() && draft!=MobileDraftRecovery::allocate(QStringLiteral("a/b")),
            "draft paths must be private and unique");
    save(draft,QByteArray::fromHex("504b0304")+"draft");
    save(draft+QStringLiteral(".pending.kra"),"incomplete");
    const QString staging=draft+QStringLiteral(".pending.kra");
    require(MobileDraftRecovery::list().size()==1 &&
            !MobileDraftRecovery::publish(draft,true) &&
            !QFile::exists(staging) && QFileInfo(draft).size()==9,
            "interrupted staging must never replace the last completed checkpoint");
    save(staging,QByteArray::fromHex("504b0304")+"failed-export");
    require(!MobileDraftRecovery::publish(draft,false) &&
            !QFile::exists(staging) && QFileInfo(draft).size()==9,
            "an export failure must preserve the current checkpoint");
    save(staging,QByteArray::fromHex("504b0304")+"replacement");
    require(MobileDraftRecovery::publish(draft,true) &&
            !QFile::exists(staging) && QFileInfo(draft).size()==15,
            "successful publication must replace the draft at its stable path");
    // Publication can fail after the native KRA export completed, for
    // example because the destination is unexpectedly a directory.
    // The valid staging archive is the latest user data: retain it and
    // permit the same completed bytes to be published on retry.
    const QString blockedDraft=MobileDraftRecovery::allocate(
        QStringLiteral("blocked-publication"));
    require(!blockedDraft.isEmpty() && QDir().mkpath(blockedDraft),
            "create a temporarily unwritable publication destination");
    const QString blockedStage=blockedDraft+QStringLiteral(".pending.kra");
    const QByteArray latestEdits=QByteArray::fromHex("504b0304")+
        "recoverable-after-rename-failure";
    save(blockedStage,latestEdits);
    require(!MobileDraftRecovery::publish(blockedDraft,true) &&
            load(blockedStage)==latestEdits,
            "failed atomic replace discarded a completed autosave");
    require(QDir().rmdir(blockedDraft) &&
            MobileDraftRecovery::publish(blockedDraft,true) &&
            load(blockedDraft)==latestEdits &&
            !QFile::exists(blockedStage),
            "completed autosave could not publish after retry");
    QFile::remove(blockedDraft);
    require(MobileDraftRecovery::hasKraHeader(draft) &&
            !MobileDraftRecovery::hasKraHeader(
                draft+QStringLiteral(".pending.kra")),
            "only completed ZIP-header drafts qualify for automatic restore");
    // Startup recovery must not parse oversized, empty, or linked
    // archive inputs. A sparse oversized test file uses no 8 GiB
    // allocation on filesystems supporting sparse resize.
    const QString invalidDraft=temp.filePath(QStringLiteral("zero-size.kra"));
    save(invalidDraft,{});
    require(!MobileDraftRecovery::hasKraHeader(invalidDraft),
            "empty project is not recoverable");
    const QString linkedDraft=temp.filePath(QStringLiteral("linked-draft.kra"));
    if(QFile::link(draft,linkedDraft)) {
        require(!MobileDraftRecovery::hasKraHeader(linkedDraft),
                "linked archive must not be parsed for recovery");
        require(QFile::remove(linkedDraft),"remove linked test archive");
    }
    const QString hugeDraft=temp.filePath(QStringLiteral("sparse-oversized.kra"));
    {
        QFile huge(hugeDraft);
        if(huge.open(QIODevice::WriteOnly)) {
            require(huge.write(QByteArray::fromHex("504b0304"))==4,
                    "write sparse test ZIP signature");
            if(huge.resize(8LL*1024*1024*1024+1)) {
                huge.close();
                require(!MobileDraftRecovery::hasKraHeader(hugeDraft),
                        "oversized archive must be refused before ZIP parsing");
            } else huge.close();
        }
    }
    QFile::remove(hugeDraft);
    QFile::remove(invalidDraft);
    const auto drafts=MobileDraftRecovery::list();
    require(drafts.size()==1 && drafts.first().absoluteFilePath()==draft &&
            MobileDraftRecovery::title(drafts.first())==QStringLiteral("a_b") &&
            !MobileDraftRecovery::isDraft(source),
            "draft listing exposed staging or external files");
    require(MobileDraftRecovery::displayTitle(drafts.first(),QStringLiteral("Recovered"))==
                QStringLiteral("Recovered") &&
            MobileDraftRecovery::displayTitle(drafts.first(),QStringLiteral("Later"))==
                QStringLiteral("Recovered"),
            "a draft keeps its generated name across checkpoints");
    // A replaced title sidecar must never source the displayed draft
    // label from an external file. The generated name is safe fallback.
    const QString titleSidecar=draft+QStringLiteral(".title");
    const QString externalTitle=QDir(temp.path()).filePath(
        QStringLiteral("external-title.txt"));
    save(externalTitle,QByteArray("untrusted-external-title"));
    require(QFile::remove(titleSidecar),"clear draft title before linked metadata");
    if(QFile::link(externalTitle,titleSidecar)) {
        require(MobileDraftRecovery::displayTitle(
                    QFileInfo(draft),QStringLiteral("Recovered"))==
                    QStringLiteral("Recovered"),
                "linked title metadata cannot override draft label");
        require(QFile::remove(titleSidecar),
                "remove linked title sidecar");
    }
    require(MobileDraftRecovery::displayTitle(
                QFileInfo(draft),QStringLiteral("Sketch"))==
                QStringLiteral("Sketch"),
            "regenerate title after rejecting linked metadata");
    require(!MobileDraftRecovery::rename(source,QStringLiteral("External")) &&
            !MobileDraftRecovery::rename(draft,QStringLiteral("  ")) &&
            MobileDraftRecovery::rename(draft,QStringLiteral("  Sketch  ")) &&
            MobileDraftRecovery::displayTitle(drafts.first(),QStringLiteral("Later"))==
                QStringLiteral("Sketch"),
            "renaming a private draft must preserve its file identity");
    require(MobileSavedVersionArchive::isKritaProjectName(QStringLiteral("Untitled.kra (2)")) &&
            !MobileSavedVersionArchive::isKritaProjectName(QStringLiteral("drawing.kra.zip")),
            "recognize Android numbered KRA names without matching arbitrary archives");
    MobileDraftRecovery::setProject(draft,source);
    const QString privateMetadata=draft+QStringLiteral(".project");
    const QByteArray savedAssociation=load(privateMetadata);
    const QString externalMetadata=QDir(temp.path()).filePath(
        QStringLiteral("external-project-association"));
    save(externalMetadata,savedAssociation);
    require(MobileDraftRecovery::projectFor(external).isEmpty(),
            "external paths cannot supply private project associations");
    require(QFile::remove(privateMetadata),
            "remove private project metadata before symlink test");
    if(QFile::link(externalMetadata,privateMetadata)) {
        require(MobileDraftRecovery::projectFor(draft).isEmpty(),
                "linked project metadata must not redirect recovery");
        require(QFile::remove(privateMetadata),
                "remove linked private project metadata");
    }
    save(privateMetadata,savedAssociation);
    require(MobileDraftRecovery::projectFor(draft)==source,
            "restore private project association after symlink test");
    require(MobileDraftRecovery::hasProjectDraft(source) &&
            !MobileDraftRecovery::hasProjectDraft(external),
            "associated recovery must block deleting its original project");
    const qint64 draftTime=QFileInfo(draft).lastModified().toMSecsSinceEpoch();
    require(MobileDraftRecovery::projectFor(draft)==source &&
            MobileDraftRecovery::checkpointsFor(source,draftTime-1).value(0).absoluteFilePath()==draft &&
            MobileDraftRecovery::checkpointsFor(source,draftTime).isEmpty(),
            "only a newer checkpoint should replace the saved project");
    QThread::msleep(12);
    const QString damagedDraft=MobileDraftRecovery::allocate(
        QStringLiteral("newer-damaged"));
    save(damagedDraft,"truncated recovery");
    MobileDraftRecovery::setProject(damagedDraft,source);
    require(MobileDraftRecovery::checkpointsFor(source,draftTime-1).value(0).absoluteFilePath()==draft,
            "invalid latest recovery must fall back to an earlier intact draft");
    save(damagedDraft,QByteArray::fromHex("504b0304")+"newer-complete");
    require(MobileDraftRecovery::checkpointsFor(source,draftTime-1).value(0).absoluteFilePath()==damagedDraft,
            "newer valid project draft should supersede an older draft");
    QFile::remove(damagedDraft);
    QFile::remove(damagedDraft+QStringLiteral(".project"));
    MobileDraftRecovery::setProject(draft,QString());
    require(!MobileDraftRecovery::hasProjectDraft(source),
            "removing recovery association permits original project deletion");
    require(MobileDraftRecovery::projectFor(draft).isEmpty(),
            "an untitled draft must have no project association");
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
    const QString decoratedUri=treeUri1+
        QStringLiteral("?grant=temporary#picker");
    require(MobileSavedVersionArchive::canonicalIdentity(decoratedUri)==docUri,
            "picker URI query and fragment must not fork a document identity");
    const QString docFolder=MobileSavedVersionArchive::folderFor(docUri,store);
    const QString treeFolder1=MobileSavedVersionArchive::folderFor(treeUri1,store);
    const QString treeFolder2=MobileSavedVersionArchive::folderFor(treeUri2,store);
    require(!docFolder.isEmpty(), "document version folder must not be empty");
    require(docFolder==treeFolder1 && docFolder==treeFolder2 &&
            docFolder==MobileSavedVersionArchive::folderFor(decoratedUri,store),
            "SAF document, tree and decorated picker URIs must share version history");
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

    // Legacy and canonical folders may already contain different KRA
    // snapshots bearing exactly the same filename after interrupted
    // migrations. Both must survive with their own checksum sidecars.
    const QString collisions=QDir(temp.path()).filePath(
        QStringLiteral("colliding-history"));
    const QString canonicalFolder=MobileSavedVersionArchive::folderFor(
        docUri,collisions);
    const QString oldFolder=QDir(collisions).filePath(legacyDigest);
    require(QDir().mkpath(canonicalFolder) && QDir().mkpath(oldFolder),
            "cannot arrange colliding archive folders");
    const QString sameName=QStringLiteral(
        "20261110T000000000-00000000000000000000000000000002.kra");
    const QByteArray oldBytes=QByteArray::fromHex("504b0304")+"old-collision";
    const QByteArray newBytes=QByteArray::fromHex("504b0304")+"new-collision";
    const QString oldPath=QDir(oldFolder).filePath(sameName);
    const QString canonicalPath=QDir(canonicalFolder).filePath(sameName);
    save(oldPath,oldBytes);
    save(oldPath+QStringLiteral(".sha256"),
         QCryptographicHash::hash(oldBytes,QCryptographicHash::Sha256).toHex());
    save(canonicalPath,newBytes);
    save(canonicalPath+QStringLiteral(".sha256"),
         QCryptographicHash::hash(newBytes,QCryptographicHash::Sha256).toHex());
    const auto collisionVersions=MobileSavedVersionArchive::list(treeUri1,collisions);
    require(collisionVersions.size()==2,
            "colliding canonical and legacy versions must both survive");
    bool foundOld=false,foundNew=false;
    for(const auto &version:collisionVersions) {
        const QByteArray bytes=load(version.path);
        foundOld|=bytes==oldBytes;
        foundNew|=bytes==newBytes;
        require(!MobileSavedVersionArchive::materializeRecovery(
                    version,recover).isEmpty(),
                "migrated collision must retain its correct checksum");
    }
    require(foundOld && foundNew && !QDir(oldFolder).exists(),
            "colliding migration lost a snapshot or left an incomplete folder");

    qInfo() << "PASS: snapshot, dedup, atomic recovery, retention,"
               " provider identity, SAF tree normalization and legacy migration";
    return 0;
}
