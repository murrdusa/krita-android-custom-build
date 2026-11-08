#!/usr/bin/env python3
"""Add a safe, on-demand autosave/backup recovery browser to Krita's mobile gallery."""
from pathlib import Path
import sys

def one(source, old, new, label):
    count=source.count(old)
    if count != 1:
        raise RuntimeError(f"Mobile recovery {label}: expected 1 anchor, found {count}")
    return source.replace(old,new,1)

def main(root):
    path=Path(root)/"libs/ui/KisWelcomePageWidget.cpp"
    s=path.read_text(encoding="utf-8")
    s=one(s, '#include <QLineEdit>\n',
          '#include <QLineEdit>\n#include "KisAutoSaveRecoveryDialog.h"\n',
          "native recovery location header")
    s=one(s,
'''        galleryControls->setColumnStretch(0,1);''',
'''        auto *recoverButton=new QPushButton(i18n("Recover"),galleryPage);
        recoverButton->setIcon(KisIconUtils::loadIcon("document-revert"));
        recoverButton->setMinimumHeight(43);
        recoverButton->setMinimumWidth(84);
        recoverButton->setToolTip(i18n("Find autosaves and backup projects"));
        galleryControls->addWidget(recoverButton,0,0);
        galleryControls->setColumnStretch(0,1);''',
          "recovery toolbar control")
    code=r'''        connect(recoverButton,&QPushButton::clicked,this,
                [this,folder]() {
            // This is an explicitly invoked browser, not a continuous
            // background scan of the device. The source files are never
            // changed, renamed, moved or deleted by this workflow.
            QDialog dialog(this);
            dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
            dialog.setWindowTitle(i18n("Recover Krita work"));
            dialog.resize(qMax(300,qMin(445,width()-26)),
                          qMax(405,qMin(675,height()-36)));
            dialog.setStyleSheet(
                "QDialog{background:#29313a;color:#f1f5fb;}"
                "QLabel{color:#e3ebf5;font-size:14px;}"
                "QPushButton{min-height:48px;background:#405569;"
                "border:1px solid #7796ad;border-radius:8px;"
                "padding:6px;color:white;font-size:14px;}"
                "QPushButton#recoverCopy{background:#3c6c85;"
                "border:2px solid #86c1e2;font-weight:600;}"
                "QListWidget{background:#252b34;color:white;border:0;}"
                "QListWidget::item{min-height:56px;padding:7px;}");
            auto *layout=new QVBoxLayout(&dialog);
            layout->setContentsMargins(13,13,13,13);
            layout->setSpacing(8);
            auto *heading=new QLabel(i18n("Recover Krita work"),&dialog);
            heading->setStyleSheet("font-size:20px;font-weight:600;");
            layout->addWidget(heading);
            auto *description=new QLabel(
                i18n("Find autosaves or backup copies. Recovery creates a "
                     "separate KRA file; your original and recovery files "
                     "remain untouched."),&dialog);
            description->setWordWrap(true);
            layout->addWidget(description);
            auto *status=new QLabel(i18n("Scanning common save locations…"),&dialog);
            status->setWordWrap(true);
            layout->addWidget(status);
            auto *list=new QListWidget(&dialog);
            list->setSelectionMode(QAbstractItemView::SingleSelection);
            list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            QScroller::grabGesture(list->viewport(),QScroller::TouchGesture);
            layout->addWidget(list,1);
            auto *recover=new QPushButton(i18n("Open as recovered copy"),&dialog);
            recover->setObjectName(QStringLiteral("recoverCopy"));
            recover->setMinimumHeight(61);
            recover->setEnabled(false);
            layout->addWidget(recover);
            connect(list,&QListWidget::itemSelectionChanged,&dialog,
                    [list,recover]() {
                recover->setEnabled(list->currentItem()!=nullptr);
            });
            auto *close=new QPushButton(i18n("Back to projects"),&dialog);
            close->setMinimumHeight(51);
            layout->addWidget(close);
            connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);

            QStringList roots={
                KisAutoSaveRecoveryDialog::autoSaveLocation(),
                *folder,
                QStringLiteral("/storage/emulated/0/Download"),
                QStringLiteral("/storage/emulated/0/Documents"),
                QStringLiteral("/storage/emulated/0/Pictures/Krita"),
                QStringLiteral("/storage/emulated/0/Krita")
            };
            roots.removeAll(QString());
            roots.removeDuplicates();
            const QStringList patterns={
                QStringLiteral("*-autosave.kra"),
                QStringLiteral(".*-autosave.kra"),
                QStringLiteral("*.kra~"),
                QStringLiteral("*.krz~")
            };
            auto iterator=std::make_shared<std::shared_ptr<QDirIterator>>();
            auto cursor=std::make_shared<int>(0);
            auto seen=std::make_shared<QSet<QString>>();
            auto *scanner=new QTimer(&dialog);
            scanner->setInterval(12);
            connect(scanner,&QTimer::timeout,&dialog,
                    [iterator,cursor,roots,patterns,seen,list,status,scanner]() {
                int batch=0;
                while(batch++<18 && list->count()<140) {
                    if(!*iterator || !(*iterator)->hasNext()) {
                        iterator->reset();
                        while(*cursor<roots.size() &&
                              !QDir(roots.at(*cursor)).exists())
                            ++*cursor;
                        if(*cursor>=roots.size()) {
                            scanner->stop();
                            status->setText(list->count() ?
                                i18np("%1 recoverable file found",
                                      "%1 recoverable files found",
                                      list->count()) :
                                i18n("No autosave or backup files found "
                                     "in accessible save folders."));
                            return;
                        }
                        const QString root=roots.at((*cursor)++);
                        *iterator=std::make_shared<QDirIterator>(
                            root,patterns,
                            QDir::Files|QDir::Readable|QDir::Hidden|
                                QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
                    }
                    if(!(*iterator)->hasNext())continue;
                    const QString path=(*iterator)->next();
                    if(path.contains(QStringLiteral("/.krita-gallery-trash/")) ||
                       seen->contains(path))continue;
                    const QFileInfo file(path);
                    if(!file.isReadable() || file.size()<=0)continue;
                    seen->insert(path);
                    const bool backup=file.fileName().endsWith(QLatin1Char('~'));
                    const QString label=QStringLiteral("%1  %2\n%3")
                        .arg(backup?QStringLiteral("Backup"):QStringLiteral("Autosave"))
                        .arg(file.fileName())
                        .arg(file.lastModified().toString(
                            QStringLiteral("MMM d, yyyy h:mm AP")));
                    auto *entry=new QListWidgetItem(
                        KisIconUtils::loadIcon("document-revert"),label,list);
                    entry->setData(Qt::UserRole,path);
                    entry->setToolTip(path);
                }
                if(list->count()>=140) {
                    scanner->stop();
                    status->setText(
                        i18n("Showing the first 140 recovery files"));
                }
            });
            scanner->start();

            connect(recover,&QPushButton::clicked,&dialog,
                    [this,list,scanner,&dialog]() {
                QListWidgetItem *selected=list->currentItem();
                if(!selected)return;
                const QString source=selected->data(Qt::UserRole).toString();
                if(!QFileInfo(source).isReadable()) {
                    QMessageBox::warning(&dialog,i18n("Recover project"),
                        i18n("This recovery file is no longer readable."));
                    return;
                }
                // Favor a user-visible folder. If Android storage permission
                // is unavailable, fall back to the autosave's directory.
                QString recoveredDir=
                    QStringLiteral("/storage/emulated/0/Documents/Krita/Recovered");
                if(!QDir().mkpath(recoveredDir)) {
                    recoveredDir=QFileInfo(source).absolutePath();
                    if(!QDir(recoveredDir).exists()) {
                        QMessageBox::warning(&dialog,i18n("Recover project"),
                            i18n("Could not create a recovery folder."));
                        return;
                    }
                }
                QString basename=QFileInfo(source).fileName();
                if(basename.startsWith(QLatin1Char('.')))basename.remove(0,1);
                basename.replace(QRegularExpression(
                    QStringLiteral("(?i)-autosave\\.kra$")),QString());
                basename.replace(QRegularExpression(
                    QStringLiteral("(?i)\\.(?:kra|krz)~$")),QString());
                basename.replace(QRegularExpression(
                    QStringLiteral("(?i)\\.(?:kra|krz)$")),QString());
                if(basename.isEmpty())basename=QStringLiteral("krita-project");
                basename.replace(QRegularExpression(
                    QStringLiteral("[^A-Za-z0-9 _.-]")),QStringLiteral("_"));
                QString target;
                const QString suffix=QDateTime::currentDateTime().toString(
                    QStringLiteral("yyyyMMdd-hhmmss"));
                for(int attempt=0;attempt<100;++attempt) {
                    const QString candidate=QDir(recoveredDir).filePath(
                        QStringLiteral("%1-recovered-%2%3.kra")
                            .arg(basename).arg(suffix)
                            .arg(attempt?QStringLiteral("-%1").arg(attempt):
                                         QString()));
                    if(!QFileInfo::exists(candidate)) {
                        target=candidate;break;
                    }
                }
                if(target.isEmpty() || !QFile::copy(source,target)) {
                    QMessageBox::warning(&dialog,i18n("Recover project"),
                        i18n("Could not create the recovered copy. "
                             "The original recovery file is unchanged."));
                    return;
                }
                scanner->stop();
                dialog.accept();
                m_mainWindow->openDocument(target,KisMainWindow::None);
            });
            dialog.exec();
        });
'''
    s=one(s,'''        galleryControls->setColumnStretch(0,1);''',
          '''        galleryControls->setColumnStretch(0,1);'''+code,
          "recovery browser")
    path.write_text(s,encoding="utf-8")
    print("Recovery browser installed: on-demand scan, copy-only restore, non-destructive")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_mobile_recovery.py KRITA_SOURCE")
    main(sys.argv[1])
