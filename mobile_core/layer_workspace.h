/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile layers interface: a VIEW/CONTROLLER for Krita's existing node tree.
 * All mutations go through KisNodeManager (native undo/constraints).
 * No second layer compositor, no image copy, no hidden layer docker.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QFrame>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <KoCanvasController.h>
#include <QGridLayout>
#include <QSlider>
#include <QScrollArea>
#include <QMenu>
#include <QInputDialog>
#include <QFont>
#include <QLineEdit>
#include <QScroller>
#include "touch_scroll.h"
#include "touch_action_picker.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QPointer>
#include <QScrollBar>
#include <QBrush>
#include <QColor>
#include <QSet>
#include <QVector>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QSizePolicy>
#include <QAction>
#include <QTimer>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLineF>
#include <QHash>
#include <QToolButton>
#include <QPixmap>
#include <QPainter>
#include <QResizeEvent>
#include <QMap>
#include <QRegion>
#include <algorithm>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QPainterPath>
#include <QIcon>
#include <QMetaObject>
#include <KoToolManager.h>
#include <KoToolBase.h>
#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <functional>

#include "engine_bridge.h"
#include "kis_canvas2.h"
#include "native_layer_reorder_policy.h"
#include "layer_rail_geometry.h"
#include <QSettings>
#include <chrono>
#include <future>
#include <memory>
#include "kis_processing_applicator.h"
#include "commands/kis_image_layer_move_command.h"
#include "commands/KisNodeRenameCommand.h"
#include "kis_node_manager.h"
#include "kis_selection_manager.h"
#include "kis_node.h"
#include "kis_layer.h"
#include "kis_group_layer.h"
#include "kis_paint_layer.h"
#include "kis_layer_properties_icons.h"
#include "kis_image.h"
#include <klocalizedstring.h>

class MobileLayerNameLabel final : public QLabel
{
public:
    explicit MobileLayerNameLabel(QWidget *parent) : QLabel(parent) {}
    void setRotated(bool rotated)
    {
        m_rotated=rotated;
        setSizePolicy(rotated?QSizePolicy::Fixed:QSizePolicy::Ignored,
                      rotated?QSizePolicy::Ignored:QSizePolicy::Fixed);
        setMinimumSize(rotated?19:0,rotated?0:19);
        setMaximumSize(rotated?19:QWIDGETSIZE_MAX,rotated?QWIDGETSIZE_MAX:19);
        update();
    }

    void setRotatedBackground(const QColor &background) {m_background=background;}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if(!m_rotated) {QLabel::paintEvent(event);return;}
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_background);
        painter.drawRoundedRect(rect(),5,5);
        // Read bottom-to-top on the right edge of a horizontal rail.
        painter.translate(0,height());
        painter.rotate(-90);
        painter.setPen(QColor("#edf5fb"));
        painter.drawText(QRect(0,0,height(),width()),Qt::AlignCenter,text());
    }

private:
    bool m_rotated=false;
    QColor m_background=QColor("#252e39");
};

class MobileLayerTileButton final : public QToolButton
{
public:
    MobileLayerTileButton(QWidget *parent,bool rotated) : QToolButton(parent),m_rotated(rotated) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if(!m_rotated) {QToolButton::paintEvent(event);return;}
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.rect=QRect(0,0,height(),width());
        QPainter painter(this);
        painter.translate(0,height());
        painter.rotate(-90);
        style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,this);
    }

private:
    bool m_rotated;
};

class KritaMobileLayersPanel final : public QFrame
{
public:
    static constexpr int compactRailWidth() {return 82;}

    KritaMobileLayersPanel(const MobileEngineBridge &engine,
                           QWidget *parent=nullptr,bool compactRail=false)
        : QFrame(parent),m_engine(engine),m_compactRail(compactRail)
    {
        m_thumbnailClock.start();
        setObjectName(QStringLiteral("MobileStudioLayersPanel"));
        // Match the earlier TabletCompactSkin rail, not Krita's
        // full-width desktop layer docker. Additional operations live
        // behind the selected tile and the hold-to-open Add menu.
        setMinimumWidth(compactRail?compactRailWidth():110);
        setMaximumWidth(166);
        setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
        setStyleSheet(
            "QFrame#MobileStudioLayersPanel{background:rgba(28,31,37,174);"
            "border:1px solid #48484e;border-radius:13px;}"
            "QLabel{color:#ecf3fc;font-size:14px;}"
            "QTreeWidget{background:transparent;color:#f4f4f5;"
            "border:0;font-size:14px;}"
            "QTreeWidget::item{padding:0;}"
            "QScrollBar:vertical{width:5px;background:transparent;}"
            "QScrollBar::handle:vertical{background:#71717a;min-height:18px;border-radius:2px;}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
            "QHeaderView::section{background:#364b5e;color:#e2effa;"
            "font-size:12px;border:0;padding:5px;}"
            "QPushButton{background:#34343a;color:#f4f4f5;"
            "border:1px solid #55555c;border-radius:8px;"
            "min-height:44px;padding:0 4px;font-size:13px;}"
            "QPushButton:pressed{background:#5685a6;}"
            "QPushButton:disabled{color:#8696a3;background:#303d48;}");
        auto *layout=new QVBoxLayout(this);
        // Compact rails should hug their thumbnails instead of carrying
        // desktop docker padding along their short axis.
        layout->setContentsMargins(compactRail?1:2,compactRail?1:2,
                                  compactRail?1:2,compactRail?1:2);
        layout->setSpacing(1);
        if(!compactRail) {
            auto *close=new QPushButton(i18n("Close Layers"),this);
            layout->addWidget(close);
            connect(close,&QPushButton::clicked,this,[this]() {
                if(onRequestClose)onRequestClose();
                else hide();
            });
        }

        m_tree=new QTreeWidget(this);
        // Restore the old overlay's Infinite Painter-like tile rows:
        // actual layer thumbnails, full-sized clipped rows, clear nested
        // group boundaries, and inline pen/finger controls. The Krita
        // node hierarchy remains the one and only document model.
        m_tree->setColumnCount(1);
        m_tree->setHeaderHidden(true);
        m_tree->setRootIsDecorated(false);
        // Row stripes already encode group depth, leaving the full tile
        // width available to nested layers and their inline controls.
        m_tree->setIndentation(compactRail?0:4);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // The compact tile handler owns touch scrolling and drag gestures.
        // QScroller otherwise steals a long press before it can reorder.
        m_tree->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,true);
        m_tree->viewport()->installEventFilter(this);
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tree->header()->setStretchLastSection(true);
        m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        QObject::connect(m_tree->verticalScrollBar(),
                         &QScrollBar::valueChanged,this,
                         [this](int) {updateVisibleLayerThumbnails();});
        m_railLayout=new QBoxLayout(QBoxLayout::TopToBottom);
        m_railLayout->setContentsMargins(0,0,0,0);
        m_railLayout->setSpacing(0);
        m_railLayout->addWidget(m_tree,1);
        layout->addLayout(m_railLayout,1);
        if(compactRail) {
            m_layerName=new MobileLayerNameLabel(this);
            m_layerName->setObjectName(QStringLiteral("mobileLayerName"));
            m_layerName->setStyleSheet(QStringLiteral(
                "QLabel#mobileLayerName{background:#252e39;color:#edf5fb;"
                "border-radius:5px;font-size:11px;padding:0 4px;}"));
            m_layerName->setAlignment(Qt::AlignCenter);
            m_layerName->setFixedHeight(19);
            m_layerName->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
            m_railLayout->addWidget(m_layerName);
        }
        m_dragGhost=new QLabel(m_tree->viewport());
        m_dragGhost->setFixedSize(60,60);
        m_dragGhost->setAlignment(Qt::AlignCenter);
        m_dragGhost->setStyleSheet(
            "QLabel{background:#354858;border:2px solid #8fd5e7;"
            "border-radius:6px;}");
        m_dragGhost->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dragGhost->hide();
        m_dropMarker=new QFrame(m_tree->viewport());
        m_dropMarker->setStyleSheet(
            "QFrame{background:#56c6a7;border:none;}");
        m_dropMarker->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dropMarker->hide();
        m_touchHoldTimer=new QTimer(this);
        m_touchHoldTimer->setSingleShot(true);
        m_touchHoldTimer->setInterval(450);
        connect(m_touchHoldTimer,&QTimer::timeout,this,[this]() {
            if(m_touches.size()==1 && !m_touchScrolling &&
               !m_touchPinching && m_touchPrimary) {
                m_touchHeld=true;
                showLayerDropPreview(m_touchPrimary,m_touchLast);
            }
        });
        m_edgeScrollTimer=new QTimer(this);
        m_edgeScrollTimer->setInterval(40);
        connect(m_edgeScrollTimer,&QTimer::timeout,this,[this]() {
            if(!m_tree || (!m_mouseHeld && !m_touchHeld))return;
            const QPoint global=m_touchHeld?m_touchLast:m_mouseLast;
            const auto *viewport=railViewport();
            const int at=along(viewport->mapFromGlobal(global));
            QScrollBar *bar=railScrollBar();
            const int extent=m_horizontalRail?viewport->width():viewport->height();
            if(at<26)bar->setValue(bar->value()-18);
            else if(at>extent-26)
                bar->setValue(bar->value()+18);
            showLayerDropPreview(m_touchHeld?m_touchPrimary:m_mouseNode,
                                 global);
        });
        // Keep native Undo/Redo accessible while the live panel is open.
        // These are Krita's existing document actions, never a separate
        // layer-only undo stack.
        auto *history=new QHBoxLayout;
        auto *undo=new QPushButton(i18n("Undo"),this);
        auto *redo=new QPushButton(i18n("Redo"),this);
        history->addWidget(undo,1);
        history->addWidget(redo,1);
        layout->addLayout(history);
        const auto wireHistory=[this](QPushButton *button,const char *id,
                                      const QString &fallback) {
            QAction *action=m_engine.action(id);
            if(!action) {
                button->setEnabled(false);
                return;
            }
            QPointer<QAction> guarded(action);
            auto update=[button,guarded,fallback]() {
                if(!guarded) {
                    button->setText(fallback);
                    button->setEnabled(false);
                    return;
                }
                button->setEnabled(guarded->isEnabled());
                const QString name=guarded->text().remove(QLatin1Char('&'));
                button->setText(name.isEmpty()?fallback:name);
                button->setToolTip(name);
            };
            connect(action,&QAction::changed,this,update);
            connect(button,&QPushButton::clicked,this,[this,guarded]() {
                if(!guarded || !guarded->isEnabled())return;
                guarded->trigger();
                // Node tree changes can be delivered asynchronously by Krita.
                // Refresh immediately and after the undo stroke completes.
                QTimer::singleShot(0,this,[this](){reload();});
                QTimer::singleShot(180,this,[this](){reload();});
            });
            update();
        };
        wireHistory(undo,"edit_undo",i18n("Undo"));
        wireHistory(redo,"edit_redo",i18n("Redo"));
        auto *hint=new QLabel(i18n(
            "Layer selection is not recorded in Undo; edits are."),this);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#afc4d6;font-size:11px;"));
        layout->addWidget(hint);

        auto *creation=new QHBoxLayout;
        addButton(creation,i18n("+ Paint"),[this]() {
            auto *view=m_engine.manager();
            const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
            if(!prepareNodeCreation(image,false))return;
            if(auto *m=manager())
                if(m->createNode(QStringLiteral("KisPaintLayer")))reload();
        });
        addButton(creation,i18n("New empty group"),[this]() {
            auto *view=m_engine.manager();
            const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
            if(!prepareNodeCreation(image,false))return;
            if(auto *m=manager())
                if(m->createNode(QStringLiteral("KisGroupLayer")))reload();
        });
        addButton(creation,i18n("Duplicate"),[this]() {
            auto *view=m_engine.manager();
            const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
            if(!prepareNodeCreation(image,false))return;
            if(auto *m=manager()) {
                m->duplicateActiveNode();
                reload();
            }
        });
        addButton(creation,i18n("Delete"),[this]() {
            deleteLayers(selectedTargets());
        });
        layout->addLayout(creation);

        // Preserve access to the full range of Krita node classes,
        // rather than adding separate renderers or per-type models.
        // These actions invoke KisNodeManager::createNode(), including
        // Krita's built-in dialogs and undo/redo for each node type.
        auto *addType=new QPushButton(QStringLiteral("+"),this);
        addType->setAccessibleName(i18n("Add layer"));
        addType->setToolTip(i18n("Add a layer or mask"));
        m_railHeader=new QWidget(this);
        m_railHeaderLayout=new QBoxLayout(QBoxLayout::LeftToRight,m_railHeader);
        m_railHeaderLayout->setContentsMargins(0,0,0,0);
        m_railHeaderLayout->setSpacing(1);
        m_railHeaderLayout->setAlignment(Qt::AlignCenter);
        m_addLayerButton=addType;
        m_railHeaderLayout->addWidget(addType);
        if(compactRail) {
            auto *minimize=new QPushButton(QStringLiteral("−"),m_railHeader);
            minimize->setAccessibleName(i18n("Minimize Layers"));
            minimize->setToolTip(i18n("Minimize Layers"));
            const QString style=QStringLiteral(
                "QPushButton{background:#34343a;color:white;border:1px solid #55555c;"
                "border-radius:5px;min-height:0;padding:0;font-size:16px;}"
                "QPushButton:pressed{background:#5685a6;}");
            addType->setStyleSheet(style);
            minimize->setStyleSheet(style);
            m_minimizeLayerButton=minimize;
            m_railHeaderLayout->addWidget(minimize);
            connect(minimize,&QPushButton::clicked,this,[this]() {
                if(onRequestClose)onRequestClose();
            });
        }
        // The buttons always occupy the rail's SHORT axis: side-by-side
        // for a vertical rail, stacked for a horizontal rail.
        updateRailHeader(82,false,true);
        m_railLayout->insertWidget(0,m_railHeader);
        connect(addType,&QPushButton::clicked,this,[this,addType]() {
            auto *view=m_engine.manager();
            auto *nodes=manager();
            if(!nodes || !view)return;
            const KisImageSP original=view->image().toStrongRef();
            if(!original)return;
            QMenu menu;
            menu.setTitle(i18n("Add layer or mask"));
            const auto heading=[&menu](const QString &title) {
                menu.addAction(title)->setProperty("mobileSectionHeading",true);
            };
            const auto add=[this,&menu](const QString &label,const char *type,const char *nativeId) {
                auto *entry=menu.addAction(label);
                entry->setData(QString::fromLatin1(type));
                if(auto *native=m_engine.action(nativeId)) {
                    entry->setIcon(native->icon());
                    entry->setEnabled(native->isEnabled());
                }
            };
            heading(i18n("Layers"));
            add(i18n("Paint layer"),"KisPaintLayer","add_new_paint_layer");
            add(i18n("Group"),"KisGroupLayer","add_new_group_layer");
            add(i18n("Vector layer"),"KisShapeLayer","add_new_shape_layer");
            add(i18n("Filter layer"),"KisAdjustmentLayer","add_new_adjustment_layer");
            add(i18n("Fill layer"),"KisGeneratorLayer","add_new_fill_layer");
            add(i18n("Clone layer"),"KisCloneLayer","add_new_clone_layer");
            add(i18n("File layer"),"KisFileLayer","add_new_file_layer");
            heading(i18n("Masks"));
            addMaskTypes(&menu,true);
            const KisNodeSP target=creationTarget(original);
            if(!target || !target->isEditable()) {
                for(auto *entry:menu.actions())
                    if(entry->data().toString().endsWith(QStringLiteral("Mask")))entry->setEnabled(false);
            }
            auto *host=parentWidget();
            if(!host)return;
            mobileShowActionList(host,addType,menu,this,[this,original,target](const QVariant &choice) {
                const auto type=choice.toString();
                const bool mask=type.endsWith(QStringLiteral("Mask"));
                if(type.isEmpty() || (mask && creationTarget(original)!=target) ||
                   !prepareNodeCreation(original,mask))return;
                if(auto *nodes=manager())
                    if(nodes->createNode(type))reload();
            });
        });

        auto *operations=new QHBoxLayout;
        addButton(operations,i18n("Up"),[this]() {
            const KisNodeList selected=selectedTargets();
            if(selected.size()==1)reorderNode(selected.first(),true);
        });
        addButton(operations,i18n("Down"),[this]() {
            const KisNodeList selected=selectedTargets();
            if(selected.size()==1)reorderNode(selected.first(),false);
        });
        addButton(operations,i18n("Show / Hide"),[this]() {
            if(auto *m=manager()){m->toggleVisibility();reload();}
        });
        addButton(operations,i18n("Lock / Unlock"),[this]() {
            if(auto *m=manager()){m->toggleLock();reload();}
        });
        layout->addLayout(operations);

