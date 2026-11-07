/*
 * TabletCompactSkin: experimental, reversible UI overlays for Krita Android.
 * This is a presentation layer, not a replacement for Krita's resource,
 * paint-op, node or canvas systems. No Q_OBJECT/moc is required.
 */
#pragma once

#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include "KisDocument.h"
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QDateTime>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QPointer>
#include <QPainter>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QHeaderView>
#include <KoToolManager.h>
#include <QScrollBar>
#include <QScroller>
#include <QCursor>
#include <QAction>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_paint_layer.h"
#include "kis_base_node.h"
#include "kis_image.h"
#include "kis_icon_utils.h"
#include "widgets/kis_cmb_composite.h"
#include <KisResourceModel.h>
#include <KoColor.h>
#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <KoResource.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <brushengine/kis_paintop_registry.h>
#include <klocalizedstring.h>

class TabletCompactSkin final : public QObject
{
public:
    TabletCompactSkin(KisMainWindow *window, QMdiArea *mdi, KisViewManager *vm)
        : QObject(window), m_main(window), m_mdi(mdi), m_vm(vm)
    {
        m_startupCompact = QSettings().value(QStringLiteral("TabletUi/compact"),true).toBool();
        m_surface = mdi->viewport();
        m_surface->installEventFilter(this);
        m_mdi->installEventFilter(this);
        // Outside touches close only the temporary wheel; pinned wheel stays.
        qApp->installEventFilter(this);

        m_brushStrip = new QFrame(m_surface);
        m_brushStrip->setObjectName(QStringLiteral("tabletBrushStrip"));
        m_brushStrip->setStyleSheet(
            "#tabletBrushStrip{background:rgba(34,34,38,225);border-radius:8px;}");
        auto *brushLayout = new QHBoxLayout(m_brushStrip);
        brushLayout->setContentsMargins(4,4,4,4);
        brushLayout->setSpacing(3);
        const QStringList labels = {i18n("Paint"), i18n("Erase"), i18n("Blend")};
        for (int slot=0; slot<3; ++slot) {
            auto *button = new QToolButton(m_brushStrip);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setText(labels.at(slot));
            button->setFixedSize(58,61);
            button->setIconSize(QSize(34,34));
            button->setAutoRaise(true);
            button->setToolTip(i18n("Tap to switch; tap again to choose a brush"));
            m_brushButtons[slot] = button;
            brushLayout->addWidget(button);
            connect(button, &QToolButton::clicked, this, [this, slot]() {
                if (slot == m_slot && m_slotReady) {
                    openPresetGrid(slot);
                    return;
                }
                activateSlot(slot);
            });
        }
        m_brushStrip->adjustSize();
        m_brushControls=new QFrame(m_surface);
        m_brushControls->setObjectName(QStringLiteral("tabletBrushControls"));
        m_brushControls->setStyleSheet(
            "#tabletBrushControls{background:rgba(34,34,38,235);border-radius:8px;}");
        m_brushControls->setFixedWidth(190);
        auto *controlLayout=new QVBoxLayout(m_brushControls);
        controlLayout->setContentsMargins(8,6,8,6);
        controlLayout->setSpacing(2);
        auto createSlider=[this,controlLayout](const QString &title,int minimum,int maximum,
                                               QSlider **slider,QLabel **readout) {
            auto *row=new QWidget(m_brushControls);
            auto *layout=new QVBoxLayout(row);
            layout->setContentsMargins(0,0,0,0);
            layout->setSpacing(0);
            auto *header=new QHBoxLayout();
            auto *label=new QLabel(title,row);
            label->setStyleSheet(QStringLiteral("color:white;"));
            *readout=new QLabel(row);
            (*readout)->setStyleSheet(QStringLiteral("color:#d2d2d2;"));
            header->addWidget(label);
            header->addStretch(1);
            header->addWidget(*readout);
            layout->addLayout(header);
            *slider=new QSlider(Qt::Horizontal,row);
            (*slider)->setRange(minimum,maximum);
            layout->addWidget(*slider);
            controlLayout->addWidget(row);
            return row;
        };
        createSlider(i18n("Size"),1,1000,&m_brushSizeSlider,&m_brushSizeValue);
        createSlider(i18n("Opacity"),0,100,&m_brushOpacitySlider,&m_brushOpacityValue);
        m_flowRow=createSlider(i18n("Flow"),0,100,
                               &m_brushFlowSlider,&m_brushFlowValue);
        auto *blendRow=new QHBoxLayout();
        auto *blendLabel=new QLabel(i18n("Blend"),m_brushControls);
        blendLabel->setStyleSheet(QStringLiteral("color:white;"));
        m_brushComposite=new QComboBox(m_brushControls);
        for(const auto &entry:std::initializer_list<std::pair<const char*,const char*>>{
                {"Normal","normal"},{"Multiply","multiply"},{"Screen","screen"},
                {"Overlay","overlay"},{"Burn","burn"},{"Dodge","dodge"},
                {"Darken","darken"},{"Lighten","lighten"}})
            m_brushComposite->addItem(QString::fromLatin1(entry.first),
                                      QString::fromLatin1(entry.second));
        blendRow->addWidget(blendLabel);
        blendRow->addWidget(m_brushComposite,1);
        controlLayout->addLayout(blendRow);
        auto *provider=m_vm->canvasResourceProvider();
        connect(m_brushSizeSlider,&QSlider::valueChanged,this,[provider,this](int value) {
            if(!m_syncingBrushControls)provider->setSize(value);
            m_brushSizeValue->setText(QStringLiteral("%1 px").arg(value));
        });
        connect(m_brushOpacitySlider,&QSlider::valueChanged,this,[provider,this](int value) {
            if(!m_syncingBrushControls)provider->setOpacity(value/100.0);
            m_brushOpacityValue->setText(QStringLiteral("%1%").arg(value));
        });
        connect(m_brushFlowSlider,&QSlider::valueChanged,this,[provider,this](int value) {
            if(!m_syncingBrushControls)provider->setFlow(value/100.0);
            m_brushFlowValue->setText(QStringLiteral("%1%").arg(value));
        });
        connect(m_brushComposite,QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,[provider,this](int index) {
            if(!m_syncingBrushControls && index>=0)
                provider->setCurrentCompositeOp(
                    m_brushComposite->itemData(index).toString());
        });
        m_brushControls->adjustSize();
        // Follow changes made through pen, shortcuts or Krita's own sliders.
        m_brushControlTimer=new QTimer(this);
        m_brushControlTimer->setInterval(350);
        connect(m_brushControlTimer,&QTimer::timeout,this,
                [this]() {
                    refreshBrushControls();
                    // The contextual selection bar may cause Qt to recreate
                    // subwindow chrome after it is pinned/unpinned.
                    if(m_compact && m_mdi->viewMode()==QMdiArea::SubWindowView) {
                        for(QMdiSubWindow *subwin:m_mdi->subWindowList()) {
                            if(subwin && !(subwin->windowFlags() &
                                           Qt::FramelessWindowHint)) {
                                subwin->setWindowFlags(
                                    subwin->windowFlags()|Qt::FramelessWindowHint);
                                subwin->showMaximized();
                            }
                        }
                    }
                });

        m_layerRail = new QFrame(m_surface);
        m_layerRail->setObjectName(QStringLiteral("tabletLayerRail"));
        m_layerRail->setStyleSheet(
            "#tabletLayerRail{background:rgba(31,31,34,230);border-radius:9px;}");
        auto *rail = new QVBoxLayout(m_layerRail);
        rail->setContentsMargins(3,5,3,5);
        rail->setSpacing(3);
        m_layerPinned = QSettings().value(QStringLiteral("TabletUi/layerPinned"),true).toBool();
        m_layerTileSize=qBound(64,QSettings().value(
            QStringLiteral("TabletUi/layerTileSize"),64).toInt(),160);
        m_layerOnRight=QSettings().value(
            QStringLiteral("TabletUi/layerOnRight"),false).toBool();
        m_layerToggleButton = new QToolButton(m_surface);
        m_layerToggleButton->setText(QStringLiteral("▤"));
        m_layerToggleButton->setToolTip(i18n("Open compact layers"));
        m_layerToggleButton->setFixedSize(48,48);
        m_layerToggleButton->setStyleSheet(
            "QToolButton{background:#252528;color:white;"
            "border:1px solid #999;border-radius:9px;font-size:25px;}");
        connect(m_layerToggleButton,&QToolButton::clicked,this,[this]() {
            m_layerExpanded=!m_layerExpanded;
            refreshLayerControls();
        });
        auto *layerHeader = new QHBoxLayout();
        layerHeader->setContentsMargins(0,0,0,0);
        layerHeader->setSpacing(2);
        rail->addLayout(layerHeader);
        m_addLayer = new QToolButton(m_layerRail);
        m_addLayer->setText(i18n("+ Add Layer"));
        m_addLayer->setFixedHeight(38);
        m_addLayer->setToolTip(i18n("Tap: new paint layer. Hold: layer and mask types"));
        layerHeader->addWidget(m_addLayer,1);
        m_layerPinButton = new QToolButton(m_layerRail);
        m_layerPinButton->setFixedSize(38,38);
        m_layerPinButton->setToolTip(i18n("Tap: pin/unpin. Hold: layer settings"));
        layerHeader->addWidget(m_layerPinButton);
        auto *layerPinTimer=new QTimer(this);
        layerPinTimer->setSingleShot(true);
        layerPinTimer->setInterval(330);
        connect(m_layerPinButton,&QToolButton::pressed,this,[this,layerPinTimer]() {
            m_layerPinLongPressed=false;
            layerPinTimer->start();
        });
        connect(layerPinTimer,&QTimer::timeout,this,[this]() {
            m_layerPinLongPressed=true;
        });
        // Opening a nested popup on timeout swallows the release/click event
        // on Android. Open it only after the finger is lifted.
        connect(m_layerPinButton,&QToolButton::released,this,[this,layerPinTimer]() {
            layerPinTimer->stop();
            if(m_layerPinLongPressed)
                QTimer::singleShot(0,this,[this]() {openLayerSettings();});
        });
        connect(m_layerPinButton,&QToolButton::clicked,this,[this]() {
            if(m_layerPinLongPressed){m_layerPinLongPressed=false;return;}
            m_layerPinned=!m_layerPinned;
            m_layerExpanded=m_layerPinned;
            QSettings().setValue(QStringLiteral("TabletUi/layerPinned"),m_layerPinned);
            refreshLayerControls();
        });
        m_addLayerHoldTimer = new QTimer(this);
        m_addLayerHoldTimer->setSingleShot(true);
        m_addLayerHoldTimer->setInterval(550);
        connect(m_addLayer, &QToolButton::pressed, this, [this]() {
            m_addLayerLongPressed = false;
            m_addLayerHoldTimer->start();
        });
        connect(m_addLayer, &QToolButton::released, this, [this]() {
            m_addLayerHoldTimer->stop();
        });
        connect(m_addLayerHoldTimer, &QTimer::timeout, this, [this]() {
            m_addLayerLongPressed = true;
            QMenu menu(m_main);
            auto addNative = [this, &menu](const QString &id) {
                if (QAction *action = m_main->actionCollection()->action(id))
                    menu.addAction(action);
                else
                    qWarning("Compact UI: missing layer creation action %s", qPrintable(id));
            };
            addNative(QStringLiteral("add_new_paint_layer"));
            addNative(QStringLiteral("add_new_group_layer"));
            addNative(QStringLiteral("add_new_clone_layer"));
            addNative(QStringLiteral("add_new_vector_layer"));
            addNative(QStringLiteral("add_new_filter_layer"));
            addNative(QStringLiteral("add_new_fill_layer"));
            addNative(QStringLiteral("add_new_file_layer"));
            menu.addSeparator();
            addNative(QStringLiteral("add_new_transparency_mask"));
            addNative(QStringLiteral("add_new_filter_mask"));
            addNative(QStringLiteral("add_new_colorize_mask"));
            addNative(QStringLiteral("add_new_transform_mask"));
            menu.exec(m_addLayer->mapToGlobal(QPoint(m_addLayer->width(), 0)));
            QTimer::singleShot(150, this, [this]() { refreshLayers(); });
        });
        connect(m_addLayer, &QToolButton::clicked, this, [this]() {
            if (m_addLayerLongPressed) {
                m_addLayerLongPressed = false;
                return;
            }
            trigger(QStringLiteral("add_new_paint_layer"));
            QTimer::singleShot(150, this, [this]() { refreshLayers(); });
        });
        m_layersScroll = new QScrollArea(m_layerRail);
        m_layersScroll->setWidgetResizable(true);
        m_layersScroll->setFrameShape(QFrame::NoFrame);
        m_layersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_layersScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_layersScroll->verticalScrollBar()->setSingleStep(16);
        m_layersScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");
        m_layersScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(m_layersScroll->viewport(),
                              QScroller::LeftMouseButtonGesture);
        m_layersContent = new QWidget(m_layersScroll);
        m_layersLayout = new QVBoxLayout(m_layersContent);
        m_layersLayout->setContentsMargins(0,0,0,0);
        m_layersLayout->setSpacing(3);
        m_layersScroll->setWidget(m_layersContent);
        rail->addWidget(m_layersScroll,0);
        m_layerName = new QLabel(m_layerRail);
        m_layerName->setAlignment(Qt::AlignCenter);
        m_layerName->setWordWrap(true);
        m_layerName->setFixedHeight(34);
        m_layerName->setStyleSheet("color:white;font-size:9px;");
        rail->addWidget(m_layerName);
        m_layerName->setToolTip(i18n("Double-tap to rename selected layer"));
        m_layerName->installEventFilter(this);
        updateLayerWidth();
        m_layerExpanded=m_layerPinned;

        m_colorButton = new QToolButton(m_surface);
        m_colorButton->setFixedSize(48,48);
        m_colorButton->setAutoRaise(false);
        m_colorButton->setToolTip(i18n("Open or collapse color wheel"));
        m_colorButton->setText(QStringLiteral("●"));
        m_colorButton->setStyleSheet(
            "QToolButton{background:#252528;color:white;border:2px solid #aaa;"
            "border-radius:24px;font-size:24px;}");
        connect(m_colorButton, &QToolButton::clicked, this, [this]() {
            toggleColor();
        });
        m_recent = new QFrame(m_surface);
        m_recent->setObjectName(QStringLiteral("tabletRecentColors"));
        m_recent->setStyleSheet(
            "#tabletRecentColors{background:rgba(30,30,33,225);border-radius:5px;}");
        m_recentLayout = new QGridLayout(m_recent);
        m_recentLayout->setContentsMargins(3,3,3,3);
        m_recentLayout->setSpacing(2);
        m_recent->setFixedHeight(30);
        m_colorHistoryRows=qBound(1,QSettings().value(
            QStringLiteral("TabletUi/colorHistoryRows"),1).toInt(),4);
        m_colorShowHistory=QSettings().value(
            QStringLiteral("TabletUi/colorHistoryEnabledV2"),true).toBool();
        m_colorHistoryOnTop=QSettings().value(
            QStringLiteral("TabletUi/colorHistoryOnTop"),false).toBool();

        m_topBar = new QToolBar(i18n("Popup actions"), m_main);
        m_topBar->setObjectName(QStringLiteral("tabletCompactPopupBar"));
        m_topBar->setMovable(false);
        m_topBar->setFloatable(false);
        m_topBar->setIconSize(QSize(22,22));
        auto *presetAction = m_topBar->addAction(
            KisIconUtils::loadIcon("paintop_settings_01"), i18n("All brush presets"));
        presetAction->setToolTip(i18n("Open Krita's full brush preset chooser"));
        m_main->actionCollection()->addAction(QStringLiteral("tablet_all_brush_presets"),presetAction);
        connect(presetAction, &QAction::triggered, this, [this]() {
            QAction *native = m_main->actionCollection()->action(QStringLiteral("show_brush_presets"));
            if (native) native->trigger();
            else if (m_brushDocker) m_brushDocker->show();
        });
        auto *optionsAction = m_topBar->addAction(
            KisIconUtils::loadIcon("configure"), i18n("Tool Options"));
        optionsAction->setToolTip(i18n("Open current tool options"));
        m_main->actionCollection()->addAction(QStringLiteral("tablet_tool_options_popup"),optionsAction);
        connect(optionsAction, &QAction::triggered, this, [this]() {
            if(QAction *native=m_main->actionCollection()->action(
                    QStringLiteral("show_tool_options")))
                native->trigger();
        });
        auto *saveClose = m_topBar->addAction(
            KisIconUtils::loadIcon("document-close"), i18n("Save and Close"));
        saveClose->setToolTip(i18n("Save artwork and return to Krita home"));
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_save_and_close"),saveClose);
        connect(saveClose,&QAction::triggered,this,[this]() {
            // Same close action as Krita's document-tab X. Handles
            // Save/Discard/Cancel and the Save As prompt natively.
            if(QAction *close=m_main->actionCollection()->action(
                    QStringLiteral("file_close")))close->trigger();
        });
        // Dedicated Compact UI toolbar: normal desktop toolbars are untouched.
        m_compactBar=new QToolBar(i18n("Compact UI shortcuts"),m_main);
        m_compactBar->setObjectName(QStringLiteral("tabletDedicatedCompactBar"));
        m_compactBar->setMovable(false);
        m_compactBar->setFloatable(false);
        m_compactBar->setIconSize(QSize(26,26));
        m_main->addToolBar(Qt::TopToolBarArea,m_compactBar);
        m_compactBar->hide();
        m_customizeAction=new QAction(
            KisIconUtils::loadIcon("configure"),i18n("Customize Compact toolbar"),this);
        connect(m_customizeAction,&QAction::triggered,this,
                [this](){configureCompactToolbar();});
        m_toolsAction=new QAction(
            KisIconUtils::loadIcon("tools"),i18n("All Tools"),this);
        connect(m_toolsAction,&QAction::triggered,this,[this](){openAllTools();});
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_all_tools_popup"),m_toolsAction);
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_customize_compact"),m_customizeAction);
        m_sampleModeAction=new QAction(
            KisIconUtils::loadIcon("krita_tool_color_sampler"),
            i18n("Sample all visible layers"),this);
        m_sampleModeAction->setCheckable(true);
        m_sampleModeAction->setChecked(QSettings().value(
            QStringLiteral("TabletUi/sampleAllVisibleLayers"),true).toBool());
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_sample_all_layers"),m_sampleModeAction);
        connect(m_sampleModeAction,&QAction::toggled,this,[this](bool all) {
            QSettings().setValue(QStringLiteral("TabletUi/sampleAllVisibleLayers"),all);
            m_sampleModeAction->setToolTip(all ?
                i18n("Sample all visible layers") :
                i18n("Sample current layer only"));
            if(m_vm->canvasBase()) {
                if(KoToolBase *tool=KoToolManager::instance()->toolById(
                        m_vm->canvasBase(),
                        QStringLiteral("KritaSelected/KisToolColorSampler"))) {
                    QMetaObject::invokeMethod(tool,"slotSetColorSource",
                                              Qt::DirectConnection,Q_ARG(int,all?0:1));
                }
            }
        });
        m_toolOptionsFloating=new QToolButton(m_surface);
        m_toolOptionsFloating->setIcon(KisIconUtils::loadIcon("configure"));
        m_toolOptionsFloating->setIconSize(QSize(28,28));
        m_toolOptionsFloating->setFixedSize(43,43);
        m_toolOptionsFloating->setToolTip(i18n("Tool options"));
        m_toolOptionsFloating->setStyleSheet(
            "QToolButton{background:#303033;border:1px solid #aaa;border-radius:7px;}");
        connect(m_toolOptionsFloating,&QToolButton::clicked,this,[this]() {
            if(QAction *a=m_main->actionCollection()->action(
                    QStringLiteral("show_tool_options")))a->trigger();
        });
        QTimer::singleShot(0,this,[this](){rebuildCompactToolbar();});
        // Tool actions remain registered for both UI modes.

        connect(m_vm->canvasResourceProvider(),
                &KisCanvasResourceProvider::sigPaintOpPresetChanged, this,
                [this](KisPaintOpPresetSP p) {
                    if (!p || m_internalPresetChange) return;
                    const QString name = p->name().toLower();
                    const int group = isBlendName(name) ? 2 :
                        (m_vm->canvasResourceProvider()->eraserMode() ? 1 : 0);
                    m_slot = group;
                    m_slotReady = true;
                    m_lastPreset[group] = p->name();
                    QSettings().setValue(QStringLiteral("TabletUi/slot%1Preset").arg(group),p->name());
                    paintSlotIcons();
                });
        connect(m_vm->nodeManager(), &KisNodeManager::sigNodeActivated,
                this, [this](KisNodeSP) {
                    if (m_compact) QTimer::singleShot(0,this,[this]() {refreshLayers();});
                });
        connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow*) {
            QTimer::singleShot(0,this,[this]() {refreshVisibility();refreshLayers();});
        });
        connect(m_vm->canvasResourceProvider(), &KisCanvasResourceProvider::sigFGColorChanged,
                this, [this](const KoColor&) {if(m_compact) refreshColors();});

        m_layerRefreshTimer = new QTimer(this);
        m_layerRefreshTimer->setInterval(5000);
        connect(m_layerRefreshTimer, &QTimer::timeout,this,[this]() {
            if(m_compact && m_vm->activeNode()) refreshLayers();
        });
        for(int i=0;i<3;++i) {
            m_lastPreset[i] = QSettings().value(
                QStringLiteral("TabletUi/slot%1Preset").arg(i)).toString();
        }
        m_layerRail->hide();
        m_layerToggleButton->hide();
        m_brushStrip->hide();
        m_brushControls->hide();
        m_colorButton->hide();
        m_recent->hide();
        m_toolOptionsFloating->hide();
        m_colorWheelPixels=qBound(190,QSettings().value(
            QStringLiteral("TabletUi/colorWheelPixels"),265).toInt(),380);
        m_colorShowHistory=QSettings().value(
            QStringLiteral("TabletUi/colorShowHistory"),false).toBool();
        m_colorPinned=QSettings().value(QStringLiteral("TabletUi/colorPinned"),false).toBool();
        // Only explicit pinning keeps the selector open after restart.
        m_colorExpanded=m_colorPinned;
        QTimer::singleShot(0,this,[this](){organizeDefaultToolbars();});
        QTimer::singleShot(450,this,[this](){organizeDefaultToolbars();});
    }

    bool eventFilter(QObject *watched,QEvent *event) override {
        if(watched==m_layerName &&
           event->type()==QEvent::MouseButtonDblClick && m_vm->activeNode()) {
            const KisNodeSP node=m_vm->activeNode();
            bool ok=false;
            const QString name=QInputDialog::getText(
                m_main,i18n("Rename Layer"),i18n("Layer name:"),
                QLineEdit::Normal,node->name(),&ok);
            if(ok && !name.trimmed().isEmpty() && name!=node->name()) {
                m_vm->nodeManager()->setNodeName(node,name.trimmed());
                refreshLayers();
            }
            return true;
        }
        if ((watched==m_surface || watched==m_mdi) &&
             (event->type()==QEvent::Resize || event->type()==QEvent::Show)) {
            QTimer::singleShot(0,this,[this](){positionOverlays();});
        }
        if(watched==m_colorDocker && event->type()==QEvent::Move &&
           m_compact && m_colorExpanded) {
            QTimer::singleShot(0,this,[this]() {
                if(m_compact && m_colorExpanded)positionOverlays();
            });
        }
        // Stylus strokes arrive as tablet events on the canvas, not mouse
        // button events. Close unpinned panels without consuming the stroke.
        const bool pointerStart=event->type()==QEvent::MouseButtonPress ||
            event->type()==QEvent::TouchBegin ||
            event->type()==QEvent::TabletPress ||
            (event->type()==QEvent::TabletMove &&
             static_cast<QTabletEvent*>(event)->pressure()>0.0);
        // Dismiss temporary overlays only when the user actually begins
        // drawing on the canvas. Android delivers some synthesized pointer
        // events to the MDI viewport even when fingers are over an overlay.
        // Those must not collapse the layer panel during a long-press.
        if(pointerStart && m_compact) {
            QWidget *target=qobject_cast<QWidget*>(watched);
            const bool canvasTarget=target && (
                target==m_surface || m_surface->isAncestorOf(target));
            QPoint globalPos;
            bool hasPosition=false;
            if(event->type()==QEvent::MouseButtonPress) {
                globalPos=static_cast<QMouseEvent*>(event)->globalPos();
                hasPosition=true;
            } else if(event->type()==QEvent::TabletPress ||
                      event->type()==QEvent::TabletMove) {
                globalPos=static_cast<QTabletEvent*>(event)->globalPos();
                hasPosition=true;
            } else if(event->type()==QEvent::TouchBegin) {
                const auto points=static_cast<QTouchEvent*>(event)->touchPoints();
                if(!points.isEmpty()) {
                    globalPos=points.first().screenPos().toPoint();
                    hasPosition=true;
                }
            }
            const bool layerHit=m_layerRail->isVisible() &&
                (target==m_layerRail || m_layerRail->isAncestorOf(target) ||
                 (hasPosition && QRect(m_layerRail->mapToGlobal(QPoint(0,0)),
                                       m_layerRail->size()).contains(globalPos)));
            const bool colorHit=m_colorDocker && m_colorDocker->isVisible() &&
                (target==m_colorDocker || m_colorDocker->isAncestorOf(target) ||
                 (hasPosition && QRect(m_colorDocker->mapToGlobal(QPoint(0,0)),
                                       m_colorDocker->size()).contains(globalPos)));
            if(canvasTarget && hasPosition && !layerHit && !colorHit) {
                if(!m_layerPinned && m_layerExpanded) {
                    m_layerExpanded=false;
                    refreshLayerControls();
                }
                if(!m_colorPinned && m_colorExpanded) {
                    m_colorExpanded=false;
                    updateColorDock();
                    refreshColorControls();
                }
            }
        }
        return QObject::eventFilter(watched,event);
    }

    void setCompactActive(bool active) {
        if (m_compact==active) return;
        m_compact=active;
        if (active) {
            rememberCompactToolbarState(true);
            rememberDockers();
            const QByteArray storedNormal=QSettings().value(
                QStringLiteral("TabletUi/normalWindowState")).toByteArray();
            if(m_startupCompact && !storedNormal.isEmpty()) {
                m_previousWindowState=storedNormal;
            } else if(m_startupCompact) {
                // An older install may have saved an already-compact layout.
                // No reliable normal snapshot exists yet; recover visible UI
                // on first exit, and save a clean baseline on next entry.
                m_recoverLegacyNormalLayout=true;
                m_previousWindowState.clear();
            } else if(!m_previousWindowState.isEmpty()) {
                QSettings().setValue(QStringLiteral("TabletUi/normalWindowState"),
                                     m_previousWindowState);
            }
            m_startupCompact=false;
            // Restore pinned panels only, not transient open panels.
            m_colorExpanded=m_colorPinned;
            m_layerExpanded=m_layerPinned;
             for(QDockWidget *dock:{m_layerDocker.data(),m_brushDocker.data(),
                                   m_toolOptionsDocker.data()}) {
                if(dock) dock->hide();
            }
            // The original color wheel and its color history are reused.
            // Its floating state is restored on return to normal Krita.
            if(m_colorDocker) {
                m_colorDocker->hide();
                m_previousColorFeatures=m_colorDocker->features();
                m_colorDocker->setFloating(true);
                m_colorDocker->setFeatures(QDockWidget::NoDockWidgetFeatures);
                auto *title=new QWidget(m_colorDocker);
                title->setFixedHeight(1);
                title->setStyleSheet("background:#353538;");
                m_colorDocker->setTitleBarWidget(title);
                // The Wide Gamut selector also has an internal header with
                // an FG/BG toggle and a hamburger menu. Its content is moved
                // into our color footer/lock settings while compact.
                if(QWidget *body=m_colorDocker->widget()) {
                    if(QLayout *bodyLayout=body->layout()) {
                        m_nativeColorMargins=bodyLayout->contentsMargins();
                        m_nativeColorSpacing=bodyLayout->spacing();
                        if(QLayoutItem *item=bodyLayout->itemAt(0)) {
                            if(QWidget *header=item->widget()) {
                                m_nativeSelectorHeader=header;
                                header->hide();
                            }
                        }
                        bodyLayout->setContentsMargins(0,0,0,0);
                        bodyLayout->setSpacing(0);
                    }
                }
                for(QToolButton *candidate:m_colorDocker->findChildren<QToolButton*>()) {
                    if(candidate->menu()) {
                        m_nativeSelectorMenuButton=candidate;
                        candidate->hide();
                        break;
                    }
                }
                // The footer must share the floating dock's window to
                // receive finger and pen input above the canvas.
                if(m_recent->parentWidget()!=m_colorDocker)
                    m_recent->setParent(m_colorDocker);
                const int wheelWidth=m_colorWheelPixels;
                m_colorDocker->resize(QSize(wheelWidth,
                    wheelWidth+24+m_recent->height()));
                if(!m_colorPinButton) {
                    // Pin and settings live in the attached history footer.
                    m_colorPinButton=new QToolButton(m_recent);
                    m_colorPinButton->setFixedSize(36,36);
                    m_colorPinButton->setIconSize(QSize(28,28));
                    m_colorPinButton->setStyleSheet(
                        "QToolButton{background:#242428;color:white;"
                        "border:1px solid #aaa;border-radius:6px;font-size:18px;}");
                    m_colorPinButton->setToolTip(i18n("Tap: pin. Hold: color wheel settings"));
                    m_colorPinButton->setAutoRaise(false);
                    m_colorDocker->installEventFilter(this);
                    auto *pinTimer=new QTimer(this);
                    pinTimer->setSingleShot(true);
                    pinTimer->setInterval(330);
                    connect(m_colorPinButton,&QToolButton::pressed,this,
                            [this,pinTimer]() {
                        m_colorPinLongPressed=false;
                        pinTimer->start();
                    });
                    connect(pinTimer,&QTimer::timeout,this,[this]() {
                        m_colorPinLongPressed=true;
                    });
                    connect(m_colorPinButton,&QToolButton::released,this,
                            [this,pinTimer]() {
                        pinTimer->stop();
                        if(m_colorPinLongPressed)
                            QTimer::singleShot(0,this,[this](){openColorSettings();});
                    });
                    connect(m_colorPinButton,&QToolButton::clicked,this,[this]() {
                        if(m_colorPinLongPressed) {
                            m_colorPinLongPressed=false;
                            return;
                        }
                        m_colorPinned=!m_colorPinned;
                        QSettings().setValue(QStringLiteral("TabletUi/colorPinned"),
                                             m_colorPinned);
                        m_colorExpanded=m_colorPinned;
                        updateColorDock();
                        refreshColorControls();
                    });
                }
                m_colorPinButton->setText(QString());
                m_colorPinButton->setIcon(lockIcon(m_colorPinned));
            }
             m_layerRefreshTimer->start();
            m_brushControlTimer->start();
        } else {
            rememberCompactToolbarState(false);
            m_layerRefreshTimer->stop();
            m_brushControlTimer->stop();
             if(m_colorDocker) {
                m_colorDocker->hide();
                m_colorDocker->setFloating(false);
                QPointer<QWidget> customTitle=m_colorDocker->titleBarWidget();
                m_colorDocker->setTitleBarWidget(nullptr);
                if(customTitle)customTitle->deleteLater();
                m_colorDocker->setFeatures(m_previousColorFeatures);
                if(m_nativeSelectorHeader)m_nativeSelectorHeader->show();
                if(QWidget *body=m_colorDocker->widget()) {
                    if(QLayout *bodyLayout=body->layout()) {
                        bodyLayout->setContentsMargins(m_nativeColorMargins);
                        bodyLayout->setSpacing(m_nativeColorSpacing);
                    }
                }
                if(m_nativeSelectorMenuButton)m_nativeSelectorMenuButton->show();
            }
            if(!m_previousWindowState.isEmpty())
                m_main->restoreState(m_previousWindowState);
            if(m_recoverLegacyNormalLayout) {
                for(QDockWidget *dock : {m_layerDocker.data(),m_brushDocker.data(),
                                        m_toolOptionsDocker.data(),m_colorDocker.data()}) {
                    if(dock)dock->show();
                }
                for(QToolBar *bar : m_main->findChildren<QToolBar*>()) {
                    if(bar!=m_topBar)bar->show();
                }
                m_recoverLegacyNormalLayout=false;
            }
        }
        refreshVisibility();
        refreshLayers();
    }

