/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Touch-first Krita document launcher and local project index.
 * Keep existing native document and provider access behavior.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QtAndroid>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScopedPointer>
#include <QSaveFile>
#include <QScrollBar>
#include "touch_scroll.h"
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <KoStore.h>
#include <KisRecentFilesManager.h>
#include <klocalizedstring.h>
#include <algorithm>
#include <atomic>
#include <memory>

#include "document_workflow.h"
#include "mobile_draft_recovery.h"
#include "saved_version_archive.h"

// ---------------------------------------------------------------------------
// Persistent project index — survives app restarts so All Projects loads
// instantly without blocking on a full filesystem scan each time.
//
// Stored as JSON in AppDataLocation/project-index.json.
// Thumbnails cached as PNG files in AppDataLocation/project-thumbs/.
// Entries merge by canonical SAF identity. A provider file may be
// temporarily unavailable, so keep its location until explicitly deleted.
// ---------------------------------------------------------------------------
struct MobileProjectIndexEntry {
    QString path;        // local path or content:// URI (canonical)
    QString title;
    qint64  modified=0; // msecs since epoch; 0 = unknown
    QString source;     // "recent" | "scan" | "saf"
};

class MobileProjectIndex {
public:
    static QString indexPath() {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/project-index.json");
    }
    static QString thumbDir() {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/project-thumbs");
    }
    static QString thumbPath(const QString &path) {
        const QByteArray key=QCryptographicHash::hash(
            path.toUtf8(),QCryptographicHash::Sha256).toHex();
        return thumbDir()+QLatin1Char('/')+QString::fromLatin1(key)+QStringLiteral(".png");
    }

    static QVector<MobileProjectIndexEntry> load() {
        QVector<MobileProjectIndexEntry> entries;
        QFile f(indexPath());
        if(!f.open(QIODevice::ReadOnly))return entries;
        const auto doc=QJsonDocument::fromJson(f.readAll());
        if(doc.isNull()||!doc.isObject())return entries;
        const auto arr=doc.object().value(QStringLiteral("entries")).toArray();
        entries.reserve(arr.size());
        for(const auto &v:arr) {
            const auto o=v.toObject();
            const QString p=o.value(QStringLiteral("path")).toString();
            if(p.isEmpty())continue;
            MobileProjectIndexEntry e;
            e.path=p;
            e.title=o.value(QStringLiteral("title")).toString();
            e.modified=qint64(o.value(QStringLiteral("modified")).toDouble());
            e.source=o.value(QStringLiteral("source")).toString();
            entries.append(e);
        }
        return entries;
    }

    static void save(const QVector<MobileProjectIndexEntry> &entries) {
        const QString path=indexPath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonArray arr;
        for(const auto &e:entries) {
            QJsonObject o;
            o.insert(QStringLiteral("path"),e.path);
            o.insert(QStringLiteral("title"),e.title);
            o.insert(QStringLiteral("modified"),double(e.modified));
            o.insert(QStringLiteral("source"),e.source);
            arr.append(o);
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"),1);
        root.insert(QStringLiteral("entries"),arr);
        // QSaveFile commits by atomic replacement; never delete the last
        // good index before its replacement is durably written.
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly))return;
        const QByteArray bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
        if(file.write(bytes)!=bytes.size()) {
            file.cancelWriting();
            return;
        }
        file.commit();
    }

    static QString key(const QString &path)
    {
        const auto identity=MobileSavedVersionArchive::canonicalIdentity(path);
        return identity.isEmpty()?path:identity;
    }

    // Merge incoming entries into existing, dedup by canonical identity.
    // Incoming entries update timestamp and source of existing ones.
    static QVector<MobileProjectIndexEntry> merge(
        QVector<MobileProjectIndexEntry> existing,
        const QVector<MobileProjectIndexEntry> &incoming)
    {
        QHash<QString,int> byId;
        for(int i=0;i<existing.size();++i) {
            byId.insert(key(existing[i].path),i);
        }
        for(const auto &e:incoming) {
            const QString id=key(e.path);
            auto it=byId.find(id);
            if(it==byId.end()) {
                byId.insert(id,existing.size());
                existing.append(e);
            } else {
                auto &ex=existing[it.value()];
                if(e.modified>0)ex.modified=e.modified;
                if(!e.title.isEmpty())ex.title=e.title;
                ex.path=e.path;
                ex.source=e.source;
            }
        }
        return existing;
    }

    // Load a cached thumbnail from disk. Returns null image if not cached
    // or if the entry's modification time has changed (stale).
    static QImage loadThumb(const QString &path, qint64 modified) {
        const QString tp=thumbPath(path);
        const QFileInfo ti(tp);
        if(!ti.isReadable())return {};
        // The thumb file's mtime is set to match the project's mtime.
        // If they differ, the thumb is stale.
        if(modified>0 && ti.lastModified().toMSecsSinceEpoch()!=modified)return {};
        // Android SAF providers sometimes report no modification time.
        // Give those thumbnail entries a finite lifetime rather than
        // showing an obsolete picture forever.
        if(modified==0 && ti.lastModified().secsTo(
                QDateTime::currentDateTime())>30*60)return {};
        QImageReader reader(tp,"png");
        return reader.read();
    }

    // Save a thumbnail to disk and stamp its mtime to match the project.
    static void saveThumb(const QString &path, qint64 modified, const QImage &image) {
        if(image.isNull())return;
        const QString dir=thumbDir();
        QDir().mkpath(dir);
        const QString tp=thumbPath(path);
        QSaveFile output(tp);
        if(!output.open(QIODevice::WriteOnly))return;
        if(!image.save(&output,"PNG") || !output.flush()) {
            output.cancelWriting();
            return;
        }
        if(modified>0)
            output.setFileTime(QDateTime::fromMSecsSinceEpoch(modified),
                               QFileDevice::FileModificationTime);
        output.commit();
    }
};

