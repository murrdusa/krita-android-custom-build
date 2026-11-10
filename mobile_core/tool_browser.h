/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio tool browser. This is presentation only:
 * every tool comes from Krita's live KoToolManager registry, and activating
 * one selects Krita's real KoToolBase implementation and input handling.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScroller>
#include <QSettings>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>
#include <klocalizedstring.h>

#include "engine_bridge.h"

class MobileToolBrowser final {
public:
    // Returns true when the user asks to open the active tool's options.
    static bool open(const MobileEngineBridge &engine,QWidget *parent,
                     bool offerOptions=false)
    {
        if(!parent || !engine.hasDocument())return false;
        const QVector<MobileEngineBridge::MobileTool> available=engine.tools();
        QDialog dialog(parent);
        dialog.setWindowTitle(i18n("Tools"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#283746;color:#f2f7fd;}"
            "QLabel{color:#e9f4ff;font-size:14px;}"
            "QLineEdit,QComboBox{background:#354b5f;color:white;"
            "border:1px solid #749bb8;border-radius:7px;"
            "min-height:44px;padding:6px;font-size:14px;}"
            "QListWidget{background:#2c3e50;color:white;"
            "border:1px solid #6e8ca4;font-size:14px;}"
            "QListWidget::item{min-height:52px;padding:6px;}"
            "QListWidget::item:selected{background:#36708c;}"
            "QPushButton{background:#446781;color:white;"
            "border:1px solid #8cb3cc;border-radius:8px;"
            "min-height:45px;padding:6px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(7);

        auto *header=new QLabel(i18n("Choose a Krita tool"),&dialog);
        header->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
        layout->addWidget(header);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search brush, select, move, transform…"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *category=new QComboBox(&dialog);
        category->addItem(i18n("All tools"),QString());
        QStringList sections;
        for(const auto &tool:available) {
            if(!tool.section.trimmed().isEmpty() &&
               !sections.contains(tool.section))
                sections.append(tool.section);
        }
        sections.sort(Qt::CaseInsensitive);
        for(const QString &section:sections)
            category->addItem(section,section);
        layout->addWidget(category);
        auto *list=new QListWidget(&dialog);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setIconSize(QSize(28,28));
        list->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(list->viewport(),
                               QScroller::LeftMouseButtonGesture);
        layout->addWidget(list,1);
        auto *count=new QLabel(&dialog);
        count->setWordWrap(true);
        layout->addWidget(count);
        auto *buttons=new QHBoxLayout;
        auto *done=new QPushButton(i18n("Close"),&dialog);
        buttons->addWidget(done);
        QObject::connect(done,&QPushButton::clicked,&dialog,&QDialog::reject);
        constexpr int showOptions=2;
        if(offerOptions) {
            auto *options=new QPushButton(i18n("Tool Options"),&dialog);
            buttons->addWidget(options);
            QObject::connect(options,&QPushButton::clicked,&dialog,
                             [&dialog]() { dialog.done(showOptions); });
        }
        layout->addLayout(buttons);

        const QString current=engine.activeTool();
        QSettings settings;
        const QStringList recent=settings.value(
            QStringLiteral("MobileStudio/recentTools")).toStringList();
        QVector<int> order;
        order.reserve(available.size());
        // Keep the active tool above recents and native section order.
        for(int i=0;i<available.size();++i)
            if(available.at(i).id==current)order.append(i);
        for(const QString &id:recent)
            for(int i=0;i<available.size();++i)
                if(available.at(i).id==id && !order.contains(i))order.append(i);
        for(int i=0;i<available.size();++i)
            if(!order.contains(i))order.append(i);

        const auto refill=[&]() {
            list->clear();
            const QString phrase=search->text().trimmed();
            const QString section=category->currentData().toString();
            int matches=0;
            for(int i:order) {
                const auto &tool=available.at(i);
                if(!section.isEmpty() && tool.section!=section)continue;
                if(!phrase.isEmpty() &&
                   !tool.label.contains(phrase,Qt::CaseInsensitive) &&
                   !tool.description.contains(phrase,Qt::CaseInsensitive) &&
                   !tool.id.contains(phrase,Qt::CaseInsensitive))continue;
                const bool active=tool.id==current;
                const bool used=recent.contains(tool.id);
                const QString prefix=active?QStringLiteral("●  "):
                    used?QStringLiteral("•  "):QStringLiteral("   ");
                QString label=tool.label.trimmed();
                if(label.isEmpty())label=tool.id;
                auto *item=new QListWidgetItem(prefix+label,list);
                item->setData(Qt::UserRole,i);
                item->setToolTip(tool.description+"\n"+tool.id);
                if(!tool.iconName.isEmpty())
                    item->setIcon(QIcon::fromTheme(tool.iconName));
                if(active) {
                    item->setBackground(QColor(43,108,140));
                    list->setCurrentItem(item);
                }
                ++matches;
            }
            count->setText(i18np("%1 tool available", "%1 tools available",
                                 matches));
        };
        QObject::connect(search,&QLineEdit::textChanged,&dialog,
                         [&refill](const QString&){refill();});
        QObject::connect(category,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         &dialog,[&refill](int){refill();});
        QString requested;
        QObject::connect(list,&QListWidget::itemClicked,&dialog,
                         [&](QListWidgetItem *item) {
            if(!item)return;
            const int index=item->data(Qt::UserRole).toInt();
            if(index<0 || index>=available.size())return;
            requested=available.at(index).id;
            dialog.accept();
        });
        refill();
        const QSize bounds=parent->window()->size();
        dialog.resize(qMax(300,qMin(540,bounds.width()-24)),
                      qMax(340,qMin(750,bounds.height()-50)));
        list->setFocus();
        const int result=dialog.exec();
        if(result==showOptions)return true;
        if(result!=QDialog::Accepted || requested.isEmpty())return false;
        if(engine.selectTool(requested)) {
            QStringList history=settings.value(
                QStringLiteral("MobileStudio/recentTools")).toStringList();
            history.removeAll(requested);
            history.prepend(requested);
            while(history.size()>8)history.removeLast();
            settings.setValue(QStringLiteral("MobileStudio/recentTools"),
                              history);
        }
        return false;
    }
};
#endif // Q_OS_ANDROID
