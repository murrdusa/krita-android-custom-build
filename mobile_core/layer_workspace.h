/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile Studio layers: a native Krita-image-backed tree, not a second layer
 * implementation. All edits route through MobileEngineBridge's undo-aware
 * operations. No Q_OBJECT or separate MOC target.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QBrush>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScroller>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QVector>
#include <functional>

#include "engine_bridge.h"

class MobileLayerWorkspace final : public QFrame {
public:
    explicit MobileLayerWorkspace(const MobileEngineBridge &engine,QWidget *parent)
        : QFrame(parent),m_engine(engine)
    {
        setObjectName(QStringLiteral("mobileNativeLayerWorkspace"));
        setMinimumWidth(260);
        setMaximumWidth(375);
        setStyleSheet(
            "QFrame#mobileNativeLayerWorkspace{background:#253342;"
            "border-left:2px solid #648ca9;}"
            "QLabel{color:#e7f2fb;font-size:13px;}"
            "QTreeWidget{background:#253342;color:#f5f7fa;"
            "border:0;font-size:13px;outline:none;}"
            "QTreeWidget::item{min-height:49px;}"
            "QTreeWidget::branch{background:#253342;}"
            "QHeaderView::section{background:#35475a;color:#d9e7f2;"
            "font-size:11px;padding:5px;border:0;}"
            "QPushButton,QToolButton{background:#405b72;color:white;"
            "border:1px solid #7199b4;border-radius:7px;"
            "min-height:39px;padding:3px;}"
            "QPushButton:checked,QToolButton:checked{background:#2b7196;"
            "border:2px solid #9acbe7;}"
            "QSlider::groove:horizontal{height:7px;background:#41576e;}"
            "QSlider::handle:horizontal{background:#b5daf1;width:21px;"
            "margin:-8px 0;border-radius:8px;}");
        auto *outer=new QVBoxLayout(this);
        outer->setContentsMargins(6,7,6,8);
        outer->setSpacing(5);
        auto *heading=new QHBoxLayout;
        auto *title=new QLabel(i18n("Layers"),this);
        title->setStyleSheet(QStringLiteral("font-weight:650;font-size:15px;"));
        heading->addWidget(title);
        heading->addStretch(1);
        m_multi=new QToolButton(this);
        m_multi->setText(i18n("Multi"));
        m_multi->setToolTip(i18n("Tap layers to add/remove from selection"));
        m_multi->setCheckable(true);
        heading->addWidget(m_multi);
        auto *close=new QToolButton(this);
        close->setText(i18n("Close"));
        close->setToolTip(i18n("Hide layer panel"));
        heading->addWidget(close);
        connect(close,&QToolButton::clicked,this,[this]() {
            if(onRequestClose)onRequestClose();
            else hide();
        });
        outer->addLayout(heading);

        m_tree=new QTreeWidget(this);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({i18n("Layer"),i18n("Eye"),
                                 i18n("Lock"),i18n("Alpha")});
        m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1,QHeaderView::Fixed);
        m_tree->header()->setSectionResizeMode(2,QHeaderView::Fixed);
        m_tree->header()->setSectionResizeMode(3,QHeaderView::Fixed);
        m_tree->setColumnWidth(1,44);
        m_tree->setColumnWidth(2,49);
        m_tree->setColumnWidth(3,46);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setIndentation(15);
        m_tree->setIconSize(QSize(40,40));
        m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tree->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(m_tree->viewport(),
                               QScroller::LeftMouseButtonGesture);
        outer->addWidget(m_tree,1);

        connect(m_tree,&QTreeWidget::itemClicked,this,
                [this](QTreeWidgetItem *item,int column) {
            tapped(item,column);
        });
        connect(m_tree,&QTreeWidget::itemExpanded,this,
                [this](QTreeWidgetItem *item) {
            auto node=nodeFor(item);
            if(node && node->childCount())node->setCollapsed(false);
        });
        connect(m_tree,&QTreeWidget::itemCollapsed,this,
                [this](QTreeWidgetItem *item) {
            auto node=nodeFor(item);
            if(node && node->childCount())node->setCollapsed(true);
        });

