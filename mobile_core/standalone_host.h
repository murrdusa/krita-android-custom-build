/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Experimental independent *window* (not an independent engine executable):
 * the UI owns its application chrome and hosts Krita's real widgetStack.
 * Krita remains the backend service for lifecycle, tools, canvas and saving.
 *
 * The move between native Krita and this host is reversible, with the actual
 * QWidget canvas stack transferred rather than mirrored or imitated.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QFrame>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QSettings>
#include "native_startup_policy.h"
#include <QStringList>
#include <QCursor>
#include <QVector>
#include <functional>

#include "kis_canvas_resource_provider.h"
#include "kis_node.h"
#include "kis_node_manager.h"
#include "kis_image.h"
#include <KoColor.h>
#include <KoToolManager.h>
#include <klocalizedstring.h>

#include "engine_bridge.h"
#include "canvas_surface.h"
#include "document_workflow.h"
#include "document_switcher.h"
#include "command_browser.h"
#include "tool_browser.h"
#include "quick_actions.h"
#include "finger_pressure_calibration.h"
#include "brush_workspace.h"
#include "layer_workspace.h"
#include "color_workspace.h"

class MobileStandaloneWindow final : public QMainWindow, public MobileCanvasSurface
{
public:
    explicit MobileStandaloneWindow(const MobileEngineBridge &bridge)
        : QMainWindow(nullptr),m_engine(bridge)
    {
        setObjectName(QStringLiteral("KritaMobileIndependentWindow"));
        setWindowTitle(i18n("Krita Mobile Studio"));
        setAttribute(Qt::WA_DeleteOnClose,false);
        setStyleSheet(
            "QMainWindow#KritaMobileIndependentWindow{background:#202934;}"
            "QFrame#mobileHeader,QFrame#mobileFooter{background:#25313e;"
            "border:0;}"
            "QLabel{color:#edf3f9;font-size:13px;}"
            "QToolButton,QPushButton{background:#405a72;color:#f7faff;"
            "border:1px solid #6c8daa;border-radius:7px;"
            "padding:6px;font-size:13px;}"
            "QToolButton:pressed,QPushButton:pressed{background:#547f9c;}");
        auto *root=new QWidget(this);
        root->setObjectName(QStringLiteral("MobileApplicationRoot"));
        auto *layout=new QVBoxLayout(root);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        setCentralWidget(root);

        auto *header=new QFrame(root);
        m_mobileHeader=header;
        header->setObjectName(QStringLiteral("mobileHeader"));
        auto *top=new QHBoxLayout(header);
        top->setContentsMargins(7,4,7,4);
        top->setSpacing(4);

        auto *home=new QToolButton(header);
        home->setText(i18n("Back"));
        home->setToolTip(i18n("Return to the existing Krita interface"));
        home->setMinimumSize(58,43);
        connect(home,&QToolButton::clicked,this,[this](){requestExit();});
        top->addWidget(home);

        // The window title already identifies Studio. An extra title
        // consumes scarce width and makes the header overflow on phones.
        m_document=new QToolButton(header);
        m_document->setText(i18n("Canvases"));
        m_document->setToolTip(i18n("Tap to switch open canvases"));
        m_document->setMinimumHeight(42);
        m_document->setSizePolicy(QSizePolicy::Ignored,
                                  QSizePolicy::Preferred);
        m_document->setMinimumWidth(0);
        connect(m_document,&QToolButton::clicked,this,[this]() {
            MobileDocumentSwitcher::open(m_engine,this);
            refresh();
        });
        top->addWidget(m_document,1);
        layout->addWidget(header,0);

        // The Krita canvas (including its native MDI document view) is
        // transferred into this slot. It is never recreated or resampled.
        // The canvas and layers are sibling views in the mobile window.
        // Unlike a modal dialog, layer operations do not block painting,
        // save, undo or other canvas navigation.
        auto *paintingArea=new QWidget(root);
        m_paintingArea=paintingArea;
        auto *paintingLayout=new QHBoxLayout(paintingArea);
        m_paintingLayout=paintingLayout;
        paintingArea->installEventFilter(this);
        paintingLayout->setContentsMargins(0,0,0,0);
        paintingLayout->setSpacing(0);
        m_canvasSlot=new QWidget(paintingArea);
        m_canvasSlot->setObjectName(QStringLiteral("MobileNativeCanvasSlot"));
        m_canvasSlot->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_canvasSlot->setMinimumSize(0,0);
        // Show our own mobile empty state, not Krita's desktop welcome
        // screen. Keep Krita's widget stack alive (and attached) behind it,
        // so document loading, tools and view notifications remain native.
        m_canvasStack=new QStackedLayout(m_canvasSlot);
        m_canvasStack->setContentsMargins(0,0,0,0);
        m_canvasHolder=new QWidget(m_canvasSlot);
        m_canvasLayout=new QVBoxLayout(m_canvasHolder);
        m_canvasLayout->setContentsMargins(0,0,0,0);
        m_canvasLayout->setSpacing(0);
        m_canvasStack->addWidget(m_canvasHolder);

        m_emptyCanvas=new QWidget(m_canvasSlot);
        auto *emptyLayout=new QVBoxLayout(m_emptyCanvas);
        emptyLayout->setContentsMargins(16,10,16,10);
        emptyLayout->setSpacing(14);
        emptyLayout->addStretch(2);
        auto *emptyTitle=new QLabel(i18n("Your canvas workspace"),m_emptyCanvas);
        emptyTitle->setAlignment(Qt::AlignCenter);
        emptyTitle->setStyleSheet(QStringLiteral(
            "font-weight:650;font-size:21px;color:#edf4fc;"));
        emptyLayout->addWidget(emptyTitle);
        auto *emptyText=new QLabel(i18n(
            "Create a new canvas or open an existing drawing."),m_emptyCanvas);
        emptyText->setAlignment(Qt::AlignCenter);
        emptyText->setWordWrap(true);
        emptyLayout->addWidget(emptyText);
        auto *emptyButtons=new QHBoxLayout;
        emptyButtons->setSpacing(8);
        auto *createCanvas=new QPushButton(i18n("New canvas"),m_emptyCanvas);
        auto *openCanvas=new QPushButton(i18n("Open drawing"),m_emptyCanvas);
        createCanvas->setMinimumHeight(51);
        openCanvas->setMinimumHeight(51);
        emptyButtons->addWidget(createCanvas,1);
        emptyButtons->addWidget(openCanvas,1);
        emptyLayout->addLayout(emptyButtons);
        emptyLayout->addStretch(3);
        connect(createCanvas,&QPushButton::clicked,this,[this]() {
            MobileDocumentWorkflow::create(m_engine,this);
            refresh();
        });
        connect(openCanvas,&QPushButton::clicked,this,[this]() {
            MobileDocumentWorkflow::open(m_engine,this);
            refresh();
        });
        m_canvasStack->addWidget(m_emptyCanvas);
        m_canvasStack->setCurrentWidget(m_emptyCanvas);
        paintingLayout->addWidget(m_canvasSlot,1);
        m_layers=new KritaMobileLayersPanel(m_engine,paintingArea);
        m_layers->onRequestClose=[this]() {
            if(m_layers)m_layers->hide();
            updateLayerPresentation();
        };
        paintingLayout->addWidget(m_layers,0);
        m_layers->hide();
        // Floating foreground color controls are overlaid on the live
        // canvas and occupy no permanent layout width. They capture
        // pointer events only over the wheel and recent color swatches.
        m_colorWorkspace=new MobileColorWorkspace(m_engine,m_canvasSlot);
        m_colorWorkspace->raise();
        layout->addWidget(paintingArea,1);
        updateLayerPresentation();

        m_brushWorkspace=new KritaMobileBrushWorkspace(m_engine,root);
        m_brushWorkspace->setSizePolicy(
            QSizePolicy::Expanding,QSizePolicy::Fixed);
        layout->addWidget(m_brushWorkspace,0);

        // Configurable, horizontally scrollable Quick Actions replace the
        // old eight-button footer without adding more chrome. Every item
        // calls either a native Krita action/tool or a mobile workflow.
        m_quickActions=new MobileQuickActions(
            m_engine,
            [this](const QString &id){runQuickAction(id);},
            root);
        layout->addWidget(m_quickActions,0);

        // The mobile shell no longer owns or observes a desktop MDI
        // widget. The backend adapter owns native document navigation.
        m_updates=new QTimer(this);
        m_updates->setInterval(450);
        connect(m_updates,&QTimer::timeout,this,[this](){
            if(isVisible())refresh();
        });
        m_updates->start();
        // Apply the empty-workspace presentation immediately, before the
        // first 450ms refresh tick renders a useless shortcut footer.
        refresh();
    }

