/*
 * Independent mobile brush controls: all strokes and resources still use
 * Krita's own paint engines. No Q_OBJECT; usable in Android QWidget shell.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QTreeWidget>
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
                connect(button,&QToolButton::clicked,this,[this,i]() {
                    if(m_hasSelectedMode && m_slot==i) {
                        openPresetBrowser(i);
                    } else switchMode(i);
                });
            }
            auto *browse=new QToolButton(this);
            browse->setText(i18n("Presets"));
            browse->setMinimumHeight(42);
            browse->setToolTip(i18n("Search Krita brush presets"));
            buttons->addWidget(browse,1);
            connect(browse,&QToolButton::clicked,this,[this]() {
                openPresetBrowser(m_slot);
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
        if(!m_hasSelectedMode || m_slot!=slot ||
           m_engine.activeTool().section(QLatin1Char('/'),-1)!=
               QStringLiteral("KisToolBrush"))
            return false;
        const QAction *eraser=m_engine.action("erase_action");
        if(eraser && eraser->isChecked()!=(slot==1))return false;
        return slot!=2 || (resources() && isSmudge(resources()->currentPreset()));
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
    void browsePresets(int slot) { openPresetBrowser(slot); }

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
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Brush blending mode"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *tree=new QTreeWidget(&dialog);
        tree->setHeaderHidden(true);
        tree->setStyleSheet(QStringLiteral("QTreeWidget::item{min-height:40px;}"));
        MobileTouchScroll::configureList(tree);
        layout->addWidget(tree,1);
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
        auto *cancel=new QPushButton(i18n("Cancel"),&dialog);
        cancel->setMinimumHeight(44);
        layout->addWidget(cancel);
        connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
        QString selected;
        connect(tree,&QTreeWidget::itemClicked,&dialog,
                [&dialog,&selected](QTreeWidgetItem *item,int) {
            const QString id=item->data(0,Qt::UserRole).toString();
            if(id.isEmpty()) {item->setExpanded(!item->isExpanded());return;}
            selected=id;
            dialog.accept();
        });
        dialog.resize(qMin(360,window()->width()),qMin(540,window()->height()));
        if(dialog.exec()!=QDialog::Accepted || selected.isEmpty())return;
        auto *now=m_engine.manager();
        if(now!=view || !now || now->image().toStrongRef()!=original ||
           resources()!=provider || provider->eraserMode() ||
           !now->activeNode() || now->activeNode()->colorSpace()!=space)return;
        if(space->compositeOp(selected)) {
            provider->setCurrentCompositeOp(selected);
            refresh();
        }
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

    class PresetTile final : public QStyledItemDelegate
    {
    public:
        explicit PresetTile(QListView *view) : QStyledItemDelegate(view),m_view(view) {}

        QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override
        {
            return m_view->gridSize();
        }

        void paint(QPainter *painter,const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
        {
            painter->save();
            const QRect tile=option.rect.adjusted(1,1,-1,-1);
            painter->fillRect(tile,(option.state & QStyle::State_Selected)?
                QColor("#365c78"):QColor("#293745"));
            const QIcon thumb=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            const QRect image=tile.adjusted(2,1,-2,-15);
            thumb.paint(painter,image,Qt::AlignCenter);
            QFont font=option.font;
            font.setPixelSize(11);
            painter->setFont(font);
            painter->setPen(QColor("#f4f9fe"));
            const QRect caption(tile.x()+2,tile.bottom()-14,tile.width()-4,15);
            const QString name=index.data().toString();
            painter->drawText(caption,Qt::AlignCenter,
                painter->fontMetrics().elidedText(name,Qt::ElideRight,caption.width()));
            painter->restore();
        }

    private:
        QListView *m_view;
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
    static bool inCategory(const QString &name,const QString &engine,int slot)
    {
        const bool clone=engine==QStringLiteral("duplicate");
        if(slot==3)return clone;
        const QString lower=name.toLower();
        const bool eraser=lower.contains(QStringLiteral("erase")) ||
                          lower.contains(QStringLiteral("rubber"));
        const bool smudge=engine==QStringLiteral("colorsmudge");
        const bool blend=smudge || lower.contains(QStringLiteral("blend")) ||
            lower.contains(QStringLiteral("smudge")) ||
            lower.contains(QStringLiteral("blur")) ||
            lower.contains(QStringLiteral("mixing"));
        if(slot==1)return eraser;
        if(slot==2)return smudge && !eraser;
        return !eraser && !blend && !clone;
    }

    static bool inCategory(const KisPaintOpPresetSP &preset,int slot)
    {
        return preset && inCategory(preset->name(),preset->paintOp().id(),slot);
    }

    static QString presetEngine(KisResourceModel *source,int row)
    {
        const auto index=source->index(row,0);
        const QString engine=index.data(Qt::UserRole+KisAbstractResourceModel::MetaData)
            .toMap().value(QStringLiteral("paintopid")).toString();
        if(!engine.isEmpty())return engine;
        // Presets indexed before paintop metadata was stored still work.
        const auto preset=source->resourceForIndex(index).dynamicCast<KisPaintOpPreset>();
        return preset?preset->paintOp().id():QString();
    }

    class PresetFilter final : public QSortFilterProxyModel
    {
    public:
        PresetFilter(KisResourceModel *source,int slot,QObject *parent)
            : QSortFilterProxyModel(parent),m_source(source),m_category(slot),
              m_smudgeOnly(slot==2)
        {
            setSourceModel(source);
        }

        void setCategory(int category)
        {
            m_category=category;
            invalidateFilter();
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
                // Scale into a square, never stretch a wide/portrait preset
                // into a distorted icon. Krita's resource model still loads
                // thumbnails lazily for visible cells only.
                return QIcon(QPixmap::fromImage(thumbnail.scaled(
                    QSize(56,56),Qt::KeepAspectRatio,Qt::SmoothTransformation)));
            }
            const auto value=QSortFilterProxyModel::data(index,role);
            if(index.column()==KisAbstractResourceModel::Name && role==Qt::DisplayRole)
                return displayPresetName(value.toString());
            return value;
        }

    protected:
        bool filterAcceptsRow(int row,const QModelIndex &parent) const override
        {
            const QString name=m_source->index(row,KisAbstractResourceModel::Name,parent)
                .data().toString();
            if(!name.contains(m_search,Qt::CaseInsensitive))return false;
            if(m_category<0 && !m_smudgeOnly)return true;
            const QString engine=presetEngine(m_source,row);
            return (!m_smudgeOnly || engine==QStringLiteral("colorsmudge")) &&
                (m_category<0 || inCategory(name,engine,m_category));
        }

    private:
        KisResourceModel *m_source;
        int m_category;
        bool m_smudgeOnly;
        QString m_search;
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
    KisPaintOpPresetSP firstPresetFor(int slot)
    {
        auto *src=model();
        for(int i=0;i<src->rowCount();++i) {
            const QString name=src->index(i,KisAbstractResourceModel::Name).data().toString();
            if(!inCategory(name,presetEngine(src,i),slot))continue;
            auto preset=src->resourceForIndex(src->index(i,0))
                           .dynamicCast<KisPaintOpPreset>();
            if(preset && preset->valid())
                return preset;
        }
        return KisPaintOpPresetSP();
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
        if(slot==2 && !isSmudge(preset))preset.clear();
        // Never restore a cached eraser or blender as the Paint preset.
        // An unsaved mode receives a matching native Krita preset.
        if(!preset && slot==0 &&
           inCategory(resources()->currentPreset(),0))
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
    void openPresetBrowser(int slot)
    {
        if(!resources())return;
        QDialog dialog(window());
        dialog.setWindowTitle(i18n("Krita brush presets"));
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#28333f;color:#edf6ff;}"
            "QLabel{color:#edf6ff;font-size:14px;}"
            "QLineEdit,QComboBox{background:#34485b;color:#fff;"
            "border:1px solid #7e9eb4;border-radius:7px;"
            "min-height:44px;padding:4px;}"
            "QListView{background:#263443;color:white;"
            "border:1px solid #617f94;}"
            "QListView::item{color:#f4f9fe;font-size:11px;}"
            "QListView::item:selected{background:#365c78;color:#ffffff;}"
            "QPushButton{background:#45667e;color:#fff;min-height:46px;"
            "border:1px solid #91bbd6;border-radius:7px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(6,6,6,6);
        layout->setSpacing(4);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search every Krita brush preset"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *category=new MobileTouchComboBox(&dialog);
        category->addItem(slot==2?i18n("All compatible brushes"):i18n("All brushes"),-1);
        if(slot!=2) {
            category->addItem(i18n("Painting"),0);
            category->addItem(i18n("Erasers"),1);
        }
        category->addItem(i18n("Blending / Smudge"),2);
        if(slot!=2)category->addItem(i18n("Clone"),3);
        category->setCurrentIndex(category->findData(slot));
        layout->addWidget(category);

        auto *grid=new QListView(&dialog);
        grid->setViewMode(QListView::IconMode);
        grid->setFlow(QListView::LeftToRight);
        grid->setWrapping(true);
        grid->setResizeMode(QListView::Adjust);
        grid->setMovement(QListView::Static);
        grid->setLayoutMode(QListView::Batched);
        grid->setBatchSize(32);
        grid->setUniformItemSizes(true);
        const QSize size=window()->size();
        const int width=qMin(550,qMax(1,size.width()-16));
        const int gridWidth=qMax(1,width-14);
        const int columns=qMax(1,gridWidth/60);
        constexpr int tileHeight=64;
        grid->setGridSize(QSize(gridWidth/columns,tileHeight));
        grid->setItemDelegate(new PresetTile(grid));
        grid->setWordWrap(false);
        grid->setTextElideMode(Qt::ElideRight);
        grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
        MobileTouchScroll::configureList(grid);
        layout->addWidget(grid,1);

        auto *close=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(close);
        connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        auto *source=model();
        auto *filter=new PresetFilter(source,slot,&dialog);
        grid->setModel(filter);
        grid->setModelColumn(KisAbstractResourceModel::Name);
        connect(search,&QLineEdit::textChanged,filter,&PresetFilter::setSearch);
        connect(category,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[filter,category](int) {
            filter->setCategory(category->currentData().toInt());
        });
        connect(grid,&QListView::clicked,&dialog,
                [this,&dialog,source,filter,slot](const QModelIndex &index) {
            const auto selected=filter->mapToSource(index);
            if(!selected.isValid())return;
            const auto preset=source->resourceForIndex(selected.sibling(selected.row(),0))
                .dynamicCast<KisPaintOpPreset>();
            if(!preset || !preset->valid() || (slot==2 && !isSmudge(preset)))return;
            m_engine.freehandBrush();
            setNativePreset(preset);
            setEraser(slot==1);
            m_slot=slot;
            m_hasSelectedMode=true;
            rememberPreset(slot,preset);
            refresh();
            dialog.accept();
        });
        const int rows=qMax(1,(filter->rowCount()+columns-1)/columns);
        dialog.resize(width,qMin(qMax(1,size.height()-28),
                                 qMin(670,layout->minimumSize().height()+rows*tileHeight)));
        dialog.exec();
    }

    MobileEngineBridge m_engine;
    QToolButton *m_modes[3]={nullptr,nullptr,nullptr};
    PresetLabel *m_presetName=nullptr;
    QWidget *m_controls=nullptr;
    QToolButton *m_detailToggle=nullptr;
    bool m_detailsExpanded=true;
    KisDoubleSliderSpinBox *m_size=nullptr,*m_opacity=nullptr,*m_flow=nullptr;
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
