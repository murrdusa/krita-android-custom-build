/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A touch grid over Krita's registered tools and native canvas actions.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QGridLayout>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QScrollArea>
#include <QSettings>
#include "transient_panel.h"
#include "touch_scroll.h"
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <kis_icon.h>
#include <klocalizedstring.h>

#include "engine_bridge.h"

class MobileToolBrowser final
{
public:
    static void dismiss(QWidget *parent)
    {
        if(auto *panel=findPanel(parent))panel->close();
    }

    static void toggle(const MobileEngineBridge &engine,QWidget *parent,
                       QWidget *opener,QAction *action)
    {
        if(!parent || !engine.hasDocument())return;
        const auto view=MobileViewIdentityGuard::uniqueActive(engine.openDocuments());
        if(!view)return;
        if(auto *panel=findPanel(parent)) {panel->close();return;}

        const auto available=engine.tools();
        const QString current=engine.activeTool();
        QVector<Group> groups;
        QSet<QString> used;
        const auto addGroup=[&](const QString &name,
                                std::initializer_list<ToolTile> tools,
                                std::initializer_list<ActionTile> actions={}) {
            Group group{name,{}};
            for(const auto &entry:tools) {
                const QString id=QString::fromLatin1(entry.id);
                // Prefer Krita's exact registered ID. If a plugin
                // registers a short ID on this build and a namespaced ID
                // on another, match only the same KisTool family. Both
                // directions matter: the curated tile may be either form.
                const auto addRegistered=[&](const MobileEngineBridge::MobileTool &tool) {
                    group.tiles.append({tool.id,entry.label,tool.description,
                                        tool.iconName,nullptr});
                    used.insert(tool.id);
                };
                bool found=false;
                for(const auto &tool:available) {
                    if(tool.id==id && !used.contains(tool.id)) {
                        addRegistered(tool);
                        found=true;
                        break;
                    }
                }
                if(found)continue;
                const QString family=id.section(QLatin1Char('/'),-1);
                // Reference Images and SVG Text are registered native
                // tools but do not use the KisTool prefix. Resolve their
                // plugin-qualified IDs just like the ordinary tool families.
                if(!family.startsWith(QStringLiteral("KisTool")) &&
                   family!=QStringLiteral("ToolReferenceImages") &&
                   family!=QStringLiteral("SvgTextTool"))continue;
                for(const auto &tool:available) {
                    if(used.contains(tool.id) ||
                       tool.id.section(QLatin1Char('/'),-1)!=family)
                        continue;
                    addRegistered(tool);
                    break;
                }
            }
            for(const auto &entry:actions) {
                if(QAction *action=engine.action(entry.id)) {
                    group.tiles.append({{},entry.label,action->toolTip(),
                                        QString::fromLatin1(entry.icon),action,
                                        QString::fromLatin1(entry.id)});
                }
            }
            if(!group.tiles.isEmpty())groups.append(group);
        };
        addGroup(i18n("Paint and Fill"),{
            {"KritaShape/KisToolBrush",i18n("Brush")},
            {"KritaFill/KisToolFill",i18n("Fill")},
            {"KritaFill/KisToolGradient",i18n("Gradient")},
            {"KisToolEncloseAndFill",i18n("Enclose and fill")},
            {"SvgTextTool",i18n("Text")},
            {"KritaSelected/KisToolColorSampler",i18n("Eyedropper")}
        });
        // Reuse Krita's selection fill/stroke commands directly. They
        // operate on the current document selection (or the full layer if
        // Krita permits it), with Krita's own undo transaction.
        addGroup(i18n("Fill and stroke actions"),{},{
            {"fill_selection_foreground_color",i18n("Fill foreground"),"format-fill-color"},
            {"fill_selection_background_color",i18n("Fill background"),"format-fill-color"},
            {"fill_selection_pattern",i18n("Fill pattern"),"format-fill-color"},
            {"stroke_selection",i18n("Stroke selection"),"draw-freehand"},
            {"stroke_shapes",i18n("Stroke shapes"),"draw-freehand"}
        });
        addGroup(i18n("Selection"),{
            {"KisToolSelectOutline",i18n("Lasso")},
            {"KisToolSelectRectangular",i18n("Rectangle")},
            {"KisToolSelectContiguous",i18n("Wand")},
            {"KisToolSelectMagnetic",i18n("Magnetic")},
            {"KisToolSelectPolygonal",i18n("Polygon")},
            {"KisToolSelectPath",i18n("Path")},
            {"KisToolSelectElliptical",i18n("Ellipse")},
            {"KisToolSelectSimilar",i18n("Color")}
        },{{"deselect",i18n("Deselect"),"edit-clear"}});
        // Selection modifiers are native document commands, not separate
        // tools. Keep them available even when the user is currently
        // painting with the freehand brush.
        addGroup(i18n("Selection actions"),{},{
            {"select_all",i18n("Select all"),"select-all"},
            {"reselect",i18n("Reselect"),"select-all"},
            {"invert_selection",i18n("Invert"),"select-invert"},
            {"growselection",i18n("Grow"),"select-all"},
            {"shrinkselection",i18n("Shrink"),"select-all"},
            {"featherselection",i18n("Feather"),"select-all"},
            {"borderselection",i18n("Border"),"select-all"},
            {"smoothselection",i18n("Smooth"),"select-all"},
            {"selectopaque",i18n("Select opaque"),"select-all"},
            {"copy_selection_to_new_layer",i18n("Copy to layer"),"edit-copy"},
            {"cut_selection_to_new_layer",i18n("Cut to layer"),"edit-cut"}
        });
        addGroup(i18n("Transform"),{
            {"KritaTransform/KisToolMove",i18n("Move")},
            {"KisToolTransform",i18n("Transform")},
            {"KisToolCrop",i18n("Crop")}
        });
        // The floating context bar is intentionally hidden for Krita's
        // freehand brush so it does not stack another row over the canvas.
        // Expose its native brush-smoothing QActions here instead: these
        // operate on Krita's current brush tool, not a mobile imitation.
        addGroup(i18n("Brush smoothing"),{},{
            {"set_no_brush_smoothing",i18n("None"),"draw-freehand"},
            {"set_simple_brush_smoothing",i18n("Basic"),"draw-freehand"},
            {"set_weighted_brush_smoothing",i18n("Weighted"),"draw-freehand"},
            {"set_stabilizer_brush_smoothing",i18n("Stabilizer"),"draw-freehand"},
            {"set_pixel_perfect_smoothing",i18n("Pixel perfect"),"draw-freehand"}
        });
        addGroup(i18n("Other drawing tools"),{
            {"KritaShape/KisToolSmartPatch",i18n("Smart Patch")},
            {"KritaShape/KisToolLazyBrush",i18n("Colorize Mask")},
            {"KritaShape/KisToolDyna",i18n("Dynamic Brush")}
        },{
            {"mobile_set_clone_source",i18n("Set Clone source"),"krita_tool_color_sampler"}
        });
        addGroup(i18n("Edit"),{},{
            {"krita_filter_colorbalance",i18n("Color balance"),"preferences-desktop-color"},
            {"krita_filter_perchannel",i18n("Curves"),"filter-color-curves"},
            {"krita_filter_gaussian blur",i18n("Blur"),"filterLayer"}
        });
        addGroup(i18n("Shapes"),{
            {"KritaShape/KisToolLine",i18n("Line")},
            {"KritaShape/KisToolRectangle",i18n("Rectangle")},
            {"KritaShape/KisToolEllipse",i18n("Ellipse")},
            {"KisToolPolygon",i18n("Polygon")},
            {"KritaShape/KisToolPolygon",i18n("Polygon")},
            {"KisToolPolyline",i18n("Polyline")},
            {"KritaShape/KisToolPolyline",i18n("Polyline")},
            {"KisToolPath",i18n("Bézier")},
            {"KritaShape/KisToolBezierCurve",i18n("Bézier")},
            {"KritaShape/KisToolPath",i18n("Path")}
        });
        addGroup(i18n("View and Guides"),{
            {"KisAssistantTool",i18n("Assistants")},
            {"KritaShape/KisToolMultiBrush",i18n("Multibrush")},
            {"ToolReferenceImages",i18n("Reference images")},
            {"KritaShape/KisToolMeasure",i18n("Measure")},
            {"KritaShape/KisToolKnife",i18n("Knife")}
        },{
            {"hmirror_action",i18n("Horizontal"),"symmetry-horizontal"},
            {"vmirror_action",i18n("Vertical"),"symmetry-vertical"},
            {"view_show_guides",i18n("Guides"),"view-grid"}
        });
        addGroup(i18n("Canvas"),{},{
            {"zoom_to_fit",i18n("Fit"),"zoom-fit"},
            {"mirror_canvas",i18n("Flip view"),"mirror-view"},
            {"wrap_around_mode",i18n("Wrap"),"wraparound"},
            {"canvassize",i18n("Canvas size"),"tool_crop"},
            {"imagesize",i18n("Resize image"),"zoom-fit"}
        });

        for(const auto &tool:available) {
            if(used.contains(tool.id))continue;
            const QString section=tool.section.trimmed().isEmpty()?
                i18n("Other tools"):tool.section;
            auto group=std::find_if(groups.begin(),groups.end(),
                [&section](const Group &candidate) {return candidate.label==section;});
            if(group==groups.end()) {
                groups.append({section,{}});
                group=std::prev(groups.end());
            }
            group->tiles.append({tool.id,tool.label.isEmpty()?tool.id:tool.label,
                                 tool.description,tool.iconName,nullptr});
        }

        // Present drawing tools before secondary document commands.
        const QStringList primaryOrder={
            i18n("Paint and Fill"),i18n("Shapes"),i18n("Selection"),
            i18n("Transform"),i18n("View and Guides"),
            i18n("Other drawing tools")};
        std::stable_sort(groups.begin(),groups.end(),
            [&primaryOrder](const Group &a,const Group &b) {
                const int ia=primaryOrder.indexOf(a.label);
                const int ib=primaryOrder.indexOf(b.label);
                return (ia<0?primaryOrder.size():ia) <
                       (ib<0?primaryOrder.size():ib);
            });

        auto *host=qobject_cast<QMainWindow*>(parent);
        auto *panel=new MobileTransientPanel(host && host->centralWidget()?host->centralWidget():parent);
        panel->setObjectName(QStringLiteral("mobileToolBrowser"));
        panel->setOpener(opener,action);
        panel->setAccessibleName(i18n("Tools"));
        panel->setStyleSheet(QStringLiteral(
            "QFrame#mobileToolBrowser{background:#272729;border:1px solid #55555c;border-radius:9px;}"
            "QLabel{color:#efeff0;}"
            "QScrollArea,QWidget#toolTiles{background:#272729;border:0;}"
            "QToolButton{background:#303035;color:#efeff0;border:0;border-radius:5px;padding:0;}"
            "QToolButton:checked,QToolButton:pressed{background:#45454c;}"
            "QToolButton:disabled,QLabel:disabled{color:#929299;}"
            "QToolButton#toolFavorite{font-size:18px;}"
            "QToolButton#toolFavorite:checked{color:#ffd26a;}"));
        auto *layout=new QVBoxLayout(panel);
        layout->setContentsMargins(6,6,6,6);
        auto *scroll=new QScrollArea(panel);
        scroll->setWidgetResizable(true);
        MobileTouchScroll::configure(scroll);
        layout->addWidget(scroll);
        const auto refill=[panel,scroll,groups,current,engine,view]() {
            const QString settingsKey=QStringLiteral("MobileStudio/favoriteTools");
            const auto favorites=QSettings().value(settingsKey).toStringList();
            const int offset=scroll->verticalScrollBar()->value();
            auto *content=new QWidget(scroll);
            content->setObjectName(QStringLiteral("toolTiles"));
            auto *sections=new QVBoxLayout(content);
            sections->setContentsMargins(0,0,0,0);
            sections->setSpacing(3);
            const int columns=panel->width()<295?1:2;
            const int textSpace=qMax(40,(panel->width()-16)/columns-67);
            const auto addTiles=[&](const Group &group,bool pinned) {
                auto *grid=new QGridLayout;
                grid->setSpacing(3);
                int count=0;
                for(const Tile &tile:group.tiles) {
                    const QString id=tile.favoriteId();
                    if(pinned!=favorites.contains(id))continue;
                    auto *cell=new QWidget(content);
                    auto *row=new QHBoxLayout(cell);
                    row->setContentsMargins(0,0,0,0);
                    row->setSpacing(2);
                    auto *button=new QToolButton(cell);
                    button->setAccessibleName(tile.label);
                    button->setToolTip(tile.description.isEmpty()?tile.label:tile.description);
                    button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
                    button->setFixedHeight(40);
                    auto *tileLayout=new QHBoxLayout(button);
                    tileLayout->setContentsMargins(4,1,2,1);
                    tileLayout->setSpacing(4);
                    auto *symbol=new QLabel(button);
                    symbol->setPixmap(tileIcon(tile));
                    symbol->setAlignment(Qt::AlignCenter);
                    symbol->setAttribute(Qt::WA_TransparentForMouseEvents);
                    symbol->setFixedSize(22,24);
                    tileLayout->addWidget(symbol);
                    auto *label=new QLabel(
                        QFontMetrics(button->font()).elidedText(tile.label,Qt::ElideRight,textSpace),button);
                    label->setAttribute(Qt::WA_TransparentForMouseEvents);
                    label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
                    label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
                    tileLayout->addWidget(label,1);
                    button->setCheckable(true);
                    button->setChecked(tile.action?tile.action->isChecked():tile.tool==current);
                    button->setEnabled(!tile.action || tile.action->isEnabled());
                    QObject::connect(button,&QToolButton::clicked,panel,[panel,engine,view,tile]() {
                        auto *host=panel->parentWidget();
                        panel->close();
                        QTimer::singleShot(0,host,[engine,view,tile]() {
                            if(!view || view!=MobileViewIdentityGuard::uniqueActive(engine.openDocuments()))return;
                            if(tile.action && tile.action->isEnabled())tile.action->trigger();
                            else if(!tile.tool.isEmpty())engine.selectTool(tile.tool);
                        });
                    });
                    row->addWidget(button,1);

                    auto *star=new QToolButton(cell);
                    star->setObjectName(QStringLiteral("toolFavorite"));
                    star->setCheckable(true);
                    star->setChecked(pinned);
                    star->setText(pinned?QStringLiteral("★"):QStringLiteral("☆"));
                    star->setAccessibleName(pinned?i18n("Remove %1 from favorites",tile.label):
                                                   i18n("Favorite %1",tile.label));
                    star->setFixedSize(32,40);
                    QObject::connect(star,&QToolButton::clicked,panel,[panel,id,settingsKey](bool checked) {
                        QSettings settings;
                        auto saved=settings.value(settingsKey).toStringList();
                        saved.removeAll(id);
                        if(checked)saved.prepend(id);
                        settings.setValue(settingsKey,saved);
                        panel->requestRefresh();
                    });
                    row->addWidget(star);
                    grid->addWidget(cell,count/columns,count%columns);
                    ++count;
                }
                if(count==0) {
                    delete grid;
                    return;
                }
                for(int column=0;column<columns;++column)grid->setColumnStretch(column,1);
                auto *heading=new QLabel(group.label,content);
                heading->setStyleSheet(QStringLiteral(
                    "color:#aeb0bc;font-size:12px;font-weight:600;padding:4px 2px 1px;"));
                sections->addWidget(heading);
                sections->addLayout(grid);
            };
            Group pinned{i18n("Favorites"),{}};
            for(const auto &id:favorites) {
                for(const auto &group:groups) {
                    const auto match=std::find_if(group.tiles.cbegin(),group.tiles.cend(),
                        [&id](const Tile &tile) {return tile.favoriteId()==id;});
                    if(match!=group.tiles.cend()) {
                        pinned.tiles.append(*match);
                        break;
                    }
                }
            }
            addTiles(pinned,true);
            for(const auto &group:groups)addTiles(group,false);
            sections->addStretch();
            scroll->setWidget(content);
            scroll->verticalScrollBar()->setValue(offset);
        };
        panel->onRefresh=refill;
        panel->present();
        refill();
    }

private:
    static QWidget *findPanel(QWidget *parent)
    {
        if(!parent)return nullptr;
        for(auto *panel:parent->window()->findChildren<QWidget*>(QStringLiteral("mobileToolBrowser")))
            if(panel->isVisible())return panel;
        return nullptr;
    }

    struct ToolTile { const char *id; QString label; };
    struct ActionTile { const char *id; QString label; const char *icon; };
    struct Tile {
        QString tool,label,description,icon;
        QPointer<QAction> action;
        QString actionId={};
        QString favoriteId() const {
            return tool.isEmpty()?QStringLiteral("action/")+actionId:
                                  QStringLiteral("tool/")+tool;
        }
    };
    struct Group { QString label; QVector<Tile> tiles; };

    static QPixmap tileIcon(const Tile &tile)
    {
        QIcon native=tile.action?tile.action->icon():QIcon();
        if(native.isNull() && !tile.icon.isEmpty())
            native=KisIconUtils::loadIcon(tile.icon);
        QPixmap icon=native.pixmap(QSize(21,21));
        if(icon.isNull())return {};
        QPainter tint(&icon);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(icon.rect(),QColor(220,220,224));
        tint.end();
        return icon;
    }
};
#endif