    void setExitCallback(std::function<void()> handler) {
        m_exit=std::move(handler);
    }

    // Mobile phone system Back dismisses panels first, without trying
    // to close or detach the painting widget. The top Back button still
    // explicitly switches directly to Krita's original interface.
    bool dismissTransientControls()
    {
        if(m_layers && m_layers->isVisible()) {
            m_layers->hide();
            updateLayerPresentation();
            return true;
        }
        if(m_colorWorkspace && m_colorWorkspace->expanded()) {
            m_colorWorkspace->setExpanded(false);
            return true;
        }
        return false;
    }

    void attachEngineCanvas(QWidget *nativeCanvas)
    {
        if(!nativeCanvas || m_nativeCanvas) return;
        m_nativeCanvas=nativeCanvas;
        m_canvasLayout->addWidget(nativeCanvas,1);
        nativeCanvas->show();
        if(m_colorWorkspace)m_colorWorkspace->raise();
        refresh();
    }
    QWidget *releaseEngineCanvas(QWidget *destinationParent=nullptr)
    {
        QWidget *canvas=m_nativeCanvas;
        if(!canvas)return nullptr;
        m_nativeCanvas=nullptr;
        m_canvasLayout->removeWidget(canvas);
        canvas->hide();
        // The destination can be the same Android top-level Krita window.
        // Reparent directly rather than briefly making Krita's live GL
        // canvas a standalone Android window.
        if(canvas->parentWidget()!=destinationParent)
            canvas->setParent(destinationParent);
        return canvas;
    }
    bool hasEngineCanvas() const {return m_nativeCanvas!=nullptr;}

