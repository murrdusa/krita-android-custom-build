/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile layers interface: a VIEW/CONTROLLER for Krita's existing node tree.
 * All mutations go through KisNodeManager (native undo/constraints).
 * No second layer compositor, no image copy, no hidden layer docker.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QFrame>
#include <QMenu>
#include <QInputDialog>
#include <QFont>
#include <QLineEdit>
#include <QScroller>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QPointer>
#include <QScrollBar>
#include <QBrush>
#include <QColor>
#include <QSet>
#include <QVector>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QAction>
#include <QTimer>
#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <functional>

#include "engine_bridge.h"
#include "native_layer_reorder_policy.h"
#include <QSettings>
#include <chrono>
#include <future>
#include <memory>
#include "kis_processing_applicator.h"
#include "commands/kis_image_layer_move_command.h"
#include "commands/KisNodeRenameCommand.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include <klocalizedstring.h>

class KritaMobileLayersPanel final : public QFrame
{
public:
    KritaMobileLayersPanel(const MobileEngineBridge &engine,
                           QWidget *parent=nullptr)
        : QFrame(parent),m_engine(engine)
    {
        setObjectName(QStringLiteral("MobileStudioLayersPanel"));
        setMinimumWidth(268);
        setMaximumWidth(386);
        setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
        setStyleSheet(
            "QFrame#MobileStudioLayersPanel{background:#27333f;"
            "border-left:2px solid #7599b4;}"
            "QLabel{color:#ecf3fc;font-size:14px;}"
            "QTreeWidget{background:#2e3e4e;color:#f0f5fb;"
            "border:1px solid #718b9f;font-size:14px;}"
            "QTreeWidget::item{min-height:48px;padding:4px;}"
            "QHeaderView::section{background:#364b5e;color:#e2effa;"
            "font-size:12px;border:0;padding:5px;}"
            "QPushButton{background:#435e74;color:#f6fbff;"
            "border:1px solid #729bb6;border-radius:7px;"
            "min-height:44px;padding:4px 7px;font-size:13px;}"
            "QPushButton:pressed{background:#5685a6;}"
            "QPushButton:disabled{color:#8696a3;background:#303d48;}"
            "QCheckBox{color:#f0f5fb;font-size:14px;min-height:44px;}"
            "QCheckBox::indicator{height:21px;width:21px;}");
        auto *layout=new QVBoxLayout(this);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(6);
        auto *top=new QHBoxLayout;
        auto *heading=new QLabel(i18n("Layers"),this);
        heading->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
        top->addWidget(heading);
        top->addStretch(1);
        m_multi=new QCheckBox(i18n("Multi-select"),this);
        top->addWidget(m_multi);
        auto *close=new QPushButton(i18n("Close"),this);
        top->addWidget(close);
        connect(close,&QPushButton::clicked,this,[this]() {
            if(onRequestClose)onRequestClose();
            else hide();
        });
        layout->addLayout(top);

        m_tree=new QTreeWidget(this);
        m_tree->setHeaderLabels({
            i18n("Layer / mask"),i18n("Shown"),i18n("Lock")});
        m_tree->setHeaderHidden(false);
        m_tree->setRootIsDecorated(true);
        m_tree->setIndentation(18);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Swipe-scroll without treating a finger drag as a row click.
        QScroller::grabGesture(m_tree->viewport(),QScroller::TouchGesture);
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        layout->addWidget(m_tree,1);
        m_summary=new QLabel(i18n("Select a layer"),this);
        layout->addWidget(m_summary);

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
            if(auto *m=manager()) {
                if(m->createNode(QStringLiteral("KisPaintLayer")))reload();
            }
        });
        addButton(creation,i18n("+ Group"),[this]() {
            if(auto *m=manager()) {
                if(m->createNode(QStringLiteral("KisGroupLayer")))reload();
            }
        });
        addButton(creation,i18n("Duplicate"),[this]() {
            if(auto *m=manager()) {
                m->duplicateActiveNode();
                reload();
            }
        });
        addButton(creation,i18n("Delete"),[this]() {
            auto *m=manager();
            const KisNodeList selection=selectedTargets();
            if(!m || selection.isEmpty())return;
            if(QMessageBox::question(this,i18n("Delete layers"),
                   i18np("Delete selected layer?", "Delete %1 selected layers?",
                         selection.size()),
                   QMessageBox::Yes|QMessageBox::No,
                   QMessageBox::No)!=QMessageBox::Yes)return;
            m->removeNode();
            reload();
        });
        layout->addLayout(creation);

        // Preserve access to the full range of Krita node classes,
        // rather than adding separate renderers or per-type models.
        // These actions invoke KisNodeManager::createNode(), including
        // Krita's built-in dialogs and undo/redo for each node type.
        auto *addType=new QPushButton(i18n("Add layer / mask…"),this);
        layout->addWidget(addType);
        connect(addType,&QPushButton::clicked,this,[this,addType]() {
            auto *view=m_engine.manager();
            auto *nodes=manager();
            if(!nodes || !view)return;
            const KisImageSP original=view->image().toStrongRef();
            if(!original)return;
            QMenu menu(this);
            menu.setStyleSheet(
                "QMenu{background:#2c4051;color:#f4f9fe;"
                "border:1px solid #769bb8;}"
                "QMenu::item{color:#f4f9fe;min-height:44px;"
                "padding:9px 14px;}"
                "QMenu::item:selected{background:#3b7394;"
                "color:#ffffff;}");
            QMenu *layers=menu.addMenu(i18n("Layer types"));
            QMenu *masks=menu.addMenu(i18n("Masks"));
            struct NewType { QAction *action; const char *type; };
            QVector<NewType> choices;
            auto add=[&](QMenu *submenu,const QString &label,
                         const char *type) {
                choices.append({submenu->addAction(label),type});
            };
            add(layers,i18n("Paint layer"),"KisPaintLayer");
            add(layers,i18n("Group layer"),"KisGroupLayer");
            add(layers,i18n("Vector layer"),"KisShapeLayer");
            add(layers,i18n("Filter layer"),"KisAdjustmentLayer");
            add(layers,i18n("Fill layer"),"KisGeneratorLayer");
            add(layers,i18n("Clone layer"),"KisCloneLayer");
            add(layers,i18n("File layer"),"KisFileLayer");
            add(masks,i18n("Transparency mask"),"KisTransparencyMask");
            add(masks,i18n("Filter mask"),"KisFilterMask");
            add(masks,i18n("Transform mask"),"KisTransformMask");
            add(masks,i18n("Selection mask"),"KisSelectionMask");
            add(masks,i18n("Colorize mask"),"KisColorizeMask");
            add(masks,i18n("Fast color overlay mask"),
                "FastColorOverlayFilterMask");
            const QAction *chosen=menu.exec(
                addType->mapToGlobal(QPoint(0,addType->height())));
            if(!chosen || !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=original)
                return;
            for(const NewType &candidate:choices) {
                if(chosen!=candidate.action)continue;
                if(nodes->createNode(QString::fromLatin1(candidate.type)))
                    reload();
                break;
            }
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

        connect(m_tree,&QTreeWidget::itemClicked,this,
                [this](QTreeWidgetItem *item,int column) {
            if(!item)return;
            const int index=item->data(0,Qt::UserRole).toInt();
            if(index<0 || index>=m_nodes.size())return;
            KisNodeSP node=m_nodes.at(index);
            if(!node || !node->parent())return;
            auto *m=manager();
            if(!m)return;
            if(column==1 || column==2) {
                // Eye / Lock columns operate on the tapped layer, not
                // the previously active one. Use Krita's native undoable
                // node commands instead of changing KisNode directly.
                m_engine.selectLayer(node);
                if(column==1)m->toggleVisibility();
                else m->toggleLock();
                reload();
                return;
            }
            if(m_multi->isChecked()) {
                if(m_selected.contains(node) && m_selected.size()>1)
                    m_selected.removeAll(node);
                else if(!m_selected.contains(node))
                    m_selected.append(node);
                // Keep one active node in the native model at all times.
                if(!m_selected.contains(m_active) || !m_active)
                    m_active=m_selected.last();
                applySelection();
            } else if(m_active==node && m_selected.size()==1 &&
                      m_selected.contains(node)) {
                // One tap selects; a second tap opens the layer menu.
                showLayerActions(node,item);
            } else {
                m_selected=KisNodeList{node};
                m_active=node;
                applySelection();
            }
        });
        connect(m_multi,&QCheckBox::toggled,this,[this](bool multi) {
            if(!multi && m_selected.size()>1) {
                KisNodeSP node=m_active ? m_active : m_selected.last();
                m_selected=KisNodeList{node};
                applySelection();
            }
            updateRows();
        });
        reload();
    }

    // Called by the host while the panel is visible. A lightweight
    // native-node signature prevents rebuilding the tree on every tick.
    // We never cache a duplicate document or layer model.
    std::function<void()> onRequestClose;
    void refreshFromEngine()
    {
        if(!isVisible() || m_reorderPending)return;
        if(engineSignature()!=m_signature)reload();
    }
    void refreshNow() {if(!m_reorderPending)reload();}

private:
    // A reorder is a Krita image stroke, not a legacy direct-command
    // edit. It enters the same document undo stack used by pen strokes.
    // Node order is bottom-to-top, whereas the displayed tree is
    // top-to-bottom. Use sibling anchors, not the old indexed move API:
    // that API treats "move up one" as a self-anchor and returns false.
    void reorderNode(KisNodeSP node,bool towardTop)
    {
        if(m_reorderPending || !node)return;
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
        if(!m_engine.selectLayer(node))return;

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

    void showLayerActions(KisNodeSP node,QTreeWidgetItem *item)
    {
        auto *m=manager();
        if(!m || !node || !node->parent() || !item)return;
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu{background:#2c4051;color:#f4f9fe;"
            "border:1px solid #769bb8;}"
            "QMenu::item{min-height:45px;padding:9px 14px;}"
            "QMenu::item:selected{background:#3b7394;}");
        QAction *rename=menu.addAction(i18n("Rename layer"));
        QAction *opacityAction=menu.addAction(
            i18n("Opacity… (%1%)").arg(
                (int(node->opacity())*100+127)/255));
        QAction *blendAction=menu.addAction(i18n("Blending mode…"));
        blendAction->setEnabled(node->colorSpace()!=nullptr);
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
        QAction *group=menu.addAction(i18n("Group selection"));
        QAction *clipping=menu.addAction(i18n("Krita clipping group"));
        QAction *ungroup=nullptr;
        if(node->inherits("KisGroupLayer"))
            ungroup=menu.addAction(i18n("Ungroup"));
        menu.addSeparator();
        QAction *remove=menu.addAction(i18n("Delete layer"));
        const QPoint point=m_tree->viewport()->mapToGlobal(
            m_tree->visualItemRect(item).bottomLeft());
        QAction *chosen=menu.exec(point);
        if(!chosen || !node || !node->parent())return;
        if(chosen==rename) {
            bool ok=false;
            const QString next=QInputDialog::getText(
                this,i18n("Rename layer"),i18n("Layer name"),
                QLineEdit::Normal,node->name(),&ok);
            if(ok && !next.trimmed().isEmpty() &&
               next.trimmed()!=node->name() && !m_reorderPending) {
                KisImageSP image=m_engine.manager() ?
                    m_engine.manager()->image().toStrongRef() : KisImageSP();
                KisNodeSP ancestor=node;
                while(ancestor && image && ancestor!=image->root())
                    ancestor=ancestor->parent();
                if(image && ancestor==image->root()) {
                    runNativeLayerStroke(image,
                        new KisNodeRenameCommand(
                            node,node->name(),next.trimmed()),
                        QStringLiteral("Native layer rename"));
                    return; // Refresh only after stroke finishes.
                }
            }
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
            const auto modes=KoCompositeOpRegistry::instance()
                                 .getCompositeOps(colorSpace);
            QStringList labels;
            QStringList ids;
            for(const auto &mode:modes) {
                if(!colorSpace->compositeOp(mode.id()))continue;
                QString label=mode.name();
                if(label.trimmed().isEmpty())label=mode.id();
                if(labels.contains(label))
                    label+=QStringLiteral(" [")+mode.id()+QStringLiteral("]");
                labels.append(label);
                ids.append(mode.id());
            }
            if(ids.isEmpty())return;
            QInputDialog dialog(this);
            dialog.setWindowTitle(i18n("Krita blending mode"));
            dialog.setLabelText(i18n("Layer blending mode"));
            dialog.setComboBoxEditable(false);
            dialog.setComboBoxItems(labels);
            const int current=ids.indexOf(node->compositeOpId());
            if(current>=0)dialog.setTextValue(labels.at(current));
            dialog.setStyleSheet(
                "QInputDialog{background:#283746;color:#f4f9fe;}"
                "QLabel{color:#f4f9fe;}"
                "QComboBox,QLineEdit{background:#eef3f7;color:#172530;"
                "min-height:43px;}"
                "QComboBox QAbstractItemView{background:#eef3f7;"
                "color:#172530;selection-background-color:#3878a4;}"
                "QPushButton{background:#446781;color:#f4f9fe;"
                "min-height:42px;}");
            if(dialog.exec()!=QDialog::Accepted ||
               !m_engine.manager() ||
               m_engine.manager()->image().toStrongRef()!=original ||
               m_reorderPending)return;
            KisNodeSP ancestor=node;
            while(ancestor && ancestor!=original->root())
                ancestor=ancestor->parent();
            if(ancestor!=original->root() ||
               node->colorSpace()!=colorSpace)return;
            const int index=labels.indexOf(dialog.textValue());
            if(index<0 || index>=ids.size())return;
            if(const KoCompositeOp *mode=colorSpace->compositeOp(ids.at(index)))
                m->setNodeCompositeOp(node,mode);
        } else if(chosen==properties) {
            m->nodeProperties(node);
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
            if(view->activeNode()!=node)
                m_engine.selectLayer(node);
            if(!m_engine.run(chosen==merge?"merge_layer":"flatten_layer"))
                qWarning() << "Mobile Studio: Krita layer operation unavailable";
        } else if(chosen==group) {
            m->createQuickGroup();
        } else if(chosen==clipping) {
            m->createQuickClippingGroup();
        } else if(chosen==ungroup) {
            m_engine.selectLayer(node);
            m->quickUngroup();
        } else if(chosen==remove) {
            if(QMessageBox::question(this,i18n("Delete layer"),
                    i18n("Delete %1?").arg(node->name()),
                    QMessageBox::Yes|QMessageBox::No,
                    QMessageBox::No)==QMessageBox::Yes) {
                m_engine.selectLayer(node);
                m->removeNode();
            }
        } else if(chosen==moveUp || chosen==moveDown) {
            reorderNode(node,chosen==moveUp);
            return; // Rebuild after the undoable move, not during it.
        } else {
            m_engine.selectLayer(node);
            if(chosen==duplicate)m->duplicateActiveNode();
        }
        reload();
    }
    QString engineSignature() const
    {
        auto *view=m_engine.manager();
        if(!view || !manager())return QStringLiteral("no-view");
        const auto image=view->image().toStrongRef();
        if(!image || !image->root())return QStringLiteral("no-image");
        QString key=QString::number(quintptr(image.data()),16);
        int count=0;
        std::function<void(KisNodeSP,int)> inspect;
        inspect=[&](KisNodeSP parent,int depth) {
            if(!parent || depth>20 || count>=500)return;
            for(KisNodeSP n=parent->lastChild();n && count<500;
                n=n->prevSibling()) {
                if(n->inherits("KisReferenceImagesLayer"))continue;
                ++count;
                key+=n->uuid().toString()+QLatin1Char('|')+n->name()+
                    QLatin1Char('|')+QString::number(n->visible())+
                    QString::number(n->userLocked())+
                    QString::number(n->opacity())+
                    QLatin1Char(':')+QString::number(depth)+QLatin1Char(';');
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

    KisNodeManager *manager() const
    {
        auto *view=m_engine.manager();
        return view?view->nodeManager():nullptr;
    }
    KisNodeList selectedTargets() const
    {
        KisNodeList result;
        for(const auto &node:m_selected)
            if(node && node->parent() && !result.contains(node))
                result.append(node);
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
    void applySelection()
    {
        auto *m=manager();
        if(!m || m_selected.isEmpty())return;
        KisNodeList nodes=selectedTargets();
        if(nodes.isEmpty())return;
        if(!nodes.contains(m_active))m_active=nodes.last();
        m->slotUiActivatedNode(m_active);
        m->slotSetSelectedNodes(nodes);
        updateRows();
    }
    void updateRows()
    {
        for(int i=0;i<m_nodes.size();++i) {
            auto *item=m_items.value(i,nullptr);
            if(!item)continue;
            const auto &node=m_nodes.at(i);
            const bool highlighted=m_selected.contains(node);
            const bool active=m_active==node;
            const bool group=node->inherits("KisGroupLayer");
            const QColor background=active?QColor(48,104,141):
                highlighted?QColor(43,83,115):
                group?QColor(34,73,103):QColor(46,62,78);
            for(int c=0;c<3;++c)item->setBackground(c,QBrush(background));
            item->setText(1,node->visible()?i18n("Yes"):i18n("No"));
            item->setText(2,node->userLocked()?i18n("Locked"):QStringLiteral("—"));
            item->setToolTip(1,i18n("Tap to toggle visibility (undoable)"));
            item->setToolTip(2,i18n("Tap to toggle lock (undoable)"));
            item->setToolTip(0,active?i18n("Tap again for layer options"):
                highlighted?i18n("Included in multi-selection"):
                i18n("Tap to select"));
        }
        if(m_summary) {
            const KisNodeList targets=selectedTargets();
            m_summary->setText(i18np("%1 layer selected","%1 layers selected",
                                   targets.size()));
        }
    }
    void reload()
    {
        if(m_reorderPending)return;
        auto *m=manager();
        auto *view=m_engine.manager();
        if(!m || !view)return;
        const auto image=view->image().toStrongRef();
        if(!image || !image->root()) {
            m_tree->clear();
            m_nodes.clear();
            m_items.clear();
            m_signature=engineSignature();
            if(m_summary)m_summary->setText(i18n("No document"));
            return;
        }
        const int scroll=m_tree->verticalScrollBar()->value();
        const bool hadPreviousTree=!m_nodes.isEmpty();
        QSet<KisNode*> expanded;
        for(int i=0;i<m_nodes.size();++i)
            if(auto *item=m_items.value(i,nullptr))
                if(item->isExpanded())expanded.insert(m_nodes[i].data());

        const KisNodeList native=m->selectedNodes();
        m_active=view->activeNode();
        m_selected.clear();
        for(auto node:native)
            if(node && node->parent() && !m_selected.contains(node))
                m_selected.append(node);
        if(m_selected.isEmpty() && m_active && m_active->parent())
            m_selected.append(m_active);

        m_tree->clear();
        m_nodes.clear();m_items.clear();
        std::function<void(KisNodeSP,QTreeWidgetItem*,int)> visit;
        visit=[&](KisNodeSP parent,QTreeWidgetItem *parentItem,int depth) {
            if(!parent || depth>20 || m_nodes.size()>=500)return;
            for(KisNodeSP node=parent->lastChild();node;node=node->prevSibling()) {
                if(node->inherits("KisReferenceImagesLayer"))continue;
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
                if(node->childCount()>0) {
                    visit(node,item,depth+1);
                    item->setExpanded(hadPreviousTree ?
                                      expanded.contains(node.data()) :
                                      !node->collapsed());
                }
            }
        };
        visit(image->root(),nullptr,0);
        updateRows();
        m_tree->verticalScrollBar()->setValue(scroll);
        m_signature=engineSignature();
    }
    MobileEngineBridge m_engine;
    QTreeWidget *m_tree=nullptr;
    QLabel *m_summary=nullptr;
    QCheckBox *m_multi=nullptr;
    QVector<KisNodeSP> m_nodes;
    QVector<QTreeWidgetItem*> m_items; // owned by m_tree
    KisNodeList m_selected;
    KisNodeSP m_active;
    QString m_signature;
    bool m_reorderPending=false;
};
#endif // Q_OS_ANDROID