class MobileDocumentHome final : public QFrame
{
public:
    MobileDocumentHome(const MobileEngineBridge &engine,
                       QSharedPointer<MobileDocumentHostPort> files,
                       QWidget *parent)
        : QFrame(parent),m_engine(engine),m_files(std::move(files))
    {
        m_scanner.setMaxThreadCount(1);
        m_thumbnails.setMaxThreadCount(2);
        setObjectName(QStringLiteral("mobileDocumentHome"));
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet(QStringLiteral(
            "QFrame#mobileDocumentHome{background:#252529;color:#f4f4f5;}"
            "QLabel{color:#f4f4f5;}"
            "QPushButton{background:#36363c;color:#f4f4f5;"
            "border:1px solid #56565d;border-radius:8px;"
            "min-height:44px;padding:4px;}"
            "QPushButton:checked,QListWidget::item:selected{background:#505061;}"
            "QListWidget{background:#29292e;color:#f4f4f5;border:0;}"
            "QListWidget::item{min-height:44px;padding:5px;}"
            "QLineEdit{background:#34343a;color:#f4f4f5;"
            "border:1px solid #777;border-radius:7px;min-height:40px;"
            "padding:4px 10px;}"));
        auto *layout=new QVBoxLayout(this);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(7);
        auto *title=new QLabel(i18n("Krita Mobile Studio"),this);
        title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:600;"));
        layout->addWidget(title);

        auto *fileRow=new QHBoxLayout;
        auto *create=new QPushButton(i18n("New Canvas"),this);
        auto *browse=new QPushButton(i18n("Browse Files"),this);
        fileRow->addWidget(create);
        fileRow->addWidget(browse);
        layout->addLayout(fileRow);

        auto *tabs=new QHBoxLayout;
        m_recent=new QPushButton(i18n("Recent"),this);
        m_library=new QPushButton(i18n("All Projects"),this);
        for(QPushButton *button:{m_recent,m_library}) {
            button->setCheckable(true);
            tabs->addWidget(button);
        }
        layout->addLayout(tabs);
        m_drafts=new QPushButton(i18n("Recover drafts"),this);
        m_drafts->setCheckable(true);
        layout->addWidget(m_drafts);

        auto *tools=new QHBoxLayout;
        m_search=new QLineEdit(this);
        m_search->setPlaceholderText(i18n("Search Krita documents"));
        m_search->setClearButtonEnabled(true);
        m_refresh=new QPushButton(i18n("Refresh"),this);
        m_refresh->setMinimumWidth(76);
        tools->addWidget(m_search,1);
        tools->addWidget(m_refresh);
        m_sort=new QPushButton(this);
        QSettings settings;
        const int previousSort=settings.value(
            QStringLiteral("MobileStudio/projectSortByName"),false).toBool()?Name:Newest;
        m_sortOrder=SortOrder(qBound(int(Newest),settings.value(
            QStringLiteral("MobileStudio/projectSortOrder"),previousSort).toInt(),int(Starred)));
        const auto favorites=settings.value(QStringLiteral("MobileStudio/starredProjects")).toStringList();
        for(const auto &identity:favorites)m_starred.insert(identity);
        auto *sortMenu=new QMenu(m_sort);
        sortMenu->setStyleSheet(QStringLiteral("QMenu::item{min-height:32px;padding:8px 14px;}"));
        for(const auto order:{Newest,Name,Starred}) {
            auto *choice=sortMenu->addAction(sortLabel(order));
            connect(choice,&QAction::triggered,this,[this,order]() {
                m_sortOrder=order;
                QSettings().setValue(QStringLiteral("MobileStudio/projectSortOrder"),int(order));
                m_sort->setText(sortLabel(order));
                sortProjects();
            });
        }
        m_sort->setMenu(sortMenu);
        m_sort->setText(sortLabel(m_sortOrder));
        tools->addWidget(m_sort);
        layout->addLayout(tools);

        m_access=new QPushButton(i18n("Allow file search"),this);
        layout->addWidget(m_access);
        m_message=new QLabel(this);
        m_message->setWordWrap(true);
        m_message->setStyleSheet(QStringLiteral("font-size:12px;color:#c3ccd7;"));
        layout->addWidget(m_message);

        m_list=new QListWidget(this);
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(124,124));
        m_list->setGridSize(QSize(148,164));
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setWordWrap(true);
        m_list->setSpacing(3);
        MobileTouchScroll::configureList(m_list);
        layout->addWidget(m_list,1);
        m_placeholder=createPlaceholder();