    // Qt-only contract: this UI implementation owns the presentation
    // slot, while MobileCanvasTransfer retains the native widget itself.
    QWidget *canvasSurfaceWindow() override {return this;}
    bool attachNativeCanvas(QWidget *nativeCanvas) override
    {
        if(!nativeCanvas || m_nativeCanvas || !m_canvasLayout)return false;
        attachEngineCanvas(nativeCanvas);
        return m_nativeCanvas==nativeCanvas;
    }
    QWidget *detachNativeCanvas(QWidget *destinationParent=nullptr) override
    {
        return releaseEngineCanvas(destinationParent);
    }
    bool ownsNativeCanvas(QWidget *nativeCanvas) const override
    {
        return nativeCanvas && m_nativeCanvas==nativeCanvas;
    }
    bool hasNativeCanvas() const override {return hasEngineCanvas();}
    void refreshCanvasSurface() override {refresh();}

    // Optional stable-canvas host. Only the controls change between the
    // two UI modes; the native Krita widgetStack retains its parent.
    void setMobilePresentationEnabled(bool enabled) override
    {
        if(m_mobilePresentationEnabled==enabled)return;
        m_mobilePresentationEnabled=enabled;
        if(m_mobileHeader)m_mobileHeader->setVisible(enabled);
        if(!enabled) {
            if(m_updates)m_updates->stop();
            if(m_brushWorkspace)m_brushWorkspace->hide();
            if(m_quickActions)m_quickActions->hide();
            if(m_layers)m_layers->hide();
            if(m_colorWorkspace)m_colorWorkspace->hide();
            // The native welcome screen and MDI views remain alive and
            // editable. Never obscure them with the mobile empty state.
            if(m_canvasStack && m_canvasHolder)
                m_canvasStack->setCurrentWidget(m_canvasHolder);
            updateLayerPresentation();
        } else {
            refresh();
            if(m_updates)m_updates->start();
        }
    }

