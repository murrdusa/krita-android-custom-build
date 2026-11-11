/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Floating toolbar for the independent Krita canvas. Layout entries
 * are stable command IDs. Qt's defaultAction drives every button.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QTimer>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include "touch_scroll.h"

class MobileNativeCommandLayout final : public QObject
{
public:
    using Commands=QMap<QString,QPointer<QAction>>;

    static bool install(QMainWindow *host,QAction *home,const QIcon &settingsIcon,
                        const Commands &commands,const QStringList &defaults,
                        const QList<QAction*> &moreActions,QAction *canvasOnly=nullptr)
    {
        QWidget *canvas=host?host->centralWidget():nullptr;
        if(!canvas || !home || toolbar(canvas))return false;
        auto *layout=new MobileNativeCommandLayout(canvas,commands,defaults);
        auto *row=new QHBoxLayout(layout->m_toolbar);
        row->setContentsMargins(1,1,1,1);
        row->setSpacing(1);
        row->addWidget(makeButton(layout->m_toolbar,home));

        layout->m_scroll=new QScrollArea(layout->m_toolbar);
        layout->m_scroll->setObjectName(QStringLiteral("_mobileNativeCommandScroll"));
        layout->m_scroll->setFrameShape(QFrame::NoFrame);
        layout->m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#_mobileNativeCommandScroll{background:transparent;border:0;}"));
        layout->m_scroll->viewport()->setAutoFillBackground(false);
        MobileTouchScroll::configure(layout->m_scroll);
        row->addWidget(layout->m_scroll,1);

        auto *menu=new QMenu(layout->m_toolbar);
        menu->setTitle(QObject::tr("Settings and commands"));
        menu->setIcon(settingsIcon);
        const QString popupStyle=QStringLiteral(
            "QMenu{background:#252529;color:#f4f4f5;border:1px solid #55555c;padding:3px;}"
            "QMenu::item{color:#f4f4f5;padding:8px 14px;min-height:28px;}"
            "QMenu::item:selected{background:#45454f;}"
            "QMenu::item:disabled{color:#9a9aa2;}");
        menu->setStyleSheet(popupStyle);
        auto *more=makeButton(layout->m_toolbar,menu->menuAction());
        more->setObjectName(QStringLiteral("_mobileNativeMoreButton"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setMenu(menu);
        row->addWidget(more);

        auto *edit=menu->addAction(QObject::tr("Edit Toolbar…"));
        QObject::connect(edit,&QAction::triggered,layout,[layout]() {
            QTimer::singleShot(0,layout,[layout]() {layout->editToolbar();});
        });
        auto *drawing=menu->addMenu(QObject::tr("Drawing controls"));
        drawing->setStyleSheet(popupStyle);
        for(const auto &id:defaults)
            if(auto action=commands.value(id))drawing->addAction(action);
        for(QAction *action:moreActions) {
            if(!action)continue;
            if(QMenu *submenu=action->menu())submenu->setStyleSheet(popupStyle);
            menu->addAction(action);
        }

        if(canvasOnly) {
            auto *restore=new QToolButton(canvas);
            layout->m_restore=restore;
            restore->setObjectName(QStringLiteral("_mobileNativeRestoreControls"));
            restore->setText(QObject::tr("Show Controls"));
            restore->setMinimumSize(110,44);
            restore->setStyleSheet(QStringLiteral(
                "QToolButton{background:#252529;color:#f4f4f5;"
                "border:1px solid #55555c;border-radius:8px;}"));
            restore->adjustSize();
            restore->move(2,2);
            restore->setVisible(canvasOnly->isChecked());
            QObject::connect(restore,&QToolButton::clicked,canvasOnly,
                             [canvasOnly]() {canvasOnly->setChecked(false);});
            QObject::connect(canvasOnly,&QAction::toggled,layout,
                             [layout,restore](bool enabled) {
                layout->m_toolbar->setVisible(!enabled);
                restore->setVisible(enabled);
                layout->position();
            });
        }
        canvas->installEventFilter(layout);
        layout->m_toolbar->setVisible(!canvasOnly || !canvasOnly->isChecked());
        layout->rebuild();
        return true;
    }

    static int panelTopInset(QWidget *canvas)
    {
        auto *bar=toolbar(canvas);
        return bar && !bar->isHidden()?bar->geometry().bottom()+3:2;
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parent() &&
           (event->type()==QEvent::Resize || event->type()==QEvent::Show))
            position();
        return false;
    }

private:
    MobileNativeCommandLayout(QWidget *canvas,const Commands &commands,
                              const QStringList &defaults)
        : QObject(canvas),m_commands(commands),m_defaults(defaults),
          m_ids(QSettings().value(settingsKey(),defaults).toStringList())
    {
        m_toolbar=new QFrame(canvas);
        m_toolbar->setObjectName(QStringLiteral("_mobileNativeToolbar"));
        m_toolbar->setStyleSheet(QStringLiteral(
            "QFrame#_mobileNativeToolbar{background:rgba(26,29,34,168);"
            "border:1px solid rgba(255,255,255,65);border-radius:8px;}"
            "QToolButton{background:transparent;color:white;border:1px solid transparent;"
            "border-radius:5px;padding:0;}"
            "QToolButton:pressed,QToolButton:checked{background:#234e62;border-color:#a7def1;}"
            "QToolButton:disabled{color:#b6b8bc;}"
            "QToolButton::menu-indicator{image:none;width:0;}"));
    }

    static QString settingsKey()
    {
        return QStringLiteral("MobileStudio/primaryToolbarCommands");
    }

    static QFrame *toolbar(QWidget *canvas)
    {
        return canvas?canvas->findChild<QFrame*>(
            QStringLiteral("_mobileNativeToolbar"),Qt::FindDirectChildrenOnly):nullptr;
    }

    static QToolButton *makeButton(QWidget *parent,QAction *action)
    {
        auto *button=new QToolButton(parent);
        button->setDefaultAction(action);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(24,24));
        const auto sync=[button,action]() {
            QString label=action->text();
            label.remove(QLatin1Char('&'));
            button->setAccessibleName(label);
            button->setVisible(action->isVisible());
            button->setToolButtonStyle(action->icon().isNull()?
                Qt::ToolButtonTextOnly:Qt::ToolButtonIconOnly);
            const int width=action->icon().isNull()?
                qBound(44,button->fontMetrics().horizontalAdvance(label)+10,132):44;
            button->setFixedSize(width,44);
        };
        QObject::connect(action,&QAction::changed,button,sync);
        sync();
        return button;
    }