        connect(create,&QPushButton::clicked,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                if(isVisible() && MobileDocumentWorkflow::create(
                    m_engine,window()))hide();
            });
        });
        connect(browse,&QPushButton::clicked,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                if(isVisible() && m_engine.openFiles(window())>0)hide();
            });
        });
        connect(m_recent,&QPushButton::clicked,this,
                [this]() {showList(Recent);});
        connect(m_library,&QPushButton::clicked,this,
                [this]() {showList(AllProjects);});
        connect(m_drafts,&QPushButton::clicked,this,
                [this]() {showList(RecoveryDrafts);});
        connect(m_refresh,&QPushButton::clicked,this,[this]() {
            m_cache.clear();
            showList(m_mode,true); // explicit Refresh always scans
        });
        connect(KisRecentFilesManager::instance(),&KisRecentFilesManager::fileAdded,
                this,[this](const QUrl &url) {
            const auto path=url.isLocalFile()?url.toLocalFile():url.toString();
            if(url.isLocalFile() && !isKra(path))return;
            // Persist new locations even if the user never visits All Projects.
            m_scanner.start([path]() {
                const MobileProjectIndexEntry entry{
                    path,MobileSavedVersionArchive::projectName(path),
                    MobileSavedVersionArchive::projectModified(path),
                    QStringLiteral("recent")};
                MobileProjectIndex::save(MobileProjectIndex::merge(
                    MobileProjectIndex::load(),{entry}));
            });
        });
        connect(m_search,&QLineEdit::textChanged,this,
                [this](const QString &) {filterItems();});
        connect(m_access,&QPushButton::clicked,this,[this]() {requestSearchAccess();});
        connect(m_list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item) {
            if(!item || !item->flags().testFlag(Qt::ItemIsEnabled))return;
            const QString path=item->data(Qt::UserRole).toString();
            const QString title=item->data(Qt::UserRole+3).toString();
            const Mode mode=m_mode;
            QTimer::singleShot(0,this,[this,path,title,mode]() {
                if(isVisible() && m_mode==mode && !path.isEmpty())
                    projectActions(path,title,mode==RecoveryDrafts);
            });
        });
        connect(m_list->verticalScrollBar(),&QScrollBar::valueChanged,
                this,[this](int) {scheduleThumbnails();});
        connect(qApp,&QGuiApplication::applicationStateChanged,
                this,[this](Qt::ApplicationState state) {
            if(state!=Qt::ApplicationActive || !isVisible())return;
            const bool access=hasAllFilesAccess();
            if(access==m_lastAccess)return;
            QSettings().remove(QStringLiteral("MobileStudio/projectIndexSharedStorageScanMs"));
            if(access || m_mode==AllProjects)showList(AllProjects,true);
            else m_lastAccess=access;
        });
        parent->installEventFilter(this);
        hide();
    }

    ~MobileDocumentHome() override
    {
        if(m_scanCancel)m_scanCancel->store(true);
        m_scanner.waitForDone();
        m_thumbnails.waitForDone();
    }

    void showHome()
    {
        showList(m_mode==AllProjects?AllProjects:Recent);
        setGeometry(parentWidget()->rect());
        show();
        raise();
        scheduleThumbnails();
        const QString key=QStringLiteral("MobileStudio/askedForProjectSearch");
        QSettings settings;
        if(!hasAllFilesAccess() && !settings.value(key,false).toBool()) {
            settings.setValue(key,true);
            QTimer::singleShot(0,this,[this]() {
                if(isVisible() && !hasAllFilesAccess())requestSearchAccess();
            });
        }
    }

    bool restoreSingleDraft()
    {
        const auto drafts=MobileDraftRecovery::list();
        if(drafts.size()!=1 || !m_files)return false;
        const auto &draft=drafts.first();
        if(!MobileDraftRecovery::projectFor(draft.absoluteFilePath()).isEmpty())return false;
        const QString attempt=draft.fileName()+QLatin1Char(':')+
            QString::number(draft.lastModified().toMSecsSinceEpoch());
        const QString key=QStringLiteral("MobileStudio/lastAutomaticDraft");
        QSettings settings;
        if(settings.value(key).toString()==attempt)return false;
        // Persist before opening. If a damaged draft terminates the app,
        // the next launch still reaches Home and offers manual recovery.
        settings.setValue(key,attempt);
        settings.sync();
        return settings.status()==QSettings::NoError &&
               m_files->openFile(draft.absoluteFilePath());
    }

    void showSavedVersions(const QString &path)
    {
        if(m_restoringVersion || !m_files)return;
        const auto versions=MobileSavedVersionArchive::list(path);
        if(versions.isEmpty()) {
            QMessageBox::information(window(),i18n("Saved versions"),
                i18n("Earlier saved copies and completed draft checkpoints "
                     "will appear here."));
            return;
        }
        QDialog dialog(window());
        dialog.setWindowTitle(i18n("Saved versions"));
        dialog.setMinimumWidth(qMin(300,qMax(1,window()->width()-24)));
        auto *layout=new QVBoxLayout(&dialog);
        auto *explanation=new QLabel(i18n(
            "Open a saved copy as a new canvas. Use Save As to keep it."),&dialog);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:44px;padding:5px;}"));
        MobileTouchScroll::configureList(list);
        for(const auto &version:versions) {
            new QListWidgetItem(i18n("%1  •  %2 MB",
                version.createdUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm")),
                QString::number(version.bytes/(1024.0*1024.0),'f',1)),list);
        }
        list->setCurrentRow(0);
        layout->addWidget(list,1);
        auto *buttons=new QDialogButtonBox(
            QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog);
        buttons->setStyleSheet(QStringLiteral("QPushButton{min-height:44px;}"));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);
        if(dialog.exec()!=QDialog::Accepted || list->currentRow()<0)return;

        openCopy(versions.at(list->currentRow()));
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parentWidget() && event->type()==QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            scheduleThumbnails();
        }
        return QFrame::eventFilter(watched,event);
    }

