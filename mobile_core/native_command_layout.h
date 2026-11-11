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
#include "recent_colors.h"
#include "canvas_layout_editor.h"
#include "toolbar_drag_gesture.h"

class MobileNativeCommandLayout final : public QObject
{
public:
    using Commands=QMap<QString,QPointer<QAction>>;

    static MobileNativeCommandLayout *install(QMainWindow *host,QAction *home,const QIcon &settingsIcon,
                        const Commands &commands,const QStringList &defaults,
                        const QList<QAction*> &moreActions,QAction *canvasOnly=nullptr,
                        MobileToolbarGestureButton::InlineAdjustment adjust={},
                        MobileToolbarGestureButton::ColorSample sample={},
                        std::function<void(const QColor &)> selectColor={})
    {
        QWidget *canvas=host?host->centralWidget():nullptr;
        if(!canvas || !home || toolbar(canvas))return nullptr;
        auto *layout=new MobileNativeCommandLayout(canvas,commands,defaults);
        layout->m_adjust=std::move(adjust);
        layout->m_sample=std::move(sample);
        layout->m_selectColor=std::move(selectColor);
        layout->m_recentColors=MobileRecentColors::load();
        layout->m_historyEnabled=layout->m_selectColor &&
            QSettings().value(QStringLiteral("MobileStudio/toolbarColorHistory"),false).toBool();
        layout->m_colorTimer.setSingleShot(true);
        layout->m_colorTimer.setInterval(200);
        QObject::connect(&layout->m_colorTimer,&QTimer::timeout,layout,[layout]() {
            const auto colors=MobileRecentColors::remember(layout->m_pendingColor);
            if(layout->m_recentColors==colors)return;
            layout->m_recentColors=colors;
            if(layout->m_historyEnabled)layout->requestPosition();
        });
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
            QTimer::singleShot(0,layout,[layout]() {layout->editCanvasLayout();});
        });
        auto *toolbars=menu->addMenu(QObject::tr("Toolbars"));
        layout->m_toolbarsMenu=toolbars;
        toolbars->setStyleSheet(popupStyle);
        auto *commandsAction=toolbars->addAction(QObject::tr("Customize toolbar commands…"));
        QObject::connect(commandsAction,&QAction::triggered,layout,[layout]() {
            QTimer::singleShot(0,layout,[layout]() {layout->editToolbar();});
        });
        if(layout->m_selectColor) {
            auto *history=toolbars->addAction(QObject::tr("Toolbar color history"));
            history->setCheckable(true);
            history->setChecked(layout->m_historyEnabled);
            QObject::connect(history,&QAction::toggled,layout,[layout](bool enabled) {
                layout->m_historyEnabled=enabled;
                QSettings().setValue(QStringLiteral("MobileStudio/toolbarColorHistory"),enabled);
                layout->position();
            });
        }
        auto *drawing=toolbars->addMenu(QObject::tr("Drawing commands"));
        drawing->setStyleSheet(popupStyle);
        for(const auto &id:defaults)
            if(auto action=commands.value(id))drawing->addAction(action);
        drawing->menuAction()->setVisible(!drawing->isEmpty());
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
                if(enabled && layout->m_editor)layout->m_editor->closeEditor(false);
                layout->m_toolbar->setVisible(!enabled);
                restore->setVisible(enabled);
                layout->position();
            });
        }
        canvas->installEventFilter(layout);
        if(auto *stack=qobject_cast<QStackedWidget*>(canvas)) {
            QObject::connect(stack,&QStackedWidget::currentChanged,layout,[layout,menu](int) {
                if(menu->isVisible())menu->hide();
                if(layout->m_editor)layout->m_editor->closeEditor(false);
                layout->requestPosition();
            });
        }
        layout->m_canvasOnly=canvasOnly && canvasOnly->isChecked();
        layout->m_toolbar->setVisible(!layout->m_canvasOnly);
        layout->rebuild();
        return layout;
    }

    void setLayoutPanels(std::function<QVector<MobileCanvasLayoutEditor::Target>()> panels,
                         std::function<void(bool)> editing)
    {
        m_layoutPanels=std::move(panels);
        m_layoutEditing=std::move(editing);
    }

    void observeForeground(const QColor &color)
    {
        if(!color.isValid() || color==m_pendingColor)return;
        m_pendingColor=color;
        m_colorTimer.start();
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
           (event->type()==QEvent::Resize || event->type()==QEvent::Show)) {
            if(m_editor)m_editor->setGeometry(static_cast<QWidget*>(parent())->rect());
            else position();
        }
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
        QRectF placement;
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
                bar.placement=data.value(QStringLiteral("placement")).toRectF();
                m_bars.append(bar);
            }
        }
        if(!m_commands.contains(QStringLiteral("shortcut")))
            for(auto &bar:m_bars)bar.ids.removeAll(QStringLiteral("shortcut"));
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
                {QStringLiteral("spacing"),bar.spacing},{QStringLiteral("opacity"),bar.opacity},
                {QStringLiteral("placement"),bar.placement}
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

    static constexpr int historyThickness=22;

    void placeHistory(QFrame *frame,Edge edge,int margin,bool show)
    {
        auto *history=frame->findChild<QFrame*>(QStringLiteral("_mobileToolbarColorHistory"),
                                                Qt::FindDirectChildrenOnly);
        int left=margin,top=margin,right=margin,bottom=margin;
        if(show) {
            switch(edge) {
            case Top:bottom+=historyThickness;break;
            case Bottom:top+=historyThickness;break;
            case Left:right+=historyThickness;break;
            case Right:left+=historyThickness;break;
            }
        }
        frame->layout()->setContentsMargins(left,top,right,bottom);
        if(!show) {
            if(history)history->hide();
            return;
        }
        const bool vertical=edge==Left || edge==Right;
        if(!history) {
            history=new QFrame(frame);
            history->setObjectName(QStringLiteral("_mobileToolbarColorHistory"));
            history->setAccessibleName(QObject::tr("Toolbar color history"));
            history->setStyleSheet(QStringLiteral(
                "QFrame#_mobileToolbarColorHistory{background:#252529;border-radius:5px;}"));
            auto *row=new QBoxLayout(vertical?QBoxLayout::TopToBottom:
                                             QBoxLayout::LeftToRight,history);
            row->setContentsMargins(2,2,2,2);
            row->setSpacing(2);
            for(int i=0;i<8;++i) {
                auto *swatch=new QToolButton(history);
                swatch->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
                swatch->setMaximumSize(vertical?18:44,vertical?44:18);
                row->addWidget(swatch,1);
                QObject::connect(swatch,&QToolButton::clicked,history,[this,i]() {
                    const QColor color(m_recentColors.value(i));
                    if(color.isValid())m_selectColor(color);
                });
            }
            row->addStretch();
        }
        const int length=vertical?frame->height():frame->width();
        const int count=qMin(m_recentColors.size(),qMax(1,length/32));
        for(int i=0;i<8;++i) {
            auto *swatch=static_cast<QToolButton*>(history->layout()->itemAt(i)->widget());
            swatch->setVisible(i<count);
            if(i>=count)continue;
            const QColor color(m_recentColors.at(i));
            const auto name=color.name(QColor::HexRgb);
            if(swatch->accessibleName()==name && swatch->property("swatchColor").value<QColor>()==color)
                continue;
            swatch->setAccessibleName(name);
            swatch->setToolTip(name);
            swatch->setProperty("swatchColor",color);
            swatch->setStyleSheet(QStringLiteral(
                "QToolButton{background:rgba(%1,%2,%3,%4);border:1px solid #a6a6ae;border-radius:3px;}"
                "QToolButton:pressed{border:2px solid white;}")
                .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha()));
        }
        const int x=edge==Left?frame->width()-historyThickness:0;
        const int y=edge==Top?frame->height()-historyThickness:0;
        history->setGeometry(x,y,vertical?historyThickness:length,
                                 vertical?length:historyThickness);
        history->show();
        history->raise();
    }

    static bool verticalEdge(Edge edge) {return edge==Left || edge==Right;}

    static void reserveEdge(QRect &usable,const QRect &bar,Edge edge)
    {
        switch(edge) {
        case Top:if(qAbs(bar.top()-usable.top())<=8)usable.setTop(bar.bottom()+3);break;
        case Bottom:if(qAbs(bar.bottom()-usable.bottom())<=8)usable.setBottom(bar.top()-3);break;
        case Left:if(qAbs(bar.left()-usable.left())<=8)usable.setLeft(bar.right()+3);break;
        case Right:if(qAbs(bar.right()-usable.right())<=8)usable.setRight(bar.left()-3);break;
        }
    }

    void placeEditedBar(QFrame *frame,const Bar &bar,const QRect &bounds,bool history)
    {
        QRect rect=MobileCanvasLayoutEditor::absolute(bar.placement,bounds);
        const bool vertical=verticalEdge(bar.edge);
        const int breadth=bar.target+4+(history?historyThickness:0);
        const int minimumLength=frame==m_toolbar?2*bar.target+6:bar.target+4;
        const int available=vertical?bounds.height():bounds.width();
        const int length=qBound(qMin(minimumLength,available),
                                vertical?rect.height():rect.width(),available);
        rect.setSize(vertical?QSize(qMin(breadth,bounds.width()),length):
                              QSize(length,qMin(breadth,bounds.height())));
        rect.moveTo(bounds.x()+qRound(bar.placement.x()*(bounds.width()-rect.width())),
                    bounds.y()+qRound(bar.placement.y()*(bounds.height()-rect.height())));
        frame->setGeometry(rect);
    }

    void editCanvasLayout()
    {
        if(m_editor || m_canvasOnly)return;
        auto *canvas=static_cast<QWidget*>(parent());
        if(m_layoutEditing)m_layoutEditing(true);
        auto targets=m_layoutPanels?m_layoutPanels():QVector<MobileCanvasLayoutEditor::Target>();
        const auto addToolbar=[&](QFrame *frame,int index) {
            const bool vertical=verticalEdge(m_bars.at(index).edge);
            const auto name=QStringLiteral("_mobileToolbarColorHistory");
            const auto *history=frame->findChild<QFrame*>(name,Qt::FindDirectChildrenOnly);
            const int strip=history && history->isVisible()?historyThickness:0;
            const int minimumBreadth=48+strip;
            const int minimumLength=index==0?100:48;
            targets.append({frame,vertical?QSize(minimumBreadth,minimumLength):
                                           QSize(minimumLength,minimumBreadth),true,
                [this,canvas,index,strip,vertical](const QRect &rect,bool rotate) {
                    const QRect bounds=canvas->rect().adjusted(2,2,-2,-2);
                    auto &bar=m_bars[index];
                    const bool finalVertical=vertical!=rotate;
                    const int target=qBound(44,(finalVertical?rect.width():rect.height())-4-strip,72);
                    bar.icon=qBound(16,qRound(bar.icon*target/qreal(bar.target)),40);
                    bar.target=target;
                    bar.edge=finalVertical?(rect.center().x()<bounds.center().x()?Left:Right):
                                            (rect.center().y()<bounds.center().y()?Top:Bottom);
                    bar.placement=MobileCanvasLayoutEditor::relative(rect,bounds);
                    m_editedBars=true;
                }});
        };
        addToolbar(m_toolbar,0);
        for(int i=0;i<m_extra.size();++i)addToolbar(m_extra.at(i),i+1);
        m_editor=new MobileCanvasLayoutEditor(canvas,targets,[this]() {
            m_editor.clear();
            if(m_editedBars) {
                m_editedBars=false;
                saveBars();
                rebuild();
            } else position();
            if(m_layoutEditing)m_layoutEditing(false);
        });
    }

    void position()
    {
        if(m_editor)return;
        auto *canvas=static_cast<QWidget*>(parent());
        const QRect bounds=canvas->rect().adjusted(2,2,-2,-2);
        if(!bounds.isValid())return;
        QRect usable=bounds;
        const auto &main=m_bars.first();
        const auto *row=m_toolbar->layout();
        const bool mainVertical=verticalEdge(main.edge);
        const auto *home=row->itemAt(0)->widget();
        const auto *settings=row->itemAt(2)->widget();
        const int pinned=mainVertical?home->height()+settings->height():home->width()+settings->width();
        const auto *commands=m_scroll->widget();
        const int stripLength=commands?(mainVertical?commands->height():commands->width()):0;
        const bool history=m_historyEnabled && !m_canvasOnly && !m_recentColors.isEmpty();
        if(main.placement.isValid()) {
            placeEditedBar(m_toolbar,main,bounds,history);
        } else {
            m_toolbar->setGeometry(usable.x(),usable.y(),
                qMin(pinned+stripLength+6,qMax(0,usable.width())),
                main.target+4+(history?historyThickness:0));
        }
        placeHistory(m_toolbar,main.edge,1,history);
        if(!m_canvasOnly)reserveEdge(usable,m_toolbar->geometry(),main.edge);
        const int minimumWidth=qMax(140,usable.width()*2/3);
        const int minimumHeight=qMax(190,usable.height()*2/3);
        // Horizontal bars reserve their rows before the side rails are placed.
        for(const auto edge:{Top,Bottom,Left,Right}) {
            const bool vertical=edge==Left || edge==Right;
            for(int i=0;i<m_extra.size();++i) {
                const auto &spec=m_bars.at(i+1);
                if(spec.edge!=edge)continue;
                auto *frame=m_extra.at(i);
                if(spec.placement.isValid()) {
                    frame->setVisible(!m_canvasOnly);
                    if(!m_canvasOnly) {
                        placeEditedBar(frame,spec,bounds,history);
                        placeHistory(frame,edge,2,history);
                        frame->raise();
                        reserveEdge(usable,frame->geometry(),edge);
                    }
                    continue;
                }
                auto *scroll=static_cast<QScrollArea*>(frame->layout()->itemAt(0)->widget());
                const int extent=vertical?scroll->widget()->height():scroll->widget()->width();
                const int available=vertical?usable.height():usable.width();
                const int toolbarBreadth=spec.target+4;
                const int remaining=vertical?usable.width()-minimumWidth:usable.height()-minimumHeight;
                const bool showHistory=history && remaining>=toolbarBreadth+historyThickness+2;
                const int breadth=toolbarBreadth+(showHistory?historyThickness:0);
                const bool fits=!m_canvasOnly && extent>0 && available>=toolbarBreadth && remaining>=breadth+2;
                frame->setVisible(fits);
                if(!fits)continue;

                const int length=qMin(available,extent+4);
                const int offset=(available-length)*spec.alignment/2;
                const int x=vertical?(edge==Left?usable.left():usable.right()-breadth+1):
                                     usable.left()+offset;
                const int y=vertical?usable.top()+offset:
                    (edge==Top?usable.top():usable.bottom()-breadth+1);
                frame->setGeometry(x,y,vertical?breadth:length,vertical?length:breadth);
                placeHistory(frame,edge,2,showHistory);
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
            if(scroll==m_scroll)scroll->setVisible((vertical?hint.height():hint.width())>0);
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
        const auto &main=m_bars.first();
        const bool vertical=verticalEdge(main.edge);
        auto *mainRow=static_cast<QBoxLayout*>(m_toolbar->layout());
        delete m_toolbar->findChild<QFrame*>(QStringLiteral("_mobileToolbarColorHistory"),
                                            Qt::FindDirectChildrenOnly);
        mainRow->setDirection(vertical?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
        mainRow->itemAt(0)->widget()->setFixedSize(main.target,main.target);
        mainRow->itemAt(2)->widget()->setFixedSize(main.target,main.target);
        m_scroll->setMinimumSize(vertical?main.target:0,vertical?0:main.target);
        populate(m_scroll,main,vertical);
        qDeleteAll(m_extra);
        m_extra.clear();
        qDeleteAll(m_toolbarFallbacks);
        m_toolbarFallbacks.clear();
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

            auto *fallback=m_toolbarsMenu->addMenu(bar.name);
            m_toolbarFallbacks.append(fallback);
            for(const auto &id:bar.ids)
                if(auto action=m_commands.value(id))fallback->addAction(action);
            fallback->menuAction()->setEnabled(!fallback->isEmpty());
        }
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

    void editToolbar()
    {
        if(m_editing)return;
        QScopedValueRollback<bool> editing(m_editing,true);
        QDialog dialog(m_toolbar->window());
        dialog.setWindowTitle(QObject::tr("Customize toolbar commands"));
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
        // Toolbar name, spacing and transparency belong to the toolbar
        // editor, not a second modal Appearance dialog. They stay
        // collapsed unless explicitly requested, saving Fold screen
        // space while preserving every previously saved preference.
        auto *properties=new QWidget(&dialog);
        auto *propertyForm=new QFormLayout(properties);
        propertyForm->setContentsMargins(2,2,2,2);
        auto *barName=new QLineEdit(properties);
        auto *barSpacing=new QSpinBox(properties);
        barSpacing->setRange(0,12);
        barSpacing->setMinimumHeight(44);
        auto *barOpacity=new QSpinBox(properties);
        barOpacity->setRange(0,100);
        barOpacity->setSuffix(QStringLiteral("%"));
        barOpacity->setMinimumHeight(44);
        propertyForm->addRow(QObject::tr("Toolbar name"),barName);
        propertyForm->addRow(QObject::tr("Button spacing"),barSpacing);
        propertyForm->addRow(QObject::tr("Toolbar opacity"),barOpacity);
        layout->insertWidget(2,properties);
        properties->hide();
        const auto updateProperties=[&]() {
            if(current<0 || current>=bars.size())return;
            QSignalBlocker nameBlock(barName);
            QSignalBlocker spacingBlock(barSpacing);
            QSignalBlocker opacityBlock(barOpacity);
            barName->setText(bars.at(current).name);
            barName->setEnabled(current>0);
            barSpacing->setValue(bars.at(current).spacing);
            barOpacity->setValue(bars.at(current).opacity);
        };
        QObject::connect(barName,&QLineEdit::textEdited,&dialog,
            [&](const QString &text) {
                if(current<=0 || current>=bars.size())return;
                const QString value=text.trimmed();
                // Keep changes on the *currently selected* toolbar,
                // even when a finger switches the selector before its
                // text editor emits editingFinished.
                if(value.isEmpty())return;
                bars[current].name=value;
                QSignalBlocker block(selector);
                selector->setItemText(current,value);
            });
        QObject::connect(barSpacing,QOverload<int>::of(&QSpinBox::valueChanged),
            &dialog,[&](int value) {
                if(current>=0 && current<bars.size())bars[current].spacing=value;
            });
        QObject::connect(barOpacity,QOverload<int>::of(&QSpinBox::valueChanged),
            &dialog,[&](int value) {
                if(current>=0 && current<bars.size())bars[current].opacity=value;
            });
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
            updateProperties();
        };
        addButton(barRow,QObject::tr("New toolbar"),[&]() {
            Bar bar;
            bar.name=QObject::tr("Toolbar %1").arg(bars.size()+1);
            bar.edge=Right;
            bars.append(bar);
            current=bars.size()-1;
            refillBars();
        });
        auto *propertiesButton=addButton(barRow,QObject::tr("Properties"),[&]() {
            properties->setVisible(!properties->isVisible());
            if(properties->isVisible())updateProperties();
        });
        propertiesButton->setToolTip(QObject::tr(
            "Toolbar name, spacing and background opacity"));
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
            updateProperties();
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
        addButton(resetRow,QObject::tr("Restore default toolbars"),[&]() {
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
        // Android may accept a dialog without moving focus out of the
        // currently edited QLineEdit. Commit its current text explicitly.
        if(current>0 && current<bars.size() &&
           !barName->text().trimmed().isEmpty())
            bars[current].name=barName->text().trimmed();
        m_bars=bars;
        saveBars();
        rebuild();
    }

    Commands m_commands;
    MobileToolbarGestureButton::InlineAdjustment m_adjust;
    MobileToolbarGestureButton::ColorSample m_sample;
    std::function<void(const QColor &)> m_selectColor;
    QTimer m_colorTimer;
    QColor m_pendingColor;
    QStringList m_recentColors;
    bool m_historyEnabled=false;
    QStringList m_defaults;
    QVector<Bar> m_bars;
    QVector<QFrame*> m_extra;
    QMenu *m_toolbarsMenu=nullptr;
    QVector<QMenu*> m_toolbarFallbacks;
    QFrame *m_toolbar=nullptr;
    QScrollArea *m_scroll=nullptr;
    QToolButton *m_restore=nullptr;
    QPointer<MobileCanvasLayoutEditor> m_editor;
    std::function<QVector<MobileCanvasLayoutEditor::Target>()> m_layoutPanels;
    std::function<void(bool)> m_layoutEditing;
    bool m_canvasOnly=false,m_editing=false,m_positionPending=false,m_editedBars=false;
};
#endif
