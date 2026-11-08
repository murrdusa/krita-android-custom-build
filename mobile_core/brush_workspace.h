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
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScroller>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QPixmap>

#include "KisMainWindow.h"
#include "KisViewManager.h"
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
    explicit KritaMobileBrushWorkspace(KisMainWindow *window,
                                       KisViewManager *viewManager,
                                       QWidget *parent)
        : QFrame(parent),m_window(window),m_vm(viewManager)
    {
        setObjectName(QStringLiteral("mobileBrushWorkspace"));
        setStyleSheet(
            "QFrame#mobileBrushWorkspace{background:rgba(32,42,53,235);"
            "border:1px solid #627e95;border-radius:10px;}"
            "QToolButton{background:#40566b;color:#f4f7fa;"
            "border:1px solid #7895aa;border-radius:7px;padding:5px;"
            "font-size:13px;}"
            "QToolButton:checked{background:#276b8b;border:2px solid #a5d9f0;}"
            "QLabel{color:#e9f4fd;}");
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(7,7,7,7);
        root->setSpacing(5);
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
        browse->setText(i18n("Brushes..."));
        browse->setMinimumHeight(42);
        browse->setToolTip(i18n("Search Krita brush presets"));
        buttons->addWidget(browse,1);
        connect(browse,&QToolButton::clicked,this,[this]() {
            openPresetBrowser(m_slot);
        });
        root->addLayout(buttons);

        auto *controlRow=new QHBoxLayout;
        controlRow->setSpacing(6);
        m_presetName=new QLabel(i18n("Current brush"),this);
        m_presetName->setMinimumWidth(70);
        m_presetName->setMaximumWidth(170);
        m_presetName->setToolTip(i18n("Active Krita brush preset"));
        controlRow->addWidget(m_presetName,1);
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
        root->addLayout(controlRow);

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
                m_presetName->setText(preset->name().left(26));
                m_presetName->setToolTip(preset->name());
                if(!m_internalChange && m_hasSelectedMode)
                    QSettings().setValue(
                        QStringLiteral("MobilePilot/brushSlot%1").arg(m_slot),
                        preset->name());
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

    void refresh()
    {
        auto *provider=resources();
        if(!provider)return;
        m_syncing=true;
        m_size->setValue(provider->size());
        m_opacity->setValue(provider->opacity()*100.0);
        if(auto preset=provider->currentPreset()) {
            m_presetName->setText(preset->name().left(26));
            m_presetName->setToolTip(preset->name());
        }
        m_syncing=false;
        for(int i=0;i<3;++i)
            if(m_modes[i])m_modes[i]->setChecked(m_hasSelectedMode && m_slot==i);
    }

private:
    static bool inCategory(const KisPaintOpPresetSP &preset,int slot)
    {
        if(!preset)return false;
        const QString name=preset->name().toLower();
        const bool eraser=name.contains(QStringLiteral("erase")) ||
            name.contains(QStringLiteral("eraser")) ||
            name.contains(QStringLiteral("rubber"));
        const bool blend=name.contains(QStringLiteral("blend")) ||
            name.contains(QStringLiteral("smudge")) ||
            name.contains(QStringLiteral("blur")) ||
            name.contains(QStringLiteral("mixing")) ||
            preset->paintOp().id()==QStringLiteral("colorsmudge");
        if(slot==1)return eraser;
        if(slot==2)return blend && !eraser;
        return !eraser && !blend;
    }
    KisCanvasResourceProvider *resources() const
    {
        return m_vm ? m_vm->canvasResourceProvider():nullptr;
    }
    KisResourceModel *model()
    {
        if(!m_model)m_model=new KisResourceModel(ResourceType::PaintOpPresets,this);
        return m_model;
    }
    KisPaintOpPresetSP findPreset(const QString &name)
    {
        if(name.isEmpty())return KisPaintOpPresetSP();
        auto *src=model();
        for(int i=0;i<src->rowCount() && i<3000;++i) {
            auto preset=src->resourceForIndex(src->index(i,0))
                           .dynamicCast<KisPaintOpPreset>();
            if(preset && preset->valid() && preset->name()==name)
                return preset;
        }
        return KisPaintOpPresetSP();
    }
    KisPaintOpPresetSP firstPresetFor(int slot)
    {
        auto *src=model();
        for(int i=0;i<src->rowCount() && i<3000;++i) {
            auto preset=src->resourceForIndex(src->index(i,0))
                           .dynamicCast<KisPaintOpPreset>();
            if(preset && preset->valid() && inCategory(preset,slot))
                return preset;
        }
        return KisPaintOpPresetSP();
    }
    void setEraser(bool enabled)
    {
        if(!m_window)return;
        QAction *eraser=m_window->actionCollection()->action(
            QStringLiteral("erase_action"));
        if(eraser && eraser->isCheckable() && eraser->isChecked()!=enabled)
            eraser->trigger();
    }
    void setNativePreset(KisPaintOpPresetSP preset)
    {
        if(!preset || !m_vm || !m_vm->paintOpBox())return;
        m_internalChange=true;
        m_vm->paintOpBox()->resourceSelected(preset);
        m_internalChange=false;
    }
    void switchMode(int slot)
    {
        if(!m_vm || !resources() || slot<0 || slot>2)return;
        // Changing presets never replaces Krita's tool or input manager.
        KoToolManager::instance()->switchToolRequested(
            QStringLiteral("KritaShape/KisToolBrush"));
        const QString name=QSettings().value(
            QStringLiteral("MobilePilot/brushSlot%1").arg(slot)).toString();
        KisPaintOpPresetSP preset=findPreset(name);
        if(!preset && slot==0)
            preset=resources()->currentPreset();
        if(!preset && slot!=0)preset=firstPresetFor(slot);
        if(preset)setNativePreset(preset);
        setEraser(slot==1);
        m_slot=slot;
        m_hasSelectedMode=true;
        if(preset)QSettings().setValue(
            QStringLiteral("MobilePilot/brushSlot%1").arg(slot),preset->name());
        refresh();
    }
    void openPresetBrowser(int slot)
    {
        if(!m_vm || !m_vm->paintOpBox() || !resources())return;
        QDialog dialog(m_window);
        dialog.setWindowTitle(i18n("Krita brush presets"));
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#28333f;color:#edf6ff;}"
            "QLabel{color:#edf6ff;font-size:14px;}"
            "QLineEdit,QComboBox{background:#34485b;color:#fff;"
            "border:1px solid #7e9eb4;border-radius:7px;"
            "min-height:44px;padding:4px;}"
            "QListWidget{background:#263443;color:white;"
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
        category->addItem(i18n("All brushes"),-1);
        category->addItem(i18n("Painting"),0);
        category->addItem(i18n("Erasers"),1);
        category->addItem(i18n("Blending / Smudge"),2);
        category->setCurrentIndex(slot+1);
        layout->addWidget(category);
        auto *grid=new QListWidget(&dialog);
        grid->setViewMode(QListView::IconMode);
        grid->setFlow(QListView::LeftToRight);
        grid->setWrapping(true);
        grid->setResizeMode(QListView::Adjust);
        grid->setMovement(QListView::Static);
        grid->setGridSize(QSize(110,116));
        grid->setIconSize(QSize(68,68));
        grid->setWordWrap(true);
        grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        grid->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        grid->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(grid->viewport(),
                               QScroller::LeftMouseButtonGesture);
        layout->addWidget(grid,1);
        auto *close=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(close);
        connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        QList<KisPaintOpPresetSP> presets;
        // Filter native brush resources. Do not maintain an independent
        // preset catalog, duplicate assets or rewrite the paint engine.
        auto refill=[this,grid,search,category,&presets]() {
            grid->clear();presets.clear();
            const QString term=search->text().trimmed();
            const int mode=category->currentData().toInt();
            auto *src=model();
            for(int i=0;i<src->rowCount() && i<3000;++i) {
                KisPaintOpPresetSP p=src->resourceForIndex(src->index(i,0))
                                     .dynamicCast<KisPaintOpPreset>();
                if(!p || !p->valid() ||
                   (mode>=0 && !inCategory(p,mode)) ||
                   (!term.isEmpty() &&
                    !p->name().contains(term,Qt::CaseInsensitive)))continue;
                int idx=presets.size();
                presets.append(p);
                auto *item=new QListWidgetItem(
                    QIcon(QPixmap::fromImage(p->image())),p->name(),grid);
                item->setData(Qt::UserRole,idx);
                item->setToolTip(p->name());
            }
        };
        connect(search,&QLineEdit::textChanged,&dialog,
                [refill](const QString &){refill();});
        connect(category,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[refill](int){refill();});
        connect(grid,&QListWidget::itemClicked,&dialog,
                [this,&dialog,&presets,slot](QListWidgetItem *item) {
            if(!item)return;
            const int index=item->data(Qt::UserRole).toInt();
            if(index<0 || index>=presets.size())return;
            auto preset=presets.at(index);
            if(!preset)return;
            KoToolManager::instance()->switchToolRequested(
                QStringLiteral("KritaShape/KisToolBrush"));
            setNativePreset(preset);
            // Keep selected category (Paint/Erase/Blend), independent of
            // which filter the user used to locate this native preset.
            setEraser(slot==1);
            m_slot=slot;m_hasSelectedMode=true;
            QSettings().setValue(
                QStringLiteral("MobilePilot/brushSlot%1").arg(slot),
                preset->name());
            refresh();
            dialog.accept();
        });
        refill();
        const QSize size=m_window->size();
        dialog.resize(qMax(300,qMin(570,size.width()-32)),
                      qMax(340,qMin(720,size.height()-65)));
        dialog.exec();
    }

    QPointer<KisMainWindow> m_window;
    KisViewManager *m_vm=nullptr;
    QToolButton *m_modes[3]={nullptr,nullptr,nullptr};
    QLabel *m_presetName=nullptr;
    KisDoubleSliderSpinBox *m_size=nullptr,*m_opacity=nullptr;
    KisResourceModel *m_model=nullptr;
    QTimer *m_refresh=nullptr;
    bool m_syncing=false,m_internalChange=false,m_hasSelectedMode=false;
    int m_slot=0;
};
#endif // Q_OS_ANDROID
