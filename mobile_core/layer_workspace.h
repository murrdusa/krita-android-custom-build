/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile layers interface: a VIEW/CONTROLLER for Krita's existing node tree.
 * All mutations go through KisNodeManager (native undo/constraints).
 * No second layer compositor, no image copy, no hidden layer docker.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QDialog>
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
#include <functional>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_node_manager.h"
#include "kis_node.h"
#include "kis_image.h"
#include <klocalizedstring.h>

class KritaMobileLayersPanel final : public QDialog
{
public:
    KritaMobileLayersPanel(KisMainWindow *main,KisViewManager *view,
                           QWidget *parent=nullptr)
        : QDialog(parent),m_main(main),m_view(view)
    {
        setWindowTitle(i18n("Layers"));
        setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
        setStyleSheet(
            "QDialog{background:#27333f;color:#ecf3fc;}"
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
        connect(close,&QPushButton::clicked,this,&QDialog::accept);
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
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        layout->addWidget(m_tree,1);
        m_summary=new QLabel(i18n("Select a layer"),this);
        layout->addWidget(m_summary);

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

        auto *operations=new QHBoxLayout;
        addButton(operations,i18n("Up"),[this]() {
            if(auto *m=manager()){m->raiseNode();reload();}
        });
        addButton(operations,i18n("Down"),[this]() {
            if(auto *m=manager()){m->lowerNode();reload();}
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
            Q_UNUSED(column);
            if(!item)return;
            const int index=item->data(0,Qt::UserRole).toInt();
            if(index<0 || index>=m_nodes.size())return;
            KisNodeSP node=m_nodes.at(index);
            if(!node)return;
            if(m_multi->isChecked()) {
                if(m_selected.contains(node) && m_selected.size()>1)
                    m_selected.removeAll(node);
                else if(!m_selected.contains(node))
                    m_selected.append(node);
                // Keep one active node in the native model at all times.
                if(!m_selected.contains(m_active) || !m_active)
                    m_active=m_selected.last();
                applySelection();
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

private:
    KisNodeManager *manager() const
    {
        return m_view?m_view->nodeManager():nullptr;
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
            const QColor background=active?QColor(96,65,71):
                highlighted?QColor(45,92,125):QColor(46,62,78);
            for(int c=0;c<3;++c)item->setBackground(c,QBrush(background));
            item->setText(1,node->visible()?i18n("Yes"):i18n("No"));
            item->setText(2,node->userLocked()?i18n("Locked"):QStringLiteral("—"));
            item->setToolTip(0,active?i18n("Active layer"):
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
        auto *m=manager();
        if(!m || !m_view)return;
        const auto image=m_view->image().toStrongRef();
        if(!image || !image->root())return;
        const int scroll=m_tree->verticalScrollBar()->value();
        QSet<KisNode*> expanded;
        for(int i=0;i<m_nodes.size();++i)
            if(auto *item=m_items.value(i,nullptr))
                if(item->isExpanded())expanded.insert(m_nodes[i].data());

        const KisNodeList native=m->selectedNodes();
        m_active=m_view->activeNode();
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
                item->setData(0,Qt::UserRole,index);
                if(parentItem)parentItem->addChild(item);
                else m_tree->addTopLevelItem(item);
                m_items.append(item);
                if(node->childCount()>0) {
                    visit(node,item,depth+1);
                    item->setExpanded(expanded.isEmpty() ||
                                      expanded.contains(node.data()));
                }
            }
        };
        visit(image->root(),nullptr,0);
        updateRows();
        m_tree->verticalScrollBar()->setValue(scroll);
    }
    QPointer<KisMainWindow> m_main;
    KisViewManager *m_view=nullptr;
    QTreeWidget *m_tree=nullptr;
    QLabel *m_summary=nullptr;
    QCheckBox *m_multi=nullptr;
    QVector<KisNodeSP> m_nodes;
    QVector<QTreeWidgetItem*> m_items; // owned by m_tree
    KisNodeList m_selected;
    KisNodeSP m_active;
};
#endif // Q_OS_ANDROID
