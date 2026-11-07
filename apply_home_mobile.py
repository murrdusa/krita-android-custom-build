#!/usr/bin/env python3
"""Android-only touch-first Krita home and searchable/starred project gallery."""
from pathlib import Path
import sys

def sub(s,old,new,label):
    count=s.count(old)
    if count!=1:
        raise RuntimeError(f"Mobile home {label}: expected one anchor, got {count}")
    return s.replace(old,new,1)

def main(root):
    path=Path(root)/"libs/ui/KisWelcomePageWidget.cpp"
    s=path.read_text(encoding="utf-8")
    s=sub(s,"#include <QTabWidget>\n",
          "#include <QTabWidget>\n#include <QGridLayout>\n"
          "#include <QLineEdit>\n#include <QClipboard>\n"
          "#include <QAbstractItemModel>\n","includes")
    # Controls in two columns, so nothing is cut off in folded portrait.
    s=sub(s,"auto *galleryControls = new QHBoxLayout;",
          "auto *galleryControls = new QGridLayout;","gallery toolbar")
    for name,position in (
        ("folderButton","0,0"),("downloadsButton","0,1"),
        ("refreshButton","1,0"),("sortMode","1,1"),
        ("allowFiles","2,0,1,2"),("undoTrash","3,0,1,2")):
        s=sub(s,f"galleryControls->addWidget({name});",
              f"galleryControls->addWidget({name},{position});",
              f"toolbar {name}")
    s=sub(s,
'''        galleryTabs->addTab(deviceList,i18n("Krita projects on device"));
        galleryLayout->addWidget(galleryTabs);''',
'''        galleryTabs->addTab(deviceList,i18n("On device"));
        galleryLayout->addWidget(galleryTabs);
        galleryLayout->setContentsMargins(4,4,4,4);
        galleryLayout->setSpacing(7);
        galleryTabs->setStyleSheet(
            "QTabBar::tab{min-height:45px;padding:0 12px;font-size:13px;}"
            "QTabWidget::pane{border:0;}");
        for(QWidget *control:{static_cast<QWidget*>(folderButton),
                               static_cast<QWidget*>(downloadsButton),
                               static_cast<QWidget*>(refreshButton),
                               static_cast<QWidget*>(sortMode),
                               static_cast<QWidget*>(allowFiles)}) {
            control->setMinimumHeight(45);
            control->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        }
        for(QListWidget *list:{galleryList,deviceList}) {
            list->setIconSize(QSize(140,140));
            list->setGridSize(QSize(164,186));
            list->setSpacing(3);
            list->setMinimumWidth(0);
        }''',"touch gallery controls")
    filter_code=r'''        auto *searchRow=new QHBoxLayout;
        searchRow->setSpacing(6);
        auto *projectSearch=new QLineEdit(galleryPage);
        projectSearch->setPlaceholderText(i18n("Search projects"));
        projectSearch->setClearButtonEnabled(true);
        projectSearch->setMinimumHeight(48);
        auto *onlyStarred=new QToolButton(galleryPage);
        onlyStarred->setText(QStringLiteral("★"));
        onlyStarred->setAccessibleName(i18n("Show starred projects only"));
        onlyStarred->setToolTip(i18n("Show starred projects only"));
        onlyStarred->setCheckable(true);
        onlyStarred->setMinimumSize(52,48);
        onlyStarred->setStyleSheet(
            "QToolButton{background:#35383e;color:white;border:1px solid #777;"
            "border-radius:7px;font-size:21px;}"
            "QToolButton:checked{background:#6b613b;border:2px solid #e1c96c;}");
        searchRow->addWidget(projectSearch,1);
        searchRow->addWidget(onlyStarred);
        galleryLayout->insertLayout(1,searchRow);
        auto *projectCount=new QLabel(galleryPage);
        projectCount->setStyleSheet("color:#aeb7c5;font-size:12px;");
        galleryLayout->insertWidget(2,projectCount);
        const auto favorites=std::make_shared<QSet<QString>>();
        for(const QString &entry:QSettings().value(
                QStringLiteral("TabletUi/starredProjectsV1")).toStringList())
            favorites->insert(entry);
        auto filterProjects=[galleryTabs,galleryList,deviceList,projectSearch,
                             onlyStarred,projectCount,favorites]() {
            const QString query=projectSearch->text().trimmed();
            QListWidget *current=galleryTabs->currentIndex()==0 ?
                galleryList:deviceList;
            int matches=0,total=0;
            for(QListWidget *list:{galleryList,deviceList}) {
                for(int i=0;i<list->count();++i) {
                    QListWidgetItem *item=list->item(i);
                    const QString path=item->data(Qt::UserRole).toString();
                    QString name=item->data(Qt::UserRole+4).toString();
                    if(name.isEmpty()) {
                        name=item->text();
                        item->setData(Qt::UserRole+4,name);
                    }
                    const bool starred=favorites->contains(path);
                    const QString display=starred?QStringLiteral("★ ")+name:name;
                    if(item->text()!=display)item->setText(display);
                    const bool visible=(!onlyStarred->isChecked() || starred) &&
                        (query.isEmpty() ||
                         name.contains(query,Qt::CaseInsensitive) ||
                         path.contains(query,Qt::CaseInsensitive));
                    item->setHidden(!visible);
                    if(list==current) {
                        ++total;
                        if(visible)++matches;
                    }
                }
            }
            projectCount->setText(i18n("%1 of %2 projects",matches,total));
        };
        auto *filterTimer=new QTimer(galleryPage);
        filterTimer->setSingleShot(true);
        const auto scheduleFilter=[filterTimer]() {
            if(!filterTimer->isActive())filterTimer->start(0);
        };
        connect(filterTimer,&QTimer::timeout,this,filterProjects);
        connect(projectSearch,&QLineEdit::textChanged,this,
                [scheduleFilter](const QString&) {scheduleFilter();});
        connect(onlyStarred,&QToolButton::toggled,this,
                [scheduleFilter](bool) {scheduleFilter();});
        connect(galleryTabs,&QTabWidget::currentChanged,this,
                [scheduleFilter](int) {scheduleFilter();});
        for(QListWidget *list:{galleryList,deviceList}) {
            connect(list->model(),&QAbstractItemModel::rowsInserted,this,
                    [scheduleFilter](const QModelIndex&,int,int) {
                scheduleFilter();
            });
            connect(list->model(),&QAbstractItemModel::modelReset,this,
                    scheduleFilter);
        }
        scheduleFilter();
'''
    s=sub(s,"        auto suppressGalleryClick=std::make_shared<QSet<QListWidget*>>();",
          filter_code+"        auto suppressGalleryClick=std::make_shared<QSet<QListWidget*>>();",
          "search and starred filter")
    s=sub(s,
'''                [this,heldIndex,heldList,suppressGalleryClick,moveToGalleryTrash]() {''',
'''                [this,heldIndex,heldList,suppressGalleryClick,moveToGalleryTrash,
                 favorites,scheduleFilter]() {''',
          "long press captures")
    s=sub(s,
'''            QAction *trash=menu.addAction(
                i18n("Move selected project to Trash (undoable)"));
            trash->setIcon(QIcon::fromTheme(QStringLiteral("user-trash")));
            const QPoint menuPos=view->viewport()->mapToGlobal(
                view->visualRect(index).center());
            if(menu.exec(menuPos)==trash)
                moveToGalleryTrash(path);''',
'''            QAction *star=menu.addAction(favorites->contains(path)?
                i18n("Remove from starred projects"):i18n("Star this project"));
            QAction *copy=menu.addAction(i18n("Copy project location"));
            menu.addSeparator();
            QAction *trash=menu.addAction(
                i18n("Move selected project to Trash (undoable)"));
            trash->setIcon(QIcon::fromTheme(QStringLiteral("user-trash")));
            const QPoint menuPos=view->viewport()->mapToGlobal(
                view->visualRect(index).center());
            QAction *selected=menu.exec(menuPos);
            if(selected==star) {
                if(favorites->contains(path))favorites->remove(path);
                else favorites->insert(path);
                QStringList values;
                for(const QString &name:*favorites)values.append(name);
                QSettings().setValue(QStringLiteral("TabletUi/starredProjectsV1"),
                                     values);
                scheduleFilter();
            } else if(selected==copy) {
                QGuiApplication::clipboard()->setText(path);
            } else if(selected==trash) {
                moveToGalleryTrash(path);
            }''',"long press project options")
    home=r'''        // Android-only start panel: retain native Krita's document
        // creation/open actions and recent file model.
        widgetCenter->hide();
        widgetRight->hide();
        horizontalLayout_16->setDirection(QBoxLayout::TopToBottom);
        horizontalLayout_16->setContentsMargins(5,5,5,5);
        horizontalLayout_16->setSpacing(8);
        verticalLayout_3->setContentsMargins(7,7,7,7);
        widgetRecentImagesColumn->setMinimumWidth(0);
        widgetRecentImagesColumn->setSizePolicy(
            QSizePolicy::Expanding,QSizePolicy::Expanding);
        auto *homePanel=new QFrame(dropFrameBorder);
        homePanel->setObjectName(QStringLiteral("tabletHomePanel"));
        homePanel->setStyleSheet(
            "QFrame#tabletHomePanel{background:#292c33;"
            "border:1px solid #434956;border-radius:13px;}"
            "QLabel#tabletHomeTitle{color:#fafafa;font-size:26px;font-weight:600;}"
            "QLabel#tabletHomeSubtitle{color:#bbc3d1;font-size:12px;}"
            "QToolButton#tabletHomeAction{background:#39404a;color:#fafafa;"
            "border:1px solid #667181;border-radius:9px;"
            "padding:6px;font-size:13px;font-weight:600;}"
            "QToolButton#tabletHomePrimary{background:#476a85;color:white;"
            "border:1px solid #719cb9;border-radius:9px;"
            "padding:6px;font-size:13px;font-weight:600;}"
            "QToolButton#tabletHomeContinue{background:#343943;color:white;"
            "border:1px solid #606b7c;border-radius:9px;padding:8px;"
            "font-size:12px;text-align:left;}");
        auto *homeLayout=new QVBoxLayout(homePanel);
        homeLayout->setContentsMargins(12,10,12,10);
        homeLayout->setSpacing(8);
        auto *title=new QLabel(i18n("Krita"),homePanel);
        title->setObjectName(QStringLiteral("tabletHomeTitle"));
        homeLayout->addWidget(title);
        auto *subtitle=new QLabel(
            i18n("Start drawing or pick up where you left off."),homePanel);
        subtitle->setObjectName(QStringLiteral("tabletHomeSubtitle"));
        subtitle->setWordWrap(true);
        homeLayout->addWidget(subtitle);
        auto *actions=new QGridLayout;
        actions->setSpacing(8);
        auto makeAction=[homePanel](const QString &text,
                                     const QString &icon,bool primary) {
            auto *button=new QToolButton(homePanel);
            button->setObjectName(primary?
                QStringLiteral("tabletHomePrimary"):
                QStringLiteral("tabletHomeAction"));
            button->setText(text);
            button->setIcon(KisIconUtils::loadIcon(icon));
            button->setIconSize(QSize(24,24));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setMinimumHeight(60);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            return button;
        };
        auto *newCanvas=makeAction(
            i18n("New canvas"),QStringLiteral("document-new"),true);
        auto *openFile=makeAction(
            i18n("Open file"),QStringLiteral("document-open"),false);
        actions->addWidget(newCanvas,0,0);
        actions->addWidget(openFile,0,1);
        actions->setColumnStretch(0,1);
        actions->setColumnStretch(1,1);
        connect(newCanvas,&QToolButton::clicked,this,
                [this]() {slotNewFileClicked();});
        connect(openFile,&QToolButton::clicked,this,
                [this]() {slotOpenFileClicked();});
        horizontalLayout_10->removeWidget(galleryButton);
        galleryButton->setParent(homePanel);
        galleryButton->setObjectName(QStringLiteral("tabletHomeAction"));
        galleryButton->setIcon(KisIconUtils::loadIcon("folder-pictures"));
        galleryButton->setIconSize(QSize(24,24));
        galleryButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        galleryButton->setMinimumHeight(58);
        galleryButton->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        actions->addWidget(galleryButton,1,0,1,2);
        homeLayout->addLayout(actions);
        auto *continueButton=new QToolButton(homePanel);
        continueButton->setObjectName(QStringLiteral("tabletHomeContinue"));
        continueButton->setText(i18n("Continue last project"));
        continueButton->setIcon(KisIconUtils::loadIcon("document-open-recent"));
        continueButton->setIconSize(QSize(23,23));
        continueButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        continueButton->setMinimumHeight(48);
        continueButton->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        homeLayout->addWidget(continueButton);
        const auto updateContinue=[this,continueButton]() {
            const QAbstractItemModel *model=recentDocumentsListView->model();
            const bool available=model && model->rowCount()>0;
            continueButton->setVisible(available);
            if(!available)return;
            QString name=model->index(0,0).data(Qt::DisplayRole).toString();
            if(name.isEmpty())name=i18n("Last project");
            name=QFontMetrics(continueButton->font()).elidedText(
                name,Qt::ElideMiddle,qMax(100,continueButton->width()-80));
            continueButton->setText(i18n("Continue: %1",name));
        };
        connect(continueButton,&QToolButton::clicked,this,[this]() {
            const QAbstractItemModel *model=recentDocumentsListView->model();
            if(model && model->rowCount()>0)
                recentDocumentClicked(model->index(0,0));
        });
        QTimer::singleShot(0,this,[this,updateContinue]() {
            updateContinue();
            if(QAbstractItemModel *model=recentDocumentsListView->model()) {
                connect(model,&QAbstractItemModel::modelReset,this,
                        updateContinue);
                connect(model,&QAbstractItemModel::rowsInserted,this,
                        [updateContinue](const QModelIndex&,int,int) {
                    updateContinue();
                });
                connect(model,&QAbstractItemModel::rowsRemoved,this,
                        [updateContinue](const QModelIndex&,int,int) {
                    updateContinue();
                });
            }
        });
        horizontalLayout_16->insertWidget(0,homePanel);
        recentDocumentsLabel->setText(i18n("Recent work"));
        recentDocumentsListView->setIconSize(QSize(110,110));
        recentDocumentsListView->setGridSize(QSize(156,178));
        recentDocumentsListView->setSpacing(7);
        recentDocumentsListView->setResizeMode(QListView::Adjust);
        recentDocumentsListView->setMovement(QListView::Static);
        recentDocumentsListView->setWrapping(true);
        recentDocumentsListView->setWordWrap(true);
        recentDocumentsListView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        recentDocsStackedWidget->setMinimumHeight(200);
'''
    s=sub(s,'        connect(folderButton, &QPushButton::clicked, this,',
          home+'        connect(folderButton, &QPushButton::clicked, this,',
          "Android main start panel")
    # Consolidate gallery into a single On Device screen. The old selected
    # folder remains an internal fallback source, not a navigation tab.
    s=sub(s,'        galleryLayout->addWidget(galleryTabs);',
'''        galleryLayout->addWidget(galleryTabs);
        galleryTabs->setCurrentIndex(1);
        galleryTabs->tabBar()->hide();''',"single on-device gallery")
    # Hide obsolete controls without changing the tested file scan/permissions
    # logic; users retain access permissions if needed.
    s=sub(s,'        auto *searchRow=new QHBoxLayout;',
'''        galleryControls->removeWidget(folderButton);
        galleryControls->removeWidget(downloadsButton);
        folderButton->hide();
        downloadsButton->hide();
        galleryControls->removeWidget(sortMode);
        galleryControls->removeWidget(refreshButton);
        galleryControls->removeWidget(undoTrash);
        galleryControls->removeWidget(allowFiles);
        galleryControls->addWidget(sortMode,0,1);
        galleryControls->addWidget(refreshButton,0,2);
        galleryControls->addWidget(undoTrash,0,3);
        galleryControls->addWidget(allowFiles,1,0,1,4);
        galleryControls->setColumnStretch(0,1);
        galleryControls->setContentsMargins(0,0,0,0);
        galleryControls->setSpacing(4);
        sortMode->setItemText(0,i18n("Newest"));
        sortMode->setItemText(1,i18n("A-Z"));
        sortMode->setFixedWidth(110);
        sortMode->setMinimumHeight(42);
        refreshButton->setText(QStringLiteral("↻"));
        refreshButton->setAccessibleName(i18n("Refresh projects"));
        refreshButton->setToolTip(i18n("Rescan projects on device"));
        refreshButton->setFixedSize(43,42);
        refreshButton->setStyleSheet("QPushButton{font-size:23px;}");
        undoTrash->setText(i18n("Undo"));
        undoTrash->setToolTip(i18n("Restore last trashed project"));
        undoTrash->setFixedSize(65,42);
        allowFiles->setMinimumHeight(44);
        auto *searchRow=new QHBoxLayout;''',"compact gallery toolbar")
    s=sub(s,'''            undoTrash->setEnabled(!stack.isEmpty() &&
                QFileInfo(stack.last().toMap().value(
                    QStringLiteral("trash")).toString()).isFile());''',
'''            undoTrash->setEnabled(!stack.isEmpty() &&
                QFileInfo(stack.last().toMap().value(
                    QStringLiteral("trash")).toString()).isFile());
            undoTrash->setVisible(undoTrash->isEnabled());''',
          "contextual undo button")
    # Save timestamp for reliable newest-first mode without opening project
    # archives just to sort. Alphabetical mode is case-insensitive.
    s=sub(s,'            item->setData(Qt::UserRole,location);',
'''            item->setData(Qt::UserRole,location);
            item->setData(Qt::UserRole+5,physicalPath.isEmpty() ? 0LL :
                QFileInfo(physicalPath).lastModified().toMSecsSinceEpoch());''',
          "project modification times")
    s=sub(s,'        auto *searchRow=new QHBoxLayout;',
'''        const auto sortDevice=[deviceList,sortMode]() {
            if(deviceList->count()<2)return;
            QList<QListWidgetItem*> sorted;
            sorted.reserve(deviceList->count());
            deviceList->setUpdatesEnabled(false);
            while(deviceList->count())sorted.append(deviceList->takeItem(0));
            std::stable_sort(sorted.begin(),sorted.end(),
                [sortMode](QListWidgetItem *a,QListWidgetItem *b) {
                    if(sortMode->currentIndex()==1) {
                        const QString an=a->data(Qt::UserRole+4).toString();
                        const QString bn=b->data(Qt::UserRole+4).toString();
                        return QString::compare(an.isEmpty()?a->text():an,
                            bn.isEmpty()?b->text():bn,Qt::CaseInsensitive)<0;
                    }
                    const qint64 am=a->data(Qt::UserRole+5).toLongLong();
                    const qint64 bm=b->data(Qt::UserRole+5).toLongLong();
                    return am==bm ?
                        QString::compare(a->text(),b->text(),
                                         Qt::CaseInsensitive)<0 : am>bm;
                });
            for(QListWidgetItem *item:sorted)deviceList->addItem(item);
            deviceList->setUpdatesEnabled(true);
        };
        // Existing incremental device scanner runs first on timeout. When
        // it stops (end of scan), sort the completed list only once.
        connect(scanTimer,&QTimer::timeout,this,[scanTimer,sortDevice]() {
            if(!scanTimer->isActive())sortDevice();
        });
        auto *searchRow=new QHBoxLayout;''',"device sorting")
    path.write_text(s,encoding="utf-8")
    print("Mobile home: quick actions, continue, responsive gallery, search, starred projects")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: apply_home_mobile.py KRITA_SOURCE")
    main(sys.argv[1])
