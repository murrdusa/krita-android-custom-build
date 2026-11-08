/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Non-destructive mobile document navigator. Uses live Krita MDI document
 * views through MobileEngineBridge; no parallel document registry or
 * paint-device duplication. Closing invokes Krita's native document view.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAbstractItemView>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScroller>
#include <QSize>
#include <QTimer>
#include <QVBoxLayout>
#include <klocalizedstring.h>
#include "document_workflow.h"

class MobileDocumentSwitcher final
{
public:
    static void open(const MobileEngineBridge &engine,QWidget *parent)
    {
        if(!parent)return;
        QDialog dialog(parent);
        dialog.setWindowTitle(i18n("Canvases"));
        dialog.setWindowFlags(Qt::Popup|Qt::FramelessWindowHint);
        dialog.setStyleSheet(
            "QDialog{background:#273746;color:#f4f8fd;}"
            "QLabel{color:#e5f1fb;font-size:14px;}"
            "QListWidget{background:#2e4354;color:#f5f9fc;"
            "border:1px solid #7699b2;font-size:14px;}"
            "QListWidget::item{min-height:55px;padding:8px;}"
            "QListWidget::item:selected{background:#34718e;}"
            "QPushButton{background:#42647f;color:#f6faff;"
            "border:1px solid #83adc8;border-radius:8px;"
            "padding:5px;min-height:45px;}");
        auto *layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(10,10,10,10);
        layout->setSpacing(7);
        auto *header=new QLabel(i18n("Open canvases"),&dialog);
        header->setStyleSheet(QStringLiteral(
            "font-size:18px;font-weight:600;"));
        layout->addWidget(header);
        auto *items=new QListWidget(&dialog);
        items->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        items->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        items->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(items->viewport(),
                               QScroller::LeftMouseButtonGesture);
        layout->addWidget(items,1);
        const auto documents=engine.openDocuments();
        for(const auto &document:documents) {
            QString text=(document.active?QStringLiteral("●  "):
                                           QStringLiteral("    "))+
                         document.title;
            if(document.modified)text+=QStringLiteral("  *");
            auto *item=new QListWidgetItem(text,items);
            item->setData(Qt::UserRole,document.nativeIndex);
            item->setToolTip(document.title+
                (document.modified?i18n(" (unsaved changes)"):QString()));
            if(document.active) {
                item->setBackground(QColor(49,106,140));
                items->setCurrentItem(item);
            }
        }
        if(documents.isEmpty()) {
            auto *item=new QListWidgetItem(i18n("No open canvases"),items);
            item->setFlags(Qt::NoItemFlags);
        }
        auto *note=new QLabel(i18n(
            "Tap a canvas to switch. * indicates unsaved changes. "
            "Close Current uses Krita's Save / Discard / Cancel prompt."),&dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *creation=new QHBoxLayout;
        auto *create=new QPushButton(i18n("New"),&dialog);
        auto *openFile=new QPushButton(i18n("Open"),&dialog);
        creation->addWidget(create,1);
        creation->addWidget(openFile,1);
        layout->addLayout(creation);
        auto *actions=new QHBoxLayout;
        auto *closeCurrent=new QPushButton(i18n("Close current canvas"),&dialog);
        auto *dismiss=new QPushButton(i18n("Done"),&dialog);
        closeCurrent->setEnabled(engine.openDocuments().size()>0);
        actions->addWidget(closeCurrent,2);
        actions->addWidget(dismiss,1);
        layout->addLayout(actions);
        int desiredIndex=-1;
        enum Choice {None,Create,Open,CloseCurrent};
        Choice choice=None;
        QObject::connect(items,&QListWidget::itemClicked,&dialog,
                         [&](QListWidgetItem *item) {
            if(!item || !item->flags().testFlag(Qt::ItemIsEnabled))return;
            desiredIndex=item->data(Qt::UserRole).toInt();
            dialog.accept();
        });
        QObject::connect(create,&QPushButton::clicked,&dialog,[&]() {
            choice=Create;dialog.accept();
        });
        QObject::connect(openFile,&QPushButton::clicked,&dialog,[&]() {
            choice=Open;dialog.accept();
        });
        QObject::connect(closeCurrent,&QPushButton::clicked,&dialog,[&]() {
            choice=CloseCurrent;
            dialog.accept();
        });
        QObject::connect(dismiss,&QPushButton::clicked,
                         &dialog,&QDialog::reject);
        const QSize bounds=parent->window()->size();
        dialog.resize(qMax(310,qMin(500,bounds.width()-24)),
                      qMax(330,qMin(620,bounds.height()-50)));
        if(dialog.exec()!=QDialog::Accepted)return;
        // End the chooser before opening a native Krita document dialog.
        if(choice==Create)MobileDocumentWorkflow::create(engine,parent);
        else if(choice==Open)MobileDocumentWorkflow::open(engine,parent);
        else if(choice==CloseCurrent) {
            // Android Qt may still be tearing down this frameless popup.
            // Close on the next event-loop pass so Krita's native
            // Save/Discard/Cancel dialog is shown above the live canvas,
            // rather than behind the document switcher.
            const MobileEngineBridge native=engine;
            QTimer::singleShot(0,parent,[native]() {
                native.closeActiveDocument();
            });
        }
        else if(desiredIndex>=0)engine.activateDocument(desiredIndex);
    }
};
#endif // Q_OS_ANDROID
