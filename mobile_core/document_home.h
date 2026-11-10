/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Touch-first native document launcher. Uses Krita's recent file manager
 * and native open/view services; never reads document layers into a
 * second renderer. KRA thumbnails are extracted from embedded previews.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QImage>
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
#include <QScrollBar>
#include <QScroller>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
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

        auto *tools=new QHBoxLayout;
        m_search=new QLineEdit(this);
        m_search->setPlaceholderText(i18n("Search Krita documents"));
        m_search->setClearButtonEnabled(true);
        m_refresh=new QPushButton(i18n("Refresh"),this);
        m_refresh->setMinimumWidth(76);
        tools->addWidget(m_search,1);
        tools->addWidget(m_refresh);
        layout->addLayout(tools);

        m_access=new QPushButton(i18n("Allow access to all local projects"),this);
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
        connect(m_refresh,&QPushButton::clicked,this,[this]() {
            showList(m_mode);
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
            QTimer::singleShot(0,this,[this,path]() {
                if(!isVisible() || !m_files || path.isEmpty())return;
                for(const auto &entry:m_engine.openDocuments()) {
                    const auto *doc=qobject_cast<KisDocument*>(
                        entry.document.data());
                    if(doc && doc->path()==path &&
                       m_engine.activateDocumentView(entry.nativeView)) {
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
        showList(Recent);
        setGeometry(parentWidget()->rect());
        show();
        raise();
        scheduleThumbnails();
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
    enum Mode {Recent,AllProjects,OpenCanvases};
    struct Project {
        QString path;
        QString title;
        qint64 timestamp=0;
    };

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
        m_search->setVisible(mode!=OpenCanvases);
        m_refresh->setVisible(mode!=OpenCanvases);
        m_lastAccess=hasAllFilesAccess();
        m_access->setVisible(mode==AllProjects && !m_lastAccess);
        m_list->setViewMode(mode==OpenCanvases?
                            QListView::ListMode:QListView::IconMode);
        m_list->setGridSize(mode==OpenCanvases?QSize():QSize(148,164));
        m_list->setIconSize(QSize(124,124));
        m_list->clear();
        m_views.clear();
        m_seen.clear();
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
                addProject({path,name,0});
            }
        } else {
            m_message->setText(m_lastAccess?
                i18n("Searching accessible storage for Krita projects…"):
                i18n("Searching common folders. Grant file access to "
                     "find projects throughout device storage."));
            startScan(m_lastAccess);
        }
        if(mode!=AllProjects && m_list->count()==0) {
            auto *empty=new QListWidgetItem(mode==OpenCanvases?
                i18n("No open canvases"):i18n("No recent Krita drawings"),
                m_list);
            empty->setFlags(Qt::NoItemFlags);
        }
        filterItems();
        scheduleThumbnails();
    }

    void addProject(const Project &project)
    {
        if(project.path.isEmpty() || m_seen.contains(project.path))return;
        m_seen.insert(project.path);
        auto *item=new QListWidgetItem(m_placeholder,project.title,m_list);
        item->setData(Qt::UserRole,project.path);
        item->setData(Qt::UserRole+1,project.timestamp);
        item->setToolTip(project.path);
        const auto cached=m_cache.constFind(project.path);
        if(cached!=m_cache.cend())item->setIcon(cached.value());
    }

    void startScan(bool fullAccess)
    {
        m_seen.clear();
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        m_scanCancel=cancel;
        const int generation=++m_scanGeneration;
        const QStringList roots=fullAccess?
            QStringList{QStringLiteral("/storage/emulated/0")}:
            QStringList{QStringLiteral("/storage/emulated/0/Download"),
                        QStringLiteral("/storage/emulated/0/Documents"),
                        QStringLiteral("/storage/emulated/0/Pictures/Krita"),
                        QStringLiteral("/storage/emulated/0/Krita")};
        QPointer<MobileDocumentHome> home(this);
        m_scanner.start([home,cancel,generation,roots]() {
            QSet<QString> seen;
            QVector<Project> batch;
            int count=0;
            QElapsedTimer elapsed;
            elapsed.start();
            for(const QString &root:roots) {
                if(cancel->load() || count>=3000 ||
                   elapsed.elapsed()>30000)break;
                if(!QDir(root).exists())continue;
                QDirIterator it(root,
                    {QStringLiteral("*.kra"),QStringLiteral("*.krz")},
                    QDir::Files|QDir::Readable|QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
                while(!cancel->load() && it.hasNext() &&
                      count<3000 && elapsed.elapsed()<=30000) {
                    const QString path=it.next();
                    if(path.contains(QStringLiteral("/.krita-gallery-trash/")) ||
                       path.contains(QStringLiteral("/Android/data/")) ||
                       path.contains(QStringLiteral("/Android/obb/")) ||
                       seen.contains(path))continue;
                    const QFileInfo info(path);
                    if(!info.isReadable() || !isKra(path))continue;
                    seen.insert(path);
                    batch.append({path,info.fileName(),
                                  info.lastModified().toMSecsSinceEpoch()});
                    ++count;
                    if(batch.size()>=32) {
                        const auto deliver=batch;
                        batch.clear();
                        QMetaObject::invokeMethod(qApp,
                            [home,cancel,generation,deliver]() {
                            if(home && !cancel->load())
                                home->appendBatch(generation,deliver);
                        },Qt::QueuedConnection);
                    }
                }
            }
            QMetaObject::invokeMethod(qApp,[home,cancel,generation,batch,count]() {
                if(!home || cancel->load())return;
                home->appendBatch(generation,batch);
                home->finishScan(generation,count);
            },Qt::QueuedConnection);
        });
    }

    void appendBatch(int generation,const QVector<Project> &projects)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        for(const auto &p:projects)addProject(p);
        filterItems();
        scheduleThumbnails();
    }

    void finishScan(int generation,int count)
    {
        if(generation!=m_scanGeneration || m_mode!=AllProjects)return;
        // Sorting after scan avoids rebuilding the visible grid for
        // every filesystem result, including large document libraries.
        QList<QListWidgetItem*> sorted;
        while(m_list->count())sorted.append(m_list->takeItem(0));
        std::stable_sort(sorted.begin(),sorted.end(),
            [](QListWidgetItem *a,QListWidgetItem *b) {
            const qint64 am=a->data(Qt::UserRole+1).toLongLong();
            const qint64 bm=b->data(Qt::UserRole+1).toLongLong();
            return am==bm?QString::compare(a->text(),b->text(),
                    Qt::CaseInsensitive)<0:am>bm;
        });
        for(QListWidgetItem *item:sorted)m_list->addItem(item);
        m_message->setText(count==0?
            i18n("No local Krita documents found. Use Browse Files "
                 "for documents from other providers."):
            i18np("%1 local Krita document found",
                   "%1 local Krita documents found",count));
        filterItems();
        scheduleThumbnails();
    }

    void filterItems()
    {
        const QString query=m_search->text().trimmed();
        for(int i=0;i<m_list->count();++i) {
            QListWidgetItem *item=m_list->item(i);
            item->setHidden(!query.isEmpty() && m_mode!=OpenCanvases &&
                !item->text().contains(query,Qt::CaseInsensitive) &&
                !item->data(Qt::UserRole).toString().contains(
                    query,Qt::CaseInsensitive));
        }
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
            m_pending.insert(path);
            --budget;
            QPointer<MobileDocumentHome> home(this);
            m_thumbnails.start([home,path]() {
                const QImage image=preview(path);
                QMetaObject::invokeMethod(qApp,[home,path,image]() {
                    if(!home)return;
                    home->m_pending.remove(path);
                    home->m_attempted.insert(path);
                    if(!image.isNull()) {
                        if(home->m_cache.size()>=96)home->m_cache.erase(
                            home->m_cache.begin());
                        const QIcon icon(QPixmap::fromImage(image));
                        home->m_cache.insert(path,icon);
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
    QPushButton *m_recent=nullptr,*m_library=nullptr,*m_open=nullptr;
    QPushButton *m_refresh=nullptr,*m_access=nullptr;
    QLineEdit *m_search=nullptr;
    QLabel *m_message=nullptr;
    QIcon m_placeholder;
    Mode m_mode=Recent;
    bool m_lastAccess=false;
    bool m_thumbnailScheduled=false;
    int m_scanGeneration=0;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    QThreadPool m_scanner,m_thumbnails;
    QVector<QPointer<QObject>> m_views;
    QSet<QString> m_seen,m_pending,m_attempted;
    QHash<QString,QIcon> m_cache;
};
#endif