    void position()
    {
        auto *canvas=static_cast<QWidget*>(parent());
        const int edge=2;
        const auto *row=m_toolbar->layout();
        const int pinnedWidth=row->itemAt(0)->widget()->width()+
                              row->itemAt(2)->widget()->width();
        const int width=qMin(pinnedWidth+m_contentWidth+6,qMax(0,canvas->width()-2*edge));
        m_toolbar->setGeometry(edge,edge,width,48);
        m_toolbar->raise();
        if(m_restore)m_restore->raise();
    }

    void rebuild()
    {
        auto *strip=new QWidget;
        strip->setAttribute(Qt::WA_TranslucentBackground);
        auto *row=new QHBoxLayout(strip);
        row->setContentsMargins(0,0,0,0);
        row->setSpacing(1);
        const auto resizeStrip=[this,strip,row]() {
            if(m_scroll->widget()!=strip)return;
            m_contentWidth=row->sizeHint().width();
            strip->resize(m_contentWidth,44);
            m_scroll->setVisible(m_contentWidth>0);
            position();
        };
        for(const auto &id:m_ids) {
            auto action=m_commands.value(id);
            if(!action)continue;
            auto *button=makeButton(strip,action);
            row->addWidget(button);
            QObject::connect(action,&QAction::changed,button,[resizeStrip,row,this]() {
                if(row->sizeHint().width()!=m_contentWidth)resizeStrip();
            });
        }
        m_scroll->setWidget(strip);
        m_scroll->setMinimumSize(0,44);
        resizeStrip();
    }

