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
#include <QSortFilterProxyModel>
#include <QPointer>
#include <QPalette>
#include <QPushButton>
#include <QScroller>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QPixmap>

#include "engine_bridge.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_slider_spin_box.h"
#include "kis_image_config.h"
#include <KisResourceModel.h>
#include <brushengine/kis_paintop_preset.h>
#include <KoToolManager.h>
#include <klocalizedstring.h>

class KritaMobileBrushWorkspace final : public QFrame {
public:
    explicit KritaMobileBrushWorkspace(const MobileEngineBridge &engine,
                                       QWidget *parent,bool slidersOnly=false)
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
        if(!slidersOnly) {
            m_presetName=new QLabel(i18n("Current brush"),this);
            m_presetName->setToolTip(i18n("Active Krita brush preset"));
            m_presetName->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
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
        details->addLayout(controlRow);
        root->addWidget(m_controls);
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
        m_refresh->setInterval(500);
        connect(m_refresh,&QTimer::timeout,this,[this]() {
            if(isVisible())refresh();
        });
        m_refresh->start();
        refresh();
    }

    bool isActiveMode(int slot) const
    {
        if(!m_hasSelectedMode || m_slot!=slot ||
           m_engine.activeTool()!=QStringLiteral("KritaShape/KisToolBrush"))
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

    void refresh()
    {
        auto *provider=resources();
        if(!provider)return;
        m_syncing=true;
        m_size->setValue(provider->size());
        m_opacity->setValue(provider->opacity()*100.0);
        if(m_presetName) {
            if(auto preset=provider->currentPreset()) {
                m_presetName->setText(preset->name().left(26));
                m_presetName->setToolTip(preset->name());
            }
        }
        m_syncing=false;
        for(int i=0;i<3;++i)
            if(m_modes[i])m_modes[i]->setChecked(m_hasSelectedMode && m_slot==i);
    }

private:
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
                return source.sibling(source.row(),KisAbstractResourceModel::Thumbnail)
                    .data(Qt::DecorationRole);
            }
            return QSortFilterProxyModel::data(index,role);
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
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#28333f;color:#edf6ff;}"
            "QLabel{color:#edf6ff;font-size:14px;}"
            "QLineEdit,QComboBox{background:#34485b;color:#fff;"
            "border:1px solid #7e9eb4;border-radius:7px;"
            "min-height:44px;padding:4px;}"
            "QListView{background:#263443;color:white;"
            "border:1px solid #617f94;}"
            "QPushButton{background:#45667e;color:#fff;min-height:46px;"
            "border:1px solid #91bbd6;border-radius:7px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        auto *search=new QLineEdit(&dialog);
        search->setPlaceholderText(i18n("Search every Krita brush preset"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);
        auto *category=new QComboBox(&dialog);
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
        grid->setGridSize(QSize(110,116));
        grid->setIconSize(QSize(68,68));
        grid->setWordWrap(true);
        grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        grid->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(grid->viewport(),QScroller::LeftMouseButtonGesture);
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
        const QSize size=window()->size();
        dialog.resize(qMax(300,qMin(570,size.width()-32)),
                      qMax(340,qMin(720,size.height()-65)));
        dialog.exec();
    }

    MobileEngineBridge m_engine;
    QToolButton *m_modes[3]={nullptr,nullptr,nullptr};
    QLabel *m_presetName=nullptr;
    QWidget *m_controls=nullptr;
    QToolButton *m_detailToggle=nullptr;
    bool m_detailsExpanded=true;
    KisDoubleSliderSpinBox *m_size=nullptr,*m_opacity=nullptr;
    KisResourceModel *m_model=nullptr;
    QTimer *m_refresh=nullptr;
    bool m_syncing=false,m_internalChange=false,m_hasSelectedMode=true;
    int m_slot=0;
};
#endif // Q_OS_ANDROID
