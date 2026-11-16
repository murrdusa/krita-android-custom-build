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
#include <QUrl>
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
    require(MobileSavedVersionArchive::privateVersionRoot(QString()).isEmpty(),
            "empty Android app-data root resolved to a relative archive directory");
    require(MobileSavedVersionArchive::privateVersionRoot(temp.path())==
            QDir(temp.path()).filePath(QStringLiteral("saved-versions")),
            "valid private archive root was not preserved");
    const QString source=QDir(temp.path()).filePath(
        QStringLiteral("My Drawing.kra"));
    const QString store=QDir(temp.path()).filePath(
        QStringLiteral("private-history"));
    const QString recover=QDir(temp.path()).filePath(
        QStringLiteral("private-recovery"));
    const QByteArray initial=QByteArray::fromHex("504b0304")+"saved-A";
    save(source,initial);
    // Restored settings can carry a legacy revision map far beyond the
    // 128-entry limit. One new Save should restore the cap immediately.
    const QString previewKey=QStringLiteral("MobileStudio/savedPreviewRevisions");
    QSettings settings;
    const QVariant originalPreview=settings.value(previewKey);
    const bool hadPreview=settings.contains(previewKey);
    QVariantMap oversizedPreview;
    for(int i=0;i<4096;++i)
        oversizedPreview.insert(QStringLiteral("legacy-%1").arg(i),i+1);
    settings.setValue(previewKey,oversizedPreview);
    MobileSavedVersionArchive::recordPreviewRevision(source);
    const QVariantMap trimmedPreview=MobileSavedVersionArchive::previewRevisions();
    require(trimmedPreview.size()==128 &&
            trimmedPreview.contains(
                MobileSavedVersionArchive::canonicalIdentity(source)) &&
            trimmedPreview.contains(QStringLiteral("legacy-4095")) &&
            trimmedPreview.contains(QStringLiteral("legacy-3969")) &&
            !trimmedPreview.contains(QStringLiteral("legacy-3968")),
            "saved-preview revision map did not prune a legacy backlog");
    if(hadPreview)settings.setValue(previewKey,originalPreview);
    else settings.remove(previewKey);
    const QString uri=QStringLiteral("content://provider/document/42");
    const QString uriFolder=MobileSavedVersionArchive::folderFor(uri,store);
    require(!uriFolder.isEmpty() &&
            uriFolder!=MobileSavedVersionArchive::folderFor(source,store) &&
            uriFolder!=MobileSavedVersionArchive::folderFor(uri+QStringLiteral("0"),store),
            "opaque provider URI needs a distinct project history");
    require(!MobileSavedVersionArchive::snapshot(uri,store) &&
            MobileSavedVersionArchive::list(uri,store).isEmpty(),
            "unreadable provider must not produce a version");
    // A routine rate-limited autosave should inspect only the latest
    // archive, even if a crash left many older files in its folder.
    // Retention cleanup intentionally keeps its complete inventory.
    const QString boundedRoot=temp.filePath(QStringLiteral("latest-only"));
    require(MobileSavedVersionArchive::snapshot(source,boundedRoot),
            "could not publish latest-only autosave fixture");
    const QString boundedFolder=MobileSavedVersionArchive::folderFor(
        source,boundedRoot);
    for(int i=0;i<48;++i) {
        const QString legacy=QDir(boundedFolder).filePath(
            QStringLiteral("20000101T000000000-%1.kra").arg(i,4,10,QLatin1Char('0')));
        save(legacy,QByteArray::fromHex("504b0304")+"old");
    }
    const auto beforeRateLimit=MobileSavedVersionArchive::list(source,boundedRoot);
    require(beforeRateLimit.size()==49 &&
            MobileSavedVersionArchive::snapshotFrom(
                source,source,boundedRoot,600000),
            "latest-only rate-limit failed with a large historical backlog");
    const auto afterRateLimit=MobileSavedVersionArchive::list(source,boundedRoot);
    require(afterRateLimit.size()==beforeRateLimit.size() &&
            afterRateLimit.first().path==beforeRateLimit.first().path,
            "latest-only scan changed the published archive inventory");

    const QString encodedProject=temp.filePath(
        QStringLiteral("a drawing with spaces.kra"));
    const QByteArray encodedBytes=QByteArray::fromHex("504b0304")+"url-encoded";
    save(encodedProject,encodedBytes);
    const QString encodedUrl=QUrl::fromLocalFile(encodedProject).toString();
    require(MobileSavedVersionArchive::snapshot(encodedUrl,store) &&
            MobileSavedVersionArchive::folderFor(encodedUrl,store)==
                MobileSavedVersionArchive::folderFor(encodedProject,store) &&
            load(MobileSavedVersionArchive::list(encodedProject,store)
                     .first().path)==encodedBytes,
            "escaped local file URL could not publish a usable backup");
    // An uncreated Save As destination under a symlinked directory
    // must map to the same version history as its real parent path.
    const QString realParent=temp.filePath(QStringLiteral("real-drawings"));
    const QString linkedParent=temp.filePath(QStringLiteral("linked-drawings"));
    require(QDir().mkpath(realParent),"create real Save As parent");
    if(QFile::link(realParent,linkedParent)) {
        const QString canonicalFuture=QDir(realParent).filePath(
            QStringLiteral("future project.kra"));
        const QString aliasFuture=QDir(linkedParent).filePath(
            QStringLiteral("future project.kra"));
        require(MobileSavedVersionArchive::canonicalIdentity(canonicalFuture)==
                    MobileSavedVersionArchive::canonicalIdentity(aliasFuture) &&
                MobileSavedVersionArchive::folderFor(canonicalFuture,store)==
                    MobileSavedVersionArchive::folderFor(aliasFuture,store) &&
                MobileSavedVersionArchive::canonicalIdentity(
                    QUrl::fromLocalFile(aliasFuture).toString())==
                    MobileSavedVersionArchive::canonicalIdentity(canonicalFuture),
                "symlinked Save As parent forked future project history");
        require(QFile::remove(linkedParent),
                "remove symlinked Save As test directory");
    }
    const QString localUrl=QUrl::fromLocalFile(source).toString();
    require(MobileSavedVersionArchive::canonicalIdentity(localUrl)==
                MobileSavedVersionArchive::canonicalIdentity(source) &&
            MobileSavedVersionArchive::folderFor(localUrl,store)==
                MobileSavedVersionArchive::folderFor(source,store) &&
            MobileSavedVersionArchive::projectName(localUrl)==
                QFileInfo(source).fileName() &&
            MobileSavedVersionArchive::projectModified(localUrl)==
                MobileSavedVersionArchive::projectModified(source),
            "file URL and local path must share saved-version identity");
    require(MobileSavedVersionArchive::snapshot(source,store),
            "first completed save snapshot failed");
    require(MobileSavedVersionArchive::snapshot(localUrl,store) &&
            MobileSavedVersionArchive::list(localUrl,store).size()==1,
            "saving through a file URL forked the local version history");
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
    // Simulate a crash or checksum write failure after the KRA itself
    // committed. The archived bytes remain a usable recovery source
    // without the optional SHA sidecar, then the next save repairs it.
    require(!MobileSavedVersionArchive::materializeRecovery(
                MobileSavedVersionArchive::list(source,store).first(),recover).isEmpty(),
            "orphaned completed archive was not recoverable");
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
    // Cancellation before a large recovery starts must not publish a
    // temporary copy or remove the historical source.
    std::atomic_bool abortRecovery{true};
    const QString cancelledCopy=MobileSavedVersionArchive::materializeRecovery(
        versions.last(),recover,&abortRecovery);
    require(cancelledCopy.isEmpty() &&
            QFile::exists(firstSnapshot) &&
            QFileInfo(copy).isFile(),
            "cancelled historical recovery changed existing artwork");
    abortRecovery.store(false);
    const QString resumedCopy=MobileSavedVersionArchive::materializeRecovery(
        versions.last(),recover,&abortRecovery);
    require(!resumedCopy.isEmpty() && load(resumedCopy)==initial,
            "noncancelled historical recovery failed");
    QFile::remove(resumedCopy);
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
            MobileSavedVersionArchive::isRecoveryCopy(
                QUrl::fromLocalFile(restored).toString()) &&
            !MobileSavedVersionArchive::isRecoveryCopy(
                QUrl::fromLocalFile(source).toString()) &&
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
    // Recovery header probes must reject a huge sparse file before
    // handing it to the native KRA reader, without allocating its size.
    const QString oversizedHeader=QDir(temp.path()).filePath(
        QStringLiteral("oversized-recovery.kra"));
    {
        QFile sparse(oversizedHeader);
        require(sparse.open(QIODevice::WriteOnly) &&
                sparse.write(QByteArray::fromHex("504b0304"))==4 &&
                sparse.resize(8LL*1024*1024*1024+1),
                "cannot prepare oversized sparse recovery fixture");
    }
    require(!MobileDraftRecovery::hasKraHeader(oversizedHeader),
            "oversized recovery archive bypassed header size guard");
    QFile::remove(oversizedHeader);
    const QString draft=MobileDraftRecovery::allocate(QStringLiteral("a/b"));
    require(!draft.isEmpty() && draft!=MobileDraftRecovery::allocate(QStringLiteral("a/b")),
            "draft paths must be private and unique");
    save(draft,QByteArray::fromHex("504b0304")+"draft");
    save(draft+QStringLiteral(".pending.kra"),"incomplete");
    const QString staging=draft+QStringLiteral(".pending.kra");
    require(MobileDraftRecovery::list().size()==1 &&
            !MobileDraftRecovery::publish(draft,true) &&
            load(staging)==QByteArray("incomplete") &&
            QFileInfo(draft).size()==9,
            "invalid successful export must preserve staging without replacing the last checkpoint");
    // A failed exporter can leave a nonparseable partial KRA. Its
    // bytes must not be destroyed by the failure cleanup path.
    const QByteArray partialExport("partial-user-data");
    save(staging,partialExport);
    require(!MobileDraftRecovery::publish(draft,false) &&
            load(staging)==partialExport && QFileInfo(draft).size()==9,
            "failed export discarded partial staging user data");
    const QByteArray failedExport=QByteArray::fromHex("504b0304")+
        "failed-export-but-readable";
    save(staging,failedExport);
    require(MobileDraftRecovery::setProject(draft,source),
            "associate original autosave before failed exporter test");
    require(!MobileDraftRecovery::publish(draft,false) &&
            !QFile::exists(staging) && QFileInfo(draft).size()==9,
            "failed export must not overwrite published draft");
    QString failedExportRescue;
    for(const QFileInfo &candidate:MobileDraftRecovery::list()) {
        if(candidate.absoluteFilePath()!=draft &&
           load(candidate.absoluteFilePath())==failedExport)
            failedExportRescue=candidate.absoluteFilePath();
    }
    require(!failedExportRescue.isEmpty() &&
            MobileDraftRecovery::isDraft(failedExportRescue) &&
            MobileDraftRecovery::projectFor(failedExportRescue)==source,
            "failed export lost readable edits or saved-project association");
    QFile::remove(failedExportRescue);
    QFile::remove(failedExportRescue+QStringLiteral(".project"));
    require(MobileDraftRecovery::setProject(draft,QString()),
            "clear failed exporter fixture association");
    save(staging,QByteArray::fromHex("504b0304")+"replacement");
    // A staged export can change while ZIP verification is in
    // progress. The same pre/post metadata check used by publish()
    // must detect that change before overwriting the last good draft.
    const QFileInfo beforePublication(staging);
    require(MobileDraftRecovery::unchangedStaging(beforePublication),
            "unchanged recovery staging failed identity validation");
    {
        QFile interrupted(staging);
        require(interrupted.open(QIODevice::WriteOnly|QIODevice::Append) &&
                interrupted.write("changed-after-verification")==
                    qint64(sizeof("changed-after-verification")-1),
                "could not mutate staged recovery fixture");
    }
    require(!MobileDraftRecovery::unchangedStaging(beforePublication) &&
            load(draft)==QByteArray::fromHex("504b0304")+"draft",
            "modified recovery stage was not distinguished from last good draft");
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
    save(blockedDraft+QStringLiteral(".title"),"User-customized-title");
    require(MobileDraftRecovery::setProject(blockedDraft,source),
            "associate blocked autosave with its saved project");
    require(!MobileDraftRecovery::publish(blockedDraft,true),
            "destination directory unexpectedly allowed a file rename");
    QString rescuedDraft;
    for(const QFileInfo &candidate:MobileDraftRecovery::list()) {
        if(candidate.absoluteFilePath()!=draft &&
           load(candidate.absoluteFilePath())==latestEdits)
            rescuedDraft=candidate.absoluteFilePath();
    }
    require(!rescuedDraft.isEmpty() &&
            MobileDraftRecovery::hasReadableProject(rescuedDraft) &&
            MobileDraftRecovery::projectFor(rescuedDraft)==source &&
            load(rescuedDraft+QStringLiteral(".title"))==
                QByteArray("User-customized-title") &&
            !QFile::exists(blockedStage),
            "failed publication lost recoverable project or its title");
    require(QDir().rmdir(blockedDraft),
            "remove blocked destination directory");
    QFile::remove(blockedDraft+QStringLiteral(".project"));
    QFile::remove(rescuedDraft);
    QFile::remove(rescuedDraft+QStringLiteral(".project"));
    QFile::remove(rescuedDraft+QStringLiteral(".title"));
    QFile::remove(blockedDraft+QStringLiteral(".title"));
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
    // A process crash may leave a fully written .pending.kra which
    // was never published. Recent staging is left alone for active
    // exporters; old completed staging becomes a separate recovery.
    // A background exporter can overwrite a pending file while the
    // recovery scanner verifies its ZIP structure. A stale QFileInfo
    // snapshot must not authorize moving those newly written bytes.
    const QString changingStage=temp.filePath(QStringLiteral("changing-stage.kra"));
    save(changingStage,QByteArray::fromHex("504b0304")+"old-stage");
    const QFileInfo beforeChange(changingStage);
    require(MobileDraftRecovery::unchangedStaging(beforeChange),
            "intact staging metadata unexpectedly changed");
    save(changingStage,QByteArray::fromHex("504b0304")+
         "newer-and-longer-stage");
    require(!MobileDraftRecovery::unchangedStaging(beforeChange),
            "recovery scanner accepted staging modified during verification");
    QFile::remove(changingStage);
    const QByteArray abandonedBytes=QByteArray::fromHex("504b0304")+
        "abandoned-but-complete";
    save(staging,abandonedBytes);
    require(MobileDraftRecovery::setProject(draft,source),
            "associate abandoned staging with saved project");
    require(MobileDraftRecovery::list().size()==1 &&
            load(staging)==abandonedBytes,
            "recent unfinished publication was recovered too early");
    {
        QFile pending(staging);
        require(pending.open(QIODevice::ReadWrite) &&
                pending.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600),
                    QFileDevice::FileModificationTime),
                "age the abandoned publication fixture");
    }
    const auto recoveredListing=MobileDraftRecovery::list();
    QString abandonedRescue;
    for(const QFileInfo &candidate:recoveredListing) {
        if(candidate.absoluteFilePath()!=draft &&
           load(candidate.absoluteFilePath())==abandonedBytes)
            abandonedRescue=candidate.absoluteFilePath();
    }
    require(!abandonedRescue.isEmpty() &&
            MobileDraftRecovery::projectFor(abandonedRescue)==source &&
            load(draft)==QByteArray::fromHex("504b0304")+"replacement" &&
            !QFile::exists(staging),
            "abandoned publication did not retain both complete canvases");
    QFile::remove(abandonedRescue);
    QFile::remove(abandonedRescue+QStringLiteral(".project"));
    require(MobileDraftRecovery::setProject(draft,QString()),
            "clear abandoned staging fixture project association");
    // Many leftover exports must not cause an unbounded number of KRA
    // parses in a single gallery refresh. Each scan rescues at most two
    // staged files and leaves the rest recoverable on later refreshes.
    QStringList pendingBatch;
    for(int i=0;i<5;++i) {
        const QString candidate=MobileDraftRecovery::allocate(
            QStringLiteral("queued-recovery"));
        require(!candidate.isEmpty(),"allocate bulk orphan fixture");
        const QString orphan=candidate+QStringLiteral(".pending.kra");
        save(orphan,QByteArray::fromHex("504b0304")+
                    QByteArray("queued-")+QByteArray::number(i));
        require(MobileDraftRecovery::setProject(candidate,source),
                "associate bulk orphan with project");
        QFile file(orphan);
        require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600),
                    QFileDevice::FileModificationTime),
                "age queued orphan fixture");
        pendingBatch.append(candidate);
    }
    const auto firstBatch=MobileDraftRecovery::list();
    require(firstBatch.size()==3,
            "gallery scan must rescue at most two abandoned KRAs");
    const auto secondBatch=MobileDraftRecovery::list();
    require(secondBatch.size()==5,
            "second gallery scan must rescue the next two KRAs");
    const auto thirdBatch=MobileDraftRecovery::list();
    require(thirdBatch.size()==6,
            "subsequent gallery scan must eventually rescue all KRAs");
    int batchRecovered=0;
    for(const QFileInfo &item:thirdBatch) {
        if(item.absoluteFilePath()==draft)continue;
        ++batchRecovered;
        require(MobileDraftRecovery::projectFor(item.absoluteFilePath())==source,
                "batched orphan lost original project association");
        QFile::remove(item.absoluteFilePath());
        QFile::remove(item.absoluteFilePath()+QStringLiteral(".project"));
        QFile::remove(item.absoluteFilePath()+QStringLiteral(".title"));
    }
    require(batchRecovered==5,"not all queued autosaves were recovered");
    for(const QString &candidate:pendingBatch) {
        require(!QFile::exists(candidate+QStringLiteral(".pending.kra")),
                "orphan staging survived after successful rescue");
        QFile::remove(candidate+QStringLiteral(".project"));
    }
    // Malformed older staging files cannot permanently block a later
    // valid orphan. Rotating the bounded scan must eventually examine
    // all candidates without deleting damaged files.
    QStringList invalidStages;
    for(int i=0;i<3;++i) {
        const QString name=MobileDraftRecovery::allocate(
            QStringLiteral("invalid-orphan"));
        require(!name.isEmpty(),"allocate invalid orphan");
        const QString stage=name+QStringLiteral(".pending.kra");
        save(stage,QByteArray("truncated-or-invalid"));
        QFile file(stage);
        require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600),
                    QFileDevice::FileModificationTime),
                "age malformed orphan");
        invalidStages.append(stage);
    }
    const QString validOrphan=MobileDraftRecovery::allocate(
        QStringLiteral("valid-behind-invalid"));
    const QString validStage=validOrphan+QStringLiteral(".pending.kra");
    const QByteArray validOrphanBytes=QByteArray::fromHex("504b0304")+
        "should-not-starve";
    save(validStage,validOrphanBytes);
    {
        QFile file(validStage);
        require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600),
                    QFileDevice::FileModificationTime),
                "age valid orphan behind invalid entries");
    }
    QString nonStarvedRecovery;
    for(int attempt=0;attempt<8 && nonStarvedRecovery.isEmpty();++attempt) {
        for(const QFileInfo &entry:MobileDraftRecovery::list()) {
            if(entry.absoluteFilePath()!=draft &&
               load(entry.absoluteFilePath())==validOrphanBytes)
                nonStarvedRecovery=entry.absoluteFilePath();
        }
    }
    require(!nonStarvedRecovery.isEmpty() && QFile::exists(invalidStages.first()),
            "damaged staging starved a valid later recovery");
    QFile::remove(nonStarvedRecovery);
    for(const QString &invalid:invalidStages)QFile::remove(invalid);
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

    // Streaming migration must handle several complete archive/sidecar
    // pairs and preserve unrelated metadata without a full file listing.
    const QString bulkStore=temp.filePath(QStringLiteral("bulk-migration"));
    const QString bulkDraft=temp.filePath(QStringLiteral("bulk-draft.kra"));
    const QString bulkProject=temp.filePath(QStringLiteral("bulk-saved.kra"));
    const QString bulkSource=MobileSavedVersionArchive::folderFor(bulkDraft,bulkStore);
    const QString bulkTarget=MobileSavedVersionArchive::folderFor(bulkProject,bulkStore);
    require(QDir().mkpath(bulkSource) && QDir().mkpath(bulkTarget),
            "create streaming history migration fixture");
    save(QDir(bulkSource).filePath(QStringLiteral("notes.txt")),
         QByteArray("preserve-metadata"));
    for(int i=0;i<7;++i) {
        const QString name=QStringLiteral("20261114T%1-%2.kra")
            .arg(i,9,10,QLatin1Char('0'))
            .arg(QStringLiteral("streaming"));
        const QByteArray payload=QByteArray::fromHex("504b0304")+
            QByteArray::number(i);
        const QString archive=QDir(bulkSource).filePath(name);
        save(archive,payload);
        save(archive+QStringLiteral(".sha256"),
             QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    }
    MobileSavedVersionArchive::adoptHistory(bulkDraft,bulkProject,bulkStore);
    const auto migratedBulk=MobileSavedVersionArchive::list(bulkProject,bulkStore);
    require(migratedBulk.size()==7 &&
            load(QDir(bulkTarget).filePath(QStringLiteral("notes.txt")))==
                QByteArray("preserve-metadata") &&
            !QDir(bulkSource).exists(),
            "streaming migration lost version pairs or unrelated metadata");
    for(const auto &version:migratedBulk)
        require(!MobileSavedVersionArchive::materializeRecovery(version,recover).isEmpty(),
                "streaming migration detached a version from its checksum");

    // Save As must not copy an oversized or symlinked SHA sidecar, nor
    // detach a source KRA from that sidecar when migration rejects it.
    const QString unsafeStore=temp.filePath(QStringLiteral("unsafe-sidecars"));
    const QString unsafeDraft=temp.filePath(QStringLiteral("unsafe-draft.kra"));
    const QString unsafeSaved=temp.filePath(QStringLiteral("unsafe-saved.kra"));
    const QString unsafeFrom=MobileSavedVersionArchive::folderFor(
        unsafeDraft,unsafeStore);
    const QString unsafeTo=MobileSavedVersionArchive::folderFor(
        unsafeSaved,unsafeStore);
    require(QDir().mkpath(unsafeFrom) && QDir().mkpath(unsafeTo),
            "create checksum migration fixture folders");
    const QString largeArchive=QDir(unsafeFrom).filePath(
        QStringLiteral("20261114T000000000-oversized.kra"));
    save(largeArchive,initial);
    save(largeArchive+QStringLiteral(".sha256"),QByteArray(4096,'a'));
    MobileSavedVersionArchive::adoptHistory(unsafeDraft,unsafeSaved,unsafeStore);
    require(QFile::exists(largeArchive) &&
            QFile::exists(largeArchive+QStringLiteral(".sha256")) &&
            MobileSavedVersionArchive::list(unsafeSaved,unsafeStore).isEmpty(),
            "oversized checksum was migrated or original archive lost");
    // A short regular sidecar is not necessarily a valid SHA-256.
    // Keep its KRA beside it instead of migrating unusable metadata.
    const QString malformedArchive=QDir(unsafeFrom).filePath(
        QStringLiteral("20261114T000000002-malformed.kra"));
    save(malformedArchive,initial);
    save(malformedArchive+QStringLiteral(".sha256"),QByteArray("not-a-sha256"));
    MobileSavedVersionArchive::adoptHistory(unsafeDraft,unsafeSaved,unsafeStore);
    require(QFile::exists(malformedArchive) &&
            load(malformedArchive+QStringLiteral(".sha256"))==
                QByteArray("not-a-sha256") &&
            MobileSavedVersionArchive::list(unsafeSaved,unsafeStore).isEmpty(),
            "malformed checksum was migrated or separated from its KRA");
    // Even a syntactically valid 64-digit digest may describe
    // different bytes. Migration must preserve that damaged pair for
    // repair instead of advertising it as a recoverable saved version.
    const QString mismatchedArchive=QDir(unsafeFrom).filePath(
        QStringLiteral("20261114T000000003-mismatched.kra"));
    save(mismatchedArchive,initial);
    const QByteArray incorrectHash=QCryptographicHash::hash(
        QByteArray("different-KRA-bytes"),QCryptographicHash::Sha256).toHex();
    save(mismatchedArchive+QStringLiteral(".sha256"),incorrectHash);
    MobileSavedVersionArchive::adoptHistory(unsafeDraft,unsafeSaved,unsafeStore);
    require(QFile::exists(mismatchedArchive) &&
            load(mismatchedArchive+QStringLiteral(".sha256"))==incorrectHash &&
            MobileSavedVersionArchive::list(unsafeSaved,unsafeStore).isEmpty(),
            "valid-looking but incorrect checksum migrated damaged history");
    const QString linkedArchive=QDir(unsafeFrom).filePath(
        QStringLiteral("20261114T000000001-linked.kra"));
    save(linkedArchive,initial);
    const QString digestTarget=temp.filePath(QStringLiteral("external-digest"));
    save(digestTarget,QCryptographicHash::hash(
        initial,QCryptographicHash::Sha256).toHex());
    if(QFile::link(digestTarget,linkedArchive+QStringLiteral(".sha256"))) {
        MobileSavedVersionArchive::adoptHistory(unsafeDraft,unsafeSaved,unsafeStore);
        require(QFile::exists(linkedArchive) &&
                QFileInfo(linkedArchive+QStringLiteral(".sha256")).isSymLink() &&
                MobileSavedVersionArchive::list(unsafeSaved,unsafeStore).isEmpty(),
                "linked checksum was followed or detached during migration");
    }

    // Save As to a brand-new history folder must not bypass checksum
    // validation via an unconditional whole-directory rename. Valid
    // versions should move, while mismatched ones remain recoverable at
    // the source path with their original SHA sidecars.
    const QString freshStore=temp.filePath(QStringLiteral("fresh-destination-history"));
    const QString freshDraft=temp.filePath(QStringLiteral("fresh-draft.kra"));
    const QString freshSaved=temp.filePath(QStringLiteral("fresh-saved.kra"));
    const QString freshFrom=MobileSavedVersionArchive::folderFor(freshDraft,freshStore);
    const QString freshTo=MobileSavedVersionArchive::folderFor(freshSaved,freshStore);
    require(QDir().mkpath(freshFrom) && !QDir(freshTo).exists(),
            "create new-destination history migration fixture");
    const QString goodArchive=QDir(freshFrom).filePath(
        QStringLiteral("20261114T000000000-valid.kra"));
    const QString badArchive=QDir(freshFrom).filePath(
        QStringLiteral("20261114T000000001-invalid.kra"));
    save(goodArchive,initial);
    save(goodArchive+QStringLiteral(".sha256"),
         QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
    save(badArchive,initial);
    const QByteArray badDigest=QCryptographicHash::hash(
        QByteArray("unrelated bytes"),QCryptographicHash::Sha256).toHex();
    save(badArchive+QStringLiteral(".sha256"),badDigest);
    MobileSavedVersionArchive::adoptHistory(freshDraft,freshSaved,freshStore);
    const auto freshVersions=MobileSavedVersionArchive::list(freshSaved,freshStore);
    require(freshVersions.size()==1 &&
            load(freshVersions.first().path)==initial &&
            !QFile::exists(goodArchive) &&
            QFile::exists(badArchive) &&
            load(badArchive+QStringLiteral(".sha256"))==badDigest,
            "new destination bypassed checksum checks or lost damaged history");

    // A corrupt sparse checkpoint can claim to be a multi-gigabyte KRA.
    // The recent-checkpoint validator must refuse it before hashing the
    // entire file or considering it sufficient to suppress a new save.
    const QString oversizedStore=QDir(temp.path()).filePath(
        QStringLiteral("oversized-history"));
    const QString oversizedFolder=MobileSavedVersionArchive::folderFor(
        source,oversizedStore);
    require(QDir().mkpath(oversizedFolder),
            "create oversized historical checkpoint folder");
    const QString oversizedVersion=QDir(oversizedFolder).filePath(
        QStringLiteral("zzzz-oversized.kra"));
    {
        QFile huge(oversizedVersion);
        require(huge.open(QIODevice::WriteOnly) &&
                huge.write(QByteArray::fromHex("504b0304"))==4,
                "create sparse oversized history fixture");
        if(huge.resize(8LL*1024*1024*1024+1)) {
            huge.close();
            save(oversizedVersion+QStringLiteral(".sha256"),
                 QByteArray(64,'0'));
            require(MobileSavedVersionArchive::snapshotFrom(
                        source,source,oversizedStore,120000),
                    "oversized checkpoint suppressed a valid version");
            const auto versionsAfter=MobileSavedVersionArchive::list(
                source,oversizedStore);
            bool recoveredValid=false;
            for(const auto &entry:versionsAfter) {
                if(entry.bytes==initial.size() &&
                   load(entry.path)==initial)recoveredValid=true;
            }
            require(recoveredValid,
                    "valid version was not published past oversized checkpoint");
        } else huge.close();
    }
    QFile::remove(oversizedVersion);
    QFile::remove(oversizedVersion+QStringLiteral(".sha256"));

    // The newest archive may itself be an oversized sparse file, while
    // an older verified checkpoint is the only working recovery copy.
    // Reserve that fallback without signed-size overflow or eviction.
    const QString fallbackRoot=temp.filePath(QStringLiteral("sparse-fallback"));
    const QString fallbackFolder=MobileSavedVersionArchive::folderFor(
        source,fallbackRoot);
    require(QDir().mkpath(fallbackFolder),
            "create sparse fallback history folder");
    const QString fallbackGood=QDir(fallbackFolder).filePath(
        QStringLiteral("20000101T000000000-good.kra"));
    save(fallbackGood,initial);
    save(fallbackGood+QStringLiteral(".sha256"),
         QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
    const QString fallbackHuge=QDir(fallbackFolder).filePath(
        QStringLiteral("zzzz-largest-damaged.kra"));
    {
        QFile huge(fallbackHuge);
        require(huge.open(QIODevice::WriteOnly) &&
                huge.write(QByteArray::fromHex("504b0304"))==4,
                "create newest sparse fallback archive");
        if(huge.resize(8LL*1024*1024*1024+1)) {
            huge.close();
            require(MobileSavedVersionArchive::snapshot(source,fallbackRoot),
                    "sparse newest checkpoint interrupted pruning");
            const auto kept=MobileSavedVersionArchive::list(source,fallbackRoot);
            bool fallbackSurvived=false;
            for(const auto &version:kept)
                if(version.path==fallbackGood)fallbackSurvived=true;
            require(fallbackSurvived && QFile::exists(fallbackGood),
                    "oversized newest archive evicted verified older fallback");
        } else huge.close();
    }
    QFile::remove(fallbackHuge);
    QFile::remove(fallbackGood);
    QFile::remove(fallbackGood+QStringLiteral(".sha256"));

    // When the latest history version is healthy, corrupt intermediate
    // entries still must not evict the last older, verified checkpoint.
    const QString priorityRoot=temp.filePath(QStringLiteral("verified-priority"));
    const QString priorityFolder=MobileSavedVersionArchive::folderFor(
        source,priorityRoot);
    require(QDir().mkpath(priorityFolder),
            "create priority pruning fixture");
    const QString olderGood=QDir(priorityFolder).filePath(
        QStringLiteral("00000000-older-good.kra"));
    save(olderGood,initial);
    save(olderGood+QStringLiteral(".sha256"),
         QCryptographicHash::hash(initial,QCryptographicHash::Sha256).toHex());
    for(int i=0;i<MobileSavedVersionArchive::maxVersions();++i) {
        const QString bad=QDir(priorityFolder).filePath(
            QStringLiteral("20261112T000000%1-corrupt.kra")
                .arg(i,3,10,QLatin1Char('0')));
        save(bad,QByteArray::fromHex("504b0304")+
                 QByteArray("damaged-intermediate")+QByteArray::number(i));
        save(bad+QStringLiteral(".sha256"),QByteArray(64,'0'));
    }
    require(MobileSavedVersionArchive::snapshot(source,priorityRoot),
            "create newest valid checkpoint for priority pruning");
    const auto priorityVersions=MobileSavedVersionArchive::list(
        source,priorityRoot);
    bool oldSurvived=false;
    for(const auto &entry:priorityVersions)
        if(entry.path==olderGood)oldSurvived=true;
    require(oldSurvived && priorityVersions.size()<=
                MobileSavedVersionArchive::maxVersions(),
            "corrupt intermediate history displaced the older valid KRA");

    // An oversized sparse archive must not consume an arithmetic quota
    // slot or cause cleanup to discard newer verified saved content.
    const QString quotaRoot=temp.filePath(QStringLiteral("sparse-quota"));
    const QString quotaFolder=MobileSavedVersionArchive::folderFor(
        source,quotaRoot);
    require(QDir().mkpath(quotaFolder),
            "create sparse quota fixture");
    const QString sparseOld=QDir(quotaFolder).filePath(
        QStringLiteral("20000101T000000000-oversized.kra"));
    {
        QFile large(sparseOld);
        require(large.open(QIODevice::WriteOnly) &&
                large.write(QByteArray::fromHex("504b0304"))==4,
                "create sparse quota archive");
        if(large.resize(MobileSavedVersionArchive::maxBytes()+1)) {
            large.close();
            require(MobileSavedVersionArchive::snapshot(source,quotaRoot),
                    "oversized historical entry prevented new checkpoint");
            const auto quotaEntries=MobileSavedVersionArchive::list(
                source,quotaRoot);
            const QByteArray newestSource=load(source);
            bool validSaved=false;
            for(const auto &item:quotaEntries) {
                if(item.bytes==newestSource.size() &&
                   load(item.path)==newestSource)validSaved=true;
            }
            require(validSaved && !QFile::exists(sparseOld),
                    "retention failed to evict oversized older archive");
        } else large.close();
    }
    QFile::remove(sparseOld);

    // Hard Android process termination may leave recovery-copy KRAs.
    // The next gallery startup may remove only old generated copies,
    // never current recoveries, unrelated files, or saved sidecars.
    const QString orphanFolder=temp.filePath(QStringLiteral("orphan-copies"));
    require(QDir().mkpath(orphanFolder),"create orphan recovery folder");
    const QString expired=QDir(orphanFolder).filePath(
        QStringLiteral("Recovered-11111111-1111-4111-8111-111111111111.kra"));
    const QString fresh=QDir(orphanFolder).filePath(
        QStringLiteral("Recovered-22222222-2222-4222-8222-222222222222.kra"));
    const QString unrelated=QDir(orphanFolder).filePath(
        QStringLiteral("notes.kra"));
    const QString badName=QDir(orphanFolder).filePath(
        QStringLiteral("Recovered-not-a-uuid.kra"));
    const QString extendedName=QDir(orphanFolder).filePath(
        QStringLiteral("Recovered-33333333-3333-4333-8333-333333333333-extra.kra"));
    save(expired,initial);
    save(fresh,initial);
    save(unrelated,initial);
    save(badName,initial);
    save(extendedName,initial);
    QFile oldFile(expired);
    require(oldFile.open(QIODevice::ReadWrite) &&
            oldFile.setFileTime(QDateTime::currentDateTime().addDays(-3),
                                QFileDevice::FileModificationTime),
            "stage old orphan recovery file");
    oldFile.close();
    QFile malformedOld(extendedName);
    require(malformedOld.open(QIODevice::ReadWrite) &&
            malformedOld.setFileTime(QDateTime::currentDateTime().addDays(-3),
                                     QFileDevice::FileModificationTime),
            "stage old noncanonical recovery-name fixture");
    malformedOld.close();
    require(MobileSavedVersionArchive::cleanupStaleRecoveryCopies(
                orphanFolder)==1 &&
            !QFile::exists(expired) && QFile::exists(fresh) &&
            QFile::exists(unrelated) && QFile::exists(badName) &&
            QFile::exists(extendedName),
            "orphan recovery pruning removed a live or unrelated file");
    require(MobileSavedVersionArchive::cleanupStaleRecoveryCopies(
                orphanFolder)==0,"orphan pruning was not idempotent");

    // Streaming completed draft enumeration must retain every valid
    // published KRA in newest-first order. Recovery checkpoint lookup
    // depends on this ordering to stop at an older save boundary.
    QStringList orderedDrafts;
    for(int i=0;i<3;++i) {
        const QString path=MobileDraftRecovery::allocate(
            QStringLiteral("order-fixture"));
        require(!path.isEmpty(),"could not allocate completed draft fixture");
        save(path,QByteArray::fromHex("504b0304")+"completed");
        QFile file(path);
        require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(QDateTime::currentDateTime().addSecs(-3600*(3-i)),
                                 QFileDevice::FileModificationTime),
                "could not timestamp completed draft fixture");
        file.close();
        orderedDrafts.append(path);
    }
    const auto ordered=MobileDraftRecovery::list();
    int lastPosition=-1;
    for(int i=2;i>=0;--i) {
        int position=-1;
        for(int j=0;j<ordered.size();++j)
            if(ordered.at(j).absoluteFilePath()==orderedDrafts.at(i)) {
                position=j;
                break;
            }
        require(position>=0 && position>lastPosition,
                "streamed draft listing dropped a valid KRA or lost newest-first order");
        lastPosition=position;
    }
    for(const auto &path:orderedDrafts)
        QFile::remove(path);

    // Interrupted history retention can leave many archive files.
    // Streaming enumeration must keep every entry and match the old
    // descending filename order, irrespective of mutable mtime.
    const QString scanSource=temp.filePath(QStringLiteral("scan-history.kra"));
    const QString scanFolder=MobileSavedVersionArchive::folderFor(scanSource,store);
    require(QDir().mkpath(scanFolder),"create version enumeration folder");
    for(int i=0;i<80;++i) {
        const QString filename=QStringLiteral("20261114T%1-test.kra")
                                   .arg(i,6,10,QLatin1Char('0'));
        save(QDir(scanFolder).filePath(filename),initial);
    }
    const auto scanned=MobileSavedVersionArchive::list(scanSource,store);
    require(scanned.size()==80,"streamed archive scan dropped saved versions");
    for(int i=1;i<scanned.size();++i)
        require(QFileInfo(scanned.at(i-1).path).fileName()>
                    QFileInfo(scanned.at(i).path).fileName(),
                "streamed archive scan changed newest-first filename order");

    // An interrupted pruning pass may leave hundreds of completed
    // versions. The Version History UI must not retain or render every
    // old entry, while ordinary list() and source files remain intact.
    const QString browserSource=temp.filePath(QStringLiteral("browser-test.kra"));
    const QString browserFolder=MobileSavedVersionArchive::folderFor(browserSource,store);
    require(QDir().mkpath(browserFolder),"could not create browser-history fixture");
    for(int i=0;i<300;++i) {
        save(QDir(browserFolder).filePath(
                 QStringLiteral("20261114T%1-browser.kra")
                     .arg(i,6,10,QLatin1Char('0'))),initial);
    }
    const auto browserVersions=MobileSavedVersionArchive::listForBrowser(
        browserSource,store);
    require(browserVersions.size()==256 &&
            QFileInfo(browserVersions.first().path).fileName()==
                QStringLiteral("20261114T000299-browser.kra") &&
            QFileInfo(browserVersions.last().path).fileName()==
                QStringLiteral("20261114T000044-browser.kra") &&
            MobileSavedVersionArchive::list(browserSource,store).size()==300,
            "Version History browser did not bound newest-first results");
    require(QFile::exists(QDir(browserFolder).filePath(
                QStringLiteral("20261114T000000-browser.kra"))),
            "browser listing must not delete older recovery files");
    std::atomic_bool stopBrowser{true};
    require(MobileSavedVersionArchive::listForBrowser(
                browserSource,store,&stopBrowser).isEmpty() &&
            MobileSavedVersionArchive::list(browserSource,store).size()==300,
            "cancelled Version History scan must keep every KRA");
    stopBrowser.store(false);
    require(MobileSavedVersionArchive::listForBrowser(
                browserSource,store,&stopBrowser).size()==256,
            "Version History must recover after canceled scan");

    // Gallery listing should inspect only a bounded window of pending
    // staging files per refresh, yet eventually reach a recoverable
    // orphan behind many recent (still actively written) candidates.
    QStringList staged;
    for(int i=0;i<260;++i) {
        const QString candidate=MobileDraftRecovery::allocate(
            QStringLiteral("recent-staging"));
        require(!candidate.isEmpty(),"could not allocate staging fixture");
        const QString pending=candidate+QStringLiteral(".pending.kra");
        save(pending,"still-being-written");
        staged.append(pending);
    }
    const QString orphanDraft=MobileDraftRecovery::allocate(
        QStringLiteral("rotated-orphan"));
    require(!orphanDraft.isEmpty(),"could not allocate aged orphan");
    const QString oldPending=orphanDraft+QStringLiteral(".pending.kra");
    save(oldPending,QByteArray::fromHex("504b0304")+"recoverable");
    QFile aged(oldPending);
    require(aged.open(QIODevice::ReadWrite) &&
            aged.setFileTime(QDateTime::currentDateTime().addSecs(-600),
                             QFileDevice::FileModificationTime),
            "could not age orphan staging fixture");
    aged.close();
    bool rescued=false;
    for(int attempt=0;attempt<8 && !rescued;++attempt) {
        MobileDraftRecovery::list();
        rescued=!QFile::exists(oldPending);
    }
    require(rescued,"bounded orphan search failed to rotate across staged files");
    for(const auto &pending:staged)
        QFile::remove(pending);
    // The test must not leave a rescued private KRA in the app's draft
    // directory where later test runs could mistake it for user work.
    for(const auto &entry:MobileDraftRecovery::list()) {
        if(entry.completeBaseName().endsWith(QStringLiteral("_rotated-orphan")))
            QFile::remove(entry.absoluteFilePath());
    }

    // The scanner must not skip a block of old recovery candidates
    // after accepting the first two from a larger iterator window.
    // Every visit may validate at most two, but a bounded sequence
    // of visits must eventually reach all six without overwriting
    // the completed drafts.
    QStringList pendingBatch;
    for(int i=0;i<6;++i) {
        const QString draft=MobileDraftRecovery::allocate(
            QStringLiteral("orphan-batch-")+QString::number(i));
        require(!draft.isEmpty(),"could not allocate batched recovery fixture");
        const QString pending=draft+QStringLiteral(".pending.kra");
        save(pending,QByteArray::fromHex("504b0304")+"batch-recovery");
        QFile entry(pending);
        require(entry.open(QIODevice::ReadWrite) &&
                entry.setFileTime(QDateTime::currentDateTime().addSecs(-600),
                                  QFileDevice::FileModificationTime),
                "could not age batched orphan staging");
        entry.close();
        pendingBatch.append(pending);
    }
    bool allRescued=false;
    for(int visit=0;visit<10 && !allRescued;++visit) {
        MobileDraftRecovery::list();
        allRescued=true;
        for(const QString &pending:pendingBatch)
            if(QFile::exists(pending))allRescued=false;
    }
    require(allRescued,
            "orphan cursor skipped eligible drafts after its validation limit");
    // The rescue path creates unique final draft IDs, so clean up by
    // the controlled test-only title rather than assuming a target ID.
    for(const QFileInfo &entry:MobileDraftRecovery::list())
        if(entry.completeBaseName().contains(QStringLiteral("_orphan-batch-")))
            QFile::remove(entry.absoluteFilePath());

    // A heavily populated recovery directory must not allocate an
    // unbounded gallery snapshot. Keep the two newest entries by
    // modification time, retaining full discovery for recovery flows.
    QStringList galleryFixtures;
    for(int i=0;i<5;++i) {
        const QString draft=MobileDraftRecovery::allocate(
            QStringLiteral("bounded-gallery-")+QString::number(i));
        require(!draft.isEmpty(),"cannot allocate bounded gallery fixture");
        save(draft,QByteArray::fromHex("504b0304")+"gallery");
        QFile item(draft);
        require(item.open(QIODevice::ReadWrite) &&
                item.setFileTime(QDateTime::currentDateTime().addSecs(-100+i),
                                 QFileDevice::FileModificationTime),
                "cannot timestamp bounded gallery fixture");
        item.close();
        galleryFixtures.append(draft);
    }
    const auto fullDrafts=MobileDraftRecovery::list();
    const auto boundedDrafts=MobileDraftRecovery::list(2);
    require(fullDrafts.size()>=5 && boundedDrafts.size()==2 &&
            boundedDrafts.at(0).absoluteFilePath()==
                fullDrafts.at(0).absoluteFilePath() &&
            boundedDrafts.at(1).absoluteFilePath()==
                fullDrafts.at(1).absoluteFilePath(),
            "bounded recovery gallery did not retain the newest two drafts");
    // Checkpoint scans should retain precisely the subset newer than
    // the saved project's timestamp; the same cutoff must work when
    // combined with gallery-style bounded enumeration.
    const qint64 cutoff=QDateTime::currentDateTime().addSecs(-98)
                            .toMSecsSinceEpoch();
    QFileInfoList expectedNewer;
    for(const auto &entry:fullDrafts)
        if(entry.lastModified().toMSecsSinceEpoch()>cutoff)
            expectedNewer.append(entry);
    const auto filtered=MobileDraftRecovery::list(0,cutoff);
    const auto newestFiltered=MobileDraftRecovery::list(2,cutoff);
    require(filtered.size()==expectedNewer.size(),
            "checkpoint cutoff changed the number of recoverable drafts");
    for(int i=0;i<filtered.size();++i)
        require(filtered.at(i).absoluteFilePath()==
                    expectedNewer.at(i).absoluteFilePath(),
                "checkpoint cutoff changed newest-first recovery ordering");
    require(newestFiltered.size()==qMin(2,filtered.size()),
            "combined time and count limits returned an incorrect size");
    for(int i=0;i<newestFiltered.size();++i)
        require(newestFiltered.at(i).absoluteFilePath()==
                    filtered.at(i).absoluteFilePath(),
                "bounded checkpoint cutoff lost a newer recovery draft");
    for(const QString &draft:galleryFixtures)QFile::remove(draft);

    // Exercise bounded enumeration under an out-of-order directory,
    // rather than only five newest-first fixture writes. A heap with an
    // inverted comparator can appear correct for a tiny input while
    // silently excluding the newest checkpoint in a larger gallery.
    QStringList stressDrafts;
    // Keep these fixtures newer than any unrelated app-data drafts so
    // the assertions necessarily exercise our 192 generated entries.
    const QDateTime stressBase=QDateTime::currentDateTime().addDays(1);
    constexpr int stressCount=192;
    for(int i=0;i<stressCount;++i) {
        const QString draft=MobileDraftRecovery::allocate(
            QStringLiteral("bounded-stress-")+QString::number(i));
        require(!draft.isEmpty(),"cannot allocate bounded stress fixture");
        save(draft,QByteArray::fromHex("504b0304")+"stress");
        QFile item(draft);
        // Pseudorandom timestamp order, but deterministic across runs.
        const int rank=(i*73)%stressCount;
        require(item.open(QIODevice::ReadWrite) &&
                item.setFileTime(stressBase.addSecs(rank*5),
                                 QFileDevice::FileModificationTime),
                "cannot timestamp bounded stress fixture");
        item.close();
        stressDrafts.append(draft);
    }
    // Project recovery must retain only the matching newer checkpoints
    // while streaming a directory of unrelated drafts. Preserve both
    // newest-first ordering and the saved-project modification cutoff.
    const QString targetedProject=temp.filePath(
        QStringLiteral("streamed-checkpoints.kra"));
    const QString unrelatedProject=temp.filePath(
        QStringLiteral("unrelated-checkpoints.kra"));
    const QString olderMatch=stressDrafts.at(1);
    const QString newerMatch=stressDrafts.at(2);
    const QString unrelatedMatch=stressDrafts.at(3);
    require(MobileDraftRecovery::setProject(olderMatch,targetedProject) &&
            MobileDraftRecovery::setProject(newerMatch,targetedProject) &&
            MobileDraftRecovery::setProject(unrelatedMatch,unrelatedProject),
            "could not associate streamed checkpoint fixtures");
    const qint64 olderMs=QFileInfo(olderMatch).lastModified().toMSecsSinceEpoch();
    const qint64 newerMs=QFileInfo(newerMatch).lastModified().toMSecsSinceEpoch();
    const auto matching=MobileDraftRecovery::checkpointsFor(
        targetedProject,qMin(olderMs,newerMs)-1);
    const QString expectedNewest=olderMs>newerMs?olderMatch:newerMatch;
    const QString expectedOlder=olderMs>newerMs?newerMatch:olderMatch;
    require(matching.size()==2 &&
            matching.at(0).absoluteFilePath()==expectedNewest &&
            matching.at(1).absoluteFilePath()==expectedOlder,
            "streamed checkpoint scan lost ordering or included unrelated drafts");
    const auto newerOnly=MobileDraftRecovery::checkpointsFor(
        targetedProject,qMin(olderMs,newerMs));
    require(newerOnly.size()==1 &&
            newerOnly.first().absoluteFilePath()==expectedNewest,
            "streamed checkpoint cutoff returned older or unrelated drafts");
    for(const QString &associated:{olderMatch,newerMatch,unrelatedMatch})
        MobileDraftRecovery::setProject(associated,QString());
    const auto stressFull=MobileDraftRecovery::list();
    const auto stressBounded=MobileDraftRecovery::list(7);
    require(stressBounded.size()==7,
            "stress gallery did not obey its seven-item memory bound");
    for(int i=0;i<stressBounded.size();++i)
        require(stressBounded.at(i).absoluteFilePath()==
                    stressFull.at(i).absoluteFilePath(),
                "stress gallery lost a newest entry during bounded heap selection");
    const qint64 stressCutoff=stressBase.addSecs(150*5).toMSecsSinceEpoch();
    const auto stressFiltered=MobileDraftRecovery::list(0,stressCutoff);
    const auto stressBoundedFiltered=MobileDraftRecovery::list(7,stressCutoff);
    require(stressBoundedFiltered.size()==7,
            "stress cutoff unexpectedly lost the newest draft entries");
    for(int i=0;i<stressBoundedFiltered.size();++i)
        require(stressBoundedFiltered.at(i).absoluteFilePath()==
                    stressFiltered.at(i).absoluteFilePath(),
                "stress cutoff and heap produced inconsistent newest-first order");
    // Project deletion checks must find an associated draft without
    // constructing the complete sorted recovery list. Choose the first
    // generated draft, whose timestamp is deliberately not the newest.
    const QString associationProject=temp.filePath(
        QStringLiteral("association-existence.kra"));
    require(MobileDraftRecovery::setProject(
                stressDrafts.first(),associationProject) &&
            MobileDraftRecovery::hasProjectDraft(associationProject),
            "streamed recovery association scan missed an older draft");
    require(MobileDraftRecovery::setProject(stressDrafts.first(),QString()) &&
            !MobileDraftRecovery::hasProjectDraft(associationProject),
            "streamed recovery association scan retained removed metadata");
    for(const QString &draft:stressDrafts)QFile::remove(draft);

    // Existence checks for project deletion must still rescue complete
    // interrupted exports, even though they no longer build list(1).
    const QString rescueAssociation=temp.filePath(
        QStringLiteral("streamed-rescue-ownership.kra"));
    const QString unfinished=MobileDraftRecovery::allocate(
        QStringLiteral("association-rescue-test"));
    require(!unfinished.isEmpty() &&
            MobileDraftRecovery::setProject(unfinished,rescueAssociation),
            "failed to associate interrupted recovery fixture");
    const QString staged=unfinished+QStringLiteral(".pending.kra");
    save(staged,QByteArray::fromHex("504b0304")+"rescue-association");
    QFile stagedEntry(staged);
    require(stagedEntry.open(QIODevice::ReadWrite) &&
            stagedEntry.setFileTime(QDateTime::currentDateTime().addSecs(-600),
                                    QFileDevice::FileModificationTime),
            "failed to age interrupted association fixture");
    stagedEntry.close();
    bool discovered=false;
    for(int visit=0;visit<12 && !discovered;++visit)
        discovered=MobileDraftRecovery::hasProjectDraft(rescueAssociation);
    require(discovered && !QFile::exists(staged),
            "streamed association check skipped interrupted export rescue");
    for(const auto &draft:MobileDraftRecovery::list()) {
        if(MobileDraftRecovery::projectFor(draft.absoluteFilePath())==
           rescueAssociation) {
            QFile::remove(draft.absoluteFilePath());
            QFile::remove(draft.absoluteFilePath()+QStringLiteral(".project"));
        }
    }
    QFile::remove(unfinished+QStringLiteral(".project"));
    require(!MobileDraftRecovery::hasProjectDraft(rescueAssociation),
            "rescued association remained after fixture cleanup");

    // A project association is limited to 16 KiB on write. A damaged
    // or concurrently growing sidecar must never trigger a readAll()
    // allocation proportional to its eventual size.
    const QString boundedAssociation=MobileDraftRecovery::allocate(
        QStringLiteral("bounded-association"));
    require(!boundedAssociation.isEmpty(),
            "cannot allocate bounded association fixture");
    const QString metadata=boundedAssociation+QStringLiteral(".project");
    const QString safeAssociation=temp.filePath(
        QStringLiteral("bounded-project.kra"));
    require(MobileDraftRecovery::setProject(boundedAssociation,safeAssociation) &&
            MobileDraftRecovery::projectFor(boundedAssociation)==safeAssociation,
            "bounded recovery association failed normal round trip");
    save(metadata,QByteArray(16385,'x'));
    require(MobileDraftRecovery::projectFor(boundedAssociation).isEmpty(),
            "oversized recovery association metadata was accepted");
    // Writer and reader must agree at the exact 16 KiB boundary.
    const QString maxAssociation(16384,QLatin1Char('a'));
    save(metadata,maxAssociation.toUtf8());
    require(MobileDraftRecovery::projectFor(boundedAssociation)==maxAssociation,
            "valid maximum-length association metadata was rejected");
    QFile::remove(metadata);

    // Long-running background saves must not trigger a full pending
    // checkpoint scan every two seconds indefinitely. Completed-save
    // signals can still wake the runtime immediately; this is the
    // bounded watchdog path only.
    const int retryDelays[]={2000,4000,8000,16000,16000};
    int retryAttempt=0;
    for(int delay:retryDelays) {
        require(MobilePendingDraftRetryPolicy::timeoutMs(retryAttempt)==delay,
                "pending draft backoff delay was not bounded");
        retryAttempt=MobilePendingDraftRetryPolicy::afterTimeoutScheduled(
            retryAttempt);
    }
    require(retryAttempt==3 &&
            MobilePendingDraftRetryPolicy::timeoutMs(-100)==2000 &&
            MobilePendingDraftRetryPolicy::timeoutMs(1000000)==16000 &&
            MobilePendingDraftRetryPolicy::afterTimeoutScheduled(1000000)==3 &&
            MobilePendingDraftRetryPolicy::afterTimeoutScheduled(-1)==1 &&
            MobilePendingDraftRetryPolicy::timeoutMs(0)==2000,
            "pending draft retries did not clamp/reset their backoff");

    qInfo() << "PASS: snapshot, dedup, atomic recovery, retention,"
               " provider identity, SAF tree normalization and legacy migration";
    return 0;
}