    void refresh()
    {
        if(!m_mobilePresentationEnabled || !m_document)return;
        const bool has=m_engine.hasDocument();
        if(m_quickActions) {
            m_quickActions->refresh();
            // New/Open are already available in the empty canvas panel.
            // Hide paint-related shortcuts and their Add/Edit chrome until
            // an actual document exists; preferences remain untouched.
            m_quickActions->setVisible(has);
        }
        if(m_canvasStack && m_canvasHolder && m_emptyCanvas) {
            QWidget *target=has?m_canvasHolder:m_emptyCanvas;
            if(m_canvasStack->currentWidget()!=target)
                m_canvasStack->setCurrentWidget(target);
        }
        // Hide controls that require a document. This also prevents a
        // stale layer panel from occupying space after the last close.
        if(m_brushWorkspace)m_brushWorkspace->setVisible(has);
        if(m_colorWorkspace) {
            m_colorWorkspace->setVisible(has);
            if(has) {
                m_colorWorkspace->raise();
                m_colorWorkspace->refresh();
            }
        }
        // Quick Actions checks document availability from the bridge.
        if(!has && m_layers && m_layers->isVisible()) {
            m_layers->hide();
        }
        updateLayerPresentation();
        const QString node=m_engine.activeLayerName();
        const QString fullTitle=has?
            m_engine.activeDocumentTitle()+
                (m_engine.documentModified()?QStringLiteral(" *"):QString())+
                (node.isEmpty()?QString():QStringLiteral(" · ")+node.left(18)):
            i18n("No document");
        // Elide long file/layer names; never let their sizeHint force
        // header buttons into a second row or outside the phone viewport.
        const int textWidth=qMax(0,m_document->width()-24);
        m_document->setText(m_document->fontMetrics().elidedText(
            fullTitle,Qt::ElideMiddle,textWidth));
        m_document->setToolTip(fullTitle);
        if(m_brushWorkspace && has)m_brushWorkspace->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshFromEngine();
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_paintingArea &&
           (event->type()==QEvent::Resize ||
            event->type()==QEvent::Show))
            updateLayerPresentation();
        return QMainWindow::eventFilter(watched,event);
    }
    void closeEvent(QCloseEvent *event) override
    {
        // Never let a system Back gesture silently close an unsaved document.
        event->ignore();
        requestExit();
    }