        auto *ops1=new QHBoxLayout;
        add(ops1,i18n("+ Paint"),[this]() {
            m_engine.addPaintLayer();scheduleReload();
        });
        add(ops1,i18n("Duplicate"),[this]() {
            m_engine.duplicateSelected();scheduleReload();
        });
        add(ops1,i18n("Delete"),[this](){confirmDelete();});
        outer->addLayout(ops1);
        auto *ops2=new QHBoxLayout;
        add(ops2,i18n("Up"),[this]() {
            m_engine.moveSelectedUp();scheduleReload();
        });
        add(ops2,i18n("Down"),[this]() {
            m_engine.moveSelectedDown();scheduleReload();
        });
        add(ops2,i18n("More"),[this]() {
            KisNodeSP node=m_engine.manager()?m_engine.manager()->activeNode():
                KisNodeSP();
            if(node)showNodeActions(node);
        });
        outer->addLayout(ops2);

        auto *opacityRow=new QHBoxLayout;
        m_opacityLabel=new QLabel(i18n("Opacity: 100%"),this);
        opacityRow->addWidget(m_opacityLabel);
        m_opacity=new QSlider(Qt::Horizontal,this);
        m_opacity->setRange(0,100);
        m_opacity->setSingleStep(1);
        m_opacity->setPageStep(5);
        m_opacity->setTracking(false);
        opacityRow->addWidget(m_opacity,1);
        outer->addLayout(opacityRow);
        connect(m_opacity,&QSlider::sliderReleased,this,[this]() {
            auto *manager=m_engine.manager();
            const KisNodeSP node=manager?manager->activeNode():KisNodeSP();
            if(node)m_engine.setNodeOpacity(node,m_opacity->value());
            scheduleReload();
        });
        m_checker=new QTimer(this);
        m_checker->setInterval(850);
        connect(m_checker,&QTimer::timeout,this,[this]() {
            if(isVisible())refresh();
        });
        m_checker->start();
    }

    std::function<void()> onRequestClose;

    void refresh() {
        if(!isVisible())return;
        const auto manager=m_engine.manager();
        const auto image=manager?manager->image().toStrongRef():KisImageSP();
        if(!image || !image->root()) {
            if(!m_tree->topLevelItemCount())return;
            m_nodes.clear();m_tree->clear();m_signature.clear();
            m_thumbCache.clear();m_lastImage=nullptr;
            m_opacity->setEnabled(false);
            return;
        }
        if(image.data()!=m_lastImage) {
            m_lastImage=image.data();
            m_thumbCache.clear();
            m_signature.clear();
        }
        QString fingerprint;
        fingerprint.reserve(1024);
        int count=0;
        std::function<void(KisNodeSP,int)> scan;
        scan=[&](KisNodeSP parent,int depth) {
            if(!parent || depth>14 || count>=320)return;
            for(KisNodeSP node=parent->lastChild();node && count<320;
                node=node->prevSibling()) {
                if(node->isFakeNode())continue;
                ++count;
                fingerprint+=node->uuid().toString()+QLatin1Char('|')+
                    node->name()+QLatin1Char('|')+
                    QString::number(node->visible())+
                    QString::number(node->userLocked())+
                    QString::number(node->collapsed())+
                    QString::number(node->thumbnailSeqNo())+
                    QString::number(node->opacity())+
                    QString::number(depth)+QLatin1Char(';');
                if(node->inherits("KisPaintLayer"))
                    fingerprint+=QString::number(
                        static_cast<KisPaintLayer*>(node.data())->alphaLocked());
                if(node->childCount())scan(node,depth+1);
            }
        };
        scan(image->root(),0);
        const KisNodeList selected=m_engine.selectedNodes();
        for(const KisNodeSP &node:selected)
            if(node)fingerprint+=node->uuid().toString();
        const KisNodeSP active=manager->activeNode();
        if(active)fingerprint+=active->uuid().toString();
        if(fingerprint==m_signature)return;
        m_signature=fingerprint;
        const QSignalBlocker block(m_tree);
        const int scroll=m_tree->verticalScrollBar()->value();
        m_tree->clear();m_nodes.clear();
        m_nodes.reserve(count);
        std::function<void(KisNodeSP,QTreeWidgetItem*,int)> append;
        append=[&](KisNodeSP parent,QTreeWidgetItem *treeParent,int depth) {
            if(!parent || depth>14 || m_nodes.size()>=320)return;
            for(KisNodeSP node=parent->lastChild();node &&
                m_nodes.size()<320;node=node->prevSibling()) {
                if(node->isFakeNode())continue;
                const int index=m_nodes.size();
                m_nodes.append(node);
                auto *item=treeParent?
                    new QTreeWidgetItem(treeParent):new QTreeWidgetItem(m_tree);
                item->setData(0,Qt::UserRole,index);
                const bool group=node->inherits("KisGroupLayer");
                QString label=node->name();
                if(group)label=QStringLiteral("▸ ")+label;
                item->setText(0,label);
                item->setToolTip(0,node->name());
                item->setIcon(0,thumbnail(node));
                item->setText(1,node->visible()?i18n("On"):i18n("Off"));
                item->setText(2,node->userLocked()?i18n("Yes"):i18n("No"));
                if(node->inherits("KisPaintLayer"))
                    item->setText(3,static_cast<KisPaintLayer*>(node.data())
                                  ->alphaLocked()?i18n("On"):i18n("Off"));
                else item->setText(3,QStringLiteral("—"));
                const bool chosen=selected.contains(node);
                const bool current=active==node;
                const QColor color=current?QColor(54,102,132):
                    chosen?QColor(48,84,103):
                    group?QColor(38,63,85):QColor(36,50,65);
                for(int column=0;column<4;++column) {
                    item->setBackground(column,color);
                    item->setTextAlignment(column,column?
                        Qt::AlignCenter:Qt::AlignLeft|Qt::AlignVCenter);
                }
                if(group) {
                    QFont font=item->font(0);font.setBold(true);
                    item->setFont(0,font);
                    item->setForeground(0,QBrush(QColor(150,211,252)));
                }
                if(node->childCount()) {
                    append(node,item,depth+1);
                    item->setExpanded(!node->collapsed());
                }
            }
        };
        append(image->root(),nullptr,0);
        m_tree->verticalScrollBar()->setValue(scroll);
        m_opacity->setEnabled(bool(active));
        if(active) {
            const int percent=active->percentOpacity();
            m_opacityLabel->setText(i18n("Opacity: %1%",percent));
            const QSignalBlocker sliderBlock(m_opacity);
            m_opacity->setValue(percent);
        }
    }

