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
#include <QScroller>
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
                for(const auto &tool:available) {
                    if(tool.id!=id)continue;
                    group.tiles.append({id,entry.label,tool.description,tool.iconName,nullptr});
                    used.insert(id);
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
            {"SvgTextTool",i18n("Text")},
            {"KritaSelected/KisToolColorSampler",i18n("Eyedropper")}
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
        addGroup(i18n("Transform"),{
            {"KritaTransform/KisToolMove",i18n("Move")},
            {"KisToolTransform",i18n("Transform")},
            {"KisToolCrop",i18n("Crop")}
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
            {"KritaShape/KisToolPath",i18n("Path")}
        });
        addGroup(i18n("Guides and symmetry"),{
            {"KisAssistantTool",i18n("Assistants")},
            {"KritaShape/KisToolMultiBrush",i18n("Multibrush")}
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
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(scroll->viewport(),QScroller::LeftMouseButtonGesture);
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
                    grid->addWidget(button,count/4,count%4);
                    ++count;
                }
                if(count==0) {
                    delete grid;
                    continue;
                }
                for(int column=0;column<4;++column)grid->setColumnStretch(column,1);
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
        dialog.resize(qMax(260,qMin(520,bounds.width()-20)),
                      qMax(220,qMin(710,bounds.height()-26)));
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
