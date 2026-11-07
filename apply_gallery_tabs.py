#!/usr/bin/env python3
"""Android gallery: separate selected-folder and accessible-device tabs."""
from pathlib import Path
import sys

def sub(source, old, new, tag):
    count=source.count(old)
    if count!=1:
        raise RuntimeError(f"Gallery tabs: {tag} expected 1 anchor, got {count}")
    return source.replace(old,new,1)

def main(root):
    root=Path(root).resolve()
    path=root/"libs/ui/KisWelcomePageWidget.cpp"
    s=path.read_text(encoding="utf-8")
    s=sub(s,"#include <QComboBox>\n",
          "#include <QComboBox>\n#include <QTabWidget>\n#include <QTimer>\n#include <QJsonArray>\n#include <QJsonObject>\n#include <QJsonDocument>\n",
          "includes")
    s=sub(s,
'''QDirIterator iter(*folder, QStringList() << QStringLiteral("*.kra")
                                                        << QStringLiteral("*.krz"),''',
'''QDirIterator iter(*folder, QStringList()
                                                        << QStringLiteral("*.kra*")
                                                        << QStringLiteral("*.krz*")
                                                        << QStringLiteral("*.ora")
                                                        << QStringLiteral("*.psd")
                                                        << QStringLiteral("*.png")
                                                        << QStringLiteral("*.jpg")
                                                        << QStringLiteral("*.jpeg")
                                                        << QStringLiteral("*.webp")
                                                        << QStringLiteral("*.tif")
                                                        << QStringLiteral("*.tiff")
                                                        << QStringLiteral("*.bmp")
                                                        << QStringLiteral("*.gif")
                                                        << QStringLiteral("*.svg")
                                                        << QStringLiteral("*.exr")
                                                        << QStringLiteral("*.xcf")
                                                        << QStringLiteral("*.jxl"),''',
          "folder compatible formats")
    s=sub(s,
'''        auto *galleryList = new QListWidget(galleryPage);''',
'''        auto *galleryTabs = new QTabWidget(galleryPage);
        auto *galleryList = new QListWidget(galleryTabs);''',
          "create tabs")
    s=sub(s,
'''        galleryLayout->addWidget(galleryList);
        pages->addWidget(galleryPage);''',
'''        galleryTabs->addTab(galleryList, i18n("Selected folder"));
        auto *deviceList = new QListWidget(galleryTabs);
        deviceList->setViewMode(QListView::IconMode);
        deviceList->setIconSize(QSize(156,156));
        deviceList->setGridSize(QSize(184,205));
        deviceList->setResizeMode(QListView::Adjust);
        deviceList->setMovement(QListView::Static);
        deviceList->setWordWrap(true);
        galleryTabs->addTab(deviceList,i18n("Accessible device files"));
        galleryLayout->addWidget(galleryTabs);
        pages->addWidget(galleryPage);''',"tab widgets")
    s=sub(s,
'''        auto populate = [this, galleryList, folder, sortMode]() {''',
'''        auto populate = [this, galleryList, folder, sortMode]() {''',
          "population signature")
    start=s.find("            // Keep SAF content URI projects in reach through Krita recent files.")
    end=s.find("        };\n        connect(galleryButton,", start)
    if start<0 or end<0:
        raise RuntimeError("Gallery tabs: recent SAF block not located")
    s=s[:start]+s[end:]
    device_code=r'''        auto updateGalleryAccess=[allowFiles]() {
            const bool granted=QAndroidJniObject::callStaticMethod<jboolean>(
                "org/krita/android/MainActivity","tabletHasAllFilesAccess","()Z");
            allowFiles->setVisible(!granted);
        };
        updateGalleryAccess();
        QObject::connect(qApp,&QGuiApplication::applicationStateChanged,this,
                         [updateGalleryAccess](Qt::ApplicationState state) {
            if(state==Qt::ApplicationActive)updateGalleryAccess();
        });

        const auto acceptedExtensions=QStringList{
            QStringLiteral("*.kra*"),QStringLiteral("*.krz*"),
            QStringLiteral("*.ora"),QStringLiteral("*.psd"),
            QStringLiteral("*.png"),QStringLiteral("*.jpg"),
            QStringLiteral("*.jpeg"),QStringLiteral("*.webp"),
            QStringLiteral("*.tif"),QStringLiteral("*.tiff"),
            QStringLiteral("*.bmp"),QStringLiteral("*.gif"),
            QStringLiteral("*.svg"),QStringLiteral("*.exr"),
            QStringLiteral("*.hdr"),QStringLiteral("*.xcf"),
            QStringLiteral("*.qoi"),QStringLiteral("*.jxl"),
            QStringLiteral("*.heic"),QStringLiteral("*.heif"),
            QStringLiteral("*.avif")
        };
        auto deviceSeen=std::make_shared<QSet<QString>>();
        auto deviceIterator=std::make_shared<std::shared_ptr<QDirIterator>>();
        auto deviceCount=std::make_shared<int>(0);
        auto scanTimer=new QTimer(galleryPage);
        scanTimer->setInterval(10);
        auto addDeviceItem=[deviceList,deviceSeen](const QString &location,
                                                  const QString &title,
                                                  const QString &physicalPath) {
            if(location.isEmpty() || deviceSeen->contains(location))return;
            if(!physicalPath.isEmpty() && deviceSeen->contains(physicalPath))return;
            QIcon thumbnail;
            if(!physicalPath.isEmpty() && QFileInfo(physicalPath).isReadable()) {
                QScopedPointer<KoStore> store(KoStore::createStore(physicalPath,KoStore::Read));
                if(store && !store->bad()) {
                    for(const QString &preview:{QStringLiteral("preview.png"),
                                                 QStringLiteral("Thumbnails/thumbnail.png")}) {
                        if(!store->hasFile(preview) || !store->open(preview))continue;
                        QImage image;
                        image.loadFromData(store->read(store->size()),"PNG");
                        store->close();
                        if(!image.isNull()) {
                            thumbnail=QIcon(QPixmap::fromImage(image.scaled(
                                156,156,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
                            break;
                        }
                    }
                }
            }
            auto *item=new QListWidgetItem(thumbnail,title,deviceList);
            item->setData(Qt::UserRole,location);
            item->setToolTip(physicalPath.isEmpty()?location:physicalPath);
            deviceSeen->insert(location);
            if(!physicalPath.isEmpty())deviceSeen->insert(physicalPath);
        };
        QObject::connect(scanTimer,&QTimer::timeout,this,
            [scanTimer,deviceIterator,deviceCount,addDeviceItem,deviceList]() {
            if(!*deviceIterator){scanTimer->stop();return;}
            int batch=0;
            while(batch++<100 && (*deviceIterator)->hasNext() &&
                  *deviceCount<3500) {
                const QString path=(*deviceIterator)->next();
                const QFileInfo info(path);
                if(!info.isReadable())continue;
                addDeviceItem(info.absoluteFilePath(),info.completeBaseName(),
                              info.absoluteFilePath());
                ++*deviceCount;
            }
            if(!(*deviceIterator)->hasNext() || *deviceCount>=3500) {
                scanTimer->stop();
                deviceIterator->reset();
            }
        });
        auto populateDevice=[this,deviceList,deviceSeen,deviceIterator,deviceCount,
                             scanTimer,acceptedExtensions,addDeviceItem,
                             updateGalleryAccess]() {
            scanTimer->stop();
            deviceIterator->reset();
            deviceList->clear();
            deviceSeen->clear();
            *deviceCount=0;
            updateGalleryAccess();
            // Existing SAF document URIs may be accessible even when an
            // absolute filesystem path is hidden by Android storage policy.
            if(const QAbstractItemModel *recent=recentDocumentsListView->model()) {
                for(int i=0;i<recent->rowCount();++i) {
                    QModelIndex index=recent->index(i,0);
                    const QString uri=index.data(Qt::ToolTipRole).toString();
                    if(!uri.isEmpty())
                        addDeviceItem(uri,index.data(Qt::DisplayRole).toString(),
                                      uri.startsWith(QStringLiteral("content://")) ?
                                          QString():uri);
                }
            }
#ifdef Q_OS_ANDROID
            // MediaStore finds indexed downloads/files that are omitted from
            // direct QDir scans. The returned URI remains readable by Krita's
            // normal Android content-URI document opener when authorized.
            const QAndroidJniObject json=QAndroidJniObject::callStaticObjectMethod(
                "org/krita/android/MainActivity","tabletIndexedArtFiles",
                "()Ljava/lang/String;");
            if(json.isValid()) {
                const QJsonDocument doc=QJsonDocument::fromJson(json.toString().toUtf8());
                if(doc.isArray())for(const QJsonValue &value:doc.array()) {
                    const QJsonObject object=value.toObject();
                    const QString uri=object.value(QStringLiteral("uri")).toString();
                    const QString path=object.value(QStringLiteral("path")).toString();
                    const QString name=object.value(QStringLiteral("name")).toString();
                    addDeviceItem(path.isEmpty()?uri:path,name,path);
                }
            }
#endif
            // Cover additional readable projects that Android has not indexed.
            // Process the scan in small timer batches to keep touch UI usable.
            const QString sharedRoot=QStringLiteral("/storage/emulated/0");
            if(QDir(sharedRoot).exists()) {
                *deviceIterator=std::make_shared<QDirIterator>(
                    sharedRoot,acceptedExtensions,
                    QDir::Files|QDir::Readable|QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
                scanTimer->start();
            }
        };
        connect(galleryTabs,&QTabWidget::currentChanged,this,
                [galleryTabs,populateDevice,scanTimer](int index) {
            if(index==1)populateDevice();
            else scanTimer->stop();
        });
        connect(deviceList,&QListWidget::itemClicked,this,
                [this](QListWidgetItem *item) {
            m_mainWindow->openDocument(item->data(Qt::UserRole).toString(),
                                        KisMainWindow::None);
        });
'''
    token="        connect(galleryButton, &QToolButton::clicked, this,"
    s=sub(s,token,device_code+token,"device listing")
    s=sub(s,
'''                [pages, galleryPage, galleryButton, populate, this]() {''',
'''                [pages, galleryPage, galleryButton, galleryTabs, scanTimer,
                 populate, populateDevice, this]() {''',
          "gallery switching capture")
    s=sub(s,
'''                pages->setCurrentWidget(galleryPage);
                galleryButton->setText(i18n("Recent Images"));''',
'''                pages->setCurrentWidget(galleryPage);
                if(galleryTabs->currentIndex()==1)populateDevice();
                galleryButton->setText(i18n("Recent Images"));''',
          "device refresh on opening")
    s=sub(s,
'''            if (pages->currentWidget() == galleryPage) {
                // Krita's recent-file model''',
'''            if (pages->currentWidget() == galleryPage) {
                scanTimer->stop();
                // Krita's recent-file model''',
          "stop scan on leaving")
    s=sub(s,
'''        connect(refreshButton, &QPushButton::clicked, this, populate);''',
'''        connect(refreshButton, &QPushButton::clicked, this,
                [galleryTabs,populate,populateDevice]() {
            if(galleryTabs->currentIndex()==1)populateDevice();
            else populate();
        });''',
          "refresh current tab")
    s=sub(s,
'''        connect(sortMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [populate](int) { populate(); });''',
'''        connect(sortMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [galleryTabs,populate,populateDevice](int) {
            if(galleryTabs->currentIndex()==1)populateDevice();
            else populate();
        });''',"sort current tab")
    path.write_text(s,encoding="utf-8")

    # Android MediaStore query complements filesystem scanning; use only
    # normal shared-storage records and retain URI permissions/visibility.
    java_path=root/"packaging/android/apk/src/org/krita/android/MainActivity.java"
    java=java_path.read_text(encoding="utf-8")
    java_code=r'''    public static String tabletIndexedArtFiles() {
        MainActivity activity=tabletPermissionActivity;
        if(activity==null || !tabletHasAllFilesAccess())return "[]";
        org.json.JSONArray found=new org.json.JSONArray();
        String[] suffixes={"kra","krz","ora","psd","png","jpg","jpeg","webp",
            "tif","tiff","bmp","gif","svg","exr","hdr","xcf","qoi",
            "jxl","heic","heif","avif"};
        StringBuilder selection=new StringBuilder();
        String[] args=new String[suffixes.length];
        for(int i=0;i<suffixes.length;i++) {
            if(i>0)selection.append(" OR ");
            selection.append(android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME)
                     .append(" LIKE ?");
            args[i]="%."+suffixes[i]+(i<2?"%":"");
        }
        android.net.Uri base=android.provider.MediaStore.Files.getContentUri("external");
        String[] columns={
            android.provider.MediaStore.Files.FileColumns._ID,
            android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME,
            android.provider.MediaStore.Files.FileColumns.DATA
        };
        try(android.database.Cursor cursor=activity.getContentResolver().query(
            base,columns,selection.toString(),args,
            android.provider.MediaStore.Files.FileColumns.DATE_MODIFIED+" DESC")) {
            if(cursor!=null)while(cursor.moveToNext() && found.length()<3500) {
                long key=cursor.getLong(0);
                String name=cursor.getString(1);
                String path=cursor.getString(2);
                org.json.JSONObject item=new org.json.JSONObject();
                item.put("uri",android.content.ContentUris.withAppendedId(
                    base,key).toString());
                item.put("name",name==null?"Artwork":name);
                item.put("path",path==null?"":path);
                found.put(item);
            }
        }catch(Exception ignored) {
            // Some Android versions/providers do not index arbitrary projects.
            // The Qt scanner and existing SAF history remain available.
        }
        return found.toString();
    }

'''
    marker="    private static final String TAG"
    java=sub(java,marker,java_code+marker,"Java index helper")
    java_path.write_text(java,encoding="utf-8")
    print("Gallery: tabs, indexed Android art and incremental shared-device scan applied")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_gallery_tabs.py KRITA_SOURCE")
    main(sys.argv[1])