private:
    static bool isEraseName(const QString &s) {
        return s.contains(QStringLiteral("eraser")) ||
               s.contains(QStringLiteral("erase")) ||
               s.contains(QStringLiteral("rubber"));
    }
    static bool isBlendName(const QString &s) {
        return s.contains(QStringLiteral("blender")) ||
               s.contains(QStringLiteral("smudge")) ||
               s.contains(QStringLiteral("blur")) ||
               s.contains(QStringLiteral("mixing"));
    }
    QString groupKey(const QString &name) const {
        return QStringLiteral("TabletUi/presetGroup/") +
            QString::fromLatin1(QCryptographicHash::hash(
                name.toUtf8(),QCryptographicHash::Md5).toHex());
    }
    QString presetPinKey(const QString &name) const {
        return QStringLiteral("TabletUi/presetPinned/") +
            QString::fromLatin1(QCryptographicHash::hash(
                name.toUtf8(),QCryptographicHash::Md5).toHex());
    }
    bool presetInGroup(const KisPaintOpPresetSP &preset,int slot) const {
        if (!preset) return false;
        const QVariant manual=QSettings().value(groupKey(preset->name()));
        if(manual.isValid()) return manual.toInt()==slot;
        const QString name=preset->name().toLower();
        if(slot==1) return isEraseName(name);
        if(slot==2) return isBlendName(name);
        return !isEraseName(name)&&!isBlendName(name);
    }
    void rememberDockers() {
        m_previousWindowState=m_main->saveState();
        m_layerDocker=nullptr;
        m_brushDocker=nullptr;
        m_colorDocker=nullptr;
        m_toolOptionsDocker=nullptr;
        for (QDockWidget *dock : m_main->findChildren<QDockWidget*>()) {
            const QString name=dock->objectName();
            if(name==QStringLiteral("KisLayerBox"))m_layerDocker=dock;
            if(name==QStringLiteral("sharedtooldocker"))m_toolOptionsDocker=dock;
            if(name.contains(QStringLiteral("BrushPreset"),Qt::CaseInsensitive) ||
               dock->windowTitle().contains(i18n("Brush Presets"),Qt::CaseInsensitive))
                m_brushDocker=dock;
            if(name.contains(QStringLiteral("ColorSelector"),Qt::CaseInsensitive) ||
               dock->windowTitle().contains(i18n("Advanced Color Selector"),Qt::CaseInsensitive))
                m_colorDocker=dock;
        }
    }
    void refreshBrushControls() {
        if(!m_compact || !m_vm->activeNode())return;
        auto *provider=m_vm->canvasResourceProvider();
        if(!provider)return;
        m_syncingBrushControls=true;
        m_brushSizeSlider->setValue(qBound(1,qRound(provider->size()),1000));
        m_brushOpacitySlider->setValue(qBound(0,qRound(provider->opacity()*100),100));
        m_brushFlowSlider->setValue(qBound(0,qRound(provider->flow()*100),100));
        bool hasFlow=false;
        const auto preset=provider->currentPreset();
        if(preset && preset->settings())
            hasFlow=preset->settings()->hasProperty(QStringLiteral("FlowValue"));
        m_flowRow->setVisible(hasFlow);
        const int index=m_brushComposite->findData(provider->currentCompositeOp());
        if(index>=0)m_brushComposite->setCurrentIndex(index);
        m_syncingBrushControls=false;
        m_brushControls->adjustSize();
    }
    void rememberCompactToolbarState(bool compactMode) {
        if(compactMode) {
            m_originalToolbars.clear();
            for(QToolBar *bar:m_main->findChildren<QToolBar*>()) {
                if(bar==m_compactBar)continue;
                if(bar->isVisible())m_originalToolbars.append(bar);
                if(bar->objectName()!=QStringLiteral("tabletTransformContextBar"))
                    bar->hide();
            }
            m_compactBar->show();
            m_compactBar->raise();
        } else {
            m_compactBar->hide();
            for(const QPointer<QToolBar> &bar:m_originalToolbars)
                if(bar)bar->show();
            m_originalToolbars.clear();
        }
    }
    QStringList compactDefaultActions() const {
        return {QStringLiteral("tablet_quick_actions"),
                QStringLiteral("tablet_save_and_close"),
                QStringLiteral("tablet_all_brush_presets"),
                QStringLiteral("tablet_sample_all_layers"),
                QStringLiteral("view_toggle_reference_images"),
                QStringLiteral("erase_action"),
                QStringLiteral("tablet_all_tools_popup")};
    }
    void rebuildCompactToolbar() {
        if(!m_compactBar)return;
        m_compactBar->clear();
        auto *collection=m_main->actionCollection();
        QAction *compact=collection->action(QStringLiteral("tablet_compact_ui"));
        if(compact)m_compactBar->addAction(compact);
        m_compactBar->addAction(m_customizeAction);
        const QStringList chosen=QSettings().value(
            QStringLiteral("TabletUi/compactActionOrderV1"),
            compactDefaultActions()).toStringList();
        for(const QString &id:chosen) {
            if(id==QStringLiteral("tablet_compact_ui") ||
               id==QStringLiteral("tablet_customize_compact"))continue;
            if(QAction *action=collection->action(id))
                m_compactBar->addAction(action);
        }
    }
    void configureCompactToolbar() {
        QDialog dialog(m_main);
        dialog.setWindowTitle(i18n("Customize Compact UI Toolbar"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *tabs=new QTabWidget(&dialog);
        layout->addWidget(tabs);
        const QStringList frequent={
            QStringLiteral("tablet_quick_actions"),
            QStringLiteral("tablet_save_and_close"),
            QStringLiteral("tablet_all_brush_presets"),
            QStringLiteral("tablet_all_tools_popup"),
            QStringLiteral("tablet_sample_all_layers"),
            QStringLiteral("view_toggle_reference_images"),
            QStringLiteral("erase_action"),
            QStringLiteral("file_save"),
            QStringLiteral("edit_undo"),
            QStringLiteral("edit_redo"),
            QStringLiteral("select_all"),
            QStringLiteral("deselect"),
            QStringLiteral("tool_crop"),
            QStringLiteral("tablet_settings")};
        const QStringList original=QSettings().value(
            QStringLiteral("TabletUi/compactActionOrderV1"),
            compactDefaultActions()).toStringList();
        auto *favorites=new QListWidget(tabs);
        favorites->setDragDropMode(QAbstractItemView::InternalMove);
        favorites->setDefaultDropAction(Qt::MoveAction);
        tabs->addTab(favorites,i18n("Frequently used"));
        auto *allPage=new QWidget(tabs);
        auto *allLayout=new QVBoxLayout(allPage);
        auto *search=new QLineEdit(allPage);
        search->setPlaceholderText(i18n("Search Krita actions..."));
        allLayout->addWidget(search);
        auto *all=new QListWidget(allPage);
        allLayout->addWidget(all);
        tabs->addTab(allPage,i18n("All Krita actions"));
        auto *collection=m_main->actionCollection();
        auto addItem=[&](QListWidget *list,QAction *action,const QString &id) {
            if(!action || id==QStringLiteral("tablet_compact_ui") ||
               id==QStringLiteral("tablet_customize_compact"))return;
            auto *item=new QListWidgetItem(
                action->text().remove(QLatin1Char('&')).isEmpty() ?
                id : action->text().remove(QLatin1Char('&')),list);
            item->setData(Qt::UserRole,id);
            item->setFlags(item->flags()|Qt::ItemIsUserCheckable|
                          Qt::ItemIsDragEnabled);
            item->setCheckState(original.contains(id)?Qt::Checked:Qt::Unchecked);
            item->setIcon(action->icon());
        };
        for(const QString &id:frequent)addItem(favorites,collection->action(id),id);
        for(QAction *action:collection->actions()) {
            if(!action || action->objectName().isEmpty())continue;
            addItem(all,action,action->objectName());
        }
        all->sortItems();
        connect(search,&QLineEdit::textChanged,all,[all](const QString &text) {
            for(int i=0;i<all->count();++i)
                all->item(i)->setHidden(
                    !all->item(i)->text().contains(text,Qt::CaseInsensitive) &&
                    !all->item(i)->data(Qt::UserRole).toString().contains(
                        text,Qt::CaseInsensitive));
        });
        auto sync=[favorites,all](QListWidget *source,int row) {
            if(row<0)return;
            auto *item=source->item(row);
            const QString id=item->data(Qt::UserRole).toString();
            QListWidget *other=source==favorites?all:favorites;
            for(int j=0;j<other->count();++j)
                if(other->item(j)->data(Qt::UserRole).toString()==id)
                    other->item(j)->setCheckState(item->checkState());
        };
        connect(favorites,&QListWidget::itemChanged,&dialog,
                [favorites,all,sync](QListWidgetItem *item) {
            sync(favorites,favorites->row(item));
        });
        connect(all,&QListWidget::itemChanged,&dialog,
                [all,sync](QListWidgetItem *item) {
            sync(all,all->row(item));
        });
        auto *buttons=new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.resize(qMin(650,m_surface->width()-20),
                      qMin(750,m_surface->height()-20));
        if(dialog.exec()!=QDialog::Accepted)return;
        QStringList selected;
        for(int i=0;i<favorites->count();++i) {
            auto *item=favorites->item(i);
            if(item->checkState()==Qt::Checked)
                selected.append(item->data(Qt::UserRole).toString());
        }
        for(const QString &id:original) {
            if(selected.contains(id))continue;
            for(int i=0;i<all->count();++i) {
                if(all->item(i)->data(Qt::UserRole).toString()==id &&
                   all->item(i)->checkState()==Qt::Checked) {
                    selected.append(id);break;
                }
            }
        }
        for(int i=0;i<all->count();++i) {
            auto *item=all->item(i);
            if(item->checkState()==Qt::Checked &&
               !selected.contains(item->data(Qt::UserRole).toString()))
                selected.append(item->data(Qt::UserRole).toString());
        }
        QSettings().setValue(QStringLiteral("TabletUi/compactActionOrderV1"),selected);
        rebuildCompactToolbar();
    }
    void openAllTools() {
        QDialog dialog(m_main);
        dialog.setWindowFlags(Qt::Popup);
        auto *layout=new QVBoxLayout(&dialog);
        auto *filter=new QLineEdit(&dialog);
        filter->setPlaceholderText(i18n("Find a tool..."));
        layout->addWidget(filter);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        auto *body=new QWidget(scroll);
        auto *grid=new QGridLayout(body);
        scroll->setWidget(body);
        layout->addWidget(scroll);
        QList<QToolButton*> entries;
        int index=0;
        for(KoToolAction *action:KoToolManager::instance()->toolActionList()) {
            if(!action)continue;
            auto *button=new QToolButton(body);
            button->setText(action->iconText());
            button->setToolTip(action->toolTip());
            button->setIcon(KisIconUtils::loadIcon(action->iconName()));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(34,34));
            button->setFixedSize(94,82);
            grid->addWidget(button,index/4,index%4);
            entries.append(button);
            connect(button,&QToolButton::clicked,&dialog,[action,&dialog]() {
                action->trigger();
                dialog.accept();
            });
            ++index;
        }
        connect(filter,&QLineEdit::textChanged,&dialog,
                [entries](const QString &text) {
            for(QToolButton *button:entries)
                button->setVisible(button->text().contains(
                    text,Qt::CaseInsensitive) || button->toolTip().contains(
                    text,Qt::CaseInsensitive));
        });
        dialog.resize(qMin(530,m_surface->width()-20),
                      qMin(700,m_surface->height()-20));
        dialog.exec();
    }
    void organizeDefaultToolbars() {
        QToolBar *mainBar=nullptr;
        const auto bars=m_main->findChildren<QToolBar*>();
        for(QToolBar *bar:bars)
            if(bar->objectName()==QStringLiteral("mainToolBar"))mainBar=bar;
        if(!mainBar)return;
        auto *actions=m_main->actionCollection();
        QAction *compact=actions->action(QStringLiteral("tablet_compact_ui"));
        // Do not insert Compact UI-only actions into the normal Krita toolbar.
        rebuildCompactToolbar();
        // Keep Compact UI first on every startup, including saved layouts.
        if(compact && mainBar->actions().value(0)!=compact) {
            for(QToolBar *bar:bars)if(bar!=mainBar)bar->removeAction(compact);
            mainBar->removeAction(compact);
            mainBar->insertAction(mainBar->actions().isEmpty() ?
                nullptr:mainBar->actions().first(),compact);
        }

        // Preserve the user's normal Krita toolbar layout. The Compact-only
        // controls no longer belong on mainToolBar.
        if(QSettings().value(QStringLiteral("TabletUi/dedicatedToolbarV1"),false).toBool())
            return;
        QSettings().setValue(QStringLiteral("TabletUi/dedicatedToolbarV1"),true);
        for(QToolBar *bar:bars) {
            if(bar==m_compactBar)continue;
            for(const char *name:{"tablet_save_and_close",
                    "tablet_all_brush_presets","tablet_tool_options_popup"}) {
                if(QAction *action=actions->action(QString::fromLatin1(name)))
                    bar->removeAction(action);
            }
        }
        return;
        // Legacy setup below is disabled: it changed normal Krita toolbars.
        if(QSettings().value(QStringLiteral("TabletUi/toolbarDefaultsV12"),false).toBool())
            return;
        QSettings().setValue(QStringLiteral("TabletUi/toolbarDefaultsV12"),true);
        for(QToolBar *bar:bars) {
            const auto items=bar->actions();
            for(QAction *action:items) {
                if(!action)continue;
                const QString id=action->objectName().toLower();
                const QString label=(action->text()+QLatin1Char(' ')+
                                     action->toolTip()).toLower();
                const bool redundant=id.contains(QStringLiteral("mirror")) ||
                    id.contains(QStringLiteral("wrap")) ||
                    id.contains(QStringLiteral("workspace")) ||
                    id==QStringLiteral("select_layout") ||
                    id==QStringLiteral("expanding_spacer_1") ||
                    label.contains(QStringLiteral("mirror horizontal")) ||
                    label.contains(QStringLiteral("mirror vertical")) ||
                    label.contains(QStringLiteral("wrap around")) ||
                    label.contains(QStringLiteral("workspace"));
                if(redundant || action==actions->action(QStringLiteral("tablet_reset_view")) ||
                   action==actions->action(QStringLiteral("tablet_settings")))
                    bar->removeAction(action);
            }
            if(compact && bar!=mainBar)bar->removeAction(compact);
        }
        if(compact) {
            mainBar->removeAction(compact);
            mainBar->insertAction(mainBar->actions().isEmpty() ?
                nullptr:mainBar->actions().first(),compact);
        }
    }
    void trigger(const QString &name) {
        if(QAction *a=m_main->actionCollection()->action(name))a->trigger();
    }
    void applyEraser(bool enable) {
        QAction *action=m_main->actionCollection()->action(QStringLiteral("erase_action"));
        if(action && action->isChecked()!=enable)action->trigger();
    }
    KisResourceModel *presetModel() {
        if (!m_presetModel) m_presetModel = new KisResourceModel(ResourceType::PaintOpPresets, this);
        return m_presetModel;
    }
    KisPaintOpPresetSP findPreset(const QString &name) {
        if(name.isEmpty()) return KisPaintOpPresetSP();
        KisResourceModel *resources = presetModel();
        for(int i=0;i<resources->rowCount() && i<3000;++i) {
            auto p=resources->resourceForIndex(resources->index(i,0))
                       .dynamicCast<KisPaintOpPreset>();
            if(p && p->name()==name && p->valid())return p;
        }
        return KisPaintOpPresetSP();
    }
    bool createBlendingVersion(const KisPaintOpPresetSP &source) {
        if(!source || !source->settings())return false;
        // Color Smudge and Pixel share this brush-tip XML property. Other
        // engines may not: refuse conversion rather than creating a broken tip.
        const QString brushXml=source->settings()->getString(
            QStringLiteral("brush_definition"));
        if(brushXml.isEmpty()) {
            QMessageBox::information(m_main,i18n("Blend brush"),
                i18n("This brush engine cannot transfer its tip directly. "
                     "Use Krita's Brush Editor to create a Color Smudge preset."));
            return false;
        }
        KisPaintOpPresetSP base;
        auto *resources=presetModel();
        for(int i=0;i<resources->rowCount() && i<3000;++i) {
            auto p=resources->resourceForIndex(resources->index(i,0))
                        .dynamicCast<KisPaintOpPreset>();
            if(p && p->valid() && p->paintOp().id()==QStringLiteral("colorsmudge")) {
                base=p; break;
            }
        }
        if(!base) {
            base=KisPaintOpRegistry::instance()->defaultPreset(
                KoID(QStringLiteral("colorsmudge")),
                source->resourcesInterface());
        }
        if(!base || !base->settings()) {
            QMessageBox::warning(m_main,i18n("Blend brush"),
                i18n("The Color Smudge engine is not available."));
            return false;
        }
        KisPaintOpPresetSP created=base->clone().dynamicCast<KisPaintOpPreset>();
        if(!created)return false;
        auto settings=created->settings()->clone();
        settings->setProperty(QStringLiteral("brush_definition"),brushXml);
        // Reuse the source tip and size, but retain genuine Color Smudge
        // options from the base preset (smudge length, color rate, etc.).
        settings->setPaintOpSize(source->settings()->paintOpSize());
        created->setSettings(settings);
        const QString newName=source->name()+QStringLiteral(" (Blend)");
        created->setName(newName);
        created->setFilename(QStringLiteral("compact_blend_%1.kpp")
                             .arg(QDateTime::currentMSecsSinceEpoch()));
        created->setResourceId(-1);
        created->setStorageLocation(QString());
        created->setImage(source->image());
        created->setValid(true);
        if(!resources->addResourceDeduplicateFileName(created,QString())) {
            QMessageBox::warning(m_main,i18n("Blend brush"),
                i18n("Could not save the blending preset to Krita resources."));
            return false;
        }
        QSettings().setValue(groupKey(newName),2);
        m_lastPreset[2]=newName;
        QSettings().setValue(QStringLiteral("TabletUi/slot2Preset"),newName);
        paintSlotIcons();
        return true;
    }
    void activateSlot(int slot) {
        if(slot<0 || slot>2) return;
        if(!m_vm->canvasResourceProvider())return;
        KisPaintOpPresetSP preset=findPreset(m_lastPreset[slot]);
        if(!preset) {
            KisResourceModel *resources = presetModel();
            for(int i=0;i<resources->rowCount() && i<3000;++i) {
                auto p=resources->resourceForIndex(resources->index(i,0))
                           .dynamicCast<KisPaintOpPreset>();
                if(p && p->valid() && presetInGroup(p,slot)){preset=p;break;}
            }
        }
        m_slot=slot;
        m_slotReady=true;
        trigger(QStringLiteral("KritaShape/KisToolBrush"));
        if(preset) {
            m_internalPresetChange=true;
            m_vm->paintOpBox()->resourceSelected(preset);
            m_internalPresetChange=false;
            m_lastPreset[slot]=preset->name();
            QSettings().setValue(QStringLiteral("TabletUi/slot%1Preset").arg(slot),preset->name());
        }
        // Blend/Smudge presets carry their own paint-op mode, not erase mode.
        applyEraser(slot==1);
        paintSlotIcons();
    }
    void paintSlotIcons() {
        for(int i=0;i<3;++i) {
            QToolButton *button=m_brushButtons[i];
            if(!button)continue;
            auto preset=findPreset(m_lastPreset[i]);
            if(preset && !preset->image().isNull())
                button->setIcon(QIcon(QPixmap::fromImage(preset->image())));
            button->setStyleSheet(i==m_slot && m_slotReady ?
                "QToolButton{color:white;border:2px solid #e56c6c;"
                "border-radius:5px;background:#38383d;}" :
                "QToolButton{color:white;border:none;border-radius:5px;}");
        }
    }
    void openPresetGrid(int slot) {
        QDialog dialog(m_main);
        dialog.setWindowTitle(i18n("Choose %1 preset",QStringList{
            i18n("Paint"),i18n("Erase"),i18n("Blend")}.at(slot)));
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        dialog.resize(qMax(310,qMin(670,m_surface->width()-20)),
                      qMax(390,qMin(790,m_surface->height()-24)));
        auto *layout=new QVBoxLayout(&dialog);
        auto *all=new QCheckBox(i18n("Show all Krita brushes"),&dialog);
        layout->addWidget(all);
        auto *grid=new QListWidget(&dialog);
        grid->setViewMode(QListView::IconMode);
        grid->setFlow(QListView::LeftToRight);
        grid->setWrapping(true);
        grid->setResizeMode(QListView::Adjust);
        grid->setIconSize(QSize(64,64));
        grid->setGridSize(QSize(99,112));
        grid->setSpacing(3);
        grid->setWordWrap(true);
        grid->setSelectionMode(QAbstractItemView::SingleSelection);
        grid->setMovement(QListView::Static);
        grid->setDragDropMode(QAbstractItemView::NoDragDrop);
        grid->setDragEnabled(false);
        grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        grid->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Android's mouse synthesis is more reliable than QScroller's touch
        // recognizer for QListWidget icon cells. Never grab both gestures:
        // Qt permits only one recognizer per viewport.
        grid->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(grid->viewport(),
                              QScroller::LeftMouseButtonGesture);
        layout->addWidget(grid,1);
        auto *assign=new QPushButton(i18n("Assign current brush to this category"),&dialog);
        layout->addWidget(assign);
        auto *cancel=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(cancel);
        connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
        QList<KisPaintOpPresetSP> candidates;
        auto refill=[this,slot,grid,&candidates,all]() {
            grid->clear();
            candidates.clear();
            KisResourceModel *resources = presetModel();
            for(int i=0;i<resources->rowCount() && i<3000;++i) {
                auto preset=resources->resourceForIndex(resources->index(i,0))
                                    .dynamicCast<KisPaintOpPreset>();
                if(!preset || !preset->valid())continue;
                if(!all->isChecked() && !presetInGroup(preset,slot))continue;
                candidates.append(preset);
            }
            std::stable_sort(candidates.begin(),candidates.end(),
                [this](const KisPaintOpPresetSP &a,const KisPaintOpPresetSP &b) {
                    return QSettings().value(presetPinKey(a->name()),false).toBool() &&
                        !QSettings().value(presetPinKey(b->name()),false).toBool();
                });
            for(int idx=0;idx<candidates.size();++idx) {
                const auto preset=candidates.at(idx);
                const bool pinned=QSettings().value(
                    presetPinKey(preset->name()),false).toBool();
                auto *item=new QListWidgetItem(
                    QIcon(QPixmap::fromImage(preset->image())),
                    (pinned?QStringLiteral("★ "):QString())+preset->name(),grid);
                item->setData(Qt::UserRole,idx);
                item->setToolTip(pinned?i18n("Pinned: %1",preset->name()):preset->name());
                if(all->isChecked() && presetInGroup(preset,slot))
                    item->setBackground(QColor(70,103,113,90));
            }
        };
        QTimer holdTimer(&dialog);
        holdTimer.setSingleShot(true);
        QListWidgetItem *heldItem=nullptr;
        bool heldAction=false;
        connect(grid,&QListWidget::itemPressed,&dialog,
                [&holdTimer,&heldItem,&heldAction](QListWidgetItem *item) {
            heldItem=item;
            heldAction=false;
            holdTimer.start(320);
        });
        connect(QScroller::scroller(grid->viewport()),&QScroller::stateChanged,
                &dialog,[&holdTimer](QScroller::State state) {
            if(state!=QScroller::Inactive)holdTimer.stop();
        });
        connect(&holdTimer,&QTimer::timeout,&dialog,
                [this,all,&candidates,&heldItem,&heldAction,refill,&dialog]() {
            if(!heldItem)return;
            const int index=heldItem->data(Qt::UserRole).toInt();
            if(index<0 || index>=candidates.size())return;
            const auto preset=candidates.at(index);
            if(!preset)return;
            heldAction=true;
            QMenu options(&dialog);
            QAction *remove=nullptr;
            if(!all->isChecked())
                remove=options.addAction(i18n("Remove from this category"));
            QAction *paint=options.addAction(i18n("Add to Paint"));
            QAction *erase=options.addAction(i18n("Add to Erase"));
            options.addAction(i18n("Add to Blend"));
            options.addSeparator();
            QAction *makeBlend=options.addAction(i18n("Create blending version"));
            const bool alreadyPinned=QSettings().value(
                presetPinKey(preset->name()),false).toBool();
            QAction *pin=options.addAction(alreadyPinned ?
                i18n("Unpin from top") : i18n("Pin to top"));
            QAction *selected=options.exec(QCursor::pos());
            if(!selected)return;
            if(selected==pin) {
                QSettings().setValue(presetPinKey(preset->name()),!alreadyPinned);
                refill();
                return;
            }
            if(selected==makeBlend) {
                if(createBlendingVersion(preset))refill();
                return;
            }
            const int group=selected==remove?-1:
                selected==paint?0:selected==erase?1:2;
            QSettings().setValue(groupKey(preset->name()),group);
            refill();
        });
        connect(all,&QCheckBox::toggled,&dialog,[&holdTimer,refill](bool){
            holdTimer.stop();refill();
        });
        connect(assign,&QPushButton::clicked,&dialog,[this,slot,refill]() {
            auto p=m_vm->canvasResourceProvider()->currentPreset();
            if(!p)return;
            QSettings().setValue(groupKey(p->name()),slot);
            m_lastPreset[slot]=p->name();
            QSettings().setValue(QStringLiteral("TabletUi/slot%1Preset").arg(slot),p->name());
            paintSlotIcons();
            refill();
        });
        connect(grid,&QListWidget::itemClicked,&dialog,
                [this,slot,&candidates,&dialog,&holdTimer,&heldAction](QListWidgetItem *item) {
            holdTimer.stop();
            if(heldAction) {heldAction=false;return;}
            int index=item->data(Qt::UserRole).toInt();
            if(index<0 || index>=candidates.size())return;
            auto preset=candidates.at(index);
            // Tapping in Show All selects without changing group membership.
            m_lastPreset[slot]=preset->name();
            activateSlot(slot);
            dialog.accept();
        });
        refill();
        dialog.exec();
    }
    void updateLayerWidth() {
        m_layerRail->setFixedWidth(m_layerTileSize+10);
        m_addLayer->setText(m_layerTileSize<110 ? QStringLiteral("+") :
                                                   i18n("+ Add Layer"));
        if(m_layerRail->isVisible())positionOverlays();
    }
    static QIcon lockIcon(bool locked) {
        QPixmap pix(32,32);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(245,245,245),2.8));
        p.setBrush(Qt::NoBrush);
        if(locked) {
            p.drawArc(QRectF(9,4,14,20),0,180*16);
        } else {
            p.drawArc(QRectF(15,4,14,20),0,180*16);
            p.drawLine(QPointF(15,14),QPointF(15,17));
        }
        p.setBrush(QColor(235,235,235));
        p.drawRoundedRect(QRectF(7,15,18,14),2,2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(42,42,45));
        p.drawEllipse(QRectF(14,20,4,4));
        // Small cog on the upper right signals long-press settings.
        p.setPen(QPen(QColor(255,255,255),1));
        p.setBrush(QColor(45,48,52));
        p.drawEllipse(QRectF(23,1,8,8));
        for(int i=0;i<8;++i) {
            const double rad=i*3.141592653589793/4.0;
            p.drawLine(QPointF(27+4*std::cos(rad),5+4*std::sin(rad)),
                       QPointF(27+5.5*std::cos(rad),5+5.5*std::sin(rad)));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(26,4,2,2));
        return QIcon(pix);
    }
    void openLayerSettings() {
        QDialog dialog(m_main);
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        auto *layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(i18n("Layer thumbnail size"),&dialog));
        auto *size=new QSlider(Qt::Horizontal,&dialog);
        size->setRange(64,160);
        size->setSingleStep(4);
        size->setValue(m_layerTileSize);
        layout->addWidget(size);
        auto *value=new QLabel(QStringLiteral("%1 × %1").arg(m_layerTileSize),&dialog);
        layout->addWidget(value);
        connect(size,&QSlider::valueChanged,&dialog,[this,value](int newSize) {
            m_layerTileSize=newSize;
            value->setText(QStringLiteral("%1 × %1").arg(newSize));
            QSettings().setValue(QStringLiteral("TabletUi/layerTileSize"),newSize);
            updateLayerWidth();
            refreshLayers();
        });
        layout->addWidget(new QLabel(i18n("Layer panel position"),&dialog));
        auto *side=new QComboBox(&dialog);
        side->addItem(i18n("Left"),false);
        side->addItem(i18n("Right"),true);
        side->setCurrentIndex(m_layerOnRight?1:0);
        layout->addWidget(side);
        connect(side,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,
                [this](int index) {
            m_layerOnRight=index==1;
            QSettings().setValue(QStringLiteral("TabletUi/layerOnRight"),m_layerOnRight);
            positionOverlays();
        });
        dialog.setMinimumWidth(260);
        dialog.exec();
    }
    void refreshLayerControls() {
        const bool show=m_compact && m_vm->activeNode() && m_surface->isVisible();
        m_layerRail->setVisible(show && (m_layerPinned || m_layerExpanded));
        m_layerToggleButton->setVisible(show && !m_layerPinned && !m_layerExpanded);
        m_layerPinButton->setText(QString());
        m_layerPinButton->setIcon(lockIcon(m_layerPinned));
        m_layerPinButton->setIconSize(QSize(26,26));
        m_layerPinButton->setToolTip(m_layerPinned ?
            i18n("Unpin and collapse layers") : i18n("Pin layers open"));
        if(show)positionOverlays();
    }
    void clearLayerRows() {
        while (m_layersLayout->count()) {
            QLayoutItem *item=m_layersLayout->takeAt(0);
            if(item->widget())item->widget()->deleteLater();
            delete item;
        }
    }
    void refreshLayers() {
        if(!m_compact || !m_vm->activeNode())return;
        auto image=m_vm->image().toStrongRef();
        if(!image || !image->root())return;
        const int oldScroll=m_layersScroll->verticalScrollBar()->value();
        clearLayerRows();
        const KisNodeSP active=m_vm->activeNode();
        int count=0;
        for(KisNodeSP node=image->root()->lastChild();node && count<100;
            node=node->prevSibling()) {
            if(node->inherits("KisReferenceImagesLayer"))continue;
            ++count;
            auto *frame=new QFrame(m_layersContent);
            const int rowWidth=m_layerTileSize;
            frame->setFixedSize(rowWidth+4,rowWidth+4);
            auto *tile=new QToolButton(frame);
            tile->setGeometry(1,1,rowWidth,rowWidth);
            tile->setToolButtonStyle(Qt::ToolButtonIconOnly);
            tile->setIconSize(QSize(rowWidth-12,rowWidth-12));
            QImage thumb=node->createThumbnail(rowWidth-12,rowWidth-12,
                                               Qt::KeepAspectRatio);
            if(!thumb.isNull())tile->setIcon(QIcon(QPixmap::fromImage(thumb)));
            tile->setText(node->name());
            tile->setToolTip(node->name());
            tile->setStyleSheet(node==active ?
                "QToolButton{border:3px solid #e86c79;border-radius:5px;"
                "background:#a4a4a4;}" :
                "QToolButton{border:2px solid #808080;border-radius:5px;"
                "background:#a4a4a4;}");
            auto *tileHold=new QTimer(tile);
            tileHold->setSingleShot(true);
            tileHold->setInterval(330);
            auto *held=new bool(false);
            connect(tile,&QObject::destroyed,this,[held]() {delete held;});
            connect(tile,&QToolButton::pressed,tile,[tileHold,held]() {
                *held=false;
                tileHold->start();
            });
            connect(tileHold,&QTimer::timeout,tile,[held]() {
                *held=true;
            });
            connect(tile,&QToolButton::released,tile,[this,tileHold,held,node]() {
                tileHold->stop();
                if(*held) QTimer::singleShot(0,this,[this,node]() {
                    m_vm->nodeManager()->slotUiActivatedNode(node);
                    openLayerActions(node);
                });
            });
            connect(tile,&QToolButton::clicked,this,[this,node,held]() {
                if(*held) {
                    *held=false;
                    return;
                }
                const bool wasActive=m_vm->activeNode()==node;
                m_vm->nodeManager()->slotUiActivatedNode(node);
                if(wasActive)openLayerActions(node);
                else QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
            auto *eye=new QToolButton(frame);
            const int overlaySize=qBound(20,rowWidth/4,38);
            const int iconX=rowWidth-overlaySize+1;
            const int iconGap=qMax(2,(rowWidth-3*overlaySize)/4);
            eye->setGeometry(iconX,iconGap,overlaySize,overlaySize);
            eye->setIconSize(QSize(overlaySize-5,overlaySize-5));
            eye->setIcon(KisIconUtils::loadIcon(
                node->visible()?QStringLiteral("visible"):
                                QStringLiteral("hidden")));
            eye->setText(node->visible()?QStringLiteral("◉"):QStringLiteral("○"));
            eye->setToolTip(i18n("Toggle layer visibility"));
            eye->setStyleSheet(node->visible() ?
                "QToolButton{border:none;background:#777;color:white;border-radius:5px;}" :
                "QToolButton{border:1px solid #ccc;background:#555;color:white;border-radius:5px;}");
            connect(eye,&QToolButton::clicked,this,[this,node]() {
                m_vm->nodeManager()->slotUiActivatedNode(node);
                m_vm->nodeManager()->toggleVisibility();
                QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
            auto *lock=new QToolButton(frame);
            lock->setGeometry(iconX,iconGap*2+overlaySize,
                              overlaySize,overlaySize);
            lock->setText(QString());
            lock->setIcon(lockIcon(node->userLocked()));
            lock->setIconSize(QSize(overlaySize-4,overlaySize-4));
            lock->setToolTip(i18n("Toggle layer lock"));
            lock->setStyleSheet(node->userLocked() ?
                "QToolButton{background:#417a89;border:1px solid white;border-radius:5px;}" :
                "QToolButton{background:#666;border:0;border-radius:5px;}");
            connect(lock,&QToolButton::clicked,this,[this,node]() {
                m_vm->nodeManager()->slotUiActivatedNode(node);
                m_vm->nodeManager()->toggleLock();
                QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
            if(auto *layer=qobject_cast<KisPaintLayer*>(node.data())) {
                auto *alpha=new QToolButton(frame);
                alpha->setGeometry(iconX,iconGap*3+overlaySize*2,
                                   overlaySize,overlaySize);
                alpha->setText(layer->alphaLocked()?QStringLiteral("α"):
                                                   QStringLiteral("α̸"));
                alpha->setToolTip(i18n("Toggle alpha lock"));
                alpha->setStyleSheet(layer->alphaLocked() ?
                    "QToolButton{background:#417a89;color:white;border:1px solid white;"
                    "border-radius:5px;font-weight:bold;}" :
                    "QToolButton{background:#666;color:white;border:0;"
                    "border-radius:5px;}");

                connect(alpha,&QToolButton::clicked,this,[this,node]() {
                    m_vm->nodeManager()->slotUiActivatedNode(node);
                    m_vm->nodeManager()->toggleAlphaLock();
                    QTimer::singleShot(0,this,[this](){refreshLayers();});
                });
            }
            m_layersLayout->addWidget(frame);
        }
        m_layersLayout->addStretch(1);
        m_visibleLayerCount=count;
        QTimer::singleShot(0,this,[this,oldScroll]() {
            if(m_layersScroll) m_layersScroll->verticalScrollBar()->setValue(oldScroll);
        });
        m_layerName->setText(active ? active->name().left(14):QString());
        positionOverlays();
    }
    void openLayerActions(KisNodeSP node) {
        if(!node)return;
        QDialog dialog(m_main);
        dialog.setWindowTitle(node->name());
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        dialog.resize(qMax(286,qMin(360,m_surface->width()-24)),
                      qMax(330,qMin(450,m_surface->height()-35)));
        auto *layout=new QVBoxLayout(&dialog);
        auto *blend=new KisCompositeOpComboBox(&dialog);
        if(node->compositeOp()) {
            blend->selectCompositeOp(KoCompositeOpRegistry::instance().getKoID(
                node->compositeOpId()));
        }
        layout->addWidget(new QLabel(i18n("Layer blending"),&dialog));
        layout->addWidget(blend);
        connect(blend,QOverload<int>::of(&QComboBox::activated),&dialog,
                [this,node,blend](int) {
            if(!node->colorSpace())return;
            const KoCompositeOp *op=node->colorSpace()->compositeOp(
                blend->selectedCompositeOp().id());
            if(op)m_vm->nodeManager()->setNodeCompositeOp(node,op);
        });
        auto *opacity=new QSlider(Qt::Horizontal,&dialog);
        opacity->setRange(0,100);
        opacity->setValue(node->percentOpacity());
        layout->addWidget(new QLabel(i18n("Opacity"),&dialog));
        layout->addWidget(opacity);
        connect(opacity,&QSlider::sliderReleased,&dialog,[this,node,opacity]() {
            m_vm->nodeManager()->setNodeOpacity(node,qRound(opacity->value()*255.0/100.0));
        });
        auto *grid=new QGridLayout();
        QToolButton *toggleButtons[3]={nullptr,nullptr,nullptr};
        auto refreshToggles=[node,&toggleButtons]() {
            const auto *paint=qobject_cast<const KisPaintLayer*>(node.data());
            if(toggleButtons[0]) {
                toggleButtons[0]->setText(node->visible()?i18n("Visible"):i18n("Hidden"));
                toggleButtons[0]->setStyleSheet(node->visible() ?
                    "QToolButton{border:2px solid #87b0c4;}" :
                    "QToolButton{border:2px solid #a0a0a0;}");
            }
            if(toggleButtons[1]) {
                toggleButtons[1]->setText(node->userLocked()?i18n("Unlock layer"):i18n("Lock layer"));
                toggleButtons[1]->setIcon(lockIcon(node->userLocked()));
                toggleButtons[1]->setStyleSheet(node->userLocked() ?
                    "QToolButton{border:2px solid #87b0c4;}" :
                    "QToolButton{border:2px solid #a0a0a0;}");
            }
            if(toggleButtons[2]) {
                const bool locked=paint && paint->alphaLocked();
                toggleButtons[2]->setText(locked?i18n("Unlock Alpha"):i18n("Lock Alpha"));
                toggleButtons[2]->setStyleSheet(locked ?
                    "QToolButton{border:2px solid #87b0c4;}" :
                    "QToolButton{border:2px solid #a0a0a0;}");
                toggleButtons[2]->setEnabled(paint!=nullptr);
            }
        };
        struct Command {const char *label;const char *action;};
        const Command commands[]={
            {"Hide","visibility"},{"Duplicate","duplicatelayer"},{"Clear","clear"},
            {"Lock","lock"},{"Merge","merge_layer"},{"Delete","remove_layer"},
            {"Clip","create_quick_clipping_group"},{"Select","select_opaque"},{"Mask","add_new_transparency_mask"},
            {"Alpha Lock","toggle_layer_alpha_lock"},{"More...",""}
        };
        for(int i=0;i<11;++i) {
            auto *btn=new QToolButton(&dialog);
            btn->setText(i18n(commands[i].label));
            btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            btn->setFixedSize(88,62);
            QAction *action=m_main->actionCollection()->action(QLatin1String(commands[i].action));
            if(action)btn->setIcon(action->icon());
            grid->addWidget(btn,i/3,i%3);
            if(i==0)toggleButtons[0]=btn;
            else if(i==3)toggleButtons[1]=btn;
            else if(i==9)toggleButtons[2]=btn;
            connect(btn,&QToolButton::clicked,&dialog,
                    [this,node,i,&dialog,refreshToggles]() {
                m_vm->nodeManager()->slotUiActivatedNode(node);
                if(i==0)m_vm->nodeManager()->toggleVisibility();
                else if(i==1)m_vm->nodeManager()->duplicateActiveNode();
                else if(i==3)m_vm->nodeManager()->toggleLock();
                else if(i==5)m_vm->nodeManager()->removeNode();
                else if(i==6)m_vm->nodeManager()->createQuickClippingGroup();
                else if(i==9)trigger(QStringLiteral("toggle_layer_alpha_lock"));
                else if(i==10) {
                    QMenu options(m_main);
                    auto addNative=[this](QMenu *menu,const QString &id) {
                        if(QAction *action=m_main->actionCollection()->action(id))
                            menu->addAction(action);
                    };
                    addNative(&options,QStringLiteral("layer_properties"));
                    addNative(&options,QStringLiteral("RenameCurrentLayer"));
                    addNative(&options,QStringLiteral("duplicatelayer"));
                    addNative(&options,QStringLiteral("merge_layer"));
                    addNative(&options,QStringLiteral("flatten_layer"));
                    addNative(&options,QStringLiteral("select_opaque"));
                    addNative(&options,QStringLiteral("remove_layer"));
                    QMenu *addMenu=options.addMenu(i18n("Add layer or mask"));
                    addNative(addMenu,QStringLiteral("add_new_paint_layer"));
                    addNative(addMenu,QStringLiteral("add_new_group_layer"));
                    addNative(addMenu,QStringLiteral("add_new_clone_layer"));
                    addNative(addMenu,QStringLiteral("add_new_transparency_mask"));
                    addNative(addMenu,QStringLiteral("add_new_filter_mask"));
                    QMenu *groupMenu=options.addMenu(i18n("Group and clipping"));
                    addNative(groupMenu,QStringLiteral("create_quick_clipping_group"));
                    addNative(groupMenu,QStringLiteral("quick_group"));
                    addNative(groupMenu,QStringLiteral("quick_ungroup"));
                    options.exec(QCursor::pos());
                }
                else if(i==7 && m_layerDocker) {
                    QMetaObject::invokeMethod(m_layerDocker, "slotSelectOpaque",
                                              Qt::DirectConnection);
                } else {
                    const QStringList actions={
                        QStringLiteral(""),QStringLiteral(""),QStringLiteral("clear"),
                        QStringLiteral(""),QStringLiteral("merge_layer"),QStringLiteral(""),
                        QStringLiteral(""),QStringLiteral(""),QStringLiteral("add_new_transparency_mask")};
                    trigger(actions.at(i));
                }
                if(i==0 || i==3 || i==9) {
                    refreshToggles();
                    QTimer::singleShot(0,this,[this](){refreshLayers();});
                } else {
                    dialog.accept();
                    QTimer::singleShot(0,this,[this](){refreshLayers();});
                }
            });
        }
        layout->addLayout(grid);
        refreshToggles();
        auto *done=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(done);
        connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
        dialog.exec();
    }
    void refreshColors() {
        auto *provider=m_vm->canvasResourceProvider();
        if(!provider)return;
        const QColor fg=provider->fgColor().toQColor();
        m_colorButton->setStyleSheet(QStringLiteral(
            "QToolButton{background:%1;border:3px solid #d8d8d8;border-radius:24px;}")
            .arg(fg.name()));
        while(m_recentLayout->count()) {
            QLayoutItem *item=m_recentLayout->takeAt(0);
            if(item->widget() && item->widget()!=m_colorPinButton)
                item->widget()->deleteLater();
            delete item;
        }
        const int columns=8;
        const int swatchSize=qMax(16,(m_colorWheelPixels-22)/columns-2);
        if(m_colorPinButton) {
            m_colorPinButton->setFixedSize(swatchSize,swatchSize);
            m_colorPinButton->setIconSize(QSize(swatchSize-3,swatchSize-3));
            m_recentLayout->addWidget(m_colorPinButton,0,7);
        }
        const QList<KoColor> colors=provider->colorHistory();
        const int maxColors=m_colorShowHistory ?
            qMin(7+columns*(m_colorHistoryRows-1),colors.size()) : 0;
        for(int i=0;i<maxColors;++i) {
            const QColor color=colors.at(i).toQColor();
            auto *swatch=new QToolButton(m_recent);
            swatch->setFixedSize(swatchSize,swatchSize);
            swatch->setStyleSheet(QStringLiteral(
                "QToolButton{background:%1;border:1px solid white;border-radius:3px;}")
                .arg(color.name()));
            swatch->setToolTip(color.name());
            connect(swatch,&QToolButton::clicked,this,[provider,color]() {
                provider->setFGColor(KoColor(color,provider->fgColor().colorSpace()));
            });
            const int row=i<7?0:1+(i-7)/columns;
            const int col=i<7?i:(i-7)%columns;
            m_recentLayout->addWidget(swatch,row,col);
        }
        m_recent->setFixedWidth(m_colorWheelPixels);
        m_recent->setFixedHeight(m_colorHistoryRows*(swatchSize+2)+6);
        if(m_colorDocker && m_compact)
            m_colorDocker->resize(m_colorWheelPixels,
                m_colorWheelPixels+24+m_recent->height());
        positionOverlays();
    }
    void openColorSettings() {
        QDialog dialog(m_main);
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        auto *layout=new QVBoxLayout(&dialog);
        auto *history=new QCheckBox(i18n("Show recent colors"),&dialog);
        history->setChecked(m_colorShowHistory);
        layout->addWidget(history);
        connect(history,&QCheckBox::toggled,&dialog,[this](bool enabled) {
            m_colorShowHistory=enabled;
            QSettings().setValue(QStringLiteral("TabletUi/colorHistoryEnabledV2"),enabled);
            updateColorDock();
            refreshColorControls();
        });
        layout->addWidget(new QLabel(i18n("Color history position"),&dialog));
        auto *historySide=new QComboBox(&dialog);
        historySide->addItem(i18n("Below color wheel"));
        historySide->addItem(i18n("Above color wheel"));
        historySide->setCurrentIndex(m_colorHistoryOnTop?1:0);
        layout->addWidget(historySide);
        connect(historySide,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[this](int index) {
            m_colorHistoryOnTop=index==1;
            QSettings().setValue(QStringLiteral("TabletUi/colorHistoryOnTop"),
                                 m_colorHistoryOnTop);
            positionOverlays();
        });
        layout->addWidget(new QLabel(i18n("Maximum history rows"),&dialog));
        auto *rows=new QSlider(Qt::Horizontal,&dialog);
        rows->setRange(1,4);
        rows->setValue(m_colorHistoryRows);
        layout->addWidget(rows);
        connect(rows,&QSlider::valueChanged,&dialog,[this](int value) {
            m_colorHistoryRows=value;
            QSettings().setValue(QStringLiteral("TabletUi/colorHistoryRows"),value);
            refreshColors();
        });
        auto *nativeSettings=new QPushButton(i18n("Krita Color Selector Options..."),&dialog);
        layout->addWidget(nativeSettings);
        connect(nativeSettings,&QPushButton::clicked,&dialog,[this]() {
            if(!m_colorDocker)return;
            if(m_nativeSelectorMenuButton && m_nativeSelectorMenuButton->menu()) {
                m_nativeSelectorMenuButton->menu()->exec(QCursor::pos());
                return;
            }
            for(QToolButton *button:m_colorDocker->findChildren<QToolButton*>()) {
                if(button->menu()) {
                    button->menu()->exec(QCursor::pos());
                    return;
                }
            }
            QMessageBox::information(m_main,i18n("Color selector"),
                i18n("Open Krita's selector menu from the selector widget."));
        });
        layout->addWidget(new QLabel(i18n("Color wheel size"),&dialog));
        auto *size=new QSlider(Qt::Horizontal,&dialog);
        size->setRange(190,380);
        size->setValue(m_colorWheelPixels);
        layout->addWidget(size);
        auto *value=new QLabel(QStringLiteral("%1 px").arg(m_colorWheelPixels),&dialog);
        layout->addWidget(value);
        connect(size,&QSlider::valueChanged,&dialog,[this,value](int pixels) {
            m_colorWheelPixels=pixels;
            value->setText(QStringLiteral("%1 px").arg(pixels));
            QSettings().setValue(QStringLiteral("TabletUi/colorWheelPixels"),pixels);
            if(m_colorDocker)m_colorDocker->resize(
                pixels,pixels+24+m_recent->height());
            updateColorDock();
            refreshColorControls();
        });
        dialog.setMinimumWidth(260);
        dialog.exec();
    }
    void toggleColor() {
        m_colorExpanded=!m_colorExpanded;
        QSettings().setValue(QStringLiteral("TabletUi/colorExpanded"),m_colorExpanded);
        updateColorDock();
        refreshColorControls();
    }
    void refreshColorControls() {
        const bool show=m_compact && m_vm->activeNode() && m_surface->isVisible();
        m_colorButton->setVisible(show && !m_colorExpanded);
        m_recent->setVisible(show && m_colorExpanded);
        if(m_colorPinButton) {
            m_colorPinButton->setText(QString());
            m_colorPinButton->setIcon(lockIcon(m_colorPinned));
            m_colorPinButton->setToolTip(m_colorPinned ?
                i18n("Unpin and collapse color wheel") :
                i18n("Pin color wheel to bottom-right corner"));
            m_colorPinButton->setVisible(show && m_colorExpanded);
            if(show && m_colorExpanded)m_colorPinButton->raise();
        }
    }
    void updateColorDock() {
        if(!m_colorDocker)return;
        if(!m_compact || !m_colorExpanded || !m_vm->activeNode()) {
            m_colorDocker->hide();
            return;
        }
        m_colorDocker->show();
        m_colorDocker->raise();
        const int bottomGap=8;
        const QPoint global=m_surface->mapToGlobal(
            QPoint(qMax(0,m_surface->width()-m_colorDocker->width()-8),
                   qMax(0,m_surface->height()-m_colorDocker->height()-bottomGap)));
        m_colorDocker->move(global);
        m_recent->setGeometry(0,
            m_colorHistoryOnTop ? 0 :
                qMax(0,m_colorDocker->height()-m_recent->height()),
            m_colorWheelPixels,m_recent->height());
        m_recent->show();
        m_recent->raise();
        if(m_colorPinButton)m_colorPinButton->show();
    }
    void positionOverlays() {
        if(!m_compact)return;
        const int w=m_surface->width();
        const int h=m_surface->height();
        const int layerX=m_layerOnRight?qMax(4,w-m_layerRail->width()-4):4;
        const int count=qMax(1,m_visibleLayerCount);
        const int rowsHeight=count*(m_layerTileSize+4)+(count-1)*3;
        const int desired=5+38+3+rowsHeight+3+34+5+12;
        const int maxHeight=qMax(140,h-125);
        m_layerRail->setGeometry(layerX,35,m_layerRail->width(),
                                 qMin(maxHeight,qMax(130,desired)));
        m_layerToggleButton->move(m_layerOnRight?qMax(6,w-54):6,42);
        m_brushStrip->adjustSize();
        m_brushStrip->move(8,qMax(0,h-m_brushStrip->height()-8));
        m_brushControls->move(8,qMax(0,h-m_brushStrip->height()-
                                      m_brushControls->height()-11));
        m_toolOptionsFloating->move(qMax(0,w-54),55);
        m_colorButton->move(qMax(0,w-58),qMax(0,h-56));

        m_layerRail->raise();
        m_layerToggleButton->raise();
        m_brushStrip->raise();
        m_brushControls->raise();
        m_toolOptionsFloating->raise();
        m_colorButton->raise();
        m_recent->raise();
        if(m_colorExpanded)updateColorDock();
    }
    void refreshVisibility() {
        const bool show=m_compact && m_vm->activeNode() && m_surface->isVisible();
        m_brushStrip->setVisible(show);
        m_brushControls->setVisible(show);
        m_toolOptionsFloating->setVisible(show);
        if(show)refreshBrushControls();
        m_colorButton->setVisible(show && !m_colorExpanded);
        m_recent->setVisible(show && m_colorExpanded);
        if(show) {
            refreshColors();
            paintSlotIcons();
            positionOverlays();
        }
        updateColorDock();
        refreshColorControls();
        refreshLayerControls();
    }

    QPointer<KisMainWindow> m_main;
    QPointer<QMdiArea> m_mdi;
    QPointer<QWidget> m_surface;
    QPointer<KisViewManager> m_vm;
    QPointer<QDockWidget> m_layerDocker;
    QPointer<QDockWidget> m_brushDocker;
    QPointer<QDockWidget> m_colorDocker;
    QPointer<QDockWidget> m_toolOptionsDocker;
    QByteArray m_previousWindowState;
    struct HiddenToolbarAction {
        QPointer<QToolBar> bar;
        QPointer<QAction> action;
        QPointer<QAction> before;
    };
    QList<HiddenToolbarAction> m_hiddenActions;
    bool m_startupCompact=false;
    bool m_recoverLegacyNormalLayout=false;
    bool m_compact=false;
    bool m_colorExpanded=false;
    bool m_colorPinned=false;
    QPointer<QToolButton> m_colorPinButton;
    QPointer<QToolButton> m_nativeSelectorMenuButton;
    QPointer<QWidget> m_nativeSelectorHeader;
    QMargins m_nativeColorMargins;
    int m_nativeColorSpacing=0;

    QDockWidget::DockWidgetFeatures m_previousColorFeatures=QDockWidget::DockWidgetFeatures();
    bool m_colorPinLongPressed=false;
    bool m_colorShowHistory=false;
    int m_colorWheelPixels=265;
    int m_colorHistoryRows=1;
    bool m_colorHistoryOnTop=false;
    bool m_internalPresetChange=false;
    bool m_slotReady=false;
    int m_slot=0;
    QString m_lastPreset[3];
    QFrame *m_brushStrip=nullptr;
    QFrame *m_brushControls=nullptr;
    QWidget *m_flowRow=nullptr;
    QSlider *m_brushSizeSlider=nullptr;
    QSlider *m_brushOpacitySlider=nullptr;
    QSlider *m_brushFlowSlider=nullptr;
    QLabel *m_brushSizeValue=nullptr;
    QLabel *m_brushOpacityValue=nullptr;
    QLabel *m_brushFlowValue=nullptr;
    QComboBox *m_brushComposite=nullptr;
    QTimer *m_brushControlTimer=nullptr;
    bool m_syncingBrushControls=false;
    QToolButton *m_brushButtons[3]={nullptr,nullptr,nullptr};
    QFrame *m_layerRail=nullptr;
    QToolButton *m_layerToggleButton=nullptr;
    QToolButton *m_layerPinButton=nullptr;
    bool m_layerPinLongPressed=false;
    bool m_layerPinned=true;
    bool m_layerExpanded=true;
    bool m_layerOnRight=false;
    int m_layerTileSize=64;
    int m_visibleLayerCount=1;
    QScrollArea *m_layersScroll=nullptr;
    QWidget *m_layersContent=nullptr;
    QVBoxLayout *m_layersLayout=nullptr;
    QToolButton *m_addLayer=nullptr;
    QTimer *m_addLayerHoldTimer=nullptr;
    bool m_addLayerLongPressed=false;
    QLabel *m_layerName=nullptr;
    QToolButton *m_colorButton=nullptr;

    QFrame *m_recent=nullptr;
    QGridLayout *m_recentLayout=nullptr;
    QToolBar *m_topBar=nullptr;
    QToolBar *m_compactBar=nullptr;
    QAction *m_customizeAction=nullptr;
    QAction *m_toolsAction=nullptr;
    QAction *m_sampleModeAction=nullptr;
    QToolButton *m_toolOptionsFloating=nullptr;
    QList<QPointer<QToolBar>> m_originalToolbars;
    QTimer *m_layerRefreshTimer=nullptr;
    KisResourceModel *m_presetModel=nullptr;
};

#endif // Q_OS_ANDROID