        auto *grouping=new QHBoxLayout;
        addButton(grouping,i18n("Group"),[this]() {
            if(auto *m=manager()){m->createQuickGroup();reload();}
        });
        addButton(grouping,i18n("Ungroup"),[this]() {
            if(auto *m=manager()){m->quickUngroup();reload();}
        });
        addButton(grouping,i18n("Properties..."),[this]() {
            if(auto *m=manager()) {
                auto nodes=selectedTargets();
                if(!nodes.isEmpty())m->nodeProperties(nodes.first());
                reload();
            }
        });
        layout->addLayout(grouping);

        // The original overlay's compact rail intentionally had NO
        // persistent duplicate/delete/up/down buttons below the tiles.
        // Their native functionality remains in each tile's action menu.
        hint->hide();
        const auto hideControls=[](QLayout *line) {
            for(int i=0;i<line->count();++i)
                if(QWidget *w=line->itemAt(i)->widget())w->hide();
        };
        hideControls(history);
        hideControls(creation);
        hideControls(operations);
        hideControls(grouping);

        connect(m_tree,&QTreeWidget::itemClicked,this,
                [this](QTreeWidgetItem *item,int column) {
            if(!item)return;
            const int index=item->data(0,Qt::UserRole).toInt();
            if(index<0 || index>=m_nodes.size())return;
            KisNodeSP node=m_nodes.at(index);
            if(!node || !node->parent())return;
            auto *m=manager();
            if(!m)return;
            Q_UNUSED(column);
            tapLayer(node,item);
        });
        reload();
    }

    // Called by the host while the panel is visible. A lightweight
    // native-node signature prevents rebuilding the tree on every tick.
    // We never cache a duplicate document or layer model.
    std::function<void()> onRequestClose;
    std::function<void()> onContentHeightChanged;
    int contentLength(bool horizontal,int thickness) const
    {
        int length=0;
        for(const auto *item:visibleItems()) {
            const auto node=m_nodes.value(item->data(0,Qt::UserRole).toInt());
            if(!node)continue;
            const int cross=horizontal?horizontalTileSize(thickness):verticalTileSize(thickness);
            length+=MobileLayerRailGeometry::tileExtent(cross,horizontal,
                node->inherits("KisGroupLayer"),node->inherits("KisMask"))+4;
        }
        return (horizontal?85:(m_layerName?58:45))+qMax(24,length);
    }

    void updateRailHeader(int thickness,bool horizontal,bool controlsAtEnd)
    {
        if(!m_railHeader || !m_railHeaderLayout || !m_addLayerButton)return;

        // controlsAtEnd indicates the canvas-facing side of layer tiles.
        // Minimize belongs on the OPPOSITE side, closest to the screen edge.
        const bool minimizeFirst=controlsAtEnd;
        m_railHeaderLayout->setDirection(horizontal?
            QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);

        m_railHeaderLayout->removeWidget(m_addLayerButton);
        if(m_minimizeLayerButton)m_railHeaderLayout->removeWidget(m_minimizeLayerButton);
        if(minimizeFirst && m_minimizeLayerButton)
            m_railHeaderLayout->addWidget(m_minimizeLayerButton,0,Qt::AlignCenter);
        m_railHeaderLayout->addWidget(m_addLayerButton,0,Qt::AlignCenter);
        if(!minimizeFirst && m_minimizeLayerButton)
            m_railHeaderLayout->addWidget(m_minimizeLayerButton,0,Qt::AlignCenter);

        const int compactThickness=qMax(horizontal?58:64,thickness);
        if(horizontal) {
            // Top/bottom rail: minimize above/below Add, respectively.
            m_railHeader->setMinimumWidth(0);
            m_railHeader->setFixedWidth(36);
            m_railHeader->setMinimumHeight(0);
            m_railHeader->setMaximumHeight(QWIDGETSIZE_MAX);
            m_addLayerButton->setFixedSize(34,
                qMax(30,compactThickness-(m_minimizeLayerButton?27:4)));
        } else {
            // Left/right rail: minimize left/right of the larger Add.
            m_railHeader->setMinimumHeight(0);
            m_railHeader->setFixedHeight(36);
            m_railHeader->setMinimumWidth(0);
            m_railHeader->setMaximumWidth(QWIDGETSIZE_MAX);
            m_addLayerButton->setFixedSize(
                qMax(30,compactThickness-(m_minimizeLayerButton?29:4)),34);
        }
        if(m_minimizeLayerButton)m_minimizeLayerButton->setFixedSize(23,22);
    }

    void setRailGeometry(const QRect &geometry,bool horizontal,bool controlsAtEnd)
    {
        const bool orientationChanged=horizontal!=m_horizontalRail;
        const int thickness=horizontal?geometry.height():geometry.width();
        const bool rebuild=orientationChanged || controlsAtEnd!=m_controlsAtEnd ||
            thickness!=(horizontal?height():width());
        m_controlsAtEnd=controlsAtEnd;
        if(rebuild)updateRailHeader(thickness,horizontal,controlsAtEnd);
        if(orientationChanged) {
            m_horizontalRail=horizontal;
            if(horizontal && !m_horizontalScroll)createHorizontalRail();
            m_tree->setVisible(!horizontal);
            if(m_horizontalScroll)m_horizontalScroll->setVisible(horizontal);
            m_railLayout->setDirection(horizontal?QBoxLayout::LeftToRight:QBoxLayout::TopToBottom);
            if(m_layerName)m_layerName->setRotated(horizontal);
            m_dragGhost->setParent(railViewport());
            m_dropMarker->setParent(railViewport());
            hideLayerDropPreview();
        }
        if(horizontal) {
            setMinimumWidth(0);
            setMaximumWidth(QWIDGETSIZE_MAX);
        } else if(width()!=geometry.width() || orientationChanged) {
            setFixedWidth(geometry.width());
        }
        setGeometry(geometry);
        updateLayerName();
        if(rebuild)QTimer::singleShot(0,this,[this]() {refreshNow();});
    }

    void syncSelectionContext()
    {
        if(m_deleteImage || m_reloading)return;
        auto *m=manager();
        auto *view=m_engine.manager();
        if(m && view && !allowsMultiple()) {
            const KisNodeSP active=m->activeNode();
            if(inImage(active,view->image().toStrongRef()) &&
               !KisNodeManager::isNodeHidden(active,true) &&
               m->selectedNodes()!=KisNodeList{active}) {
                m_active=active;
                m_selected=KisNodeList{active};
                m->slotSetSelectedNodes(m_selected);
            }
        }
        refreshFromEngine();
    }
    void refreshFromEngine()
    {
        if(!isVisible() || m_deleteImage || m_reorderPending || m_clipBusy ||
           m_clipPropertyPending || m_mouseNode ||
           !m_touches.isEmpty())return;
        if(engineSignature()!=m_signature) {
            reload();
        } else {
            // Painting changes a layer's projection without changing the
            // structural signature. Refresh the active preview at a bounded
            // interval rather than freezing it until the next tree rebuild.
            // Reuse the host's existing refresh cadence (no extra timer).
            const qint64 now=m_thumbnailClock.elapsed();
            if(now-m_lastActiveThumbnailRefresh>=2500) {
                m_lastActiveThumbnailRefresh=now;
                for(int i=0;i<m_nodes.size();++i) {
                    if(m_nodes.at(i)==m_active) {
                        m_thumbnailReady[i]=false;
                        break;
                    }
                }
            }
            updateVisibleLayerThumbnails();
        }
    }
    void refreshNow() {
        if(!m_reorderPending && !m_clipBusy &&
           !m_clipPropertyPending && !m_mouseNode &&
           m_touches.isEmpty())reload();
    }

