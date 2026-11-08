/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio configurable quick actions. Actions are references to
 * Krita's existing QAction/tool registry, or to explicit mobile workflows.
 * There is no second command implementation or shadow document model.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <klocalizedstring.h>

#include "engine_bridge.h"

class MobileQuickActions final : public QFrame
{
public:
    struct Entry {
        QString id;
        QString title;
        QString details;
        QString group;
        bool frequent=false;
        bool enabled=true;
    };

    MobileQuickActions(const MobileEngineBridge &engine,
                       std::function<void(const QString&)> dispatch,
                       QWidget *parent=nullptr)
        : QFrame(parent),m_engine(engine),m_dispatch(std::move(dispatch))
    {
        setObjectName(QStringLiteral("MobileQuickActionStrip"));
        setStyleSheet(
            "QFrame#MobileQuickActionStrip{background:#263543;"
            "border-top:1px solid #57738a;}"
            "QToolButton,QPushButton{background:#405b73;color:#f2f8ff;"
            "border:1px solid #6d90ab;border-radius:7px;"
            "min-height:43px;padding:5px;font-size:13px;}"
            "QToolButton:pressed,QPushButton:pressed{background:#527995;}"
            "QToolButton:disabled{color:#95a5b2;background:#334758;}");
        auto *outer=new QHBoxLayout(this);
        outer->setContentsMargins(4,4,4,4);
        outer->setSpacing(4);
        m_scroll=new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        m_scroll->setFixedHeight(52);
        auto *content=new QWidget(m_scroll);
        m_row=new QHBoxLayout(content);
        m_row->setContentsMargins(0,0,0,0);
        m_row->setSpacing(4);
        m_scroll->setWidget(content);
        // Touch swipes scroll the horizontal quick strip. The editor uses
        // a separate list so dragging buttons cannot activate commands.
        QScroller::grabGesture(m_scroll->viewport(),QScroller::TouchGesture);
        outer->addWidget(m_scroll,1);

        auto *add=new QToolButton(this);
        add->setText(i18n("+ Add"));
        add->setToolTip(i18n("Add a frequent command, search all commands, or add a Krita tool"));
        add->setMinimumWidth(65);
        connect(add,&QToolButton::clicked,this,[this](){addAction();});
        outer->addWidget(add);

        auto *edit=new QToolButton(this);
        edit->setText(i18n("Edit"));
        edit->setToolTip(i18n("Reorder or remove Quick Actions"));
        edit->setMinimumWidth(54);
        connect(edit,&QToolButton::clicked,this,[this](){editActions();});
        outer->addWidget(edit);

        load();
        rebuild();
    }