private:
    void updateLayerPresentation()
    {
        if(!m_layers || !m_paintingArea || !m_paintingLayout)return;
        const bool compact=m_paintingArea->width()<640;
        if(compact!=m_layersFloating) {
            if(compact) {
                // On folded/portrait screens a side dock would leave too
                // little width for actual Krita painting. Present the same
                // native node panel as a bottom drawer over the canvas.
                m_paintingLayout->removeWidget(m_layers);
                m_layers->setMinimumWidth(0);
                m_layers->setMaximumWidth(QWIDGETSIZE_MAX);
            } else {
                m_layers->setMinimumWidth(268);
                m_layers->setMaximumWidth(386);
                m_paintingLayout->addWidget(m_layers,0);
            }
            m_layersFloating=compact;
        }
        int drawerHeight=0;
        if(compact && m_layers->isVisible()) {
            drawerHeight=qMax(165,qMin(455,
                            m_paintingArea->height()*55/100));
            drawerHeight=qMin(drawerHeight,m_paintingArea->height());
            const QRect target(0,m_paintingArea->height()-drawerHeight,
                               m_paintingArea->width(),drawerHeight);
            if(m_layers->geometry()!=target)
                m_layers->setGeometry(target);
            m_layers->raise();
        }
        if(m_colorWorkspace)
            m_colorWorkspace->avoidBottom(drawerHeight);
    }
    void requestExit()
    {
        if(m_exit)m_exit();
    }
    void runQuickAction(const QString &id)
    {
        if(id.startsWith(QStringLiteral("native:"))) {
            QAction *action=m_engine.action(id.mid(7));
            if(action && action->isEnabled())action->trigger();
        } else if(id.startsWith(QStringLiteral("tool:"))) {
            m_engine.selectTool(id.mid(5));
        } else if(id==QStringLiteral("mobile:new")) {
            MobileDocumentWorkflow::create(m_engine,this);
        } else if(id==QStringLiteral("mobile:open")) {
            MobileDocumentWorkflow::open(m_engine,this);
        } else if(id==QStringLiteral("mobile:save")) {
            MobileDocumentWorkflow::save(m_engine,this);
        } else if(id==QStringLiteral("mobile:save_as")) {
            MobileDocumentWorkflow::save(m_engine,this,true);
        } else if(id==QStringLiteral("mobile:undo")) {
            m_engine.run("edit_undo");
        } else if(id==QStringLiteral("mobile:redo")) {
            m_engine.run("edit_redo");
        } else if(id==QStringLiteral("mobile:layers")) {
            openLayers();
        } else if(id==QStringLiteral("mobile:color")) {
            openColor();
        } else if(id==QStringLiteral("mobile:tools")) {
            MobileToolBrowser::open(m_engine,this);
        } else if(id==QStringLiteral("mobile:canvases")) {
            MobileDocumentSwitcher::open(m_engine,this);
        } else if(id==QStringLiteral("mobile:close")) {
            // Same guarded identity as Canvases and More. Never let an
            // ambiguous or stale MDI selection close an unrelated view.
            for(const auto &document:m_engine.openDocuments()) {
                if(document.active && document.nativeView) {
                    m_engine.closeDocumentView(document.nativeView);
                    break;
                }
            }
        } else if(id==QStringLiteral("mobile:commands")) {
            MobileCommandBrowser::open(m_engine,this);
        } else if(id==QStringLiteral("mobile:more")) {
            openMore();
        }
        refresh();
    }
    void openMore()
    {
        QMenu menu(this);
        auto *tools=menu.addAction(i18n("Tools — search Krita's tool registry"));
        tools->setEnabled(m_engine.hasDocument());
        connect(tools,&QAction::triggered,this,[this]() {
            // Open after the menu closes; tool activation stays on Krita's
            // input manager and never routes pen strokes through this UI.
            QTimer::singleShot(0,this,[this]() {
                MobileToolBrowser::open(m_engine,this);
                refresh();
            });
        });
        menu.addSeparator();
        auto *mobileStartup=menu.addAction(
            i18n("Start in Mobile Studio on next launch"));
        mobileStartup->setCheckable(true);
        mobileStartup->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/startDirectly"),true).toBool());
        mobileStartup->setToolTip(i18n(
            "Skip desktop controls at launch. An unsuccessful automatic "
            "start reverts to the Krita fallback."));
        connect(mobileStartup,&QAction::toggled,this,[](bool enabled) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/startDirectly"),enabled);
            settings.setValue(
                QStringLiteral("MobileStudio/bootPending"),false);
            settings.sync();
        });
        auto *openNative=menu.addAction(
            i18n("Open independent Krita engine now"));
        QPointer<QAction> nativeEntry=
            m_engine.action(QStringLiteral("tablet_standalone_engine_test"));
        openNative->setEnabled(nativeEntry && nativeEntry->isEnabled());
        connect(openNative,&QAction::triggered,this,[this,nativeEntry]() {
            QTimer::singleShot(0,this,[nativeEntry]() {
                if(nativeEntry && nativeEntry->isEnabled())
                    nativeEntry->trigger();
            });
        });
        auto *nativeStartup=menu.addAction(
            i18n("Start independent Krita engine on next launch"));
        nativeStartup->setCheckable(true);
        {
            QSettings settings;
            nativeStartup->setChecked(MobileNativeStartupPolicy::enabled(settings));
        }
        nativeStartup->setToolTip(i18n(
            "Start in the separate native canvas on next app launch. "
            "A failed startup reverts to original Krita automatically."));
        connect(nativeStartup,&QAction::toggled,this,[](bool on) {
            QSettings settings;
            MobileNativeStartupPolicy::setEnabled(settings,on);
        });
        auto *fingerPressure=menu.addAction(
            i18n("Experimental finger pressure (contact area)"));
        fingerPressure->setCheckable(true);
        fingerPressure->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/fingerContactPressureV1"),false)
            .toBool());
        fingerPressure->setToolTip(i18n(
            "Off by default. When painting with one finger, use the "
            "contact area reported by Android as a smoothed pressure "
            "estimate. Stylus and multi-finger gestures stay unchanged. "
            "Changes apply on the next finger stroke."));
        connect(fingerPressure,&QAction::toggled,this,[](bool enabled) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/fingerContactPressureV1"),
                enabled);
            settings.sync();
        });
        auto *fingerCalibration=menu.addAction(
            i18n("Finger pressure calibration + live diagnostics"));
        fingerCalibration->setToolTip(i18n(
            "Calibrate light, medium and broad contacts, adjust the "
            "response curve and stability, and see raw contact levels. "
            "Saved changes apply on the next finger stroke."));
        connect(fingerCalibration,&QAction::triggered,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                MobileFingerPressureCalibrationDialog calibration(this);
                calibration.resize(qMax(320,qMin(530,width()-20)),
                                   qMax(490,qMin(730,height()-40)));
                calibration.exec();
            });
        });
        auto *stationaryHost=menu.addAction(
            i18n("Experimental stationary canvas UI (next restart)"));
        stationaryHost->setCheckable(true);
        stationaryHost->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/nativeDockCanvasV1"),false)
            .toBool());
        stationaryHost->setToolTip(i18n(
            "Off by default. Try Mobile Studio controls as Qt dockers "
            "around Krita's original canvas without moving the canvas. "
            "Separate from the older persistent-host experiment. "
            "Requires an app restart; save test drawings first."));
        connect(stationaryHost,&QAction::toggled,this,[](bool enabled) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/nativeDockCanvasV1"),enabled);
            settings.sync();
        });
        auto *stableHost=menu.addAction(
            i18n("Experimental persistent canvas host (next restart)"));
        stableHost->setCheckable(true);
        stableHost->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/persistentCanvasHostV1"),false)
            .toBool());
        stableHost->setToolTip(i18n(
            "Off by default. Keeps Krita's canvas attached when switching "
            "between Mobile Studio and desktop controls after the first "
            "handoff. Requires an app restart to change modes. "
            "Experimental: save drawings before testing."));
        connect(stableHost,&QAction::toggled,this,[](bool enabled) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/persistentCanvasHostV1"),
                enabled);
            settings.sync();
        });
        menu.addSeparator();
        auto *closeCanvas=menu.addAction(i18n("Close current canvas"));
        // Capture the exact native view while this menu is open. The
        // following event-loop turn may close/reorder MDI subwindows;
        // indices or a fresh active-window lookup could hit another file.
        QPointer<QObject> closeTarget;
        const auto openViews=m_engine.openDocuments();
        for(const auto &document:openViews)
            if(document.active && document.nativeView) {
                closeTarget=document.nativeView;
                break;
            }
        if(!closeTarget && openViews.size()==1)
            closeTarget=openViews.first().nativeView;
        closeCanvas->setEnabled(!closeTarget.isNull());
        connect(closeCanvas,&QAction::triggered,this,
                [this,closeTarget]() {
            QTimer::singleShot(0,this,[this,closeTarget]() {
                if(closeTarget)m_engine.closeDocumentView(closeTarget);
                refresh();
            });
        });
        auto *saveAs=menu.addAction(i18n("Save As..."));
        connect(saveAs,&QAction::triggered,this,[this]() {
            QTimer::singleShot(0,this,[this]() {
                MobileDocumentWorkflow::save(m_engine,this,true);
                refresh();
            });
        });
        menu.addSeparator();
        auto *browse=menu.addAction(i18n("Menus — File, Edit, View and more"));
        connect(browse,&QAction::triggered,this,[this]() {
            // Let the menu close before opening the independent browser.
            QTimer::singleShot(0,this,[this]() {
                MobileCommandBrowser::open(m_engine,this);
            });
        });
        menu.addSeparator();
        auto *transform=menu.addAction(i18n("Transform selected pixels/layers"));
        connect(transform,&QAction::triggered,this,[this](){
            KoToolManager::instance()->switchToolRequested(
                QStringLiteral("KisToolTransform"));
        });
        auto *move=menu.addAction(i18n("Move tool"));
        connect(move,&QAction::triggered,this,[](){
            KoToolManager::instance()->switchToolRequested(
                QStringLiteral("KritaTransform/KisToolMove"));
        });
        menu.addSeparator();
        auto *diagnostics=menu.addAction(i18n("Copy Mobile Studio diagnostics"));
        diagnostics->setToolTip(i18n(
            "Copy canvas and toolbar state without filenames or drawing data"));
        connect(diagnostics,&QAction::triggered,this,[this]() {
            // Let the popup finish first; clipboard and notification use
            // Qt's existing native Android integration, not a new window.
            QTimer::singleShot(0,this,[this]() {
                const bool canvasPresent=hasNativeCanvas();
                const bool sameTopLevel=canvasPresent &&
                    m_nativeCanvas->window()==window();
                const auto documents=m_engine.openDocuments();
                const QVariant samples=qApp->property(
                    "_mobileFingerAreaLastSampleCount");
                const QString fingerSignal=!samples.isValid()?
                    i18n("not sampled yet"):
                    samples.toInt()<=0?
                        i18n("unavailable (Qt reported no contact ellipse)"):
                        i18n("%1 usable samples").arg(samples.toInt());
                const QVariant minDiameter=qApp->property(
                    "_mobileFingerAreaLastMinDiameter");
                const QVariant maxDiameter=qApp->property(
                    "_mobileFingerAreaLastMaxDiameter");
                const QString diameterRange=samples.toInt()>0?
                    QStringLiteral("%1 to %2 logical px")
                        .arg(minDiameter.toDouble(),0,'f',1)
                        .arg(maxDiameter.toDouble(),0,'f',1):
                    QStringLiteral("unknown");
                const auto calibration=MobileFingerPressureSettings::read(
                    QSettings());
                const auto response=MobileFingerPressureSettings::readResponse(
                    QSettings());
                const QString report=QStringList({
                    QStringLiteral("Krita Mobile Studio diagnostics v1"),
                    QStringLiteral("Host: embedded Qt widget"),
                    QStringLiteral("Canvas attached: %1").arg(canvasPresent),
                    QStringLiteral("Canvas shares native window: %1")
                        .arg(sameTopLevel),
                    QStringLiteral("Persistent canvas host installed: %1")
                        .arg(window()->property(
                            "_mobileStudioPersistentCanvasHost").toBool()),
                    QStringLiteral("Mobile presentation active: %1")
                        .arg(m_mobilePresentationEnabled),
                    QStringLiteral("Canvas count: %1").arg(documents.size()),
                    QStringLiteral("Document available: %1")
                        .arg(m_engine.hasDocument()),
                    QStringLiteral("Host geometry: %1 x %2")
                        .arg(width()).arg(height()),
                    QStringLiteral("Quick Actions visible: %1")
                        .arg(m_quickActions && m_quickActions->isVisible()),
                    QStringLiteral("Brush controls visible: %1")
                        .arg(m_brushWorkspace && m_brushWorkspace->isVisible()),
                    QStringLiteral("Layers visible: %1")
                        .arg(m_layers && m_layers->isVisible()),
                    QStringLiteral("Color controls visible: %1")
                        .arg(m_colorWorkspace && m_colorWorkspace->isVisible()),
                    QStringLiteral("Finger pressure simulation enabled: %1")
                        .arg(QSettings().value(
                            QStringLiteral("MobileStudio/fingerContactPressureV1"),
                            false).toBool()),
                    QStringLiteral("Calibrated finger diameter: %1 to %2 logical px")
                        .arg(calibration.minimum,0,'f',1)
                        .arg(calibration.maximum,0,'f',1),
                    QStringLiteral("Finger response gamma: %1")
                        .arg(response.gamma,0,'f',2),
                    QStringLiteral("Finger stability: %1%")
                        .arg(response.stability*100.0,0,'f',0),
                    QStringLiteral("Prefer raw Android contact: %1")
                        .arg(response.useRawAndroid),
                    QStringLiteral("Last finger area source: %1")
                        .arg(qApp->property(
                            "_mobileFingerAreaLastSource").toString()),
                    QStringLiteral("Distinct finger contact levels: %1")
                        .arg(qApp->property(
                            "_mobileFingerAreaLastUniqueSteps").toInt()),
                    QStringLiteral("Last finger samples raw / Qt / aligned: %1 / %2 / %3")
                        .arg(qApp->property(
                            "_mobileFingerAreaLastRawSamples").toInt())
                        .arg(qApp->property(
                            "_mobileFingerAreaLastQtSamples").toInt())
                        .arg(qApp->property(
                            "_mobileFingerAreaLastAlignedFallbacks").toInt()),
                    QStringLiteral("Latest Android raw diameter: %1 logical px")
                        .arg(qApp->property(
                            "_mobileFingerRawDiameter").toDouble(),0,'f',1),
                    QStringLiteral("Last finger area data: %1")
                        .arg(fingerSignal),
                    QStringLiteral("Last finger contact diameter range: %1")
                        .arg(diameterRange)
                }).join(QLatin1Char('\n'));
                QClipboard *clipboard=QGuiApplication::clipboard();
                if(!clipboard) {
                    QMessageBox::warning(this,i18n("Diagnostics unavailable"),
                        i18n("The system clipboard is unavailable."));
                    return;
                }
                clipboard->setText(report);
                QMessageBox::information(this,i18n("Diagnostics copied"),
                    i18n("Mobile Studio state copied. You can paste it into "
                         "a bug report. No document contents or paths "
                         "were included."));
            });
        });
        auto *nativeMenu=m_engine.nativeMenus(&menu);
        nativeMenu->setTitle(i18n("Legacy menus (compatibility)"));
        menu.addMenu(nativeMenu);
        menu.exec(mapToGlobal(QPoint(qMax(4,width()-270),
                                      qMax(4,height()-65))));
    }
    void openLayers()
    {
        if(!m_layers)return;
        if(!m_engine.manager() || !m_engine.hasDocument()) {
            // Quick Actions do not mirror layer visibility.
            return;
        }
        const bool opening=!m_layers->isVisible();
        m_layers->setVisible(opening);
        updateLayerPresentation();
        if(opening)m_layers->refreshNow();
    }
    void openColor()
    {
        if(!m_engine.hasDocument() || !m_colorWorkspace)return;
        m_colorWorkspace->setExpanded(!m_colorWorkspace->expanded());
        m_colorWorkspace->show();
        m_colorWorkspace->raise();
    }

    MobileEngineBridge m_engine;
    QPointer<QFrame> m_mobileHeader;
    bool m_mobilePresentationEnabled=true;
    QPointer<QWidget> m_paintingArea;
    QHBoxLayout *m_paintingLayout=nullptr;
    bool m_layersFloating=false;
    QPointer<QWidget> m_nativeCanvas;
    QPointer<QWidget> m_canvasSlot;
    QStackedLayout *m_canvasStack=nullptr;
    QWidget *m_canvasHolder=nullptr,*m_emptyCanvas=nullptr;
    QVBoxLayout *m_canvasLayout=nullptr;
    KritaMobileBrushWorkspace *m_brushWorkspace=nullptr;
    KritaMobileLayersPanel *m_layers=nullptr;
    MobileColorWorkspace *m_colorWorkspace=nullptr;
    MobileQuickActions *m_quickActions=nullptr;
    QToolButton *m_document=nullptr;
    QTimer *m_updates=nullptr;
    std::function<void()> m_exit;
};

#endif // Q_OS_ANDROID