private:
    void deleteLayers(const KisNodeList &targets)
    {
        auto *view=m_engine.manager();
        KisImageSP image=view?view->image().toStrongRef():KisImageSP();
        if(!image || targets.isEmpty() || m_deleteImage || m_reorderPending ||
           m_clipBusy || m_clipPropertyPending)return;
        const QString prompt=targets.size()==1?
            i18n("Delete %1 and any layers or masks inside it?",targets.first()->name()):
            i18np("Delete %1 selected item and its contents?",
                  "Delete %1 selected items and their contents?",targets.size());
        if(QMessageBox::question(this,i18n("Delete layers"),prompt,
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        view=m_engine.manager();
        if(!view || view->image().toStrongRef()!=image)return;
        if(auto *transform=activeTransform()) {
            if(!canRetarget(transform))return;
            m_engine.freehandBrush();
        }
        auto *nodes=manager();
        if(!nodes || !m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=image ||
           m_deleteImage || m_reorderPending || m_clipBusy || m_clipPropertyPending)return;
        for(const auto &node:targets)
            if(!inImage(node,image) || !node->parent())return;
        if(!nodes->canModifyLayers(targets))return;

        // Krita owns child removal, clone dependencies, replacement of the
        // last paint layer and Undo. Keep the compact tree out of the graph
        // while the native removal and its reselection are in flight.
        m_deleteImage=image;
        setEnabled(false);
        if(targets.size()==1)nodes->removeSingleNode(targets.first());
        else {
            nodes->slotSetSelectedNodes(targets);
            nodes->removeNode();
        }
        image->requestStrokeEnd();
        QTimer::singleShot(0,this,[this]() {finishDelete();});
    }

    void finishDelete()
    {
        auto *view=m_engine.manager();
        if(!view || view->image().toStrongRef()!=m_deleteImage) {
            m_deleteImage.clear();
        } else if(!m_deleteImage->isIdle()) {
            QTimer::singleShot(24,this,[this]() {finishDelete();});
            return;
        } else {
            m_deleteImage.clear();
        }
        setEnabled(true);
        QTimer::singleShot(0,this,[this]() {refreshNow();});
    }

    // Reordering mutates Krita's real node hierarchy. Never move a
    // transform target while an uncommitted preview is active, and
    // recheck document identity after switching back to the brush.
    bool prepareLayerReorder(const KisImageSP &image)
    {
        if(!image || m_reorderPending || m_clipBusy ||
           m_clipPropertyPending)return false;
        if(auto *transform=activeTransform()) {
            if(!canRetarget(transform))return false;
            m_engine.freehandBrush();
        }
        auto *view=m_engine.manager();
        return view && view->image().toStrongRef()==image;
    }

    // A reorder is a Krita image stroke, not a legacy direct-command
    // edit. It enters the same document undo stack used by pen strokes.
    // Node order is bottom-to-top, whereas the displayed tree is
    // top-to-bottom. Use sibling anchors, not the old indexed move API:
    // that API treats "move up one" as a self-anchor and returns false.
    void reorderNode(KisNodeSP node,bool towardTop)
    {
        if(m_reorderPending || m_clipBusy || m_clipPropertyPending ||
           !node)return;
        auto *view=m_engine.manager();
        auto *nodes=manager();
        if(!view || !nodes)return;
        KisImageSP image=view->image().toStrongRef();
        KisNodeSP parent=node->parent();
        if(!image || !image->root() ||
           !parent || !parent->isEditable(false) ||
           !parent->allowAsChild(node))return;

        // Ensure no stale layer from another open document is moved.
        KisNodeSP ancestor=node;
        while(ancestor && ancestor!=image->root())
            ancestor=ancestor->parent();
        if(ancestor!=image->root())return;

        // The old Compact overlay treated clipping groups as invisible.
        // Up/Down must move in the same flat *visual* order as finger/pen
        // dragging; moving only inside Krita's hidden clip wrapper would
        // change a different layer relationship than the user sees.
        KisNodeSP logical=logicalParent(node);
        KisNodeList visual;
        logicalChildren(logical,visual);
        bool hasClips=inheritsAlpha(node);
        for(const KisNodeSP &candidate:visual)
            if(inheritsAlpha(candidate))hasClips=true;
        if(logical!=parent || hasClips) {
            const int index=visual.indexOf(node);
            const int destination=index+(towardTop?1:-1);
            if(index>=0 && destination>=0 && destination<visual.size())
                moveCompactLayer(node,visual.at(destination),towardTop);
            return;
        }

        const auto plan=MobileNativeLayerReorderPolicy::plan(
            parent->index(node),int(parent->childCount()),towardTop);
        if(!plan.valid)return;

        KisNodeSP above=plan.anchorIndex>=0 ?
                        parent->at(plan.anchorIndex) : KisNodeSP();
        if((plan.anchorIndex>=0 && !above) ||
           above==node || (above && above->parent()!=parent))return;

        // Selection is UI state; the following command is the actual
        // undoable document mutation. Never use a different document's
        // image or Krita's desktop node-manager reorder action.
        if(!prepareLayerReorder(image) || !inImage(node,image) ||
           !m_engine.selectLayer(node))return;
        // Leaving Transform and activating the row can dispatch native
        // selection/node signals. Recheck the exact parent and sibling
        // anchor immediately before handing ownership to Krita's stroke.
        auto *currentView=m_engine.manager();
        if(!currentView || currentView->image().toStrongRef()!=image ||
           !inImage(node,image) || node->parent()!=parent ||
           !parent->isEditable(false) || !parent->allowAsChild(node) ||
           (above && (above==node || above->parent()!=parent)) ||
           parent->index(node)<0 ||
           (above && parent->index(above)<0))return;

        runNativeLayerStroke(image,
            new KisImageLayerMoveCommand(image,node,parent,above,true),
            QStringLiteral("Native reorder"));
    }

    void runNativeLayerStroke(KisImageSP image,KUndo2Command *command,
                              const QString &operation)
    {
        if(!image || !command || m_reorderPending) {
            delete command;
            return;
        }
        m_reorderPending=true;
        checkpointMove(operation+QStringLiteral(" stroke queued"));
        KisProcessingApplicator stroke(
            image,KisNodeList(),KisProcessingApplicator::NONE,
            KisImageSignalVector(),command->text());
        stroke.applyCommand(command,KisStrokeJobData::SEQUENTIAL,
                            KisStrokeJobData::EXCLUSIVE);
        auto completion=std::make_shared<std::future<bool>>(
            stroke.successfullyCompletedFuture());
        stroke.end();
        awaitReorder(completion,operation);
    }

    void awaitReorder(const std::shared_ptr<std::future<bool>> &result,
                      const QString &operation)
    {
        // Do not block the Qt UI thread or walk the node graph while
        // Krita is applying/reversing a processing stroke.
        if(!result || !result->valid()) {
            m_reorderPending=false;
            checkpointMove(operation+QStringLiteral(" future unavailable"));
            reload();
            return;
        }
        if(result->wait_for(std::chrono::milliseconds(0))!=
           std::future_status::ready) {
            QTimer::singleShot(40,this,[this,result,operation]() {
                awaitReorder(result,operation);
            });
            return;
        }
        bool succeeded=false;
        try {
            succeeded=result->get();
        } catch(const std::future_error &) {
            succeeded=false;
        }
        m_reorderPending=false;
        checkpointMove(operation+
            (succeeded?QStringLiteral(" stroke committed to Undo"):
                       QStringLiteral(" stroke not completed")));
        reload();
    }

    static void checkpointMove(const QString &stage)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/nativeLayerMoveStage"),
                          stage);
        settings.sync();
    }

    void addMaskTypes(QMenu *menu,bool checkActiveLayer=false)
    {
        const auto add=[&](const QString &label,const char *type,const char *nativeId) {
            auto *entry=menu->addAction(label);
            entry->setData(QString::fromLatin1(type));
            if(auto *native=m_engine.action(nativeId)) {
                entry->setIcon(native->icon());
                if(checkActiveLayer)entry->setEnabled(native->isEnabled());
            }
        };
        add(i18n("Transparency mask"),"KisTransparencyMask","add_new_transparency_mask");
        add(i18n("Filter mask"),"KisFilterMask","add_new_filter_mask");
        add(i18n("Transform mask"),"KisTransformMask","add_new_transform_mask");
        add(i18n("Selection mask"),"KisSelectionMask","add_new_selection_mask");
        add(i18n("Colorize mask"),"KisColorizeMask","add_new_colorize_mask");
        add(i18n("Fast color overlay mask"),"FastColorOverlayFilterMask",
            "add_new_fast_color_overlay_mask");
    }

    void renameLayer(KisNodeSP node,const KisImageSP &image)
    {
        if(!inImage(node,image) || m_deleteImage || m_reorderPending ||
           m_clipBusy || m_clipPropertyPending)return;
        bool ok=false;
        const QString name=QInputDialog::getText(
            this,i18n("Rename layer"),i18n("Layer name"),
            QLineEdit::Normal,node->name(),&ok).trimmed();
        if(!ok || name.isEmpty() || name==node->name() || m_deleteImage ||
           m_reorderPending || m_clipBusy || m_clipPropertyPending ||
           !m_engine.manager() || m_engine.manager()->image().toStrongRef()!=image ||
           !inImage(node,image))return;
        runNativeLayerStroke(image,new KisNodeRenameCommand(node,node->name(),name),
                             QStringLiteral("Native layer rename"));
    }

    void showLayerActions(KisNodeSP node,QTreeWidgetItem *item)
    {
        auto *m=manager();
        auto *view=m_engine.manager();
        const KisImageSP originalImage=view?view->image().toStrongRef():KisImageSP();
        if(!m || !inImage(node,originalImage) || !item)return;
        QMenu menu(this);
        menu.setTitle(i18n("More layer options"));
        QAction *rename=menu.addAction(i18n("Rename layer"));
        QAction *opacityAction=menu.addAction(
            i18n("Opacity… (%1%)").arg(
                (int(node->opacity())*100+127)/255));
        QAction *blendAction=menu.addAction(i18n("Blending mode…"));
        blendAction->setEnabled(node->colorSpace()!=nullptr);
        auto *paint=qobject_cast<KisPaintLayer*>(node.data());
        auto *layer=qobject_cast<KisLayer*>(node.data());
        QAction *alphaLock=menu.addAction(i18n("Alpha lock"));
        alphaLock->setCheckable(true);
        alphaLock->setChecked(paint && paint->alphaLocked());
        alphaLock->setEnabled(paint!=nullptr);
        // Make the actual nearest unclipped base explicit, including
        // groups (whose combined alpha provides the clipping mask).
        const KisNodeSP clipBase=clipBaseFor(node);
        const bool clipped=inheritsAlpha(node);
        QAction *inheritAlpha=menu.addAction(
            clipped ? (clipBase?
                i18n("Clipped to %1").arg(clipBase->name()):
                i18n("Clipping (no base)")):
            clipBase ? i18n("Clip to %1").arg(clipBase->name()):
                       i18n("Clip (no base below)"));
        inheritAlpha->setCheckable(true);
        inheritAlpha->setChecked(clipped);
        // Krita's KisGroupLayer also supports alpha inheritance:
        // its combined children are clipped as a single unit.
        inheritAlpha->setEnabled(layer && (clipped || clipBase));
        QAction *properties=menu.addAction(i18n("Layer properties"));
        menu.addSeparator();
        QAction *duplicate=menu.addAction(i18n("Duplicate"));
        QAction *moveUp=menu.addAction(i18n("Move up"));
        QAction *moveDown=menu.addAction(i18n("Move down"));
        menu.addSeparator();
        QAction *merge=menu.addAction(i18n("Merge with layer below"));
        merge->setEnabled(m_engine.canRun("merge_layer"));
        QAction *flatten=menu.addAction(i18n("Flatten layer to paint"));
        flatten->setEnabled(m_engine.canRun("flatten_layer"));
        menu.addSeparator();
        QAction *group=menu.addAction(i18n("Group selected layers…"));
        QAction *ungroup=nullptr;
        if(node->inherits("KisGroupLayer"))
            ungroup=menu.addAction(i18n("Ungroup"));
        menu.addSeparator();
        QAction *remove=menu.addAction(i18n("Delete layer"));
        QAction *visibility=menu.addAction(node->visible()?i18n("Hide"):i18n("Show"));
        QAction *lock=menu.addAction(node->userLocked()?i18n("Unlock"):i18n("Lock"));
        QAction *clear=menu.addAction(i18n("Clear"));
        clear->setEnabled(paint && node->isEditable());
        QMenu *select=menu.addMenu(i18n("Select opaque"));
        select->menuAction()->setEnabled(layer!=nullptr);
        select->addAction(i18n("Replace selection"))->setData(int(SELECTION_REPLACE));
        select->addAction(i18n("Add to selection"))->setData(int(SELECTION_ADD));
        select->addAction(i18n("Subtract from selection"))->setData(int(SELECTION_SUBTRACT));
        select->addAction(i18n("Intersect selection"))->setData(int(SELECTION_INTERSECT));
        select->addAction(i18n("Symmetric difference"))->setData(int(SELECTION_SYMMETRICDIFFERENCE));
        QMenu *mask=menu.addMenu(i18n("Add mask"));
        mask->menuAction()->setEnabled(layer && node->isEditable());
        addMaskTypes(mask);
        const auto validTarget=[this,node,originalImage]() {
            return m_engine.manager() &&
                   m_engine.manager()->image().toStrongRef()==originalImage &&
                   inImage(node,originalImage);
        };
        const bool isMask=node->inherits("KisMask");
        QDialog sheet(this);
        sheet.setWindowTitle(isMask?i18n("Mask options"):i18n("Layer options"));
        MobileTouchScroll::styleMenuDialog(sheet,QSize(440,620));
        auto *outer=new QVBoxLayout(&sheet);
        auto *scrollArea=new QScrollArea(&sheet);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        MobileTouchScroll::configure(scrollArea);
        auto *body=new QWidget(scrollArea);
        body->setObjectName(QStringLiteral("mobileLayerOptionsBody"));
        body->setStyleSheet(QStringLiteral(
            "QWidget#mobileLayerOptionsBody{background:#252529;}"));
        auto *sheetLayout=new QVBoxLayout(body);
        scrollArea->setWidget(body);
        outer->addWidget(scrollArea);
        QAction *chosen=nullptr;
        const auto makeButton=[&](QAction *action,const QString &label) {
            auto *button=new QPushButton(label,&sheet);
            button->setEnabled(action->isEnabled());
            connect(button,&QPushButton::clicked,&sheet,[&,action]() {
                chosen=action;
                sheet.accept();
            });
            return button;
        };
        sheetLayout->addWidget(makeButton(rename,node->name()));
        if(isMask && node->parent()) {
            auto *ownerLabel=new QLabel(i18n("Attached to %1",node->parent()->name()),body);
            ownerLabel->setWordWrap(true);
            sheetLayout->addWidget(ownerLabel);
        }
        const QString blendName=KoCompositeOpRegistry::instance()
            .getCompositeOpDisplayName(node->compositeOpId());
        if(!isMask) {
            auto *blendButton=makeButton(blendAction,blendName.isEmpty()?blendAction->text():blendName);
            blendButton->setAccessibleDescription(i18n("Change layer blending mode"));
            sheetLayout->addWidget(blendButton);
        }
        auto *opacityLabel=new QLabel(&sheet);
        auto *opacitySlider=new QSlider(Qt::Horizontal,&sheet);
        opacitySlider->setRange(0,100);
        const int initialOpacity=(int(node->opacity())*100+127)/255;
        opacitySlider->setValue(initialOpacity);
        opacityLabel->setText(i18n("Opacity: %1%").arg(initialOpacity));
        opacitySlider->setMinimumHeight(48);
        opacitySlider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal{height:8px;background:#575762;border-radius:4px;}"
            "QSlider::sub-page:horizontal{background:#6ca9c9;border-radius:4px;}"
            "QSlider::handle:horizontal{width:28px;margin:-10px 0;"
            "background:#f3f6fa;border:1px solid #263341;border-radius:14px;}"));
        connect(opacitySlider,&QSlider::valueChanged,&sheet,[opacityLabel](int value) {
            opacityLabel->setText(i18n("Opacity: %1%").arg(value));
        });
        if(isMask) {
            opacityLabel->hide();
            opacitySlider->hide();
        } else {
            sheetLayout->addWidget(opacityLabel);
            sheetLayout->addWidget(opacitySlider);
        }
        auto *grid=new QGridLayout;
        const QList<QPair<QAction*,QString>> buttons=isMask?
            QList<QPair<QAction*,QString>>{
                {properties,i18n("Mask settings")},{visibility,visibility->text()},{lock,lock->text()},
                {moveUp,i18n("Move up")},{moveDown,i18n("Move down")},{remove,i18n("Delete")}
            }:QList<QPair<QAction*,QString>>{
            {visibility,visibility->text()},{duplicate,i18n("Duplicate")},{clear,i18n("Clear")},
            {alphaLock,alphaLock->isChecked()?i18n("Alpha lock: On"):i18n("Alpha lock: Off")},
            {lock,lock->text()},{merge,i18n("Merge")},{remove,i18n("Delete")},
            {select->menuAction(),i18n("Select")},{mask->menuAction(),i18n("Mask")}
        };
        for(int i=0;i<buttons.size();++i) {
            auto *button=makeButton(buttons[i].first,buttons[i].second);
            if(buttons[i].first==alphaLock && alphaLock->isChecked())
                button->setStyleSheet(QStringLiteral("background:#365c78;"));
            grid->addWidget(button,i/3,i%3);
        }
        sheetLayout->addLayout(grid);
        if(inheritAlpha->isEnabled()) {
            const QString full=clipped && clipBase?
                i18n("Unclip from %1",clipBase->name()):inheritAlpha->text();
            auto *clip=makeButton(inheritAlpha,full);
            clip->setText(clip->fontMetrics().elidedText(
                full,Qt::ElideRight,qMax(1,qMin(300,window()->width()-64))));
            clip->setAccessibleName(full);
            sheetLayout->addWidget(clip);
        }
        if(!isMask)sheetLayout->addWidget(makeButton(group,group->text()));
        auto *footer=new QHBoxLayout;
        auto *more=new QPushButton(i18n("More…"),&sheet);
        connect(more,&QPushButton::clicked,&sheet,[&sheet]() {sheet.done(2);});
        auto *done=new QPushButton(i18n("Done"),&sheet);
        connect(done,&QPushButton::clicked,&sheet,&QDialog::accept);
        footer->addWidget(more);
        footer->addWidget(done);
        outer->addLayout(footer);
        const int sheetHeight=body->sizeHint().height()+footer->sizeHint().height()+36;
        sheet.resize(qMin(440,qMax(1,window()->width()-24)),
                     qMin(sheetHeight,qMax(1,window()->height()-24)));
        sheet.setMaximumSize(qMax(1,window()->width()-12),
                             qMax(1,window()->height()-12));
        const int result=sheet.exec();
        if(result==QDialog::Rejected || !validTarget())return;
        if(opacitySlider->value()!=initialOpacity)
            m->setNodeOpacity(node,(opacitySlider->value()*255+50)/100);
        if(result==2)chosen=mobilePickAction(window(),menu);
        else if(chosen==select->menuAction())chosen=mobilePickAction(window(),*select);
        else if(chosen==mask->menuAction())chosen=mobilePickAction(window(),*mask);
        if(!chosen || !validTarget())return;
        if(chosen==visibility || chosen==lock) {
            if(chosen==visibility)
                setLayerProperty(node,KisLayerPropertiesIcons::visible,!node->visible());
            else setLayerProperty(node,KisLayerPropertiesIcons::locked,!node->userLocked());
        } else if(select->actions().contains(chosen)) {
            view->selectionManager()->selectOpaqueOnNode(
                node,static_cast<SelectionAction>(chosen->data().toInt()));
        } else if(chosen==clear || mask->actions().contains(chosen)) {
            if(auto *transform=activeTransform()) {
                if(!canRetarget(transform))return;
                m_engine.freehandBrush();
            }
            if(!m_engine.selectLayer(node) || !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=originalImage ||
               m_engine.manager()->activeNode()!=node)return;
            // Masks and Clear target one specific layer even if a previous
            // multi-layer transform left several nodes selected.
            m->slotSetSelectedNodes(KisNodeList{node});
            if(chosen==clear)m_engine.run("clear");
            else m->createNode(chosen->data().toString());
        } else if(chosen==rename) {
            renameLayer(node,originalImage);
            return;
        } else if(chosen==opacityAction) {
            // Use Krita's native undoable node-opacity command, not
            // direct setOpacity() or a mobile-only layer history.
            // Guard the document identity across the Android dialog:
            // a view switch must not modify another canvas.
            const KisImageSP originalImage=m_engine.manager() ?
                m_engine.manager()->image().toStrongRef() : KisImageSP();
            if(!originalImage)return;
            QInputDialog dialog(this);
            dialog.setWindowTitle(i18n("Layer opacity"));
            dialog.setLabelText(i18n("Opacity (0–100%)"));
            dialog.setInputMode(QInputDialog::IntInput);
            dialog.setIntRange(0,100);
            dialog.setIntStep(1);
            dialog.setIntValue((int(node->opacity())*100+127)/255);
            dialog.setStyleSheet(
                "QInputDialog{background:#283746;color:#f4f9fe;}"
                "QLabel{color:#f4f9fe;}"
                "QSpinBox{background:#eef3f7;color:#172530;"
                "min-height:42px;}"
                "QPushButton{background:#446781;color:#f4f9fe;"
                "min-height:42px;}");
            if(dialog.exec()!=QDialog::Accepted ||
               m_reorderPending || !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=originalImage)
                return;
            KisNodeSP ancestor=node;
            while(ancestor && ancestor!=originalImage->root())
                ancestor=ancestor->parent();
            if(ancestor!=originalImage->root())return;
            m->setNodeOpacity(node,(dialog.intValue()*255+50)/100);
        } else if(chosen==blendAction) {
            // Enumerate Krita's actual color-space-supported modes
            // rather than hardcoding a short list of mobile effects.
            // KisNodeManager commits the selected mode to Krita Undo.
            auto *view=m_engine.manager();
            if(!view || !node->colorSpace())return;
            const KisImageSP original=view->image().toStrongRef();
            if(!original)return;
            const KoColorSpace *colorSpace=node->colorSpace();
            const auto &registry=KoCompositeOpRegistry::instance();
            const auto modes=registry.getCompositeOps(colorSpace);
            QDialog dialog(this);
            dialog.setWindowTitle(i18n("Layer blending mode"));
            auto *layout=new QVBoxLayout(&dialog);
            auto *tree=new QTreeWidget(&dialog);
            tree->setHeaderHidden(true);
            tree->setStyleSheet(QStringLiteral("QTreeWidget::item{min-height:40px;}"));
            MobileTouchScroll::configureList(tree);
            layout->addWidget(tree,1);
            const auto addMode=[&](QTreeWidgetItem *parent,const KoID &mode) {
                auto *item=new QTreeWidgetItem(parent,{mode.name()});
                item->setData(0,Qt::UserRole,mode.id());
                if(mode.id()==node->compositeOpId())
                    item->setCheckState(0,Qt::Checked);
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            };
            auto *favorites=new QTreeWidgetItem(tree,{i18n("Favorites")});
            for(const auto &id:{COMPOSITE_OVER,COMPOSITE_MULT,COMPOSITE_OVERLAY,
                               COMPOSITE_SCREEN,COMPOSITE_ERASE,COMPOSITE_DARKEN,
                               COMPOSITE_LIGHTEN,COMPOSITE_DODGE,COMPOSITE_BURN}) {
                for(const auto &mode:modes) {
                    if(mode.id()!=id)continue;
                    addMode(favorites,mode);
                    break;
                }
            }
            favorites->setExpanded(true);
            for(const auto &category:registry.getCategories()) {
                const auto categoryModes=registry.getCompositeOps(category,colorSpace);
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
                if(id.isEmpty()) {
                    item->setExpanded(!item->isExpanded());
                    return;
                }
                selected=id;
                dialog.accept();
            });
            dialog.resize(qMin(360,window()->width()),qMin(540,window()->height()));
            if(dialog.exec()!=QDialog::Accepted || !validTarget() ||
               m_reorderPending || node->colorSpace()!=colorSpace)return;
            if(const KoCompositeOp *mode=colorSpace->compositeOp(selected))
                m->setNodeCompositeOp(node,mode);
        } else if(chosen==properties) {
            m->nodePropertiesIgnoreSelection(node);
        } else if(chosen==alphaLock || chosen==inheritAlpha) {
            // Do NOT switch active layers then toggle: KisNodeManager's
            // activeNode()/selectedNodes() can still refer to the previous
            // layer on the same Qt event turn. Address exactly the tapped
            // node with Krita's undoable property command instead.
            if(chosen==alphaLock) {
                if(auto *paint=qobject_cast<KisPaintLayer*>(node.data()))
                    setLayerProperty(node,KisLayerPropertiesIcons::alphaLocked,
                                     !paint->alphaLocked());
            } else if(qobject_cast<KisLayer*>(node.data())) {
                setCompactClip(node,!inheritsAlpha(node));
                return; // regroup asynchronously before rebuilding the UI
            }
        } else if(chosen==merge || chosen==flatten) {
            // Invoke the native Krita layer manager so group/mask
            // semantics, disabled states and image undo stay intact.
            // Never merge a stale node belonging to another canvas.
            auto *view=m_engine.manager();
            if(!view || !view->image().toStrongRef() ||
               m_reorderPending)return;
            KisNodeSP ancestor=node;
            const KisImageSP image=view->image().toStrongRef();
            while(ancestor && ancestor!=image->root())
                ancestor=ancestor->parent();
            if(ancestor!=image->root())return;
            if(auto *transform=activeTransform()) {
                if(!canRetarget(transform))return;
                m_engine.freehandBrush();
            }
            if(!m_engine.selectLayer(node) ||
               !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=image)return;
            m->slotSetSelectedNodes(KisNodeList{node});
            if(!m_engine.run(chosen==merge?"merge_layer":"flatten_layer"))
                qWarning() << "Mobile Studio: Krita layer operation unavailable";
        } else if(chosen==group) {
            chooseGroupLayers(node,originalImage);
        } else if(chosen==ungroup) {
            if(!node->inherits("KisGroupLayer"))return;
            if(auto *transform=activeTransform()) {
                if(!canRetarget(transform))return;
                m_engine.freehandBrush();
            }
            if(!m_engine.selectLayer(node) ||
               !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=originalImage)return;
            m->slotSetSelectedNodes(KisNodeList{node});
            m->quickUngroup();
        } else if(chosen==remove) {
            deleteLayers({node});
            return;
        } else if(chosen==moveUp || chosen==moveDown) {
            reorderNode(node,chosen==moveUp);
            return; // Rebuild after the undoable move, not during it.
        } else {
            if(chosen==duplicate) {
                // Duplicate only the tapped layer, not a stale selection
                // left behind by Krita's multi-layer transform.
                if(auto *transform=activeTransform()) {
                    if(!canRetarget(transform))return;
                    m_engine.freehandBrush();
                }
                if(!m_engine.manager() ||
                   m_engine.manager()->image().toStrongRef()!=originalImage ||
                   !inImage(node,originalImage) ||
                   !m_engine.selectLayer(node))return;
                m->slotSetSelectedNodes(KisNodeList{node});
                m->duplicateActiveNode();
            } else {
                m_engine.selectLayer(node);
            }
        }
        reload();
    }
    QString engineSignature() const
    {
        auto *view=m_engine.manager();
        if(!view || !manager())return QStringLiteral("no-view");
        const auto image=view->image().toStrongRef();
        if(!image || !image->root())return QStringLiteral("no-image");
        QString key=QString::number(quintptr(image.data()),16)+
            QLatin1Char(allowsMultiple()?'+':'-');
        int count=0;
        std::function<void(KisNodeSP,int)> inspect;
        inspect=[&](KisNodeSP parent,int depth) {
            if(!parent || depth>20 || count>=500)return;
            for(KisNodeSP n=parent->lastChild();n && count<500;
                n=n->prevSibling()) {
                // Krita's standard layer docker hides the root-level
                // global selection mask. It is the canvas selection,
                // not another paintable user layer.
                if(n->inherits("KisReferenceImagesLayer") ||
                   KisNodeManager::isNodeHidden(n,true))continue;
                ++count;
                key+=n->uuid().toString()+QLatin1Char('|')+n->name()+
                    QLatin1Char('|')+QString::number(n->visible())+
                    QString::number(n->userLocked())+
                    QString::number(n->opacity())+
                    QString::number(inheritsAlpha(n))+
                    QString::number(n->collapsed())+
                    n->compositeOpId()+
                    QLatin1Char(':')+QString::number(depth)+QLatin1Char(';');
                if(auto *paint=qobject_cast<KisPaintLayer*>(n.data()))
                    key+=QLatin1Char(paint->alphaLocked()?'1':'0');
                if(n->childCount())inspect(n,depth+1);
            }
        };
        inspect(image->root(),0);
        const auto selected=manager()->selectedNodes();
        for(const KisNodeSP &n:selected)
            if(n)key+=n->uuid().toString();
        const KisNodeSP active=view->activeNode();
        if(active)key+=active->uuid().toString();
        return key;
    }

    // Unlike the native active-layer toggle actions, this explicitly
    // addresses the row the user touched, even before selection signals
    // and rendering catch up. The native command remains fully undoable.
    bool setLayerProperty(KisNodeSP node,const KoID &id,bool enabled)
    {
        auto *view=m_engine.manager();
        KisImageSP image=view?view->image().toStrongRef():KisImageSP();
        if(!image || !node || !node->parent())return false;
        if(!inImage(node,image))return false;
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(node,id,enabled,image);
        return true;
    }

    // Creation uses Krita's native node manager and the current document.
    // A live multi-layer transform must not silently gain a new target or
    // inherit the old selection when a layer/mask is inserted.
    KisNodeSP creationTarget(const KisImageSP &image) const
    {
        auto *nodes=manager();
        if(!image || !nodes)return {};
        KisNodeSP target=m_active;
        if(!inImage(target,image) || KisNodeManager::isNodeHidden(target,true))
            target=nodes->activeNode();
        if(target && target->inherits("KisMask"))target=target->parent();
        return inImage(target,image) && !KisNodeManager::isNodeHidden(target,true)?target:KisNodeSP{};
    }

    bool prepareNodeCreation(const KisImageSP &image,bool mask)
    {
        auto *view=m_engine.manager();
        auto *nodes=manager();
        if(!image || !view || !nodes ||
           view->image().toStrongRef()!=image)return false;
        if(m_reorderPending || m_clipBusy || m_clipPropertyPending)return false;
        const KisNodeSP target=creationTarget(image);
        const bool validTarget=bool(target);
        // Newly created or emptied canvases can have no selectable layer
        // yet. Native Krita still allows creating their first paint layer.
        if(mask && (!validTarget || !target->inherits("KisLayer") ||
                    !target->isEditable()))return false;
        if(auto *transform=activeTransform()) {
            if(!canRetarget(transform))return false;
            m_engine.freehandBrush();
        }
        if(!m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=image)return false;
        if(validTarget) {
            if(!m_engine.selectLayer(target))return false;
            nodes->slotSetSelectedNodes(KisNodeList{target});
        } else {
            // An empty document has no layer to target. Leave insertion
            // placement to Krita's native node manager.
            nodes->slotSetSelectedNodes(KisNodeList());
        }
        return true;
    }

    KisNodeManager *manager() const
    {
        auto *view=m_engine.manager();
        return view?view->nodeManager():nullptr;
    }
    KisNodeList selectedTargets() const
    {
        KisNodeList result;
        auto *view=m_engine.manager();
        const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
        for(const auto &node:m_selected)
            if(node && node->parent() && inImage(node,image) &&
               !KisNodeManager::isNodeHidden(node,true) &&
               !result.contains(node))
                result.append(node);
        if(allowsMultiple() && image && image->root()) {
            // A layer rail tap can change selection while Transform/Move
            // is already active, bypassing the tool-entry normalization.
            // A selected group owns its descendants: submit it only once.
            const auto hidden=[](const KisNodeSP &node) {
                return KisNodeManager::isNodeHidden(node,true);
            };
            return MobileLayerIdentity::transformTargets(
                image->root(),result,KisNodeSP(),hidden,false);
        }
        return result;
    }
    void addButton(QHBoxLayout *row,const QString &label,
                   std::function<void()> callback)
    {
        auto *button=new QPushButton(label,this);
        button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        row->addWidget(button,1);
        connect(button,&QPushButton::clicked,this,[callback]() {
            callback();
        });
    }
    // Small, distinguishable labels for masks in the narrow rail.
    // Ordinary layers have image thumbnails, whereas masks otherwise
    // looked like generic empty grey tiles with no visible type.
    static QString maskKind(KisNodeSP node)
    {
        if(!node)return QString();
        if(node->inherits("KisTransparencyMask"))return i18n("ALPHA");
        if(node->inherits("KisFilterMask"))return i18n("FILTER");
        if(node->inherits("KisTransformMask"))return i18n("XFORM");
        if(node->inherits("KisSelectionMask"))return i18n("SELECT");
        if(node->inherits("KisColorizeMask"))return i18n("COLOR");
        if(node->inherits("FastColorOverlayFilterMask") ||
           node->inherits("KisFastColorOverlayFilterMask"))
            return i18n("OVERLAY");
        return i18n("MASK");
    }

    static bool isCompactClipWrapper(KisNodeSP node)
    {
        return node && node->inherits("KisGroupLayer") &&
            node->name().startsWith(QStringLiteral("[Compact Clip] "));
    }

    static bool inheritsAlpha(KisNodeSP node)
    {
        return node && node->inherits("KisLayer") &&
            KisLayerPropertiesIcons::nodeProperty(
                node,KisLayerPropertiesIcons::inheritAlpha,false).toBool();
    }

    static bool inImage(KisNodeSP node,const KisImageSP &image)
    {
        return image && MobileLayerIdentity::inTree(node,image->root());
    }

    static KisNodeSP logicalParent(KisNodeSP node)
    {
        if(!node)return KisNodeSP();
        KisNodeSP parent=node->parent();
        return isCompactClipWrapper(parent)?parent->parent():parent;
    }

    static void logicalChildren(KisNodeSP parent,KisNodeList &out)
    {
        if(!parent)return;
        for(KisNodeSP node=parent->firstChild();node;
            node=node->nextSibling()) {
            if(isCompactClipWrapper(node))logicalChildren(node,out);
            else out.append(node);
        }
    }

    // Report a single logical base for the user's clipping action.
    // Layers are ordered bottom-to-top, so walking backwards reaches
    // the closest un-clipped sibling. An ordinary Krita group is a
    // valid base; its combined alpha clips the layers above it.
    static KisNodeSP clipBaseFor(KisNodeSP node)
    {
        KisNodeList ordered;
        logicalChildren(logicalParent(node),ordered);
        const int index=ordered.indexOf(node);
        for(int i=index-1;i>=0;--i) {
            KisNodeSP candidate=ordered.at(i);
            if(candidate->inherits("KisLayer") &&
               !inheritsAlpha(candidate))return candidate;
        }
        return KisNodeSP();
    }

    // Clipping is UI-relative: a clipped layer attaches to the FIRST
    // unclipped layer beneath it, even if layers have been moved. Krita
    // requires an actual group to make inheritAlpha mean that. Preserve
    // those native groups/undo commands and hide only our generated
    // "[Compact Clip]" wrappers in the tile display.
    void setCompactClip(KisNodeSP node,bool on)
    {
        auto *view=m_engine.manager();
        const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
        if(m_clipBusy || m_clipPropertyPending || m_reorderPending ||
           !inImage(node,image) || !node->inherits("KisLayer") ||
           isCompactClipWrapper(node) || inheritsAlpha(node)==on)
            return;
        KisNodeSP parent=logicalParent(node);
        if(!parent || !manager())return;
        if(on) {
            if(!clipBaseFor(node)) {
                QMessageBox::information(this,i18n("Clip layer"),
                    i18n("Place an unclipped layer below this one first."));
                return;
            }
        }
        // Clipping changes the native node tree, so an unfinished
        // transform must not be retargeted underneath it.
        if(auto *transform=activeTransform()) {
            if(!canRetarget(transform))return;
            m_engine.freehandBrush();
        }
        if(!m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=image ||
           !inImage(node,image) || logicalParent(node)!=parent)return;
        if(!setLayerProperty(node,KisLayerPropertiesIcons::inheritAlpha,on))
            return;
        // Property edits are undoable Krita image strokes. Starting a
        // node-tree regroup while that stroke is still pending can
        // observe the OLD inherit-alpha value and silently leave the
        // layer outside its intended clipping group.
        m_clipPropertyPending=true;
        m_clipPropertyImage=image;
        QTimer::singleShot(0,this,[this,node,parent,on]() {
            waitCompactClipProperty(node,parent,on,0);
        });
    }

    void waitCompactClipProperty(KisNodeSP node,KisNodeSP parent,
                                 bool expected,int attempt)
    {
        if(!m_clipPropertyPending)return;
        auto *vm=m_engine.manager();
        if(!vm || vm->image().toStrongRef()!=m_clipPropertyImage ||
           !inImage(node,m_clipPropertyImage) ||
           !inImage(parent,m_clipPropertyImage)) {
            m_clipPropertyPending=false;
            m_clipPropertyImage.clear();
            return;
        }
        if(inheritsAlpha(node)==expected) {
            m_clipPropertyPending=false;
            m_clipPropertyImage.clear();
            beginCompactClipRebuild(parent,node);
        } else if(attempt<40) {
            QTimer::singleShot(50,this,[this,node,parent,expected,attempt]() {
                waitCompactClipProperty(node,parent,expected,attempt+1);
            });
        } else {
            qWarning() << "Mobile Studio: inherit-alpha edit did not settle";
            m_clipPropertyPending=false;
            m_clipPropertyImage.clear();
            reload();
        }
    }

    void finishCompactClipRebuild()
    {
        auto *m=manager();
        auto *vm=m_engine.manager();
        if(m && vm && vm->image().toStrongRef()==m_clipImage &&
           m_clipRestoreActive && m_clipRestoreActive->parent()) {
            m->slotUiActivatedNode(m_clipRestoreActive);
            m->slotSetSelectedNodes(KisNodeList{m_clipRestoreActive});
        }
        m_clipBusy=false;
        m_clipImage.clear();
        m_clipRestoreActive.clear();
        m_clipParents.clear();
        m_clipPendingWrappers.clear();
        m_clipAfterUnwrap=nullptr;
        m_clipMoveSource.clear();
        m_clipExpectedParent.clear();
        m_clipExpectedBelow.clear();
        QTimer::singleShot(120,this,[this]() {reload();});
    }

    void waitCompactClipGroup(KisNodeSP base,
                              KisNodeSP originalParent,KisNodeList cluster,
                              int attempt)
    {
        if(!m_clipBusy || !m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=m_clipImage) {
            finishCompactClipRebuild();
            return;
        }
        // A native grouping stroke completes asynchronously. The active
        // image can remain selected even if a parent was detached by an
        // intervening edit, so do not rename a group outside its tree.
        if(!inImage(originalParent,m_clipImage) ||
           !inImage(base,m_clipImage)) {
            finishCompactClipRebuild();
            return;
        }
        KisNodeSP group=base?base->parent():KisNodeSP();
        bool ready=group && group!=originalParent &&
            group->parent()==originalParent &&
            group->inherits("KisGroupLayer") &&
            inImage(group,m_clipImage);
        for(const KisNodeSP &node:cluster)
            if(!node || !inImage(node,m_clipImage) ||
               node->parent()!=group)ready=false;
        if(ready) {
            manager()->setNodeName(
                group,QStringLiteral("[Compact Clip] ")+base->name());
            group->setCollapsed(false);
            QTimer::singleShot(110,this,[this]() {
                continueCompactClipRebuild();
            });
        } else if(attempt<35) {
            QTimer::singleShot(100,this,[this,base,originalParent,cluster,attempt]() {
                waitCompactClipGroup(base,originalParent,cluster,attempt+1);
            });
        } else {
            qWarning() << "Mobile Studio: compact clip group creation timed out";
            finishCompactClipRebuild();
        }
    }

    void continueCompactClipRebuild()
    {
        auto *vm=m_engine.manager();
        auto *m=manager();
        if(!m_clipBusy || !m || !vm ||
           vm->image().toStrongRef()!=m_clipImage) {
            finishCompactClipRebuild();
            return;
        }
        while(!m_clipParents.isEmpty()) {
            KisNodeSP parent=m_clipParents.first();
            if(!parent || !inImage(parent,m_clipImage)) {
                // Never create a clip group in a detached hierarchy.
                finishCompactClipRebuild();
                return;
            }
            KisNodeList ordered;
            for(KisNodeSP node=parent->firstChild();node;
                node=node->nextSibling())ordered.append(node);
            for(int i=0;i<ordered.size();) {
                KisNodeSP base=ordered.at(i);
                if(isCompactClipWrapper(base) || inheritsAlpha(base) ||
                   !base->inherits("KisLayer")){++i;continue;}
                KisNodeList cluster{base};
                int j=i+1;
                while(j<ordered.size() && inheritsAlpha(ordered.at(j))) {
                    cluster.append(ordered.at(j));++j;
                }
                if(cluster.size()>1 &&
                   m->canMoveLayers(cluster,false)) {
                    m->slotUiActivatedNode(base);
                    // Selection activation can cause reentrant node
                    // notifications. Do not group a different document
                    // or siblings that have just been reparented.
                    if(!m_engine.manager() ||
                       m_engine.manager()->image().toStrongRef()!=m_clipImage ||
                       manager()!=m || !inImage(parent,m_clipImage) ||
                       !m->canMoveLayers(cluster,false)) {
                        finishCompactClipRebuild();
                        return;
                    }
                    bool attached=true;
                    for(const KisNodeSP &node:cluster)
                        if(!node || node->parent()!=parent ||
                           !inImage(node,m_clipImage))attached=false;
                    if(!attached) {
                        finishCompactClipRebuild();
                        return;
                    }
                    m->slotSetSelectedNodes(cluster);
                    m->createQuickGroup();
                    QTimer::singleShot(100,this,[this,base,parent,cluster]() {
                        waitCompactClipGroup(base,parent,cluster,0);
                    });
                    return;
                }
                i=j;
            }
            m_clipParents.removeFirst();
        }
        finishCompactClipRebuild();
    }

    void waitCompactClipUngroup(int attempt)
    {
        if(!m_clipBusy || !m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=m_clipImage) {
            finishCompactClipRebuild();return;
        }
        bool pending=false;
        for(const KisNodeSP &node:m_clipPendingWrappers)
            if(node && node->parent())pending=true;
        if(pending && attempt<35) {
            QTimer::singleShot(100,this,[this,attempt]() {
                waitCompactClipUngroup(attempt+1);
            });
        } else if(pending) {
            qWarning() << "Mobile Studio: compact clip ungroup timed out";
            finishCompactClipRebuild();
        } else if(m_clipAfterUnwrap) {
            // Reorder against the *logical* flat layer stack, then build
            // the hidden Krita clip groups back around the new neighbors.
            auto operation=std::move(m_clipAfterUnwrap);
            m_clipAfterUnwrap=nullptr;
            operation();
            QTimer::singleShot(95,this,[this]() {
                waitCompactClipMove(0);
            });
        } else {
            continueCompactClipRebuild();
        }
    }

    void waitCompactClipMove(int attempt)
    {
        if(!m_clipBusy || !m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=m_clipImage) {
            finishCompactClipRebuild();return;
        }
        if(m_clipMoveSource &&
           (m_clipMoveSource->parent()!=m_clipExpectedParent ||
            m_clipMoveSource->prevSibling()!=m_clipExpectedBelow)) {
            if(attempt<35) {
                QTimer::singleShot(95,this,[this,attempt]() {
                    waitCompactClipMove(attempt+1);
                });
            } else {
                qWarning() << "Mobile Studio: layer move did not settle";
                finishCompactClipRebuild();
            }
        } else continueCompactClipRebuild();
    }

    void beginCompactClipRebuild(
        KisNodeSP parent,KisNodeSP restore,
        std::function<void()> afterUnwrap={},
        KisNodeSP otherParent=KisNodeSP())
    {
        auto *m=manager();
        auto *vm=m_engine.manager();
        if(m_clipBusy || !m || !vm || !parent)return;
        m_clipImage=vm->image().toStrongRef();
        if(!m_clipImage)return;
        m_clipBusy=true;
        m_clipParents=QList<KisNodeSP>{parent};
        if(otherParent && otherParent!=parent)
            m_clipParents.append(otherParent);
        m_clipRestoreActive=restore;
        m_clipAfterUnwrap=std::move(afterUnwrap);
        m_clipPendingWrappers.clear();
        m_clipMoveSource.clear();
        m_clipExpectedParent.clear();
        m_clipExpectedBelow.clear();
        std::function<void(KisNodeSP)> ungroup=[&](KisNodeSP container) {
            for(KisNodeSP child=container->firstChild();child;
                child=child->nextSibling()) {
                if(isCompactClipWrapper(child)) {
                    ungroup(child);
                    m_clipPendingWrappers.append(child);
                }
            }
        };
        for(const KisNodeSP &container:m_clipParents)
            ungroup(container);
        for(const KisNodeSP &wrapper:m_clipPendingWrappers) {
            m->slotUiActivatedNode(wrapper);
            m->slotSetSelectedNodes(KisNodeList{wrapper});
            m->quickUngroup();
        }
        QTimer::singleShot(100,this,[this]() {waitCompactClipUngroup(0);});
    }

    static bool hasCompactClipStructure(KisNodeSP parent)
    {
        if(!parent)return false;
        for(KisNodeSP child=parent->firstChild();child;
            child=child->nextSibling()) {
            if(isCompactClipWrapper(child) || inheritsAlpha(child))
                return true;
        }
        return false;
    }

    void moveCompactLayer(KisNodeSP source,KisNodeSP target,
                          bool insertAbove,bool intoGroup=false)
    {
        auto *m=manager();
        auto *vm=m_engine.manager();
        const KisImageSP image=vm?vm->image().toStrongRef():KisImageSP();
        if(m_clipBusy || m_clipPropertyPending || m_reorderPending ||
           !m || !vm || !inImage(source,image) || !inImage(target,image) ||
           source==target)return;
        KisNodeSP oldParent=logicalParent(source);
        KisNodeSP destination=intoGroup?target:logicalParent(target);
        if(!oldParent || !destination || !source->parent() ||
           !destination->isEditable(false) || !destination->allowAsChild(source) ||
           !m->canMoveLayer(source,false))return;
        // Never move a folder inside its own descendants.
        for(KisNodeSP p=destination;p;p=p->parent())
            if(p==source)return;

        const KisNodeSP originalParent=source->parent();
        if(!prepareLayerReorder(image) || !inImage(source,image) ||
           !inImage(target,image) || !inImage(destination,image) ||
           !m_engine.manager() ||
           m_engine.manager()->image().toStrongRef()!=image ||
           manager()!=m || source->parent()!=originalParent ||
           logicalParent(source)!=oldParent ||
           destination!=(intoGroup?target:logicalParent(target)) ||
           !destination->isEditable(false) ||
           !destination->allowAsChild(source) ||
           !m->canMoveLayer(source,false))return;
        for(KisNodeSP p=destination;p;p=p->parent())
            if(p==source)return;
        // Plain layer moves do not need the asynchronous clip wrapper
        // teardown/rebuild. Use the same native undoable image stroke
        // as Up/Down, only regroup when a clipping run is involved.
        const bool mask=source->inherits("KisMask");
        if(mask || (!hasCompactClipStructure(oldParent) &&
           !hasCompactClipStructure(destination) && !inheritsAlpha(source))) {
            KisNodeSP below=intoGroup?destination->lastChild():
                (insertAbove?target:target->prevSibling());
            if(!m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=image ||
               !inImage(source,image) || !inImage(destination,image) ||
               source->parent()!=originalParent ||
               !destination->isEditable(false) ||
               !destination->allowAsChild(source) ||
               !m->canMoveLayer(source,false) ||
               (below && (below==source || below->parent()!=destination)))
                return;
            if(source->parent()==destination &&
               below==source->prevSibling())return;
            if(mask && source->inherits("KisSelectionMask") && source->parent()!=destination) {
                m->moveNodesDirect(KisNodeList{source},destination,below);
                return;
            }
            if(!mask && !m_engine.selectLayer(source))return;
            runNativeLayerStroke(image,
                new KisImageLayerMoveCommand(
                    image,source,destination,below,true),
                QStringLiteral("Native drag reorder"));
            return;
        }
        auto onFlattened=[this,source,target,destination,
                          intoGroup,insertAbove]() {
            auto *nodes=manager();
            auto *view=m_engine.manager();
            if(!nodes || !view || !m_clipImage ||
               view->image().toStrongRef()!=m_clipImage ||
               !inImage(source,m_clipImage) ||
               !inImage(target,m_clipImage) ||
               !inImage(destination,m_clipImage) ||
               !destination->allowAsChild(source))return;
            KisNodeSP below=intoGroup?destination->lastChild():
                (insertAbove?target:target->prevSibling());
            for(KisNodeSP parent=destination;parent;parent=parent->parent())
                if(parent==source)return;
            if(!destination->isEditable(false) ||
               !nodes->canMoveLayer(source,false) ||
               (below && (below==source || below->parent()!=destination)) ||
               (source->parent()==destination &&
                below==source->prevSibling()))return;
            m_clipMoveSource=source;
            m_clipExpectedParent=destination;
            m_clipExpectedBelow=below;
            nodes->moveNodesDirect(KisNodeList{source},destination,below);
        };
        beginCompactClipRebuild(oldParent,source,onFlattened,destination);
    }

    int along(const QPoint &point) const {return m_horizontalRail?point.x():point.y();}
    int verticalTileSize(int width) const {return m_compactRail?qBound(54,width-9,111):73;}
    static int horizontalTileSize(int height) {
        // Use the short axis for the thumbnail, leaving only a narrow
        // border around the tile rather than an oversized empty gutter.
        return qBound(48,height-8,111);
    }

    QWidget *railViewport() const
    {
        return m_horizontalRail?m_horizontalScroll->viewport():m_tree->viewport();
    }

    QScrollBar *railScrollBar() const
    {
        return m_horizontalRail?m_horizontalScroll->horizontalScrollBar():m_tree->verticalScrollBar();
    }

    QList<QTreeWidgetItem*> visibleItems() const
    {
        QList<QTreeWidgetItem*> result;
        for(auto *item:m_items) {
            auto *parent=item->parent();
            while(parent && parent->isExpanded())parent=parent->parent();
            if(!parent)result.append(item);
        }
        return result;
    }

    void createHorizontalRail()
    {
        m_horizontalScroll=new QScrollArea(this);
        m_horizontalScroll->setFrameShape(QFrame::NoFrame);
        m_horizontalScroll->setWidgetResizable(true);
        m_horizontalScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_horizontalScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_horizontalScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:0;}"));
        auto *viewport=m_horizontalScroll->viewport();
        viewport->setAutoFillBackground(false);
        viewport->setAttribute(Qt::WA_AcceptTouchEvents,true);
        viewport->installEventFilter(this);
        m_horizontalBody=new QWidget;
        m_horizontalBody->setAttribute(Qt::WA_TranslucentBackground);
        m_horizontalRows=new QHBoxLayout(m_horizontalBody);
        m_horizontalRows->setContentsMargins(0,0,0,0);
        m_horizontalRows->setSpacing(0);
        m_horizontalRows->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        m_horizontalScroll->setWidget(m_horizontalBody);
        if(m_layerName)m_railLayout->insertWidget(m_railLayout->indexOf(m_layerName),m_horizontalScroll,1);
        else m_railLayout->addWidget(m_horizontalScroll,1);
        connect(m_horizontalScroll->horizontalScrollBar(),&QScrollBar::valueChanged,
                this,[this](int) {updateVisibleLayerThumbnails();});
    }

    void clearHorizontalRows()
    {
        if(!m_horizontalRows)return;
        while(auto *item=m_horizontalRows->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        for(auto *row:m_horizontalBody->findChildren<QFrame*>(QStringLiteral("mobileLayerTileRow"),
                                                               Qt::FindDirectChildrenOnly))
            delete row;
        m_horizontalBody->setMinimumWidth(0);
    }

    QRect layerRect(QTreeWidgetItem *item) const
    {
        if(!m_horizontalRail)return m_tree->visualItemRect(item);
        auto *frame=m_rowFrames.value(item->data(0,Qt::UserRole).toInt()).data();
        if(!frame || frame->isHidden())return {};
        return QRect(frame->mapTo(railViewport(),QPoint()),frame->size());
    }

    QTreeWidgetItem *layerAt(const QPoint &point) const
    {
        if(!m_horizontalRail)return m_tree->itemAt(point);
        if(!railViewport()->rect().contains(point))return nullptr;
        auto *child=m_horizontalBody->childAt(
            m_horizontalBody->mapFromGlobal(railViewport()->mapToGlobal(point)));
        while(child && child->parentWidget()!=m_horizontalBody)child=child->parentWidget();
        return m_items.value(m_rowFrames.indexOf(qobject_cast<QFrame*>(child)));
    }

    bool childDropZone(const KisNodeSP &source,const KisNodeSP &target,
                       const QRect &rect,const QPoint &point) const
    {
        if(source->inherits("KisMask"))return target->inherits("KisLayer");
        const bool center=qAbs(along(point)-along(rect.center()))<
            (m_horizontalRail?rect.width():rect.height())/4;
        return target->inherits("KisGroupLayer") &&
            ((m_horizontalRail?point.y()>rect.bottom()-20:point.x()>rect.right()-20) ||
             (!target->firstChild() && center));
    }

    void moveCompactLayerAt(KisNodeSP source,const QPoint &screenPos)
    {
        if(!m_tree || !source)return;
        const QPoint inView=railViewport()->mapFromGlobal(screenPos);
        QTreeWidgetItem *targetItem=layerAt(inView);
        if(!targetItem)return;
        const int index=m_items.indexOf(targetItem);
        if(index<0 || index>=m_nodes.size())return;
        KisNodeSP target=m_nodes.at(index);
        if(!target || target==source)return;
        const QRect rect=layerRect(targetItem);
        const bool insertAbove=along(inView)<along(rect.center());
        moveCompactLayer(source,target,insertAbove,childDropZone(source,target,rect,inView));
    }

    QTreeWidgetItem *itemForNode(KisNodeSP node) const
    {
        const int i=m_nodes.indexOf(node);
        return i>=0?m_items.value(i,nullptr):nullptr;
    }

    KisNodeSP nodeAtScreen(const QPoint &screen) const
    {
        if(!m_tree)return KisNodeSP();
        const QPoint inView=railViewport()->mapFromGlobal(screen);
        QTreeWidgetItem *item=layerAt(inView);
        const int i=m_items.indexOf(item);
        return i>=0?m_nodes.value(i):KisNodeSP();
    }

    void suppressSyntheticClick(KisNodeSP node)
    {
        m_suppressedClickNode=node;
        m_suppressedClickUntil=QDateTime::currentMSecsSinceEpoch()+450;
    }

    void armMouseLayer(KisNodeSP node,QToolButton *tile,
                       const QPoint &start)
    {
        if(!node || !tile || m_clipBusy || m_reorderPending ||
           !m_touches.isEmpty())return;
        if(m_mouseDown && m_mouseNode==node)return;
        m_mouseNode=node;
        m_mouseTile=tile;
        m_mouseStart=start;
        m_mouseLast=start;
        m_mouseDown=true;
        m_mouseHeld=false;
        m_mouseMoved=false;
        m_mousePreviouslySelected=(m_active==node);
        if(qApp)qApp->installEventFilter(this);
        const auto press=++m_mouseHoldGeneration;
        QTimer::singleShot(node->inherits("KisMask")?260:450,this,[this,node,press]() {
            if(press!=m_mouseHoldGeneration || !m_mouseDown || m_mouseNode!=node ||
               !m_touches.isEmpty())return;
            m_mouseHeld=true;
            m_edgeScrollTimer->start();
            showLayerDropPreview(node,m_mouseLast);
        });
    }

    void updateMouseLayer(const QPoint &point)
    {
        if(!m_mouseDown || !m_mouseNode)return;
        m_mouseLast=point;
        if(m_mouseHeld &&
           (point-m_mouseStart).manhattanLength()>18) {
            m_mouseMoved=true;
            showLayerDropPreview(m_mouseNode,point);
        }
    }

    void finishMouseLayer()
    {
        if(!m_mouseDown)return;
        const KisNodeSP source=m_mouseNode;
        const QPoint point=m_mouseLast;
        const bool moved=m_mouseHeld && m_mouseMoved;
        const bool wasHeld=m_mouseHeld;
        m_mouseNode.clear();
        m_mouseTile.clear();
        m_mouseDown=false;
        m_mouseHeld=false;
        m_mouseMoved=false;
        if(qApp)qApp->removeEventFilter(this);
        m_edgeScrollTimer->stop();
        hideLayerDropPreview();
        if(wasHeld) {
            suppressSyntheticClick(source);
            if(moved)QTimer::singleShot(0,this,[this,source,point]() {
                moveCompactLayerAt(source,point);
            });
            else openHeldLayer(source);
        }
    }

    void showLayerDropPreview(KisNodeSP source,const QPoint &global)
    {
        if(!source || !m_tree || !m_dragGhost || !m_dropMarker)return;
        QWidget *viewport=railViewport();
        const QPoint point=viewport->mapFromGlobal(global);
        if(!m_dragGhost->isVisible()) {
            const QImage thumb=source->createThumbnail(
                54,54,Qt::KeepAspectRatio);
            if(!thumb.isNull()) {
                const auto preview=m_horizontalRail?thumb.transformed(QTransform().rotate(-90)):thumb;
                m_dragGhost->setPixmap(QPixmap::fromImage(preview));
            } else m_dragGhost->setText(QStringLiteral("▣"));
            m_dragGhost->show();
        }
        m_dragGhost->move(
            qBound(0,point.x()+8,qMax(0,viewport->width()-60)),
            qBound(0,point.y()-65,qMax(0,viewport->height()-60)));
        m_dragGhost->raise();
        QTreeWidgetItem *target=layerAt(point);
        const auto node=m_nodes.value(m_items.indexOf(target));
        if(!node || node==source) {
            m_dropMarker->hide();
            return;
        }
        const QRect rect=layerRect(target);
        const bool inside=childDropZone(source,node,rect,point);
        const auto destination=inside?node:logicalParent(node);
        if(!destination || !destination->isEditable(false) ||
           !destination->allowAsChild(source) || !manager() ||
           !manager()->canMoveLayer(source,false)) {
            m_dropMarker->hide();
            return;
        }
        for(auto parent=destination;parent;parent=parent->parent()) {
            if(parent==source) {
                m_dropMarker->hide();
                return;
            }
        }
        if(inside) {
            m_dropMarker->setStyleSheet(QStringLiteral(
                "background:transparent;border:3px solid #b0ebff;border-radius:6px;"));
            m_dropMarker->setGeometry(rect.adjusted(2,1,-2,-1));
        } else {
            m_dropMarker->setStyleSheet(QStringLiteral("background:#81d4fa;"));
            const bool above=along(point)<along(rect.center());
            if(m_horizontalRail)
                m_dropMarker->setGeometry(above?rect.left():rect.right()-2,0,4,viewport->height());
            else
                m_dropMarker->setGeometry(0,above?rect.top():rect.bottom()-2,viewport->width(),4);
        }
        m_dropMarker->show();
        m_dropMarker->raise();
    }

    void hideLayerDropPreview()
    {
        if(m_dragGhost)m_dragGhost->hide();
        if(m_dropMarker)m_dropMarker->hide();
    }

    void groupPinchedTiles(KisNodeSP a,KisNodeSP b)
    {
        auto *m=manager();
        auto *view=m_engine.manager();
        const KisImageSP image=view?view->image().toStrongRef():KisImageSP();
        if(!m || !inImage(a,image) || !inImage(b,image) || a==b ||
           a->parent()!=b->parent() ||
           isCompactClipWrapper(a->parent()))return;
        KisNodeSP parent=a->parent();
        KisNodeList all;
        for(KisNodeSP n=parent->firstChild();n;n=n->nextSibling())
            all.append(n);
        int first=all.indexOf(a),last=all.indexOf(b);
        if(first<0 || last<0)return;
        if(first>last)qSwap(first,last);
        KisNodeList chosen;
        for(int i=first;i<=last;++i) {
            KisNodeSP node=all.at(i);
            if(!node->inherits("KisLayer"))return;
            chosen.append(node);
        }
        if(!m->canMoveLayers(chosen,false) ||
           !prepareLayerReorder(image))return;
        // prepareLayerReorder may switch away from a pending transform.
        // The selected nodes, parent and canvas must still be identical
        // before starting Krita's native grouping/undo operation.
        auto *current=m_engine.manager();
        if(!current || current->image().toStrongRef()!=image ||
           manager()!=m || a->parent()!=parent || b->parent()!=parent ||
           !inImage(a,image) || !inImage(b,image) ||
           !m->canMoveLayers(chosen,false))return;
        m->slotUiActivatedNode(a);
        m->slotSetSelectedNodes(chosen);
        m->createQuickGroup();
        QTimer::singleShot(250,this,[this,a,b,parent,image]() {
            auto *view=m_engine.manager();
            if(!view || view->image().toStrongRef()!=image)return;
            if(!inImage(a,image) || !inImage(b,image))return;
            KisNodeSP group=a?a->parent():KisNodeSP();
            if(group && group!=parent &&
               group==b->parent() &&
               group->inherits("KisGroupLayer")) {
                group->setCollapsed(true);
            }
            reload();
        });
    }

    bool handleNativeLayerTouch(QObject *watched,QEvent *event)
    {
        if(!m_tree || m_clipBusy || m_clipPropertyPending ||
           m_reorderPending)return false;
        const QEvent::Type type=event->type();
        if(type!=QEvent::TouchBegin && type!=QEvent::TouchUpdate &&
           type!=QEvent::TouchEnd && type!=QEvent::TouchCancel)
            return false;
        auto *widget=qobject_cast<QWidget*>(watched);
        const bool relevant=widget &&
            (widget==railViewport() ||
             m_rowButtons.contains(qobject_cast<QToolButton*>(widget)) ||
             m_rowFrames.contains(qobject_cast<QFrame*>(widget)));
        if(!relevant && m_touches.isEmpty())return false;
        if(type==QEvent::TouchCancel) {
            m_touchHoldTimer->stop();
            m_touches.clear();
            m_touchPrimary.clear();
            m_touchControl.clear();
            m_touchHeld=false;
            m_touchPinching=false;
            m_edgeScrollTimer->stop();
            hideLayerDropPreview();
            return true;
        }
        auto *touch=static_cast<QTouchEvent*>(event);
        for(const QTouchEvent::TouchPoint &point:touch->touchPoints()) {
            const int id=point.id();
            const QPoint pos=point.screenPos().toPoint();
            const bool released=point.state()&Qt::TouchPointReleased;
            if(!m_touches.contains(id) && !released && relevant) {
                TileTouch item;
                item.node=nodeAtScreen(pos);
                item.start=pos;
                item.last=pos;
                if(m_touches.isEmpty()) {
                    m_touchPrimary=item.node;
                    m_touchLast=pos;
                    // Qt may route touch on a small eye/lock/alpha button
                    // to its touch-enabled viewport. Preserve that
                    // button's action rather than selecting the layer.
                    auto *hit=qobject_cast<QToolButton*>(
                        QApplication::widgetAt(pos));
                    m_touchControl=(hit &&
                        !m_rowButtons.contains(hit) &&
                        railViewport()->isAncestorOf(hit))?hit:nullptr;
                    m_touchHeld=false;
                    m_touchMoved=false;
                    m_touchScrolling=false;
                    m_touchPinching=false;
                    m_touchPinchReady=false;
                    if(!m_touchControl)
                        m_touchHoldTimer->start(item.node && item.node->inherits("KisMask")?260:450);
                }
                m_touches.insert(id,item);
            }
            if(m_touches.contains(id))
                m_touches[id].last=pos;
        }
        if(m_touches.size()>=2) {
            const auto points=m_touches.values();
            if(!m_touchPinching) {
                m_touchPinching=true;
                m_touchHoldTimer->stop();
                m_touchPinchA=points.at(0).node;
                m_touchPinchB=points.at(1).node;
                m_touchPinchDistance=QLineF(
                    points.at(0).start,points.at(1).start).length();
            }
            if(m_touchPinchDistance>=65) {
                const qreal now=QLineF(
                    points.at(0).last,points.at(1).last).length();
                if(now<m_touchPinchDistance*0.55 &&
                   m_touchPinchDistance-now>45)
                    m_touchPinchReady=true;
            }
        } else if(m_touches.size()==1 && !m_touchPinching &&
                  (type==QEvent::TouchUpdate || type==QEvent::TouchEnd)) {
            const TileTouch point=m_touches.constBegin().value();
            const int travel=(point.last-point.start).manhattanLength();
            if(!m_touchHeld && travel>12) {
                m_touchScrolling=true;
                m_touchHoldTimer->stop();
                auto *bar=railScrollBar();
                bar->setValue(bar->value()+along(m_touchLast)-along(point.last));
            } else if(m_touchHeld && travel>18) {
                m_touchMoved=true;
                showLayerDropPreview(m_touchPrimary,point.last);
                m_edgeScrollTimer->start();
            }
            m_touchLast=point.last;
        }
        for(const QTouchEvent::TouchPoint &point:touch->touchPoints())
            if(point.state()&Qt::TouchPointReleased)
                m_touches.remove(point.id());
        if(type==QEvent::TouchEnd && touch->touchPoints().isEmpty())
            m_touches.clear();
        if(m_touches.isEmpty()) {
            m_touchHoldTimer->stop();
            m_edgeScrollTimer->stop();
            hideLayerDropPreview();
            const KisNodeSP source=m_touchPrimary;
            const QPointer<QToolButton> control=m_touchControl;
            const KisNodeSP a=m_touchPinchA;
            const KisNodeSP b=m_touchPinchB;
            const QPoint last=m_touchLast;
            const bool group=m_touchPinching && m_touchPinchReady;
            const bool moved=m_touchHeld && m_touchMoved;
            const bool held=m_touchHeld;
            const bool scrolling=m_touchScrolling;
            const bool pinching=m_touchPinching;
            m_touchPrimary.clear();m_touchPinchA.clear();
            m_touchPinchB.clear();m_touchControl.clear();
            m_touchHeld=false;m_touchMoved=false;
            m_touchPinching=false;m_touchPinchReady=false;
            m_touchScrolling=false;
            if(source)suppressSyntheticClick(source);
            if(group)groupPinchedTiles(a,b);
            else if(!pinching && !scrolling && control) {
                control->click();
            } else if(!pinching && !scrolling && source) {
                if(moved)moveCompactLayerAt(source,last);
                else if(!held) {
                    if(auto *item=itemForNode(source))tapLayer(source,item);
                } else openHeldLayer(source);
            }
        }
        event->accept();
        return true;
    }

    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(handleNativeLayerTouch(watched,event))return true;
        const QEvent::Type type=event->type();
        if(m_clipBusy || m_clipPropertyPending || m_reorderPending)
            return QFrame::eventFilter(watched,event);
        auto *tile=qobject_cast<QToolButton*>(watched);
        auto *frame=qobject_cast<QFrame*>(watched);
        auto *widget=qobject_cast<QWidget*>(watched);
        int index=tile?m_rowButtons.indexOf(tile):-1;
        if(index<0 && frame)index=m_rowFrames.indexOf(frame);

        // Android's S Pen can start on the tree viewport or the row
        // frame instead of the thumbnail button. A null 'tile' used to
        // make armMouseLayer() reject the press entirely. Resolve the
        // row from the native tablet coordinates, but do not hijack
        // the separate eye/lock/alpha controls.
        if(type==QEvent::TabletPress) {
            auto *pen=static_cast<QTabletEvent*>(event);
            const QPoint global=pen->globalPos();
            auto *hit=qobject_cast<QToolButton*>(QApplication::widgetAt(global));
            const bool rowHit=widget && m_tree &&
                (widget==railViewport() ||
                 railViewport()->isAncestorOf(widget));
            const bool inlineControl=hit && railViewport()->isAncestorOf(hit) &&
                !m_rowButtons.contains(hit);
            if(rowHit && !inlineControl) {
                KisNodeSP node=nodeAtScreen(global);
                const int row=m_nodes.indexOf(node);
                if(row>=0 && m_rowButtons.value(row)) {
                    m_penTracking=true;
                    armMouseLayer(node,m_rowButtons.value(row),global);
                    if(m_mouseDown) {
                        event->accept();
                        return true;
                    }
                    m_penTracking=false;
                }
            }
        }
        // While a real tablet stream owns the press, ignore Android's
        // synthesized mouse duplicate. Only TabletRelease ends the
        // gesture, preventing an early button-release signal from
        // cancelling the stylus hold-to-drag.
        if(m_penTracking && (type==QEvent::MouseButtonPress ||
                             type==QEvent::MouseMove ||
                             type==QEvent::MouseButtonRelease)) {
            // Some Android devices send tablet press/release but only
            // mouse move updates. Consume the duplicate for widget
            // click handling, while keeping the drag position current.
            if(type==QEvent::MouseMove)
                updateMouseLayer(static_cast<QMouseEvent*>(event)->globalPos());
            event->accept();
            return true;
        }
        // Native Android input may retarget mouse-move/release events
        // as the pen/finger crosses other layer widgets. The temporary
        // application event filter installed at press must follow the
        // whole gesture, rather than requiring watched==m_mouseTile.
        if(m_mouseDown && m_touches.isEmpty() &&
           (type==QEvent::MouseMove ||
            type==QEvent::MouseButtonRelease)) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            updateMouseLayer(mouse->globalPos());
            if(type==QEvent::MouseButtonRelease && m_mouseHeld) {
                finishMouseLayer();
                // Let the original QToolButton see the release to
                // clear its pressed state. finishMouseLayer suppresses
                // any delayed synthetic clicked() during a drag.
                return false;
            }
        }
        // Android may emit a synthesized mouse-only pen stream on the
        // viewport. Treat both the row frame and the viewport as drag
        // handles, without changing finger-touch or inline controls.
        if(type==QEvent::MouseButtonPress && widget &&
           (widget==railViewport() ||
            (index>=0 && (frame || m_rowButtons.contains(tile))))) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            const QPoint global=mouse->globalPos();
            auto *hit=qobject_cast<QToolButton*>(QApplication::widgetAt(global));
            const bool inlineControl=hit && railViewport()->isAncestorOf(hit) &&
                !m_rowButtons.contains(hit);
            if(mouse->button()==Qt::LeftButton && !inlineControl) {
                KisNodeSP node=nodeAtScreen(global);
                const int row=m_nodes.indexOf(node);
                if(row>=0 && m_rowButtons.value(row)) {
                    armMouseLayer(node,m_rowButtons.value(row),global);
                    // Preserve QToolButton::pressed/clicked for regular
                    // taps, while consuming a bare viewport/row press
                    // which has no button to finish the gesture.
                    if(widget==railViewport() || frame)return true;
                }
            }
        }
        if((frame || widget==railViewport()) &&
           type==QEvent::MouseButtonRelease &&
           m_mouseDown && !m_mouseHeld) {
            const KisNodeSP pressed=m_mouseNode;
            finishMouseLayer();
            if(auto *item=itemForNode(pressed))
                tapLayer(pressed,item);
            return true;
        }
        if(m_penTracking && m_mouseNode &&
           (type==QEvent::TabletMove ||
            type==QEvent::TabletRelease)) {
            auto *pen=static_cast<QTabletEvent*>(event);
            updateMouseLayer(pen->globalPos());
            if(type==QEvent::TabletRelease) {
                // Some Android stylus paths send tablet events with NO
                // synthesized QToolButton::clicked. Complete a regular
                // pen tap ourselves; suppress any later mouse duplicate.
                const KisNodeSP pressed=m_mouseNode;
                const bool tapped=m_mouseDown && !m_mouseHeld &&
                                  !m_mouseMoved;
                // If Qt synthesized a button press before delivering
                // TabletPress, its matching mouse release was consumed
                // above. Clear the thumbnail's visual down state.
                if(m_mouseTile)m_mouseTile->setDown(false);
                m_penTracking=false;
                finishMouseLayer();
                if(pressed)suppressSyntheticClick(pressed);
                if(tapped && pressed) {
                    if(auto *item=itemForNode(pressed))
                        tapLayer(pressed,item);
                }
                return true;
            }
        }
        return QFrame::eventFilter(watched,event);
    }

    void openHeldLayer(KisNodeSP node)
    {
        QTimer::singleShot(0,this,[this,node]() {
            if(isVisible() && !m_mouseDown && m_touches.isEmpty())
                if(auto *item=itemForNode(node))showLayerActions(node,item);
        });
    }

    bool allowsMultiple() const
    {
        const QString tool=m_engine.activeTool().section(QLatin1Char('/'),-1);
        return tool==QStringLiteral("KisToolTransform") ||
               tool==QStringLiteral("KisToolMove");
    }

    KoToolBase *activeTransform() const
    {
        auto *tools=KoToolManager::instance();
        auto *controller=tools?tools->activeCanvasController():nullptr;
        auto *view=m_engine.manager();
        if(!controller || !view || controller->canvas()!=view->canvasBase() ||
           tools->activeToolId().section(QLatin1Char('/'),-1)!=
               QStringLiteral("KisToolTransform"))return nullptr;
        return tools->toolById(controller->canvas(),tools->activeToolId());
    }

    bool canRetarget(KoToolBase *transform)
    {
        bool allowed=false;
        if(QMetaObject::invokeMethod(transform,"mobileCanRetargetLayers",
                                     Qt::DirectConnection,Q_RETURN_ARG(bool,allowed)) &&
           allowed)return true;
        QMessageBox::information(this,i18n("Transform in progress"),
            i18n("Apply or reset the current transform before changing layers."));
        return false;
    }

    void chooseGroupLayers(KisNodeSP origin,const KisImageSP &image)
    {
        if(m_reorderPending || m_clipBusy || m_clipPropertyPending ||
           !inImage(origin,image) || !origin->parent())return;
        // createQuickGroup operates on one native sibling list. A compact
        // clip wrapper is a real Krita group even when hidden in our UI:
        // never pass nodes from different parents to one grouping command.
        const KisNodeSP parent=origin->parent();
        QDialog dialog(window());
        dialog.setWindowTitle(i18n("Group layers"));
        auto *layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(i18n("Tap the layers to put in the group."),&dialog));
        auto *list=new QListWidget(&dialog);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:44px;}"));
        MobileTouchScroll::configureList(list);
        KisNodeList candidates;
        for(const auto &node:m_nodes) {
            if(!node || node->parent()!=parent ||
               !inImage(node,image) || !node->inherits("KisLayer") ||
               !node->isEditable())continue;
            candidates.append(node);
            auto *entry=new QListWidgetItem(node->name(),list);
            entry->setCheckState(node==origin || m_selected.contains(node)?
                                 Qt::Checked:Qt::Unchecked);
            entry->setFlags(entry->flags() & ~Qt::ItemIsUserCheckable);
        }
        connect(list,&QListWidget::itemClicked,&dialog,[](QListWidgetItem *item) {
            item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
        });
        layout->addWidget(list,1);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
        auto *group=buttons->addButton(i18n("Group"),QDialogButtonBox::AcceptRole);
        group->setMinimumHeight(44);
        group->setEnabled(!candidates.isEmpty());
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(group,&QPushButton::clicked,&dialog,[&dialog,list]() {
            for(int row=0;row<list->count();++row) {
                if(list->item(row)->checkState()==Qt::Checked) {
                    dialog.accept();
                    return;
                }
            }
        });
        layout->addWidget(buttons);
        if(dialog.exec()!=QDialog::Accepted || !manager() ||
           m_reorderPending || m_clipBusy || m_clipPropertyPending ||
           m_engine.manager()->image().toStrongRef()!=image ||
           !inImage(origin,image) || origin->parent()!=parent)return;
        KisNodeList chosen;
        for(int row=0;row<candidates.size();++row) {
            const KisNodeSP node=candidates.at(row);
            if(list->item(row)->checkState()==Qt::Checked &&
               inImage(node,image) && node->parent()==parent &&
               node->inherits("KisLayer") && node->isEditable())
                chosen.append(node);
        }
        if(chosen.isEmpty())return;
        auto *nodes=manager();
        if(!nodes || !nodes->canMoveLayers(chosen,false))return;
        if(auto *transform=activeTransform()) {
            if(!canRetarget(transform))return;
            m_engine.freehandBrush();
        }
        // Leaving Transform, activating a layer and applying a multi-row
        // selection may trigger synchronous native node notifications.
        // Never submit the subsequent group operation against a stale
        // image, a detached child, or a newly changed sibling list.
        const auto stillValid=[this,image,parent,nodes,&chosen]() {
            if(!m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=image ||
               manager()!=nodes || !inImage(parent,image) ||
               !nodes->canMoveLayers(chosen,false))return false;
            for(const KisNodeSP &node:chosen)
                if(!node || node->parent()!=parent ||
                   !inImage(node,image) || !node->isEditable())
                    return false;
            return true;
        };
        if(!stillValid())return;
        m_active=chosen.contains(origin)?origin:chosen.first();
        m_selected=chosen;
        applySelection();
        if(!stillValid())return;
        const KisNodeList actual=nodes->selectedNodes();
        if(actual.size()!=chosen.size())return;
        for(const KisNodeSP &node:chosen)
            if(!actual.contains(node))return;
        nodes->createQuickGroup();
    }

    void tapLayer(KisNodeSP node,QTreeWidgetItem *item)
    {
        auto *view=m_engine.manager();
        if(!item || !view || !manager() ||
           !inImage(node,view->image().toStrongRef()))return;
        if(allowsMultiple()) {
            QPointer<KoToolBase> transform=activeTransform();
            if(m_engine.activeTool().section(QLatin1Char('/'),-1)==
               QStringLiteral("KisToolTransform") &&
               (!transform || !canRetarget(transform)))return;
            if(editingGlobalSelection(manager())) {
                m_selected.clear();
                m_active.clear();
            }
            if(m_selected.contains(node)) {
                // Native transform requires at least one target. A second
                // tap on the last selected layer opens its actions instead
                // of clearing the selection and leaving a stale active node.
                if(m_selected.size()==1) {
                    showLayerActions(node,item);
                    return;
                }
                m_selected.removeAll(node);
                if(m_active==node)m_active=m_selected.last();
            } else {
                m_selected.append(node);
                m_active=node;
            }
            applySelection();
            if(transform)QMetaObject::invokeMethod(transform.data(),"mobileRetargetLayers");
        } else if(!editingGlobalSelection(manager()) && m_active==node &&
                  m_selected.size()==1 && m_selected.contains(node)) {
            showLayerActions(node,item);
        } else {
            m_selected=KisNodeList{node};
            m_active=node;
            applySelection();
        }
    }

    void applySelection()
    {
        auto *m=manager();
        if(!m)return;
        const KisNodeList nodes=selectedTargets();
        // Keep the visual rail selection consistent with the targets
        // actually submitted to Krita's native transform tool.
        if(allowsMultiple())m_selected=nodes;
        if(!nodes.isEmpty()) {
            if(!nodes.contains(m_active))m_active=nodes.last();
            if(m->activeNode()!=m_active)m->slotUiActivatedNode(m_active);
        }
        m->slotSetSelectedNodes(nodes);
        updateRows();
    }
    void updateLayerName()
    {
        if(!m_layerName)return;
        const QString name=editingGlobalSelection(manager())?i18n("Editing selection"):
            m_active?m_active->name():QString();
        const QString display=m_layerName->fontMetrics().elidedText(
            name,Qt::ElideRight,qMax(0,(m_horizontalRail?height():width())-14));
        // The layer rail refreshes during painting. Avoid spurious Qt
        // label updates and accessibility events while nothing changed.
        if(m_layerName->text()!=display)m_layerName->setText(display);
        if(m_layerName->toolTip()!=name)m_layerName->setToolTip(name);
        if(m_layerName->accessibleName()!=name)
            m_layerName->setAccessibleName(name);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        updateLayerName();
    }

    void updateRows()
    {
        updateLayerName();
        // Avoid repolishing every QWidget on routine selection updates.
        // Qt setStyleSheet() invalidates style/layout even for identical
        // strings; only changed rows need that work.
        const auto setStyleIfChanged=[](QWidget *widget,const QString &style) {
            if(widget->styleSheet()!=style)widget->setStyleSheet(style);
        };
        for(int i=0;i<m_nodes.size();++i) {
            auto *item=m_items.value(i,nullptr);
            if(!item)continue;
            const KisNodeSP node=m_nodes.at(i);
            const bool highlighted=m_selected.contains(node);
            const bool active=highlighted && m_active==node;
            const bool group=node->inherits("KisGroupLayer");
            const bool mask=node->inherits("KisMask");
            const bool clipped=inheritsAlpha(node);
            const bool clipBase=m_clipBases.contains(node.data());
            const QColor background=active?QColor(46,103,139):
                highlighted?QColor(43,82,114):
                group?QColor(37,66,83):QColor(42,55,68);
            if(QFrame *frame=m_rowFrames.value(i).data()) {
                const QString edge=mask?QStringLiteral("#f0b96c"):
                    (clipped||clipBase)?QStringLiteral("#46b5a5"):
                    group?QStringLiteral("#74acda"):
                    m_rowDepths.value(i)>0?QStringLiteral("#607c92"):
                                        QStringLiteral("#536879");
                setStyleIfChanged(frame,QStringLiteral(
                    "QFrame#mobileLayerTileRow{background:%1;"
                    "border-%2:%3px solid %4;border-radius:5px;}")
                    .arg(background.name(),m_horizontalRail?QStringLiteral("bottom"):QStringLiteral("left"))
                    .arg(clipped?4:1).arg(edge));
            }
            if(QToolButton *thumb=m_rowButtons.value(i).data()) {
                if(group) {
                    setStyleIfChanged(thumb,active?
                        "QToolButton{background:#365c78;border:2px solid #f1949d;border-radius:6px;}":
                        "QToolButton{background:#243848;border:1px solid #577d96;border-radius:6px;}");
                } else if(mask) {
                    // Type text is deliberately visible rather than an
                    // unloaded thumbnail; preserve selection highlights.
                    setStyleIfChanged(thumb,active?
                        "QToolButton{background:#795737;color:#fff5dc;"
                        "border:3px solid #ec7683;border-radius:5px;"
                        "font-size:10px;font-weight:bold;}":
                        highlighted?
                        "QToolButton{background:#795737;color:#fff5dc;"
                        "border:3px solid #7fc3eb;border-radius:5px;"
                        "font-size:10px;font-weight:bold;}":
                        "QToolButton{background:#5c4736;color:#fff5dc;"
                        "border:2px solid #e7ad67;border-radius:5px;"
                        "font-size:10px;font-weight:bold;}");
                } else {
                    setStyleIfChanged(thumb,active?
                        "QToolButton{background:#808b97;color:white;"
                        "border:3px solid #ec7683;border-radius:5px;}":
                        highlighted?
                        "QToolButton{background:#697d90;color:white;"
                        "border:3px solid #7fc3eb;border-radius:5px;}":
                        "QToolButton{background:#6a7480;color:white;"
                        "border:2px solid #8a9dac;border-radius:5px;}");
                }
            }
            item->setToolTip(0,allowsMultiple()?
                (highlighted?i18n("Tap to remove from selection"):i18n("Tap to add to selection")):
                (active?i18n("Tap again for layer options"):i18n("Tap to select")));
        }
    }

    // Qt's item widgets stay as small touch-friendly cards. Image previews
    // are only decoded for visible rows, not all 500 layers on every stroke.
    void updateVisibleLayerThumbnails()
    {
        // Clearing or laying out the horizontal rows can emit scrollbar
        // signals while their items and thumbnail state are being replaced.
        if(m_reloading || !m_tree || !isVisible())return;
        const QRect viewport=railViewport()->rect().adjusted(-60,-60,60,60);
        const qint64 now=m_thumbnailClock.elapsed();
        for(int i=0;i<m_nodes.size();++i) {
            if(m_thumbnailReady.value(i,false))continue;
            if(m_thumbnailRetryAfter.value(i,0)>now)continue;
            QTreeWidgetItem *item=m_items.value(i);
            QToolButton *button=m_rowButtons.value(i);
            KisNodeSP node=m_nodes.value(i);
            if(!item || !button || !node)continue;
            if(!layerRect(item).intersects(viewport))continue;
            if(!node->inherits("KisGroupLayer") &&
               !node->inherits("KisMask")) {
                const QImage thumb=node->createThumbnail(
                    59,59,Qt::KeepAspectRatio);
                if(thumb.isNull()) {
                    // A native projection can temporarily have no
                    // thumbnail. Back off failed attempts rather than
                    // requesting the same expensive image every UI tick.
                    m_thumbnailRetryAfter[i]=now+1500;
                    continue;
                }
                button->setIcon(QIcon(QPixmap::fromImage(thumb)));
                const int extent=qMax(12,qMin(button->width(),button->height())-10);
                button->setIconSize(QSize(extent,extent));
            }
            m_thumbnailRetryAfter.remove(i);
            m_thumbnailReady[i]=true;
        }
    }

    static QIcon groupFolderIcon(bool expanded)
    {
        const auto draw=[](bool open) {
            QPixmap icon(32,32);
            icon.fill(Qt::transparent);
            QPainter p(&icon);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(open?QColor("#ffd084"):QColor("#f4f8ff"),2,Qt::SolidLine,
                          Qt::RoundCap,Qt::RoundJoin));
            p.setBrush(open?QColor("#9b682f"):QColor("#365167"));
            QPainterPath folder;
            folder.moveTo(4,25);
            folder.lineTo(4,8);
            folder.lineTo(12,8);
            folder.lineTo(15,11);
            folder.lineTo(27,11);
            folder.lineTo(27,25);
            folder.closeSubpath();
            p.drawPath(folder);
            if(open) {
                QPainterPath flap;
                flap.moveTo(4,25);
                flap.lineTo(9,16);
                flap.lineTo(30,16);
                flap.lineTo(26,25);
                flap.closeSubpath();
                p.setBrush(QColor("#b78142"));
                p.drawPath(flap);
            }
            return QIcon(icon);
        };
        static const QIcon closed=draw(false),open=draw(true);
        return expanded?open:closed;
    }

    void makeLayerRow(QTreeWidgetItem *item,KisNodeSP node,int depth)
    {
        if(!item || !node)return;
        const bool group=node->inherits("KisGroupLayer");
        const bool mask=node->inherits("KisMask");
        const bool clipped=inheritsAlpha(node);
        const int cross=m_horizontalRail?horizontalTileSize(height()):verticalTileSize(width());
        const int extent=MobileLayerRailGeometry::tileExtent(cross,m_horizontalRail,group,mask);
        const int rowLength=extent+4;
        int groupDepth=group?1:0;
        for(auto *parent=item->parent();parent;parent=parent->parent()) {
            const auto ancestor=m_nodes.value(parent->data(0,Qt::UserRole).toInt());
            if(ancestor && ancestor->inherits("KisGroupLayer"))++groupDepth;
        }
        const int gutter=2+qMin(5,groupDepth)*3+(mask?6:0);
        const int thickness=cross+4;
        item->setSizeHint(0,QSize(0,rowLength));
        auto *frame=new QFrame(m_horizontalRail?m_horizontalBody:static_cast<QWidget*>(m_tree));
        frame->setObjectName(QStringLiteral("mobileLayerTileRow"));
        frame->setFixedHeight(m_horizontalRail?thickness:rowLength);
        if(m_horizontalRail)frame->setFixedWidth(rowLength);
        frame->setAttribute(Qt::WA_AcceptTouchEvents,true);
        frame->installEventFilter(this);
        const auto orient=[this,thickness](const QRect &rect) {
            return m_horizontalRail?MobileLayerRailGeometry::rotateTileRect(rect,thickness):rect;
        };
        const QRect card(gutter+(clipped?4:0),2,thickness-2*gutter-(clipped?4:0),extent);
        auto *tile=new MobileLayerTileButton(frame,m_horizontalRail);
        tile->setGeometry(orient(card));
        tile->setToolButtonStyle(mask?Qt::ToolButtonTextOnly:Qt::ToolButtonIconOnly);
        if(mask) {
            tile->setText(maskKind(node));
            if(node->parent())tile->setAccessibleName(i18n("%1 mask on %2",node->name(),node->parent()->name()));
        }
        const auto base=clipped?clipBaseFor(node):KisNodeSP();
        tile->setToolTip((base?
            i18n("%1 — clipped to %2",node->name(),base->name()):
            mask && node->parent()?
            i18n("%1 — mask attached to %2").arg(node->name(),
                                                   node->parent()->name()):
            node->name()) + QStringLiteral(
            "\nTap: select; tap again: layer options"));
        if(base)tile->setAccessibleDescription(i18n("Clipped to %1",base->name()));
        tile->setAttribute(Qt::WA_AcceptTouchEvents,true);
        tile->installEventFilter(this);
        QObject::connect(tile,&QToolButton::pressed,this,[this,node,tile]() {
            armMouseLayer(node,tile,QCursor::pos());
        });
        QObject::connect(tile,&QToolButton::released,this,[this,node]() {
            // The pen's native TabletRelease, not Qt's synthetic mouse
            // button release, must complete the stylus drag.
            if(m_mouseNode==node && !m_penTracking)finishMouseLayer();
        });
        QObject::connect(tile,&QToolButton::clicked,this,[this,node,item]() {
            if(m_suppressedClickNode==node &&
               QDateTime::currentMSecsSinceEpoch()<m_suppressedClickUntil)
                return;
            tapLayer(node,item);
        });
        const int count=qobject_cast<KisPaintLayer*>(node.data())?3:2;
        const int overlay=group?qMin(30,(card.width()-2)/2):qMin(22,(extent-4)/count);
        // The rotation maps the card's right edge to
        // the top. Keep the controls on the canvas-facing edge.
        const bool controlsOnRight=m_controlsAtEnd!=m_horizontalRail;
        const int iconX=controlsOnRight?card.right()-overlay+1:card.left();
        const int gap=qMax(0,(extent-count*overlay)/(count+1));
        const auto addOverlay=[&](const QRect &geometry,const QIcon &icon,const QString &tooltip) {
            auto *button=new MobileLayerTileButton(frame,m_horizontalRail);
            button->setGeometry(orient(geometry));
            button->setIcon(icon);
            const int iconSize=overlay-(group?4:3);
            button->setIconSize(QSize(iconSize,iconSize));
            button->setAccessibleName(tooltip);
            button->setToolTip(tooltip);
            button->setStyleSheet(
                "QToolButton{background:#34343a;color:#f3faff;"
                "border:1px solid #66666d;border-radius:4px;padding:0;}"
                "QToolButton:checked{background:#674f35;border-color:#efb969;}"
                "QToolButton:pressed{background:#387d9c;}");
            return button;
        };
        const auto iconRect=[&](int n) {
            return QRect(iconX,card.top()+gap*(n+1)+overlay*n,overlay,overlay);
        };
        const auto propertyButton=[&](const QRect &geometry,const QString &icon,const QString &tooltip,
                                      const KoID &property,bool desired) {
            auto *button=addOverlay(geometry,QIcon(icon),tooltip);
            connect(button,&QToolButton::clicked,this,[this,node,property,desired]() {
                setLayerProperty(node,property,desired);
                QTimer::singleShot(0,this,[this]() {reload();});
            });
        };
        const int centered=card.center().y()-overlay/2;
        propertyButton(group?QRect(iconX,centered,overlay,overlay):iconRect(0),
                       node->visible()?QStringLiteral(":/light_visible.svg"):
                                       QStringLiteral(":/light_novisible.svg"),
                       group?i18n("Show or hide this group"):i18n("Show or hide this layer"),
                       KisLayerPropertiesIcons::visible,!node->visible());
        if(group) {
            const bool expanded=!node->collapsed();
            const int folderX=controlsOnRight?card.left():card.right()-overlay+1;
            auto *folder=addOverlay(QRect(folderX,centered,overlay,overlay),groupFolderIcon(expanded),
                                    expanded?i18n("Collapse group"):i18n("Expand group"));
            folder->setCheckable(true);
            folder->setChecked(expanded);
            connect(folder,&QToolButton::clicked,this,[this,node,folder](bool open) mutable {
                if(auto *groupItem=itemForNode(node)) {
                    groupItem->setExpanded(open);
                    node->setCollapsed(!open);
                    folder->setIcon(groupFolderIcon(open));
                    const auto label=open?i18n("Collapse group"):i18n("Expand group");
                    folder->setToolTip(label);
                    folder->setAccessibleName(label);
                    QTimer::singleShot(0,this,[this]() {reload();});
                }
            });
        } else {
            propertyButton(iconRect(1),node->userLocked()?QStringLiteral(":/light_layer-locked.svg"):
                                                         QStringLiteral(":/light_layer-unlocked.svg"),
                           i18n("Lock or unlock this layer"),KisLayerPropertiesIcons::locked,!node->userLocked());
            if(auto *paint=qobject_cast<KisPaintLayer*>(node.data())) {
                propertyButton(iconRect(2),paint->alphaLocked()?QStringLiteral(":/transparency-locked.svg"):
                                                                QStringLiteral(":/transparency-unlocked.svg"),
                               i18n("Alpha lock: affects ONLY this layer"),
                               KisLayerPropertiesIcons::alphaLocked,!paint->alphaLocked());
            }
        }
        QString kind;
        if(node->inherits("KisShapeLayer"))kind=i18n("VECTOR");
        else if(node->inherits("KisGeneratorLayer"))kind=i18n("FILL");
        else if(node->inherits("KisAdjustmentLayer"))kind=i18n("FILTER");
        else if(node->inherits("KisFileLayer"))kind=i18n("FILE");
        else if(node->inherits("KisCloneLayer"))kind=i18n("CLONE");
        if(!mask && !clipped && !kind.isEmpty()) {
            auto *marker=new MobileLayerNameLabel(frame);
            marker->setRotated(m_horizontalRail);
            marker->setRotatedBackground(QColor("#275b73"));
            marker->setGeometry(orient(QRect(card.left()+2,card.bottom()-18,
                                              qMin(47,card.width()-4),19)));
            marker->setText(kind);
            marker->setAlignment(Qt::AlignCenter);
            marker->setStyleSheet(
                "QLabel{background:#275b73;color:#f0faff;"
                "border-radius:4px;font-weight:bold;font-size:10px;}");
            marker->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        if(!m_horizontalRail)m_tree->setItemWidget(item,0,frame);
        else frame->hide();
        m_rowFrames.append(frame);
        m_rowButtons.append(tile);
        m_rowDepths.append(depth);
        m_thumbnailReady.append(group||mask);
    }

    // The tree already stores group ancestry and expansion. Draw guides
    // across the visible rows without changing item geometry or hit tests.
    class LayerGuides final : public QWidget
    {
    public:
        struct Edge { int depth; bool top; bool bottom; };
        struct Clip { int depth; bool above; bool below; bool clipped; bool base; };
        LayerGuides(QWidget *parent,QVector<Edge> edges,QVector<Clip> clips,const QRect &tile,bool horizontal)
            : QWidget(parent),m_edges(std::move(edges)),m_clips(std::move(clips)),m_tile(tile),
              m_horizontal(horizontal)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    protected:
        void paintEvent(QPaintEvent *) override
        {
            static const QColor colors[]={QColor("#d3f3ff"),QColor("#ffd985"),
                                          QColor("#dbbcff"),QColor("#ffaec8")};
            QPainter painter(this);
            const int cross=m_horizontal?height():width();
            const int length=m_horizontal?width():height();
            QTransform transform;
            if(m_horizontal) {
                transform.translate(0,height()-1);
                transform.rotate(-90);
                painter.setTransform(transform);
            }
            painter.save();
            const QRect content(QPoint(),QSize(cross,length));
            painter.setClipRegion(QRegion(content).subtracted(
                QRegion(transform.inverted().mapRect(m_tile))));
            for(const auto &edge:m_edges) {
                const int left=2+qMin(5,edge.depth)*3+qMax(0,edge.depth-5);
                const int right=cross-1-left;
                painter.setPen(QPen(colors[edge.depth%4],edge.depth<5?2:1));
                painter.drawLine(left,0,left,length-1);
                painter.drawLine(right,0,right,length-1);
                if(edge.top)painter.drawLine(left,1,right,1);
                if(edge.bottom)painter.drawLine(left,length-2,right,length-2);
            }
            painter.restore();
            const int middle=length/2;
            const QColor clipColor("#65f3db");
            painter.setPen(QPen(clipColor,2));
            painter.setBrush(clipColor);
            for(const auto &clip:m_clips) {
                const int x=cross/4+qMin(6,clip.depth)*4;
                if(clip.above)painter.drawLine(x,0,x,middle);
                if(clip.below)painter.drawLine(x,middle,x,length);
                if(clip.clipped) {
                    painter.drawLine(x,middle,x+9,middle);
                    painter.drawLine(x+5,middle-4,x+9,middle);
                    painter.drawLine(x+5,middle+4,x+9,middle);
                }
                if(clip.base)painter.drawEllipse(QPoint(x,middle),3,3);
            }
        }
    private:
        QVector<Edge> m_edges;
        QVector<Clip> m_clips;
        QRect m_tile;
        bool m_horizontal;
    };

    QList<QTreeWidgetItem*> visibleGroups(QTreeWidgetItem *item) const
    {
        QList<QTreeWidgetItem*> groups;
        for(auto *entry=item;entry;entry=entry->parent()) {
            const auto node=m_nodes.value(entry->data(0,Qt::UserRole).toInt());
            if(node && node->inherits("KisGroupLayer") &&
               entry->isExpanded() && entry->childCount())
                groups.prepend(entry);
        }
        return groups;
    }

    void addLayerGuides()
    {
        const auto visible=visibleItems();
        QHash<const KisNode*,int> indexForNode;
        m_clipBases.clear();
        for(int i=0;i<visible.size();++i) {
            const int index=visible.at(i)->data(0,Qt::UserRole).toInt();
            indexForNode.insert(m_nodes.at(index).data(),i);
        }
        struct ClipRun { int depth=0; QSet<int> clipped; };
        QMap<int,ClipRun> runsByBase;
        QVector<QVector<LayerGuides::Clip>> clips(visible.size());
        for(int i=0;i<visible.size();++i) {
            const int index=visible.at(i)->data(0,Qt::UserRole).toInt();
            const auto node=m_nodes.at(index);
            if(!inheritsAlpha(node))continue;
            const auto base=clipBaseFor(node);
            const int end=indexForNode.value(base.data(),-1);
            if(end<=i) {
                clips[i].append({m_rowDepths.value(index),false,false,true,false});
                continue;
            }
            m_clipBases.insert(base.data());
            auto &run=runsByBase[end];
            run.depth=m_rowDepths.value(index);
            run.clipped.insert(i);
        }
        for(auto run=runsByBase.cbegin();run!=runsByBase.cend();++run) {
            const int first=*std::min_element(run->clipped.cbegin(),run->clipped.cend());
            const int last=run.key();
            for(int i=first;i<=last;++i)
                clips[i].append({run->depth,i>first,i<last,run->clipped.contains(i),i==last});
        }
        for(int i=0;i<visible.size();++i) {
            auto *item=visible.at(i);
            const auto groups=visibleGroups(item);
            const auto next=visibleGroups(visible.value(i+1));
            QVector<LayerGuides::Edge> edges;
            for(int depth=0;depth<groups.size();++depth)
                edges.append({depth,groups.at(depth)==item,!next.contains(groups.at(depth))});
            if(edges.isEmpty() && clips.at(i).isEmpty())continue;
            const int index=item->data(0,Qt::UserRole).toInt();
            auto *frame=m_rowFrames.value(index).data();
            auto *tile=m_rowButtons.value(index).data();
            if(!frame || !tile)continue;
            const int guideWidth=(m_compactRail?qBound(73,width()-9,111):73)+4;
            auto *guides=new LayerGuides(frame,edges,clips.at(i),tile->geometry(),m_horizontalRail);
            guides->setGeometry(0,0,m_horizontalRail?frame->width():guideWidth,frame->height());
            guides->show();
        }
    }

    static bool editingGlobalSelection(KisNodeManager *nodes)
    {
        if(!nodes)return false;
        const KisNodeSP active=nodes->activeNode();
        return active && KisNodeManager::isNodeHidden(active,true) &&
            !KisNodeManager::isNodeHidden(active,false) &&
            nodes->selectedNodes()==KisNodeList{active};
    }

    void reload()
    {
        if(m_reloading || m_deleteImage || m_reorderPending || m_clipBusy || m_clipPropertyPending ||
           m_mouseNode || !m_touches.isEmpty())return;
        const QScopedValueRollback<bool> refreshing(m_reloading,true);
        auto *m=manager();
        auto *view=m_engine.manager();
        if(!m || !view)return;
        const auto image=view->image().toStrongRef();
        if(!image || !image->root()) {
            clearHorizontalRows();
            m_tree->clear();
            m_nodes.clear();
            m_items.clear();
            m_rowFrames.clear();
            m_rowButtons.clear();
            m_rowDepths.clear();
            m_thumbnailReady.clear();
            m_thumbnailRetryAfter.clear();
            m_clipBases.clear();
            m_selected.clear();
            m_signature=engineSignature();
            m_active.clear();
            updateLayerName();
            return;
        }
        const int scroll=railScrollBar()->value();

        const KisNodeList native=m->selectedNodes();
        const KisNodeSP engineActive=view->activeNode();
        const KisNodeSP previousActive=m_active;
        m_active=engineActive;
        // Edit Selection selects the global mask explicitly. Ordinary
        // layer operations still need a visible layer as their target.
        const bool editingSelection=editingGlobalSelection(m) &&
            inImage(engineActive,image);
        if(editingSelection) {
            // Keep the explicit native Edit Selection target off the layer rail.
            m_active.clear();
        } else if(!inImage(m_active,image) ||
                  KisNodeManager::isNodeHidden(m_active,true)) {
            m_active.clear();
            if(inImage(previousActive,image) &&
               !KisNodeManager::isNodeHidden(previousActive,true))
                m_active=previousActive;
            if(!m_active) {
                KisNodeList visible;
                logicalChildren(image->root(),visible);
                for(auto it=visible.crbegin();it!=visible.crend();++it) {
                    const auto &node=*it;
                    if(node->inherits("KisLayer") &&
                       !isCompactClipWrapper(node) &&
                       !KisNodeManager::isNodeHidden(node,true)) {
                        m_active=node;
                        break;
                    }
                }
            }
        }
        m_selected.clear();
        for(const KisNodeSP &node:native) {
            if(inImage(node,image) &&
               !KisNodeManager::isNodeHidden(node,true) &&
               !m_selected.contains(node))
                m_selected.append(node);
        }
        // Native node signals can rebuild the rail while Transform or
        // Move is already active. Normalize that incoming selection too:
        // a group and its child must not become two transform targets.
        // Keep explicit Edit Selection handling on the global mask.
        if(allowsMultiple() && !editingSelection) {
            const auto hidden=[](const KisNodeSP &node) {
                return KisNodeManager::isNodeHidden(node,true);
            };
            // Match tool-entry semantics: when native selection is
            // empty, a visible active layer remains the transform target.
            // Otherwise a rail refresh can silently leave Move/Transform
            // without a target until the next explicit layer tap.
            m_selected=MobileLayerIdentity::transformTargets(
                image->root(),m_selected,m_active,hidden,false);
            if(!m_selected.isEmpty() && !m_selected.contains(m_active))
                m_active=m_selected.last();
        } else if(!allowsMultiple() && m_active) {
            m_selected=KisNodeList{m_active};
        }
        if(m_active && m_active!=engineActive)
            m->slotUiActivatedNode(m_active);
        if(!editingSelection && native!=m_selected)
            m->slotSetSelectedNodes(m_selected);

        clearHorizontalRows();
        m_tree->clear();
        m_nodes.clear();m_items.clear();
        m_rowFrames.clear();m_rowButtons.clear();
        m_rowDepths.clear();m_thumbnailReady.clear();
        m_thumbnailRetryAfter.clear();
        std::function<void(KisNodeSP,QTreeWidgetItem*,int)> visit;
        visit=[&](KisNodeSP parent,QTreeWidgetItem *parentItem,int depth) {
            if(!parent || depth>20)return;
            for(KisNodeSP node=parent->lastChild();node && m_nodes.size()<500;
                node=node->prevSibling()) {
                // Match Krita's normal layer docker: a top-level
                // selection mask belongs to the canvas selection.
                // User-created selection masks *within* a layer stay
                // visible, with the MASK badge and type label.
                if(node->inherits("KisReferenceImagesLayer") ||
                   KisNodeManager::isNodeHidden(node,true))continue;
                if(isCompactClipWrapper(node)) {
                    // Krita keeps this real group for alpha inheritance,
                    // but Compact UI shows its children directly instead.
                    visit(node,parentItem,depth);
                    continue;
                }
                const int index=m_nodes.size();
                m_nodes.append(node);
                auto *item=new QTreeWidgetItem;
                item->setText(0,node->name());
                if(node->inherits("KisGroupLayer")) {
                    QFont font=item->font(0);
                    font.setBold(true);
                    item->setFont(0,font);
                    item->setForeground(0,QBrush(QColor(171,217,247)));
                }
                item->setData(0,Qt::UserRole,index);
                if(parentItem)parentItem->addChild(item);
                else m_tree->addTopLevelItem(item);
                m_items.append(item);
                // A paint layer's children are attached masks. Unlike
                // groups, paint layers have no disclosure arrow in the
                // compact rail; keep those masks visible immediately.
                const bool showAttachedMasks=node->childCount()>0 &&
                    !node->inherits("KisGroupLayer");
                const bool expandedByUser=!node->collapsed();
                item->setExpanded(showAttachedMasks || expandedByUser);
                makeLayerRow(item,node,depth);
                if(node->childCount()>0) {
                    visit(node,item,depth+1);
                    item->setExpanded(showAttachedMasks || expandedByUser);
                }
            }
        };
        visit(image->root(),nullptr,0);
        if(m_horizontalRail) {
            int length=0;
            for(auto *item:visibleItems()) {
                auto *frame=m_rowFrames.value(item->data(0,Qt::UserRole).toInt()).data();
                m_horizontalRows->addWidget(frame);
                frame->show();
                length+=frame->width();
            }
            m_horizontalBody->setMinimumWidth(length);
        }
        addLayerGuides();
        updateRows();
        railScrollBar()->setValue(scroll);
        m_signature=engineSignature();
        if(onContentHeightChanged)onContentHeightChanged();
        QTimer::singleShot(0,this,[this]() {
            updateVisibleLayerThumbnails();
        });
    }
    MobileEngineBridge m_engine;
    const bool m_compactRail;
    QTreeWidget *m_tree=nullptr;
    QBoxLayout *m_railLayout=nullptr;
    QWidget *m_railHeader=nullptr;
    QBoxLayout *m_railHeaderLayout=nullptr;
    QPushButton *m_addLayerButton=nullptr,*m_minimizeLayerButton=nullptr;
    MobileLayerNameLabel *m_layerName=nullptr;
    bool m_controlsAtEnd=true;
    QScrollArea *m_horizontalScroll=nullptr;
    QWidget *m_horizontalBody=nullptr;
    QHBoxLayout *m_horizontalRows=nullptr;
    bool m_horizontalRail=false;
    QVector<KisNodeSP> m_nodes;
    QVector<QTreeWidgetItem*> m_items; // owned by m_tree
    QVector<QPointer<QFrame>> m_rowFrames;
    QVector<QPointer<QToolButton>> m_rowButtons;
    QVector<int> m_rowDepths;
    QSet<const KisNode*> m_clipBases;
    QVector<bool> m_thumbnailReady;
    QHash<int,qint64> m_thumbnailRetryAfter;
    QElapsedTimer m_thumbnailClock;
    qint64 m_lastActiveThumbnailRefresh=-2500;
    KisNodeList m_selected;
    KisNodeSP m_active;
    bool m_clipBusy=false;
    bool m_clipPropertyPending=false;
    KisImageSP m_clipPropertyImage;
    KisImageSP m_clipImage;
    QList<KisNodeSP> m_clipParents;
    KisNodeSP m_clipRestoreActive;
    KisNodeList m_clipPendingWrappers;
    std::function<void()> m_clipAfterUnwrap;
    KisNodeSP m_clipMoveSource;
    KisNodeSP m_clipExpectedParent;
    KisNodeSP m_clipExpectedBelow;
    struct TileTouch {
        KisNodeSP node;
        QPoint start;
        QPoint last;
    };
    QHash<int,TileTouch> m_touches;
    KisNodeSP m_touchPrimary,m_touchPinchA,m_touchPinchB;
    QPointer<QToolButton> m_touchControl;
    QPoint m_touchLast;
    qreal m_touchPinchDistance=0;
    bool m_touchHeld=false,m_touchMoved=false,m_touchScrolling=false;
    bool m_touchPinching=false,m_touchPinchReady=false;
    QPointer<QTimer> m_touchHoldTimer,m_edgeScrollTimer;
    QPointer<QLabel> m_dragGhost;
    QPointer<QFrame> m_dropMarker;
    KisNodeSP m_mouseNode;
    QPointer<QToolButton> m_mouseTile;
    QPoint m_mouseStart,m_mouseLast;
    quint64 m_mouseHoldGeneration=0;
    bool m_mouseDown=false,m_mouseHeld=false,m_mouseMoved=false;
    bool m_mousePreviouslySelected=false,m_penTracking=false;
    KisNodeSP m_suppressedClickNode;
    qint64 m_suppressedClickUntil=0;
    QString m_signature;
    bool m_reorderPending=false,m_reloading=false;
    KisImageSP m_deleteImage;
};
#endif // Q_OS_ANDROID