    QString chooseCommand(QWidget *parent,const QStringList &existing)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Add command"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(QObject::tr("Search commands"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:42px;padding:4px;}"));
        MobileTouchScroll::configureList(list);
        layout->addWidget(list,1);
        for(auto command=m_commands.cbegin();command!=m_commands.cend();++command) {
            if(!command.value() || existing.contains(command.key()))continue;
            QString name=command.value()->text();
            name.remove(QLatin1Char('&'));
            if(name.isEmpty())continue;
            auto *item=new QListWidgetItem(command.value()->icon(),name,list);
            item->setData(Qt::UserRole,command.key());
        }
        list->sortItems();
        QObject::connect(search,&QLineEdit::textChanged,&dialog,[list](const QString &term) {
            for(int i=0;i<list->count();++i) {
                auto *item=list->item(i);
                item->setHidden(!item->text().contains(term,Qt::CaseInsensitive) &&
                    !item->data(Qt::UserRole).toString().contains(term,Qt::CaseInsensitive));
            }
        });
        QString selected;
        QObject::connect(list,&QListWidget::itemClicked,&dialog,[&](QListWidgetItem *item) {
            selected=item->data(Qt::UserRole).toString();
            dialog.accept();
        });
        auto *cancel=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(cancel);
        QObject::connect(cancel,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.resize(parent->width(),parent->height());
        return dialog.exec()==QDialog::Accepted?selected:QString();
    }

    void editToolbar()
    {
        if(m_editing)return;
        QScopedValueRollback<bool> editing(m_editing,true);
        QDialog dialog(m_toolbar->window());
        dialog.setWindowTitle(QObject::tr("Edit Toolbar"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:42px;padding:4px;}"));
        MobileTouchScroll::configureList(list);
        layout->addWidget(list,1);
        QStringList ids=m_ids;
        const auto refresh=[&](int index) {
            list->clear();
            for(const auto &id:ids) {
                if(auto action=m_commands.value(id)) {
                    QString text=action->text();
                    text.remove(QLatin1Char('&'));
                    new QListWidgetItem(action->icon(),text,list);
                } else {
                    new QListWidgetItem(id,list);
                }
            }
            list->setCurrentRow(qBound(-1,index,list->count()-1));
        };
        auto *row=new QHBoxLayout;
        const auto addButton=[&](const QString &label,auto callback) {
            auto *button=new QPushButton(label,&dialog);
            button->setMinimumHeight(44);
            row->addWidget(button);
            QObject::connect(button,&QPushButton::clicked,&dialog,callback);
            return button;
        };
        addButton(QObject::tr("Add"),[&]() {
            const QString id=chooseCommand(&dialog,ids);
            if(id.isEmpty())return;
            ids.append(id);
            refresh(ids.size()-1);
        });
        auto *remove=addButton(QObject::tr("Remove"),[&]() {
            const int i=list->currentRow();
            if(i<0)return;
            ids.removeAt(i);
            refresh(i);
        });
        const auto move=[&](int step) {
            const int i=list->currentRow(),target=i+step;
            if(i<0 || target<0 || target>=ids.size())return;
            ids.move(i,target);
            refresh(target);
        };
        auto *up=addButton(QObject::tr("Up"),[&]() {move(-1);});
        auto *down=addButton(QObject::tr("Down"),[&]() {move(1);});
        QObject::connect(list,&QListWidget::currentRowChanged,&dialog,[&](int i) {
            remove->setEnabled(i>=0);
            up->setEnabled(i>0);
            down->setEnabled(i>=0 && i+1<ids.size());
        });
        layout->addLayout(row);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        auto *reset=buttons->addButton(QObject::tr("Reset"),QDialogButtonBox::ResetRole);
        QObject::connect(reset,&QPushButton::clicked,&dialog,[&]() {
            ids=m_defaults;
            refresh(0);
        });
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);
        refresh(0);
        dialog.resize(qMin(400,m_toolbar->window()->width()),qMin(540,m_toolbar->window()->height()));
        if(dialog.exec()!=QDialog::Accepted)return;
        m_ids=ids;
        QSettings().setValue(settingsKey(),m_ids);
        rebuild();
    }

    Commands m_commands;
    QStringList m_defaults,m_ids;
    QFrame *m_toolbar=nullptr;
    QScrollArea *m_scroll=nullptr;
    QToolButton *m_restore=nullptr;
    int m_contentWidth=0;
    bool m_editing=false;
};
#endif
