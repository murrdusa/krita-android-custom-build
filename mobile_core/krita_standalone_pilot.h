/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Explicit opt-in, minimal real-engine Android smoke window.
 * This does not alter the existing Mobile Studio launcher or migrate
 * any live Krita document. The temporary toolbar is diagnostic only.
 *
 * Launch from Settings > Standalone Engine Test (experimental).
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include "document_service.h"
#include "krita_standalone_documents.h"
#include "krita_standalone_runtime.h"

class MobileKritaStandalonePilot final
{
public:
    static bool launch()
    {
        // A second standalone input manager can interfere with the
        // first. Focus the existing diagnostic window instead.
        for(QWidget *widget:QApplication::topLevelWidgets()) {
            if(widget->objectName()==
                    QStringLiteral("_mobileStandaloneEnginePilot")) {
                widget->show();
                widget->raise();
                widget->activateWindow();
                return true;
            }
        }

        // This is a new window and new view stack, not an MDI canvas
        // transfer. An existing Krita drawing is never touched.
        auto *window=new MobileLightweightCanvasHost;
        window->setObjectName(
            QStringLiteral("_mobileStandaloneEnginePilot"));
        window->setWindowTitle(
            QObject::tr("Krita native engine test"));
        window->setAttribute(Qt::WA_DeleteOnClose,true);
        window->resize(860,650);
        if(!window->createNativeDocumentRoot()) {
            delete window;
            return false;
        }
        MobileKritaStandaloneRuntime *runtime=
            MobileKritaStandaloneRuntime::create(window);
        if(!runtime) {
            delete window;
            return false;
        }
        const MobileHostServices services=
            MobileKritaStandaloneDocumentsBootstrap::create(runtime);
        if(!services.ready()) {
            delete window;
            return false;
        }
        auto documents=QSharedPointer<MobileDocumentService>::create(
            services.documents,services.views);
        QPointer<MobileLightweightCanvasHost> alive=window;

        // Diagnostics, not a new permanent mobile toolbar.
        auto *bar=window->addToolBar(
            QObject::tr("Engine test controls"));
        bar->setMovable(false);
        auto *newAction=bar->addAction(QObject::tr("New"));
        auto *openAction=bar->addAction(QObject::tr("Open"));
        auto *undoAction=bar->addAction(QObject::tr("Undo"));
        auto *redoAction=bar->addAction(QObject::tr("Redo"));
        auto *saveAction=bar->addAction(QObject::tr("Save"));
        auto *saveAsAction=bar->addAction(QObject::tr("Save As"));
        auto *closeAction=bar->addAction(QObject::tr("Close view"));

        QObject::connect(newAction,&QAction::triggered,window,
                         [alive,documents]() {
            if(alive && !documents->createCanvas(
                       QStringLiteral("Engine test"),512,512,72,true))
                QMessageBox::warning(
                    alive,QObject::tr("Engine test"),
                    QObject::tr("Could not create an independent Krita canvas."));
        });
        QObject::connect(openAction,&QAction::triggered,window,
                         [alive,documents]() {
            if(alive)documents->openFiles(alive);
        });
        // Exercise Krita's genuine image undo stack; this is not a
        // reconstructed history of mobile gestures. Read the currently
        // selected native document on every click to avoid stale views.
        auto history=[views=services.views](bool redo) {
            if(!views)return;
            const auto current=views->currentDocument();
            auto *doc=qobject_cast<KisDocument*>(current.data());
            if(!doc || !doc->undoStack())return;
            if(redo)doc->undoStack()->redo();
            else doc->undoStack()->undo();
        };
        QObject::connect(undoAction,&QAction::triggered,window,
                         [history](){history(false);});
        QObject::connect(redoAction,&QAction::triggered,window,
                         [history](){history(true);});
        QObject::connect(saveAction,&QAction::triggered,window,
                         [alive,documents]() {
            if(alive && !documents->saveCurrentDocument())
                qWarning() << "Independent Krita Save canceled or rejected";
        });
        QObject::connect(saveAsAction,&QAction::triggered,window,
                         [alive,documents]() {
            if(alive && !documents->saveCurrentDocument(true))
                qWarning() << "Independent Krita Save As canceled or rejected";
        });
        QObject::connect(closeAction,&QAction::triggered,window,
                         [alive,documents]() {
            if(alive && !documents->closeActiveDocument())
                qWarning() << "Independent Krita close canceled or rejected";
        });

        // The real KisCanvas2 must become visible before Qt initializes
        // brush resources and its OpenGL context. Both the initial canvas
        // and later documents use the same native KisPart engine path.
        window->show();
        QTimer::singleShot(0,window,[alive,documents]() {
            if(alive && !documents->createCanvas(
                       QStringLiteral("Engine test"),512,512,72,true)) {
                QMessageBox::warning(
                    alive,QObject::tr("Engine test"),
                    QObject::tr("Native view initialization failed. "
                                "The existing Krita interface is unchanged."));
            }
        });
        return true;
    }
};
#endif
