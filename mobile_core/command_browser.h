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
#include "transient_panel.h"
#include <memory>
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
#include "view_identity_guard.h"

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
        for(auto *existing:parent->findChildren<QFrame*>(QStringLiteral("mobileCommandBrowser"),
                                                        Qt::FindDirectChildrenOnly)) {
            if(existing->isVisible()) {existing->raise();return;}
        }

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
                if(!a || a->isSeparator() || a->objectName()==QStringLiteral("mobileBrowseCommands"))
                    continue;
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

        auto *panel=new MobileTransientPanel(parent);
        panel->setObjectName(QStringLiteral("mobileCommandBrowser"));
        panel->setAccessibleName(i18n("All Krita Commands"));
        panel->anchorTo(nullptr,QSize(535,725));
        auto *layout=new QVBoxLayout(panel);
        layout->setContentsMargins(9,8,9,9);
        layout->setSpacing(6);

        auto *navigation=new QHBoxLayout;
        auto *back=new QPushButton(i18n("Back"),panel);
        auto *breadcrumb=new QLabel(panel);
        breadcrumb->setWordWrap(true);
        navigation->addWidget(back);
        navigation->addWidget(breadcrumb,1);
        layout->addLayout(navigation);
        auto *search=new QLineEdit(panel);
        search->setPlaceholderText(i18n("Search commands"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *list=new QListWidget(panel);
        list->setUniformItemSizes(true);
        MobileTouchScroll::configureList(list);
        layout->addWidget(list,1);
        const auto catalog=std::make_shared<const QVector<MenuItem>>(std::move(tree));
        const auto folder=std::make_shared<int>(0);
        const auto view=MobileViewIdentityGuard::uniqueActive(engine.openDocuments());
        const auto render=[catalog,folder,list,search,breadcrumb,back]() {
            const auto &tree=*catalog;
            list->clear();
            const QString term=search->text().trimmed();
            const bool searching=!term.isEmpty();
            const QString current=tree.at(*folder).path;
            breadcrumb->setText(searching?
                i18n("Search across all menus"):
                current.isEmpty()?i18n("All menus"):current);
            back->setEnabled(searching || *folder!=0);
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
                    item->setSizeHint(QSize(0,42));
                    if(!entry.action->isEnabled())
                        item->setForeground(QColor(151,169,185));
                }
            } else {
                for(int id:tree.at(*folder).children) {
                    const auto &entry=tree.at(id);
                    auto *item=new QListWidgetItem(
                        entry.name+(entry.submenu?QStringLiteral("  ›"):
                                                    QString()),list);
                    item->setData(Qt::UserRole,id);
                    item->setToolTip(entry.path);
                    item->setSizeHint(QSize(0,42));
                    if(!entry.submenu && entry.action &&
                       !entry.action->isEnabled())
                        item->setForeground(QColor(151,169,185));
                }
            }
        };
        QObject::connect(search,&QLineEdit::textChanged,panel,render);
        QObject::connect(back,&QPushButton::clicked,panel,[catalog,folder,search,render]() {
            if(!search->text().isEmpty())search->clear();
            else if(*folder!=0) {
                *folder=qMax(0,catalog->at(*folder).parent);
                render();
            }
        });
        QObject::connect(list,&QListWidget::itemClicked,panel,
                         [panel,engine,view,catalog,folder,search,render](QListWidgetItem *item) {
            if(!item)return;
            const int id=item->data(Qt::UserRole).toInt();
            if(id<1 || id>=catalog->size())return;
            const auto &entry=catalog->at(id);
            if(entry.submenu) {
                *folder=id;
                if(search->text().isEmpty())render();
                else search->clear();
                return;
            }
            const auto action=entry.action;
            if(!action || !action->isEnabled())return;
            const QPointer<QWidget> host(panel->parentWidget());
            panel->close();
            if(host)QTimer::singleShot(0,host,[engine,view,action]() {
                if(action && action->isEnabled() && view &&
                   view==MobileViewIdentityGuard::uniqueActive(engine.openDocuments()))
                    action->trigger();
            });
        });
        render();
        panel->present();
    }
};
#endif // Q_OS_ANDROID
