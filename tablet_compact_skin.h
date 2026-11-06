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
#include <QListWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
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

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"
#include "kis_node_manager.h"
#include "kis_node.h"
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

        m_layerRail = new QFrame(m_surface);
        m_layerRail->setObjectName(QStringLiteral("tabletLayerRail"));
        m_layerRail->setStyleSheet(
            "#tabletLayerRail{background:rgba(31,31,34,230);border-radius:9px;}");
        auto *rail = new QVBoxLayout(m_layerRail);
        rail->setContentsMargins(3,5,3,5);
        rail->setSpacing(3);
        m_addLayer = new QToolButton(m_layerRail);
        m_addLayer->setText(QStringLiteral("+"));
        m_addLayer->setFixedSize(62,40);
        m_addLayer->setToolTip(i18n("Tap: new paint layer. Hold: layer and mask types"));
        rail->addWidget(m_addLayer);
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
        m_layersScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");
        m_layersContent = new QWidget(m_layersScroll);
        m_layersLayout = new QVBoxLayout(m_layersContent);
        m_layersLayout->setContentsMargins(0,0,0,0);
        m_layersLayout->setSpacing(3);
        m_layersScroll->setWidget(m_layersContent);
        rail->addWidget(m_layersScroll,1);
        m_layerName = new QLabel(m_layerRail);
        m_layerName->setAlignment(Qt::AlignCenter);
        m_layerName->setWordWrap(true);
        m_layerName->setFixedHeight(34);
        m_layerName->setStyleSheet("color:white;font-size:9px;");
        rail->addWidget(m_layerName);
        m_layerRail->setFixedWidth(74);

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
        m_colorSizeButton = new QToolButton(m_surface);
        m_colorSizeButton->setFixedSize(38,38);
        m_colorSizeButton->setText(QStringLiteral("↗"));
        m_colorSizeButton->setToolTip(i18n("Resize color wheel"));
        m_colorSizeButton->setStyleSheet(
            "QToolButton{background:#252528;color:white;border:1px solid #999;"
            "border-radius:10px;font-size:22px;}");
        connect(m_colorSizeButton, &QToolButton::clicked, this, [this]() {
            m_colorSizeStep = (m_colorSizeStep+1)%3;
            QSettings().setValue(QStringLiteral("TabletUi/colorWheelSize"),m_colorSizeStep);
            if(m_colorDocker) {
                const int w=QList<int>{225,265,330}.at(m_colorSizeStep);
                m_colorDocker->resize(w,w+70);
                updateColorDock();
            }
        });
        m_recent = new QFrame(m_surface);
        m_recent->setObjectName(QStringLiteral("tabletRecentColors"));
        m_recent->setStyleSheet(
            "#tabletRecentColors{background:rgba(30,30,33,225);border-radius:5px;}");
        m_recentLayout = new QHBoxLayout(m_recent);
        m_recentLayout->setContentsMargins(3,3,3,3);
        m_recentLayout->setSpacing(2);
        m_recent->setFixedHeight(26);

        m_topBar = new QToolBar(i18n("Compact tools"), m_main);
        m_topBar->setObjectName(QStringLiteral("tabletCompactPopupBar"));
        m_topBar->setMovable(false);
        m_topBar->setFloatable(false);
        m_topBar->setIconSize(QSize(22,22));
        auto *presetAction = m_topBar->addAction(
            KisIconUtils::loadIcon("paintop_settings_01"), i18n("All brush presets"));
        presetAction->setToolTip(i18n("Open Krita's full brush preset chooser"));
        connect(presetAction, &QAction::triggered, this, [this]() {
            QAction *native = m_main->actionCollection()->action(QStringLiteral("show_brush_presets"));
            if (native) native->trigger();
            else if (m_brushDocker) m_brushDocker->show();
        });
        auto *optionsAction = m_topBar->addAction(
            KisIconUtils::loadIcon("view-choose"), i18n("Tool Options"));
        optionsAction->setToolTip(i18n("Open current tool options"));
        connect(optionsAction, &QAction::triggered, this, [this]() {
            QAction *native = m_main->actionCollection()->action(QStringLiteral("show_tool_options"));
            if (native) native->trigger();
            else if(m_toolOptionsDocker) {
                const bool wasVisible=m_toolOptionsDocker->isVisible();
                m_toolOptionsDocker->setVisible(!wasVisible);
                if(!wasVisible) m_toolOptionsDocker->raise();
            }
        });
        m_main->addToolBar(Qt::TopToolBarArea,m_topBar);
        m_topBar->hide();

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
        m_brushStrip->hide();
        m_colorButton->hide();
        m_recent->hide();
        m_colorSizeButton->hide();
        m_colorSizeStep = QSettings().value(
            QStringLiteral("TabletUi/colorWheelSize"),1).toInt();
        m_colorSizeStep=qBound(0,m_colorSizeStep,2);
        m_colorPinned=QSettings().value(QStringLiteral("TabletUi/colorPinned"),false).toBool();
        // Only explicit pinning keeps the selector open after restart.
        m_colorExpanded=m_colorPinned;
    }

    bool eventFilter(QObject *watched,QEvent *event) override {
        if ((watched==m_surface || watched==m_mdi) &&
             (event->type()==QEvent::Resize || event->type()==QEvent::Show)) {
            QTimer::singleShot(0,this,[this](){positionOverlays();});
        }
        if (watched==m_colorDocker && event->type()==QEvent::Resize &&
            m_colorPinButton) {
            m_colorPinButton->move(7,30);
            m_colorPinButton->raise();
        }
        if (m_compact && m_colorExpanded && !m_colorPinned &&
            (event->type()==QEvent::MouseButtonPress ||
             event->type()==QEvent::TouchBegin)) {
            QWidget *target=qobject_cast<QWidget*>(watched);
            // Keep interacting with the wheel, its pin or size controls.
            const bool wheelTouch=m_colorDocker && target &&
                (target==m_colorDocker || m_colorDocker->isAncestorOf(target));
            const bool controlTouch=target &&
                (target==m_colorButton || target==m_colorSizeButton);
            if(target && !wheelTouch && !controlTouch) {
                m_colorExpanded=false;
                updateColorDock();
                refreshColorControls();
            }
        }
        return QObject::eventFilter(watched,event);
    }

    void setCompactActive(bool active) {
        if (m_compact==active) return;
        m_compact=active;
        if (active) {
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
            // Restore pinned wheel only, not a transient open wheel.
            m_colorExpanded=m_colorPinned;
            hideCompactToolbarClutter();
            for(QDockWidget *dock:{m_layerDocker.data(),m_brushDocker.data(),
                                   m_toolOptionsDocker.data()}) {
                if(dock) dock->hide();
            }
            // The original color wheel and its color history are reused.
            // Its floating state is restored on return to normal Krita.
            if(m_colorDocker) {
                m_colorDocker->hide();
                m_colorDocker->setFloating(true);
                const int wheelWidth=QList<int>{225,265,330}.at(m_colorSizeStep);
                m_colorDocker->resize(QSize(wheelWidth,wheelWidth+70));
                if(!m_colorPinButton) {
                    m_colorPinButton=new QToolButton(m_colorDocker);
                    m_colorPinButton->setFixedSize(42,34);
                    m_colorPinButton->setStyleSheet(
                        "QToolButton{background:#242428;color:white;"
                        "border:1px solid #aaa;border-radius:6px;font-size:18px;}");
                    m_colorPinButton->setToolTip(i18n("Pin or unpin color wheel"));
                    m_colorDocker->installEventFilter(this);
                    connect(m_colorPinButton,&QToolButton::clicked,this,[this]() {
                        m_colorPinned=!m_colorPinned;
                        QSettings().setValue(QStringLiteral("TabletUi/colorPinned"),
                                             m_colorPinned);
                        // Unpinning returns immediately to the collapsed button.
                        m_colorExpanded=m_colorPinned;
                        updateColorDock();
                        refreshColorControls();
                    });
                }
                m_colorPinButton->move(7,30);
                m_colorPinButton->setText(m_colorPinned ? QStringLiteral("🔒") :
                                                         QStringLiteral("🔓"));
            }
            m_topBar->show();
            m_layerRefreshTimer->start();
        } else {
            m_layerRefreshTimer->stop();
            restoreCompactToolbarClutter();
            m_topBar->hide();
            if(m_colorDocker) {
                m_colorDocker->hide();
                m_colorDocker->setFloating(false);
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
            // Restoring a saved main-window state can make this toolbar visible.
            m_topBar->hide();
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
    void hideCompactToolbarClutter() {
        m_hiddenActions.clear();
        for(QToolBar *bar : m_main->findChildren<QToolBar*>()) {
            if(bar == m_topBar)continue;
            const QList<QAction*> originalActions=bar->actions();
            for(QAction *action : originalActions) {
                if(!action || !action->isVisible())continue;
                const QString id=action->objectName().toLower();
                const QString title=(action->text()+QStringLiteral(" ")+
                                     action->toolTip()).toLower();
                if(id.contains(QStringLiteral("mirror")) ||
                   id.contains(QStringLiteral("wrap")) ||
                   id.contains(QStringLiteral("workspace")) ||
                   title.contains(QStringLiteral("mirror horizontal")) ||
                   title.contains(QStringLiteral("mirror vertical")) ||
                   title.contains(QStringLiteral("wrap around")) ||
                   title.contains(QStringLiteral("workspace"))) {
                    const int index=originalActions.indexOf(action);
                    QAction *next=index+1<originalActions.size() ?
                        originalActions.at(index+1):nullptr;
                    m_hiddenActions.append({bar,action,next});
                    bar->removeAction(action);
                }
            }
        }
    }
    void restoreCompactToolbarClutter() {
        // Reinsert from the end to preserve the original toolbar order.
        for(int i=m_hiddenActions.size()-1;i>=0;--i) {
            const HiddenToolbarAction &item=m_hiddenActions.at(i);
            if(item.bar && item.action)
                item.bar->insertAction(item.before,item.action);
        }
        m_hiddenActions.clear();
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
        dialog.resize(qMax(270,qMin(425,m_surface->width()-24)),
                      qMax(240,qMin(440,m_surface->height()-35)));
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
                const int idx=candidates.size();
                candidates.append(preset);
                auto *item=new QListWidgetItem(
                    QIcon(QPixmap::fromImage(preset->image())),preset->name(),grid);
                item->setData(Qt::UserRole,idx);
                item->setToolTip(preset->name());
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
            holdTimer.start(550);
        });
        connect(QScroller::scroller(grid->viewport()),&QScroller::stateChanged,
                &dialog,[&holdTimer](QScroller::State state) {
            if(state!=QScroller::Inactive)holdTimer.stop();
        });
        connect(&holdTimer,&QTimer::timeout,&dialog,
                [this,slot,grid,all,&candidates,&heldItem,&heldAction,refill,&dialog]() {
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
            QAction *blend=options.addAction(i18n("Add to Blend"));
            QAction *selected=options.exec(QCursor::pos());
            if(!selected)return;
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
                [this,slot,all,&candidates,&dialog,&holdTimer,&heldAction](QListWidgetItem *item) {
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
            frame->setFixedSize(64,67);
            auto *tile=new QToolButton(frame);
            tile->setGeometry(1,1,60,62);
            tile->setToolButtonStyle(Qt::ToolButtonIconOnly);
            tile->setIconSize(QSize(52,52));
            QImage thumb=node->createThumbnail(52,52,Qt::KeepAspectRatio);
            if(!thumb.isNull())tile->setIcon(QIcon(QPixmap::fromImage(thumb)));
            tile->setText(node->name());
            tile->setToolTip(node->name());
            tile->setStyleSheet(node==active ?
                "QToolButton{border:3px solid #e86c79;border-radius:5px;"
                "background:#a4a4a4;}" :
                "QToolButton{border:2px solid #808080;border-radius:5px;"
                "background:#a4a4a4;}");
            connect(tile,&QToolButton::clicked,this,[this,node]() {
                const bool wasActive=m_vm->activeNode()==node;
                m_vm->nodeManager()->slotUiActivatedNode(node);
                if(wasActive)openLayerActions(node);
                else QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
            auto *eye=new QToolButton(frame);
            eye->setGeometry(40,2,20,20);
            eye->setIcon(KisIconUtils::loadIcon(
                node->visible()?QStringLiteral("visible"):
                                QStringLiteral("hidden")));
            eye->setText(node->visible()?QStringLiteral("◉"):QStringLiteral("○"));
            eye->setToolTip(i18n("Toggle layer visibility"));
            eye->setStyleSheet("QToolButton{border:none;background:#777;"
                "color:white;border-radius:10px;}");
            connect(eye,&QToolButton::clicked,this,[this,node]() {
                m_vm->nodeManager()->slotUiActivatedNode(node);
                m_vm->nodeManager()->toggleVisibility();
                QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
            m_layersLayout->addWidget(frame);
        }
        m_layersLayout->addStretch(1);
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
            connect(btn,&QToolButton::clicked,&dialog,[this,node,i,&dialog]() {
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
                dialog.accept();
                QTimer::singleShot(0,this,[this](){refreshLayers();});
            });
        }
        layout->addLayout(grid);
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
            if(item->widget())item->widget()->deleteLater();
            delete item;
        }
        const QList<KoColor> colors=provider->colorHistory();
        const int maxColors=qMin(8,colors.size());
        for(int i=0;i<maxColors;++i) {
            const QColor color=colors.at(i).toQColor();
            auto *swatch=new QToolButton(m_recent);
            swatch->setFixedSize(20,20);
            swatch->setStyleSheet(QStringLiteral(
                "QToolButton{background:%1;border:1px solid white;border-radius:3px;}")
                .arg(color.name()));
            swatch->setToolTip(color.name());
            connect(swatch,&QToolButton::clicked,this,[provider,color]() {
                provider->setFGColor(KoColor(color,provider->fgColor().colorSpace()));
            });
            m_recentLayout->addWidget(swatch);
        }
        m_recent->setFixedWidth(qMax(42,maxColors*23+6));
        positionOverlays();
    }
    void toggleColor() {
        m_colorExpanded=!m_colorExpanded;
        QSettings().setValue(QStringLiteral("TabletUi/colorExpanded"),m_colorExpanded);
        updateColorDock();
        refreshColorControls();
    }
    void refreshColorControls() {
        const bool show=m_compact && m_vm->activeNode() && m_surface->isVisible();
        m_colorButton->setVisible(show && !m_colorPinned);
        m_colorSizeButton->setVisible(show && !m_colorPinned);
        m_recent->setVisible(show && !m_colorPinned && !m_colorExpanded);
        if(m_colorPinButton) {
            m_colorPinButton->setText(m_colorPinned ? QStringLiteral("🔒") :
                                                     QStringLiteral("🔓"));
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
        const int bottomGap=m_colorPinned ? 8 : 95;
        const QPoint global=m_surface->mapToGlobal(
            QPoint(qMax(0,m_surface->width()-m_colorDocker->width()-8),
                   qMax(0,m_surface->height()-m_colorDocker->height()-bottomGap)));
        m_colorDocker->move(global);
        if(m_colorPinButton) {
            m_colorPinButton->move(7,30);
            m_colorPinButton->show();
            m_colorPinButton->raise();
        }
    }
    void positionOverlays() {
        if(!m_compact)return;
        const int w=m_surface->width();
        const int h=m_surface->height();
        m_layerRail->setGeometry(4,35,74,qMax(140,h-125));
        m_brushStrip->adjustSize();
        m_brushStrip->move(8,qMax(0,h-m_brushStrip->height()-8));
        m_colorButton->move(qMax(0,w-58),qMax(0,h-56));
        m_colorSizeButton->move(qMax(0,w-102),qMax(0,h-49));
        m_recent->move(qMax(0,w-m_recent->width()-8),qMax(0,h-90));
        m_layerRail->raise();
        m_brushStrip->raise();
        m_colorButton->raise();
        m_colorSizeButton->raise();
        m_recent->raise();
        if(m_colorExpanded)updateColorDock();
    }
    void refreshVisibility() {
        const bool show=m_compact && m_vm->activeNode() && m_surface->isVisible();
        m_layerRail->setVisible(show);
        m_brushStrip->setVisible(show);
        m_colorButton->setVisible(show && !m_colorPinned);
        m_colorSizeButton->setVisible(show && !m_colorPinned);
        m_recent->setVisible(show && !m_colorPinned && !m_colorExpanded);
        if(show) {
            refreshColors();
            paintSlotIcons();
            positionOverlays();
        }
        updateColorDock();
        refreshColorControls();
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
    QToolButton *m_colorPinButton=nullptr;
    bool m_internalPresetChange=false;
    bool m_slotReady=false;
    int m_slot=0;
    QString m_lastPreset[3];
    QFrame *m_brushStrip=nullptr;
    QToolButton *m_brushButtons[3]={nullptr,nullptr,nullptr};
    QFrame *m_layerRail=nullptr;
    QScrollArea *m_layersScroll=nullptr;
    QWidget *m_layersContent=nullptr;
    QVBoxLayout *m_layersLayout=nullptr;
    QToolButton *m_addLayer=nullptr;
    QTimer *m_addLayerHoldTimer=nullptr;
    bool m_addLayerLongPressed=false;
    QLabel *m_layerName=nullptr;
    QToolButton *m_colorButton=nullptr;
    QToolButton *m_colorSizeButton=nullptr;
    int m_colorSizeStep=1;
    QFrame *m_recent=nullptr;
    QHBoxLayout *m_recentLayout=nullptr;
    QToolBar *m_topBar=nullptr;
    QTimer *m_layerRefreshTimer=nullptr;
    KisResourceModel *m_presetModel=nullptr;
};

#endif // Q_OS_ANDROID
