/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Document launcher for the standalone Studio window. File selection and
 * document lifetime stay with Krita's existing document and view hosts.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScroller>
#include <QTimer>
#include <QVBoxLayout>
#include <KisRecentFilesManager.h>
#include <klocalizedstring.h>

#include "document_workflow.h"

class MobileDocumentHome final : public QFrame
{
public:
    MobileDocumentHome(const MobileEngineBridge &engine,
                       QSharedPointer<MobileDocumentHostPort> files,
                       QWidget *parent)
        : QFrame(parent),m_engine(engine),m_files(std::move(files))
    {
        setObjectName(QStringLiteral("mobileDocumentHome"));
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet(QStringLiteral(
            "QFrame#mobileDocumentHome{background:#252529;color:#f4f4f5;}"
            "QLabel{color:#f4f4f5;}"
            "QPushButton{background:#36363c;color:#f4f4f5;border:1px solid #56565d;"
            "border-radius:8px;min-height:46px;padding:4px;}"
            "QPushButton:checked,QListWidget::item:selected{background:#505061;}"
            "QListWidget{background:#29292e;color:#f4f4f5;border:0;}"
            "QListWidget::item{min-height:48px;padding:5px;}"));
        auto *layout=new QVBoxLayout(this);
        layout->setContentsMargins(16,16,16,16);
        layout->setSpacing(8);
        auto *title=new QLabel(i18n("Krita Mobile Studio"),this);
        title->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;"));
        layout->addWidget(title);
        auto *fileRow=new QHBoxLayout;
        auto *create=new QPushButton(i18n("New Canvas"),this);
        auto *browse=new QPushButton(i18n("Browse Files"),this);
        fileRow->addWidget(create);
        fileRow->addWidget(browse);
        layout->addLayout(fileRow);
        auto *tabs=new QHBoxLayout;
        m_recent=new QPushButton(i18n("Recent"),this);
        m_open=new QPushButton(i18n("Open Canvases"),this);
        for(QPushButton *button:{m_recent,m_open}) {
            button->setCheckable(true);
            tabs->addWidget(button);
        }
        layout->addLayout(tabs);

        m_list=new QListWidget(this);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(m_list->viewport(),QScroller::LeftMouseButtonGesture);
        layout->addWidget(m_list,1);

        connect(create,&QPushButton::clicked,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                if(isVisible() && MobileDocumentWorkflow::create(m_engine,window()))hide();
            });
        });
        connect(browse,&QPushButton::clicked,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                if(isVisible() && m_engine.openFiles(window())>0)hide();
            });
        });
        connect(m_recent,&QPushButton::clicked,this,[this]() { showList(false); });
        connect(m_open,&QPushButton::clicked,this,[this]() { showList(true); });
        connect(m_list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item) {
            if(!item || !item->flags().testFlag(Qt::ItemIsEnabled))return;
            const int row=item->data(Qt::UserRole).toInt();
            if(m_showOpen) {
                const QPointer<QObject> view=m_views.value(row);
                if(view && m_engine.activateDocumentView(view))hide();
            } else {
                const QUrl url=m_urls.value(row);
                QTimer::singleShot(0,this,[this,url]() {
                    if(!isVisible() || !m_files || url.isEmpty())return;
                    const QString path=url.isLocalFile()?url.toLocalFile():url.toString();
                    for(const auto &entry:m_engine.openDocuments()) {
                        const auto *doc=qobject_cast<KisDocument*>(entry.document.data());
                        if(doc && doc->path()==path &&
                           m_engine.activateDocumentView(entry.nativeView)) {
                            hide();
                            return;
                        }
                    }
                    if(m_files->openFile(path))hide();
                    else QMessageBox::warning(this,i18n("Open failed"),
                        i18n("Could not open this drawing. You can locate it with Browse Files."));
                });
            }
        });
        parent->installEventFilter(this);
        hide();
    }

    void showHome()
    {
        showList(false);
        setGeometry(parentWidget()->rect());
        show();
        raise();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parentWidget() && event->type()==QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return QFrame::eventFilter(watched,event);
    }

private:
    void showList(bool open)
    {
        m_showOpen=open;
        m_recent->setChecked(!open);
        m_open->setChecked(open);
        m_list->clear();
        m_urls.clear();
        m_views.clear();
        if(open) {
            for(const auto &entry:m_engine.openDocuments()) {
                auto *item=new QListWidgetItem(entry.title+
                    (entry.modified?QStringLiteral(" *"):QString()),m_list);
                item->setData(Qt::UserRole,m_views.size());
                m_views.append(entry.nativeView);
            }
        } else {
            const auto entries=KisRecentFilesManager::instance()->recentFiles();
            for(auto it=entries.crbegin();it!=entries.crend();++it) {
                if(it->m_url.isEmpty())continue;
                auto *item=new QListWidgetItem(it->m_displayName,m_list);
                item->setData(Qt::UserRole,m_urls.size());
                m_urls.append(it->m_url);
            }
        }
        if(m_list->count()==0) {
            auto *empty=new QListWidgetItem(
                open?i18n("No open canvases"):i18n("No recent drawings"),m_list);
            empty->setFlags(Qt::NoItemFlags);
        }
    }

    MobileEngineBridge m_engine;
    QSharedPointer<MobileDocumentHostPort> m_files;
    QListWidget *m_list=nullptr;
    QPushButton *m_recent=nullptr,*m_open=nullptr;
    QVector<QUrl> m_urls;
    QVector<QPointer<QObject>> m_views;
    bool m_showOpen=false;
};
#endif
