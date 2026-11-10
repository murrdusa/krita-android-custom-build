/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Krita's native menu structure, rendered as touch-first navigation.
 * Krita continues to own QAction instances and every command handler.
 * Mobile Studio owns only browsing/search and the presentation of menus.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include "touch_scroll.h"
#include <QSet>
#include <QSize>
#include <QString>
#include <QToolButton>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <klocalizedstring.h>
#include "engine_bridge.h"

class MobileCommandBrowser final
{
    struct MenuItem {
        QString name;
        QString path;
        QPointer<QAction> action;
        int parent=-1;
        QVector<int> children;
        bool submenu=false;
    };
    static QString label(QString text)
    {
        text.remove(QLatin1Char('&'));
        text.replace(QLatin1Char('\n'),QLatin1Char(' '));
        return text.simplified();
    }

public:
    static void open(const MobileEngineBridge &engine,QWidget *parent)
    {
        if(!parent)return;

        // Snapshot Qt's live File/Edit/View/Image/Layer/... hierarchy.
        // Only hold QPointer<QAction>, never copy the command handlers.
        QVector<MenuItem> tree;
        tree.append({i18n("Krita menus"),QString(),nullptr,-1,{},true});
        QSet<QAction*> alreadyListed;
        const auto append=[&](int owner,const QString &text,
                              QAction *native,bool folder) {
            const int id=tree.size();
            const QString path=tree.at(owner).path.isEmpty() ?
                text : tree.at(owner).path+QStringLiteral(" / ")+text;
            tree.append({text,path,native,owner,{},folder});
            tree[owner].children.append(id);
            return id;
        };
        std::function<void(QMenu*,int,int,QSet<QMenu*>)> scan;
        scan=[&](QMenu *menu,int owner,int depth,QSet<QMenu*> ancestry) {
            if(!menu || depth>=12 || ancestry.contains(menu))return;
            ancestry.insert(menu);
            for(QAction *a:menu->actions()) {
                if(!a || a->isSeparator())continue;
                QString name=label(a->text());
                if(name.isEmpty() && a->menu())
                    name=label(a->menu()->title());
                if(name.isEmpty())continue;
                QMenu *child=a->menu();
                const int id=append(owner,name,a,child!=nullptr);
                alreadyListed.insert(a);
                if(child)scan(child,id,depth+1,ancestry);
            }
        };
        for(QAction *a:engine.menuRootActions()) {
            if(!a || a->isSeparator() ||
               a->objectName()==QStringLiteral("mobileBrowseCommands"))continue;
            QString name=label(a->text());
            if(name.isEmpty() && a->menu())
                name=label(a->menu()->title());
            if(name.isEmpty())continue;
            const int id=append(0,name,a,a->menu()!=nullptr);
            alreadyListed.insert(a);
            if(a->menu())scan(a->menu(),id,1,{});
        }

        // Certain Krita plugin actions are not exposed in the menubar.
        // Retain access to those under an explicit extra category and
        // include them in the same global search as ordinary menu actions.
        const int extra=append(0,i18n("Other Krita commands"),nullptr,true);
        for(QAction *a:engine.availableActions()) {
            if(!a || alreadyListed.contains(a) || a->isSeparator() ||
               a->menu())continue;
            const QString name=label(a->text());
            if(name.isEmpty())continue;
            append(extra,name,a,false);
        }
        if(tree[extra].children.isEmpty()) {
            tree[0].children.removeAll(extra);
        }

        QDialog dialog(parent);
        dialog.setWindowTitle(i18n("Menus"));
        dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#263746;color:#edf6fc;}"
            "QLabel{color:#edf6fc;font-size:13px;}"
            "QLineEdit{background:#354d61;color:white;"
            "border:1px solid #7c9fb9;border-radius:7px;"
            "min-height:44px;padding:7px;font-size:14px;}"
            "QListWidget{background:#2a3d4d;color:#f1f7fc;"
            "border:0;font-size:14px;outline:0;}"
            "QListWidget::item{min-height:48px;padding:7px;}"
            "QListWidget::item:selected{background:#396f8c;}"
            "QPushButton{background:#42647f;color:white;"
            "border:1px solid #86adc7;border-radius:7px;"
            "min-height:43px;padding:6px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(9,8,9,9);
        layout->setSpacing(6);

        auto *heading=new QLabel(i18n("Krita menus"),&dialog);
        heading->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
        layout->addWidget(heading);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search every menu and command"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *bar=new QHBoxLayout;
        auto *back=new QPushButton(i18n("Back"),&dialog);
        back->setFixedWidth(85);
        auto *breadcrumb=new QLabel(&dialog);
        breadcrumb->setWordWrap(true);
        bar->addWidget(back);
        bar->addWidget(breadcrumb,1);
        layout->addLayout(bar);
        auto *list=new QListWidget(&dialog);
        MobileTouchScroll::configureList(list);
        layout->addWidget(list,1);
        auto *count=new QLabel(&dialog);
        count->setStyleSheet(QStringLiteral("color:#b9ccdc;font-size:12px;"));
        layout->addWidget(count);
        auto *close=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(close);
        QObject::connect(close,&QPushButton::clicked,
                         &dialog,&QDialog::reject);

        int folder=0;
        QPointer<QAction> selected;
        const auto render=[&]() {
            list->clear();
            const QString term=search->text().trimmed();
            const bool searching=!term.isEmpty();
            const QString current=tree.at(folder).path;
            breadcrumb->setText(searching?
                i18n("Search across all menus"):
                current.isEmpty()?i18n("All menus"):current);
            back->setEnabled(searching || folder!=0);
            int matches=0;
            if(searching) {
                // Search across every nested menu, not just the current
                // section. Show full parent paths to disambiguate labels.
                for(int i=1;i<tree.size();++i) {
                    const auto &entry=tree.at(i);
                    if(entry.submenu || !entry.action)continue;
                    if(!entry.path.contains(term,Qt::CaseInsensitive) &&
                       !entry.action->objectName().contains(
                           term,Qt::CaseInsensitive) &&
                       !entry.action->toolTip().contains(
                           term,Qt::CaseInsensitive))continue;
                    auto *item=new QListWidgetItem(entry.path,list);
                    item->setData(Qt::UserRole,i);
                    item->setToolTip(entry.action->objectName());
                    if(!entry.action->icon().isNull())
                        item->setIcon(entry.action->icon());
                    if(!entry.action->isEnabled())
                        item->setForeground(QColor(151,169,185));
                    ++matches;
                }
            } else {
                for(int id:tree.at(folder).children) {
                    const auto &entry=tree.at(id);
                    auto *item=new QListWidgetItem(
                        entry.name+(entry.submenu?QStringLiteral("  ›"):
                                                    QString()),list);
                    item->setData(Qt::UserRole,id);
                    item->setToolTip(entry.path);
                    if(entry.action && !entry.action->icon().isNull())
                        item->setIcon(entry.action->icon());
                    if(!entry.submenu && entry.action &&
                       !entry.action->isEnabled())
                        item->setForeground(QColor(151,169,185));
                    ++matches;
                }
            }
            count->setText(i18np("%1 menu item","%1 menu items",matches));
        };
        QObject::connect(search,&QLineEdit::textChanged,&dialog,
                         [&](const QString&){render();});
        QObject::connect(back,&QPushButton::clicked,&dialog,[&]() {
            if(!search->text().isEmpty()) {
                search->clear();
            } else if(folder!=0) {
                folder=qMax(0,tree.at(folder).parent);
                render();
            }
        });
        QObject::connect(list,&QListWidget::itemClicked,&dialog,
                         [&](QListWidgetItem *item) {
            if(!item)return;
            const int id=item->data(Qt::UserRole).toInt();
            if(id<1 || id>=tree.size())return;
            const auto &entry=tree.at(id);
            if(entry.submenu) {
                folder=id;
                search->clear();
                render();
                return;
            }
            if(!entry.action || !entry.action->isEnabled())return;
            selected=entry.action;
            dialog.accept();
        });
        render();
        const QSize bounds=parent->window()->size();
        dialog.resize(qMax(300,qMin(535,bounds.width()-24)),
                      qMax(350,qMin(725,bounds.height()-42)));
        dialog.exec();

        // Let the chooser close completely before invoking a native Krita
        // command, especially any action which opens a file/dialog.
        if(selected && selected->isEnabled())selected->trigger();
    }
};
#endif // Q_OS_ANDROID