    void refresh()
    {
        for(const auto &widget:m_buttons) {
            if(!widget.second)continue;
            widget.second->setEnabled(isEnabled(widget.first));
        }
    }

private:
    static QString settingsKey() {
        return QStringLiteral("MobileStudio/quickActions/v1");
    }
    static QStringList defaults()
    {
        return {QStringLiteral("mobile:new"),
                QStringLiteral("mobile:open"),
                QStringLiteral("mobile:save"),
                QStringLiteral("mobile:undo"),
                QStringLiteral("mobile:redo"),
                QStringLiteral("mobile:layers"),
                QStringLiteral("mobile:color"),
                QStringLiteral("mobile:more")};
    }
    static QString cleanLabel(QString text)
    {
        text.remove(QLatin1Char('&'));
        text.replace(QLatin1Char('\n'),QLatin1Char(' '));
        return text.simplified();
    }
    static QVector<Entry> mobileEntries()
    {
        return {
            {QStringLiteral("mobile:new"),i18n("New"),i18n("Create a Krita canvas"),
             i18n("Mobile"),true,true},
            {QStringLiteral("mobile:open"),i18n("Open"),i18n("Open a drawing"),
             i18n("Mobile"),true,true},
            {QStringLiteral("mobile:save"),i18n("Save"),i18n("Save current document"),
             i18n("Mobile"),true,true},
            {QStringLiteral("mobile:save_as"),i18n("Save As"),
             i18n("Save another file"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:undo"),i18n("Undo"),
             i18n("Undo last Krita change"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:redo"),i18n("Redo"),
             i18n("Redo last Krita change"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:layers"),i18n("Layers"),
             i18n("Toggle live layer panel"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:color"),i18n("Color"),
             i18n("Choose paint color"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:tools"),i18n("Tools"),
             i18n("Choose Krita's native tools"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:canvases"),i18n("Canvases"),
             i18n("Switch open documents"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:close"),i18n("Close Canvas"),
             i18n("Krita will prompt to save unsaved changes"),
             i18n("Mobile"),false,true},
            {QStringLiteral("mobile:commands"),i18n("Commands"),
             i18n("Search all Krita commands"),i18n("Mobile"),true,true},
            {QStringLiteral("mobile:more"),i18n("More"),
             i18n("Open mobile and legacy menus"),i18n("Mobile"),true,true}
        };
    }
    QVector<Entry> catalog() const
    {
        QVector<Entry> result=mobileEntries();
        // The native action registry includes commands from every
        // Krita plugin, including ones unavailable in the active tool.
        // Show disabled actions for configuring ahead of time; do not
        // execute them until Krita reports them enabled.
        for(QAction *a:m_engine.availableActions()) {
            if(!a || a->isSeparator() || a->menu() ||
               a->objectName().isEmpty() ||
               a->text().trimmed().isEmpty())continue;
            const QString name=cleanLabel(a->text());
            if(name.isEmpty())continue;
            result.append({QStringLiteral("native:")+a->objectName(),
                name,QStringLiteral("Krita action: ")+a->objectName(),
                i18n("Krita commands"),false,a->isEnabled()});
        }
        for(const auto &t:m_engine.tools()) {
            QString name=cleanLabel(t.label);
            if(name.isEmpty())name=t.id;
            result.append({QStringLiteral("tool:")+t.id,
                name,t.description+QStringLiteral(" — ")+t.id,
                i18n("Krita tools"),false,true});
        }
        return result;
    }
    static bool mobileNeedsDocument(const QString &id)
    {
        return id==QStringLiteral("mobile:save") ||
               id==QStringLiteral("mobile:save_as") ||
               id==QStringLiteral("mobile:undo") ||
               id==QStringLiteral("mobile:redo") ||
               id==QStringLiteral("mobile:layers") ||
               id==QStringLiteral("mobile:color") ||
               id==QStringLiteral("mobile:tools") ||
               id==QStringLiteral("mobile:close");
    }
    bool isEnabled(const QString &id) const
    {
        if(id.startsWith(QStringLiteral("native:"))) {
            QAction *a=m_engine.action(id.mid(7));
            return a && a->isEnabled();
        }
        if(id.startsWith(QStringLiteral("tool:")))
            return m_engine.hasDocument();
        if(id.startsWith(QStringLiteral("mobile:")))
            return !mobileNeedsDocument(id) || m_engine.hasDocument();
        return false;
    }
    QString label(const QString &id) const
    {
        for(const auto &entry:mobileEntries())
            if(entry.id==id)return entry.title;
        if(id.startsWith(QStringLiteral("native:"))) {
            QAction *a=m_engine.action(id.mid(7));
            return a?cleanLabel(a->text()):id.mid(7);
        }
        if(id.startsWith(QStringLiteral("tool:"))) {
            for(const auto &t:m_engine.tools())
                if(QStringLiteral("tool:")+t.id==id)
                    return cleanLabel(t.label);
            return id.mid(5);
        }
        return id;
    }
    void load()
    {
        QSettings settings;
        QStringList fromSettings=settings.value(
            settingsKey(),defaults()).toStringList();
        m_ids.clear();
        for(const QString &id:fromSettings) {
            if(id.isEmpty() || m_ids.contains(id) || m_ids.size()>=24)continue;
            if(id.startsWith(QStringLiteral("mobile:")) ||
               id.startsWith(QStringLiteral("native:")) ||
               id.startsWith(QStringLiteral("tool:")))
                m_ids.append(id);
        }
        // Respect an explicitly empty toolbar; the Add/Edit buttons stay.
    }
    void save()
    {
        QSettings settings;
        settings.setValue(settingsKey(),m_ids);
        settings.sync();
    }
    void rebuild()
    {
        m_buttons.clear();
        while(QLayoutItem *item=m_row->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        for(const QString &id:m_ids) {
            auto *button=new QToolButton(m_scroll->widget());
            button->setText(label(id));
            button->setToolTip(id);
            button->setMinimumWidth(76);
            button->setMaximumWidth(164);
            button->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
            connect(button,&QToolButton::clicked,this,[this,id]() {
                if(isEnabled(id) && m_dispatch)m_dispatch(id);
                refresh();
            });
            m_row->addWidget(button);
            m_buttons.append(qMakePair(id,QPointer<QToolButton>(button)));
        }
        m_row->addStretch(1);
        refresh();
    }

    // The chooser is shared by the quick Add button and the editor.
    // "All commands" includes every non-menu QAction plus native tools.
    QString choose(QWidget *parent,const QStringList &already)
    {
        QDialog dlg(parent);
        dlg.setWindowTitle(i18n("Add Quick Action"));
        dlg.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        dlg.setStyleSheet(
            "QDialog{background:#263746;color:white;}"
            "QLabel{color:#edf5fc;font-size:14px;}"
            "QLineEdit,QComboBox{background:#354c60;color:white;"
            "border:1px solid #78a0bb;border-radius:8px;"
            "padding:6px;min-height:43px;font-size:14px;}"
            "QListWidget{background:#2e4052;color:#f3f8ff;"
            "border:1px solid #6c91ad;font-size:13px;}"
            "QListWidget::item{min-height:49px;padding:6px;}"
            "QListWidget::item:selected{background:#386f8c;}"
            "QPushButton{background:#44647f;color:white;"
            "border:1px solid #87b0c9;border-radius:7px;"
            "min-height:44px;padding:6px;}");
        auto *layout=new QVBoxLayout(&dlg);
        layout->setContentsMargins(11,10,11,10);
        layout->setSpacing(7);
        auto *heading=new QLabel(i18n("Add a Quick Action"),&dlg);
        heading->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
        layout->addWidget(heading);
        auto *type=new QComboBox(&dlg);
        type->addItem(i18n("Frequently used"),0);
        type->addItem(i18n("All commands and tools"),1);
        type->addItem(i18n("Krita tools"),2);
        layout->addWidget(type);
        auto *search=new QLineEdit(&dlg);
        search->setPlaceholderText(i18n("Search actions, names or identifiers"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *list=new QListWidget(&dlg);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(list->viewport(),QScroller::LeftMouseButtonGesture);
        layout->addWidget(list,1);
        auto *count=new QLabel(&dlg);
        layout->addWidget(count);
        auto *actions=new QHBoxLayout;
        auto *cancel=new QPushButton(i18n("Cancel"),&dlg);
        auto *add=new QPushButton(i18n("Add"),&dlg);
        actions->addWidget(cancel,1);
        actions->addWidget(add,1);
        layout->addLayout(actions);
        QObject::connect(cancel,&QPushButton::clicked,&dlg,&QDialog::reject);
        const QVector<Entry> entries=catalog();
        const auto refill=[&]() {
            list->clear();
            int found=0;
            const QString query=search->text().trimmed();
            const int filter=type->currentData().toInt();
            for(const Entry &entry:entries) {
                if(filter==0 && !entry.frequent)continue;
                if(filter==2 && entry.group!=i18n("Krita tools"))continue;
                if(already.contains(entry.id))continue;
                if(!query.isEmpty() &&
                   !entry.title.contains(query,Qt::CaseInsensitive) &&
                   !entry.details.contains(query,Qt::CaseInsensitive) &&
                   !entry.id.contains(query,Qt::CaseInsensitive))continue;
                auto *item=new QListWidgetItem(entry.title,list);
                item->setData(Qt::UserRole,entry.id);
                item->setToolTip(entry.details);
                if(!entry.enabled)
                    item->setForeground(QColor(160,179,194));
                ++found;
            }
            count->setText(i18np("%1 action available",
                                  "%1 actions available",found));
            add->setEnabled(list->currentItem()!=nullptr);
        };
        QObject::connect(type,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         &dlg,[&](int){refill();});
        QObject::connect(search,&QLineEdit::textChanged,&dlg,
                         [&](const QString&){refill();});
        QObject::connect(list,&QListWidget::currentItemChanged,&dlg,
                         [add](QListWidgetItem *item,QListWidgetItem*) {
            add->setEnabled(item!=nullptr);
        });
        QString chosen;
        const auto acceptChoice=[&]() {
            auto *item=list->currentItem();
            if(!item)return;
            chosen=item->data(Qt::UserRole).toString();
            if(!chosen.isEmpty())dlg.accept();
        };
        QObject::connect(add,&QPushButton::clicked,&dlg,acceptChoice);
        QObject::connect(list,&QListWidget::itemDoubleClicked,&dlg,
                         [&](QListWidgetItem*){acceptChoice();});
        refill();
        const QSize bounds=parent->window()->size();
        dlg.resize(qMax(300,qMin(520,bounds.width()-24)),
                   qMax(340,qMin(700,bounds.height()-45)));
        if(dlg.exec()!=QDialog::Accepted)return {};
        return chosen;
    }

    void addAction()
    {
        if(m_ids.size()>=24)return;
        const QString id=choose(this,m_ids);
        if(id.isEmpty())return;
        m_ids.append(id);
        save();
        rebuild();
    }

    void editActions()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(i18n("Edit Quick Actions"));
        dlg.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        dlg.setStyleSheet(
            "QDialog{background:#263746;color:white;}"
            "QLabel{color:#eaf3fb;font-size:14px;}"
            "QListWidget{background:#2e4051;color:white;"
            "border:1px solid #7099b4;font-size:14px;}"
            "QListWidget::item{min-height:50px;padding:6px;}"
            "QListWidget::item:selected{background:#386f90;}"
            "QPushButton{background:#45647e;color:white;"
            "border:1px solid #87acc6;border-radius:7px;"
            "min-height:43px;padding:5px;}");
        auto *layout=new QVBoxLayout(&dlg);
        layout->setContentsMargins(11,9,11,10);
        auto *title=new QLabel(i18n("Arrange Quick Actions"),&dlg);
        title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
        layout->addWidget(title);
        auto *hint=new QLabel(i18n(
            "Hold and drag a row to reorder. Up and Down also work with "
            "finger or pen. Changes save only when you tap Save."),&dlg);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *list=new QListWidget(&dlg);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setDragEnabled(true);
        list->setAcceptDrops(true);
        list->viewport()->setAcceptDrops(true);
        list->setDropIndicatorShown(true);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        layout->addWidget(list,1);
        const auto populate=[&](const QStringList &ids) {
            list->clear();
            for(const QString &id:ids) {
                auto *item=new QListWidgetItem(QStringLiteral("☰   ")+label(id),list);
                item->setData(Qt::UserRole,id);
                item->setToolTip(id);
                item->setFlags(item->flags()|Qt::ItemIsDragEnabled|
                               Qt::ItemIsDropEnabled);
            }
        };
        populate(m_ids);
        const auto currentIds=[list]() {
            QStringList ids;
            for(int i=0;i<list->count();++i) {
                auto *item=list->item(i);
                if(item)ids.append(item->data(Qt::UserRole).toString());
            }
            return ids;
        };
        auto *tools=new QHBoxLayout;
        auto *add=new QPushButton(i18n("+ Add"),&dlg);
        auto *remove=new QPushButton(i18n("Remove"),&dlg);
        auto *up=new QPushButton(i18n("Up"),&dlg);
        auto *down=new QPushButton(i18n("Down"),&dlg);
        for(auto *b:{add,remove,up,down})tools->addWidget(b,1);
        layout->addLayout(tools);
        QObject::connect(add,&QPushButton::clicked,&dlg,[&]() {
            const QStringList ids=currentIds();
            if(ids.size()>=24)return;
            const QString id=choose(&dlg,ids);
            if(id.isEmpty())return;
            auto *item=new QListWidgetItem(QStringLiteral("☰   ")+label(id),list);
            item->setData(Qt::UserRole,id);
            item->setToolTip(id);
            list->setCurrentItem(item);
        });
        QObject::connect(remove,&QPushButton::clicked,&dlg,[&]() {
            const int index=list->currentRow();
            if(index<0)return;
            delete list->takeItem(index);
            if(list->count())list->setCurrentRow(qMin(index,list->count()-1));
        });
        const auto move=[&](int delta) {
            const int index=list->currentRow();
            const int dest=index+delta;
            if(index<0 || dest<0 || dest>=list->count())return;
            QListWidgetItem *item=list->takeItem(index);
            list->insertItem(dest,item);
            list->setCurrentItem(item);
            list->scrollToItem(item);
        };
        QObject::connect(up,&QPushButton::clicked,&dlg,[&]() {move(-1);});
        QObject::connect(down,&QPushButton::clicked,&dlg,[&]() {move(1);});
        auto *bottom=new QHBoxLayout;
        auto *reset=new QPushButton(i18n("Defaults"),&dlg);
        auto *cancel=new QPushButton(i18n("Cancel"),&dlg);
        auto *saveButton=new QPushButton(i18n("Save"),&dlg);
        bottom->addWidget(reset,1);
        bottom->addWidget(cancel,1);
        bottom->addWidget(saveButton,1);
        layout->addLayout(bottom);
        QObject::connect(reset,&QPushButton::clicked,&dlg,
                         [&]() {populate(defaults());});
        QObject::connect(cancel,&QPushButton::clicked,&dlg,&QDialog::reject);
        QObject::connect(saveButton,&QPushButton::clicked,&dlg,&QDialog::accept);
        const QSize bounds=window()->size();
        dlg.resize(qMax(315,qMin(535,bounds.width()-24)),
                   qMax(370,qMin(760,bounds.height()-35)));
        if(dlg.exec()!=QDialog::Accepted)return;
        m_ids=currentIds();
        save();
        rebuild();
    }

    MobileEngineBridge m_engine;
    std::function<void(const QString&)> m_dispatch;
    QStringList m_ids;
    QScrollArea *m_scroll=nullptr;
    QHBoxLayout *m_row=nullptr;
    QVector<QPair<QString,QPointer<QToolButton>>> m_buttons;
};
#endif // Q_OS_ANDROID
