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
#include <QScroller>
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
// Entries are merged by canonical SAF identity; stale entries (file gone
// and not reachable via SAF) are pruned on each successful rescan.
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

    // Merge incoming entries into existing, dedup by canonical identity.
    // Incoming entries update timestamp and source of existing ones.
    static QVector<MobileProjectIndexEntry> merge(
        QVector<MobileProjectIndexEntry> existing,
        const QVector<MobileProjectIndexEntry> &incoming)
    {
        QHash<QString,int> byId;
        for(int i=0;i<existing.size();++i) {
            byId.insert(MobileSavedVersionArchive::canonicalIdentity(existing[i].path),i);
        }
        for(const auto &e:incoming) {
            const QString id=MobileSavedVersionArchive::canonicalIdentity(e.path);
            auto it=byId.find(id);
            if(it==byId.end()) {
                byId.insert(id,existing.size());
                existing.append(e);
            } else {
                auto &ex=existing[it.value()];
                // Prefer the entry with more information
                if(e.modified>ex.modified)ex.modified=e.modified;
                if(ex.title.isEmpty()&&!e.title.isEmpty())ex.title=e.title;
                // scan/saf sources are more authoritative than "recent"
                if(e.source!=QStringLiteral("recent"))ex.source=e.source;
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
        QImageReader reader(tp,"png");
        return reader.read();
    }

    // Save a thumbnail to disk and stamp its mtime to match the project.
    static void saveThumb(const QString &path, qint64 modified, const QImage &image) {
        if(image.isNull())return;
        const QString dir=thumbDir();
        QDir().mkpath(dir);
        const QString tp=thumbPath(path);
        image.save(tp,"PNG");
        // Stamp mtime so stale detection works
        if(modified>0) {
            const QDateTime dt=QDateTime::fromMSecsSinceEpoch(modified);
            QFile f(tp);
            if(f.open(QIODevice::ReadWrite)) {
                f.setFileTime(dt,QFileDevice::FileModificationTime);
                f.close();
            }
        }
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
        m_open=new QPushButton(i18n("Open Canvases"),this);
        for(QPushButton *button:{m_recent,m_library,m_open}) {
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
        m_sortByName=QSettings().value(QStringLiteral("MobileStudio/projectSortByName"),false).toBool();
        updateSortLabel();
        tools->addWidget(m_sort);
        m_versions=new QPushButton(i18n("Versions…"),this);
        m_versions->setCheckable(true);
        tools->addWidget(m_versions);
        m_discardDraft=new QPushButton(i18n("Discard…"),this);
        m_discardDraft->setCheckable(true);
        tools->addWidget(m_discardDraft);
        layout->addLayout(tools);

        auto *locations=new QHBoxLayout;
        m_access=new QPushButton(i18n("Allow all local files"),this);
        m_addFolder=new QPushButton(i18n("Folders…"),this);
        auto *folderMenu=new QMenu(m_addFolder);
        folderMenu->setStyleSheet(QStringLiteral("QMenu::item{min-height:30px;padding:8px 14px;}"));
        auto *addFolderAction=folderMenu->addAction(i18n("Add folder…"));
        auto *removeFolderAction=folderMenu->addAction(i18n("Remove linked folder…"));
        m_addFolder->setMenu(folderMenu);
        locations->addWidget(m_access);
        locations->addWidget(m_addFolder);
        layout->addLayout(locations);
        m_message=new QLabel(this);
        m_message->setWordWrap(true);
        m_message->setStyleSheet(QStringLiteral("font-size:12px;color:#c3ccd7;"));
        layout->addWidget(m_message);
        m_versionPrompt=new QLabel(i18n("Tap a project to see its saved versions."),this);
        m_versionPrompt->setWordWrap(true);
        m_versionPrompt->hide();
        layout->addWidget(m_versionPrompt);

        m_list=new QListWidget(this);
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(124,124));
        m_list->setGridSize(QSize(148,164));
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setWordWrap(true);
        m_list->setSpacing(3);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(m_list->viewport(),
                               QScroller::LeftMouseButtonGesture);
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
        connect(m_open,&QPushButton::clicked,this,
                [this]() {showList(OpenCanvases);});
        connect(m_drafts,&QPushButton::clicked,this,
                [this]() {showList(RecoveryDrafts);});
        connect(m_refresh,&QPushButton::clicked,this,[this]() {
            m_cache.clear();
            showList(m_mode);
        });
        connect(m_sort,&QPushButton::clicked,this,[this]() {
            m_sortByName=!m_sortByName;
            QSettings().setValue(QStringLiteral("MobileStudio/projectSortByName"),m_sortByName);
            updateSortLabel();
            sortProjects();
        });
        connect(addFolderAction,&QAction::triggered,this,[this]() {
            QTimer::singleShot(0,this,[this]() {addFolder();});
        });
        connect(removeFolderAction,&QAction::triggered,this,[this]() {
            QTimer::singleShot(0,this,[this]() {removeFolder();});
        });
        connect(m_versions,&QPushButton::toggled,this,[this](bool armed) {
            m_message->setVisible(!armed);
            m_versionPrompt->setVisible(armed);
        });
        connect(m_discardDraft,&QPushButton::toggled,this,[this](bool armed) {
            if(m_mode!=RecoveryDrafts)return;
            m_message->setText(armed?i18n("Choose an unopened draft to discard."):
                i18n("Recovered drafts open as unsaved drawings."));
        });
        connect(m_search,&QLineEdit::textChanged,this,
                [this](const QString &) {filterItems();});
        connect(m_access,&QPushButton::clicked,this,[this]() {
            if(hasAllFilesAccess()) {
                showList(AllProjects);
                return;
            }
            if(QMessageBox::question(this,i18n("All Krita documents"),
                i18n("Android requires optional All files access to search "
                     "for Krita documents throughout shared storage, "
                     "including Downloads. This does not grant access to "
                     "other apps' private folders. You can revoke it in "
                     "Android settings. Open permission settings?"))
                !=QMessageBox::Yes)return;
            QAndroidJniObject::callStaticMethod<void>(
                "org/krita/android/MainActivity",
                "tabletRequestAllFilesAccess","()V");
        });
        connect(m_list,&QListWidget::itemClicked,this,
                [this](QListWidgetItem *item) {
            if(!item || !item->flags().testFlag(Qt::ItemIsEnabled))return;
            if(m_mode==OpenCanvases) {
                const int index=item->data(Qt::UserRole).toInt();
                const auto view=m_views.value(index);
                if(view && m_engine.activateDocumentView(view))hide();
                return;
            }
            const QString path=item->data(Qt::UserRole).toString();
            if(m_versions->isChecked()) {
                m_versions->setChecked(false);
                QTimer::singleShot(0,this,[this,path]() {
                    if(isVisible())showSavedVersions(path);
                });
                return;
            }
            if(m_mode==RecoveryDrafts && m_discardDraft->isChecked()) {
                m_discardDraft->setChecked(false);
                QTimer::singleShot(0,this,[this,path]() {discardDraft(path);});
                return;
            }
            QTimer::singleShot(0,this,[this,path]() {
                if(!isVisible() || !m_files || path.isEmpty())return;
                for(const auto &entry:m_engine.openDocuments()) {
                    const auto *doc=qobject_cast<KisDocument*>(
                        entry.document.data());
                    const QString docPath=doc?doc->path():QString();
                    const bool sameDoc=doc && (docPath==path ||
                        (!docPath.isEmpty() &&
                         MobileSavedVersionArchive::canonicalIdentity(docPath)==
                         MobileSavedVersionArchive::canonicalIdentity(path)) ||
                        doc->property("_mobileDraftPath").toString()==path);
                    if(sameDoc && m_engine.activateDocumentView(entry.nativeView)) {
                        hide();
                        return;
                    }
                }
                if(m_files->openFile(path))hide();
                else QMessageBox::warning(this,i18n("Open failed"),
                    i18n("Could not open this drawing. Use Browse Files "
                         "if Android needs you to grant access again."));
            });
        });
        connect(m_list->verticalScrollBar(),&QScrollBar::valueChanged,
                this,[this](int) {scheduleThumbnails();});
        connect(qApp,&QGuiApplication::applicationStateChanged,
                this,[this](Qt::ApplicationState state) {
            if(state==Qt::ApplicationActive && isVisible() &&
               m_mode==AllProjects && hasAllFilesAccess()!=m_lastAccess)
                showList(AllProjects);
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
        showList(MobileDraftRecovery::list().isEmpty()?Recent:RecoveryDrafts);
        setGeometry(parentWidget()->rect());
        show();
        raise();
        scheduleThumbnails();
    }

    bool restoreSingleDraft()
    {
        const auto drafts=MobileDraftRecovery::list();
        if(drafts.size()!=1 || !m_files)return false;
        const auto &draft=drafts.first();
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
                i18n("Completed KRA saves will appear here."));
            return;
        }
        QDialog dialog(window());
        dialog.setWindowTitle(i18n("Saved versions"));
        dialog.setMinimumWidth(300);
        auto *layout=new QVBoxLayout(&dialog);
        auto *explanation=new QLabel(i18n(
            "Open a saved copy as a new canvas. Use Save As to keep it."),&dialog);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:44px;padding:5px;}"));
        QScroller::grabGesture(list->viewport(),QScroller::LeftMouseButtonGesture);
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

        m_restoringVersion=true;
        const auto version=versions.at(list->currentRow());
        QPointer<MobileDocumentHome> home(this);
        QThreadPool::globalInstance()->start([home,version]() {
            const QString copy=MobileSavedVersionArchive::materializeRecovery(version);
            QMetaObject::invokeMethod(qApp,[home,copy]() {
                const bool opened=home && !copy.isEmpty() && home->m_files->openFile(copy);
                if(!copy.isEmpty())QFile::remove(copy);
                if(!home)return;
                home->m_restoringVersion=false;
                if(opened)home->hide();
                else QMessageBox::warning(home->window(),i18n("Saved versions"),
                    i18n("Could not open that saved copy."));
            },Qt::QueuedConnection);
        });
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
    enum Mode {Recent,AllProjects,OpenCanvases,RecoveryDrafts};
    struct Project {
        QString path;
        QString title;
        qint64 timestamp=0;
        QString folder;
    };

    void addFolder()
    {
        const auto action=QAndroidJniObject::fromString(
            QStringLiteral("android.intent.action.OPEN_DOCUMENT_TREE"));
        const QAndroidJniObject intent("android/content/Intent",
            "(Ljava/lang/String;)V",action.object<jstring>());
        if(!intent.isValid())return;
        QPointer<MobileDocumentHome> home(this);
        QtAndroid::startActivity(intent,0,[home](int,int result,const QAndroidJniObject &data) {
            if(result!=-1 || !data.isValid())return; // Android Activity.RESULT_OK
            const bool granted=QAndroidJniObject::callStaticMethod<jboolean>(
                "org/krita/android/MainActivity","tabletRememberProjectFolder",
                "(Landroid/content/Intent;)Z",data.object());
            QMetaObject::invokeMethod(qApp,[home,granted]() {
                if(!home || !home->isVisible())return;
                if(granted)home->showList(AllProjects);
                else QMessageBox::warning(home,i18n("Folder access unavailable"),
                    i18n("Android could not keep access to this folder. Try a different folder."));
            },Qt::QueuedConnection);
        });
    }

    void removeFolder()
    {
        const auto grants=QAndroidJniObject::callStaticObjectMethod(
            "org/krita/android/MainActivity","tabletProjectFolders","()Ljava/lang/String;");
        const auto folders=QJsonDocument::fromJson(grants.toString().toUtf8()).array();
        if(folders.isEmpty()) {
            QMessageBox::information(this,i18n("Linked folders"),
                i18n("No folders have been linked."));
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Remove linked folder"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:44px;padding:5px;}"));
        QScroller::grabGesture(list->viewport(),QScroller::LeftMouseButtonGesture);
        for(const auto &folder:folders) {
            const auto entry=folder.toObject();
            auto *item=new QListWidgetItem(entry.value(QStringLiteral("name")).toString(),list);
            item->setData(Qt::UserRole,entry.value(QStringLiteral("uri")).toString());
        }
        list->setCurrentRow(0);
        layout->addWidget(list);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
        auto *remove=buttons->addButton(i18n("Remove link"),QDialogButtonBox::AcceptRole);
        buttons->setStyleSheet(QStringLiteral("QPushButton{min-height:44px;}"));
        connect(remove,&QPushButton::clicked,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);
        if(dialog.exec()!=QDialog::Accepted || !list->currentItem())return;

        const QString path=list->currentItem()->data(Qt::UserRole).toString();
        for(const auto &entry:m_engine.openDocuments()) {
            const auto *doc=qobject_cast<KisDocument*>(entry.document.data());
            if(doc && doc->path().startsWith(path+QLatin1Char('/'))) {
                QMessageBox::information(this,i18n("Folder has an open drawing"),
                    i18n("Close drawings opened from this folder before removing its link."));
                return;
            }
        }
        const auto uri=QAndroidJniObject::fromString(path);
        const bool removed=QAndroidJniObject::callStaticMethod<jboolean>(
            "org/krita/android/MainActivity","tabletForgetProjectFolder",
            "(Ljava/lang/String;)Z",uri.object<jstring>());
        if(removed)showList(AllProjects);
        else QMessageBox::warning(this,i18n("Linked folders"),
            i18n("Android could not remove that folder link."));
    }

    void updateSortLabel()
    {
        m_sort->setText(m_sortByName?i18n("Name"):i18n("Newest"));
        m_sort->setToolTip(i18n("Switch project sort order"));
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
        m_cache.remove(path);
        showList(RecoveryDrafts);
    }

    static bool isKra(const QString &path)
    {
        const QString suffix=QFileInfo(path).suffix();
        return suffix.compare(QStringLiteral("kra"),Qt::CaseInsensitive)==0 ||
               suffix.compare(QStringLiteral("krz"),Qt::CaseInsensitive)==0;
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

    void showList(Mode mode)
    {
        m_mode=mode;
        m_recent->setChecked(mode==Recent);
        m_library->setChecked(mode==AllProjects);
        m_open->setChecked(mode==OpenCanvases);
        const auto drafts=MobileDraftRecovery::list();
        m_drafts->setChecked(mode==RecoveryDrafts);
        m_drafts->setVisible(mode==RecoveryDrafts || !drafts.isEmpty());
        m_versions->setChecked(false);
        m_versions->setVisible(mode==Recent || mode==AllProjects);
        m_discardDraft->setChecked(false);
        m_discardDraft->setVisible(mode==RecoveryDrafts && !drafts.isEmpty());
        m_search->setVisible(mode!=OpenCanvases);
        m_refresh->setVisible(mode!=OpenCanvases);
        m_lastAccess=hasAllFilesAccess();
        m_access->setVisible(mode==AllProjects && !m_lastAccess);
        m_addFolder->setVisible(mode==AllProjects);
        m_sort->setVisible(mode==AllProjects);
        m_list->setViewMode(mode==OpenCanvases?
                            QListView::ListMode:QListView::IconMode);
        m_list->setGridSize(mode==OpenCanvases?QSize():QSize(148,164));
        m_list->setIconSize(QSize(124,124));
        m_projectItems.clear();
        m_list->clear();
        m_views.clear();
        ++m_previewGeneration;
        m_thumbnails.clear();
        m_pending.clear();
        m_attempted.clear();
        if(m_scanCancel)m_scanCancel->store(true);
        if(mode==OpenCanvases) {
            m_message->setText(i18n("Switch between open drawings"));
            for(const auto &entry:m_engine.openDocuments()) {
                auto *item=new QListWidgetItem(entry.title+
                    (entry.modified?QStringLiteral(" *"):QString()),m_list);
                item->setData(Qt::UserRole,m_views.size());
                m_views.append(entry.nativeView);
            }
        } else if(mode==Recent) {
            m_message->setText(i18n("Recently opened drawings"));
            const auto recent=KisRecentFilesManager::instance()->recentFiles();
            for(auto it=recent.crbegin();it!=recent.crend();++it) {
                if(it->m_url.isEmpty())continue;
                const QString path=it->m_url.isLocalFile()?
                    it->m_url.toLocalFile():it->m_url.toString();
                const QString name=it->m_displayName.isEmpty()?
                    QFileInfo(path).fileName():it->m_displayName;
                const qint64 modified=it->m_url.isLocalFile()?
                    QFileInfo(path).lastModified().toMSecsSinceEpoch():0;
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
                i18n("Searching common folders and folders you added…"));
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
                    const QString name=it->m_displayName.isEmpty()?
                        QFileInfo(p).fileName():it->m_displayName;
                    const qint64 mod=it->m_url.isLocalFile()?
                        QFileInfo(p).lastModified().toMSecsSinceEpoch():0;
                    addProject({p,name,mod,{}});
                }
            }
            if(m_list->count()>0)
                m_message->setText(i18n("Updating project list in background…"));
            startScan();
        }
        if(mode!=AllProjects && m_list->count()==0) {
            auto *empty=new QListWidgetItem(mode==OpenCanvases?
                i18n("No open canvases"):mode==RecoveryDrafts?
                i18n("No recoverable drafts"):i18n("No recent Krita drawings"),
                m_list);
            empty->setFlags(Qt::NoItemFlags);
        }
        filterItems();
        scheduleThumbnails();
    }

    void addProject(const Project &project)
    {
        if(project.path.isEmpty())return;
        const QString canonical=MobileSavedVersionArchive::canonicalIdentity(
            project.path);
        // Canonical SAF identity unifies direct and tree-grant URIs. For
        // images and other paths the KRA identity policy doesn't handle,
        // retain the unmodified path as the key.
        const QString key=canonical.isEmpty()?project.path:canonical;
        auto found=m_projectItems.find(key);
        if(found!=m_projectItems.end()) {
            // Scanned results can be newer than the persisted index. Update
            // the metadata and invalidate a stale thumbnail, rather than
            // duplicating the tile or retaining its old preview forever.
            auto *item=found.value();
            const qint64 previous=item->data(Qt::UserRole+1).toLongLong();
            if(project.timestamp>previous) {
                const QString existingPath=item->data(Qt::UserRole).toString();
                item->setData(Qt::UserRole+1,project.timestamp);
                m_cache.remove(existingPath);
                m_attempted.remove(existingPath);
                item->setIcon(m_placeholder);
                scheduleThumbnails();
            }
            return;
        }
        auto *item=new QListWidgetItem(m_placeholder,project.title,m_list);
        m_projectItems.insert(key,item);
        item->setData(Qt::UserRole,project.path);
        item->setData(Qt::UserRole+1,project.timestamp);
        const QString location=project.folder.isEmpty()?
            project.path:project.folder;
        item->setData(Qt::UserRole+2,location);
        item->setToolTip(location);
        filterItem(item,m_search->text().trimmed());
        const auto cached=m_cache.find(project.path);
        if(cached!=m_cache.end()) {
            if(project.timestamp>0 && cached->modified!=project.timestamp)
                m_cache.erase(cached);
            else item->setIcon(cached->icon);
        }
    }

    void startScan()
    {
        // Keep identities loaded from the index and Recent. Clearing them
        // here would duplicate every indexed project during the scan.
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        m_scanCancel=cancel;
        const int generation=++m_scanGeneration;
        QPointer<MobileDocumentHome> home(this);
        m_scanner.start([home,cancel,generation]() {
            const auto androidList=[](const char *method) {
                const auto result=QAndroidJniObject::callStaticObjectMethod(
                    "org/krita/android/MainActivity",method,"()Ljava/lang/String;");
                return QJsonDocument::fromJson(result.toString().toUtf8()).array();
            };
            QStringList roots;
            for(const auto &volume:androidList("tabletProjectRoots")) {
                const QString root=volume.toString();
                if(root.isEmpty())continue;
                // Always scan Downloads only. Permission lets us read
                // its KRA files; it must not trigger a full-device scan.
                // Other folders are indexed only when explicitly linked.
                roots.append(QDir(root).filePath(QStringLiteral("Download")));
            }
            QSet<QString> seen;
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
                const QString root=roots.takeLast();
                QDirIterator it(root,QDir::Dirs|QDir::Files|QDir::Hidden|
                    QDir::NoDotAndDotDot|QDir::NoSymLinks);
                while(!cancel->load() && it.hasNext()) {
                    const QString path=it.next();
                    const QFileInfo info=it.fileInfo();
                    if(info.isDir()) {
                        if(info.fileName()!=QStringLiteral(".krita-gallery-trash") &&
                           !path.endsWith(QStringLiteral("/Android/data")) &&
                           !path.endsWith(QStringLiteral("/Android/obb")))
                            roots.append(path);
                    } else if(info.isReadable() && isKra(path)) {
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
            QMetaObject::invokeMethod(qApp,[home,cancel,generation]() {
                if(home && !cancel->load())home->finishScan(generation);
            },Qt::QueuedConnection);
        });
    }

    void appendBatch(int generation,const QVector<Project> &projects)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        for(const auto &p:projects)addProject(p);
        scheduleThumbnails();
    }

    void finishScan(int generation)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        sortProjects();
        const int count=m_list->count();
        m_message->setText(count==0?
            i18n("No Krita projects found. Add a folder or use Browse Files."):
            i18np("%1 Krita project found", "%1 Krita projects found",count));
        // Persist the discovered set so the next launch can show it instantly.
        QVector<MobileProjectIndexEntry> entries;
        entries.reserve(count);
        for(int i=0;i<count;++i) {
            const auto *item=m_list->item(i);
            MobileProjectIndexEntry e;
            e.path=item->data(Qt::UserRole).toString();
            e.title=item->text();
            e.modified=item->data(Qt::UserRole+1).toLongLong();
            e.source=QStringLiteral("scan");
            entries.append(e);
        }
        // Run the index save on the scanner pool so it doesn't block the UI.
        m_scanner.start([entries]() {
            MobileProjectIndex::save(entries);
        });
    }

    void sortProjects()
    {
        QList<QListWidgetItem*> sorted;
        while(m_list->count())sorted.append(m_list->takeItem(0));
        std::stable_sort(sorted.begin(),sorted.end(),
            [this](QListWidgetItem *a,QListWidgetItem *b) {
                const qint64 am=a->data(Qt::UserRole+1).toLongLong();
                const qint64 bm=b->data(Qt::UserRole+1).toLongLong();
                if(!m_sortByName && am!=bm)return am>bm;
                const int name=QString::localeAwareCompare(a->text().toCaseFolded(),
                                                            b->text().toCaseFolded());
                return name==0?am>bm:name<0;
            });
        for(QListWidgetItem *item:sorted)m_list->addItem(item);
        filterItems();
    }

    void filterItem(QListWidgetItem *item,const QString &query) const
    {
        item->setHidden(!query.isEmpty() && m_mode!=OpenCanvases &&
            !item->text().contains(query,Qt::CaseInsensitive) &&
            !item->data(Qt::UserRole+2).toString().contains(query,Qt::CaseInsensitive));
    }

    void filterItems()
    {
        const QString query=m_search->text().trimmed();
        for(int i=0;i<m_list->count();++i)filterItem(m_list->item(i),query);
        scheduleThumbnails();
    }

    void scheduleThumbnails()
    {
        if(!isVisible() || m_mode==OpenCanvases || !m_list)return;
        if(m_thumbnailScheduled)return;
        m_thumbnailScheduled=true;
        QTimer::singleShot(50,this,[this]() {
            m_thumbnailScheduled=false;
            requestVisibleThumbnails();
        });
    }

    void requestVisibleThumbnails()
    {
        if(!isVisible() || m_mode==OpenCanvases)return;
        const QRect region=m_list->viewport()->rect().adjusted(0,-170,0,170);
        int budget=10;
        for(int i=0;i<m_list->count() && budget>0;++i) {
            auto *item=m_list->item(i);
            if(item->isHidden() ||
               !m_list->visualItemRect(item).intersects(region))continue;
            const QString path=item->data(Qt::UserRole).toString();
            if(path.isEmpty() || m_pending.contains(path) ||
               m_attempted.contains(path) || m_cache.contains(path))continue;
            // Try the persistent on-disk thumbnail first (fast, no KoStore open).
            const qint64 modified=item->data(Qt::UserRole+1).toLongLong();
            {
                const QImage cached=MobileProjectIndex::loadThumb(path,modified);
                if(!cached.isNull()) {
                    const QIcon icon(QPixmap::fromImage(cached));
                    m_cache.insert(path,{icon,modified});
                    item->setIcon(icon);
                    m_attempted.insert(path);
                    continue;
                }
            }
            m_pending.insert(path);
            --budget;
            QPointer<MobileDocumentHome> home(this);
            const quint64 generation=m_previewGeneration;
            m_thumbnails.start([home,path,modified,generation]() {
                const QImage image=preview(path);
                // Persist to disk so future sessions don't need to re-open the archive.
                if(!image.isNull())
                    MobileProjectIndex::saveThumb(path,modified,image);
                QMetaObject::invokeMethod(qApp,[home,path,image,modified,generation]() {
                    if(!home || generation!=home->m_previewGeneration)return;
                    home->m_pending.remove(path);
                    home->m_attempted.insert(path);
                    if(!image.isNull()) {
                        if(home->m_cache.size()>=96)home->m_cache.erase(
                            home->m_cache.begin());
                        const QIcon icon(QPixmap::fromImage(image));
                        home->m_cache.insert(path,{icon,modified});
                        for(int j=0;j<home->m_list->count();++j) {
                            auto *item=home->m_list->item(j);
                            if(item->data(Qt::UserRole).toString()==path)
                                item->setIcon(icon);
                        }
                    }
                    home->scheduleThumbnails();
                },Qt::QueuedConnection);
            });
        }
    }

    MobileEngineBridge m_engine;
    QSharedPointer<MobileDocumentHostPort> m_files;
    QListWidget *m_list=nullptr;
    QPushButton *m_recent=nullptr,*m_library=nullptr,*m_open=nullptr,*m_drafts=nullptr;
    QPushButton *m_refresh=nullptr,*m_access=nullptr,*m_discardDraft=nullptr;
    QPushButton *m_addFolder=nullptr,*m_sort=nullptr,*m_versions=nullptr;
    QLineEdit *m_search=nullptr;
    QLabel *m_message=nullptr,*m_versionPrompt=nullptr;
    QIcon m_placeholder;
    Mode m_mode=Recent;
    bool m_lastAccess=false;
    bool m_sortByName=false;
    bool m_restoringVersion=false;
    bool m_thumbnailScheduled=false;
    int m_scanGeneration=0;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    QThreadPool m_scanner,m_thumbnails;
    QVector<QPointer<QObject>> m_views;
    QHash<QString,QListWidgetItem*> m_projectItems;
    QSet<QString> m_pending,m_attempted;
    struct CachedPreview {QIcon icon; qint64 modified=0;};
    QHash<QString,CachedPreview> m_cache;
    quint64 m_previewGeneration=0;
};
#endif