private:
    enum Mode {Recent,AllProjects,RecoveryDrafts};
    enum SortOrder {Newest,Name,Starred};
    struct Project {
        QString path;
        QString title;
        qint64 timestamp=0;
        QString folder;
    };

    static QString sortLabel(SortOrder order)
    {
        switch(order) {
        case Newest:return i18n("Newest");
        case Name:return i18n("Name");
        case Starred:return i18n("Starred");
        }
        return {};
    }

    static QString projectKey(const QString &path)
    {
        return MobileProjectIndex::key(path);
    }

    void requestSearchAccess()
    {
        if(hasAllFilesAccess())return;
        if(QMessageBox::question(this,i18n("Search for projects?"),
            i18n("Allow Android file access to find Krita projects in shared storage. "
                 "You can also open individual documents with Browse Files."),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes)return;
        QAndroidJniObject::callStaticMethod<void>(
            "org/krita/android/MainActivity","tabletRequestAllFilesAccess","()V");
    }

    void projectActions(const QString &path,const QString &title,bool draft)
    {
        if(m_restoringVersion)return;
        QDialog dialog(window());
        dialog.setWindowTitle(title);
        auto *layout=new QVBoxLayout(&dialog);
        enum Action {Open=1,Duplicate,History,Star,Delete};
        const auto add=[&](const QString &label,Action action) {
            auto *button=new QPushButton(label,&dialog);
            button->setMinimumHeight(44);
            layout->addWidget(button);
            connect(button,&QPushButton::clicked,&dialog,[&dialog,action]() {dialog.done(action);});
        };
        const QString key=projectKey(path);
        add(i18n("Open"),Open);
        if(!draft && isKra(title))add(i18n("Duplicate"),Duplicate);
        add(i18n("Version history"),History);
        if(!draft)add(m_starred.contains(key)?i18n("Remove star"):i18n("Star project"),Star);
        add(draft?i18n("Discard draft…"):i18n("Delete…"),Delete);
        auto *cancel=new QPushButton(i18n("Cancel"),&dialog);
        cancel->setMinimumHeight(44);
        layout->addWidget(cancel);
        connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
        const int chosen=dialog.exec();
        if(!isVisible())return;
        const QString draftProject=draft?MobileDraftRecovery::projectFor(path):QString();
        switch(chosen) {
        case Open:
            if(!draftProject.isEmpty()) {
                MobileSavedVersionArchive::Version source;
                source.path=path;
                openCopy(source);
                break;
            }
            if(m_files && m_files->openFile(path))hide();
            else QMessageBox::warning(this,i18n("Drawing not opened"),
                i18n("Could not open this drawing. Browse Files can renew Android file access."));
            break;
        case Duplicate: {
            MobileSavedVersionArchive::Version source;
            const auto *item=m_projectItems.value(key);
            source.path=item?item->data(Qt::UserRole+4).toString():path;
            openCopy(source);
            break;
        }
        case History:showSavedVersions(draftProject.isEmpty()?path:draftProject);break;
        case Star:
            if(!m_starred.remove(key))m_starred.insert(key);
            saveStars();
            if(auto *item=m_projectItems.value(key))updateProjectLabel(item);
            if(m_mode==AllProjects && m_sortOrder==Starred)sortProjects();
            break;
        case Delete:
            if(draft)discardDraft(path);
            else deleteProject(path,title);
            break;
        }
    }

    void openCopy(const MobileSavedVersionArchive::Version &version)
    {
        m_restoringVersion=true;
        const auto views=m_engine.openDocuments();
        const QPointer<QObject> expected=views.isEmpty()?QPointer<QObject>():views.first().nativeView;
        const bool hadCanvas=bool(expected);
        QPointer<MobileDocumentHome> home(this);
        QThreadPool::globalInstance()->start([home,version,expected,hadCanvas]() {
            const QString copy=MobileSavedVersionArchive::materializeRecovery(version);
            QMetaObject::invokeMethod(qApp,[home,copy,expected,hadCanvas]() {
                const auto current=home?home->m_engine.openDocuments():
                    QVector<MobileEngineBridge::OpenDocument>{};
                const bool sameCanvas=hadCanvas?
                    expected && current.size()==1 && current.first().nativeView==expected:
                    current.isEmpty();
                const bool opened=home && sameCanvas && !copy.isEmpty() && home->m_files &&
                    home->m_files->openFile(copy);
                if(!copy.isEmpty())QFile::remove(copy);
                if(!home)return;
                home->m_restoringVersion=false;
                if(opened)home->hide();
                else if(sameCanvas)QMessageBox::warning(home->window(),i18n("Drawing not opened"),
                    copy.isEmpty()?
                        i18n("This copy could not be recovered. Its archive may be damaged or unreadable. The original drawing and other versions were not changed."):
                        i18n("Could not open the project copy."));
            },Qt::QueuedConnection);
        });
    }

    void saveStars() const
    {
        QStringList identities;
        for(const auto &identity:m_starred)identities.append(identity);
        QSettings().setValue(QStringLiteral("MobileStudio/starredProjects"),identities);
    }

    void deleteProject(const QString &path,const QString &title)
    {
        const QString key=projectKey(path);
        if(QMessageBox::question(this,i18n("Delete drawing?"),
            i18n("Permanently delete %1 from storage?",title),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes ||
            !isVisible())return;
        for(const auto &entry:m_engine.openDocuments()) {
            const auto *document=qobject_cast<KisDocument*>(entry.document.data());
            if(document && projectKey(document->path())==key)return;
        }
        const bool provider=path.startsWith(QStringLiteral("content://"));
        const auto uri=QAndroidJniObject::fromString(path);
        const bool removed=provider?QAndroidJniObject::callStaticMethod<jboolean>(
            "org/krita/android/MainActivity","tabletDeleteProject","(Ljava/lang/String;)Z",
            uri.object<jstring>()):QFile::remove(path);
        if(!removed) {
            QMessageBox::warning(this,i18n("Could not delete drawing"),
                i18n("Android or the file provider did not allow deletion."));
            return;
        }
        auto *recent=KisRecentFilesManager::instance();
        for(const auto &entry:recent->recentFiles()) {
            const QString candidate=entry.m_url.isLocalFile()?
                entry.m_url.toLocalFile():entry.m_url.toString();
            if(projectKey(candidate)==key)recent->remove(entry.m_url);
        }
        if(m_scanCancel)m_scanCancel->store(true);
        m_starred.remove(key);
        saveStars();
        m_cache.remove(path);
        if(auto *item=m_projectItems.take(key))delete item;
        // Serialize removal after any queued index save.
        m_scanner.start([key]() {
            auto entries=MobileProjectIndex::load();
            entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const auto &entry) {
                return projectKey(entry.path)==key;
            }),entries.end());
            MobileProjectIndex::save(entries);
        });
        m_message->setText(i18n("Drawing deleted."));
    }

    void updateProjectLabel(QListWidgetItem *item) const
    {
        const QString title=item->data(Qt::UserRole+3).toString();
        const bool starred=m_starred.contains(projectKey(item->data(Qt::UserRole).toString()));
        item->setText(starred?QStringLiteral("★ ")+title:title);
    }

    bool draftIsOpen(const QString &path) const
    {
        for(const auto &entry:m_engine.openDocuments()) {
            if(entry.document &&
               entry.document->property("_mobileDraftPath").toString()==path)
                return true;
        }
        return false;
    }

    void discardDraft(const QString &path)
    {
        if(!isVisible() || m_mode!=RecoveryDrafts ||
           !MobileDraftRecovery::isDraft(path))return;
        if(draftIsOpen(path)) {
            QMessageBox::information(this,i18n("Draft is open"),
                i18n("Close the drawing without saving to discard its draft."));
            return;
        }
        if(QMessageBox::question(this,i18n("Discard draft?"),
            i18n("Discard the unsaved changes in %1?",
                 MobileDraftRecovery::title(QFileInfo(path))),
            QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)
                !=QMessageBox::Discard)return;
        if(draftIsOpen(path) || !MobileDraftRecovery::isDraft(path))return;
        if(!QFile::remove(path)) {
            QMessageBox::warning(this,i18n("Could not discard draft"),
                i18n("The recovery file could not be removed."));
            return;
        }
        // The document is no longer open, so any interrupted staging
        // file belongs to this discarded draft. Do not leave a stale
        // .pending.kra consuming space or a project association behind.
        QFile::remove(path+QStringLiteral(".pending.kra"));
        QFile::remove(path+QStringLiteral(".project"));
        m_cache.remove(path);
        showList(RecoveryDrafts);
    }

    static bool isKra(const QString &path)
    {
        return MobileSavedVersionArchive::isKritaProjectName(QFileInfo(path).fileName());
    }

    static bool hasAllFilesAccess()
    {
        return QAndroidJniObject::callStaticMethod<jboolean>(
            "org/krita/android/MainActivity",
            "tabletHasAllFilesAccess","()Z");
    }

    static QIcon createPlaceholder()
    {
        QPixmap pix(120,120);
        pix.fill(QColor(48,55,65));
        QPainter painter(&pix);
        painter.setPen(QColor(177,193,207));
        painter.setBrush(QColor(68,79,94));
        painter.drawRoundedRect(QRect(22,13,76,92),8,8);
        painter.setPen(QColor(236,242,247));
        painter.drawText(QRect(22,13,76,92),Qt::AlignCenter,
                         QStringLiteral("KRA"));
        return QIcon(pix);
    }

    static QImage decodePreview(const QByteArray &bytes)
    {
        if(bytes.isEmpty() || bytes.size()>8*1024*1024)return {};
        QBuffer buffer;
        buffer.setData(bytes);
        if(!buffer.open(QIODevice::ReadOnly))return {};
        QImageReader reader(&buffer,"png");
        const QSize original=reader.size();
        if(!original.isValid() || original.width()>8192 ||
           original.height()>8192)return {};
        reader.setScaledSize(original.scaled(124,124,Qt::KeepAspectRatio));
        return reader.read();
    }

    static QImage preview(const QString &path)
    {
        if(path.startsWith(QStringLiteral("content:"),Qt::CaseInsensitive)) {
            // Only Android's ContentResolver can access documents opened
            // with SAF. Read just the small embedded KRA PNG preview.
            const QAndroidJniObject uri=QAndroidJniObject::fromString(path);
            const auto data=QAndroidJniObject::callStaticObjectMethod(
                "org/krita/android/MainActivity","tabletKraPreview",
                "(Ljava/lang/String;)[B",uri.object<jstring>());
            if(!data.isValid())return {};
            QAndroidJniEnvironment env;
            const jbyteArray array=data.object<jbyteArray>();
            const jsize size=env->GetArrayLength(array);
            if(size<=0 || size>8*1024*1024)return {};
            QByteArray bytes(size,'\0');
            env->GetByteArrayRegion(array,0,size,
                                    reinterpret_cast<jbyte*>(bytes.data()));
            return decodePreview(bytes);
        }
        if(!QFileInfo(path).isReadable())return {};
        if(!isKra(path)) {
            // Imported PNG/JPEG drawings are also part of Krita's recent
            // history. Read them with the native Qt image decoder.
            QImageReader reader(path);
            const QSize size=reader.size();
            if(!size.isValid() || size.width()>8192 || size.height()>8192)
                return {};
            reader.setScaledSize(size.scaled(124,124,Qt::KeepAspectRatio));
            return reader.read();
        }
        QScopedPointer<KoStore> store(KoStore::createStore(path,KoStore::Read));
        if(!store || store->bad())return {};
        for(const QString &part:{QStringLiteral("preview.png"),
                                 QStringLiteral("Thumbnails/thumbnail.png")}) {
            if(!store->hasFile(part) || !store->open(part))continue;
            const qint64 size=store->size();
            if(size<=0 || size>8LL*1024*1024) {
                store->close();
                continue;
            }
            const QByteArray bytes=store->read(size);
            store->close();
            const QImage image=decodePreview(bytes);
            if(!image.isNull())return image;
        }
        return {};
    }

    void showList(Mode mode,bool forceScan=false)
    {
        if(m_mode==AllProjects && m_list->count()>0)persistIndex();
        m_mode=mode;
        m_forceThumbnailRefresh=forceScan;
        if(forceScan)m_cache.clear();
        m_recent->setChecked(mode==Recent);
        m_library->setChecked(mode==AllProjects);
        const auto drafts=MobileDraftRecovery::list();
        m_latestDrafts.clear();
        for(const auto &draft:drafts) {
            const auto path=MobileDraftRecovery::projectFor(draft.absoluteFilePath());
            if(!path.isEmpty() && !m_latestDrafts.contains(projectKey(path)))
                m_latestDrafts.insert(projectKey(path),draft);
        }
        m_drafts->setChecked(mode==RecoveryDrafts);
        m_drafts->setVisible(mode==RecoveryDrafts || !drafts.isEmpty());
        m_refresh->setVisible(mode==AllProjects);
        m_lastAccess=hasAllFilesAccess();
        m_access->setVisible(mode==AllProjects && !m_lastAccess);
        m_sort->setVisible(mode==AllProjects);
        m_projectItems.clear();
        m_list->clear();
        ++m_previewGeneration;
        m_thumbnails.clear();
        m_pending.clear();
        m_attempted.clear();
        if(m_scanCancel)m_scanCancel->store(true);
        if(mode==Recent) {
            m_message->setText(i18n("Recently opened drawings"));
            QHash<QString,MobileProjectIndexEntry> indexed;
            for(const auto &entry:MobileProjectIndex::load())
                indexed.insert(projectKey(entry.path),entry);
            const auto recent=KisRecentFilesManager::instance()->recentFiles();
            for(auto it=recent.crbegin();it!=recent.crend();++it) {
                if(it->m_url.isEmpty())continue;
                const QString path=it->m_url.isLocalFile()?
                    it->m_url.toLocalFile():it->m_url.toString();
                const auto known=indexed.value(projectKey(path));
                const QString name=!known.title.isEmpty()?known.title:
                    it->m_displayName.isEmpty()?QFileInfo(path).fileName():it->m_displayName;
                const qint64 modified=it->m_url.isLocalFile()?
                    QFileInfo(path).lastModified().toMSecsSinceEpoch():known.modified;
                addProject({path,name,modified,{}});
            }
        } else if(mode==RecoveryDrafts) {
            m_message->setText(i18n("Recovered drafts open as unsaved drawings."));
            for(const auto &draft:drafts)
                addProject({draft.absoluteFilePath(),MobileDraftRecovery::title(draft),
                            draft.lastModified().toMSecsSinceEpoch(),{}});
        } else {
            m_message->setText(m_lastAccess?
                i18n("Searching accessible storage for Krita projects…"):
                i18n("Checking accessible files and Android file grants…"));
            // Load persisted index immediately so the list is populated
            // before the background scan starts.  The scan will add new
            // entries and update timestamps; on completion the merged index
            // is saved back to disk.
            const auto indexed=MobileProjectIndex::load();
            for(const auto &e:indexed)
                addProject({e.path,e.title,e.modified,{}});
            // Always include Krita's Recent files in All Projects so SAF
            // documents that live outside the scan roots are never missing.
            {
                const auto recent=KisRecentFilesManager::instance()->recentFiles();
                for(auto it=recent.crbegin();it!=recent.crend();++it) {
                    if(it->m_url.isEmpty())continue;
                    const QString p=it->m_url.isLocalFile()?
                        it->m_url.toLocalFile():it->m_url.toString();
                    if(p.isEmpty())continue;
                    const auto *known=m_projectItems.value(projectKey(p));
                    const QString name=known?known->data(Qt::UserRole+3).toString():
                        it->m_displayName.isEmpty()?QFileInfo(p).fileName():it->m_displayName;
                    const qint64 mod=it->m_url.isLocalFile()?
                        QFileInfo(p).lastModified().toMSecsSinceEpoch():0;
                    addProject({p,name,mod,{}});
                }
            }
            // Keep the index visible during refresh and reuse it between scans.
            const qint64 now=QDateTime::currentMSecsSinceEpoch();
            const qint64 last=QSettings().value(
                QStringLiteral("MobileStudio/projectIndexSharedStorageScanMs"),0).toLongLong();
            const bool needsScan=forceScan || last<=0 || last>now ||
                                  now-last>24LL*60*60*1000;
            if(needsScan) {
                if(m_list->count()>0) {
                    m_message->setText(i18n("Updating saved projects in background…"));
                    // Retain newly opened Recent files even if a long scan
                    // is canceled when the user switches away from Home.
                    persistIndex();
                }
                startScan(forceScan);
            } else {
                const int count=m_list->count();
                m_message->setText(count==0?
                    i18n("No indexed Krita projects. Tap Refresh or use Browse Files."):
                    i18np("%1 indexed Krita project", "%1 indexed Krita projects",count));
                // New recent entries are remembered immediately rather than
                // waiting for a full scan to finish.
                persistIndex();
            }
        }
        if(mode!=AllProjects && m_list->count()==0) {
            auto *empty=new QListWidgetItem(mode==RecoveryDrafts?
                i18n("No recoverable drafts"):i18n("No recent Krita drawings"),
                m_list);
            empty->setFlags(Qt::NoItemFlags);
        }
        if(mode==AllProjects)sortProjects();
        else filterItems();
    }

    void addProject(const Project &project)
    {
        if(project.path.isEmpty())return;
        const QString key=projectKey(project.path);
        auto found=m_projectItems.find(key);
        if(found!=m_projectItems.end()) {
            // Scanned results can be newer than the persisted index. Update
            // the metadata and invalidate a stale thumbnail, rather than
            // duplicating the tile or retaining its old preview forever.
            auto *item=found.value();
            const QString existingPath=item->data(Qt::UserRole).toString();
            const qint64 previous=item->data(Qt::UserRole+1).toLongLong();
            const bool refreshedPath=existingPath!=project.path;
            const bool modified=project.timestamp>0 && project.timestamp!=previous;
            // An index record may carry an older SAF tree/direct URI for
            // this same canonical document. Replace it with the most
            // recently discovered usable location (Recent or current scan),
            // otherwise the tile can keep opening a stale provider grant.
            // Do not change the canonical key or any version-history files.
            if(refreshedPath) {
                item->setData(Qt::UserRole,project.path);
                const QString location=project.folder.isEmpty()?
                    project.path:project.folder;
                item->setData(Qt::UserRole+2,location);
                item->setToolTip(location);
                m_cache.remove(existingPath);
                m_pending.remove(existingPath);
                m_attempted.remove(existingPath);
            }
            if(modified)
                item->setData(Qt::UserRole+1,project.timestamp);
            if(!project.title.isEmpty() && item->data(Qt::UserRole+3).toString()!=project.title) {
                item->setData(Qt::UserRole+3,project.title);
                updateProjectLabel(item);
                filterItem(item,m_search->text().trimmed());
            }
            if(refreshedPath || modified) {
                m_cache.remove(project.path);
                m_attempted.remove(project.path);
                updatePreviewSource(item);
                filterItem(item,m_search->text().trimmed());
                scheduleThumbnails();
            }
            return;
        }
        auto *item=new QListWidgetItem(m_placeholder,project.title,m_list);
        m_projectItems.insert(key,item);
        item->setData(Qt::UserRole,project.path);
        item->setData(Qt::UserRole+1,project.timestamp);
        item->setData(Qt::UserRole+3,project.title);
        updateProjectLabel(item);
        const QString location=project.folder.isEmpty()?
            project.path:project.folder;
        item->setData(Qt::UserRole+2,location);
        item->setToolTip(location);
        filterItem(item,m_search->text().trimmed());
        updatePreviewSource(item);
    }

    void updatePreviewSource(QListWidgetItem *item)
    {
        const QString project=item->data(Qt::UserRole).toString();
        const qint64 savedTime=item->data(Qt::UserRole+1).toLongLong();
        const auto draft=m_latestDrafts.value(projectKey(project));
        const qint64 draftTime=draft.lastModified().toMSecsSinceEpoch();
        const bool useDraft=draftTime>savedTime;
        const QString path=useDraft?draft.absoluteFilePath():project;
        const qint64 modified=useDraft?draftTime:savedTime;
        item->setData(Qt::UserRole+4,path);
        item->setData(Qt::UserRole+5,modified);
        m_attempted.remove(path);
        const auto cached=m_cache.find(path);
        if(cached!=m_cache.end() && cached->modified==modified)
            item->setIcon(cached->icon);
        else {
            if(cached!=m_cache.end())m_cache.erase(cached);
            item->setIcon(m_placeholder);
        }
    }

    void startScan(bool fullScan)
    {
        // Keep identities loaded from the index and Recent. Clearing them
        // here would duplicate every indexed project during the scan.
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        m_scanCancel=cancel;
        const int generation=++m_scanGeneration;
        m_scanDiscoveredCount=0;
        QPointer<MobileDocumentHome> home(this);
        const bool broadAccess=m_lastAccess;
        m_scanner.start([home,cancel,generation,fullScan,broadAccess]() {
            // Prioritize common directories during a timed background scan.
            // Refresh walks all accessible shared directories.
            QElapsedTimer timer;
            timer.start();
            const qint64 automaticLimitMs=12000;
            bool truncated=false;
            const auto androidList=[](const char *method) {
                const auto result=QAndroidJniObject::callStaticObjectMethod(
                    "org/krita/android/MainActivity",method,"()Ljava/lang/String;");
                return QJsonDocument::fromJson(result.toString().toUtf8()).array();
            };
            QStringList roots,common;
            for(const auto &volume:androidList("tabletProjectRoots")) {
                const QString root=volume.toString();
                if(root.isEmpty())continue;
                if(broadAccess)roots.append(root);
                for(const auto &folder:{QStringLiteral("Pictures"),QStringLiteral("Documents"),
                                       QStringLiteral("Download")})
                    common.append(QDir(root).filePath(folder));
            }
            roots.append(common);
            QSet<QString> seen,visited;
            QVector<Project> batch;
            const auto deliverBatch=[&]() {
                if(batch.isEmpty())return;
                auto deliver=std::move(batch);
                batch.clear();
                QMetaObject::invokeMethod(qApp,
                    [home,cancel,generation,deliver]() {
                        if(home && !cancel->load())
                            home->appendBatch(generation,deliver);
                    },Qt::QueuedConnection);
            };
            while(!roots.isEmpty() && !cancel->load()) {
                if(!fullScan && timer.elapsed()>automaticLimitMs) {
                    truncated=true;
                    break;
                }
                const QString root=roots.takeLast();
                if(visited.contains(root))continue;
                visited.insert(root);
                QDirIterator it(root,QDir::Dirs|QDir::Files|QDir::Hidden|
                    QDir::NoDotAndDotDot|QDir::NoSymLinks);
                while(!cancel->load() && it.hasNext()) {
                    if(!fullScan && timer.elapsed()>automaticLimitMs) {
                        truncated=true;
                        break;
                    }
                    const QString path=it.next();
                    const QFileInfo info=it.fileInfo();
                    if(info.isDir()) {
                        if(!info.fileName().startsWith(QLatin1Char('.')) &&
                           !path.endsWith(QStringLiteral("/Android/data")) &&
                           !path.endsWith(QStringLiteral("/Android/obb")))
                            roots.append(path);
                    } else if(info.isReadable() && isKra(path) &&
                              !info.fileName().startsWith(QLatin1Char('.'))) {
                        seen.insert(QDir::cleanPath(path));
                        batch.append({path,info.fileName(),
                                      info.lastModified().toMSecsSinceEpoch(),{}});
                        if(batch.size()>=32)deliverBatch();
                    }
                }
            }
            deliverBatch();
            if(cancel->load())return;
            for(const auto &project:androidList("tabletGrantedProjects")) {
                if(cancel->load())return;
                const auto entry=project.toObject();
                if(seen.contains(QDir::cleanPath(entry.value(QStringLiteral("localPath")).toString())))
                    continue;
                batch.append({entry.value(QStringLiteral("uri")).toString(),
                              entry.value(QStringLiteral("name")).toString(),
                              qint64(entry.value(QStringLiteral("modified")).toDouble()),
                              entry.value(QStringLiteral("folder")).toString()});
                if(batch.size()>=32)deliverBatch();
            }
            deliverBatch();
            QMetaObject::invokeMethod(qApp,[home,cancel,generation,truncated]() {
                if(home && !cancel->load())
                    home->finishScan(generation,truncated);
            },Qt::QueuedConnection);
        });
    }

    void appendBatch(int generation,const QVector<Project> &projects)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        for(const auto &p:projects)addProject(p);
        m_scanDiscoveredCount+=projects.size();
        if(m_scanDiscoveredCount>0)
            m_message->setText(i18np(
                "Checking folders… %1 project discovered",
                "Checking folders… %1 projects discovered",
                m_scanDiscoveredCount));
        scheduleThumbnails();
    }

    void persistIndex()
    {
        QVector<MobileProjectIndexEntry> entries;
        entries.reserve(m_list->count());
        for(int i=0;i<m_list->count();++i) {
            const auto *item=m_list->item(i);
            if(!item || item->data(Qt::UserRole).toString().isEmpty())continue;
            MobileProjectIndexEntry entry;
            entry.path=item->data(Qt::UserRole).toString();
            entry.title=item->data(Qt::UserRole+3).toString();
            entry.modified=item->data(Qt::UserRole+1).toLongLong();
            entry.source=QStringLiteral("index");
            entries.append(entry);
        }
        // The pool serializes index writes; no need to block the touch UI.
        m_scanner.start([entries]() {
            MobileProjectIndex::save(MobileProjectIndex::merge(MobileProjectIndex::load(),entries));
        });
    }

    void finishScan(int generation,bool truncated)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        sortProjects();
        const int count=m_list->count();
        if(truncated) {
            m_message->setText(
                i18n("%1 indexed projects. Tap Refresh to finish searching shared storage.",count));
        } else {
            m_message->setText(count==0?
                i18n("No Krita projects found. Try Browse Files."):
                i18np("%1 Krita project found", "%1 Krita projects found",count));
        }
        QSettings().setValue(
            QStringLiteral("MobileStudio/projectIndexSharedStorageScanMs"),
            QDateTime::currentMSecsSinceEpoch());
        persistIndex();
    }

    void sortProjects()
    {
        QList<QListWidgetItem*> sorted;
        while(m_list->count())sorted.append(m_list->takeItem(0));
        std::stable_sort(sorted.begin(),sorted.end(),
            [this](QListWidgetItem *a,QListWidgetItem *b) {
                const qint64 am=a->data(Qt::UserRole+5).toLongLong();
                const qint64 bm=b->data(Qt::UserRole+5).toLongLong();
                if(m_sortOrder==Starred) {
                    const bool aStar=m_starred.contains(projectKey(a->data(Qt::UserRole).toString()));
                    const bool bStar=m_starred.contains(projectKey(b->data(Qt::UserRole).toString()));
                    if(aStar!=bStar)return aStar;
                }
                if(m_sortOrder!=Name && am!=bm)return am>bm;
                const int name=QString::localeAwareCompare(
                    a->data(Qt::UserRole+3).toString().toCaseFolded(),
                    b->data(Qt::UserRole+3).toString().toCaseFolded());
                return name==0?am>bm:name<0;
            });
        for(QListWidgetItem *item:sorted)m_list->addItem(item);
        filterItems();
    }

    void filterItem(QListWidgetItem *item,const QString &query) const
    {
        item->setHidden(!query.isEmpty() &&
            !item->data(Qt::UserRole+3).toString().contains(query,Qt::CaseInsensitive));
    }

    void filterItems()
    {
        const QString query=m_search->text().trimmed();
        for(int i=0;i<m_list->count();++i)filterItem(m_list->item(i),query);
        scheduleThumbnails();
    }

    void scheduleThumbnails()
    {
        if(!isVisible() || !m_list)return;
        if(m_thumbnailScheduled)return;
        m_thumbnailScheduled=true;
        QTimer::singleShot(50,this,[this]() {
            m_thumbnailScheduled=false;
            requestVisibleThumbnails();
        });
    }

    void requestVisibleThumbnails()
    {
        if(!isVisible())return;
        const QRect region=m_list->viewport()->rect().adjusted(0,-170,0,170);
        int budget=10;
        for(int i=0;i<m_list->count() && budget>0;++i) {
            auto *item=m_list->item(i);
            if(item->isHidden() ||
               !m_list->visualItemRect(item).intersects(region))continue;
            const QString project=item->data(Qt::UserRole).toString();
            const QString path=item->data(Qt::UserRole+4).toString();
            if(path.isEmpty() || m_pending.contains(path) ||
               m_attempted.contains(path) || m_cache.contains(path))continue;
            const qint64 modified=item->data(Qt::UserRole+5).toLongLong();
            m_pending.insert(path);
            --budget;
            QPointer<MobileDocumentHome> home(this);
            const quint64 generation=m_previewGeneration;
            const bool refresh=m_forceThumbnailRefresh;
            m_thumbnails.start([home,project,path,modified,generation,refresh]() {
                // Even cached PNG decoding belongs off the touch/UI thread.
                // Explicit Refresh always reopens the project rather than
                // reusing the previous disk cache (important for SAF URIs
                // with unknown modification timestamps).
                QImage image=refresh?QImage():
                    MobileProjectIndex::loadThumb(path,modified);
                if(image.isNull()) {
                    image=preview(path);
                    // Persist to disk so next launch avoids opening KRA.
                    if(!image.isNull())
                        MobileProjectIndex::saveThumb(path,modified,image);
                }
                QMetaObject::invokeMethod(qApp,[home,project,path,image,modified,generation]() {
                    if(!home || generation!=home->m_previewGeneration)return;
                    home->m_pending.remove(path);
                    auto *current=home->m_projectItems.value(projectKey(project));
                    if(!current || current->data(Qt::UserRole).toString()!=project ||
                       current->data(Qt::UserRole+4).toString()!=path ||
                       current->data(Qt::UserRole+5).toLongLong()!=modified) {
                        // A scan or save replaced this project while decoding.
                        // Let the current revision request its own preview.
                        home->scheduleThumbnails();
                        return;
                    }
                    home->m_attempted.insert(path);
                    if(!image.isNull()) {
                        if(home->m_cache.size()>=96)home->m_cache.erase(
                            home->m_cache.begin());
                        const QIcon icon(QPixmap::fromImage(image));
                        home->m_cache.insert(path,{icon,modified});
                        current->setIcon(icon);
                    }
                    home->scheduleThumbnails();
                },Qt::QueuedConnection);
            });
        }
    }

    MobileEngineBridge m_engine;
    QSharedPointer<MobileDocumentHostPort> m_files;
    QListWidget *m_list=nullptr;
    QPushButton *m_recent=nullptr,*m_library=nullptr,*m_drafts=nullptr;
    QPushButton *m_refresh=nullptr,*m_access=nullptr,*m_sort=nullptr;
    QLineEdit *m_search=nullptr;
    QLabel *m_message=nullptr;
    QIcon m_placeholder;
    Mode m_mode=Recent;
    bool m_lastAccess=false;
    SortOrder m_sortOrder=Newest;
    QSet<QString> m_starred;
    bool m_restoringVersion=false;
    bool m_thumbnailScheduled=false;
    bool m_forceThumbnailRefresh=false;
    int m_scanGeneration=0;
    int m_scanDiscoveredCount=0;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    QThreadPool m_scanner,m_thumbnails;
    QHash<QString,QListWidgetItem*> m_projectItems;
    QHash<QString,QFileInfo> m_latestDrafts;
    QSet<QString> m_pending,m_attempted;
    struct CachedPreview {QIcon icon; qint64 modified=0;};
    QHash<QString,CachedPreview> m_cache;
    quint64 m_previewGeneration=0;
};
#endif
