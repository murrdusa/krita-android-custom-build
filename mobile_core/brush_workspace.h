/*
 * Independent mobile brush controls: all strokes and resources still use
 * Krita's own paint engines. No Q_OBJECT; usable in Android QWidget shell.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QFile>
#include <QCheckBox>
#include <QInputMethod>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QListView>
#include <QTreeWidget>
#include <QListWidget>
#include <QUuid>
#include "transient_panel.h"
#include "touch_action_picker.h"
#include <factory_brush_preset.h>
#include <KoResourceBundle.h>
#include <KisGlobalResourcesInterface.h>
#include <KisResourceUserOperations.h>
#include <QSortFilterProxyModel>
#include <QPointer>
#include <QPalette>
#include <QPushButton>
#include "touch_scroll.h"
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QPixmap>
#include <QPixmapCache>
#include <QImage>
#include <QShowEvent>
#include <QHideEvent>

#include "engine_bridge.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_slider_spin_box.h"
#include "kis_image_config.h"
#include <KisResourceModel.h>
#include <brushengine/kis_paintop_preset.h>
#include <KoToolManager.h>
#include <KoCompositeOpRegistry.h>
#include "kis_node.h"
#include <brushengine/kis_paintop_settings.h>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <functional>
#include <memory>
#include <cmath>
#include <klocalizedstring.h>

class KritaMobileBrushWorkspace final : public QFrame {
public:
    explicit KritaMobileBrushWorkspace(const MobileEngineBridge &engine,
                                       QWidget *parent,bool slidersOnly=false,
                                       bool toolSettings=false)
        : QFrame(parent),m_engine(engine),m_hasSelectedMode(!slidersOnly)
    {
        setObjectName(QStringLiteral("mobileBrushWorkspace"));
        setStyleSheet(
            "QFrame#mobileBrushWorkspace{background:#252529;"
            "border:1px solid #48484e;border-radius:13px;}"
            "QToolButton{background:#34343a;color:#f4f4f5;"
            "border:1px solid #55555c;border-radius:8px;padding:5px;"
            "font-size:13px;}"
            "QToolButton:checked{background:#276b8b;border:2px solid #a5d9f0;}"
            "QLabel{color:#e9f4fd;}");
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(7,7,7,7);
        root->setSpacing(5);
        if(!slidersOnly) {
            auto *buttons=new QHBoxLayout;
            buttons->setSpacing(5);
            const QStringList labels={i18n("Paint"),i18n("Erase"),i18n("Blend")};
            for(int i=0;i<3;++i) {
                auto *button=new QToolButton(this);
                button->setText(labels.at(i));
                button->setMinimumHeight(42);
                button->setCheckable(true);
                button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
                m_modes[i]=button;
                buttons->addWidget(button,1);
                connect(button,&QToolButton::clicked,this,[this,i,button]() {
                    if(m_hasSelectedMode && m_slot==i) {
                        openPresetBrowser(i,nullptr,button);
                    } else switchMode(i);
                });
            }
            auto *browse=new QToolButton(this);
            browse->setText(i18n("Presets"));
            browse->setMinimumHeight(42);
            browse->setToolTip(i18n("Search Krita brush presets"));
            buttons->addWidget(browse,1);
            connect(browse,&QToolButton::clicked,this,[this,browse]() {
                openPresetBrowser(m_slot,nullptr,browse);
            });
            m_detailToggle=new QToolButton(this);
            m_detailToggle->setToolTip(i18n("Show or hide detailed brush controls"));
            m_detailToggle->setMinimumSize(36,42);
            buttons->addWidget(m_detailToggle);
            connect(m_detailToggle,&QToolButton::clicked,this,[this]() {
                setDetailsExpanded(!m_detailsExpanded);
            });
            root->addLayout(buttons);

        }
        m_controls=new QWidget(this);
        auto *details=new QVBoxLayout(m_controls);
        details->setContentsMargins(0,0,0,0);
        details->setSpacing(4);
        auto *controlRow=new QHBoxLayout;
        controlRow->setContentsMargins(0,0,0,0);
        controlRow->setSpacing(6);
        if(!slidersOnly || toolSettings) {
            m_presetName=new PresetLabel(m_controls);
            details->addWidget(m_presetName);
        }
        m_size=new KisDoubleSliderSpinBox(this);
        m_size->setRange(0.01,KisImageConfig(true).maxBrushSize(),2);
        m_size->setExponentRatio(3.0);
        m_size->setPrefix(i18n("Size: "));
        m_size->setSuffix(i18n(" px"));
        m_size->setSingleStep(1);
        m_size->setMinimumHeight(43);
        controlRow->addWidget(m_size,2);
        m_opacity=new KisDoubleSliderSpinBox(this);
        m_opacity->setRange(0,100,0);
        m_opacity->setPrefix(i18n("Opacity: "));
        m_opacity->setSuffix(QStringLiteral("%"));
        m_opacity->setSingleStep(5);
        m_opacity->setMinimumHeight(43);
        controlRow->addWidget(m_opacity,2);
        if(slidersOnly) {
            for(QWidget *slider:{m_size,m_opacity}) {
                QPalette colors=slider->palette();
                colors.setColor(QPalette::Base,QColor(QStringLiteral("#34343a")));
                colors.setColor(QPalette::Button,QColor(QStringLiteral("#34343a")));
                colors.setColor(QPalette::Text,Qt::white);
                colors.setColor(QPalette::ButtonText,Qt::white);
                colors.setColor(QPalette::Highlight,QColor(QStringLiteral("#32616b")));
                colors.setColor(QPalette::HighlightedText,Qt::white);
                slider->setPalette(colors);
            }
        }
        if(slidersOnly && !toolSettings) {
            setStyleSheet(QStringLiteral(
                "QFrame#mobileBrushWorkspace{background:rgba(28,31,37,150);"
                "border:0;border-radius:8px;}"));
            for(auto *slider:{m_size,m_opacity}) {
                slider->setButtonSymbols(QAbstractSpinBox::NoButtons);
                slider->setFixedHeight(36);
                QPalette colors=slider->palette();
                colors.setColor(QPalette::Base,QColor(38,46,55,160));
                colors.setColor(QPalette::Button,QColor(38,46,55,160));
                slider->setPalette(colors);
                slider->setStyleSheet(QStringLiteral(
                    "QAbstractSpinBox{background:transparent;border:0;padding:0 5px;}"));
            }
        }
        details->addLayout(controlRow);
        if(toolSettings) {
            m_flow=new KisDoubleSliderSpinBox(m_controls);
            m_flow->setRange(0,100,0);
            m_flow->setPrefix(i18n("Flow: "));
            m_flow->setSuffix(QStringLiteral("%"));
            m_flow->setMinimumHeight(44);
            details->addWidget(m_flow);
            connect(m_flow,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),
                    this,[this](qreal percent) {
                auto *provider=resources();
                if(m_syncing || !provider)return;
                const auto preset=provider->currentPreset();
                if(!supportsFlow(preset))return;
                preset->settings()->setPaintOpFlow(percent/100.0);
                provider->setFlow(percent/100.0);
            });

        }
        if(toolSettings) {
            const auto slider=[this,details](const QString &label,int minimum,int maximum,const QString &suffix) {
                auto *control=new KisDoubleSliderSpinBox(m_controls);
                control->setRange(minimum,maximum,0);
                control->setPrefix(label+QStringLiteral(" "));
                control->setSuffix(suffix);
                control->setMinimumHeight(44);
                details->addWidget(control);
                return control;
            };
            m_angle=slider(i18n("Tip angle:"),-180,180,QStringLiteral("°"));
            m_smudgeStrength=slider(i18n("Smudge strength:"),0,100,QStringLiteral("%"));
            m_smudgeRadius=slider(i18n("Smudge radius:"),0,300,QStringLiteral("%"));
            m_sampleLayers=new QCheckBox(i18n("Sample merged layers"),m_controls);
            m_sampleLayers->setMinimumHeight(44);
            m_sampleLayers->setStyleSheet(QStringLiteral("color:#f4f4f5;"));
            details->addWidget(m_sampleLayers);
            const auto settings=[this]() -> KisPaintOpSettingsSP {
                auto *provider=resources();
                const auto preset=provider?provider->currentPreset():KisPaintOpPresetSP{};
                return !m_syncing && preset?preset->settings():KisPaintOpSettingsSP{};
            };
            connect(m_angle,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),this,[settings](qreal degrees) {
                if(auto active=settings())active->setPaintOpAngle(degrees);
            });
            connect(m_smudgeStrength,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),this,[settings](qreal value) {
                if(auto active=settings()) {
                    active->setProperty(QStringLiteral("PressureSmudgeRate"),true);
                    active->setProperty(QStringLiteral("SmudgeRateValue"),value/100);
                }
            });
            connect(m_smudgeRadius,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),this,[settings](qreal value) {
                if(auto active=settings()) {
                    active->setProperty(QStringLiteral("PressureSmudgeRadius"),true);
                    active->setProperty(QStringLiteral("SmudgeRadiusVersion"),2);
                    active->setProperty(QStringLiteral("SmudgeRadiusValue"),value/100);
                }
            });
            connect(m_sampleLayers,&QCheckBox::toggled,this,[settings](bool enabled) {
                if(auto active=settings())active->setProperty(QStringLiteral("MergedPaint"),enabled);
            });
        }
        if(toolSettings) {
            m_composite=new QPushButton(i18n("Brush blending mode…"),m_controls);
            m_composite->setAccessibleName(i18n("Brush blending mode"));
            m_composite->setToolTip(i18n("Brush blending mode"));
            m_composite->setMinimumHeight(44);
            details->addWidget(m_composite);
            connect(m_composite,&QPushButton::clicked,this,
                    [this]() {openBrushBlendModes();});
            if(auto *editor=m_engine.action("show_brush_editor")) {
                auto *advanced=new QPushButton(i18n("Advanced Brush Settings"),m_controls);
                advanced->setMinimumHeight(44);
                advanced->setEnabled(editor->isEnabled());
                connect(editor,&QAction::changed,advanced,[advanced,editor]() {
                    advanced->setEnabled(editor->isEnabled());
                });
                connect(advanced,&QPushButton::clicked,this,[this,editor]() {
                    hide();
                    if(editor->isEnabled())editor->trigger();
                });
                details->addWidget(advanced);
            }
        }
        if(toolSettings) {
            auto *scroll=new QScrollArea(this);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            MobileTouchScroll::configure(scroll);
            scroll->setWidget(m_controls);
            root->addWidget(scroll);
        } else {
            root->addWidget(m_controls);
        }
        if(slidersOnly) {
            root->setContentsMargins(2,2,2,2);
        } else {
            setDetailsExpanded(QSettings().value(
                QStringLiteral("MobileStudio/brushDetailsExpanded"),true).toBool());
        }

        connect(m_size,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),
                this,[this](qreal size) {
            if(m_syncing)return;
            if(auto *provider=resources())provider->setSize(size);
        });
        connect(m_opacity,QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged),
                this,[this](qreal percent) {
            if(m_syncing)return;
            if(auto *provider=resources())provider->setOpacity(percent/100.0);
        });
        if(auto *provider=resources()) {
            connect(provider,&KisCanvasResourceProvider::sigPaintOpPresetChanged,
                    this,[this](KisPaintOpPresetSP preset) {
                if(!preset)return;
                if(!m_internalChange && isActiveMode(m_slot))
                    rememberPreset(m_slot,preset);
                refresh();
            });
        }
        m_refresh=new QTimer(this);
        m_refresh->setInterval(700);
        connect(m_refresh,&QTimer::timeout,this,[this]() {refresh();});
        // Hidden brushes/sliders should not poll the engine. The show
        // event starts a single timer and refreshes immediately.
        refresh();
    }

    bool isActiveMode(int slot) const
    {
        if(m_engine.activeTool().section(QLatin1Char('/'),-1)!=QStringLiteral("KisToolBrush"))
            return false;
        const QAction *eraser=m_engine.action("erase_action");
        if(eraser && eraser->isChecked())return slot==1;
        if(slot==1)return false;
        const bool blending=resources() && capabilities(resources()->currentPreset()).blend;
        return slot==(blending?2:0);
    }

    bool activateCloneBrush()
    {
        auto *provider=resources();
        if(!provider)return false;
        auto preset=provider->currentPreset();
        if(!preset || preset->paintOp().id()!=QStringLiteral("duplicate")) {
            preset.clear();
            auto *source=model();
            for(int row=0;row<source->rowCount();++row) {
                auto candidate=source->resourceForIndex(source->index(row,0))
                                     .dynamicCast<KisPaintOpPreset>();
                if(candidate && candidate->valid() &&
                   candidate->paintOp().id()==QStringLiteral("duplicate")) {
                    preset=candidate;
                    break;
                }
            }
            if(!preset)return false;
        }
        m_engine.freehandBrush();
        if(provider->currentPreset()!=preset)setNativePreset(preset);
        setEraser(false);
        m_slot=0;
        m_hasSelectedMode=true;
        refresh();
        return provider->currentPreset() &&
               provider->currentPreset()->paintOp().id()==QStringLiteral("duplicate");
    }

    void activateMode(int slot) { switchMode(slot); }
    void browsePresets(int slot,QAction *action) {openPresetBrowser(slot,action);}
    void dismissPresetBrowser() {if(m_presetBrowser)m_presetBrowser->close();}

    void setSliderVisibility(bool size,bool opacity)
    {
        m_size->setVisible(size);
        m_opacity->setVisible(opacity);
    }

    void refresh()
    {
        auto *provider=resources();
        if(!provider)return;
        m_syncing=true;
        m_size->setValue(provider->size());
        m_opacity->setValue(provider->opacity()*100.0);
        if(m_flow) {
            m_flow->setVisible(supportsFlow(provider->currentPreset()));
            m_flow->setValue(provider->flow()*100.0);
        }
        if(m_angle) {
            const auto preset=provider->currentPreset();
            const auto settings=preset?preset->settings():KisPaintOpSettingsSP{};
            const bool tip=settings && !settings->getString(QStringLiteral("brush_definition")).isEmpty();
            const bool smudge=isSmudge(preset) && !provider->eraserMode();
            m_angle->setVisible(tip);
            if(tip)m_angle->setValue(std::remainder(settings->paintOpAngle(),360.0));
            m_smudgeStrength->setVisible(smudge);
            m_sampleLayers->setVisible(smudge);
            const bool dulling=smudge && settings->getInt(QStringLiteral("SmudgeRateMode"))==1;
            m_smudgeRadius->setVisible(dulling);
            if(smudge) {
                m_smudgeStrength->setValue(settings->getBool(QStringLiteral("PressureSmudgeRate"))?
                    settings->getDouble(QStringLiteral("SmudgeRateValue"),1)*100:100);
                m_sampleLayers->setChecked(settings->getBool(QStringLiteral("MergedPaint")));
            }
            if(dulling) {
                m_smudgeRadius->setRange(0,settings->getBool(QStringLiteral("SmudgeRateUseNewEngine"))?100:300,0);
                const double scale=settings->getInt(QStringLiteral("SmudgeRadiusVersion"),1)<2?1:100;
                m_smudgeRadius->setValue(settings->getBool(QStringLiteral("PressureSmudgeRadius"))?
                    settings->getDouble(QStringLiteral("SmudgeRadiusValue"))*scale:0);
            }
        }
        if(m_composite) {
            auto *view=m_engine.manager();
            const auto node=view?view->activeNode():KisNodeSP();
            const auto *space=node?node->colorSpace():nullptr;
            const bool eraser=provider->eraserMode();
            const QString mode=provider->currentCompositeOp();
            // Registry category walks and widget relayout are only needed
            // when the active layer's color space or brush mode changes.
            if(space!=m_compositeSpace || eraser!=m_compositeEraser ||
               mode!=m_compositeMode) {
                m_compositeSpace=space;
                m_compositeEraser=eraser;
                m_compositeMode=mode;
                const auto &registry=KoCompositeOpRegistry::instance();
                const bool available=space && !registry.getCompositeOps(space).isEmpty();
                m_composite->setVisible(available && !eraser);
                const QString name=registry.getCompositeOpDisplayName(mode);
                m_composite->setText(i18n("Blending mode: %1").arg(
                    name.isEmpty()?i18n("Normal"):name));
            }
        }
        if(m_presetName) {
            const auto preset=provider->currentPreset();
            m_presetName->setPreset(preset?preset->name():i18n("Current brush"));
        }
        m_syncing=false;
        for(int i=0;i<3;++i)
            if(m_modes[i])m_modes[i]->setChecked(m_hasSelectedMode && m_slot==i);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QFrame::showEvent(event);
        if(m_refresh)m_refresh->start();
        refresh();
    }

    void hideEvent(QHideEvent *event) override
    {
        if(m_refresh)m_refresh->stop();
        QFrame::hideEvent(event);
    }

private:
    // Match Layers' Favorites-first, categorized native composite picker.
    // Only apply an operation if this is still the same active canvas.
    void openBrushBlendModes()
    {
        auto *provider=resources();
        auto *view=m_engine.manager();
        const auto node=view?view->activeNode():KisNodeSP();
        const auto *space=node?node->colorSpace():nullptr;
        if(!provider || provider->eraserMode() || !space)return;
        const auto &registry=KoCompositeOpRegistry::instance();
        const auto modes=registry.getCompositeOps(space);
        if(modes.isEmpty())return;
        const KisImageSP original=view->image().toStrongRef();
        if(!original)return;
        if(MobileTransientPanel::closeForAnchor(parentWidget(),m_composite))return;
        auto *panel=new MobileTransientPanel(parentWidget());
        panel->setAccessibleName(i18n("Brush blending mode"));
        panel->setObjectName(QStringLiteral("mobileBrushComposite"));
        panel->setStyleSheet(QStringLiteral(
            "QFrame#mobileBrushComposite,QTreeWidget{background:#252529;color:#f4f4f5;}"
            "QTreeWidget::item{min-height:40px;}"));
        auto *layout=new QVBoxLayout(panel);
        auto *tree=new QTreeWidget(panel);
        tree->setHeaderHidden(true);
        MobileTouchScroll::configureList(tree);
        layout->addWidget(tree);
        const QString current=provider->currentCompositeOp();
        const auto addMode=[&](QTreeWidgetItem *parent,const KoID &mode) {
            auto *item=new QTreeWidgetItem(parent,{mode.name()});
            item->setData(0,Qt::UserRole,mode.id());
            if(mode.id()==current)item->setCheckState(0,Qt::Checked);
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        };
        auto *favorites=new QTreeWidgetItem(tree,{i18n("Favorites")});
        for(const auto &id:{COMPOSITE_OVER,COMPOSITE_MULT,COMPOSITE_OVERLAY,
                            COMPOSITE_SCREEN,COMPOSITE_ERASE,COMPOSITE_DARKEN,
                            COMPOSITE_LIGHTEN,COMPOSITE_DODGE,COMPOSITE_BURN}) {
            for(const auto &mode:modes)
                if(mode.id()==id) {addMode(favorites,mode);break;}
        }
        favorites->setExpanded(true);
        for(const auto &category:registry.getCategories()) {
            const auto categoryModes=registry.getCompositeOps(category,space);
            if(categoryModes.isEmpty())continue;
            auto *entry=new QTreeWidgetItem(tree,{category.name()});
            for(const auto &mode:categoryModes)addMode(entry,mode);
        }
        connect(tree,&QTreeWidget::itemClicked,this,
                [this,panel,view,provider,space,original](QTreeWidgetItem *item,int) {
            const QString id=item->data(0,Qt::UserRole).toString();
            if(id.isEmpty()) {item->setExpanded(!item->isExpanded());return;}
            panel->close();
            auto *now=m_engine.manager();
            if(now!=view || !now || now->image().toStrongRef()!=original ||
               resources()!=provider || provider->eraserMode() ||
               !now->activeNode() || now->activeNode()->colorSpace()!=space)return;
            if(space->compositeOp(id)) {
                provider->setCurrentCompositeOp(id);
                refresh();
            }
        });
        panel->anchorTo(m_composite,QSize(300,365));
        panel->present();
    }

    static QString displayPresetName(QString name)
    {
        static const QRegularExpression categoryPrefix(QStringLiteral(R"(^[a-z]\)\s+)"));
        return name.remove(categoryPrefix);
    }

    class PresetLabel final : public QLabel
    {
    public:
        explicit PresetLabel(QWidget *parent) : QLabel(parent)
        {
            setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
            setContentsMargins(5,3,5,3);
            setStyleSheet(QStringLiteral(
                "background:#252529;color:#f4f4f5;font-size:14px;"));
        }

        void setPreset(const QString &name)
        {
            if(m_name==name)return;
            m_name=name;
            setAccessibleName(name);
            setToolTip(name);
            fit();
        }

    protected:
        void resizeEvent(QResizeEvent *event) override
        {
            QLabel::resizeEvent(event);
            fit();
        }

    private:
        void fit()
        {
            setText(fontMetrics().elidedText(displayPresetName(m_name),
                Qt::ElideRight,qMax(0,contentsRect().width())));
        }

        QString m_name;
    };

    class PresetRow final : public QStyledItemDelegate
    {
    public:
        PresetRow(QListView *view,std::function<void(const QString &)> onFavorite)
            : QStyledItemDelegate(view),m_view(view),m_onFavorite(std::move(onFavorite)),
              m_favorites(QSettings().value(
                  QStringLiteral("MobileStudio/brushFavorites")).toStringList()) {}

        QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override
        {
            return QSize(1,58);
        }

        void paint(QPainter *painter,const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRect row=option.rect.adjusted(3,2,-3,-2);
            if(option.state & QStyle::State_Selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor("#394459"));
                painter->drawRoundedRect(row,8,8);
            }

            const QRect preview(row.left()+5,row.center().y()-21,42,42);
            QPainterPath clip;
            clip.addRoundedRect(preview,7,7);
            painter->save();
            painter->setClipPath(clip);
            painter->fillRect(preview,QColor("#363942"));
            index.data(Qt::DecorationRole).value<QIcon>().paint(painter,preview);
            painter->restore();

            const QRect star=starArea(option.rect);
            const QRect caption(preview.right()+10,row.top(),
                                qMax(0,star.left()-preview.right()-12),row.height());
            QFont font=option.font;
            font.setPixelSize(14);
            painter->setFont(font);
            painter->setPen(QColor("#f1f2f5"));
            painter->drawText(caption,Qt::AlignVCenter|Qt::AlignLeft,
                painter->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,caption.width()));

            const bool favorite=m_favorites.contains(resourceName(index));
            font.setPixelSize(21);
            painter->setFont(font);
            painter->setPen(favorite?QColor("#ffd478"):QColor("#9da4b3"));
            painter->drawText(star,Qt::AlignCenter,favorite?QStringLiteral("★"):QStringLiteral("☆"));
            painter->restore();
        }

        bool editorEvent(QEvent *event,QAbstractItemModel *,const QStyleOptionViewItem &option,
                         const QModelIndex &index) override
        {
            if(event->type()!=QEvent::MouseButtonRelease)return false;
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton || !starArea(option.rect).contains(mouse->pos()))return false;
            const QString name=resourceName(index);
            if(name.isEmpty())return false;
            m_view->setProperty("_mobileFavoriteTap",true);
            m_onFavorite(name);
            m_favorites=QSettings().value(
                QStringLiteral("MobileStudio/brushFavorites")).toStringList();
            m_view->viewport()->update();
            QTimer::singleShot(0,m_view,[view=m_view]() {
                view->setProperty("_mobileFavoriteTap",false);
            });
            return true;
        }

    private:
        static QRect starArea(const QRect &row)
        {
            return QRect(row.right()-43,row.top(),44,row.height());
        }

        static QString resourceName(const QModelIndex &index)
        {
            const auto *proxy=qobject_cast<const QSortFilterProxyModel*>(index.model());
            const auto source=proxy?proxy->mapToSource(index):index;
            return source.sibling(source.row(),KisAbstractResourceModel::Name).data().toString();
        }

        QListView *m_view;
        std::function<void(const QString &)> m_onFavorite;
        QStringList m_favorites;
    };

    static bool supportsFlow(const KisPaintOpPresetSP &preset)
    {
        return preset && (preset->paintOp().id()==QStringLiteral("paintbrush") || isSmudge(preset));
    }

    static bool isSmudge(const KisPaintOpPresetSP &preset)
    {
        return preset && preset->paintOp().id()==QStringLiteral("colorsmudge");
    }

    void setDetailsExpanded(bool expanded)
    {
        m_detailsExpanded=expanded;
        if(m_controls)m_controls->setVisible(expanded);
        if(m_detailToggle)m_detailToggle->setText(
            expanded?QStringLiteral("−"):QStringLiteral("+"));
        QSettings().setValue(
            QStringLiteral("MobileStudio/brushDetailsExpanded"),expanded);
    }
    struct PresetCapabilities {
        bool paint=false;
        bool eraser=false;
        bool blend=false;
        bool convertible=false;

        bool supports(int slot) const
        {
            if(slot==0)return paint;
            if(slot==1)return paint || eraser;
            return blend || convertible;
        }
    };

    static PresetCapabilities capabilities(const KisPaintOpPresetSP &preset)
    {
        if(!preset || !preset->valid() || !preset->settings())return {};
        const auto settings=preset->settings();
        const QString engine=preset->paintOp().id();
        const bool eraser=settings->getString(QStringLiteral("CompositeOp"))==COMPOSITE_ERASE;
        const bool smudgeOnly=isSmudge(preset) &&
            (!settings->getBool(QStringLiteral("PressureColorRate")) ||
             settings->getDouble(QStringLiteral("ColorRateValue"),1.0)<=0.0);
        const bool samplesCanvas=
            (engine==QStringLiteral("gridbrush") &&
             settings->getBool(QStringLiteral("ColorOption/sampleInputColor"))) ||
            (engine==QStringLiteral("hairybrush") &&
             settings->getBool(QStringLiteral("HairyInk/enabled")) &&
             settings->getBool(QStringLiteral("HairyInk/soak")));
        const bool effect=engine==QStringLiteral("duplicate") ||
                          engine==QStringLiteral("filter") ||
                          engine==QStringLiteral("deform");
        const bool paint=!eraser && !smudgeOnly && !samplesCanvas && !effect;
        return {paint,eraser && !effect,!eraser && (smudgeOnly || samplesCanvas),
                paint && !settings->getString(QStringLiteral("brush_definition")).isEmpty()};
    }

    static int paintingFamily(const QString &name)
    {
        const QString text=displayPresetName(name).toLower();
        if(text.contains(QStringLiteral("basic")) &&
           !text.contains(QStringLiteral("water")))return 9;
        if(text.contains(QStringLiteral("watercolor")) || text.contains(QStringLiteral("waterc")))return 14;
        if(text.contains(QStringLiteral("airbrush")))return 15;
        if(text.contains(QStringLiteral("pencil")) || text.contains(QStringLiteral("chalk")) ||
           text.contains(QStringLiteral("charcoal")) || text.contains(QStringLiteral("pastel")))return 10;
        if(text.contains(QStringLiteral("ink")) || text.contains(QStringLiteral("pen")) ||
           text.contains(QStringLiteral("marker")))return 11;
        if(text.contains(QStringLiteral("texture")) || text.contains(QStringLiteral("stamp")) ||
           text.contains(QStringLiteral("pattern")) || text.contains(QStringLiteral("spray")))return 12;
        return 13;
    }

    static QString presetCategoryKey(int id)
    {
        return QStringLiteral("MobileStudio/brushCategory/%1").arg(id);
    }

    static QStringList customCategories()
    {
        return QSettings().value(QStringLiteral("MobileStudio/brushCategories")).toStringList();
    }

    class PresetFilter final : public QSortFilterProxyModel
    {
    public:
        PresetFilter(KisResourceModel *source,int slot,QObject *parent)
            : QSortFilterProxyModel(parent),m_source(source),m_category(slot),
              m_slot(slot)
        {
            const auto clearCapabilities=[this]() {m_capabilities.clear();};
            connect(source,&QAbstractItemModel::dataChanged,this,clearCapabilities);
            connect(source,&QAbstractItemModel::modelReset,this,clearCapabilities);
            connect(source,&QAbstractItemModel::rowsRemoved,this,clearCapabilities);
            setSourceModel(source);
            setDynamicSortFilter(true);
            reloadPersonalLists();
            sort(KisAbstractResourceModel::Name);
        }

        bool hasFamily(int family) const
        {
            for(int row=0;row<m_source->rowCount();++row) {
                const QString name=m_source->index(row,KisAbstractResourceModel::Name).data().toString();
                if(paintingFamily(name)==family && rowCapabilities(row).convertible)return true;
            }
            return false;
        }

        void setCategory(int category,const QString &custom={})
        {
            if(m_category==category && m_custom==custom)return;
            m_category=category;
            m_custom=custom;
            invalidate();
            sort(KisAbstractResourceModel::Name);
        }

        void reloadPersonalLists()
        {
            const QSettings settings;
            m_assignments.clear();
            m_favorites=settings.value(
                QStringLiteral("MobileStudio/brushFavorites")).toStringList();
            m_recent=settings.value(
                QStringLiteral("MobileStudio/brushRecent%1").arg(m_slot)).toStringList();
            invalidate();
            sort(KisAbstractResourceModel::Name);
        }

        void setSearch(const QString &search)
        {
            m_search=search.trimmed();
            invalidateFilter();
        }

        QVariant data(const QModelIndex &index,int role) const override
        {
            if(role==Qt::DecorationRole && index.isValid()) {
                const auto source=mapToSource(index);
                const auto thumbnail=source.sibling(source.row(),KisAbstractResourceModel::Thumbnail)
                    .data(Qt::DecorationRole).value<QImage>();
                if(thumbnail.isNull())return {};
                // Image cacheKey changes when the underlying thumbnail
                // changes. Use Qt's bounded, shared pixmap cache instead of
                // rescaling the same resource on every item repaint.
                const QString cacheKey=QStringLiteral("mobile-preset-thumb-56-%1")
                    .arg(thumbnail.cacheKey());
                QPixmap scaled;
                if(!QPixmapCache::find(cacheKey,&scaled)) {
                    scaled=QPixmap::fromImage(thumbnail.scaled(
                        QSize(56,56),Qt::KeepAspectRatio,Qt::SmoothTransformation));
                    QPixmapCache::insert(cacheKey,scaled);
                }
                return QIcon(scaled);
            }
            const auto value=QSortFilterProxyModel::data(index,role);
            if(index.column()==KisAbstractResourceModel::Name && role==Qt::DisplayRole)
                return displayPresetName(value.toString());
            return value;
        }

    protected:
        bool lessThan(const QModelIndex &left,const QModelIndex &right) const override
        {
            const QStringList *order=m_category==-5?&m_recent:
                                     m_category==-4?&m_favorites:nullptr;
            if(order || m_category==-1) {
                const QString a=left.sibling(left.row(),KisAbstractResourceModel::Name).data().toString();
                const QString b=right.sibling(right.row(),KisAbstractResourceModel::Name).data().toString();
                if(order) {
                    const int ia=order->indexOf(a),ib=order->indexOf(b);
                    if(ia>=0 && ib>=0 && ia!=ib)return ia<ib;
                } else {
                    const int ia=paintingFamily(a),ib=paintingFamily(b);
                    if(ia!=ib)return ia<ib;
                }
            }
            return QSortFilterProxyModel::lessThan(left,right);
        }

        bool filterAcceptsRow(int row,const QModelIndex &parent) const override
        {
            const QString name=m_source->index(row,KisAbstractResourceModel::Name,parent)
                .data().toString();
            if(!name.contains(m_search,Qt::CaseInsensitive))return false;
            if(m_category==-4 && !m_favorites.contains(name))return false;
            if(m_category==-5 && !m_recent.contains(name))return false;
            if(m_category>=9 || !m_custom.isEmpty()) {
                const int id=m_source->index(row,KisAbstractResourceModel::Id,parent).data().toInt();
                auto it=m_assignments.constFind(id);
                const QString custom=it!=m_assignments.cend()?it.value():
                    m_assignments.insert(id,QSettings().value(presetCategoryKey(id)).toString()).value();
                if(!m_custom.isEmpty()) {
                    if(m_custom!=custom)return false;
                } else if(!custom.isEmpty() || paintingFamily(name)!=m_category)return false;
            }
            const auto available=rowCapabilities(row);
            if(!available.supports(m_slot))return false;
            if(m_category==1)return available.eraser;
            if(m_category==2)return available.blend;
            if(m_slot==2 && m_category>=9)return available.convertible;
            return true;
        }

    private:
        PresetCapabilities rowCapabilities(int row) const
        {
            const auto index=m_source->index(row,KisAbstractResourceModel::Id);
            const int id=index.data().toInt();
            const auto cached=m_capabilities.constFind(id);
            if(cached!=m_capabilities.cend())return cached.value();
            const auto available=capabilities(m_source->resourceForIndex(index).dynamicCast<KisPaintOpPreset>());
            m_capabilities.insert(id,available);
            return available;
        }

        mutable QHash<int,PresetCapabilities> m_capabilities;
        KisResourceModel *m_source;
        int m_category;
        QString m_search,m_custom;
        mutable QHash<int,QString> m_assignments;
        QStringList m_favorites,m_recent;
        int m_slot;
    };

    KisCanvasResourceProvider *resources() const
    {
        return m_engine.brushResources();
    }
    KisResourceModel *model()
    {
        if(!m_model)m_model=new KisResourceModel(ResourceType::PaintOpPresets,this);
        return m_model;
    }
    static void rememberPreset(int slot,const KisPaintOpPresetSP &preset)
    {
        QSettings settings;
        const QString key=QStringLiteral("MobilePilot/brushSlot%1").arg(slot);
        settings.setValue(key,preset->name());
        settings.setValue(key+QStringLiteral("ResourceId"),preset->resourceId());
        const QString recentKey=QStringLiteral("MobileStudio/brushRecent%1").arg(slot);
        QStringList recent=settings.value(recentKey).toStringList();
        recent.removeAll(preset->name());
        recent.prepend(preset->name());
        while(recent.size()>16)recent.removeLast();
        settings.setValue(recentKey,recent);
    }

    KisPaintOpPresetSP findPreset(int slot)
    {
        const QSettings settings;
        const QString key=QStringLiteral("MobilePilot/brushSlot%1").arg(slot);
        const QString name=settings.value(key).toString();
        if(name.isEmpty())return {};
        auto *src=model();
        const int id=settings.value(key+QStringLiteral("ResourceId"),-1).toInt();
        if(id>=0) {
            const auto index=src->indexForResourceId(id);
            if(index.isValid()) {
                auto preset=src->resourceForIndex(index).dynamicCast<KisPaintOpPreset>();
                if(preset && preset->valid() && preset->name()==name)return preset;
            }
        }
        for(int i=0;i<src->rowCount();++i) {
            if(src->index(i,KisAbstractResourceModel::Name).data().toString()!=name)continue;
            auto preset=src->resourceForIndex(src->index(i,0))
                           .dynamicCast<KisPaintOpPreset>();
            if(preset && preset->valid())
                return preset;
        }
        return {};
    }
    KisPaintOpPresetSP firstPresetFor(int slot,bool smudgeEngineOnly=false)
    {
        auto *src=model();
        KisPaintOpPresetSP fallback;
        for(int row=0;row<src->rowCount();++row) {
            auto preset=src->resourceForIndex(src->index(row,0)).dynamicCast<KisPaintOpPreset>();
            const auto available=capabilities(preset);
            if(slot==0 && available.paint)return preset;
            if(slot==1 && available.eraser)return preset;
            if(slot==1 && available.paint && !fallback)fallback=preset;
            if(slot==2 && available.blend && (!smudgeEngineOnly || isSmudge(preset)))return preset;
        }
        return fallback;
    }
    void setEraser(bool enabled)
    {
        m_engine.setEraser(enabled);
    }
    void setNativePreset(KisPaintOpPresetSP preset)
    {
        if(!preset)return;
        m_internalChange=true;
        m_engine.selectBrushPreset(preset);
        m_internalChange=false;
    }
    void switchMode(int slot)
    {
        if(!resources() || slot<0 || slot>2)return;
        // Changing presets never replaces Krita's tool or input manager.
        m_engine.freehandBrush();
        KisPaintOpPresetSP preset=findPreset(slot);
        const auto available=capabilities(preset);
        if(!available.supports(slot) || (slot==2 && !available.blend))preset.clear();
        if(!preset && slot==0 && capabilities(resources()->currentPreset()).paint)
            preset=resources()->currentPreset();
        if(!preset)preset=firstPresetFor(slot);
        if(slot==2 && !preset)return;
        if(preset)setNativePreset(preset);
        setEraser(slot==1);
        m_slot=slot;
        m_hasSelectedMode=true;
        if(preset)rememberPreset(slot,preset);
        refresh();
    }
    void usePreset(int slot,const KisPaintOpPresetSP &preset)
    {
        m_engine.freehandBrush();
        setNativePreset(preset);
        setEraser(slot==1);
        m_slot=slot;
        m_hasSelectedMode=true;
        rememberPreset(slot,preset);
        refresh();
    }

    bool savePresetCopy(const KisPaintOpPresetSP &copy,const QString &name)
    {
        copy->setResourceId(-1);
        copy->setVersion(-1);
        copy->setMD5Sum(QString());
        copy->setFilename(QStringLiteral("studio-brush-")+
                          QUuid::createUuid().toString(QUuid::WithoutBraces)+QStringLiteral(".kpp"));
        copy->setName(name);
        return model()->addResourceDeduplicateFileName(copy);
    }

    bool revertPreset(const KisPaintOpPresetSP &preset,const QString &factorySource)
    {
        if(factorySource.isEmpty())return model()->reloadResource(preset);

        const QString filename=MobileFactoryBrushPreset::originalFilename(preset);
        auto original=KisPaintOpPresetSP::create(filename);
        if(factorySource.endsWith(QStringLiteral(".bundle"),Qt::CaseInsensitive)) {
            KoResourceBundle bundle(factorySource);
            if(!bundle.loadResource(original))return false;
        } else {
            QFile file(factorySource);
            if(!file.open(QIODevice::ReadOnly) ||
               !original->loadFromDevice(&file,KisGlobalResourcesInterface::instance()))return false;
        }
        if(!original->valid())return false;

        preset->setSettings(original->settings());
        preset->setImage(original->image());
        preset->setName(original->name());
        return model()->updateResource(preset);
    }

    static void renamePersonalEntries(const QString &oldName,const QString &newName)
    {
        QSettings settings;
        QStringList keys={QStringLiteral("MobileStudio/brushFavorites")};
        for(int slot=0;slot<3;++slot) {
            const QString key=QStringLiteral("MobilePilot/brushSlot%1").arg(slot);
            if(settings.value(key).toString()==oldName) {
                if(newName.isEmpty()) {
                    settings.remove(key);
                    settings.remove(key+QStringLiteral("ResourceId"));
                } else settings.setValue(key,newName);
            }
            keys.append(QStringLiteral("MobileStudio/brushRecent%1").arg(slot));
        }
        for(const auto &key:keys) {
            auto names=settings.value(key).toStringList();
            if(!names.contains(oldName))continue;
            if(newName.isEmpty())names.removeAll(oldName);
            else for(auto &name:names) {
                if(name==oldName)name=newName;
            }
            names.removeDuplicates();
            settings.setValue(key,names);
        }
    }

    void showPresetActions(MobileTransientPanel *drawer,const KisPaintOpPresetSP &preset,
                           std::function<void(const QString &)> refreshList)
    {
        if(!preset || preset->resourceId()<0)return;
        enum Operation { Duplicate, Revert, Rename, Remove, Move };
        const int id=preset->resourceId();
        const QString factory=MobileFactoryBrushPreset::sourcePath(preset);
        const QString title=displayPresetName(preset->name());
        QMenu menu(title);
        menu.addAction(i18n("Duplicate brush"))->setData(Duplicate);
        menu.addAction(factory.isEmpty()?i18n("Revert to saved preset"):
                                        i18n("Restore factory preset"))->setData(Revert);
        if(factory.isEmpty()) {
            menu.addAction(i18n("Move to category…"))->setData(Move);
            menu.addAction(i18n("Rename brush…"))->setData(Rename);
            menu.addAction(i18n("Delete brush…"))->setData(Remove);
        }
        const QPointer<MobileTransientPanel> alive=drawer;
        mobileShowActionList(drawer,nullptr,menu,this,[this,alive,id,title,refreshList](const QVariant &value) {
            if(!alive || !alive->isVisible())return;
            auto *source=model();
            auto preset=source->resourceForId(id).dynamicCast<KisPaintOpPreset>();
            if(!preset)return;
            const QString factory=MobileFactoryBrushPreset::sourcePath(preset);
            const QString oldName=preset->name();
            bool success=false;
            switch(value.toInt()) {
            case Duplicate: {
                bool accepted=false;
                const auto name=QInputDialog::getText(parentWidget(),i18n("Duplicate brush"),i18n("Name"),
                    QLineEdit::Normal,i18n("%1 Copy",title),&accepted).trimmed();
                if(!accepted || name.isEmpty())return;
                const auto copy=preset->clone().dynamicCast<KisPaintOpPreset>();
                success=copy && savePresetCopy(copy,name);
                break;
            }
            case Revert:
                if(!factory.isEmpty() && QMessageBox::question(parentWidget(),i18n("Restore factory preset"),
                    i18n("Restore the original settings for “%1”?",title))!=QMessageBox::Yes)return;
                success=revertPreset(preset,factory);
                if(success && oldName!=preset->name())renamePersonalEntries(oldName,preset->name());
                break;
            case Rename: {
                if(!factory.isEmpty())return;
                bool accepted=false;
                const auto name=QInputDialog::getText(parentWidget(),i18n("Rename brush"),i18n("Name"),
                    QLineEdit::Normal,oldName,&accepted).trimmed();
                if(!accepted || name.isEmpty() || name==oldName)return;
                success=KisResourceUserOperations::renameResourceWithUserInput(parentWidget(),preset,name);
                if(success)renamePersonalEntries(oldName,name);
                break;
            }
            case Move: {
                if(!factory.isEmpty())return;
                QSettings settings;
                const QString key=presetCategoryKey(id);
                const QString automatic=i18n("Automatic category");
                auto names=customCategories();
                QStringList choices=names;
                choices.prepend(automatic);
                const int current=qMax(0,choices.indexOf(settings.value(key).toString()));
                bool accepted=false;
                QString name=QInputDialog::getItem(parentWidget(),i18n("Move brush"),
                    i18n("Select a category or type a new name"),choices,current,true,&accepted).trimmed();
                if(!accepted || name.isEmpty())return;
                if(name==automatic) {
                    settings.remove(key);
                    name.clear();
                } else {
                    for(const auto &existing:names)
                        if(existing.compare(name,Qt::CaseInsensitive)==0) {name=existing;break;}
                    if(!names.contains(name)) {
                        names.append(name);
                        settings.setValue(QStringLiteral("MobileStudio/brushCategories"),names);
                    }
                    settings.setValue(key,name);
                }
                if(!name.isEmpty())settings.setValue(
                    QStringLiteral("MobileStudio/brushLastCategory/%1").arg(m_browserSlot),
                    QStringLiteral("custom/%1").arg(name));
                if(alive && alive->isVisible())refreshList(name);
                return;
            }
            case Remove: {
                if(!factory.isEmpty() || QMessageBox::question(parentWidget(),i18n("Delete brush"),
                    i18n("Remove “%1” from your presets?",title))!=QMessageBox::Yes)return;
                const auto index=source->indexForResourceId(id);
                if(!index.isValid())return;
                success=source->setResourceActive(index,false);
                if(success) {
                    renamePersonalEntries(oldName,{});
                    QSettings().remove(presetCategoryKey(id));
                    if(resources() && resources()->currentPreset() &&
                       resources()->currentPreset()->resourceId()==id) {
                        if(auto fallback=firstPresetFor(m_slot))usePreset(m_slot,fallback);
                    }
                }
                break;
            }
            }
            if(!success)QMessageBox::warning(parentWidget(),i18n("Brush presets"),i18n("Could not update “%1”.",title));
            if(alive && alive->isVisible())refreshList({});
            refresh();
        });
    }

    KisPaintOpPresetSP makeBlender(const KisPaintOpPresetSP &source)
    {
        if(!capabilities(source).convertible)return {};
        const auto base=isSmudge(source)?source:firstPresetFor(2,true);
        if(!base)return {};
        auto preset=base->clone().dynamicCast<KisPaintOpPreset>();
        if(!preset)return {};
        auto settings=preset->settings()->clone();
        const auto original=source->settings();
        settings->setProperty(QStringLiteral("brush_definition"),
                              original->getProperty(QStringLiteral("brush_definition")));
        for(const auto &option:{QStringLiteral("Size"),QStringLiteral("Opacity"),
                                QStringLiteral("Rotation"),QStringLiteral("Scatter")}) {
            for(const auto &suffix:{"Sensor","UseCurve","UseSameCurve","Value","curveMode","commonCurve"}) {
                const QString key=option+QString::fromLatin1(suffix);
                QVariant value;
                if(original->getProperty(key,value))settings->setProperty(key,value);
            }
            const QString key=QStringLiteral("Pressure")+option;
            QVariant value;
            if(original->getProperty(key,value))settings->setProperty(key,value);
        }
        settings->setResourcesInterface(original->resourcesInterface());
        settings->setPaintOpSize(original->paintOpSize());
        settings->setProperty(QStringLiteral("CompositeOp"),COMPOSITE_OVER);
        settings->setProperty(QStringLiteral("EraserMode"),false);
        settings->setProperty(QStringLiteral("PressureColorRate"),false);
        settings->setProperty(QStringLiteral("ColorRateValue"),0.0);
        // Cross-engine tips can use lightness maps, which require
        // Krita's current smudge algorithm.
        if(!isSmudge(source))settings->setProperty(QStringLiteral("SmudgeRateUseNewEngine"),true);
        settings->setProperty(QStringLiteral("PressureSmudgeRate"),true);
        settings->setProperty(QStringLiteral("SmudgeRateValue"),
                              qMax(0.35,settings->getDouble(QStringLiteral("SmudgeRateValue"),0.5)));
        preset->setSettings(settings);
        preset->setImage(source->image());
        return savePresetCopy(preset,i18n("%1 Blender",displayPresetName(source->name())))?
            preset:KisPaintOpPresetSP();
    }

    void openPresetBrowser(int slot,QAction *action=nullptr,QWidget *opener=nullptr)
    {
        if(m_presetBrowser && m_presetBrowser->isVisible()) {
            dismissPresetBrowser();
            if(m_browserSlot==slot)return;
        }
        if(!resources() || !parentWidget())return;
        const auto view=MobileViewIdentityGuard::uniqueActive(m_engine.openDocuments());
        if(!view)return;
        auto *panel=new MobileTransientPanel(parentWidget());
        m_presetBrowser=panel;
        m_browserSlot=slot;
        panel->setOpener(opener,action);
        panel->setFocusPolicy(Qt::StrongFocus);
        panel->setObjectName(QStringLiteral("mobilePresetBrowser"));
        panel->setAccessibleName(slot==1?i18n("Eraser presets"):slot==2?i18n("Blend presets"):i18n("Brush presets"));
        panel->setStyleSheet(QStringLiteral(
            "QFrame#mobilePresetBrowser{background:#25262b;border:1px solid #44464f;border-radius:12px;}"
            "QListWidget#brushCategories{background:#202126;color:#bfc3ce;border:0;border-radius:8px;outline:0;}"
            "QListWidget#brushCategories::item{min-height:40px;padding:0 8px;border-radius:6px;}"
            "QListWidget#brushCategories::item:selected{background:#353b4b;color:#e0e7fb;}"
            "QListView#brushRows{background:#25262b;color:#f1f2f5;border:0;outline:0;}"
            "QLineEdit,QToolButton{background:#30323b;color:#f1f2f5;border:0;border-radius:6px;"
            "min-height:36px;padding:2px 8px;}"
            "QToolButton:disabled{color:#a0a1aa;}QLabel{color:#f1f2f5;}"));
        auto *layout=new QHBoxLayout(panel);
        layout->setContentsMargins(7,7,7,7);
        layout->setSpacing(6);

        auto *categories=new QListWidget(panel);
        categories->setObjectName(QStringLiteral("brushCategories"));
        categories->setAccessibleName(i18n("Brush categories"));
        MobileTouchScroll::configureList(categories);
        layout->addWidget(categories);
        const auto category=[categories](const QString &name,int id) {
            auto *item=new QListWidgetItem(name,categories);
            item->setData(Qt::UserRole,id);
        };
        auto *source=model();
        auto *filter=new PresetFilter(source,slot,panel);
        category(i18n("Favorites"),-4);
        category(i18n("Recent"),-5);
        if(slot==2)category(i18n("Blenders"),2);
        category(i18n("All"),-1);
        if(slot==1)category(i18n("Erasers"),1);
        const auto family=[slot,filter,category](const QString &name,int id) {
            if(slot!=2 || filter->hasFamily(id))category(name,id);
        };
        family(i18n("Basic"),9);
        family(i18n("Sketch"),10);
        family(i18n("Inking"),11);
        family(i18n("Painting"),13);
        family(i18n("Watercolor"),14);
        family(i18n("Airbrush"),15);
        family(i18n("Texture"),12);
        const auto addCustom=[categories](const QString &name) {
            auto *item=new QListWidgetItem(name,categories);
            item->setData(Qt::UserRole,-6);
            item->setData(Qt::UserRole+1,name);
            return item;
        };
        for(const auto &name:customCategories())addCustom(name);
        const auto categoryKey=[](QListWidgetItem *item) {
            const auto custom=item->data(Qt::UserRole+1).toString();
            return custom.isEmpty()?item->data(Qt::UserRole).toString():QStringLiteral("custom/")+custom;
        };
        const QString lastCategoryKey=QStringLiteral("MobileStudio/brushLastCategory/%1").arg(slot);
        const QString saved=QSettings().value(lastCategoryKey).toString();
        int initialCategory=2;
        for(int row=0;row<categories->count();++row)
            if(categoryKey(categories->item(row))==saved) {initialCategory=row;break;}
        categories->setCurrentRow(initialCategory);

        auto *body=new QVBoxLayout;
        body->setSpacing(4);
        layout->addLayout(body,1);
        auto *search=new QLineEdit(panel);
        search->setPlaceholderText(i18n("Search brushes"));
        search->setClearButtonEnabled(true);
        search->setMinimumWidth(0);
        search->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        body->addWidget(search);
        auto *list=new QListView(panel);
        list->setObjectName(QStringLiteral("brushRows"));
        list->setAccessibleName(i18n("Brush presets"));
        list->setUniformItemSizes(true);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        body->addWidget(list,1);
        auto *empty=new QLabel(i18n("No matching brushes"),panel);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->hide();
        body->addWidget(empty,1);

        auto *conversion=new QToolButton(panel);
        conversion->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        body->addWidget(conversion);
        conversion->hide();
        auto candidate=std::make_shared<KisPaintOpPresetSP>();
        list->setModel(filter);
        list->setModelColumn(KisAbstractResourceModel::Name);
        filter->setCategory(categories->currentItem()->data(Qt::UserRole).toInt(),
                            categories->currentItem()->data(Qt::UserRole+1).toString());
        const auto valid=[this,view]() {
            return view && view==MobileViewIdentityGuard::uniqueActive(m_engine.openDocuments()) && resources();
        };
        const auto updateSelected=[this,list,source,filter]() {
            const auto preset=resources()?resources()->currentPreset():KisPaintOpPresetSP();
            if(!preset)return;
            const auto row=source->indexForResourceId(preset->resourceId());
            list->setCurrentIndex(filter->mapFromSource(row.sibling(row.row(),KisAbstractResourceModel::Name)));
        };
        auto *delegate=new PresetRow(list,[filter,updateSelected](const QString &name) {
            QSettings settings;
            const QString key=QStringLiteral("MobileStudio/brushFavorites");
            QStringList favorites=settings.value(key).toStringList();
            if(favorites.contains(name))favorites.removeAll(name);
            else favorites.prepend(name);
            settings.setValue(key,favorites);
            QTimer::singleShot(0,filter,[filter,updateSelected]() {
                filter->reloadPersonalLists();
                updateSelected();
            });
        });
        list->setItemDelegate(delegate);
        const auto clearExtras=[conversion,candidate]() {
            conversion->hide();
            candidate->clear();
        };
        const auto updateList=[list,empty,filter]() {
            const bool hasRows=filter->rowCount()>0;
            list->setVisible(hasRows);
            empty->setVisible(!hasRows);
        };
        connect(filter,&QAbstractItemModel::rowsInserted,panel,updateList);
        connect(filter,&QAbstractItemModel::rowsRemoved,panel,updateList);
        connect(filter,&QAbstractItemModel::modelReset,panel,updateList);
        const auto chooseCategory=[filter,list,clearExtras,updateSelected,lastCategoryKey,categoryKey](QListWidgetItem *item) {
            clearExtras();
            QSettings().setValue(lastCategoryKey,categoryKey(item));
            filter->setCategory(item->data(Qt::UserRole).toInt(),item->data(Qt::UserRole+1).toString());
            list->scrollToTop();
            updateSelected();
        };
        connect(categories,&QListWidget::itemClicked,panel,chooseCategory);
        connect(categories,&QListWidget::itemActivated,panel,chooseCategory);
        connect(search,&QLineEdit::textChanged,panel,[filter,list,clearExtras,updateSelected](const QString &query) {
            clearExtras();
            filter->setSearch(query);
            list->scrollToTop();
            updateSelected();
        });
        MobileTouchScroll::configureList(list,[=](const QModelIndex &index) {
            if(!valid())return;
            const auto selected=filter->mapToSource(index);
            const auto preset=source->resourceForIndex(selected).dynamicCast<KisPaintOpPreset>();
            showPresetActions(panel,preset,[=](const QString &name) {
                filter->reloadPersonalLists();
                if(name.isEmpty()) {
                    updateSelected();
                    return;
                }
                QListWidgetItem *item=nullptr;
                for(int row=0;row<categories->count();++row) {
                    auto *entry=categories->item(row);
                    if(entry->data(Qt::UserRole+1).toString()==name) {item=entry;break;}
                }
                if(!item)item=addCustom(name);
                categories->setCurrentItem(item);
                chooseCategory(item);
            });
        });
        const auto hideSearchHandle=[search,panel]() {
            search->deselect();
            panel->setFocus(Qt::OtherFocusReason);
            if(QGuiApplication::inputMethod()->isVisible())QGuiApplication::inputMethod()->hide();
        };
        connect(categories,&QListWidget::itemClicked,panel,hideSearchHandle);
        connect(list,&QListView::clicked,this,[=](const QModelIndex &index) {
            hideSearchHandle();
            if(!valid() || list->property("_mobileFavoriteTap").toBool())return;
            const auto selected=filter->mapToSource(index);
            const auto preset=source->resourceForIndex(selected.sibling(selected.row(),0))
                .dynamicCast<KisPaintOpPreset>();
            if(!preset || !preset->valid())return;
            const auto available=capabilities(preset);
            if(!available.supports(slot))return;
            const auto samePreset=[preset](const KisPaintOpPresetSP &other) {
                return other && (other==preset || (preset->resourceId()>=0 &&
                                                    preset->resourceId()==other->resourceId()));
            };
            if(slot==2 && !available.blend) {
                if(samePreset(*candidate)) {
                    panel->close();
                    return;
                }
                *candidate=preset;
                conversion->setText(i18n("Create blender from %1",displayPresetName(preset->name())));
                conversion->show();
                return;
            }
            if(samePreset(resources()->currentPreset())) {
                panel->close();
                return;
            }
            clearExtras();
            usePreset(slot,preset);
            filter->reloadPersonalLists();
            updateSelected();
        });
        connect(conversion,&QToolButton::clicked,this,[=]() {
            if(!valid() || !*candidate)return;
            const auto result=makeBlender(*candidate);
            if(!result) {
                conversion->setText(i18n("Could not save blender"));
                return;
            }
            clearExtras();
            usePreset(2,result);
            filter->reloadPersonalLists();
            updateSelected();
        });
        const auto fit=[panel,categories]() {
            categories->setFixedWidth(qBound(82,panel->width()*28/100,134));
        };
        panel->onRefresh=fit;
        panel->anchorTo(nullptr,QSize(510,650));
        panel->present();
        panel->setFocus(Qt::OtherFocusReason);
        categories->scrollToItem(categories->currentItem(),QAbstractItemView::EnsureVisible);
        fit();
        updateSelected();
        updateList();
    }

    QPointer<MobileTransientPanel> m_presetBrowser;
    int m_browserSlot=-1;
    MobileEngineBridge m_engine;
    QToolButton *m_modes[3]={nullptr,nullptr,nullptr};
    PresetLabel *m_presetName=nullptr;
    QWidget *m_controls=nullptr;
    QToolButton *m_detailToggle=nullptr;
    bool m_detailsExpanded=true;
    KisDoubleSliderSpinBox *m_size=nullptr,*m_opacity=nullptr,*m_flow=nullptr;
    KisDoubleSliderSpinBox *m_angle=nullptr,*m_smudgeStrength=nullptr,*m_smudgeRadius=nullptr;
    QCheckBox *m_sampleLayers=nullptr;
    QPushButton *m_composite=nullptr;
    const KoColorSpace *m_compositeSpace=nullptr;
    QString m_compositeMode;
    bool m_compositeEraser=false;
    KisResourceModel *m_model=nullptr;
    QTimer *m_refresh=nullptr;
    bool m_syncing=false,m_internalChange=false,m_hasSelectedMode=true;
    int m_slot=0;
};
#endif // Q_OS_ANDROID