private:
    struct CachedThumb{int sequence=-1;QIcon icon;};
    QIcon thumbnail(KisNodeSP node) {
        if(!node)return {};
        const QUuid id=node->uuid();
        const int sequence=node->thumbnailSeqNo();
        auto it=m_thumbCache.constFind(id);
        if(it!=m_thumbCache.cend() && it->sequence==sequence)
            return it->icon;
        // 40 px native preview with Krita's thumbnail calculation.
        // Cached across UI refreshes to avoid repeatedly reading paint devices.
        QImage preview=node->createPreferredThumbnail(
            40,40,Qt::KeepAspectRatio);
        QIcon icon=preview.isNull()?node->icon():
            QIcon(QPixmap::fromImage(preview));
        m_thumbCache.insert(id,{sequence,icon});
        return icon;
    }
    KisNodeSP nodeFor(QTreeWidgetItem *item) const {
        if(!item)return {};
        const int index=item->data(0,Qt::UserRole).toInt();
        if(index<0 || index>=m_nodes.size())return {};
        KisNodeSP node=m_nodes.at(index);
        return m_engine.isLiveNode(node)?node:KisNodeSP();
    }
    void scheduleReload() {
        m_signature.clear();
        QTimer::singleShot(150,this,[this](){if(isVisible())refresh();});
    }
    void tapped(QTreeWidgetItem *item,int column) {
        const KisNodeSP node=nodeFor(item);
        if(!node)return;
        if(column>=1 && column<=3) {
            if(column==1)
                m_engine.setNodeVisibility(node,!node->visible());
            if(column==2)
                m_engine.setNodeLocked(node,!node->userLocked());
            if(column==3 && node->inherits("KisPaintLayer")) {
                auto *layer=static_cast<KisPaintLayer*>(node.data());
                m_engine.setNodeAlphaLocked(node,!layer->alphaLocked());
            }
            scheduleReload();return;
        }
        const KisNodeList selected=m_engine.selectedNodes();
        if(m_multi->isChecked()) {
            KisNodeList next=selected;
            if(next.contains(node)) {
                if(next.size()==1)return;
                next.removeAll(node);
            } else next.append(node);
            m_engine.setSelectedNodes(next);
            scheduleReload();
        } else if(selected.size()==1 && selected.contains(node) &&
                  m_engine.manager() && m_engine.manager()->activeNode()==node) {
            showNodeActions(node);
        } else {
            m_engine.selectLayer(node);
            scheduleReload();
        }
    }
    QPushButton *add(QHBoxLayout *row,const QString &label,
                     std::function<void()> callback) {
        auto *button=new QPushButton(label,this);
        button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        connect(button,&QPushButton::clicked,this,[callback]() {
            callback();
        });
        row->addWidget(button,1);
        return button;
    }
    void confirmDelete() {
        const KisNodeList selected=m_engine.selectedNodes();
        if(selected.isEmpty())return;
        const auto choice=QMessageBox::question(this,i18n("Delete layers"),
            i18np("Delete the selected layer?",
                  "Delete %1 selected layers?",selected.size()),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        if(choice!=QMessageBox::Yes)return;
        m_engine.deleteSelected();scheduleReload();
    }
    void rename(KisNodeSP node) {
        if(!m_engine.isLiveNode(node))return;
        bool ok=false;
        const QString text=QInputDialog::getText(this,i18n("Rename layer"),
            i18n("Name"),QLineEdit::Normal,node->name(),&ok);
        if(ok && !text.trimmed().isEmpty())
            m_engine.renameNode(node,text);
        scheduleReload();
    }
    void showNodeActions(KisNodeSP node) {
        if(!m_engine.isLiveNode(node))return;
        QDialog dialog(this);
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setWindowTitle(node->name());
        dialog.setStyleSheet(
            "QDialog{background:#263746;color:white;}"
            "QLabel{color:#ecf5fb;font-size:14px;}"
            "QPushButton{background:#42627e;color:#fff;"
            "border:1px solid #8aabc2;border-radius:8px;"
            "min-height:47px;padding:5px;}");
        auto *layout=new QVBoxLayout(&dialog);
        auto *label=new QLabel(node->name(),&dialog);
        label->setWordWrap(true);
        layout->addWidget(label);
        std::function<void()> command;
        auto option=[&](const QString &text,std::function<void()> callback) {
            auto *btn=new QPushButton(text,&dialog);
            layout->addWidget(btn);
            connect(btn,&QPushButton::clicked,&dialog,
                    [&dialog,callback]() {
                dialog.accept();
                QTimer::singleShot(0,&dialog,callback);
            });
        };
        // Queue native actions on this persistent panel, not on the soon-to-be
        // destroyed popup. The callbacks carry shared node pointers by value.
        option(i18n("Rename"),[this,node]() {rename(node);});
        option(i18n("Duplicate"),[this,node]() {
            m_engine.selectLayer(node);
            m_engine.duplicateSelected();scheduleReload();
        });
        option(i18n("Move up"),[this,node]() {
            m_engine.selectLayer(node);
            m_engine.moveSelectedUp();scheduleReload();
        });
        option(i18n("Move down"),[this,node]() {
            m_engine.selectLayer(node);
            m_engine.moveSelectedDown();scheduleReload();
        });
        option(i18n("Group selected layers"),[this]() {
            m_engine.groupSelected();scheduleReload();
        });
        option(i18n("Create clipping group"),[this]() {
            m_engine.groupSelected(true);scheduleReload();
        });
        if(node->inherits("KisGroupLayer"))
            option(i18n("Ungroup"),[this,node]() {
                m_engine.selectLayer(node);
                m_engine.ungroup();scheduleReload();
            });
        option(i18n("Delete"),[this,node]() {
            m_engine.selectLayer(node);
            confirmDelete();
        });
        auto *done=new QPushButton(i18n("Close"),&dialog);
        layout->addWidget(done);
        connect(done,&QPushButton::clicked,&dialog,&QDialog::reject);
        dialog.resize(qMax(280,qMin(380,width()-10)),520);
        dialog.exec();
    }

    MobileEngineBridge m_engine;
    QTreeWidget *m_tree=nullptr;
    QToolButton *m_multi=nullptr;
    QLabel *m_opacityLabel=nullptr;
    QSlider *m_opacity=nullptr;
    QTimer *m_checker=nullptr;
    QVector<KisNodeSP> m_nodes;
    QHash<QUuid,CachedThumb> m_thumbCache;
    KisImage *m_lastImage=nullptr;
    QString m_signature;
};
#endif // Q_OS_ANDROID
