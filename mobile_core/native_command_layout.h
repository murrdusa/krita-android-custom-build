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
#include <QFormLayout>
#include <QSpinBox>
#include <QStackedWidget>
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
#include <QSignalBlocker>
#include <QVector>
#include <QToolButton>
#include <QVBoxLayout>
#include "touch_scroll.h"
#include "toolbar_drag_gesture.h"

class MobileNativeCommandLayout final : public QObject
{
public:
    using Commands=QMap<QString,QPointer<QAction>>;

    static bool install(QMainWindow *host,QAction *home,const QIcon &settingsIcon,
                        const Commands &commands,const QStringList &defaults,
                        const QList<QAction*> &moreActions,QAction *canvasOnly=nullptr,
                        MobileToolbarGestureButton::InlineAdjustment adjust={},
                        MobileToolbarGestureButton::ColorSample sample={})
    {
        QWidget *canvas=host?host->centralWidget():nullptr;
        if(!canvas || !home || toolbar(canvas))return false;
        auto *layout=new MobileNativeCommandLayout(canvas,commands,defaults);
        layout->m_adjust=std::move(adjust);
        layout->m_sample=std::move(sample);
        auto *row=new QHBoxLayout(layout->m_toolbar);
        row->setContentsMargins(1,1,1,1);
        row->setSpacing(1);
        row->addWidget(makeButton(layout->m_toolbar,home));

        layout->m_scroll=new QScrollArea(layout->m_toolbar);
        layout->m_scroll->setObjectName(QStringLiteral("_mobileNativeCommandScroll"));
        layout->m_scroll->setFrameShape(QFrame::NoFrame);
        layout->m_scroll->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        layout->m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#_mobileNativeCommandScroll{background:transparent;border:0;}"));
        layout->m_scroll->viewport()->setAutoFillBackground(false);
        MobileTouchScroll::configure(layout->m_scroll);
        row->addWidget(layout->m_scroll,1);

        auto *settings=new QAction(settingsIcon,QObject::tr("Settings and commands"),layout);
        auto *more=makeButton(layout->m_toolbar,settings);
        more->setObjectName(QStringLiteral("_mobileNativeMoreButton"));
        row->addWidget(more);
        auto *menu=new QMenu(more);
        menu->setObjectName(QStringLiteral("_mobileNativeSettingsMenu"));
        const QString popupStyle=QStringLiteral(
            "QMenu{background:#252529;color:#f4f4f5;border:1px solid #55555c;padding:3px;}"
            "QMenu::item{color:#f4f4f5;padding:8px 14px;min-height:28px;}"
            "QMenu::item:selected{background:#45454f;}"
            "QMenu::item:disabled{color:#9a9aa2;}");
        menu->setStyleSheet(popupStyle);
        // Open the real menu directly. Associating its menuAction with the
        // tool button can make Qt wrap it in a second, unstyled menu.
        QObject::connect(settings,&QAction::triggered,menu,[more,menu]() {
            const int x=more->width()-menu->sizeHint().width();
            menu->popup(more->mapToGlobal(QPoint(x,more->height())));
        });
        QObject::connect(menu,&QMenu::aboutToShow,more,[more]() {more->setDown(true);});
        QObject::connect(menu,&QMenu::aboutToHide,more,[more]() {more->setDown(false);});

        auto *edit=menu->addAction(QObject::tr("Edit UI Layout…"));
        QObject::connect(edit,&QAction::triggered,layout,[layout]() {
            QTimer::singleShot(0,layout,[layout]() {layout->editToolbar();});
        });
        auto *drawing=menu->addMenu(QObject::tr("Drawing controls"));
        drawing->setStyleSheet(popupStyle);
        for(const auto &id:defaults)
            if(auto action=commands.value(id))drawing->addAction(action);
        layout->m_toolbarMenu=menu->addMenu(QObject::tr("Custom toolbar commands"));
        layout->m_toolbarMenu->setStyleSheet(popupStyle);
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
                layout->m_canvasOnly=enabled;
                layout->m_toolbar->setVisible(!enabled);
                restore->setVisible(enabled);
                layout->position();
            });
        }
        canvas->installEventFilter(layout);
        if(auto *stack=qobject_cast<QStackedWidget*>(canvas)) {
            QObject::connect(stack,&QStackedWidget::currentChanged,layout,[layout,menu](int) {
                if(menu->isVisible())menu->hide();
                layout->requestPosition();
            });
        }
        layout->m_canvasOnly=canvasOnly && canvasOnly->isChecked();
        layout->m_toolbar->setVisible(!layout->m_canvasOnly);
        layout->rebuild();
        return true;
    }

    static const char *overlayBoundsProperty()
    {
        return "_mobileToolbarOverlayBounds";
    }

    static QRect overlayBounds(QWidget *canvas)
    {
        if(!canvas)return {};
        const QVariant saved=canvas->property(overlayBoundsProperty());
        return saved.isValid()?saved.toRect():canvas->rect().adjusted(2,2,-2,-2);
    }

    static int panelTopInset(QWidget *canvas)
    {
        return overlayBounds(canvas).top();
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
    enum Edge {Top,Bottom,Left,Right};
    struct Bar {
        QString name;
        QStringList ids;
        Edge edge=Top;
        int alignment=0;
        int target=44;
        int icon=24;
        int spacing=1;
        int opacity=66;
    };

    MobileNativeCommandLayout(QWidget *canvas,const Commands &commands,
                              const QStringList &defaults)
        : QObject(canvas),m_commands(commands),m_defaults(defaults)
    {
        m_bars.append(defaultBar());
        const QSettings settings;
        const auto saved=settings.value(settingsKey()).toList();
        if(saved.isEmpty()) {
            m_bars.first().ids=settings.value(
                QStringLiteral("MobileStudio/primaryToolbarCommands"),defaults).toStringList();
        } else {
            m_bars.clear();
            for(const auto &item:saved) {
                const auto data=item.toMap();
                Bar bar;
                bar.name=data.value(QStringLiteral("name")).toString();
                bar.ids=data.value(QStringLiteral("commands")).toStringList();
                bar.edge=Edge(qBound(0,data.value(QStringLiteral("edge")).toInt(),3));
                bar.alignment=qBound(0,data.value(QStringLiteral("alignment")).toInt(),2);
                bar.target=qBound(44,data.value(QStringLiteral("target"),44).toInt(),72);
                bar.icon=qBound(16,data.value(QStringLiteral("icon"),24).toInt(),40);
                bar.spacing=qBound(0,data.value(QStringLiteral("spacing"),1).toInt(),12);
                bar.opacity=qBound(0,data.value(QStringLiteral("opacity"),66).toInt(),100);
                m_bars.append(bar);
            }
        }
        m_toolbar=new QFrame(canvas);
        m_toolbar->setObjectName(QStringLiteral("_mobileNativeToolbar"));
    }

    Bar defaultBar() const
    {
        Bar bar;
        bar.name=QObject::tr("Main toolbar");
        bar.ids=m_defaults;
        return bar;
    }

    static QString settingsKey()
    {
        return QStringLiteral("MobileStudio/toolbars");
    }

    static QString toolbarStyle(const Bar &bar)
    {
        return QStringLiteral(
            "QFrame[mobileCommandToolbar=\"true\"]{background:rgba(26,29,34,%1);"
            "border:1px solid rgba(255,255,255,%2);border-radius:8px;}"
            "QToolButton{background:transparent;color:white;border:1px solid transparent;"
            "border-radius:5px;padding:0;}"
            "QToolButton:pressed,QToolButton:checked{background:#234e62;border-color:#a7def1;}"
            "QToolButton:disabled{color:#b6b8bc;}"
            "QToolButton::menu-indicator{image:none;width:0;}")
            .arg(bar.opacity*255/100).arg(bar.opacity*98/100);
    }

    void saveBars() const
    {
        QVariantList values;
        for(const auto &bar:m_bars) {
            const QVariantMap value{
                {QStringLiteral("name"),bar.name},{QStringLiteral("commands"),bar.ids},
                {QStringLiteral("edge"),int(bar.edge)},{QStringLiteral("alignment"),bar.alignment},
                {QStringLiteral("target"),bar.target},{QStringLiteral("icon"),bar.icon},
                {QStringLiteral("spacing"),bar.spacing},{QStringLiteral("opacity"),bar.opacity}
            };
            values.append(value);
        }
        QSettings().setValue(settingsKey(),values);
    }

    static QFrame *toolbar(QWidget *canvas)
    {
        return canvas?canvas->findChild<QFrame*>(
            QStringLiteral("_mobileNativeToolbar"),Qt::FindDirectChildrenOnly):nullptr;
    }

    static QToolButton *makeButton(QWidget *parent,QAction *action,
                                   int target=44,int icon=24,bool vertical=false)
    {
        auto *button=new QToolButton(parent);
        button->setDefaultAction(action);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(icon,icon));
        const auto sync=[button,action,target,vertical]() {
            QString label=action->text();
            label.remove(QLatin1Char('&'));
            button->setAccessibleName(label);
            button->setVisible(action->isVisible());
            button->setToolButtonStyle(action->icon().isNull()?
                Qt::ToolButtonTextOnly:Qt::ToolButtonIconOnly);
            const int width=action->icon().isNull() && !vertical?
                qBound(target,button->fontMetrics().horizontalAdvance(label)+10,132):target;
            button->setFixedSize(width,target);
            button->setToolTip(action->toolTip().isEmpty()?label:action->toolTip());
        };
        QObject::connect(action,&QAction::changed,button,sync);
        sync();
        return button;
    }

    void position()
    {
        auto *canvas=static_cast<QWidget*>(parent());
        QRect usable=canvas->rect().adjusted(2,2,-2,-2);
        const auto &main=m_bars.first();
        const auto *row=m_toolbar->layout();
        const int pinnedWidth=row->itemAt(0)->widget()->width()+
                              row->itemAt(2)->widget()->width();
        const int stripWidth=m_scroll->widget()?m_scroll->widget()->width():0;
        m_toolbar->setGeometry(usable.x(),usable.y(),
            qMin(pinnedWidth+stripWidth+6,qMax(0,usable.width())),main.target+4);
        if(!m_canvasOnly)usable.setTop(m_toolbar->geometry().bottom()+3);
        const int minimumWidth=qMax(140,usable.width()*2/3);
        const int minimumHeight=qMax(190,usable.height()*2/3);
        // Horizontal bars reserve their rows before the side rails are placed.
        for(const auto edge:{Top,Bottom,Left,Right}) {
            const bool vertical=edge==Left || edge==Right;
            for(int i=0;i<m_extra.size();++i) {
                const auto &spec=m_bars.at(i+1);
                if(spec.edge!=edge)continue;
                auto *frame=m_extra.at(i);
                auto *scroll=static_cast<QScrollArea*>(frame->layout()->itemAt(0)->widget());
                const int extent=vertical?scroll->widget()->height():scroll->widget()->width();
                const int available=vertical?usable.height():usable.width();
                const int breadth=spec.target+4;
                const int remaining=vertical?usable.width()-minimumWidth:usable.height()-minimumHeight;
                const bool fits=!m_canvasOnly && extent>0 && available>=breadth && remaining>=breadth+2;
                frame->setVisible(fits);
                if(!fits)continue;

                const int length=qMin(available,extent+4);
                const int offset=(available-length)*spec.alignment/2;
                const int x=vertical?(edge==Left?usable.left():usable.right()-breadth+1):
                                     usable.left()+offset;
                const int y=vertical?usable.top()+offset:
                    (edge==Top?usable.top():usable.bottom()-breadth+1);
                frame->setGeometry(x,y,vertical?breadth:length,vertical?length:breadth);
                frame->raise();
                switch(edge) {
                case Top:usable.setTop(usable.top()+breadth+2);break;
                case Bottom:usable.setBottom(usable.bottom()-breadth-2);break;
                case Left:usable.setLeft(usable.left()+breadth+2);break;
                case Right:usable.setRight(usable.right()-breadth-2);break;
                }
            }
        }
        m_toolbar->raise();
        if(m_restore)m_restore->raise();
        if(canvas->property(overlayBoundsProperty()).toRect()!=usable)
            canvas->setProperty(overlayBoundsProperty(),usable);
    }

    void populate(QScrollArea *scroll,const Bar &bar,bool vertical)
    {
        auto *strip=new QWidget;
        strip->setAttribute(Qt::WA_TranslucentBackground);
        auto *row=new QBoxLayout(vertical?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight,strip);
        row->setContentsMargins(0,0,0,0);
        row->setSpacing(bar.spacing);
        const auto shortcut=m_commands.value(QStringLiteral("shortcut"));
        QToolButton *shortcutButton=nullptr;
        for(const auto &id:bar.ids) {
            auto action=m_commands.value(id);
            if(!action)continue;
            QToolButton *button=nullptr;
            if(MobileToolbarDrag::usesButton(id) ||
               id==QStringLiteral("color") ||
               id==QStringLiteral("tool-settings") ||
               id==QStringLiteral("paint") || id==QStringLiteral("erase") ||
               id==QStringLiteral("blend") || id==QStringLiteral("tools") ||
               id==QStringLiteral("shortcut")) {
                // Construct even for currently disabled assignments:
                // settings can change without rebuilding toolbar layout.
                auto *gesture=new MobileToolbarGestureButton(
                    strip,id,vertical,m_adjust,m_sample);
                gesture->setDefaultAction(action);
                gesture->setFocusPolicy(Qt::NoFocus);
                gesture->setIconSize(QSize(bar.icon,bar.icon));
                button=gesture;
                const auto sync=[gesture,action,bar,vertical]() {
                    QString label=action->text();
                    label.remove(QLatin1Char('&'));
                    gesture->setAccessibleName(label);
                    gesture->setVisible(action->isVisible());
                    gesture->setToolButtonStyle(action->icon().isNull()?
                        Qt::ToolButtonTextOnly:Qt::ToolButtonIconOnly);
                    const int width=action->icon().isNull() && !vertical?
                        qBound(bar.target,gesture->fontMetrics().horizontalAdvance(label)+10,132):
                        bar.target;
                    gesture->setFixedSize(width,bar.target);
                    gesture->setToolTip(action->toolTip().isEmpty()?label:action->toolTip());
                };
                QObject::connect(action,&QAction::changed,gesture,sync);
                sync();
            } else button=makeButton(strip,action,bar.target,bar.icon,vertical);
            if(action==shortcut)shortcutButton=button;
            row->addWidget(button);
        }
        const auto updateSize=[this,scroll,strip,row,vertical,bar,shortcutButton]() {
            if(scroll->widget()!=strip)return;
            if(shortcutButton && shortcutButton->defaultAction()) {
                auto *action=shortcutButton->defaultAction();
                const auto *target=action->data().value<QObject*>();
                bool duplicate=false;
                if(target) {
                    for(int i=0;i<row->count();++i) {
                        auto *button=static_cast<QToolButton*>(row->itemAt(i)->widget());
                        auto *candidate=button->defaultAction();
                        if(button!=shortcutButton && candidate==target && candidate->isVisible()) {
                            duplicate=true;
                            break;
                        }
                    }
                }
                shortcutButton->setVisible(action->isVisible() && !duplicate);
            }
            const auto hint=row->sizeHint();
            const QSize size=vertical?QSize(bar.target,hint.height()):QSize(hint.width(),bar.target);
            if(strip->size()!=size)strip->setFixedSize(size);
            if(scroll==m_scroll)scroll->setVisible(hint.width()>0);
        };
        for(int i=0;i<row->count();++i) {
            auto *button=static_cast<QToolButton*>(row->itemAt(i)->widget());
            QObject::connect(button->defaultAction(),&QAction::changed,button,[this,updateSize]() {
                updateSize();
                requestPosition();
            });
        }
        scroll->setWidget(strip);
        strip->setAutoFillBackground(false);
        updateSize();
    }

    void requestPosition()
    {
        if(m_positionPending)return;
        m_positionPending=true;
        QTimer::singleShot(0,this,[this]() {
            m_positionPending=false;
            position();
        });
    }

    void rebuild()
    {
        m_toolbar->setProperty("mobileCommandToolbar",true);
        m_toolbar->setStyleSheet(toolbarStyle(m_bars.first()));
        m_scroll->setMinimumSize(0,m_bars.first().target);
        populate(m_scroll,m_bars.first(),false);
        qDeleteAll(m_extra);
        m_extra.clear();
        qDeleteAll(m_toolbarMenu->findChildren<QMenu*>(QString(),Qt::FindDirectChildrenOnly));
        m_toolbarMenu->clear();
        auto *canvas=static_cast<QWidget*>(parent());
        for(int i=1;i<m_bars.size();++i) {
            const auto &bar=m_bars.at(i);
            auto *frame=new QFrame(canvas);
            frame->setObjectName(QStringLiteral("_mobileCustomToolbar"));
            frame->setAccessibleName(bar.name);
            frame->setProperty("mobileCommandToolbar",true);
            frame->setStyleSheet(toolbarStyle(bar));
            auto *row=new QHBoxLayout(frame);
            row->setContentsMargins(2,2,2,2);
            auto *scroll=new QScrollArea(frame);
            scroll->setObjectName(QStringLiteral("_mobileCustomToolbarScroll"));
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
            scroll->setStyleSheet(QStringLiteral(
                "QScrollArea#_mobileCustomToolbarScroll{background:transparent;border:0;}"));
            scroll->viewport()->setAutoFillBackground(false);
            MobileTouchScroll::configure(scroll);
            row->addWidget(scroll);
            populate(scroll,bar,bar.edge==Left || bar.edge==Right);
            m_extra.append(frame);

            auto *fallback=m_toolbarMenu->addMenu(bar.name);
            for(const auto &id:bar.ids)
                if(auto action=m_commands.value(id))fallback->addAction(action);
            fallback->menuAction()->setEnabled(!fallback->isEmpty());
        }
        m_toolbarMenu->menuAction()->setVisible(!m_extra.isEmpty());
        position();
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

    void styleToolbar(QWidget *parent,Bar &bar,bool main)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Toolbar appearance"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        MobileTouchScroll::configure(scroll);
        auto *body=new QWidget(scroll);
        auto *form=new QFormLayout(body);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        scroll->setWidget(body);
        layout->addWidget(scroll,1);
        auto *name=new QLineEdit(bar.name,body);
        auto *edge=new MobileTouchComboBox(body);
        auto *alignment=new MobileTouchComboBox(body);
        if(!main) {
            form->addRow(QObject::tr("Name"),name);
            edge->addItems({QObject::tr("Top · horizontal"),QObject::tr("Bottom · horizontal"),
                            QObject::tr("Left · vertical"),QObject::tr("Right · vertical")});
            edge->setCurrentIndex(bar.edge);
            form->addRow(QObject::tr("Edge"),edge);
            alignment->addItems({QObject::tr("Start"),QObject::tr("Center"),QObject::tr("End")});
            alignment->setCurrentIndex(bar.alignment);
            form->addRow(QObject::tr("Position along edge"),alignment);
        } else {
            name->hide();
            edge->hide();
            alignment->hide();
        }
        const auto spin=[form,body](const QString &label,int value,int low,int high) {
            auto *field=new QSpinBox(body);
            field->setMinimumHeight(44);
            field->setRange(low,high);
            field->setValue(value);
            form->addRow(label,field);
            return field;
        };
        auto *target=spin(QObject::tr("Button size"),bar.target,44,72);
        auto *icon=spin(QObject::tr("Icon size"),bar.icon,16,40);
        auto *spacing=spin(QObject::tr("Spacing"),bar.spacing,0,12);
        auto *opacity=spin(QObject::tr("Background opacity"),bar.opacity,0,100);
        opacity->setSuffix(QStringLiteral("%"));
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.resize(parent->size());
        if(dialog.exec()!=QDialog::Accepted)return;
        if(!main) {
            if(!name->text().trimmed().isEmpty())bar.name=name->text().trimmed();
            bar.edge=Edge(edge->currentIndex());
            bar.alignment=alignment->currentIndex();
        }
        bar.target=target->value();
        bar.icon=icon->value();
        bar.spacing=spacing->value();
        bar.opacity=opacity->value();
    }

    void editToolbar()
    {
        if(m_editing)return;
        QScopedValueRollback<bool> editing(m_editing,true);
        QDialog dialog(m_toolbar->window());
        dialog.setWindowTitle(QObject::tr("Edit UI Layout"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *selector=new MobileTouchComboBox(&dialog);
        selector->setMinimumHeight(44);
        layout->addWidget(selector);
        auto *list=new QListWidget(&dialog);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:42px;padding:4px;}"));
        MobileTouchScroll::configureList(list);
        layout->addWidget(list,1);
        auto bars=m_bars;
        int current=0;
        const auto ids=[&]() -> QStringList& {return bars[current].ids;};
        const auto refresh=[&](int index) {
            list->clear();
            for(const auto &id:ids()) {
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
        auto *barRow=new QHBoxLayout;
        layout->insertLayout(1,barRow);
        auto *commandRow=new QHBoxLayout;
        layout->addLayout(commandRow);
        const auto addButton=[&](QHBoxLayout *row,const QString &label,auto callback) {
            auto *button=new QPushButton(label,&dialog);
            button->setMinimumHeight(44);
            row->addWidget(button);
            QObject::connect(button,&QPushButton::clicked,&dialog,callback);
            return button;
        };
        QPushButton *deleteBar=nullptr;
        const auto refillBars=[&]() {
            QSignalBlocker block(selector);
            selector->clear();
            for(const auto &bar:bars)selector->addItem(bar.name);
            selector->setCurrentIndex(current);
            deleteBar->setEnabled(current>0);
            refresh(0);
        };
        addButton(barRow,QObject::tr("New toolbar"),[&]() {
            Bar bar;
            bar.name=QObject::tr("Toolbar %1").arg(bars.size()+1);
            bar.edge=Right;
            bars.append(bar);
            current=bars.size()-1;
            refillBars();
        });
        addButton(barRow,QObject::tr("Appearance"),[&]() {
            styleToolbar(&dialog,bars[current],current==0);
            refillBars();
        });
        deleteBar=addButton(barRow,QObject::tr("Delete toolbar"),[&]() {
            if(current==0)return;
            bars.removeAt(current);
            current=qMin(current,bars.size()-1);
            refillBars();
        });
        QObject::connect(selector,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         &dialog,[&](int index) {
            if(index<0)return;
            current=index;
            deleteBar->setEnabled(current>0);
            refresh(0);
        });

        addButton(commandRow,QObject::tr("Add"),[&]() {
            const QString id=chooseCommand(&dialog,ids());
            if(id.isEmpty())return;
            ids().append(id);
            refresh(ids().size()-1);
        });
        auto *remove=addButton(commandRow,QObject::tr("Remove"),[&]() {
            const int i=list->currentRow();
            if(i<0)return;
            ids().removeAt(i);
            refresh(i);
        });
        const auto move=[&](int step) {
            const int i=list->currentRow(),target=i+step;
            if(i<0 || target<0 || target>=ids().size())return;
            ids().move(i,target);
            refresh(target);
        };
        auto *up=addButton(commandRow,QObject::tr("Up"),[&]() {move(-1);});
        auto *down=addButton(commandRow,QObject::tr("Down"),[&]() {move(1);});
        const auto sync=[&](int i) {
            remove->setEnabled(i>=0);
            up->setEnabled(i>0);
            down->setEnabled(i>=0 && i+1<ids().size());
        };
        QObject::connect(list,&QListWidget::currentRowChanged,&dialog,sync);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        auto *resetRow=new QHBoxLayout;
        layout->addLayout(resetRow);
        addButton(resetRow,QObject::tr("Reset toolbar"),[&]() {
            if(current==0)bars[0]=defaultBar();
            else {
                const auto old=bars.at(current);
                Bar clean;
                clean.name=old.name;
                clean.edge=old.edge;
                bars[current]=clean;
            }
            refillBars();
        });
        addButton(resetRow,QObject::tr("Reset layout"),[&]() {
            bars={defaultBar()};
            current=0;
            refillBars();
        });
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);
        refillBars();
        sync(list->currentRow());
        dialog.resize(qMin(470,m_toolbar->window()->width()),qMin(600,m_toolbar->window()->height()));
        if(dialog.exec()!=QDialog::Accepted)return;
        m_bars=bars;
        saveBars();
        rebuild();
    }

    Commands m_commands;
    MobileToolbarGestureButton::InlineAdjustment m_adjust;
    MobileToolbarGestureButton::ColorSample m_sample;
    QStringList m_defaults;
    QVector<Bar> m_bars;
    QVector<QFrame*> m_extra;
    QMenu *m_toolbarMenu=nullptr;
    QFrame *m_toolbar=nullptr;
    QScrollArea *m_scroll=nullptr;
    QToolButton *m_restore=nullptr;
    bool m_canvasOnly=false,m_editing=false,m_positionPending=false;
};
#endif
