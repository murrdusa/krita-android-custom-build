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
    require(!MobileSavedVersionArchive::folderFor(
        QStringLiteral("content://provider/project.kra"),store).size(),
        "Android content URI must not be mistaken for filesystem storage");
    require(!MobileSavedVersionArchive::snapshot(
        QStringLiteral("content://provider/project.kra"),store),
        "SAF snapshot attempt should not silently succeed");
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
    qInfo() << "PASS: snapshot, dedup, atomic recovery, retention,"
               " SAF rejection and original-file preservation";
    return 0;
}
