/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A touch grid over Krita's registered tools and native canvas actions.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
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
    // Returns true when the user asks to show native Tool Options.
    static bool open(const MobileEngineBridge &engine,QWidget *parent,
                     bool offerOptions=false)
    {
        if(!parent || !engine.hasDocument())return false;

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
                                        QString::fromLatin1(entry.icon),action});
                }
            }
            if(!group.tiles.isEmpty())groups.append(group);
        };
        addGroup(i18n("Draw"),{
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
        addGroup(i18n("Select"),{
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
        addGroup(i18n("Retouch and color"),{
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
        addGroup(i18n("Guides and symmetry"),{
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

        QDialog dialog(parent);
        dialog.setWindowTitle(i18n("Tools"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(QStringLiteral(
            "QDialog,QWidget#toolTiles{background:#272729;color:#efeff0;}"
            "QLabel{color:#efeff0;}"
            "QLineEdit,QPushButton{background:#343438;color:#efeff0;"
            "border:1px solid #515156;border-radius:8px;min-height:43px;padding:4px;}"
            "QScrollArea{background:#272729;border:0;}"
            "QToolButton{background:transparent;color:#efeff0;border:0;"
            "border-radius:8px;padding:3px;}"
            "QToolButton:checked,QToolButton:pressed{background:#45454c;}"
            "QToolButton:disabled,QLabel:disabled{color:#929299;}"));
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search tools"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        MobileTouchScroll::configure(scroll);
        layout->addWidget(scroll,1);

        auto *buttons=new QHBoxLayout;
        auto *close=new QPushButton(i18n("Close"),&dialog);
        buttons->addWidget(close);
        QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        constexpr int showOptions=2;
        if(offerOptions) {
            auto *options=new QPushButton(i18n("Tool Options"),&dialog);
            buttons->addWidget(options);
            QObject::connect(options,&QPushButton::clicked,&dialog,
                             [&dialog]() {dialog.done(showOptions);});
        }
        layout->addLayout(buttons);

        Tile requested;
        // Use two columns on very narrow displays instead of squeezing
        // four full tool tiles into unreadable touch targets.
        const int columns=parent->window()->width()<390?2:4;
        const auto refill=[&]() {
            const QString phrase=search->text().trimmed();
            auto *content=new QWidget(scroll);
            content->setObjectName(QStringLiteral("toolTiles"));
            auto *sections=new QVBoxLayout(content);
            sections->setContentsMargins(0,0,0,0);
            sections->setSpacing(8);
            for(const Group &group:groups) {
                auto *grid=new QGridLayout;
                grid->setSpacing(2);
                int count=0;
                for(const Tile &tile:group.tiles) {
                    if(!phrase.isEmpty() &&
                       !tile.label.contains(phrase,Qt::CaseInsensitive) &&
                       !tile.description.contains(phrase,Qt::CaseInsensitive) &&
                       !tile.tool.contains(phrase,Qt::CaseInsensitive) &&
                       !group.label.contains(phrase,Qt::CaseInsensitive))continue;
                    auto *button=new QToolButton(content);
                    button->setAccessibleName(tile.label);
                    button->setToolTip(tile.description);
                    button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
                    button->setFixedHeight(72);
                    auto *tileLayout=new QVBoxLayout(button);
                    tileLayout->setContentsMargins(2,4,2,4);
                    tileLayout->setSpacing(2);
                    auto *symbol=new QLabel(button);
                    symbol->setPixmap(tileIcon(tile));
                    symbol->setAlignment(Qt::AlignCenter);
                    symbol->setAttribute(Qt::WA_TransparentForMouseEvents);
                    symbol->setFixedHeight(26);
                    tileLayout->addWidget(symbol);
                    auto *label=new QLabel(tile.label,button);
                    label->setWordWrap(true);
                    label->setAlignment(Qt::AlignCenter);
                    label->setAttribute(Qt::WA_TransparentForMouseEvents);
                    label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
                    tileLayout->addWidget(label);
                    button->setCheckable(true);
                    button->setChecked(tile.action?tile.action->isChecked():tile.tool==current);
                    button->setEnabled(!tile.action || tile.action->isEnabled());
                    QObject::connect(button,&QToolButton::clicked,&dialog,[&,tile]() {
                        requested=tile;
                        dialog.accept();
                    });
                    grid->addWidget(button,count/columns,count%columns);
                    ++count;
                }
                if(count==0) {
                    delete grid;
                    continue;
                }
                for(int column=0;column<columns;++column)grid->setColumnStretch(column,1);
                auto *heading=new QLabel(group.label,content);
                heading->setStyleSheet(QStringLiteral("font-weight:600;padding:4px;"));
                sections->addWidget(heading);
                sections->addLayout(grid);
            }
            if(sections->count()==0)
                sections->addWidget(new QLabel(i18n("No matching tools"),content));
            sections->addStretch();
            scroll->setWidget(content);
        };
        QObject::connect(search,&QLineEdit::textChanged,&dialog,
                         [&refill](const QString &) {refill();});
        refill();
        const QSize bounds=parent->window()->size();
        dialog.resize(qMin(520,qMax(1,bounds.width()-20)),
                      qMin(710,qMax(1,bounds.height()-26)));
        dialog.setMaximumSize(qMax(1,bounds.width()-12),qMax(1,bounds.height()-12));
        close->setFocus();
        const int result=dialog.exec();
        if(result==showOptions)return true;
        if(result!=QDialog::Accepted || !engine.hasDocument())return false;
        if(requested.action && requested.action->isEnabled()) {
            requested.action->trigger();
        } else if(!requested.tool.isEmpty()) {
            engine.selectTool(requested.tool);
        }
        return false;
    }

private:
    struct ToolTile { const char *id; QString label; };
    struct ActionTile { const char *id; QString label; const char *icon; };
    struct Tile {
        QString tool,label,description,icon;
        QPointer<QAction> action;
    };
    struct Group { QString label; QVector<Tile> tiles; };

    static QPixmap tileIcon(const Tile &tile)
    {
        QIcon native=tile.action?tile.action->icon():QIcon();
        if(native.isNull() && !tile.icon.isEmpty())
            native=KisIconUtils::loadIcon(tile.icon);
        QPixmap icon=native.pixmap(QSize(26,26));
        if(icon.isNull())return {};
        QPainter tint(&icon);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(icon.rect(),QColor(220,220,224));
        tint.end();
        return icon;
    }
};
#endif
