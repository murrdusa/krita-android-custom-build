/*
 * TabletCompactSkin: experimental, reversible UI overlays for Krita Android.
 * This is a presentation layer, not a replacement for Krita's resource,
 * paint-op, node or canvas systems. No Q_OBJECT/moc is required.
 */
#pragma once

#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QApplication>
#include <QtAndroid>
#include <QCheckBox>
#include <QComboBox>
#include <QBoxLayout>
#include <QMap>
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
#include <QMenuBar>
#include <QStatusBar>
#include <QTabBar>
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
#include <KoToolBase.h>
#include <KoToolDocker.h>
#include <kis_canvas2.h>
#include <utility>
#include <initializer_list>
#include <QScrollBar>
#include <QRegularExpression>
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
#include "kis_slider_spin_box.h"
#include "kis_image_config.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_paint_layer.h"
#include "kis_group_layer.h"
#include "kis_mask.h"
#include "kis_layer_properties_icons.h"
#include <functional>
#include <QHash>
#include <QLineF>
#include "kis_base_node.h"
#include "kis_image.h"
#include "kis_icon_utils.h"
#include "widgets/kis_cmb_composite.h"
#include "widgets/kis_tool_options_popup.h"
#include <QScreen>
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
        if(m_main->menuBar())m_main->menuBar()->installEventFilter(this);
        if(m_main->statusBar())m_main->statusBar()->installEventFilter(this);
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
        m_brushControls->setFixedWidth(qBound(200,m_surface->width()/3,300));
        auto *controlLayout=new QVBoxLayout(m_brushControls);
        controlLayout->setContentsMargins(8,6,8,6);
        controlLayout->setSpacing(2);
        auto *header=new QHBoxLayout;
        auto *headerLabel=new QLabel(i18n("Brush controls"),m_brushControls);
        headerLabel->setStyleSheet(QStringLiteral("color:#e6e6e6;font-weight:bold;"));
        header->addWidget(headerLabel);
        header->addStretch(1);
        auto *controlSettings=new QToolButton(m_brushControls);
        controlSettings->setIcon(KisIconUtils::loadIcon("configure"));
        controlSettings->setIconSize(QSize(17,17));
        controlSettings->setFixedSize(29,26);
        controlSettings->setToolTip(i18n("Customize brush controls"));
        header->addWidget(controlSettings);
        controlLayout->addLayout(header);
        connect(controlSettings,&QToolButton::clicked,this,
                [this](){configureBrushControls();});
        // Use the same touch-friendly native Krita slider/spinbox widget
        // used in the standard brush toolbar (including its size curve).
        auto createSlider=[this,controlLayout](const QString &title,qreal minimum,
                                               qreal maximum,int decimals,
                                               KisDoubleSliderSpinBox **slider) {
            auto *row=new QWidget(m_brushControls);
            auto *layout=new QVBoxLayout(row);
            layout->setContentsMargins(0,0,0,0);
            layout->setSpacing(1);
            *slider=new KisDoubleSliderSpinBox(row);
            (*slider)->setRange(minimum,maximum,decimals);
            (*slider)->setPrefix(title+QStringLiteral(": "));
            (*slider)->setMinimumHeight(38);
            (*slider)->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            (*slider)->setBlockUpdateSignalOnDrag(false);
            layout->addWidget(*slider);
            controlLayout->addWidget(row);
            return row;
        };
        m_sizeRow=createSlider(i18n("Size"),0.01,
                KisImageConfig(true).maxBrushSize(),2,&m_brushSizeSlider);
        m_brushSizeSlider->setExponentRatio(3.0);
        m_brushSizeSlider->setSuffix(i18n(" px"));
        m_brushSizeSlider->setSingleStep(1);
        m_opacityRow=createSlider(i18n("Opacity"),0,100,0,&m_brushOpacitySlider);
        m_brushOpacitySlider->setSuffix(QStringLiteral("%"));
        m_brushOpacitySlider->setSingleStep(5);
        m_flowRow=createSlider(i18n("Flow"),0,100,0,&m_brushFlowSlider);
        m_brushFlowSlider->setSuffix(QStringLiteral("%"));
        m_brushFlowSlider->setSingleStep(5);
        m_compositeRow=new QWidget(m_brushControls);
        auto *blendRow=new QHBoxLayout(m_compositeRow);
        blendRow->setContentsMargins(0,0,0,0);
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
        controlLayout->addWidget(m_compositeRow);
        auto *provider=m_vm->canvasResourceProvider();
        connect(m_brushSizeSlider,QOverload<qreal>::of(
                &KisDoubleSliderSpinBox::valueChanged),
                this,[provider,this](qreal value) {
            if(!m_syncingBrushControls)provider->setSize(value);
        });
        connect(m_brushOpacitySlider,QOverload<qreal>::of(
                &KisDoubleSliderSpinBox::valueChanged),
                this,[provider,this](qreal value) {
            if(!m_syncingBrushControls)provider->setOpacity(value/100.0);
        });
        connect(m_brushFlowSlider,QOverload<qreal>::of(
                &KisDoubleSliderSpinBox::valueChanged),
                this,[provider,this](qreal value) {
            if(!m_syncingBrushControls)provider->setFlow(value/100.0);
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
        m_brushControlTimer->setInterval(650);
        connect(m_brushControlTimer,&QTimer::timeout,this,[this]() {
            refreshBrushControls();
            refreshCompactToolbarIconSize();
            // Do not mutate the MDI view mode/window flags here.
            // Pinning Selection Actions emits configChanged(), and repeatedly
            // reparenting/maximizing the canvas caused it to collapse.
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
        layerPinTimer->setInterval(holdMs());
        connect(m_layerPinButton,&QToolButton::pressed,this,[this,layerPinTimer]() {
            m_layerPinLongPressed=false;
            layerPinTimer->start(holdMs());
        });
        connect(layerPinTimer,&QTimer::timeout,this,[this]() {
            m_layerPinLongPressed=true;
            feedbackForHold();
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
        m_addLayerHoldTimer->setInterval(holdMs());
        connect(m_addLayer, &QToolButton::pressed, this, [this]() {
            m_addLayerLongPressed = false;
            m_addLayerHoldTimer->start(holdMs());
        });
        connect(m_addLayer, &QToolButton::released, this, [this]() {
            m_addLayerHoldTimer->stop();
        });
        connect(m_addLayerHoldTimer, &QTimer::timeout, this, [this]() {
            m_addLayerLongPressed = true;
            feedbackForHold();
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
        // The old LeftMouseButtonGesture stole pen/finger-generated mouse
        // release events from the layer tiles, preventing drag-to-reorder.
        // The tile gesture handler owns thumbnail touches; Qt's scroller
        // handles touches on empty scrollbar space only.
        m_layersScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,true);
        // Finger scrolling is handled by handleLayerTouch(). Do not
        // install a competing QScroller recognizer: once a long press
        // arms a drag, only edge autoscroll may change the scroll offset.
        m_layersContent = new QWidget(m_layersScroll);
        m_layersLayout = new QVBoxLayout(m_layersContent);
        m_layersLayout->setContentsMargins(0,0,0,0);
        m_layersLayout->setSpacing(3);
        m_layersScroll->setWidget(m_layersContent);
        // Coalesce scroll-triggered thumbnail loads. Building thumbnails for
        // offscreen layers is deferred until they enter the viewport.
        m_layerPreviewTimer=new QTimer(this);
        m_layerPreviewTimer->setSingleShot(true);
        connect(m_layerPreviewTimer,&QTimer::timeout,this,[this]() {
            refreshVisibleLayerThumbnails(false);
        });
        connect(m_layersScroll->verticalScrollBar(),&QScrollBar::valueChanged,
                this,[this](int) {
            if(m_compact)m_layerPreviewTimer->start(95);
        });
        m_layerDropMarker=new QFrame(m_layersContent);
        m_layerDropMarker->setFixedHeight(4);
        m_layerDropMarker->setStyleSheet(
            "QFrame{background:#ff6677;border-radius:2px;}");
        m_layerDropMarker->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_layerDropMarker->hide();
        m_layerDragGhost=new QLabel(m_surface);
        m_layerDragGhost->setFixedSize(67,67);
        m_layerDragGhost->setAlignment(Qt::AlignCenter);
        m_layerDragGhost->setStyleSheet(
            "QLabel{border:3px solid #f17a90;border-radius:7px;"
            "background:rgba(38,40,43,222);}");
        m_layerDragGhost->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_layerDragGhost->hide();
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
        connect(optionsAction, &QAction::triggered,this,
                [this]() {showCompactToolOptions();});
        QPixmap saveClosePixmap(32,32);
        saveClosePixmap.fill(Qt::transparent);
        {
            QPainter painter(&saveClosePixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(QColor(235,235,235),2));
            painter.setBrush(QColor(54,59,64));
            painter.drawRoundedRect(QRectF(4,3,23,25),3,3);
            painter.setBrush(QColor(220,220,220));
            painter.drawRect(QRectF(9,5,12,8));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(9,18,11,6));
            painter.setPen(QPen(QColor(245,113,112),3));
            painter.drawLine(QPointF(20,20),QPointF(29,29));
            painter.drawLine(QPointF(29,20),QPointF(20,29));
        }
        auto *saveClose = m_topBar->addAction(
            QIcon(saveClosePixmap), i18n("Save and Close"));
        saveClose->setToolTip(i18n("Save artwork and return to Krita home"));
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_save_and_close"),saveClose);
        connect(saveClose,&QAction::triggered,this,[this]() {
            // Krita's standard Close QAction is not named "file_close".
            // Call the exact implementation used by the document-tab X;
            // it handles unsaved work, cancellation and Save As correctly.
            if(m_mdi) {
                QMdiSubWindow *sub=m_mdi->currentSubWindow();
                if(!sub)sub=m_mdi->activeSubWindow();
                if(sub)sub->close();
            }
        });
        // Dedicated Compact UI toolbar: normal desktop toolbars are untouched.
        m_compactBar=new QToolBar(i18n("Compact UI shortcuts"),m_main);
        m_compactBar->setObjectName(QStringLiteral("tabletDedicatedCompactBar"));
        m_compactBar->setMovable(false);
        m_compactBar->setFloatable(false);
        const int compactIconSize=qBound(20,QSettings().value(
            QStringLiteral("TabletUi/compactToolbarIconSize"),26).toInt(),72);
        m_compactBar->setIconSize(QSize(compactIconSize,compactIconSize));
        m_compactBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
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
        // Always-second, nonremovable hub: settings, native Krita menus and
        // customizable user actions remain available without a menu bar.
        m_hubMenu=new QMenu(i18n("Compact UI menu"),m_main);
        m_hubMenu->menuAction()->setIcon(m_customizeAction->icon());
        m_hubMenu->menuAction()->setToolTip(
            i18n("Compact settings, Krita menus and quick actions"));
        m_main->actionCollection()->addAction(
            QStringLiteral("tablet_compact_hub"),m_hubMenu->menuAction());
        connect(m_hubMenu,&QMenu::aboutToShow,this,[this]() {
            m_hubMenu->clear();
            auto *collection=m_main->actionCollection();
            m_hubMenu->addSection(i18n("Compact UI settings"));
            // Keep Tablet Controls and toolbar customization in this
            // always-present settings group, not in optional quick actions.
            QAction *tabletSettings=m_hubMenu->addAction(i18n("Tablet Controls..."));
            tabletSettings->setIcon(KisIconUtils::loadIcon("configure"));
            connect(tabletSettings,&QAction::triggered,this,[this]() {
                if(QAction *native=m_main->actionCollection()->action(
                        QStringLiteral("tablet_settings")))
                    native->trigger();
            });
            m_hubMenu->addAction(m_customizeAction);
            QAction *brush=m_hubMenu->addAction(i18n("Brush controls..."));
            connect(brush,&QAction::triggered,this,
                    [this](){configureBrushControls();});
            QAction *layers=m_hubMenu->addAction(i18n("Layers display..."));
            connect(layers,&QAction::triggered,this,
                    [this](){openLayerSettings();});
            QAction *color=m_hubMenu->addAction(i18n("Color selector settings..."));
            connect(color,&QAction::triggered,this,
                    [this](){openColorSettings();});
            QMenu *nativeMenus=m_hubMenu->addMenu(i18n("Krita menus"));
            nativeMenus->setIcon(KisIconUtils::loadIcon("application-menu"));
            for(QAction *topLevel:m_main->menuBar()->actions()) {
                if(topLevel && topLevel->menu())
                    nativeMenus->addMenu(topLevel->menu());
            }
            m_hubMenu->addSection(i18n("Quick actions"));
            const QStringList defaults={
                QStringLiteral("gradients"),QStringLiteral("patterns"),
                QStringLiteral("duplicatelayer"),QStringLiteral("quick_group")};
            const QStringList selected=QSettings().value(
                QStringLiteral("TabletUi/quickActionsV2"),defaults).toStringList();
            for(const QString &key:selected) {
                if(key==QStringLiteral("tablet_compact_ui") ||
                   key==QStringLiteral("tablet_quick_actions") ||
                   key==QStringLiteral("tablet_settings"))continue;
                if(QAction *a=collection->action(key))m_hubMenu->addAction(a);
            }
            // The existing submenu contains the native Quick Actions editor.
            if(QAction *legacy=collection->action(
                    QStringLiteral("tablet_quick_actions"))) {
                if(legacy->menu()) {
                    m_hubMenu->addSeparator();
                    m_hubMenu->addMenu(legacy->menu());
                }
            }
        });
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
        m_compactToolPopup=new KisToolOptionsPopup(m_main);
        m_compactToolPopup->setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        m_compactToolPopup->hide();
        connect(KoToolManager::instance(),
                &KoToolManager::toolOptionWidgetsChanged,this,
                [this](KoCanvasController *controller,
                       const QList<QPointer<QWidget>> &widgets) {
            if(controller!=KoToolManager::instance()->activeCanvasController())
                return;
            if(m_compact && m_compactToolPopup)
                m_compactToolPopup->newOptionWidgets(widgets);
        });
        connect(m_toolOptionsFloating,&QToolButton::clicked,this,
                [this]() {showCompactToolOptions();});
        QTimer::singleShot(0,this,[this]() {
            rebuildCompactToolbar();
            refreshVisibility();
        });
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
            QTimer::singleShot(0,this,[this]() {
                enforceCompactChrome();refreshVisibility();refreshLayers();
            });
            // View-manager activation may occur after QMdiArea emits this
            // signal, especially when a document is still being loaded.
            QTimer::singleShot(200,this,[this]() {refreshVisibility();});
        });
        connect(m_vm->canvasResourceProvider(), &KisCanvasResourceProvider::sigFGColorChanged,
                this, [this](const KoColor&) {if(m_compact) refreshColors();});

        // Krita/Qt may retarget pen TabletMove events to a viewport even
        // though QToolButton received the initial synthesized press.
        qApp->installEventFilter(this);
        m_layerEdgeScrollTimer=new QTimer(this);
        m_layerEdgeScrollTimer->setInterval(35);
        connect(m_layerEdgeScrollTimer,&QTimer::timeout,this,[this]() {
            if(!m_compact || !m_layersScroll || !m_layerRail)return;
            const bool finger=m_touchLayerArmed && !m_touchLayerPinching &&
                !m_touchLayerScrolling && !m_layerTouches.isEmpty();
            const bool pen=m_layerDragArmed && bool(m_layerDragNode);
            if(!finger && !pen){m_layerEdgeScrollTimer->stop();return;}
            const QPoint pos=finger?m_touchLayerLast:m_layerDragLast;
            const QRect bounds(m_layersScroll->viewport()->mapToGlobal(QPoint()),
                               m_layersScroll->viewport()->size());
            if(pos.x()<bounds.left()-24 || pos.x()>bounds.right()+24)return;
            const int edge=qMin(52,qMax(28,bounds.height()/7));
            int delta=0;
            if(pos.y()<=bounds.top()+edge)
                delta=-qBound(5,(bounds.top()+edge-pos.y())/2+5,24);
            else if(pos.y()>=bounds.bottom()-edge)
                delta=qBound(5,(pos.y()-bounds.bottom()+edge)/2+5,24);
            if(delta) {
                QScrollBar *bar=m_layersScroll->verticalScrollBar();
                bar->setValue(bar->value()+delta);
                showLayerDragPreview(finger?m_touchLayerPrimary:m_layerDragNode,pos);
            }
        });
        m_layerTouchHoldTimer=new QTimer(this);
        m_layerTouchHoldTimer->setSingleShot(true);
        connect(m_layerTouchHoldTimer,&QTimer::timeout,this,[this]() {
            if(m_layerTouches.size()==1 && !m_touchLayerScrolling &&
               !m_touchLayerPinching) {
                m_touchLayerArmed=true;
                suspendLayerScroller();
                if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->start();
                showLayerDragPreview(m_touchLayerPrimary,m_touchLayerLast);
                feedbackForHold();
            }
        });
        m_layerRefreshTimer = new QTimer(this);
        m_layerRefreshTimer->setInterval(3200);
        connect(m_layerRefreshTimer, &QTimer::timeout,this,[this]() {
            if(!m_compact || !m_vm->activeNode())return;
            // The model comparison is cheap when nothing structural changed.
            // Only the active visible painting thumbnail needs a live refresh.
            refreshLayers();
            refreshVisibleLayerThumbnails(true);
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
            QStringLiteral("TabletUi/colorHistoryEnabledV2"),true).toBool();
        m_colorPinned=QSettings().value(QStringLiteral("TabletUi/colorPinned"),false).toBool();
        // Only explicit pinning keeps the selector open after restart.
        m_colorExpanded=m_colorPinned;
        QTimer::singleShot(0,this,[this](){organizeDefaultToolbars();});
        QTimer::singleShot(450,this,[this](){organizeDefaultToolbars();});
    }

    bool eventFilter(QObject *watched,QEvent *event) override {
        // Handle native multi-touch before Qt can translate it to an ordinary
        // click or feed it to the layer scroller. This supports both finger
        // reorder and pinching two separate layer thumbnails into a group.
        if(handleLayerTouch(watched,event))return true;
        // Native Android stylus events don't reliably synthesize QToolButton
        // press/move/release. Start the hold directly from TabletPress.
        if(m_compact && !m_clipTransactionBusy && event->type()==QEvent::TabletPress &&
           m_layerRail && m_layerRail->isVisible()) {
            QTabletEvent *pen=static_cast<QTabletEvent*>(event);
            const QPoint p=pen->globalPos();
            const CompactLayerRow *row=touchLayerAt(p);
            if(row && row->tile) {
                m_layerPenTracking=true;
                m_layerDragNode=row->node;
                m_layerDragTile=row->tile;
                m_layerDragStart=p;
                m_layerDragLast=p;
                m_layerDragArmed=false;
                m_layerDragMoved=false;
                const KisNodeSP heldNode=row->node;
                QTimer::singleShot(holdMs(),this,[this,heldNode]() {
                    if(!m_layerPenTracking || m_layerDragNode!=heldNode)
                        return;
                    m_layerDragArmed=true;
                    if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->start();
                    suspendLayerScroller();
                    showLayerDragPreview(heldNode,m_layerDragLast);
                    feedbackForHold();
                });
            }
        }
        // Track native stylus packets even if the pen leaves the thumbnail
        // or Qt delivers them to the canvas viewport instead of the tile.
        const bool penPacket=event->type()==QEvent::TabletMove ||
                             event->type()==QEvent::TabletRelease;
        if(m_compact && m_layerDragNode && penPacket) {
            QTabletEvent *tablet=static_cast<QTabletEvent*>(event);
            m_layerDragLast=tablet->globalPos();
            if(m_layerDragArmed &&
               (m_layerDragLast-m_layerDragStart).manhattanLength()>16) {
                m_layerDragMoved=true;
                showLayerDragPreview(m_layerDragNode,m_layerDragLast);
            }
            // Complete pen gestures at TabletRelease rather than waiting for
            // a synthesized Qt mouse release that Android may never send.
            if(event->type()==QEvent::TabletRelease && m_layerPenTracking) {
                const KisNodeSP source=m_layerDragNode;
                const QPoint last=m_layerDragLast;
                const bool moved=m_layerDragArmed && m_layerDragMoved;
                const bool held=m_layerDragArmed;
                finishLayerDrag();
                if(moved)moveLayerAtFinger(source,last);
                else if(held) {
                    m_vm->nodeManager()->slotUiActivatedNode(source);
                    openLayerActions(source);
                } else m_vm->nodeManager()->slotUiActivatedNode(source);
            }
        }
        if(watched==m_layerDragTile && m_layerDragNode &&
           (event->type()==QEvent::MouseMove ||
            event->type()==QEvent::MouseButtonRelease)) {
            const QPoint global=static_cast<QMouseEvent*>(event)->globalPos();
            m_layerDragLast=global;
            if(m_layerDragArmed &&
               (global-m_layerDragStart).manhattanLength()>16) {
                m_layerDragMoved=true;
                showLayerDragPreview(m_layerDragNode,global);
            }
        }
        // Krita's startup/welcome-page layout may re-show menu/status bars
        // after the restored Compact UI action fires. Keep chrome hidden
        // without altering the stored normal workspace visibility.
        if(m_compact && event->type()==QEvent::Show &&
           (watched==m_main->menuBar() || watched==m_main->statusBar())) {
            QPointer<QWidget> chrome=qobject_cast<QWidget*>(watched);
            QTimer::singleShot(0,this,[this,chrome]() {
                if(m_compact && chrome)chrome->hide();
            });
        }
        // Dockers are normal-Krita widgets, not Compact UI overlays. Some
        // plugins re-show their dockers after document/selection changes.
        if(m_compact && event->type()==QEvent::Show) {
            if(QDockWidget *dock=qobject_cast<QDockWidget*>(watched)) {
                if(dock!=m_colorDocker) {
                    QPointer<QDockWidget> guard(dock);
                    QTimer::singleShot(0,this,[this,guard]() {
                        if(m_compact && guard && guard!=m_colorDocker)
                            guard->hide();
                    });
                }
            }
        }
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
            // Record the actual normal-mode chrome, not its hidden compact
            // state. restoreState() does not restore menu/status bar visibility.
            if(!m_startupCompact) {
                m_normalMenuVisible=m_main->menuBar() &&
                    m_main->menuBar()->isVisible();
                m_normalStatusVisible=m_main->statusBar() &&
                    m_main->statusBar()->isVisible();
            }
            // Snapshot the real Krita workspace before hiding a single toolbar.
            rememberDockers();
            rememberCompactToolbarState(true);
            enforceCompactChrome();
            // One deferred pass is enough for Qt's asynchronous dock/MDI
            // changes. The immediate pass happens at the end of this method.
            QTimer::singleShot(420,this,[this]() {
                if(m_compact) {
                    enforceCompactChrome();
                    refreshVisibility();
                }
            });
            const QByteArray storedNormal=QSettings().value(
                QStringLiteral("TabletUi/normalWindowStateV2")).toByteArray();
            if(m_startupCompact && !storedNormal.isEmpty()) {
                m_previousWindowState=storedNormal;
            } else if(m_startupCompact) {
                // An older install may have saved an already-compact layout.
                // No reliable normal snapshot exists yet; recover visible UI
                // on first exit, and save a clean baseline on next entry.
                m_recoverLegacyNormalLayout=true;
                m_previousWindowState.clear();
            } else if(!m_previousWindowState.isEmpty()) {
                QSettings().setValue(QStringLiteral("TabletUi/normalWindowStateV2"),
                                     m_previousWindowState);
            }
            m_startupCompact=false;
            // Restore pinned panels only, not transient open panels.
            m_colorExpanded=m_colorPinned;
            m_layerExpanded=m_layerPinned;
            // Hide the entire native docker set: Toolbox, Compositions,
            // undo/history, extra selectors, etc. The single color docker
            // reused as the floating Compact wheel is handled separately.
            for(const QPointer<QDockWidget> &dock:m_compactNativeDockers) {
                if(dock && dock!=m_colorDocker) dock->hide();
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
                if(QWidget *selector=m_colorDocker->widget()) {
                    if(m_recent->parentWidget()!=selector)
                        m_recent->setParent(selector);
                    if(auto *layout=dynamic_cast<QBoxLayout*>(selector->layout()))
                        layout->addWidget(m_recent);
                }
                const int wheelWidth=m_colorWheelPixels;
                m_colorDocker->resize(QSize(wheelWidth,
                    wheelWidth+8+m_recent->height()));
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
                    pinTimer->setInterval(holdMs());
                    connect(m_colorPinButton,&QToolButton::pressed,this,
                            [this,pinTimer]() {
                        m_colorPinLongPressed=false;
                        pinTimer->start(holdMs());
                    });
                    connect(pinTimer,&QTimer::timeout,this,[this]() {
                        m_colorPinLongPressed=true;
                        feedbackForHold();
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
            restoreLayerScroller();
            hideLayerDragPreview();
            if(m_compactToolPopup)m_compactToolPopup->hide();
            // Reparent the actual option widgets to the normal native
            // container; requesting activation of the same tool is a no-op.
            QTimer::singleShot(0,this,[this]() {restoreNormalToolOptions();});
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
                if(QWidget *selector=m_colorDocker->widget()) {
                    if(QLayout *layout=selector->layout())
                        layout->removeWidget(m_recent);
                }
                m_recent->hide();
                m_recent->setParent(m_surface);
                if(m_nativeSelectorMenuButton)m_nativeSelectorMenuButton->show();
            }
            if(!m_previousWindowState.isEmpty())
                m_main->restoreState(m_previousWindowState);
            // restoreState() also restores toolbar visibility. Do the
            // normal-bar restore *after* it, keeping Compact shortcuts hidden.
            rememberCompactToolbarState(false);
            if(m_main->menuBar() && m_normalMenuVisible)
                m_main->menuBar()->show();
            if(m_main->statusBar() && m_normalStatusVisible)
                m_main->statusBar()->show();
            // Delayed workspace restoration sometimes runs after the toggle;
            // reassert normal-mode chrome without touching user preferences.
            QTimer::singleShot(120,this,[this]() {
                if(m_compact)return;
                if(m_main->menuBar() && m_normalMenuVisible)
                    m_main->menuBar()->show();
                if(m_main->statusBar() && m_normalStatusVisible)
                    m_main->statusBar()->show();
                for(QToolBar *bar:m_main->findChildren<QToolBar*>())
                    if(bar && bar->objectName()==QStringLiteral("mainToolBar"))
                        bar->show();
            });
            organizeDefaultToolbars();
            if(m_recoverLegacyNormalLayout) {
                for(QDockWidget *dock : {m_layerDocker.data(),m_brushDocker.data(),
                                        m_toolOptionsDocker.data(),m_colorDocker.data()}) {
                    if(dock)dock->show();
                }
                // Legacy recovery cannot recover hidden toolbar preferences.
                // Only expose the normal primary toolbar (with the mode toggle).
                for(QToolBar *bar : m_main->findChildren<QToolBar*>()) {
                    if(bar->objectName()==QStringLiteral("mainToolBar"))
                        bar->show();
                }
                m_recoverLegacyNormalLayout=false;
            }
        }
        refreshVisibility();
        refreshLayers();
        QTimer::singleShot(0,this,[this](){refreshNativeColorSelectorLayout();});
        if(!active) {
            // Krita may apply deferred toolbar state after the mode changes.
            // Reassert only the normal toolbar's Compact toggle, not its
            // other user-configured actions or the rest of the workspace.
            QTimer::singleShot(0,this,[this](){organizeDefaultToolbars();});
            QTimer::singleShot(250,this,[this](){organizeDefaultToolbars();});
        }
    }

private:
    KoToolBase *activeCompactTool() const {
        if(!m_vm || !m_vm->canvasBase())return nullptr;
        KoToolManager *tools=KoToolManager::instance();
        return tools->toolById(m_vm->canvasBase(),tools->activeToolId());
    }
    void restoreNormalToolOptions() {
        KoToolBase *tool=activeCompactTool();
        if(!tool)return;
        const QList<QPointer<QWidget>> widgets=tool->optionWidgets();
        if(auto *native=qobject_cast<KoToolDocker*>(m_toolOptionsDocker.data()))
            native->setOptionWidgets(widgets);
        else if(m_vm && m_vm->paintOpBox())
            m_vm->paintOpBox()->newOptionWidgets(widgets);
    }
    void showCompactToolOptions() {
        if(!m_compact || !m_vm->activeNode() || !m_compactToolPopup)return;
        // requestToolActivation() returns early when this tool is already
        // selected. Read its live option widgets instead; this also works
        // when normal Krita uses the Tool Options docker.
        if(KoToolBase *tool=activeCompactTool())
            m_compactToolPopup->newOptionWidgets(tool->optionWidgets());
        QTimer::singleShot(0,this,[this]() {
            if(!m_compact || !m_compactToolPopup)return;
            m_compactToolPopup->adjustSize();
            const QPoint anchor=m_toolOptionsFloating->mapToGlobal(
                QPoint(0,m_toolOptionsFloating->height()+4));
            const QRect available=m_main->screen() ?
                m_main->screen()->availableGeometry():QRect(0,0,1000,1600);
            const QSize sz=m_compactToolPopup->sizeHint().boundedTo(
                QSize(qMax(260,available.width()-24),
                      qMax(220,available.height()-60)));
            m_compactToolPopup->resize(sz);
            m_compactToolPopup->move(
                qBound(available.left()+4,anchor.x(),
                       qMax(available.left()+4,available.right()-sz.width()-4)),
                qBound(available.top()+4,anchor.y(),
                       qMax(available.top()+4,available.bottom()-sz.height()-4)));
            m_compactToolPopup->show();
            m_compactToolPopup->raise();
        });
    }
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
        m_compactNativeDockers.clear();
        for (QDockWidget *dock : m_main->findChildren<QDockWidget*>()) {
            if(!dock)continue;
            m_compactNativeDockers.append(dock);
            dock->removeEventFilter(this);
            dock->installEventFilter(this);
            const QString name=dock->objectName();
            if(name==QStringLiteral("KisLayerBox"))m_layerDocker=dock;
            if(name==QStringLiteral("sharedtooldocker"))m_toolOptionsDocker=dock;
            if(name.contains(QStringLiteral("BrushPreset"),Qt::CaseInsensitive) ||
               dock->windowTitle().contains(i18n("Brush Presets"),Qt::CaseInsensitive))
                m_brushDocker=dock;
            const bool wideGamut=name.contains(QStringLiteral("WideGamut"),
                                               Qt::CaseInsensitive) ||
                dock->windowTitle().contains(QStringLiteral("Wide Gamut Color Selector"),
                                             Qt::CaseInsensitive);
            const bool isColor=name.contains(QStringLiteral("ColorSelector"),
                                            Qt::CaseInsensitive) ||
                dock->windowTitle().contains(i18n("Advanced Color Selector"),
                                             Qt::CaseInsensitive);
            if(wideGamut || (isColor && !m_colorDocker))
                m_colorDocker=dock;
        }
    }
    int holdMs() const {
        return qBound(160,QSettings().value(
            QStringLiteral("TabletUi/longPressMs"),260).toInt(),600);
    }
    void feedbackForHold() const {
        if(QSettings().value(QStringLiteral("TabletUi/hapticLongPress"),false).toBool())
            QAndroidJniObject::callStaticMethod<void>(
                "org/krita/android/MainActivity","vibrateHold","()V");
    }
    void configureBrushControls() {
        QDialog dialog(m_main);
        dialog.setWindowTitle(i18n("Brush Control Settings"));
        auto *layout=new QVBoxLayout(&dialog);
        QStringList names={i18n("Size"),i18n("Opacity"),
                           i18n("Flow (when supported)"),i18n("Blend mode")};
        QStringList keys={QStringLiteral("size"),QStringLiteral("opacity"),
                          QStringLiteral("flow"),QStringLiteral("composite")};
        QList<QCheckBox*> toggles;
        for(int i=0;i<keys.size();++i) {
            auto *checkbox=new QCheckBox(names.at(i),&dialog);
            checkbox->setChecked(QSettings().value(
                QStringLiteral("TabletUi/brushControl/")+keys.at(i),true).toBool());
            layout->addWidget(checkbox);
            toggles.append(checkbox);
        }
        auto *buttons=new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        for(int i=0;i<keys.size();++i) {
            m_brushControlEnabled[i]=toggles.at(i)->isChecked();
            QSettings().setValue(
                QStringLiteral("TabletUi/brushControl/")+keys.at(i),
                m_brushControlEnabled[i]);
        }
        refreshBrushControls();
        positionOverlays();
    }
    void refreshBrushControls() {
        if(!m_compact || !m_vm->activeNode())return;
        auto *provider=m_vm->canvasResourceProvider();
        if(!provider)return;
        m_syncingBrushControls=true;
        m_brushSizeSlider->setValue(provider->size());
        m_brushOpacitySlider->setValue(provider->opacity()*100.0);
        m_brushFlowSlider->setValue(provider->flow()*100.0);
        bool hasFlow=false;
        const auto preset=provider->currentPreset();
        if(preset && preset->settings())
            hasFlow=preset->settings()->hasProperty(QStringLiteral("FlowValue"));
        if(!m_brushPrefsLoaded) {
            const QStringList keys={QStringLiteral("size"),
                QStringLiteral("opacity"),QStringLiteral("flow"),
                QStringLiteral("composite")};
            for(int i=0;i<4;++i)
                m_brushControlEnabled[i]=QSettings().value(
                    QStringLiteral("TabletUi/brushControl/")+keys.at(i),
                    true).toBool();
            m_brushPrefsLoaded=true;
        }
        const bool sizeVisible=m_sizeRow->isVisible();
        const bool opacityVisible=m_opacityRow->isVisible();
        const bool flowVisible=m_flowRow->isVisible();
        const bool compositeVisible=m_compositeRow->isVisible();
        m_sizeRow->setVisible(m_brushControlEnabled[0]);
        m_opacityRow->setVisible(m_brushControlEnabled[1]);
        m_flowRow->setVisible(hasFlow && m_brushControlEnabled[2]);
        m_compositeRow->setVisible(m_brushControlEnabled[3]);
        const int index=m_brushComposite->findData(provider->currentCompositeOp());
        if(index>=0)m_brushComposite->setCurrentIndex(index);
        m_syncingBrushControls=false;
        if(sizeVisible!=m_sizeRow->isVisible() ||
           opacityVisible!=m_opacityRow->isVisible() ||
           flowVisible!=m_flowRow->isVisible() ||
           compositeVisible!=m_compositeRow->isVisible())
            m_brushControls->adjustSize();
    }
    void refreshCompactToolbarIconSize() {
        if(!m_compactBar)return;
        const int px=qBound(20,QSettings().value(
            QStringLiteral("TabletUi/compactToolbarIconSize"),26).toInt(),72);
        const QSize square(px,px);
        if(m_compactBar->iconSize()!=square)
            m_compactBar->setIconSize(square);
    }
    void enforceCompactChrome() {
        if(!m_compact)return;
        if(m_main->menuBar())m_main->menuBar()->hide();
        if(m_main->statusBar())m_main->statusBar()->hide();
        if(m_mdi && m_mdi->viewMode()==QMdiArea::SubWindowView) {
            for(QTabBar *tabs:m_mdi->findChildren<QTabBar*>())
                tabs->hide();
        }
    }
    void rememberCompactToolbarState(bool compactMode) {
        if(compactMode) {
            m_originalToolbars.clear();
            for(QToolBar *bar:m_main->findChildren<QToolBar*>()) {
                if(!bar || bar==m_compactBar || bar==m_topBar)continue;
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
            // Even older broken Compact snapshots must never strand the user.
            for(QToolBar *bar:m_main->findChildren<QToolBar*>()) {
                if(bar && bar->objectName()==QStringLiteral("mainToolBar")) {
                    bar->show();
                    break;
                }
            }
        }
    }
    QStringList compactDefaultActions() const {
        return {QStringLiteral("tablet_save_and_close"),
                QStringLiteral("tablet_all_brush_presets"),
                QStringLiteral("tablet_sample_all_layers"),
                QStringLiteral("view_toggle_reference_images"),
                QStringLiteral("erase_action"),
                QStringLiteral("tablet_all_tools_popup")};
    }
    void rebuildCompactToolbar() {
        if(!m_compactBar)return;
        refreshCompactToolbarIconSize();
        m_compactBar->clear();
        auto *collection=m_main->actionCollection();
        QAction *compact=collection->action(QStringLiteral("tablet_compact_ui"));
        if(compact)m_compactBar->addAction(compact);
        if(m_hubMenu)m_compactBar->addAction(m_hubMenu->menuAction());
        const QStringList chosen=QSettings().value(
            QStringLiteral("TabletUi/compactActionOrderV1"),
            compactDefaultActions()).toStringList();
        for(const QString &id:chosen) {
            if(id==QStringLiteral("tablet_compact_ui") ||
               id==QStringLiteral("tablet_customize_compact") ||
               id==QStringLiteral("tablet_compact_hub") ||
               id==QStringLiteral("tablet_quick_actions"))continue;
            if(QAction *action=collection->action(id))
                m_compactBar->addAction(action);
        }
        if(m_hubMenu) {
            if(QToolButton *button=qobject_cast<QToolButton*>(
                    m_compactBar->widgetForAction(m_hubMenu->menuAction()))) {
                button->setPopupMode(QToolButton::InstantPopup);
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            }
        }
    }
    void configureCompactToolbar() {
        QDialog dialog(m_main);
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setWindowTitle(i18n("Customize Compact UI Toolbar"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *tabs=new QTabWidget(&dialog);
        layout->addWidget(tabs);
        // Common drawing actions: file, history, brushes, selections,
        // layer operations, transforms, navigation and canvas controls.
        // Unavailable actions are naturally omitted for this Krita build.
        const QStringList frequent={
            QStringLiteral("tablet_save_and_close"),
            QStringLiteral("tablet_all_brush_presets"),
            QStringLiteral("tablet_all_tools_popup"),
            QStringLiteral("tablet_sample_all_layers"),
            QStringLiteral("view_toggle_reference_images"),
            QStringLiteral("erase_action"),
            QStringLiteral("tablet_settings"),
            QStringLiteral("file_new"),
            QStringLiteral("file_open"),
            QStringLiteral("file_save"),
            QStringLiteral("file_save_as"),
            QStringLiteral("file_export_file"),
            QStringLiteral("file_close"),
            QStringLiteral("edit_undo"),
            QStringLiteral("edit_redo"),
            QStringLiteral("edit_cut"),
            QStringLiteral("edit_copy"),
            QStringLiteral("edit_paste"),
            QStringLiteral("copy_merged"),
            QStringLiteral("clear"),
            QStringLiteral("fill_selection_foreground_color"),
            QStringLiteral("fill_selection_background_color"),
            QStringLiteral("select_all"),
            QStringLiteral("deselect"),
            QStringLiteral("reselect"),
            QStringLiteral("invert_selection"),
            QStringLiteral("feather"),
            QStringLiteral("similar"),
            QStringLiteral("toggle_display_selection"),
            QStringLiteral("add_new_paint_layer"),
            QStringLiteral("add_new_group_layer"),
            QStringLiteral("duplicatelayer"),
            QStringLiteral("merge_layer"),
            QStringLiteral("merge_selected_layers"),
            QStringLiteral("flatten_image"),
            QStringLiteral("new_from_visible"),
            QStringLiteral("create_quick_group"),
            QStringLiteral("create_quick_clipping_group"),
            QStringLiteral("quick_ungroup"),
            QStringLiteral("layer_style"),
            QStringLiteral("mirrorNodeX"),
            QStringLiteral("mirrorNodeY"),
            QStringLiteral("imagesize"),
            QStringLiteral("canvassize"),
            QStringLiteral("rotate_canvas_left"),
            QStringLiteral("rotate_canvas_right"),
            QStringLiteral("reset_canvas_rotation"),
            QStringLiteral("mirror_canvas"),
            QStringLiteral("view_zoom_in"),
            QStringLiteral("view_zoom_out"),
            QStringLiteral("zoom_to_100pct"),
            QStringLiteral("zoom_to_fit"),
            QStringLiteral("wrap_around_mode"),
            QStringLiteral("view_show_canvas_only"),
            QStringLiteral("view_grid"),
            QStringLiteral("view_toggle_painting_assistants"),
            QStringLiteral("view_toggle_assistant_previews"),
            QStringLiteral("sample_screen_color")
        };
        const QStringList original=QSettings().value(
            QStringLiteral("TabletUi/compactActionOrderV1"),
            compactDefaultActions()).toStringList();
        auto *favorites=new QListWidget(tabs);
        // Finger drag scroll; use explicit move buttons to reorder instead
        // of stealing swipe gestures for item drag-and-drop.
        favorites->setDragDropMode(QAbstractItemView::NoDragDrop);
        favorites->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        favorites->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QScroller::grabGesture(favorites->viewport(),
                               QScroller::LeftMouseButtonGesture);
        tabs->addTab(favorites,i18n("Frequently used"));
        auto *moveBar=new QHBoxLayout;
        auto *upButton=new QPushButton(i18n("Move up"),&dialog);
        auto *downButton=new QPushButton(i18n("Move down"),&dialog);
        moveBar->addWidget(upButton);
        moveBar->addWidget(downButton);
        layout->addLayout(moveBar);
        auto moveSelected=[favorites](int direction) {
            const int row=favorites->currentRow();
            const int destination=row+direction;
            if(row<0 || destination<0 || destination>=favorites->count())return;
            auto *item=favorites->takeItem(row);
            favorites->insertItem(destination,item);
            favorites->setCurrentRow(destination);
        };
        connect(upButton,&QPushButton::clicked,&dialog,
                [moveSelected](){moveSelected(-1);});
        connect(downButton,&QPushButton::clicked,&dialog,
                [moveSelected](){moveSelected(1);});
        auto *allPage=new QWidget(tabs);
        auto *allLayout=new QVBoxLayout(allPage);
        auto *search=new QLineEdit(allPage);
        search->setPlaceholderText(i18n("Search Krita actions..."));
        allLayout->addWidget(search);
        auto *all=new QListWidget(allPage);
        all->setDragDropMode(QAbstractItemView::NoDragDrop);
        all->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        all->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QScroller::grabGesture(all->viewport(),
                               QScroller::LeftMouseButtonGesture);
        allLayout->addWidget(all);
        tabs->addTab(allPage,i18n("All Krita actions"));
        auto *collection=m_main->actionCollection();
        auto addItem=[&](QListWidget *list,QAction *action,const QString &id) {
            if(!action || id==QStringLiteral("tablet_compact_ui") ||
               id==QStringLiteral("tablet_customize_compact") ||
               id==QStringLiteral("tablet_compact_hub") ||
               id==QStringLiteral("tablet_quick_actions"))return;
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
            const QSignalBlocker blocker(other);
            for(int j=0;j<other->count();++j)
                if(other->item(j)->data(Qt::UserRole).toString()==id)
                    other->item(j)->setCheckState(item->checkState());
        };
        connect(favorites,&QListWidget::itemChanged,&dialog,
                [favorites,sync](QListWidgetItem *item) {
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
        QDialog dialog(m_surface);
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#303034;border:1px solid #56565b;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(8,8,8,8);
        layout->setSpacing(5);
        auto *filter=new QLineEdit(&dialog);
        filter->setPlaceholderText(i18n("Find a tool..."));
        filter->setMinimumHeight(37);
        layout->addWidget(filter);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body=new QWidget(scroll);
        auto *grid=new QGridLayout(body);
        grid->setContentsMargins(2,2,2,2);
        grid->setHorizontalSpacing(5);
        grid->setVerticalSpacing(5);
        scroll->setWidget(body);
        layout->addWidget(scroll);
        QList<QToolButton*> entries;
        QMap<QString,QList<KoToolAction*>> sections;
        for(KoToolAction *action:KoToolManager::instance()->toolActionList())
            if(action)sections[action->section()].append(action);
        const int tileWidth=110;
        const int columns=qBound(2,(m_surface->width()-36)/(tileWidth+5),6);
        int row=0;
        for(auto it=sections.constBegin();it!=sections.constEnd();++it) {
            const QString category=it.key().section(QLatin1Char('/'),-1);
            auto *heading=new QLabel(category.isEmpty() ?
                i18n("Other Tools") : category,body);
            heading->setStyleSheet(QStringLiteral(
                "font-weight:bold;padding-top:6px;color:#ddd;"));
            grid->addWidget(heading,row++,0,1,columns);
            int column=0;
            for(KoToolAction *action:it.value()) {
                auto *button=new QToolButton(body);
                QString caption=action->iconText().trimmed();
                if(caption.endsWith(QStringLiteral(" Tool"),Qt::CaseInsensitive))
                    caption.chop(5);
                if(caption.length()>21)
                    caption=caption.left(19)+QStringLiteral("…");
                button->setText(caption);
                button->setToolTip(action->toolTip());
                button->setIcon(KisIconUtils::loadIcon(action->iconName()));
                button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                button->setIconSize(QSize(38,38));
                button->setFixedSize(tileWidth,85);
                button->setStyleSheet(
                    "QToolButton{background:#404044;color:#ddd;"
                    "border-radius:5px;padding:3px;}"
                    "QToolButton:pressed{background:#5b6d7d;}");
                grid->addWidget(button,row,column);
                entries.append(button);
                connect(button,&QToolButton::clicked,&dialog,[action,&dialog]() {
                    action->trigger();
                    dialog.accept();
                });
                if(++column==columns){column=0;++row;}
            }
            if(column)++row;
        }
        QScroller::grabGesture(scroll->viewport(),
                               QScroller::LeftMouseButtonGesture);
        connect(filter,&QLineEdit::textChanged,&dialog,
                [entries](const QString &text) {
            for(QToolButton *button:entries)
                button->setVisible(button->text().contains(
                    text,Qt::CaseInsensitive) || button->toolTip().contains(
                    text,Qt::CaseInsensitive));
        });
        const int desiredWidth=columns*(tileWidth+5)+22;
        const int desiredHeight=grid->sizeHint().height()+
                                filter->sizeHint().height()+35;
        dialog.resize(qMin(m_surface->width()-16,desiredWidth),
                      qMin(m_surface->height()-22,desiredHeight));
        dialog.exec();
    }
    void organizeDefaultToolbars() {
        auto *collection=m_main->actionCollection();
        QAction *compact=collection->action(QStringLiteral("tablet_compact_ui"));
        if(!compact)return;
        // Never remove the shared toggle QAction from the Compact toolbar:
        // the two modes intentionally use the same action in different bars.
        QToolBar *mainBar=nullptr;
        for(QToolBar *bar:m_main->findChildren<QToolBar*>()) {
            if(bar && bar->objectName()==QStringLiteral("mainToolBar")) {
                mainBar=bar;
                break;
            }
        }
        if(!mainBar)return;
        // This is the ONLY action forced into the regular Krita toolbar.
        if(mainBar->actions().value(0)!=compact) {
            mainBar->removeAction(compact);
            mainBar->insertAction(mainBar->actions().isEmpty() ?
                nullptr:mainBar->actions().first(),compact);
        }
        if(!m_compact)mainBar->show();
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
            // Searching the entire Krita brush resource DB on every UI
            // visibility pass is expensive, especially on Android storage.
            if(m_cachedSlotIconNames[i]!=m_lastPreset[i] ||
               !m_cachedSlotIconInitialized[i]) {
                auto preset=findPreset(m_lastPreset[i]);
                if(preset && !preset->image().isNull())
                    button->setIcon(QIcon(QPixmap::fromImage(preset->image())));
                m_cachedSlotIconNames[i]=m_lastPreset[i];
                m_cachedSlotIconInitialized[i]=true;
            }
            if(m_cachedPaintHighlighted[i]!=(i==m_slot && m_slotReady) ||
               !m_cachedPaintStyleInitialized[i]) {
                button->setStyleSheet(i==m_slot && m_slotReady ?
                    "QToolButton{color:white;border:2px solid #e56c6c;"
                    "border-radius:5px;background:#38383d;}" :
                    "QToolButton{color:white;border:none;border-radius:5px;}");
                m_cachedPaintHighlighted[i]=i==m_slot && m_slotReady;
                m_cachedPaintStyleInitialized[i]=true;
            }
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
                [this,&holdTimer,&heldItem,&heldAction](QListWidgetItem *item) {
            heldItem=item;
            heldAction=false;
            holdTimer.start(holdMs());
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
            feedbackForHold();
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
        auto *pinchGrouping=new QCheckBox(
            i18n("Pinch two layer thumbnails together to create a group"),&dialog);
        pinchGrouping->setChecked(QSettings().value(
            QStringLiteral("TabletUi/pinchToGroupLayers"),true).toBool());
        layout->addWidget(pinchGrouping);
        connect(pinchGrouping,&QCheckBox::toggled,&dialog,[](bool enabled) {
            QSettings().setValue(
                QStringLiteral("TabletUi/pinchToGroupLayers"),enabled);
        });
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
    struct CompactLayerRow {
        KisNodeSP node;
        QPointer<QWidget> widget;
        QPointer<QToolButton> tile;
        int depth=0;
        QString appearance;
        bool thumbnailReady=false;
    };
    QString layerAppearanceKey(KisNodeSP node) const {
        if(!node)return QString();
        const bool group=node->inherits("KisGroupLayer");
        const bool clipped=compactInheritsAlpha(node);
        QString key=node->name();
        key+=QLatin1Char('\x1f');
        key+=QLatin1Char(node->visible()?'1':'0');
        key+=QLatin1Char(node->userLocked()?'1':'0');
        key+=QLatin1Char(clipped?'1':'0');
        key+=QLatin1Char(group?'1':'0');
        key+=QLatin1Char(node->inherits("KisMask")?'1':'0');
        key+=QLatin1Char(group && node->collapsed()?'1':'0');
        key+=QLatin1Char(group &&
            qobject_cast<KisGroupLayer*>(node.data())->passThroughMode()?'1':'0');
        key+=QLatin1Char(node->nextSibling() &&
            compactInheritsAlpha(node->nextSibling())?'1':'0');
        if(auto *paint=qobject_cast<KisPaintLayer*>(node.data()))
            key+=QLatin1Char(paint->alphaLocked()?'1':'0');
        return key;
    }
    void refreshVisibleLayerThumbnails(bool forceActive) {
        if(!m_compact || m_clipTransactionBusy || !m_layersScroll ||
           !m_layersScroll->viewport() || !m_layerRail->isVisible())return;
        // Don't decode new thumbnails under a finger during kinetic scroll
        // or stylus reordering. Display them when movement has settled.
        if(!m_layerTouches.isEmpty() || m_layerDragNode) {
            if(m_layerPreviewTimer)m_layerPreviewTimer->start(160);
            return;
        }
        QWidget *viewport=m_layersScroll->viewport();
        QRect view(viewport->mapToGlobal(QPoint()),viewport->size());
        view.adjust(0,-50,0,50);
        const KisNodeSP active=m_vm->activeNode();
        for(CompactLayerRow &row:m_compactLayerRows) {
            if(!row.widget || !row.tile || !row.node)continue;
            if(row.node->inherits("KisGroupLayer")) {
                row.thumbnailReady=true;continue;
            }
            if(forceActive && row.node!=active)continue;
            if(row.thumbnailReady && !forceActive)continue;
            const QRect bounds(row.widget->mapToGlobal(QPoint()),
                               row.widget->size());
            if(!bounds.intersects(view))continue;
            const QSize size=row.tile->iconSize();
            const QImage thumb=row.node->createThumbnail(
                size.width(),size.height(),Qt::KeepAspectRatio);
            if(!thumb.isNull())
                row.tile->setIcon(QIcon(QPixmap::fromImage(thumb)));
            row.thumbnailReady=true;
        }
    }
    void refreshSelectedLayerTiles(KisNodeSP active) {
        if(m_lastLayerActive==active)return;
        for(const CompactLayerRow &row:m_compactLayerRows) {
            if(!row.tile || (row.node!=active &&
               row.node!=m_lastLayerActive))continue;
            row.tile->setStyleSheet(row.node==active ?
                "QToolButton{border:3px solid #e86c79;border-radius:5px;"
                "background:#a4a4a4;}" :
                "QToolButton{border:2px solid #808080;border-radius:5px;"
                "background:#a4a4a4;}");
        }
        m_lastLayerActive=active;
    }
    struct CompactTouchPoint {
        KisNodeSP node;
        QPoint start;
        QPoint last;
    };
    const CompactLayerRow *touchLayerAt(const QPoint &globalPos) const {
        QWidget *hit=QApplication::widgetAt(globalPos);
        for(const CompactLayerRow &row:m_compactLayerRows) {
            if(!row.widget || !row.tile ||
               !QRect(row.widget->mapToGlobal(QPoint()),row.widget->size())
                   .contains(globalPos))continue;
            // Ignore touches on visibility, alpha lock, folder expansion and
            // the other small buttons that have their own native actions.
            if(hit && hit!=row.widget && hit!=row.tile &&
               !row.tile->isAncestorOf(hit))return nullptr;
            return &row;
        }
        return nullptr;
    }
    void groupPinchedLayers(KisNodeSP start,KisNodeSP finish) {
        if(!start || !finish || start==finish || !start->parent() ||
           start->parent()!=finish->parent())return;
        // Group siblings in Krita's actual node ordering, including every
        // layer between the two fingers. Masks cannot be grouped as layers.
        const KisNodeSP parent=start->parent();
        KisNodeList ordered;
        for(KisNodeSP node=parent->firstChild();node;node=node->nextSibling())
            ordered.append(node);
        const int a=ordered.indexOf(start);
        const int b=ordered.indexOf(finish);
        if(a<0 || b<0 || a==b)return;
        KisNodeList selected;
        for(int i=qMin(a,b);i<=qMax(a,b);++i) {
            KisNodeSP node=ordered.at(i);
            if(!node->inherits("KisLayer") ||
               !parent->allowAsChild(node)) {
                m_layerName->setText(i18n("Only compatible sibling layers can be grouped"));
                return;
            }
            selected.append(node);
        }
        if(!m_vm->nodeManager()->canMoveLayers(selected,false)) {
            m_layerName->setText(i18n("Unlock these layers before grouping"));
            return;
        }
        m_vm->nodeManager()->slotUiActivatedNode(start);
        m_vm->nodeManager()->slotSetSelectedNodes(selected);
        m_vm->nodeManager()->createQuickGroup();
        // Match the pinch-to-folder interaction: the newly-created group
        // starts collapsed, but native Krita grouping remains fully undoable.
        QTimer::singleShot(250,this,[this,start,finish,parent]() mutable {
            KisNodeSP newParent=start->parent();
            if(newParent && newParent!=parent &&
               newParent==finish->parent() &&
               newParent->inherits("KisGroupLayer"))
                newParent->setCollapsed(true);
            refreshLayers();
        });
    }
    bool handleLayerTouch(QObject *watched,QEvent *event) {
        if(!m_compact || m_clipTransactionBusy || !m_layerRail || !m_layerRail->isVisible())return false;
        const QEvent::Type type=event->type();
        if(type!=QEvent::TouchBegin && type!=QEvent::TouchUpdate &&
           type!=QEvent::TouchEnd && type!=QEvent::TouchCancel)return false;
        QWidget *target=qobject_cast<QWidget*>(watched);
        if(!target)return false;
        const bool inRail=target==m_layerRail ||
            m_layerRail->isAncestorOf(target);
        if(!inRail && m_layerTouches.isEmpty())return false;
        if(type==QEvent::TouchCancel) {
            restoreLayerScroller();
            m_layerTouchHoldTimer->stop();
            m_layerTouches.clear();
            m_touchLayerArmed=false;
            m_touchLayerPinching=false;
            if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->stop();
            hideLayerDragPreview();
            return false;
        }
        QTouchEvent *touch=static_cast<QTouchEvent*>(event);
        bool consumed=!m_layerTouches.isEmpty();
        for(const QTouchEvent::TouchPoint &point:touch->touchPoints()) {
            const int id=point.id();
            const QPoint pos=point.screenPos().toPoint();
            const bool released=point.state() & Qt::TouchPointReleased;
            if(!m_layerTouches.contains(id) && !released && inRail) {
                const CompactLayerRow *row=touchLayerAt(pos);
                if(!row)continue;
                consumed=true;
                CompactTouchPoint item;
                item.node=row->node;
                item.start=pos;
                item.last=pos;
                if(m_layerTouches.isEmpty()) {
                    m_touchLayerPrimary=row->node;
                    m_touchLayerArmed=false;
                    m_touchLayerMoved=false;
                    m_touchLayerScrolling=false;
                    m_touchLayerPinching=false;
                    m_touchPinchReady=false;
                    m_touchLayerLast=pos;
                    m_layerTouchHoldTimer->start(holdMs());
                }
                m_layerTouches.insert(id,item);
            }
            if(m_layerTouches.contains(id)) {
                m_layerTouches[id].last=pos;
                consumed=true;
            }
        }
        if(!consumed)return false;
        if(m_layerTouches.size()>=2) {
            const auto pts=m_layerTouches.values();
            if(!m_touchLayerPinching) {
                m_touchLayerPinching=true;
                m_layerTouchHoldTimer->stop();
                m_touchPinchA=pts.at(0).node;
                m_touchPinchB=pts.at(1).node;
                m_touchPinchStartDistance=QLineF(
                    pts.at(0).start,pts.at(1).start).length();
            }
            if(QSettings().value(
                   QStringLiteral("TabletUi/pinchToGroupLayers"),true).toBool() &&
               (type==QEvent::TouchUpdate || type==QEvent::TouchEnd) &&
               m_touchPinchStartDistance>=65) {
                const qreal current=QLineF(
                    pts.at(0).last,pts.at(1).last).length();
                if(current<m_touchPinchStartDistance*0.55 &&
                   m_touchPinchStartDistance-current>45)
                    m_touchPinchReady=true;
            }
        } else if(m_layerTouches.size()==1 && !m_touchLayerPinching &&
                  (type==QEvent::TouchUpdate || type==QEvent::TouchEnd)) {
            const CompactTouchPoint point=m_layerTouches.constBegin().value();
            const int travel=(point.last-point.start).manhattanLength();
            if(!m_touchLayerArmed && travel>12) {
                m_touchLayerScrolling=true;
                m_layerTouchHoldTimer->stop();
                m_layersScroll->verticalScrollBar()->setValue(
                    m_layersScroll->verticalScrollBar()->value()+
                    m_touchLayerLast.y()-point.last.y());
            } else if(m_touchLayerArmed && travel>18) {
                m_touchLayerMoved=true;
                showLayerDragPreview(m_touchLayerPrimary,point.last);
                for(const CompactLayerRow &row:m_compactLayerRows) {
                    if(row.node && row.widget &&
                       QRect(row.widget->mapToGlobal(QPoint()),row.widget->size())
                           .contains(point.last) && row.node!=m_touchLayerPrimary) {
                        m_layerName->setText(i18n("Move near %1",row.node->name()));
                        break;
                    }
                }
            }
            m_touchLayerLast=point.last;
        }
        for(const QTouchEvent::TouchPoint &point:touch->touchPoints()) {
            if(point.state() & Qt::TouchPointReleased)
                m_layerTouches.remove(point.id());
        }
        // Qt may send separate TouchEnd events to separate widgets when the
        // two fingers started on different thumbnails. Don't finish a pinch
        // while the second finger is still touching its own widget.
        if(type==QEvent::TouchEnd && touch->touchPoints().isEmpty())
            m_layerTouches.clear();
        if(m_layerTouches.isEmpty()) {
            m_layerTouchHoldTimer->stop();
            restoreLayerScroller();
            hideLayerDragPreview();
            if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->stop();
            const KisNodeSP primary=m_touchLayerPrimary;
            const KisNodeSP pinchedA=m_touchPinchA;
            const KisNodeSP pinchedB=m_touchPinchB;
            const bool doGroup=m_touchLayerPinching && m_touchPinchReady;
            const bool wasPinching=m_touchLayerPinching;
            const bool wasScroll=m_touchLayerScrolling;
            const bool drag=m_touchLayerArmed && m_touchLayerMoved;
            const bool held=m_touchLayerArmed;
            const QPoint last=m_touchLayerLast;
            m_touchLayerPrimary.clear();
            m_touchPinchA.clear();
            m_touchPinchB.clear();
            m_touchLayerArmed=false;
            m_touchLayerPinching=false;
            m_touchPinchReady=false;
            if(doGroup) {
                groupPinchedLayers(pinchedA,pinchedB);
            } else if(!wasPinching && !wasScroll && primary) {
                if(drag)moveLayerAtFinger(primary,last);
                else if(held) {
                    m_vm->nodeManager()->slotUiActivatedNode(primary);
                    openLayerActions(primary);
                } else {
                    m_vm->nodeManager()->slotUiActivatedNode(primary);
                }
            }
            QTimer::singleShot(0,this,[this](){refreshLayers();});
        }
        event->accept();
        return true;
    }

    // Krita still stores true groups for alpha inheritance, but Compact
    // omits the wrapper. Detect older unrenamed Group N wrappers only if
    // they contain exactly one base and clipped children above it.
    static bool isCompactClipGroup(KisNodeSP node) {
        if(!node || !node->inherits("KisGroupLayer"))return false;
        if(node->name().startsWith(QStringLiteral("[Compact Clip] ")))return true;
        static const QRegularExpression defaultName(QStringLiteral(
            "^(?:Group|Clipping Group)(?: [0-9]+)?$"),
            QRegularExpression::CaseInsensitiveOption);
        if(!defaultName.match(node->name()).hasMatch() ||
           node->childCount()<2)return false;
        KisNodeSP base=node->firstChild();
        if(!base || !base->inherits("KisLayer") ||
           compactInheritsAlpha(base))return false;
        for(KisNodeSP n=base->nextSibling();n;n=n->nextSibling())
            if(!compactInheritsAlpha(n))return false;
        return true;
    }
    static bool compactInheritsAlpha(KisNodeSP node) {
        return node && node->inherits("KisLayer") &&
            KisLayerPropertiesIcons::nodeProperty(
                node,KisLayerPropertiesIcons::inheritAlpha,false).toBool();
    }
    static KisNodeSP compactLogicalParent(KisNodeSP node) {
        if(!node)return KisNodeSP();
        KisNodeSP parent=node->parent();
        return isCompactClipGroup(parent)?parent->parent():parent;
    }
    bool containsCompactClipGroups(KisNodeSP parent) const {
        if(!parent)return false;
        for(KisNodeSP n=parent->firstChild();n;n=n->nextSibling())
            if(isCompactClipGroup(n))return true;
        return false;
    }
    // Krita's compressed node juggler runs on the image thread. Do not
    // issue the next grouping operation until its actual nodes have moved.
    void finishCompactClipTransaction() {
        if(m_vm && m_vm->nodeManager()) {
            if(m_vm->image().toStrongRef()==m_clipWorkingImage &&
               m_clipRestoreActive && m_clipRestoreActive->parent()) {
                m_vm->nodeManager()->slotUiActivatedNode(m_clipRestoreActive);
                m_vm->nodeManager()->slotSetSelectedNodes(
                    KisNodeList{m_clipRestoreActive});
            } else {
                m_vm->nodeManager()->slotSetSelectedNodes(KisNodeList());
            }
        }
        m_clipRebuildParents.clear();
        m_clipPendingWrappers.clear();
        m_clipPendingMoved.clear();
        m_clipExpectedParent.clear();
        m_clipExpectedBelow.clear();
        m_clipRestoreActive.clear();
        m_clipWorkingImage.clear();
        m_clipTransactionBusy=false;
        QTimer::singleShot(120,this,[this](){refreshLayers();});
    }
    void waitForCompactGroup(KisNodeSP parent,KisNodeSP base,
                             KisNodeList cluster,int attempt) {
        if(!m_clipTransactionBusy || !m_vm ||
           m_vm->image().toStrongRef()!=m_clipWorkingImage) {
            finishCompactClipTransaction();return;
        }
        KisNodeSP group=base?base->parent():KisNodeSP();
        bool settled=group && group!=parent &&
            group->parent()==parent && group->inherits("KisGroupLayer");
        for(KisNodeSP item:cluster)
            if(!item || item->parent()!=group)settled=false;
        if(settled) {
            m_vm->nodeManager()->setNodeName(
                group,QStringLiteral("[Compact Clip] ")+base->name());
            group->setCollapsed(false);
            QTimer::singleShot(120,this,[this](){continueCompactClipRebuild();});
        } else if(attempt<35) {
            QTimer::singleShot(110,this,[this,parent,base,cluster,attempt]() {
                waitForCompactGroup(parent,base,cluster,attempt+1);
            });
        } else {
            finishCompactClipTransaction();
        }
    }
    void continueCompactClipRebuild() {
        if(!m_clipTransactionBusy || !m_vm || !m_vm->nodeManager() ||
           m_vm->image().toStrongRef()!=m_clipWorkingImage) {
            finishCompactClipTransaction();return;
        }
        while(!m_clipRebuildParents.isEmpty()) {
            KisNodeSP parent=m_clipRebuildParents.first();
            if(!parent) {m_clipRebuildParents.removeFirst();continue;}
            KisNodeList ordered;
            for(KisNodeSP n=parent->firstChild();n;n=n->nextSibling())
                ordered.append(n);
            // Sibling order is bottom-to-top. Each consecutive inherited-
            // alpha run belongs to the first non-clipped base below it.
            for(int i=0;i<ordered.size();) {
                KisNodeSP base=ordered.at(i);
                if(isCompactClipGroup(base) || compactInheritsAlpha(base) ||
                   !base->inherits("KisLayer")){++i;continue;}
                KisNodeList cluster{base};
                int j=i+1;
                while(j<ordered.size() &&
                      compactInheritsAlpha(ordered.at(j))) {
                    cluster.append(ordered.at(j));++j;
                }
                if(cluster.size()>1 &&
                   m_vm->nodeManager()->canMoveLayers(cluster,false)) {
                    m_vm->nodeManager()->slotUiActivatedNode(base);
                    m_vm->nodeManager()->slotSetSelectedNodes(cluster);
                    m_vm->nodeManager()->createQuickGroup();
                    QTimer::singleShot(120,this,[this,parent,base,cluster]() {
                        waitForCompactGroup(parent,base,cluster,0);
                    });
                    return; // The next group must not race this one.
                }
                i=j;
            }
            m_clipRebuildParents.removeFirst();
        }
        finishCompactClipTransaction();
    }
    void waitForCompactPlacement(int attempt) {
        if(!m_clipTransactionBusy || !m_vm || !m_vm->nodeManager() ||
           m_vm->image().toStrongRef()!=m_clipWorkingImage) {
            finishCompactClipTransaction();return;
        }
        if(m_clipPendingMoved &&
           (m_clipPendingMoved->parent()!=m_clipExpectedParent ||
            m_clipPendingMoved->prevSibling()!=m_clipExpectedBelow)) {
            if(attempt<35) {
                QTimer::singleShot(110,this,[this,attempt]() {
                    waitForCompactPlacement(attempt+1);
                });
            } else {
                finishCompactClipTransaction();
            }
            return;
        }
        continueCompactClipRebuild();
    }
    void waitForCompactFlatten(int attempt,
                                const std::function<void()> &afterFlatten) {
        if(!m_clipTransactionBusy || !m_vm || !m_vm->nodeManager() ||
           m_vm->image().toStrongRef()!=m_clipWorkingImage) {
            finishCompactClipTransaction();return;
        }
        bool hasWrappers=false;
        // Track the actual wrapper nodes: a legacy "Group N" can stop
        // matching the structural heuristic as its children move out,
        // before Krita has removed the now-empty folder.
        for(KisNodeSP wrapper:m_clipPendingWrappers)
            if(wrapper && wrapper->parent())hasWrappers=true;
        if(hasWrappers && attempt<35) {
            QTimer::singleShot(110,this,[this,attempt,afterFlatten]() {
                waitForCompactFlatten(attempt+1,afterFlatten);
            });
            return;
        }
        if(hasWrappers) {finishCompactClipTransaction();return;}
        if(afterFlatten)afterFlatten();
        QTimer::singleShot(afterFlatten?160:100,this,[this]() {
            waitForCompactPlacement(0);
        });
    }
    void beginCompactClipRebuild(KisNodeSP first,
                                 KisNodeSP second=KisNodeSP(),
                                 std::function<void()> afterFlatten={}) {
        if(m_clipTransactionBusy || !first || !m_vm ||
           !m_vm->nodeManager())return;
        m_clipWorkingImage=m_vm->image().toStrongRef();
        if(!m_clipWorkingImage)return;
        m_clipRestoreActive=m_vm->activeNode();
        m_clipTransactionBusy=true;
        m_clipRebuildParents.clear();
        m_clipPendingWrappers.clear();
        m_clipPendingMoved.clear();
        m_clipExpectedParent.clear();
        m_clipExpectedBelow.clear();
        m_clipRebuildParents.append(first);
        if(second && second!=first)m_clipRebuildParents.append(second);
        ungroupCompactClips(first);
        if(second && second!=first)ungroupCompactClips(second);
        QTimer::singleShot(120,this,[this,afterFlatten]() {
            waitForCompactFlatten(0,afterFlatten);
        });
    }
    void setCompactClip(KisNodeSP node,bool enabled) {
        if(m_clipTransactionBusy || !node || !node->inherits("KisLayer"))return;
        KisNodeSP parent=compactLogicalParent(node);
        if(!parent)return;
        if(enabled) {
            bool foundBase=false;
            bool after=false;
            for(const CompactLayerRow &row:m_compactLayerRows) {
                if(row.node==node){after=true;continue;}
                if(after && compactLogicalParent(row.node)==parent &&
                   row.node->inherits("KisLayer") &&
                   !compactInheritsAlpha(row.node)) {
                    foundBase=true;break;
                }
            }
            if(!foundBase) {
                if(m_layerName)m_layerName->setText(
                    i18n("Add a non-clipped layer below this layer first"));
                return;
            }
        }
        if(auto image=m_vm->image().toStrongRef()) {
            KisLayerPropertiesIcons::setNodePropertyAutoUndo(
                node,KisLayerPropertiesIcons::inheritAlpha,enabled,image);
            beginCompactClipRebuild(parent);
        }
    }
    bool canMoveCompactNode(KisNodeSP node,KisNodeSP newParent) const {
        if(!node || !newParent || !node->parent() || node==newParent)
            return false;
        // Reject self-parenting and putting a group into its own descendants.
        for(KisNodeSP p=newParent;p;p=p->parent())
            if(p==node)return false;
        return newParent->allowAsChild(node);
    }

    void suspendLayerScroller() {
        if(!m_layersScroll || !m_layersScroll->viewport() ||
           m_layerScrollerSuspended)return;
        QWidget *viewport=m_layersScroll->viewport();
        if(QScroller *scroller=QScroller::scroller(viewport))scroller->stop();
        QScroller::ungrabGesture(viewport);
        m_layerScrollerSuspended=true;
    }
    void restoreLayerScroller() {
        if(!m_layerScrollerSuspended || !m_layersScroll ||
           !m_layersScroll->viewport())return;
        QScroller::grabGesture(m_layersScroll->viewport(),
                              QScroller::TouchGesture);
        m_layerScrollerSuspended=false;
    }
    void finishLayerDrag() {
        restoreLayerScroller();
        m_layerPenTracking=false;
        hideLayerDragPreview();
        if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->stop();
        m_layerDragNode.clear();
        m_layerDragTile.clear();
        m_layerDragArmed=false;
        m_layerDragMoved=false;
    }
    const CompactLayerRow *compactDropRowAt(const QPoint &globalPos,
                                            KisNodeSP source) const {
        const CompactLayerRow *closest=nullptr;
        int best=INT_MAX;
        const QRect area(m_layersScroll->viewport()->mapToGlobal(QPoint()),
                         m_layersScroll->viewport()->size());
        // A drag may be between tiles or near the upper/lower autoscroll
        // border. Snap to the nearest *visible* row instead of dropping nowhere.
        if(globalPos.x()<area.left()-28 || globalPos.x()>area.right()+28 ||
           globalPos.y()<area.top()-56 || globalPos.y()>area.bottom()+56)
            return nullptr;
        for(const CompactLayerRow &row:m_compactLayerRows) {
            if(!row.widget || !row.node || row.node==source)continue;
            const QRect box(row.widget->mapToGlobal(QPoint()),row.widget->size());
            if(box.bottom()<area.top() || box.top()>area.bottom())continue;
            if(box.contains(globalPos))return &row;
            const int d=qAbs(globalPos.y()-box.center().y());
            if(d<best){best=d;closest=&row;}
        }
        return closest;
    }
    void hideLayerDragPreview() {
        if(m_layerDragGhost)m_layerDragGhost->hide();
        if(m_layerDropMarker)m_layerDropMarker->hide();
    }
    void showLayerDragPreview(KisNodeSP source,const QPoint &globalPos) {
        if(!source || !m_layerDragGhost || !m_compact)return;
        if(!m_layerDragGhost->isVisible()) {
            QPixmap pix=source->inherits("KisGroupLayer") ?
                KisIconUtils::loadIcon("folder").pixmap(56,56) :
                QPixmap::fromImage(source->createThumbnail(
                    56,56,Qt::KeepAspectRatio));
            if(pix.isNull())pix=KisIconUtils::loadIcon("document-open").pixmap(54,54);
            m_layerDragGhost->setPixmap(pix.scaled(
                53,53,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            m_layerDragGhost->show();
        }
        const QPoint local=m_surface->mapFromGlobal(globalPos);
        m_layerDragGhost->move(
            qBound(0,local.x()+10,qMax(0,m_surface->width()-67)),
            qBound(0,local.y()-73,qMax(0,m_surface->height()-67)));
        m_layerDragGhost->raise();
        if(m_layerDropMarker)m_layerDropMarker->hide();
        if(const CompactLayerRow *candidate=compactDropRowAt(globalPos,source)) {
            const CompactLayerRow &row=*candidate;
            const QRect box(row.widget->mapToGlobal(QPoint()),row.widget->size());
            const bool folder=row.node->inherits("KisGroupLayer") &&
                              globalPos.x()>box.center().x();
            const bool upper=globalPos.y()<box.center().y();
            KisNodeSP parent=folder?row.node:compactLogicalParent(row.node);
            if(!canMoveCompactNode(source,parent)) {
                m_layerName->setText(i18n("Cannot move to this layer"));
                return;
            }
            if(m_layerDropMarker) {
                m_layerDropMarker->setFixedWidth(row.widget->width());
                const int y=folder?row.widget->y()+box.height()/2:
                    row.widget->y()+(upper?0:row.widget->height());
                m_layerDropMarker->move(row.widget->x(),y);
                m_layerDropMarker->show();
                m_layerDropMarker->raise();
            }
            m_layerName->setText(folder?
                i18n("Move into %1",row.node->name()):
                upper?i18n("Move above %1",row.node->name()):
                      i18n("Move below %1",row.node->name()));
            return;
        }
    }
    void ungroupCompactClips(KisNodeSP parent) {
        if(!parent || !m_vm || !m_vm->nodeManager())return;
        // Deepest wrapper first; leave ordinary named user groups intact.
        QList<KisNodeSP> wrappers;
        std::function<void(KisNodeSP)> collect=[&](KisNodeSP container) {
            for(KisNodeSP n=container->firstChild();n;n=n->nextSibling()) {
                if(isCompactClipGroup(n)) {
                    collect(n);
                    wrappers.append(n);
                }
            }
        };
        collect(parent);
        for(KisNodeSP wrapper:wrappers) {
            m_clipPendingWrappers.append(wrapper);
            m_vm->nodeManager()->slotUiActivatedNode(wrapper);
            m_vm->nodeManager()->slotSetSelectedNodes(KisNodeList{wrapper});
            m_vm->nodeManager()->quickUngroup();
        }
    }
    void moveLayerAtFinger(KisNodeSP source,const QPoint &screenPos) {
        if(m_clipTransactionBusy || !source || !m_vm ||
           !m_vm->image().toStrongRef())return;
        const CompactLayerRow *row=compactDropRowAt(screenPos,source);
        if(!row || !row->widget || !row->node)return;
        const KisNodeSP target=row->node;
        const QRect bounds(row->widget->mapToGlobal(QPoint()),row->widget->size());
        const bool intoGroup=target->inherits("KisGroupLayer") &&
            screenPos.x()>bounds.center().x();
        const bool upper=screenPos.y()<bounds.center().y();
        KisNodeSP destination=intoGroup?target:compactLogicalParent(target);
        KisNodeSP previous=compactLogicalParent(source);
        if(!canMoveCompactNode(source,destination) ||
           !m_vm->nodeManager()->canMoveLayer(source,false)){
            m_layerName->setText(i18n("Layer cannot move to that location"));
            return;
        }
        // The Compact order is authoritative; native groups are rebuilt
        // around whichever unclipped base is below each clipped layer NOW.
        beginCompactClipRebuild(previous,destination,
            [this,source,target,destination,intoGroup,upper]() {
                if(!source || !target || !destination || !m_vm ||
                   !m_vm->nodeManager())return;
                KisNodeSP above=intoGroup?destination->lastChild():
                    (upper?target:target->prevSibling());
                if(above==source && source->parent()==destination)return;
                if(!canMoveCompactNode(source,destination))return;
                m_clipPendingMoved=source;
                m_clipExpectedParent=destination;
                m_clipExpectedBelow=above;
                m_vm->nodeManager()->moveNodesDirect(
                    KisNodeList{source},destination,above);
            });
    }

    void refreshLayers() {
        if(!m_compact || m_clipTransactionBusy || !m_vm->activeNode() || m_layerDragNode ||
           !m_layerTouches.isEmpty())return;
        const auto image=m_vm->image().toStrongRef();
        if(!image || !image->root())return;
        const KisNodeSP active=m_vm->activeNode();
        // A selection or painting update must not destroy and rebuild the
        // whole UI. First compare inexpensive node identities/properties.
        // Rebuild only for real stacking, grouping, property or size changes.
        bool identical=m_lastLayerImage==image.data();
        if(identical) {
            QList<QPair<KisNodeSP,int>> layoutNodes;
            std::function<void(KisNodeSP,int)> collect;
            collect=[&](KisNodeSP parent,int depth) {
                if(!parent || layoutNodes.size()>=180)return;
                for(KisNodeSP node=parent->lastChild();node &&
                    layoutNodes.size()<180;node=node->prevSibling()) {
                    if(node->inherits("KisReferenceImagesLayer"))continue;
                    if(isCompactClipGroup(node)) {
                        collect(node,depth);continue;
                    }
                    layoutNodes.append(qMakePair(node,depth));
                    if(node->childCount() &&
                       (!node->inherits("KisGroupLayer") ||
                        !node->collapsed()))
                        collect(node,depth+1);
                }
            };
            collect(image->root(),0);
            identical=layoutNodes.size()==m_compactLayerRows.size();
            if(identical) {
                for(int i=0;i<layoutNodes.size();++i) {
                    const CompactLayerRow &row=m_compactLayerRows.at(i);
                    if(!row.widget || !row.tile ||
                       row.node!=layoutNodes.at(i).first ||
                       row.depth!=layoutNodes.at(i).second ||
                       row.widget->width()!=m_layerTileSize+4 ||
                       row.appearance!=layerAppearanceKey(row.node)) {
                        identical=false;break;
                    }
                }
            }
            if(identical) {
                refreshSelectedLayerTiles(active);
                m_layerName->setText(active->name().left(14));
                refreshVisibleLayerThumbnails(false);
                return;
            }
        }
        const int oldScroll=m_layersScroll->verticalScrollBar()->value();
        m_compactLayerRows.clear();
        m_compactLayerHeight=0;
        clearLayerRows();
        int count=0;
        std::function<void(KisNodeSP,int)> appendChildren;
        appendChildren=[this,&appendChildren,&count,active](KisNodeSP parent,int depth) {
            if(!parent || count>=180)return;
            // Higher sibling indices are the topmost Krita layers.
            for(KisNodeSP node=parent->lastChild();node && count<180;
                node=node->prevSibling()) {
                if(node->inherits("KisReferenceImagesLayer"))continue;
                if(isCompactClipGroup(node)) {
                    // Flatten the internal group in Compact only; normal Krita
                    // retains the actual folder for native editing and undo.
                    appendChildren(node,depth);
                    continue;
                }
                ++count;
                const bool isGroup=node->inherits("KisGroupLayer");
                const bool isMask=node->inherits("KisMask");
                const bool inheritsAlpha=KisLayerPropertiesIcons::nodeProperty(
                    node,KisLayerPropertiesIcons::inheritAlpha,false).toBool();
                const bool passThrough=isGroup &&
                    qobject_cast<KisGroupLayer*>(node.data())->passThroughMode();
                // Only real folders and masks are short rows; clipped
                // paint layers keep the same dimensions as ordinary layers.
                const int rowWidth=m_layerTileSize;
                const bool compactRow=isGroup || isMask;
                const int rowHeight=compactRow?qMax(42,rowWidth*53/100):rowWidth;
                const int iconPx=compactRow?qMax(25,rowHeight-9):rowWidth-12;
                m_compactLayerHeight+=rowHeight+7;
                auto *frame=new QFrame(m_layersContent);
                frame->setFixedSize(rowWidth+4,rowHeight+4);
                // Full-size clipped tiles receive a distinct inset alpha edge;
                // no artificial folder is shown to the Compact user.
                if(inheritsAlpha) {
                    frame->setObjectName(QStringLiteral("compactClippedLayer"));
                    frame->setStyleSheet(
                        "QFrame#compactClippedLayer{border-left:8px solid #4aafa1;"
                        "background:#454b4b;border-radius:3px;}");
                } else if(node->nextSibling() &&
                          KisLayerPropertiesIcons::nodeProperty(
                              node->nextSibling(),
                              KisLayerPropertiesIcons::inheritAlpha,false).toBool()) {
                    frame->setObjectName(QStringLiteral("compactClipBase"));
                    frame->setStyleSheet(
                        "QFrame#compactClipBase{border-left:4px solid #4aafa1;"
                        "border-top:2px solid #4aafa1;border-radius:3px;}");
                } else if(depth>0) {
                    frame->setObjectName(QStringLiteral("compactNestedLayer"));
                    frame->setStyleSheet(
                        "QFrame#compactNestedLayer{border-left:3px solid #657f90;}");
                }
                auto *tile=new QToolButton(frame);
                tile->setObjectName(QStringLiteral("compactLayerTouchTile"));
                tile->setAttribute(Qt::WA_AcceptTouchEvents,true);
                // Same overall row size, with a small visible left inset for
                // clipped layers instead of shrinking them to a short card.
                tile->setGeometry(inheritsAlpha?7:1,1,
                                  rowWidth-(inheritsAlpha?6:0),rowHeight);
                tile->setToolButtonStyle(compactRow ?
                    Qt::ToolButtonTextBesideIcon:Qt::ToolButtonIconOnly);
                tile->setIconSize(QSize(iconPx,iconPx));
                // Actual image thumbnails are decoded only after Qt lays out
                // the rows, and only for tiles near the visible viewport.
                if(isGroup)tile->setIcon(KisIconUtils::loadIcon("folder"));
                tile->setText(compactRow ?
                    QFontMetrics(tile->font()).elidedText(node->name(),
                        Qt::ElideRight,qMax(20,rowWidth-iconPx-22)):node->name());
                const QString type=isGroup?i18n("Group"):
                    isMask?i18n("Mask"):i18n("Layer");
                QString tooltip=node->name()+QStringLiteral(" — ")+type;
                if(depth)tooltip+=i18n(" (nested %1 levels)",depth);
                if(inheritsAlpha)tooltip+=i18n(" — Inherit alpha / clipped");
                if(passThrough)tooltip+=i18n(" — pass-through group");
                tile->setToolTip(tooltip+i18n(
                    "\nTap: select. Hold: options. Hold and drag: reorder. Move near the top or bottom to scroll."));
                tile->setStyleSheet(node==active ?
                    "QToolButton{border:3px solid #e86c79;border-radius:5px;"
                    "background:#a4a4a4;}" :
                    "QToolButton{border:2px solid #808080;border-radius:5px;"
                    "background:#a4a4a4;}");
                // Hierarchy stripes stay within the square thumbnail.
                for(int d=0;d<qMin(depth,4);++d) {
                    auto *stripe=new QFrame(frame);
                    stripe->setGeometry(1+d*2,2,2,rowHeight-2);
                    stripe->setStyleSheet(
                        "background:#5a9db7;border:none;");
                    stripe->setAttribute(Qt::WA_TransparentForMouseEvents);
                }
                auto *typeBadge=new QLabel(frame);
                typeBadge->setGeometry(3+qMin(depth,4)*2,3,22,20);
                typeBadge->setAlignment(Qt::AlignCenter);
                typeBadge->setText(isGroup?QStringLiteral("▣"):
                                   isMask?QStringLiteral("M"):
                                   inheritsAlpha?QStringLiteral("α"):QString());
                typeBadge->setStyleSheet(isMask?
                    "QLabel{background:#9b6caa;color:white;border-radius:3px;"
                    "font-weight:bold;}" :
                    "QLabel{background:#435767;color:white;border-radius:3px;"
                    "font-weight:bold;}");
                typeBadge->setVisible(isMask);
                typeBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
                if(isGroup) {
                    auto *expander=new QToolButton(frame);
                    expander->setGeometry(3,3,24,24);
                    expander->setText(node->collapsed()?
                        QStringLiteral("▸"):QStringLiteral("▾"));
                    expander->setToolTip(node->collapsed()?
                        i18n("Expand group"):i18n("Collapse group"));
                    expander->setStyleSheet(
                        "QToolButton{background:#3c5c70;color:white;"
                        "border:1px solid #ddd;border-radius:4px;}");
                    connect(expander,&QToolButton::clicked,this,[this,node]() mutable {
                        node->setCollapsed(!node->collapsed());
                        refreshLayers();
                    });
                }
                if(inheritsAlpha || passThrough) {
                    auto *clip=new QLabel(frame);
                    clip->setText(passThrough?QStringLiteral("⇢"):
                                             QStringLiteral("↳α"));
                    clip->setGeometry(7,qMax(2,rowHeight-27),30,24);
                    clip->setAlignment(Qt::AlignCenter);
                    clip->setToolTip(passThrough?i18n("Pass-through group"):
                                                  i18n("Inherit alpha / clipped"));
                    clip->setStyleSheet(
                        "QLabel{background:#326d68;color:white;border-radius:4px;"
                        "font-weight:bold;}");
                    clip->setAttribute(Qt::WA_TransparentForMouseEvents);
                }
                auto *tileHold=new QTimer(tile);
                tileHold->setSingleShot(true);
                tileHold->setInterval(holdMs());
                auto *held=new bool(false);
                connect(tile,&QObject::destroyed,this,[held]() {delete held;});
                tile->installEventFilter(this);
                connect(tile,&QToolButton::pressed,tile,
                        [this,tileHold,held,tile,node]() {
                    if(m_layerPenTracking && m_layerDragNode==node)return;
                    *held=false;
                    m_layerDragNode=node;
                    m_layerDragTile=tile;
                    m_layerDragArmed=false;
                    m_layerDragMoved=false;
                    m_layerDragStart=QCursor::pos();
                    m_layerDragLast=m_layerDragStart;
                    tileHold->start(holdMs());
                });
                connect(tileHold,&QTimer::timeout,tile,[this,held]() {
                    *held=true;
                    m_layerDragArmed=true;
                    suspendLayerScroller();
                    if(m_layerEdgeScrollTimer)m_layerEdgeScrollTimer->start();
                    showLayerDragPreview(m_layerDragNode,m_layerDragLast);
                    feedbackForHold();
                });
                connect(tile,&QToolButton::released,tile,
                        [this,tileHold,held,node]() {
                    if(m_layerPenTracking && m_layerDragNode==node)return;
                    tileHold->stop();
                    const bool moved=m_layerDragMoved;
                    const QPoint drop=m_layerDragLast;
                    if(m_layerDragNode!=node)return;
                    finishLayerDrag();
                    if(*held)QTimer::singleShot(0,this,
                        [this,node,moved,drop]() {
                            if(moved)moveLayerAtFinger(node,drop);
                            else {
                                m_vm->nodeManager()->slotUiActivatedNode(node);
                                openLayerActions(node);
                            }
                        });
                });
                connect(tile,&QToolButton::clicked,this,[this,node,held]() {
                    if(*held) {*held=false;return;}
                    const bool wasActive=m_vm->activeNode()==node;
                    m_vm->nodeManager()->slotUiActivatedNode(node);
                    if(wasActive)openLayerActions(node);
                    else QTimer::singleShot(0,this,[this](){refreshLayers();});
                });
                const int overlaySize=qBound(20,rowWidth/4,38);
                const int iconX=rowWidth-overlaySize+1;
                const int iconGap=compactRow ?
                    qMax(2,(rowHeight-overlaySize)/2) :
                    qMax(2,(rowWidth-3*overlaySize)/4);
                auto *eye=new QToolButton(frame);
                eye->setGeometry(iconX,iconGap,overlaySize,overlaySize);
                eye->setIconSize(QSize(overlaySize-5,overlaySize-5));
                eye->setIcon(KisIconUtils::loadIcon(
                    node->visible()?QStringLiteral("visible"):
                                    QStringLiteral("hidden")));
                eye->setToolTip(i18n("Toggle visibility"));
                eye->setStyleSheet(node->visible()?
                    "QToolButton{border:none;background:#777;border-radius:5px;}":
                    "QToolButton{border:1px solid #ccc;background:#555;border-radius:5px;}");
                connect(eye,&QToolButton::clicked,this,[this,node]() {
                    m_vm->nodeManager()->slotUiActivatedNode(node);
                    m_vm->nodeManager()->toggleVisibility();
                    QTimer::singleShot(0,this,[this](){refreshLayers();});
                });
                auto *lock=new QToolButton(frame);
                lock->setGeometry(iconX,iconGap*2+overlaySize,
                                   overlaySize,overlaySize);
                lock->setVisible(!compactRow);
                lock->setIcon(lockIcon(node->userLocked()));
                lock->setIconSize(QSize(overlaySize-4,overlaySize-4));
                lock->setToolTip(i18n("Toggle layer lock"));
                lock->setStyleSheet(node->userLocked()?
                    "QToolButton{background:#417a89;border:1px solid white;border-radius:5px;}":
                    "QToolButton{background:#666;border:0;border-radius:5px;}");
                connect(lock,&QToolButton::clicked,this,[this,node]() {
                    m_vm->nodeManager()->slotUiActivatedNode(node);
                    m_vm->nodeManager()->toggleLock();
                    QTimer::singleShot(0,this,[this](){refreshLayers();});
                });
                if(!compactRow)if(auto *paint=qobject_cast<KisPaintLayer*>(node.data())) {
                    auto *alpha=new QToolButton(frame);
                    alpha->setGeometry(iconX,iconGap*3+overlaySize*2,
                                       overlaySize,overlaySize);
                    alpha->setText(paint->alphaLocked()?
                                   QStringLiteral("α"):QStringLiteral("α̸"));
                    alpha->setToolTip(i18n("Toggle alpha lock"));
                    alpha->setStyleSheet(paint->alphaLocked()?
                        "QToolButton{background:#417a89;color:white;"
                        "border:1px solid white;border-radius:5px;}":
                        "QToolButton{background:#666;color:white;"
                        "border:0;border-radius:5px;}");
                    connect(alpha,&QToolButton::clicked,this,[this,node]() {
                        m_vm->nodeManager()->slotUiActivatedNode(node);
                        m_vm->nodeManager()->toggleAlphaLock();
                        QTimer::singleShot(0,this,[this](){refreshLayers();});
                    });
                }
                m_layersLayout->addWidget(frame);
                m_compactLayerRows.append({
                    node,frame,tile,depth,layerAppearanceKey(node),isGroup});
                if(node->childCount() && (!isGroup || !node->collapsed()))
                    appendChildren(node,depth+1);
            }
        };
        appendChildren(image->root(),0);
        m_layersLayout->addStretch(1);
        m_visibleLayerCount=count;
        m_lastLayerImage=image.data();
        m_lastLayerActive=active;
        QTimer::singleShot(0,this,[this,oldScroll]() {
            if(m_layersScroll)m_layersScroll->verticalScrollBar()->setValue(oldScroll);
            refreshVisibleLayerThumbnails(false);
        });
        m_layerName->setText(active?active->name().left(14):QString());
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
        if(node->inherits("KisLayer")) {
            const bool inherit=KisLayerPropertiesIcons::nodeProperty(
                node,KisLayerPropertiesIcons::inheritAlpha,false).toBool();
            auto *clip=new QCheckBox(i18n("Clip to nearest layer below"),&dialog);
            clip->setChecked(inherit);
            layout->addWidget(clip);
            connect(clip,&QCheckBox::toggled,&dialog,[this,node](bool checked) {
                setCompactClip(node,checked);
            });
        }
        if(node->inherits("KisGroupLayer")) {
            const bool pass=KisLayerPropertiesIcons::nodeProperty(
                node,KisLayerPropertiesIcons::passThrough,false).toBool();
            auto *through=new QCheckBox(i18n("Pass-through group"),&dialog);
            through->setChecked(pass);
            layout->addWidget(through);
            connect(through,&QCheckBox::toggled,&dialog,[this,node](bool checked) {
                if(auto image=m_vm->image().toStrongRef())
                    KisLayerPropertiesIcons::setNodePropertyAutoUndo(
                        node,KisLayerPropertiesIcons::passThrough,
                        checked,image);
                QTimer::singleShot(100,this,[this](){refreshLayers();});
            });
        }
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
            {"Clip","toggle_layer_inherit_alpha"},{"Select","select_opaque"},{"Mask","add_new_transparency_mask"},
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
                else if(i==6)setCompactClip(node,!compactInheritsAlpha(node));
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
                    addNative(&options,QStringLiteral("raise_layer"));
                    addNative(&options,QStringLiteral("lower_layer"));
                    QMenu *addMenu=options.addMenu(i18n("Add layer or mask"));
                    addNative(addMenu,QStringLiteral("add_new_paint_layer"));
                    addNative(addMenu,QStringLiteral("add_new_group_layer"));
                    addNative(addMenu,QStringLiteral("add_new_clone_layer"));
                    addNative(addMenu,QStringLiteral("add_new_transparency_mask"));
                    addNative(addMenu,QStringLiteral("add_new_filter_mask"));
                    addNative(addMenu,QStringLiteral("add_new_transform_mask"));
                    addNative(addMenu,QStringLiteral("add_new_colorize_mask"));
                    addNative(addMenu,QStringLiteral("add_new_selection_mask"));
                    addNative(addMenu,QStringLiteral("add_new_vector_layer"));
                    addNative(addMenu,QStringLiteral("add_new_fill_layer"));
                    addNative(addMenu,QStringLiteral("add_new_file_layer"));
                    QMenu *groupMenu=options.addMenu(i18n("Group and clipping"));
                    addNative(groupMenu,QStringLiteral("create_quick_clipping_group"));
                    addNative(groupMenu,QStringLiteral("quick_group"));
                    addNative(groupMenu,QStringLiteral("quick_ungroup"));
                    // Full original Layer menu, including plugins/operations
                    // not mirrored by this Compact dialog.
                    for(QAction *entry:m_main->menuBar()->actions()) {
                        if(entry && entry->menu() &&
                           entry->text().remove(QLatin1Char('&')).compare(
                               i18n("Layer"),Qt::CaseInsensitive)==0) {
                            options.addSeparator();
                            options.addMenu(entry->menu());
                            break;
                        }
                    }
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
        const int swatchSize=qMax(16,(m_colorWheelPixels-20)/columns);
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
                m_colorWheelPixels+8+m_recent->height());
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
        auto *nativeBars=new QCheckBox(
            i18n("Show native shade sliders and extra color patches"),&dialog);
        nativeBars->setChecked(QSettings().value(
            QStringLiteral("TabletUi/compactColorNativeExtras"),false).toBool());
        layout->addWidget(nativeBars);
        connect(nativeBars,&QCheckBox::toggled,&dialog,[this](bool on) {
            QSettings().setValue(QStringLiteral("TabletUi/compactColorNativeExtras"),on);
            refreshNativeColorSelectorLayout();
        });
        auto *nativeSettings=new QPushButton(
            i18n("Krita wheel type, color model and selector settings..."),&dialog);
        layout->addWidget(nativeSettings);
        connect(nativeSettings,&QPushButton::clicked,&dialog,[this]() {
            QPointer<QDockWidget> docker=m_colorDocker;
            QTimer::singleShot(0,this,[docker]() {
                if(docker)QMetaObject::invokeMethod(
                    docker.data(),"slotOpenSettings",Qt::DirectConnection);
            });
        });
        layout->addWidget(new QLabel(i18n("Rotate hue ring"),&dialog));
        auto *rotation=new QSlider(Qt::Horizontal,&dialog);
        rotation->setRange(0,359);
        const int initialRotation=QSettings().value(
            QStringLiteral("TabletUi/colorHueRotationDegrees"),0).toInt();
        rotation->setValue(qBound(0,initialRotation,359));
        rotation->setToolTip(i18n(
            "Rotate the hue ring without changing the selected paint color"));
        layout->addWidget(rotation);
        auto *rotationValue=new QLabel(
            QStringLiteral("%1°").arg(rotation->value()),&dialog);
        layout->addWidget(rotationValue);
        connect(rotation,&QSlider::valueChanged,&dialog,
                [this,rotationValue](int degrees) {
            rotationValue->setText(QStringLiteral("%1°").arg(degrees));
            QSettings().setValue(
                QStringLiteral("TabletUi/colorHueRotationDegrees"),degrees);
            refreshNativeColorSelectorLayout();
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
                pixels,pixels+8+m_recent->height());
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
    void refreshNativeColorSelectorLayout() {
        if(m_colorDocker) {
            QMetaObject::invokeMethod(m_colorDocker.data(),
                "slotConfigurationChanged",Qt::DirectConnection);
            // No cross-library visual-selector symbols in Krita's main UI.
            // The hue-ring widget detects its own changed rotation on paint.
            for(QWidget *child:m_colorDocker->findChildren<QWidget*>())
                child->update();
        }
        if(m_compact && m_colorExpanded)
            QTimer::singleShot(0,this,[this](){updateColorDock();});
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
        if(QWidget *selector=m_colorDocker->widget()) {
            if(auto *layout=dynamic_cast<QBoxLayout*>(selector->layout())) {
                const int desired=m_colorHistoryOnTop?0:layout->count()-1;
                if(layout->indexOf(m_recent)!=desired) {
                    layout->removeWidget(m_recent);
                    layout->insertWidget(
                        m_colorHistoryOnTop?0:layout->count(),m_recent);
                }
            }
        }
        m_recent->show();
        if(m_colorPinButton)m_colorPinButton->show();
    }
    void positionOverlays() {
        if(!m_compact)return;
        const int w=m_surface->width();
        const int h=m_surface->height();
        m_brushStrip->adjustSize();
        m_brushStrip->move(8,qMax(0,h-m_brushStrip->height()-8));
        m_brushControls->move(8,qMax(0,h-m_brushStrip->height()-
                                      m_brushControls->height()-11));
        const int layerX=m_layerOnRight?qMax(4,w-m_layerRail->width()-4):4;
        const int layerY=35;
        const int rowsHeight=qMax(m_layerTileSize+4,m_compactLayerHeight);
        const int desired=5+38+3+rowsHeight+3+34+5+12;
        // Keep the rail above the brush panel when both share the left side.
        // On the right, also avoid a pinned color selector at the bottom.
        int availableBottom=h-8;
        if(!m_layerOnRight)
            availableBottom=qMin(availableBottom,m_brushControls->y()-10);
        else if(m_colorExpanded && m_colorDocker && m_colorDocker->isVisible())
            availableBottom=qMin(availableBottom,h-m_colorDocker->height()-12);
        const int availableHeight=qMax(0,availableBottom-layerY);
        m_layerRail->setGeometry(layerX,layerY,m_layerRail->width(),
                                 qMin(availableHeight,qMax(130,desired)));
        m_layerToggleButton->move(m_layerOnRight?qMax(6,w-54):6,42);
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
        // The welcome page is not a painting workspace: don't render the
        // dedicated Compact drawing toolbar over Recent Images / Project Gallery.
        // It returns automatically once a document is active.
        // QMdiArea::activeSubWindow() may temporarily be null after
        // toggling document view modes; the image/view manager is authoritative.
        const bool documentOpen=m_vm && m_vm->activeNode() &&
            !m_vm->image().toStrongRef().isNull();
        if(m_compactBar)m_compactBar->setVisible(m_compact && documentOpen);
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
    bool m_normalMenuVisible=true;
    bool m_normalStatusVisible=true;
    bool m_layerPenTracking=false;
    bool m_clipTransactionBusy=false;
    KisImageSP m_clipWorkingImage;
    KisNodeSP m_clipRestoreActive;
    QList<KisNodeSP> m_clipRebuildParents;
    QList<KisNodeSP> m_clipPendingWrappers;
    KisNodeSP m_clipPendingMoved;
    KisNodeSP m_clipExpectedParent;
    KisNodeSP m_clipExpectedBelow;
    bool m_layerScrollerSuspended=false;
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
    QWidget *m_sizeRow=nullptr;
    QWidget *m_opacityRow=nullptr;
    QWidget *m_compositeRow=nullptr;
    KisDoubleSliderSpinBox *m_brushSizeSlider=nullptr;
    KisDoubleSliderSpinBox *m_brushOpacitySlider=nullptr;
    KisDoubleSliderSpinBox *m_brushFlowSlider=nullptr;
    QComboBox *m_brushComposite=nullptr;
    QTimer *m_brushControlTimer=nullptr;
    bool m_syncingBrushControls=false;
    bool m_brushPrefsLoaded=false;
    bool m_brushControlEnabled[4]={true,true,true,true};
    QString m_cachedSlotIconNames[3];
    bool m_cachedSlotIconInitialized[3]={false,false,false};
    bool m_cachedPaintHighlighted[3]={false,false,false};
    bool m_cachedPaintStyleInitialized[3]={false,false,false};
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
    int m_compactLayerHeight=0;
    QList<CompactLayerRow> m_compactLayerRows;
    KisImage *m_lastLayerImage=nullptr;
    KisNodeSP m_lastLayerActive;
    QTimer *m_layerPreviewTimer=nullptr;
    QHash<int,CompactTouchPoint> m_layerTouches;
    QTimer *m_layerTouchHoldTimer=nullptr;
    QTimer *m_layerEdgeScrollTimer=nullptr;
    KisNodeSP m_touchLayerPrimary;
    KisNodeSP m_touchPinchA;
    KisNodeSP m_touchPinchB;
    QPoint m_touchLayerLast;
    qreal m_touchPinchStartDistance=0;
    bool m_touchLayerArmed=false;
    bool m_touchLayerMoved=false;
    bool m_touchLayerScrolling=false;
    bool m_touchLayerPinching=false;
    bool m_touchPinchReady=false;
    KisNodeSP m_layerDragNode;
    QPointer<QWidget> m_layerDragTile;
    QLabel *m_layerDragGhost=nullptr;
    QFrame *m_layerDropMarker=nullptr;
    QPoint m_layerDragStart;
    QPoint m_layerDragLast;
    bool m_layerDragArmed=false;
    bool m_layerDragMoved=false;
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
    QMenu *m_hubMenu=nullptr;
    QAction *m_toolsAction=nullptr;
    QAction *m_sampleModeAction=nullptr;
    QToolButton *m_toolOptionsFloating=nullptr;
    KisToolOptionsPopup *m_compactToolPopup=nullptr;
    QList<QPointer<QToolBar>> m_originalToolbars;
    QList<QPointer<QDockWidget>> m_compactNativeDockers;
    QTimer *m_layerRefreshTimer=nullptr;
    KisResourceModel *m_presetModel=nullptr;
};

#endif // Q_OS_ANDROID
