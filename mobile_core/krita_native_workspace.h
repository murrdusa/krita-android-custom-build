/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reuse Mobile Studio's existing brush, layer, color and tool
 * controllers on the native MDI-free engine host. These are views over
 * the real Krita engine and document services, not copies of its state.
 *
 * All controls are created lazily so a blank/failed engine startup
 * never triggers brush preset or node UI initialization.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <optional>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCursor>
#include <QKeyEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QScrollArea>
#include "touch_scroll.h"
#include <QSpinBox>
#include <QObject>
#include <QDockWidget>
#include <QEvent>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <array>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QtMath>
#include <QTouchEvent>
#include <QTabWidget>
#include <QTabBar>
#include <resources/KoAbstractGradient.h>
#include <resources/KoPattern.h>
#include "widgets/gradient/KisGradientChooser.h"
#include "widgets/kis_pattern_chooser.h"
#include "kis_custom_pattern.h"
#include <QSettings>
#include <QVBoxLayout>
#include <KoCanvasController.h>
#include <KoDockFactoryBase.h>
#include <KoDockRegistry.h>
#include <KoToolBase.h>
#include <KoToolDocker.h>
#include <KoToolManager.h>
#include "kis_canvas2.h"
#include "kis_image.h"
#include "kis_selection.h"
#include "kis_config.h"
#include "kis_config_notifier.h"
#include "kis_paint_device.h"
#include "kis_coordinates_converter.h"
#include <KisReferenceImagesLayer.h>
#include <KisReferenceImagesDecoration.h>

#include "KisDocument.h"
#include "input/KisTouchGestureAction.h"
#include "input/kis_input_manager.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_shortcut_configuration.h"
#include "engine_bridge.h"
#include "brush_workspace.h"
#include "layer_workspace.h"
#include "layer_rail_geometry.h"
#include "color_workspace.h"
#include "tool_browser.h"
#include "command_browser.h"
#include "lightweight_canvas_host.h"
#include "kis_mainwindow_observer.h"
#include "native_command_layout.h"
#include "context_tool_bar.h"
#include "finger_pressure_calibration.h"

class MobileKritaNativeWorkspace final : public QObject
{
public:

    explicit MobileKritaNativeWorkspace(
        MobileLightweightCanvasHost *host,
        const MobileHostServices &services,KisViewManager *manager)
        : QObject(host),m_host(host),m_engine(services,manager)
    {
        setObjectName(QStringLiteral("_mobileNativeEngineWorkspaces"));
        // Krita 6 shows a floating Selection Action Bar automatically.
        // Mobile Studio provides a dedicated compact Deselect control and
        // grouped selection actions instead; suppress the desktop bar.
        {
            KisConfig config(false);
            config.setSelectionActionBar(false);
            KisConfigNotifier::instance()->notifyConfigChanged();
        }
        const QSettings settings;
        m_keepLayers=settings.value(QStringLiteral("MobileStudio/keepLayersOpen"),true).toBool();
        m_keepColor=settings.value(QStringLiteral("MobileStudio/keepColorOpen"),false).toBool();
        const bool oldSliders=settings.value(QStringLiteral("MobileStudio/showBrushSliders"),false).toBool();
        m_showSize=settings.value(QStringLiteral("MobileStudio/showSizeSlider"),oldSliders).toBool();
        m_showOpacity=settings.value(QStringLiteral("MobileStudio/showOpacitySlider"),oldSliders).toBool();
        m_fadePanels=settings.value(QStringLiteral("MobileStudio/fadePanels"),true).toBool();
        m_idleOpacity=qBound(15,settings.value(QStringLiteral("MobileStudio/idlePanelOpacity"),30).toInt(),90);
        m_fadeDelay=qBound(1,settings.value(QStringLiteral("MobileStudio/panelFadeDelay"),3).toInt(),30);
        m_fadeClock.start();
        m_fadeTimer=new QTimer(this);
        m_fadeTimer->setInterval(160);
        connect(m_fadeTimer,&QTimer::timeout,this,[this]() {updatePanelFades();});
        // Fading is only a foreground visual effect. Stop the recurring
        // wakeups while Android has backgrounded the drawing app.
        connect(qApp,&QGuiApplication::applicationStateChanged,this,
                [this](Qt::ApplicationState state) {
            if(!m_fadeTimer)return;
            if(state==Qt::ApplicationActive && m_fadePanels)
                m_fadeTimer->start();
            else m_fadeTimer->stop();
        });
        if(m_fadePanels && QGuiApplication::applicationState()==Qt::ApplicationActive)
            m_fadeTimer->start();
        m_samplePreviewTimer.setSingleShot(true);
        m_samplePreviewTimer.setInterval(16);
        connect(&m_samplePreviewTimer,&QTimer::timeout,this,[this]() {
            if(const auto sample=toolbarColorAt(m_samplePoint,m_sampleLayerOnly)) {
                if(auto *preview=toolbarPreview())preview->color(sample->toQColor(),m_samplePoint);
            } else if(m_toolbarPreview) {
                m_toolbarPreview->hide();
            }
        });
        qApp->installEventFilter(this);
        auto *contextRefresh=new QTimer(this);
        contextRefresh->setSingleShot(true);
        connect(contextRefresh,&QTimer::timeout,this,
                [this]() {syncContextToolBar();});
        if(auto *tools=KoToolManager::instance()) {
            connect(tools,&KoToolManager::aboutToChangeTool,this,
                    [this,tools](KoCanvasController *controller) {
                if(controller!=tools->activeCanvasController())return;
                if(m_toolOptions)m_toolOptions->hide();
                if(m_brushOptions)m_brushOptions->hide();
            });
            connect(tools,&KoToolManager::toolOptionWidgetsChanged,contextRefresh,
                    [tools,contextRefresh](KoCanvasController *controller,
                                            const QList<QPointer<QWidget>> &) {
                if(controller==tools->activeCanvasController())
                    contextRefresh->start(0);
            });
        }
        // The factory installs the native activation router as each
        // document is attached. Refresh after, not before, that turn.
        if(auto *root=host ? dynamic_cast<MobileNativeViewStack*>(
                                 host->nativeCanvas()) : nullptr) {
            root->installEventFilter(this);
            QObject::connect(root,&QStackedWidget::currentChanged,this,
                             [this,root=QPointer<MobileNativeViewStack>(root)](int) {
                if(!root)return;
                armEyedropper(false);
                MobileToolBrowser::dismiss(m_host);
                if(m_brush)m_brush->dismissPresetBrowser();
                const QPointer<QWidget> selected=root->currentWidget();
                // The native factory schedules manager->setCurrentView()
                // on the first event-loop turn. The workspace is wired
                // earlier than that factory, so its original single-shot
                // refresh ran before Krita switched tools and documents.
                // A second queued turn follows native activation; reject
                // obsolete refreshes when the user switches again quickly.
                QTimer::singleShot(0,this,[this,root,selected]() {
                    QTimer::singleShot(0,this,[this,root,selected]() {
                        if(!root || root->currentWidget()!=selected)return;
                        refresh();
                        placeDrawingOverlays();
                        raiseOverlays();
                    });
                });
            });
        }
        auto *poll=new QTimer(this);
        poll->setInterval(600);
        QObject::connect(poll,&QTimer::timeout,this,[this]() {
            if(m_layers && m_layers->isVisible())
                m_layers->refreshFromEngine();
            syncSelectionDismissButton();
            syncCloneSourceButton();
            syncReferenceShortcut();
        });
        poll->start();
    }

    void bindBrushModes(QAction *paint,QAction *erase,QAction *blend)
    {
        m_brushModes={paint,erase,blend};
        for(auto *action:m_brushModes) {
            action->setCheckable(true);
            action->setProperty("mobileBrushMode",true);
        }
        if(auto *nativeErase=m_engine.action("erase_action"))
            connect(nativeErase,&QAction::toggled,this,[this]() {syncBrushModes();});
        if(auto *provider=m_engine.brushResources())
            connect(provider,&KisCanvasResourceProvider::sigPaintOpPresetChanged,
                    this,[this]() {syncBrushModes();});
        syncBrushModes();
    }

    void toggleBrushes(int slot)
    {
        if(!m_host || !m_engine.hasDocument())return;
        if(!ensureBrushControls() || m_switchingBrush)return;
        const QScopedValueRollback<bool> switching(m_switchingBrush,true);
        const auto request=++m_brushRequest;
        if(!m_brush->isActiveMode(slot)) {
            m_brush->dismissPresetBrowser();
            m_brush->activateMode(slot);
            syncBrushModes();
            return;
        }
        const auto view=MobileViewIdentityGuard::uniqueActive(m_engine.openDocuments());
        QTimer::singleShot(0,this,[this,slot,view,request]() {
            if(request!=m_brushRequest)return;
            if(m_brush && m_brush->isActiveMode(slot) && view && view==MobileViewIdentityGuard::uniqueActive(m_engine.openDocuments()))
                m_brush->browsePresets(slot,m_brushModes.value(slot));
            syncBrushModes();
        });
    }

    void pickCloneSource()
    {
        const auto *view=m_engine.manager();
        const QPointer<KisDocument> document=view?view->document():nullptr;
        if(!document)return;
        QTimer::singleShot(0,this,[this,document]() {
            auto *view=m_engine.manager();
            if(!document || !view || view->document()!=document || !ensureBrushControls())return;
            if(!m_brush->activateCloneBrush()) {
                view->showFloatingMessage(tr("No Clone Brush preset is available."),QIcon());
                return;
            }
            if(!document || view->document()!=document)return;
            auto *tools=KoToolManager::instance();
            auto *controller=tools?tools->activeCanvasController():nullptr;
            if(!controller || controller->canvas()!=view->canvasBase() ||
               tools->activeToolId().section(QLatin1Char('/'),-1)!=
                   QStringLiteral("KisToolBrush"))return;
            auto *tool=tools->toolById(controller->canvas(),tools->activeToolId());
            bool armed=false;
            if(tool && QMetaObject::invokeMethod(tool,"mobileArmCloneSource",
                   Qt::DirectConnection,Q_RETURN_ARG(bool,armed)) && armed)
                view->showFloatingMessage(tr("Tap the canvas to set the Clone Brush source."),QIcon());
            syncBrushModes();
            syncCloneSourceButton();
        });
    }

    void bindPanelActions(QAction *layers,QAction *color,QAction *options,QAction *tools)
    {
        m_toolsAction=tools;
        m_layersAction=layers;
        m_colorAction=color;
        m_optionsAction=options;
        for(auto *action:{layers,color,options})action->setCheckable(true);
        syncPanelActions();
    }

    void toggleToolSettings()
    {
        if(!m_host || !m_engine.hasDocument() || m_canvasOnly)return;
        if(m_engine.activeTool().section(QLatin1Char('/'),-1)!=QStringLiteral("KisToolBrush") &&
           !(m_brush && m_brush->isActiveMode(2))) {
            toggleToolOptions();
            return;
        }
        if(!m_brushOptions) {
            m_brushOptions=new KritaMobileBrushWorkspace(m_engine,m_host->nativeCanvas(),true,true);
            m_brushOptions->installEventFilter(this);
        } else if(m_brushOptions->isVisible()) {
            m_brushOptions->hide();
            return;
        }
        m_brushOptions->refresh();
        showOverlay(m_brushOptions);
    }

    void openColorOptions()
    {
        if(!m_host || !m_engine.hasDocument())return;
        if(!m_color && !ensureColorPanel())return;
        m_color->openWheelOptions();
        placeDrawingOverlays();
    }

    void applyScreenWorkspace(const QString &id,bool portrait)
    {
        m_workspaceId=id;
        if(m_host && m_host->nativeCanvas())m_workspaceSize=m_host->nativeCanvas()->size();
        const QString prefix=MobileWorkspaceProfiles::group(id);
        const QSettings settings;
        m_panelPlacements=settings.value(prefix+QStringLiteral("canvasPanelPlacements")).toMap();
        m_layersHorizontal=settings.value(prefix+QStringLiteral("layersHorizontal"),portrait).toBool();
        const bool oldSliders=settings.value(prefix+QStringLiteral("showBrushSliders"),false).toBool();
        m_showSize=settings.value(prefix+QStringLiteral("showSizeSlider"),oldSliders).toBool();
        m_showOpacity=settings.value(prefix+QStringLiteral("showOpacitySlider"),oldSliders).toBool();
        for(int i=0;i<2;++i) {
            const QString key=prefix+sliderLayoutKey(i);
            m_sliderCorners[i]=qBound(0,settings.value(key+QStringLiteral("Corner"),2).toInt(),3);
            m_sliderWidths[i]=qBound(120,settings.value(key+QStringLiteral("Width"),168).toInt(),360);
        }
        m_layersCollapsed=settings.value(prefix+QStringLiteral("layersCollapsed"),false).toBool();
        m_layerCorner=qBound(0,settings.value(prefix+QStringLiteral("layerCorner"),1).toInt(),3);
        m_layerWidth=qBound(64,settings.value(prefix+QStringLiteral("layerWidth"),76).toInt(),138);
        m_layerHeight=qBound(25,settings.value(prefix+QStringLiteral("layerHeight"),100).toInt(),100);
        m_legacyLayerCorner=settings.contains(prefix+QStringLiteral("layerCorner"));
        syncLayersVisibility();
        updateBrushSliders();
        raiseOverlays();
    }

    void toggleLayers()
    {
        if(!m_host || !m_engine.hasDocument() || m_canvasOnly)return;
        m_layersCollapsed=!m_layersCollapsed;
        saveWorkspacePanels();
        syncLayersVisibility();
        placeDrawingOverlays();
        raiseOverlays();
    }

    // Reuse the same KoToolBase option widgets shown by Krita's
    // desktop Tool Options docker. They are live engine controls,
    // not copies or reverse-engineered equivalents. This also exposes
    // selection, transform, perspective and fill configuration.
    void toggleToolOptions()
    {
        if(!m_host || !m_engine.hasDocument())return;
        KoToolManager *tools=KoToolManager::instance();
        if(!tools || !tools->activeCanvasController())return;
        QWidget *canvas=m_host->nativeCanvas();
        if(!canvas)return;

        if(!m_toolOptions) {
            auto *dock=new KoToolDocker(canvas);
            dock->setObjectName(QStringLiteral("_mobileNativeToolOptions"));
            // Native tool widgets inherit Qt's Android palette, which
            // may put white text into white editable fields. Limit the
            // contrast correction to this dock, not the drawing canvas
            // or Krita's own tool behavior.
            dock->setStyleSheet(QStringLiteral(
                "QDockWidget#_mobileNativeToolOptions{"
                "background:#29323b;color:#f3f7fc;}"
                "QDockWidget#_mobileNativeToolOptions QLineEdit,"
                "QDockWidget#_mobileNativeToolOptions QComboBox,"
                "QDockWidget#_mobileNativeToolOptions QSpinBox,"
                "QDockWidget#_mobileNativeToolOptions QDoubleSpinBox{"
                "background:#eef3f7;color:#172530;"
                "selection-background-color:#3878a4;"
                "selection-color:#ffffff;}"
                "QDockWidget#_mobileNativeToolOptions QComboBox QAbstractItemView{"
                "background:#eef3f7;color:#172530;"
                "selection-background-color:#3878a4;"
                "selection-color:#ffffff;}"
                "QDockWidget#_mobileNativeToolOptions QLabel,"
                "QDockWidget#_mobileNativeToolOptions QRadioButton,"
                "QDockWidget#_mobileNativeToolOptions QCheckBox{"
                "color:#f3f7fc;}"));
            auto *titleBar=new QWidget(dock);
            titleBar->setFixedHeight(0);
            dock->setTitleBarWidget(titleBar);
            dock->installEventFilter(this);
            if(auto *scroll=qobject_cast<QScrollArea*>(dock->widget())) {
                QScroller::ungrabGesture(scroll);
                MobileTouchScroll::configure(scroll);
            }
            m_toolOptions=dock;
            // One owner for the option widgets; the active native
            // tool continues to own their state and lifetime.
            QObject::connect(tools,&KoToolManager::toolOptionWidgetsChanged,
                             dock,[this,dock](KoCanvasController *controller,
                                  const QList<QPointer<QWidget>> &widgets) {
                if(!m_host || !m_toolOptions)return;
                auto *manager=KoToolManager::instance();
                if(!manager || controller!=manager->activeCanvasController())
                    return;
                dock->setOptionWidgets(widgets);
            });
            checkpoint("Krita native tool options: overlay connected");
        } else if(m_toolOptions->isVisible()) {
            m_toolOptions->hide();
            return;
        }

        // KoToolDocker initially reports only its collapsed title-frame
        // size until the active tool widgets have been installed. Populate
        // the native option widgets *before* determining overlay geometry.
        if(auto *activeTool=tools->toolById(
               tools->activeCanvasController()->canvas(),
               tools->activeToolId()))
            m_toolOptions->setOptionWidgets(activeTool->optionWidgets());
        showOverlay(m_toolOptions);
    }

    void toggleColor()
    {
        if(!m_host || !m_engine.hasDocument()) {
            checkpoint("Color wheel: no active native document");
            return;
        }
        // A floating child of QMainWindow can sit BEHIND its central
        // canvas/dock layout on Android. Parent it to the real native
        // document root, so raise() places it above painting views.
        QWidget *canvas=m_host->nativeCanvas();
        if(!canvas) {
            checkpoint("Color wheel: no native canvas root");
            return;
        }
        if(m_color && m_color->isVisible()) {
            m_color->hide();
            return;
        }
        if(!ensureColorPanel())return;
        m_color->refresh();
        showOverlay(m_color);
    }

    void chooseTool(QWidget *opener=nullptr)
    {
        if(!m_host || !m_engine.hasDocument())return;
        QTimer::singleShot(0,this,[this,opener=QPointer<QWidget>(opener)]() {
            if(m_host)MobileToolBrowser::toggle(m_engine,m_host,
                opener?opener.data():m_contextToolOpener.data(),m_toolsAction);
        });
    }

    void browseCommands()
    {
        if(!m_host || !m_engine.hasDocument())return;
        QPointer<MobileLightweightCanvasHost> host=m_host;
        // The native More popup must close before the modeless-to-modal
        // transition; the browser dispatches only currently enabled
        // Krita QActions, not synthetic keyboard shortcuts.
        QTimer::singleShot(0,this,[this,host]() {
            if(host)MobileCommandBrowser::open(m_engine,host.data());
        });
    }

    void chooseFillResource(bool gradient,QWidget *anchor=nullptr)
    {
        if(!m_host || !m_engine.hasDocument())return;
        QTimer::singleShot(0,this,[this,gradient,anchor=QPointer<QWidget>(anchor)]() {
            if(!m_host || !m_engine.hasDocument())return;
            auto *manager=m_engine.manager();
            QPointer<KisCanvasResourceProvider> provider=m_engine.brushResources();
            QPointer<KisDocument> document=manager?manager->document():nullptr;
            if(!provider || !document)return;

            if(MobileTransientPanel::closeForAnchor(m_host,anchor))return;
            auto *panel=new MobileTransientPanel(m_host);
            panel->setObjectName(QStringLiteral("mobileFillResources"));
            panel->setAccessibleName(gradient?tr("Gradients"):tr("Patterns"));
            panel->setStyleSheet(QStringLiteral(
                "QFrame#mobileFillResources{background:#252529;border:1px solid #55555c;"
                "border-radius:8px;}"
                "QDialogButtonBox QPushButton{min-height:40px;padding:0 14px;}"
                "QTabBar::tab{min-height:36px;padding:0 10px;}"));
            auto *layout=new QVBoxLayout(panel);
            const auto applyResource=[this,panel,provider,document,gradient](const KoResourceSP &resource) {
                if(!resource || !provider || !document || !m_engine.manager() ||
                   m_engine.manager()->document()!=document)return;
                if(gradient)provider->slotGradientActivated(resource);
                else provider->slotPatternActivated(resource);
                panel->close();
            };
            std::function<KoResourceSP()> selected;
            if(gradient) {
                auto *chooser=new KisGradientChooser(panel,nullptr,false);
                chooser->setCanvasResourcesInterface(
                    provider->resourceManager()->canvasResourcesInterface());
                chooser->setItemSize(KisGradientChooser::ItemSize_Large);
                chooser->setCurrentResource(provider->currentGradient());
                layout->addWidget(chooser,1);
                selected=[chooser]() {return chooser->currentResource();};
            } else {
                auto *tabs=new QTabWidget(panel);
                auto *chooser=new KisPatternChooser(tabs);
                chooser->setPreviewOrientation(Qt::Vertical);
                if(auto pattern=provider->currentPattern())
                    chooser->setCurrentPattern(pattern);
                tabs->addTab(chooser,tr("Patterns"));
                auto *custom=new KisCustomPattern(tabs,"mobileCanvasPattern",
                                                  tr("From Canvas"),manager);
                tabs->addTab(custom,tr("From Canvas"));
                connect(custom,&KisCustomPattern::activatedResource,panel,applyResource);
                connect(custom,&KisCustomPattern::patternAdded,
                        chooser,&KisPatternChooser::setCurrentPattern);
                selected=[chooser,tabs]() {
                    return tabs->currentIndex()==0?chooser->currentResource():KoResourceSP{};
                };
                layout->addWidget(tabs,1);
            }
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply,panel);
            connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,panel,
                    [applyResource,selected]() {applyResource(selected());});
            layout->addWidget(buttons);
            panel->anchorTo(anchor,QSize(440,460));
            panel->present();
        });
    }

    // Use the same settings and input profile as the earlier Android UI.
    void openTouchGestures(QWidget *parent)
    {
        using Shortcut=KisShortcutConfiguration;
        using Touch=KisTouchGestureAction;
        using Choices=QMap<QString,QStringList>;
        const auto binding=[](const QString &action,int mode=0) {
            return QStringList{action,QString::number(mode)};
        };
        const auto command=[&](Touch::Shortcut mode) {
            return binding(QStringLiteral("Touch Gestures"),mode);
        };
        Choices moreCommands;
        for(const auto *action:KisInputProfileManager::instance()->actions()) {
            if(action->id()!=QStringLiteral("Touch Gestures"))continue;
            const auto modes=action->shortcutIndexes();
            for(auto mode=modes.cbegin();mode!=modes.cend();++mode)
                moreCommands.insert(mode.key(),binding(action->id(),mode.value()));
        }

        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Touch gestures"));
        MobileTouchScroll::styleMenuDialog(dialog,QSize(480,640));
        auto *layout=new QVBoxLayout(&dialog);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        MobileTouchScroll::configure(scroll);
        auto *body=new QWidget(scroll);
        auto *form=new QFormLayout(body);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        scroll->setWidget(body);
        layout->addWidget(scroll,1);
        auto *note=new QLabel(QObject::tr(
            "Profile defaults follow Krita’s active input profile. "
            "One-finger taps and drags follow Krita’s touch-painting setting."),body);
        note->setWordWrap(true);
        form->addRow(note);

        QSettings settings;
        settings.beginGroup(QStringLiteral("MobileStudio/touchGestures"));
        QVector<QPair<QString,QComboBox*>> rows;
        for(const auto gesture:{Shortcut::OneFingerDoubleTap,Shortcut::OneFingerTap,
            Shortcut::OneFingerHold,Shortcut::TwoFingerTap,Shortcut::ThreeFingerTap,
            Shortcut::FourFingerTap,Shortcut::FiveFingerTap,Shortcut::OneFingerDrag,
            Shortcut::TwoFingerDrag,Shortcut::ThreeFingerDrag,Shortcut::FourFingerDrag,
            Shortcut::FiveFingerDrag}) {
            const QString key=QString::number(gesture);
            const QString label=Shortcut::gestureToText(gesture);
            auto *combo=new MobileTouchComboBox(body);
            combo->addItem((gesture==Shortcut::OneFingerDoubleTap || gesture==Shortcut::TwoFingerTap)?
                QObject::tr("Default (Flip Canvas)"):
                gesture==Shortcut::OneFingerTap?QObject::tr("Default (no popup palette)"):
                QObject::tr("Krita profile default"));
            combo->addItem(QObject::tr("Disabled"),QStringList{QStringLiteral("disabled")});
            combo->addItem(QObject::tr("Undo"),command(Touch::UndoActionShortcut));
            combo->addItem(QObject::tr("Redo"),command(Touch::RedoActionShortcut));
            combo->addItem(QObject::tr("Flip Canvas"),command(Touch::MirrorView));
            combo->addItem(QObject::tr("Reset view"),command(Touch::ResetDisplay));
            combo->addItem(QObject::tr("Canvas Only"),command(Touch::ToggleCanvasOnlyShortcut));
            Choices more=moreCommands;
            if(gesture>=Shortcut::OneFingerDrag && gesture<=Shortcut::FiveFingerDrag) {
                combo->addItem(QObject::tr("Pan canvas"),binding(QStringLiteral("Pan Canvas")));
                if(gesture!=Shortcut::OneFingerDrag) {
                    combo->addItem(QObject::tr("Zoom and rotate"),
                                   binding(QStringLiteral("Zoom and Rotate Canvas")));
                    more.insert(QObject::tr("Zoom canvas"),binding(QStringLiteral("Zoom Canvas")));
                    more.insert(QObject::tr("Rotate canvas"),binding(QStringLiteral("Rotate Canvas")));
                }
            } else if(gesture==Shortcut::OneFingerHold) {
                combo->addItem(QObject::tr("Sample color"),binding(QStringLiteral("Alternate Invocation"),4));
            }
            if(gesture>=Shortcut::OneFingerTap && gesture<=Shortcut::FiveFingerTap)
                more.insert(QObject::tr("Show popup palette"),binding(QStringLiteral("Show Popup Widget")));
            const int moreIndex=combo->count();
            combo->addItem(QObject::tr("More actions…"));
            const auto setChoice=[combo,moreIndex,more](const QVariant &value) {
                int index=value.isValid()?combo->findData(value):0;
                if(index<0) {
                    index=combo->count();
                    combo->addItem(more.key(value.toStringList(),QObject::tr("Custom action")),value);
                }
                combo->setCurrentIndex(index==moreIndex?0:index);
            };
            setChoice(settings.value(key));
            auto previous=std::make_shared<int>(combo->currentIndex());
            connect(combo,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[combo,moreIndex,more,setChoice,previous,label](int index) {
                    if(index!=moreIndex) {
                        *previous=index;
                        return;
                    }
                    QSignalBlocker blocker(combo);
                    combo->setCurrentIndex(*previous);
                    QDialog picker(combo);
                    picker.setWindowTitle(label);
                    picker.resize(qMax(260,combo->width()),400);
                    auto *pickerLayout=new QVBoxLayout(&picker);
                    auto *list=new QListWidget(&picker);
                    list->setStyleSheet(QStringLiteral("QListWidget::item{min-height:44px;padding:5px;}"));
                    MobileTouchScroll::configureList(list);
                    for(auto option=more.cbegin();option!=more.cend();++option) {
                        if(combo->findData(option.value())>=0)continue;
                        auto *item=new QListWidgetItem(option.key(),list);
                        item->setData(Qt::UserRole,option.value());
                    }
                    pickerLayout->addWidget(list,1);
                    connect(list,&QListWidget::itemClicked,&picker,&QDialog::accept);
                    auto *cancel=new QPushButton(QObject::tr("Cancel"),&picker);
                    pickerLayout->addWidget(cancel);
                    connect(cancel,&QPushButton::clicked,&picker,&QDialog::reject);
                    if(picker.exec()==QDialog::Accepted && list->currentItem()) {
                        setChoice(list->currentItem()->data(Qt::UserRole));
                        *previous=combo->currentIndex();
                    }
                });
            form->addRow(label,combo);
            rows.append(qMakePair(key,static_cast<QComboBox*>(combo)));
        }
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        for(const auto &row:rows) {
            if(row.second->currentIndex()==0)settings.remove(row.first);
            else settings.setValue(row.first,row.second->currentData());
        }
        if(auto *manager=m_engine.manager()) {
            QMetaObject::invokeMethod(manager->inputManager(),"profileChanged",Qt::QueuedConnection);
        }
    }

    // Qt's native brush input still decides which gestures are painting.
    void openInputSettings()
    {
        if(!m_host)return;
        QPointer<MobileLightweightCanvasHost> host=m_host;
        QTimer::singleShot(0,this,[this,host]() {
            if(!host)return;
            QDialog dialog(host);
            dialog.setWindowTitle(QObject::tr("Touch, Pressure & Buttons"));
            MobileTouchScroll::styleMenuDialog(dialog,QSize(500,660));
            auto *outer=new QVBoxLayout(&dialog);
            outer->setContentsMargins(12,10,12,10);
            auto *heading=new QLabel(tr("Touch, Pressure & Buttons"),&dialog);
            heading->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
            outer->addWidget(heading);
            auto *scroll=new QScrollArea(&dialog);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            MobileTouchScroll::configure(scroll);
            auto *body=new QWidget(scroll);
            body->setObjectName(QStringLiteral("mobileInputSettingsBody"));
            auto *form=new QFormLayout(body);
            form->setRowWrapPolicy(QFormLayout::WrapAllRows);
            form->setVerticalSpacing(10);
            scroll->setWidget(body);
            outer->addWidget(scroll,1);
            QSettings settings;

            auto *finger=new QCheckBox(QObject::tr(
                "Finger contact area controls brush pressure"),body);
            finger->setObjectName(QStringLiteral("mobileFingerPressureEnabled"));
            finger->setChecked(settings.value(QString::fromLatin1(
                MobileFingerPressureSettings::enabledKey()),
                MobileFingerPressureSettings::defaultEnabled()).toBool());
            form->addRow(finger);
            QObject::connect(finger,&QCheckBox::toggled,&dialog,[](bool enabled) {
                QSettings().setValue(QString::fromLatin1(
                    MobileFingerPressureSettings::enabledKey()),enabled);
            });
            auto *pressureInfo=new QLabel(QObject::tr(
                "Finger contact size estimates pressure for single-finger painting."),body);
            pressureInfo->setWordWrap(true);
            form->addRow(pressureInfo);
            auto *calibrate=new QPushButton(QObject::tr(
                "Calibrate contact size and sensitivity"),body);
            form->addRow(calibrate);
            QObject::connect(calibrate,&QPushButton::clicked,&dialog,[host]() {
                if(!host)return;
                MobileFingerPressureCalibrationDialog calibration(host);
                calibration.exec();
            });

            auto *gestures=new QPushButton(QObject::tr("Touch gestures…"),body);
            form->insertRow(0,gestures);
            connect(gestures,&QPushButton::clicked,&dialog,[this,&dialog]() {
                openTouchGestures(&dialog);
            });

            // Reference-image gestures reuse Krita's native image shape
            // hit testing even while Paint/Erase/Blend is active.
            // Expose the same persisted preferences used by the
            // independent-canvas input patch.
            auto *referenceDrag=new QCheckBox(QObject::tr(
                "Drag reference images with one finger in any tool"),body);
            referenceDrag->setChecked(settings.value(
                QStringLiteral("TabletUi/referenceFingerDrag"),true).toBool());
            form->addRow(referenceDrag);
            QObject::connect(referenceDrag,&QCheckBox::toggled,&dialog,
                [](bool enabled) {
                    QSettings().setValue(
                        QStringLiteral("TabletUi/referenceFingerDrag"),enabled);
                });
            auto *referenceResize=new QCheckBox(QObject::tr(
                "Two-finger resize and rotate reference images"),body);
            referenceResize->setChecked(settings.value(
                QStringLiteral("TabletUi/referenceTwoFinger"),true).toBool());
            form->addRow(referenceResize);
            QObject::connect(referenceResize,&QCheckBox::toggled,&dialog,
                [](bool enabled) {
                    QSettings().setValue(
                        QStringLiteral("TabletUi/referenceTwoFinger"),enabled);
                });
            auto *referenceFlip=new QCheckBox(QObject::tr(
                "Double-tap reference image to flip horizontally"),body);
            referenceFlip->setChecked(settings.value(
                QStringLiteral("TabletUi/referenceDoubleTapEnabled"),
                true).toBool());
            form->addRow(referenceFlip);
            QObject::connect(referenceFlip,&QCheckBox::toggled,&dialog,
                [](bool enabled) {
                    QSettings().setValue(
                        QStringLiteral("TabletUi/referenceDoubleTapEnabled"),
                        enabled);
                });
            auto *referenceFlipInterval=new QSpinBox(body);
            referenceFlipInterval->setRange(120,650);
            referenceFlipInterval->setSuffix(QObject::tr(" ms"));
            referenceFlipInterval->setValue(qBound(120,settings.value(
                QStringLiteral("TabletUi/referenceDoubleTapMs"),350).toInt(),650));
            referenceFlipInterval->setEnabled(referenceFlip->isChecked());
            form->addRow(QObject::tr("Reference image double-tap interval"),
                         referenceFlipInterval);
            QObject::connect(referenceFlip,&QCheckBox::toggled,
                             referenceFlipInterval,&QWidget::setEnabled);
            QObject::connect(referenceFlipInterval,
                QOverload<int>::of(&QSpinBox::valueChanged),
                &dialog,[](int value) {
                    QSettings().setValue(
                        QStringLiteral("TabletUi/referenceDoubleTapMs"),value);
                });
            auto *referenceFlipInfo=new QLabel(QObject::tr(
                "Reference-image double-tap is enabled by default. "
                "It flips only the reference, even while painting. "
                "Small finger movement does not move a reference."),body);
            referenceFlipInfo->setWordWrap(true);
            form->addRow(referenceFlipInfo);

            auto *mirrorAtPen=new QCheckBox(QObject::tr("Keep drawing point in place when mirroring"),body);
            mirrorAtPen->setChecked(settings.value(
                QStringLiteral("MobileStudio/mirrorAtPen"),true).toBool());
            form->addRow(mirrorAtPen);
            QObject::connect(mirrorAtPen,&QCheckBox::toggled,&dialog,[](bool enabled) {
                QSettings().setValue(QStringLiteral("MobileStudio/mirrorAtPen"),enabled);
            });
            auto *doubleTap=new QSpinBox(body);
            doubleTap->setRange(120,600);
            doubleTap->setSuffix(QObject::tr(" ms"));
            doubleTap->setValue(qBound(120,settings.value(
                QStringLiteral("TabletUi/doubleTapMs"),250).toInt(),600));
            form->addRow(QObject::tr("One-finger double tap interval"),doubleTap);
            QObject::connect(doubleTap,QOverload<int>::of(&QSpinBox::valueChanged),
                &dialog,[](int value) {
                    QSettings().setValue(QStringLiteral("TabletUi/doubleTapMs"),value);
                });
            auto *pinch=new QCheckBox(QObject::tr(
                "Quick two-finger pinch: reset canvas view"),body);
            pinch->setChecked(settings.value(
                QStringLiteral("TabletUi/quickPinch"),false).toBool());
            form->addRow(pinch);
            QObject::connect(pinch,&QCheckBox::toggled,&dialog,[](bool checked) {
                QSettings().setValue(QStringLiteral("TabletUi/quickPinch"),checked);
            });
            auto *pinchMs=new QSpinBox(body);
            pinchMs->setRange(120,500);
            pinchMs->setSuffix(QObject::tr(" ms"));
            pinchMs->setValue(qBound(120,settings.value(
                QStringLiteral("TabletUi/pinchMs"),250).toInt(),500));
            form->addRow(QObject::tr("Quick pinch interval"),pinchMs);
            QObject::connect(pinchMs,QOverload<int>::of(&QSpinBox::valueChanged),
                &dialog,[](int value) {
                    QSettings().setValue(QStringLiteral("TabletUi/pinchMs"),value);
                });

            auto *volume=new QCheckBox(QObject::tr(
                "Use volume keys for drawing actions"),body);
            volume->setChecked(settings.value(
                QStringLiteral("TabletUi/volumeKeys"),true).toBool());
            form->addRow(volume);
            QObject::connect(volume,&QCheckBox::toggled,&dialog,[](bool enabled) {
                QSettings().setValue(QStringLiteral("TabletUi/volumeKeys"),enabled);
            });
            const auto addVolume=[&](const QString &label,const char *key,
                                     int fallback) {
                auto *combo=new MobileTouchComboBox(body);
                combo->addItem(QObject::tr("Normal volume / Disabled"),0);
                combo->addItem(QObject::tr("Undo"),1);
                combo->addItem(QObject::tr("Redo"),2);
                combo->addItem(QObject::tr("Toggle eraser"),3);
                combo->addItem(QObject::tr("Pick color once"),4);
                combo->addItem(QObject::tr("Brush size +"),5);
                combo->addItem(QObject::tr("Brush size -"),6);
                const QString setting=QString::fromLatin1(key);
                combo->setCurrentIndex(qMax(0,combo->findData(
                    settings.value(setting,fallback).toInt())));
                form->addRow(label,combo);
                QObject::connect(combo,QOverload<int>::of(&QComboBox::currentIndexChanged),
                    &dialog,[combo,setting](int) {
                        QSettings().setValue(setting,combo->currentData().toInt());
                    });
            };
            addVolume(QObject::tr("Volume down / tap"),
                "TabletUi/volumeDownShort",1);
            addVolume(QObject::tr("Volume up / tap"),
                "TabletUi/volumeUpShort",2);
            addVolume(QObject::tr("Volume down / hold"),
                "TabletUi/volumeDownLong",1);
            addVolume(QObject::tr("Volume up / hold"),
                "TabletUi/volumeUpLong",2);
            auto *holdMs=new QSpinBox(body);
            holdMs->setRange(80,1000);
            holdMs->setSuffix(QObject::tr(" ms"));
            holdMs->setValue(qBound(80,settings.value(
                QStringLiteral("TabletUi/volumeLongMs"),200).toInt(),1000));
            form->addRow(QObject::tr("Volume hold threshold"),holdMs);
            QObject::connect(holdMs,QOverload<int>::of(&QSpinBox::valueChanged),
                &dialog,[](int value) {
                    QSettings().setValue(QStringLiteral("TabletUi/volumeLongMs"),value);
                });

            auto *done=new QPushButton(QObject::tr("Done"),&dialog);
            outer->addWidget(done);
            QObject::connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
            dialog.exec();
        });
    }

    // The Android Java volume handler invokes this only on the Qt GUI
    // thread. Actions reuse the standalone app's document-safe history.
    void bindVolumeHistory(QAction *undo,QAction *redo)
    {
        m_volumeUndo=undo;
        m_volumeRedo=redo;
    }

    void resetCanvasView()
    {
        if(!m_engine.hasDocument())return;
        if(QAction *rotation=m_engine.action("reset_canvas_rotation"))
            if(rotation->isEnabled())rotation->trigger();
        if(QAction *fit=m_engine.action("zoom_to_fit"))
            if(fit->isEnabled())fit->trigger();
    }

    bool dispatchVolumeAction(int code)
    {
        if(!m_engine.hasDocument())return false;
        QAction *action=nullptr;
        switch(code) {
        case 1: action=m_volumeUndo;break;
        case 2: action=m_volumeRedo;break;
        case 3: action=m_engine.action("erase_action");break;
        case 4: action=m_engine.action("tablet_pick_once");break;
        case 5: action=m_engine.action("increase_brush_size");break;
        case 6: action=m_engine.action("decrease_brush_size");break;
        default: return false;
        }
        if(!action || !action->isEnabled())return false;
        action->trigger();
        return true;
    }

    QVector<MobileCanvasLayoutEditor::Target> layoutPanels()
    {
        QVector<MobileCanvasLayoutEditor::Target> targets;
        auto *canvas=m_host?m_host->nativeCanvas():nullptr;
        if(!canvas)return targets;
        for(const auto &entry:editablePanels()) {
            auto *panel=entry.second;
            if(!panel || !panel->isVisible())continue;
            QSize minimum(120,44);
            if(panel==m_color)minimum=QSize(104,104);
            const bool layers=panel==m_layers;
            if(layers)minimum=m_layersHorizontal?QSize(110,58):QSize(64,68);
            targets.append({panel,minimum,layers,
                [this,canvas,id=entry.first,layers](const QRect &rect,bool rotate) {
                    m_layoutPanelsDirty=true;
                    m_panelPlacements.insert(id,MobileCanvasLayoutEditor::relative(
                        rect,canvas->rect().adjusted(2,2,-2,-2)));
                    if(layers && rotate) {
                        m_layersHorizontal=!m_layersHorizontal;

                    }
                },layers?tr("Rotate Layers"):QString(),layers?
                    Qt::Orientations(m_layersHorizontal?Qt::Vertical:Qt::Horizontal):Qt::Orientations()});
        }
        return targets;
    }

    void setCanvasLayoutEditing(bool editing)
    {
        m_canvasLayoutEditing=editing;
        if(editing) {
            clearToolbarPreview();
            return;
        }
        if(m_layoutPanelsDirty)saveWorkspacePanels();
        m_layoutPanelsDirty=false;
        syncLayersVisibility();
        updateBrushSliders();
        syncContextToolBar();
        placeDrawingOverlays();
        raiseOverlays();
    }

    void adjustToolbarDrag(const QString &parameter,qreal pixels,const QPoint &global,
                           MobileToolbarDrag::Phase phase)
    {
        using MobileToolbarDrag::Phase;
        auto *provider=m_engine.brushResources();
        if(phase==Phase::Cancel || !m_engine.hasDocument() || !provider) {
            clearToolbarPreview();
            return;
        }
        const bool opacity=parameter==QStringLiteral("opacity");
        if(m_dragParameter!=parameter) {
            m_dragParameter=parameter;
            m_dragBaseline=opacity?provider->opacity():provider->size();
            m_dragMaximum=opacity?1.0:KisImageConfig(true).maxBrushSize();
        }
        const qreal value=opacity?qBound(qreal(0),m_dragBaseline+pixels/200.0,qreal(1)):
            qBound(qreal(0.01),m_dragBaseline*qPow(2.0,pixels/120.0),m_dragMaximum);
        if(phase==Phase::Commit) {
            if(opacity)provider->setOpacity(value);
            else provider->setSize(value);
            clearToolbarPreview();
        } else if(auto *preview=toolbarPreview()) {
            const QString text=opacity?tr("Opacity %1%").arg(qRound(value*100)):
                tr("Size %1 px").arg(QString::number(value,'f',value<1?2:value<10?1:0));
            preview->value(text,opacity?value:qBound(qreal(0.01),pixels/400.0+0.5,qreal(1)),global);
        }
    }

    void bindReferenceShortcut(QAction *action)
    {
        m_referenceShortcut=action;
        syncReferenceShortcut();
    }

    void toggleReferenceVisibility()
    {
        if(auto *toggle=m_engine.action("view_toggle_reference_images")) {
            if(toggle->isEnabled())toggle->trigger();
        }
        syncReferenceShortcut();
    }

    void bindEyedropper(QAction *action)
    {
        m_eyedropper=action;
        m_sampleActiveLayer=QSettings().value(QStringLiteral("MobileStudio/eyedropperActiveLayer"),false).toBool();
        action->setCheckable(true);
        connect(action,&QAction::triggered,this,[this](bool armed) {
            armEyedropper(armed);
        });
        syncEyedropper();
    }

    void toggleEyedropperSource()
    {
        m_sampleActiveLayer=!m_sampleActiveLayer;
        QSettings().setValue(QStringLiteral("MobileStudio/eyedropperActiveLayer"),m_sampleActiveLayer);
        syncEyedropper();
    }

    void sampleEyedropperDrag(const QPoint &global,MobileToolbarDrag::Phase phase)
    {
        armEyedropper(false);
        sampleToolbarColor(global,phase,m_sampleActiveLayer);
    }

    void sampleToolbarColor(const QPoint &global,MobileToolbarDrag::Phase phase,bool layerOnly=false)
    {
        using MobileToolbarDrag::Phase;
        if(phase!=Phase::Preview) {
            m_samplePreviewTimer.stop();
            if(phase==Phase::Commit) {
                if(auto *provider=m_engine.brushResources())
                    if(const auto sample=toolbarColorAt(global,layerOnly))
                        provider->setFGColor(*sample);
            }
            clearToolbarPreview();
            return;
        }
        m_samplePoint=global;
        m_sampleLayerOnly=layerOnly;
        if(!m_samplePreviewTimer.isActive())m_samplePreviewTimer.start();
    }

    void openSettings()
    {
        if(!m_host)return;
        QTimer::singleShot(0,this,[this]() {
            if(!m_host)return;
            QDialog dialog(m_host);
            dialog.setWindowTitle(QObject::tr("Workspace Settings"));
            MobileTouchScroll::styleMenuDialog(dialog,QSize(500,600));
            auto *outer=new QVBoxLayout(&dialog);
            outer->setContentsMargins(12,10,12,10);
            auto *heading=new QLabel(tr("Workspace Settings"),&dialog);
            heading->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
            outer->addWidget(heading);
            auto *tabs=new QTabBar(&dialog);
            tabs->addTab(tr("Panels"));
            tabs->addTab(tr("Toolbar gestures"));
            outer->addWidget(tabs);
            auto *pages=new QStackedWidget(&dialog);
            outer->addWidget(pages,1);
            connect(tabs,&QTabBar::currentChanged,pages,&QStackedWidget::setCurrentIndex);
            const auto page=[pages]() {
                auto *scroll=new QScrollArea(pages);
                scroll->setWidgetResizable(true);
                scroll->setFrameShape(QFrame::NoFrame);
                MobileTouchScroll::configure(scroll);
                auto *body=new QWidget(scroll);
                auto *layout=new QVBoxLayout(body);
                layout->setContentsMargins(3,8,3,8);
                layout->setSpacing(7);
                scroll->setWidget(body);
                pages->addWidget(scroll);
                return body;
            };
            auto *body=page();
            auto *layout=qobject_cast<QVBoxLayout*>(body->layout());
            // Combine the two panel-retention checkboxes into a
            // single choice. All historical preference combinations
            // remain representable and use the same persisted keys.
            auto *persistentPanels=new MobileTouchComboBox(body);
            persistentPanels->addItem(QObject::tr("Neither"),0);
            persistentPanels->addItem(QObject::tr("Layers only"),1);
            persistentPanels->addItem(QObject::tr("Color Wheel only"),2);
            persistentPanels->addItem(QObject::tr("Layers + Color Wheel"),3);
            persistentPanels->setCurrentIndex(
                (m_keepLayers?1:0)|(m_keepColor?2:0));
            auto *floatingSliders=new MobileTouchComboBox(body);
            floatingSliders->addItem(QObject::tr("Hidden"),0);
            floatingSliders->addItem(QObject::tr("Size only"),1);
            floatingSliders->addItem(QObject::tr("Opacity only"),2);
            floatingSliders->addItem(QObject::tr("Size + Opacity"),3);
            floatingSliders->setCurrentIndex(
                (m_showSize?1:0)|(m_showOpacity?2:0));
            layout->addWidget(new QLabel(QObject::tr("Keep panels open"),body));
            layout->addWidget(persistentPanels);
            layout->addWidget(new QLabel(QObject::tr("Floating brush sliders"),body));
            layout->addWidget(floatingSliders);
            auto *fade=new QCheckBox(QObject::tr("Fade idle panels"),body);
            fade->setChecked(m_fadePanels);
            layout->addWidget(fade);
            auto *fadeDetails=new QWidget(body);
            auto *fadeForm=new QFormLayout(fadeDetails);
            fadeForm->setContentsMargins(0,0,0,0);
            const auto fadeSlider=[fadeForm,fadeDetails](const QString &label,int low,int high,
                                                         int value,const QString &suffix) {
                auto *slider=new QSlider(Qt::Horizontal,fadeDetails);
                slider->setRange(low,high);
                slider->setValue(value);
                slider->setMinimumHeight(42);
                slider->setAccessibleName(label);
                auto *caption=new QLabel(fadeDetails);
                const auto update=[caption,label,suffix](int current) {
                    caption->setText(QStringLiteral("%1  %2%3").arg(label).arg(current).arg(suffix));
                };
                update(value);
                connect(slider,&QSlider::valueChanged,caption,update);
                fadeForm->addRow(caption);
                fadeForm->addRow(slider);
                return slider;
            };
            auto *idleOpacity=fadeSlider(tr("Idle opacity"),15,90,m_idleOpacity,QStringLiteral("%"));
            auto *fadeDelay=fadeSlider(tr("Fade after"),1,30,m_fadeDelay,tr(" s"));
            fadeDetails->setVisible(fade->isChecked());
            connect(fade,&QCheckBox::toggled,fadeDetails,&QWidget::setVisible);
            layout->addWidget(fadeDetails);
            layout->addStretch(1);
            body=page();
            layout=qobject_cast<QVBoxLayout*>(body->layout());
            auto *opacityDrag=new MobileTouchComboBox(body);
            auto *sizeDrag=new MobileTouchComboBox(body);
            const auto fillDragChoices=[](MobileTouchComboBox *combo,
                                          const QString &selected) {
                combo->addItem(QObject::tr("Off"),QString());
                for(const auto &pair:QList<QPair<QString,QString>>{
                    {QStringLiteral("color"),QObject::tr("Color wheel button")},
                    {QStringLiteral("tool-settings"),QObject::tr("Brush / Tool Settings button")},
                    {QStringLiteral("paint"),QObject::tr("Paint button")},
                    {QStringLiteral("erase"),QObject::tr("Erase button")},
                    {QStringLiteral("blend"),QObject::tr("Blend button")},
                    {QStringLiteral("tools"),QObject::tr("Tools button")}})
                    combo->addItem(pair.second,pair.first);
                const int index=combo->findData(selected);
                combo->setCurrentIndex(index>=0?index:0);
            };
            fillDragChoices(opacityDrag,MobileToolbarDrag::assignment("opacity"));
            fillDragChoices(sizeDrag,MobileToolbarDrag::assignment("size"));
            for(auto *combo:{persistentPanels,floatingSliders,opacityDrag,sizeDrag})
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            // Visually resolve conflicts at selection time rather than
            // silently disabling one of the assignments on Save.
            connect(opacityDrag,QOverload<int>::of(&QComboBox::currentIndexChanged),
                    &dialog,[opacityDrag,sizeDrag](int) {
                if(opacityDrag->currentIndex()>0 &&
                   opacityDrag->currentData()==sizeDrag->currentData())
                    sizeDrag->setCurrentIndex(0);
            });
            connect(sizeDrag,QOverload<int>::of(&QComboBox::currentIndexChanged),
                    &dialog,[opacityDrag,sizeDrag](int) {
                if(sizeDrag->currentIndex()>0 &&
                   opacityDrag->currentData()==sizeDrag->currentData())
                    opacityDrag->setCurrentIndex(0);
            });
            layout->addWidget(new QLabel(QObject::tr(
                "Swipe along toolbar to adjust"),body));
            layout->addWidget(new QLabel(QObject::tr("Opacity button"),body));
            layout->addWidget(opacityDrag);
            layout->addWidget(new QLabel(QObject::tr("Brush size button"),body));
            layout->addWidget(sizeDrag);
            auto *sampleColor=new QCheckBox(tr("Drag Color to sample"),body);
            sampleColor->setChecked(QSettings().value(
                MobileToolbarDrag::preference("sample"),true).toBool());
            layout->addWidget(sampleColor);
            layout->addStretch(1);
            auto *done=new QPushButton(QObject::tr("Done"),&dialog);
            connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
            outer->addWidget(done);
            if(dialog.exec()!=QDialog::Accepted)return;
            const int panelMode=persistentPanels->currentData().toInt();
            m_keepLayers=bool(panelMode&1);
            m_keepColor=bool(panelMode&2);
            const int sliderMode=floatingSliders->currentData().toInt();
            const bool oldSize=m_showSize,oldOpacity=m_showOpacity;
            m_showSize=bool(sliderMode&1);
            m_showOpacity=bool(sliderMode&2);
            QSettings settings;
            settings.setValue(QStringLiteral("MobileStudio/keepLayersOpen"),m_keepLayers);
            settings.setValue(QStringLiteral("MobileStudio/keepColorOpen"),m_keepColor);
            if(oldSize!=m_showSize || oldOpacity!=m_showOpacity) {
                const QString prefix=m_workspaceId.isEmpty()?QStringLiteral("MobileStudio/"):
                    MobileWorkspaceProfiles::group(m_workspaceId);
                if(!m_workspaceId.isEmpty())
                    MobileWorkspaceProfiles::remember(settings,m_workspaceId,m_workspaceSize);
                settings.setValue(prefix+QStringLiteral("showSizeSlider"),m_showSize);
                settings.setValue(prefix+QStringLiteral("showOpacitySlider"),m_showOpacity);
            }
            m_fadePanels=fade->isChecked();
            m_idleOpacity=idleOpacity->value();
            m_fadeDelay=fadeDelay->value();
            settings.setValue(QStringLiteral("MobileStudio/fadePanels"),m_fadePanels);
            settings.setValue(QStringLiteral("MobileStudio/idlePanelOpacity"),m_idleOpacity);
            settings.setValue(QStringLiteral("MobileStudio/panelFadeDelay"),m_fadeDelay);
            if(m_fadePanels && QGuiApplication::applicationState()==Qt::ApplicationActive)
                m_fadeTimer->start();
            else m_fadeTimer->stop();
            for(auto *panel:fadeTargets())wakePanel(panel);
            QString selectedOpacity=opacityDrag->currentData().toString();
            QString selectedSize=sizeDrag->currentData().toString();
            // One button cannot control two independent parameters.
            if(!selectedOpacity.isEmpty() && selectedOpacity==selectedSize)
                selectedSize.clear();
            settings.setValue(MobileToolbarDrag::preference("opacity"),selectedOpacity);
            settings.setValue(MobileToolbarDrag::preference("size"),selectedSize);
            settings.setValue(MobileToolbarDrag::preference("sample"),
                              sampleColor->isChecked());
            updateBrushSliders();
        });
    }

    void refresh()
    {
        if(!m_host)return;
        syncPanelActions();
        syncBrushModes();
        syncReferenceShortcut();
        if(!m_engine.hasDocument()) {
            hidePanels();
            return;
        }
        syncSelectionDismissButton();
        syncCloneSourceButton();
        syncLayersVisibility();
        updateBrushSliders();
        syncContextToolBar();
        // Switching between native canvases changes the active tool's
        // KoCanvasController. Rebind the existing Tool Options dock to
        // that canvas immediately instead of leaving stale controls.
        if(m_toolOptions && m_toolOptions->isVisible())
            syncActiveToolOptions();
        if(m_brush && m_brush->isVisible())m_brush->refresh();
        if(m_opacityStrip && m_opacityStrip->isVisible())m_opacityStrip->refresh();
        if(m_brushOptions && m_brushOptions->isVisible())m_brushOptions->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshNow();
        if(m_color && m_color->isVisible())m_color->refresh();
    }

    void hidePanels()
    {
        armEyedropper(false);
        MobileToolBrowser::dismiss(m_host);
        ++m_brushRequest;
        if(m_brush)m_brush->dismissPresetBrowser();
        clearToolbarPreview();
        if(m_layers)m_layers->hide();
        if(m_showLayers)m_showLayers->hide();
        if(m_brush)m_brush->hide();
        if(m_opacityStrip)m_opacityStrip->hide();
        if(m_toolOptions)m_toolOptions->hide();
        if(m_brushOptions)m_brushOptions->hide();
        if(m_textFormatting)m_textFormatting->hide();
        if(m_color)m_color->hide();
        if(m_contextToolBar)m_contextToolBar->hide();
        if(m_selectionDismiss)m_selectionDismiss->hide();
        if(m_cloneSourceButton)m_cloneSourceButton->hide();
    }

    void setCanvasOnly(bool enabled)
    {
        m_canvasOnly=enabled;
        if(enabled)hidePanels();
        else {
            syncLayersVisibility();
            updateBrushSliders();
            syncContextToolBar();
            syncSelectionDismissButton();
        }
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if((watched==m_layers || watched==m_color || watched==m_toolOptions ||
            watched==m_brushOptions || watched==m_textFormatting || watched==m_contextToolBar) &&
           (event->type()==QEvent::Show || event->type()==QEvent::Hide)) {
            syncPanelActions();
            scheduleOverlayPlacement();
        }
        const auto type=event->type();
        if(handleEyedropperInput(watched,event))return true;
        if(!m_canvasLayoutEditing && m_host && (type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick ||
                      type==QEvent::TabletPress || type==QEvent::TouchBegin)) {
            auto *target=pressTarget(watched,event);
            dismissTemporaryPanels(target);
            if(m_fadePanels) {
                const int channel=type==QEvent::TouchBegin?0:type==QEvent::TabletPress?1:2;
                m_fadeHeld[channel]=panelAt(target);
                wakePanel(m_fadeHeld[channel]);
            }
        } else if(m_fadePanels) {
            if(type==QEvent::TouchEnd || type==QEvent::TouchCancel)endPanelInput(0);
            else if(type==QEvent::TabletRelease)endPanelInput(1);
            else if((type==QEvent::MouseButtonRelease && QApplication::mouseButtons()==Qt::NoButton) ||
                    type==QEvent::UngrabMouse)
                endPanelInput(2);
            else if(type==QEvent::Wheel)wakePanel(panelAt(qobject_cast<QWidget*>(watched)));
            else if(type==QEvent::Show) {
                auto *target=qobject_cast<QWidget*>(watched);
                if(target && panelAt(target)==target)wakePanel(target);
            } else if(type==QEvent::ApplicationDeactivate) {
                for(int i=0;i<3;++i)endPanelInput(i);
            }
        }
        if(m_host && watched==m_host->nativeCanvas() &&
           (event->type()==QEvent::Resize || event->type()==QEvent::Show ||
            (event->type()==QEvent::DynamicPropertyChange &&
             static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()==
                MobileNativeCommandLayout::overlayBoundsProperty()))) {
            scheduleOverlayPlacement();
        }
        if((watched==m_brush || watched==m_opacityStrip || watched==m_brushOptions) &&
           event->type()==QEvent::LayoutRequest)
            scheduleOverlayPlacement();
        return QObject::eventFilter(watched,event);
    }

private:
    QList<QPair<QString,QWidget*>> editablePanels() const
    {
        return {{QStringLiteral("layers"),m_layers.data()},
                {QStringLiteral("color"),m_color.data()},
                {QStringLiteral("size"),m_brush.data()},
                {QStringLiteral("opacity"),m_opacityStrip.data()},
                {QStringLiteral("tool-options"),m_toolOptions.data()},
                {QStringLiteral("brush-options"),m_brushOptions.data()},
                {QStringLiteral("text-options"),m_textFormatting.data()},
                {QStringLiteral("context"),m_contextToolBar.data()}};
    }

    bool hasEditedPlacement(const QWidget *panel) const
    {
        if(!panel || m_panelPlacements.isEmpty())return false;
        for(const auto &entry:editablePanels())
            if(panel==entry.second)return m_panelPlacements.value(entry.first).toRectF().isValid();
        return false;
    }

    QRect preferredPanel(const QWidget *panel,const QRect &bounds,const QRect &fallback) const
    {
        if(!hasEditedPlacement(panel))return fallback;
        for(const auto &entry:editablePanels())
            if(panel==entry.second)
                return MobileCanvasLayoutEditor::absolute(
                    m_panelPlacements.value(entry.first).toRectF(),bounds);
        return fallback;
    }

    QRect placePanel(QWidget *panel,const QRect &preferred,const QRect &bounds,
                     QList<QRect> &occupied,const QSize &minimum)
    {
        if(!panel || !panel->isVisible())return {};
        const QRect geometry=MobileOverlayPlacement::nearest(
            preferred,bounds,occupied,minimum.boundedTo(preferred.size()));
        panel->setGeometry(geometry);
        if(geometry.isValid())occupied.append(geometry);
        return geometry;
    }

    MobileToolbarDragPreview *toolbarPreview()
    {
        if(!m_toolbarPreview && m_host && m_host->nativeCanvas())
            m_toolbarPreview=new MobileToolbarDragPreview(m_host->nativeCanvas());
        return m_toolbarPreview;
    }

    void clearToolbarPreview()
    {
        m_samplePreviewTimer.stop();
        if(m_toolbarPreview)m_toolbarPreview->hide();
        m_dragParameter.clear();
    }

    void syncReferenceShortcut()
    {
        if(!m_referenceShortcut)return;
        const auto *manager=m_engine.manager();
        const auto *canvas=manager?manager->canvasBase():nullptr;
        const bool shown=canvas && canvas->referenceImagesDecoration() &&
                         canvas->referenceImagesDecoration()->visible();
        if(m_referenceState==int(shown))return;
        m_referenceState=int(shown);
        QPixmap icon(48,48);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setOpacity(shown?1.0:0.45);
        QIcon(QStringLiteral(":/light_krita_tool_reference_images.svg")).paint(&painter,QRect(0,0,48,48));
        painter.setOpacity(1);
        painter.setRenderHint(QPainter::Antialiasing);
        if(shown) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#83d9bd"));
            painter.drawEllipse(QPoint(40,40),6,6);
        } else {
            painter.setPen(QPen(QColor("#f4ab9e"),4,Qt::SolidLine,Qt::RoundCap));
            painter.drawLine(9,39,39,9);
        }
        painter.end();
        m_referenceShortcut->setIcon(QIcon(icon));
        m_referenceShortcut->setToolTip(shown?
            tr("Reference images shown. Hold to hide."):
            tr("Reference images hidden. Hold to show."));
    }

    void syncEyedropper()
    {
        if(!m_eyedropper)return;
        const auto source=m_sampleActiveLayer?tr("Active layer"):tr("Visible colors");
        m_eyedropper->setToolTip(tr("Eyedropper · %1. Tap to arm, drag to sample, hold to change source.").arg(source));
        QPixmap icon(48,48);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        QIcon(QStringLiteral(":/light_krita_tool_color_sampler.svg")).paint(&painter,QRect(0,0,39,39));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#28596c"));
        painter.drawRoundedRect(QRect(26,25,22,23),4,4);
        painter.setPen(Qt::white);
        QFont font=painter.font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(26,25,22,23),Qt::AlignCenter,
                         m_sampleActiveLayer?tr("L","eyedropper active layer badge"):
                                             tr("V","eyedropper visible colors badge"));
        painter.end();
        m_eyedropper->setIcon(QIcon(icon));
    }

    void armEyedropper(bool armed)
    {
        armed=armed && m_engine.hasDocument() && !m_canvasLayoutEditing;
        if(m_eyedropper)m_eyedropper->setChecked(armed);
        if(m_eyedropperArmed==armed)return;
        m_eyedropperArmed=armed;
        if(auto *manager=m_engine.manager()) {
            if(auto *canvas=manager->canvasBase())
                if(auto *widget=canvas->canvasWidget()) {
                    if(armed) {
                        m_eyedropperCanvas=widget;
                        m_eyedropperCursor=widget->cursor();
                        m_eyedropperHadCursor=widget->testAttribute(Qt::WA_SetCursor);
                        m_eyedropperMouseTracking=widget->hasMouseTracking();
                        widget->setMouseTracking(true);
                        widget->setCursor(Qt::CrossCursor);
                    }
                }
        }
        if(!armed)clearToolbarPreview();
        if(!armed && m_eyedropperCanvas) {
            if(m_eyedropperHadCursor)m_eyedropperCanvas->setCursor(m_eyedropperCursor);
            else m_eyedropperCanvas->unsetCursor();
            m_eyedropperCanvas->setMouseTracking(m_eyedropperMouseTracking);
            m_eyedropperCanvas.clear();
        }
    }

    bool handleEyedropperInput(QObject *watched,QEvent *event)
    {
        const auto type=event->type();
        if(type==QEvent::ApplicationDeactivate) {
            m_eyedropperInput=QEvent::None;
            armEyedropper(false);
            clearToolbarPreview();
            return false;
        }
        if(m_eyedropperArmed && type==QEvent::KeyPress &&
           static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
            armEyedropper(false);
            return true;
        }
        if(m_eyedropperInput!=QEvent::None) {
            const bool touch=m_eyedropperInput==QEvent::TouchBegin;
            const bool tablet=m_eyedropperInput==QEvent::TabletPress;
            const auto release=touch?QEvent::TouchEnd:tablet?QEvent::TabletRelease:QEvent::MouseButtonRelease;
            const auto move=touch?QEvent::TouchUpdate:tablet?QEvent::TabletMove:QEvent::MouseMove;
            if(type==release || (touch && type==QEvent::TouchCancel))m_eyedropperInput=QEvent::None;
            else if(type!=move) {
                const bool mouse=type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick ||
                                 type==QEvent::MouseButtonRelease || type==QEvent::MouseMove;
                if((!touch && !tablet) || !mouse ||
                   static_cast<QMouseEvent*>(event)->source()==Qt::MouseEventNotSynthesized)return false;
            }
            event->accept();
            return true;
        }
        if(!m_eyedropperArmed || !m_eyedropperCanvas ||
           QApplication::activeModalWidget() || QApplication::activePopupWidget())return false;
        const bool tabletHover=type==QEvent::TabletMove &&
            static_cast<QTabletEvent*>(event)->pressure()==0;
        const bool mouseHover=type==QEvent::MouseMove &&
            static_cast<QMouseEvent*>(event)->buttons()==Qt::NoButton;
        if(tabletHover || mouseHover) {
            QPoint global;
            auto *target=pressTarget(watched,event,&global);
            if(target && (target==m_eyedropperCanvas || m_eyedropperCanvas->isAncestorOf(target)))
                sampleToolbarColor(global,MobileToolbarDrag::Phase::Preview,m_sampleActiveLayer);
            else clearToolbarPreview();
            return false;
        }
        if(type==QEvent::TabletLeaveProximity ||
           (type==QEvent::Leave && watched==m_eyedropperCanvas))
            clearToolbarPreview();
        if(type!=QEvent::TouchBegin && type!=QEvent::TabletPress &&
           type!=QEvent::MouseButtonPress && type!=QEvent::MouseButtonDblClick)return false;
        QPoint global;
        auto *target=pressTarget(watched,event,&global);
        if(!target || (target!=m_eyedropperCanvas && !m_eyedropperCanvas->isAncestorOf(target)))return false;
        if(type==QEvent::TouchBegin) {
            const auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            if(touch->points().size()!=1)return false;
#else
            if(touch->touchPoints().size()!=1)return false;
#endif
        }
        if((type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick) &&
           static_cast<QMouseEvent*>(event)->button()!=Qt::LeftButton)return false;
        m_eyedropperInput=type==QEvent::MouseButtonDblClick?QEvent::MouseButtonPress:type;
        dismissTemporaryPanels(target);
        armEyedropper(false);
        sampleToolbarColor(global,MobileToolbarDrag::Phase::Commit,m_sampleActiveLayer);
        event->accept();
        return true;
    }

    std::optional<KoColor> toolbarColorAt(const QPoint &global,bool layerOnly) const
    {
        if(!m_engine.hasDocument())return {};
        auto *manager=m_engine.manager();
        auto *canvas=manager?manager->canvasBase():nullptr;
        if(!canvas || !canvas->canvasWidget())return {};
        const auto image=manager->image().toStrongRef();
        if(!image)return {};
        const QPoint widgetPoint=canvas->canvasWidget()->mapFromGlobal(global);
        if(!canvas->canvasWidget()->rect().contains(widgetPoint))return {};
        const QPoint point=canvas->coordinatesConverter()->widgetToImage(widgetPoint).toPoint();
        if(!layerOnly && canvas->referenceImagesDecoration() &&
           canvas->referenceImagesDecoration()->visible()) {
            const auto *document=manager->document();
            const auto refs=document?document->referenceImagesLayer():nullptr;
            if(refs) {
                const auto color=refs->getPixel(point);
                if(color.isValid())return KoColor(color,image->colorSpace());
            }
        }
        if(!image->bounds().contains(point))return {};
        const auto node=manager->activeNode();
        const auto device=layerOnly?(node?node->projection():KisPaintDeviceSP{}):image->projection();
        if(!device)return {};
        KoColor sampled;
        device->pixel(point.x(),point.y(),&sampled);
        return sampled;
    }

    void saveWorkspacePanels()
    {
        if(m_workspaceId.isEmpty())return;
        QSettings settings;
        MobileWorkspaceProfiles::remember(settings,m_workspaceId,m_workspaceSize);
        const QString prefix=MobileWorkspaceProfiles::group(m_workspaceId);
        settings.setValue(prefix+QStringLiteral("canvasPanelPlacements"),m_panelPlacements);
        settings.setValue(prefix+QStringLiteral("layersHorizontal"),m_layersHorizontal);
        settings.setValue(prefix+QStringLiteral("layersCollapsed"),m_layersCollapsed);
    }

    void syncLayersVisibility()
    {
        if(m_canvasLayoutEditing || !m_host)return;
        auto *canvas=m_host->nativeCanvas();
        if(!canvas)return;
        const bool available=!m_canvasOnly && m_engine.hasDocument();
        if(available && !m_layers) {
            m_layers=new KritaMobileLayersPanel(m_engine,canvas,true);
            m_layers->installEventFilter(this);
            m_layers->onContentHeightChanged=[this]() {placeLayerOverlay();};
            m_layers->onRequestClose=[this]() {toggleLayers();};
            m_showLayers=new QToolButton(canvas);
            m_showLayers->setAccessibleName(tr("Show Layers"));
            m_showLayers->setToolTip(tr("Show Layers"));
            m_showLayers->setIcon(m_layersAction?m_layersAction->icon():QIcon());
            if(m_showLayers->icon().isNull())m_showLayers->setText(tr("Layers"));
            m_showLayers->setFixedSize(44,44);
            m_showLayers->setStyleSheet(QStringLiteral(
                "QToolButton{background:#34343a;color:white;border:1px solid #66666d;border-radius:7px;}"));
            connect(m_showLayers,&QToolButton::clicked,this,[this]() {toggleLayers();});
        }
        if(m_layers)m_layers->setVisible(available && !m_layersCollapsed);
        if(m_showLayers)m_showLayers->setVisible(available && m_layersCollapsed);
        syncPanelActions();
    }

    static QString sliderLayoutKey(int index)
    {
        return index==0?QStringLiteral("sizeSlider"):QStringLiteral("opacitySlider");
    }

    std::array<QWidget*,4> fadeTargets() const
    {
        return {m_layers.data(),m_color.data(),m_brush.data(),m_opacityStrip.data()};
    }

    QWidget *panelAt(QWidget *target) const
    {
        if(!target)return nullptr;
        for(auto *panel:fadeTargets())
            if(panel && panel->isVisible() && (target==panel || panel->isAncestorOf(target)))
                return panel;
        return nullptr;
    }

    void wakePanel(QWidget *panel)
    {
        if(!panel)return;
        m_lastPanelInput[panel]=m_fadeClock.elapsed();
        if(auto *effect=qobject_cast<QGraphicsOpacityEffect*>(panel->graphicsEffect())) {
            if(auto *animation=effect->findChild<QPropertyAnimation*>())animation->stop();
            effect->setOpacity(1);
            if(!m_fadePanels)panel->setGraphicsEffect(nullptr);
        }
    }

    void endPanelInput(int channel)
    {
        wakePanel(m_fadeHeld[channel]);
        m_fadeHeld[channel].clear();
    }

    void updatePanelFades()
    {
        if(!m_fadePanels || QApplication::activeModalWidget() || QApplication::activePopupWidget())return;
        const auto targets=fadeTargets();
        const qint64 now=m_fadeClock.elapsed();
        for(int i=0;i<int(targets.size());++i) {
            auto *panel=targets[i];
            if(!panel || !panel->isVisible())continue;
            if((i==0 && !m_keepLayers) || (i==1 && !m_keepColor)) {
                wakePanel(panel);
                continue;
            }
            bool held=false;
            for(const auto &source:m_fadeHeld)held|=source==panel;
            if(held)continue;
            if(!m_lastPanelInput.contains(panel))m_lastPanelInput[panel]=now;
            if(now-m_lastPanelInput.value(panel)<m_fadeDelay*1000)continue;

            auto *effect=qobject_cast<QGraphicsOpacityEffect*>(panel->graphicsEffect());
            if(!effect) {
                effect=new QGraphicsOpacityEffect(panel);
                effect->setOpacity(1);
                panel->setGraphicsEffect(effect);
            }
            const qreal target=m_idleOpacity/100.0;
            auto *animation=effect->findChild<QPropertyAnimation*>();
            if(qFuzzyCompare(effect->opacity(),target) ||
               (animation && animation->state()==QAbstractAnimation::Running))continue;
            if(!animation)animation=new QPropertyAnimation(effect,"opacity",effect);
            animation->setDuration(180);
            animation->setStartValue(effect->opacity());
            animation->setEndValue(target);
            animation->start();
        }
    }

    void syncPanelActions()
    {
        const bool active=m_engine.hasDocument();
        const auto sync=[active](QAction *action,bool open) {
            if(!action)return;
            action->setEnabled(active);
            action->setChecked(open);
        };
        sync(m_layersAction,m_layers && m_layers->isVisible());
        sync(m_colorAction,m_color && m_color->isVisible());
        sync(m_optionsAction,(m_toolOptions && m_toolOptions->isVisible()) ||
                             (m_brushOptions && m_brushOptions->isVisible()));
    }

    static QWidget *pressTarget(QObject *watched,QEvent *event,QPoint *position=nullptr)
    {
        auto *fallback=qobject_cast<QWidget*>(watched);
        QPoint global;
        switch(event->type()) {
        case QEvent::TouchBegin: {
            const auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            if(touch->points().isEmpty())return fallback;
            global=touch->points().first().globalPosition().toPoint();
#else
            if(touch->touchPoints().isEmpty())return fallback;
            global=touch->touchPoints().first().screenPos().toPoint();
#endif
            break;
        }
        case QEvent::TabletPress:
        case QEvent::TabletMove:
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            global=static_cast<QTabletEvent*>(event)->globalPosition().toPoint();
#else
            global=static_cast<QTabletEvent*>(event)->globalPos();
#endif
            break;
        default:
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
#else
            global=static_cast<QMouseEvent*>(event)->globalPos();
#endif
            break;
        }
        if(position)*position=global;
        if(auto *hit=QApplication::widgetAt(global))return hit;
        return fallback;
    }

    void dismissTemporaryPanels(QWidget *target)
    {
        if(!target || target->window()!=m_host || QApplication::activePopupWidget() ||
           QApplication::activeModalWidget())return;
        auto *canvas=m_host->nativeCanvas();
        if(!canvas || (target!=canvas && !canvas->isAncestorOf(target)))return;
        auto *button=qobject_cast<QToolButton*>(target);
        QAction *action=button?button->defaultAction():nullptr;
        const auto dismiss=[target,action](QWidget *panel,QAction *opener) {
            if(panel && panel->isVisible() && panel!=target &&
               !panel->isAncestorOf(target) && action!=opener)panel->hide();
        };

        if(!m_keepLayers && m_layers && m_layers->isVisible() && target!=m_layers &&
           !m_layers->isAncestorOf(target) && action!=m_layersAction)toggleLayers();
        if(!m_keepColor)dismiss(m_color,m_colorAction);
        dismiss(m_brushOptions,m_optionsAction);
        dismiss(m_toolOptions,m_optionsAction);
        if(target!=m_textFormatOpener)dismiss(m_textFormatting,m_optionsAction);
    }

    bool ensureColorPanel()
    {
        if(m_color)return true;
        QWidget *canvas=m_host?m_host->nativeCanvas():nullptr;
        if(!canvas)return false;
        m_color=new MobileColorWorkspace(m_engine,canvas,true);
        m_color->installEventFilter(this);
        return true;
    }

    void syncBrushModes()
    {
        int active=-1;
        if(m_engine.hasDocument() &&
           m_engine.activeTool().section(QLatin1Char('/'),-1)==
               QStringLiteral("KisToolBrush")) {
            const auto *erase=m_engine.action("erase_action");
            active=erase && erase->isChecked()?1:
                m_brush && m_brush->isActiveMode(2)?2:0;
        }
        for(int slot=0;slot<m_brushModes.size();++slot)
            if(m_brushModes.at(slot))m_brushModes.at(slot)->setChecked(slot==active);
    }

    // A tiny always-available escape hatch from selections, independent
    // of the currently active tool. Preserve Krita's native Undo behavior.
    void syncSelectionDismissButton()
    {
        QWidget *canvas=m_host?m_host->nativeCanvas():nullptr;
        auto *view=m_engine.manager();
        const auto selection=view?view->selection():KisSelectionSP();
        const bool visible=canvas && !m_canvasOnly && m_engine.hasDocument() &&
                           selection && !selection->selectedRect().isEmpty();
        if(!visible) {
            if(m_selectionDismiss && m_selectionDismiss->isVisible()) {
                m_selectionDismiss->hide();
                scheduleOverlayPlacement();
            }
            return;
        }
        if(!m_selectionDismiss) {
            m_selectionDismiss=new QToolButton(canvas);
            m_selectionDismiss->setObjectName(QStringLiteral("mobileDeselectShortcut"));
            m_selectionDismiss->setText(tr("Deselect"));
            m_selectionDismiss->setAccessibleName(tr("Deselect current selection"));
            m_selectionDismiss->setToolTip(m_selectionDismiss->accessibleName());
            m_selectionDismiss->setFixedSize(85,36);
            m_selectionDismiss->setStyleSheet(QStringLiteral(
                "QToolButton#mobileDeselectShortcut{background:rgba(35,38,44,215);"
                "color:#f1f2f5;border:1px solid #818894;border-radius:9px;"
                "font-size:12px;padding:1px 4px;}"));
            connect(m_selectionDismiss,&QToolButton::clicked,this,[this]() {
                if(auto *action=m_engine.action("deselect");
                   action && action->isEnabled())action->trigger();
                QTimer::singleShot(0,this,[this]() {syncSelectionDismissButton();});
            });
        }
        if(!m_selectionDismiss->isVisible()) {
            m_selectionDismiss->show();
            scheduleOverlayPlacement();
        }
    }

    void syncCloneSourceButton()
    {
        auto *canvas=m_host?m_host->nativeCanvas():nullptr;
        const auto *provider=m_engine.hasDocument()?m_engine.brushResources():nullptr;
        const auto preset=provider?provider->currentPreset():KisPaintOpPresetSP{};
        const bool visible=canvas && !m_canvasOnly && preset && preset->valid() &&
            preset->paintOp().id()==QStringLiteral("duplicate") &&
            m_engine.activeTool().section(QLatin1Char('/'),-1)==QStringLiteral("KisToolBrush");
        if(visible && !m_cloneSourceButton) {
            m_cloneSourceButton=new QToolButton(canvas);
            m_cloneSourceButton->setObjectName(QStringLiteral("mobileCloneSourceShortcut"));
            m_cloneSourceButton->setText(tr("Set Clone Source"));
            m_cloneSourceButton->setToolTip(tr("Tap the canvas to choose a new clone source"));
            m_cloneSourceButton->setFixedSize(qMax(116,m_cloneSourceButton->sizeHint().width()+12),36);
            m_cloneSourceButton->setStyleSheet(QStringLiteral(
                "QToolButton#mobileCloneSourceShortcut{background:rgba(35,38,44,215);"
                "color:#f1f2f5;border:1px solid #818894;border-radius:9px;"
                "font-size:12px;padding:1px 4px;}"));
            connect(m_cloneSourceButton,&QToolButton::clicked,this,
                    &MobileKritaNativeWorkspace::pickCloneSource);
        }
        if(m_cloneSourceButton && visible==m_cloneSourceButton->isHidden()) {
            m_cloneSourceButton->setVisible(visible);
            scheduleOverlayPlacement();
        }
    }

    void syncContextToolBar()
    {
        if(m_canvasLayoutEditing)return;
        syncBrushModes();
        syncCloneSourceButton();
        if(m_layers)m_layers->syncSelectionContext();
        QWidget *canvas=m_host ? m_host->nativeCanvas() : nullptr;
        auto *tools=KoToolManager::instance();
        auto *controller=tools ? tools->activeCanvasController() : nullptr;
        const QString id=tools ? tools->activeToolId() : QString();
        if(m_textFormatting && m_textFormatting->isVisible()) {
            if(id.section(QLatin1Char('/'),-1)==QStringLiteral("SvgTextTool") &&
               controller && m_host &&
               m_host->nativeViewPort() &&
               m_host->nativeViewPort()->currentDocument())
                bindTextFormatting(controller->canvas());
            else
                m_textFormatting->hide();
        }
        auto *view=m_engine.manager();
        if(!canvas || !m_host->nativeViewPort() ||
           !m_host->nativeViewPort()->currentDocument() || m_canvasOnly ||
           !view || !controller || controller->canvas()!=view->canvasBase() ||
           id.isEmpty() || id.section(QLatin1Char('/'),-1)==
               QStringLiteral("KisToolBrush")) {
            if(m_contextToolBar && !m_contextToolBar->isHidden()) {
                m_contextToolBar->hide();
                placeDrawingOverlays();
            }
            return;
        }
        auto *tool=tools->toolById(controller->canvas(),id);
        if(!tool) {
            // A document or tool change can invalidate the native tool
            // before the next UI refresh. Never leave controls wired to
            // the preceding document/tool displayed over the canvas.
            if(m_contextToolBar && !m_contextToolBar->isHidden()) {
                m_contextToolBar->hide();
                placeDrawingOverlays();
            }
            return;
        }
        if(!m_contextToolBar) {
            m_contextToolBar=new MobileContextToolBar(canvas,m_optionsAction);
            m_contextToolOpener=m_contextToolBar->toolButton();
            m_contextToolBar->chooseTool=[this](QWidget *opener) {chooseTool(opener);};
            m_contextToolBar->chooseFillResource=[this](bool gradient,QWidget *anchor) {
                chooseFillResource(gradient,anchor);
            };
            if(KoDockRegistry::instance()->value(QStringLiteral("TextProperties")))
                m_contextToolBar->openTextFormatting=[this](QWidget *opener) {
                    m_textFormatOpener=opener;
                    toggleTextFormatting();
                };
        }
        QString label=id;
        for(const auto &entry:m_engine.tools()) {
            if(entry.id==id && !entry.label.isEmpty()) {
                label=entry.label;
                break;
            }
        }
        m_contextToolBar->setTool(id,label,tool->optionWidgets(),m_engine,tool);
        m_contextToolBar->show();
        placeDrawingOverlays();
        m_contextToolBar->raise();
    }

    void toggleTextFormatting()
    {
        if(!m_host || !m_host->nativeViewPort() || m_canvasOnly ||
           m_engine.activeTool().section(QLatin1Char('/'),-1)!=
               QStringLiteral("SvgTextTool") ||
           !m_host->nativeViewPort()->currentDocument())return;
        auto *canvas=m_host->nativeCanvas();
        auto *controller=KoToolManager::instance()->activeCanvasController();
        if(!canvas || !controller)return;
        if(m_textFormatting && m_textFormatting->isVisible()) {
            m_textFormatting->hide();
            return;
        }
        if(!m_textFormatting) {
            auto *factory=KoDockRegistry::instance()->value(QStringLiteral("TextProperties"));
            if(!factory)return;
            auto *panel=factory->createDockWidget();
            auto *observer=dynamic_cast<KisMainwindowObserver*>(panel);
            if(!observer) {
                delete panel;
                return;
            }
            if(!panel->property("_mobileTextFormattingReady").toBool()) {
                delete panel;
                QMessageBox::information(m_host,QObject::tr("Text Formatting"),
                    QObject::tr("The formatting panel could not load. Edit Text is still available."));
                return;
            }
            panel->setParent(canvas,Qt::Widget);
            auto *close=new QPushButton(QObject::tr("Close Text Formatting"),panel);
            close->setMinimumHeight(44);
            panel->setTitleBarWidget(close);
            connect(close,&QPushButton::clicked,panel,&QWidget::hide);
            observer->setViewManager(m_engine.manager());
            m_textFormatting=panel;
        }
        bindTextFormatting(controller->canvas());
        showOverlay(m_textFormatting);
    }

    void bindTextFormatting(KoCanvasBase *canvas)
    {
        if(auto *observer=dynamic_cast<KisMainwindowObserver*>(m_textFormatting.data())) {
            if(canvas && observer->observedCanvas()!=canvas)
                observer->setObservedCanvas(canvas);
        }
    }

    bool ensureBrushControls()
    {
        if(m_brush)return true;
        QWidget *canvas=m_host ? m_host->nativeCanvas() : nullptr;
        if(!canvas)return false;
        m_brush=new KritaMobileBrushWorkspace(m_engine,canvas,true);
        m_brush->installEventFilter(this);
        return true;
    }

    void updateBrushSliders()
    {
        if(m_canvasLayoutEditing)return;
        const bool available=!m_canvasOnly && m_engine.hasDocument();
        if(available && m_showSize && ensureBrushControls()) {
            m_brush->setSliderVisibility(true,false);
            m_brush->show();
            m_brush->raise();
        } else if(m_brush) {
            m_brush->hide();
        }
        if(available && m_showOpacity && m_host && m_host->nativeCanvas()) {
            if(!m_opacityStrip) {
                m_opacityStrip=new KritaMobileBrushWorkspace(m_engine,m_host->nativeCanvas(),true);
                m_opacityStrip->setSliderVisibility(false,true);
            }
            m_opacityStrip->show();
            m_opacityStrip->raise();
        } else if(m_opacityStrip) {
            m_opacityStrip->hide();
        }
        placeDrawingOverlays();
    }

    QRect placeSliders(QRect available,const QRect &bounds,QList<QRect> &occupied)
    {
        const std::array<QWidget*,2> panels{m_brush.data(),m_opacityStrip.data()};
        for(auto *panel:panels) {
            if(hasEditedPlacement(panel)) {
                auto geometry=preferredPanel(panel,bounds,{});
                const auto anchor=MobileCanvasLayoutEditor::relative(geometry,bounds);
                geometry.setHeight(qMin(40,bounds.height()));
                geometry.moveTop(bounds.top()+qRound(anchor.y()*(bounds.height()-geometry.height())));
                placePanel(panel,geometry,bounds,occupied,QSize(120,36));
            }
        }
        for(int band=0;band<2 && available.isValid();++band) {
            std::array<int,2> widths{0,0};
            int total=0;
            int height=0;
            int count=0;
            for(int i=0;i<2;++i) {
                if(!panels[i] || !panels[i]->isVisible() || hasEditedPlacement(panels[i]) ||
                   m_sliderCorners[i]/2!=band)continue;
                widths[i]=qMin(m_sliderWidths[i],available.width());
                height=qMax(height,qMin(40,panels[i]->sizeHint().height()));
                total+=widths[i];
                ++count;
            }
            if(!count)continue;
            height=qBound(0,height,qMax(0,available.height()));
            const int gap=count==2?2:0;
            const int capacity=qMax(0,available.width()-gap);
            if(total>capacity && count==2) {
                widths[0]=capacity*widths[0]/total;
                widths[1]=capacity-widths[0];
            }
            int left=available.left(),right=available.right()+1;
            const int y=band==0?available.top():available.bottom()-height+1;
            for(int i=0;i<2;++i) {
                if(!widths[i])continue;
                const bool atRight=m_sliderCorners[i]%2;
                const int x=atRight?right-widths[i]:left;
                placePanel(panels[i],QRect(x,y,widths[i],height),available,occupied,QSize(120,36));
                if(atRight)right=x-gap;
                else left=x+widths[i]+gap;
            }
            if(band==0)available.setTop(available.top()+height+2);
            else available.setBottom(available.bottom()-height-2);
        }
        return available;
    }

    void placeLayerOverlay()
    {
        if(m_layers && m_layers->isVisible())scheduleOverlayPlacement();
    }

    // Geometry updates emit Resize/LayoutRequest synchronously. Coalesce
    // repeated invalidations into one next-turn placement; never recurse
    // into the entire overlay solver while a pass is still in progress.
    void scheduleOverlayPlacement()
    {
        if(m_overlayPlacementQueued)return;
        m_overlayPlacementQueued=true;
        QTimer::singleShot(0,this,[this]() {
            m_overlayPlacementQueued=false;
            placeDrawingOverlays();
        });
    }

    void placeDrawingOverlays()
    {
        if(m_overlayPlacementInProgress) {
            scheduleOverlayPlacement();
            return;
        }
        const QScopedValueRollback<bool> placing(m_overlayPlacementInProgress,true);
        QWidget *canvas=m_host?m_host->nativeCanvas():nullptr;
        if(!canvas || m_canvasLayoutEditing)return;
        const QRect canvasBounds=canvas->rect().adjusted(2,2,-2,-2);
        if(!canvasBounds.isValid())return;
        QList<QRect> obstacles;
        for(auto *toolbar:canvas->findChildren<QFrame*>(QString(),Qt::FindDirectChildrenOnly))
            if(toolbar->isVisible() && toolbar->property("mobileCommandToolbar").toBool())
                obstacles.append(toolbar->geometry());
        QRect viewport=MobileNativeCommandLayout::overlayBounds(canvas);
        if(!viewport.isValid())viewport=canvasBounds;
        QRect layerAnchor,railReservation;
        int layerLength=0;
        bool layerReverse=false;
        const bool editedLayers=hasEditedPlacement(m_layers);
        const QRect railBounds=editedLayers?canvasBounds:viewport;
        const bool expanded=m_layers && m_layers->isVisible();
        const bool collapsed=m_showLayers && m_showLayers->isVisible();
        if((expanded || collapsed) && railBounds.isValid()) {
            if(editedLayers) {
                const QRectF saved=m_panelPlacements.value(QStringLiteral("layers")).toRectF();
                layerAnchor=MobileCanvasLayoutEditor::absolute(saved,railBounds);
                if(m_layersHorizontal)layerAnchor.setHeight(qBound(58,layerAnchor.height(),138));
                else layerAnchor.setWidth(qBound(64,layerAnchor.width(),138));
                layerReverse=m_layersHorizontal?saved.x()>0.75:saved.y()>0.75;
            } else if(m_layersHorizontal) {
                layerAnchor=QRect(railBounds.topLeft(),QSize(railBounds.width(),64));
            } else {
                const bool right=!m_legacyLayerCorner || m_layerCorner%2;
                layerReverse=m_legacyLayerCorner && m_layerCorner>=2;
                const int width=qMin(m_layerWidth,railBounds.width());
                layerAnchor=QRect(right?railBounds.right()-width+1:railBounds.left(),
                                  railBounds.top(),width,railBounds.height());
            }
            const int thickness=m_layersHorizontal?layerAnchor.height():layerAnchor.width();
            layerLength=m_layers->contentLength(m_layersHorizontal,thickness);
            if(m_legacyLayerCorner && !editedLayers && !m_layersHorizontal)
                layerLength=qMin(layerLength,railBounds.height()*m_layerHeight/100);
            if(collapsed) {
                QRect preferred(layerAnchor.topLeft(),QSize(44,44));
                if(layerReverse) {
                    if(m_layersHorizontal)preferred.moveRight(layerAnchor.right());
                    else preferred.moveBottom(layerAnchor.bottom());
                } else if(!m_layersHorizontal && layerAnchor.center().x()>railBounds.center().x()) {
                    preferred.moveRight(layerAnchor.right());
                }
                placePanel(m_showLayers,preferred,railBounds,obstacles,QSize(44,44));
                layerAnchor={};
            } else {
                const auto initial=MobileLayerRailGeometry::expand(
                    layerAnchor,railBounds,qMin(layerLength,128),m_layersHorizontal,layerReverse,obstacles);
                if(initial.isValid()) {
                    if(!editedLayers && m_layersHorizontal)viewport.setTop(initial.bottom()+3);
                    else {railReservation=initial;obstacles.append(initial);}
                }
            }
        }
        const bool customContext=hasEditedPlacement(m_contextToolBar);
        // Mobile tool settings are most reachable as a bottom dock.
        // Keep manually positioned bars untouched, and fit the default
        // above Android/system/toolbars using the native overlay bounds.
        const int contextHeight=48;
        const auto context=placePanel(m_contextToolBar,
            preferredPanel(m_contextToolBar,canvasBounds,
                QRect(viewport.left(),viewport.bottom()-contextHeight+1,
                      qMin(760,viewport.width()),contextHeight)),
            customContext?canvasBounds:viewport,obstacles,QSize(120,40));
        if(!customContext && context.isValid() &&
           context.bottom()==viewport.bottom())
            viewport.setBottom(context.top()-3);
        if(!viewport.isValid())viewport=canvasBounds;
        const QRect wheelBounds=viewport;
        viewport=placeSliders(viewport,canvasBounds,obstacles);
        if(!viewport.isValid())viewport=wheelBounds;

        QRect besideLayers=viewport;
        if(layerAnchor.isValid() && !m_layersHorizontal) {
            if(layerAnchor.center().x()>viewport.center().x())besideLayers.setRight(layerAnchor.left()-2);
            else besideLayers.setLeft(layerAnchor.right()+2);
        }
        if(m_toolOptions || m_textFormatting || m_brushOptions) {
            const QRect room=besideLayers.width()>=240?besideLayers:viewport;
            const int optionsWidth=qMin(340,room.width());
            const QRect bounds(room.right()-optionsWidth+1,room.top(),
                               optionsWidth,qMin(440,room.height()));
            const auto placeOptions=[&](QWidget *panel,const QRect &fallback) {
                placePanel(panel,preferredPanel(panel,canvasBounds,fallback),
                           hasEditedPlacement(panel)?canvasBounds:viewport,
                           obstacles,QSize(170,80));
            };
            placeOptions(m_toolOptions,bounds);
            if(m_brushOptions)
                placeOptions(m_brushOptions,QRect(bounds.x(),bounds.y(),bounds.width(),
                    qMin(room.height(),m_brushOptions->sizeHint().height())));
            placeOptions(m_textFormatting,bounds);
        }
        if(m_color && m_color->isVisible()) {
            auto wheelObstacles=obstacles;
            if(layerAnchor.isValid())
                wheelObstacles.append(MobileLayerRailGeometry::expand(
                    layerAnchor,railBounds,qMin(layerLength,115),m_layersHorizontal,layerReverse,obstacles));
            if(hasEditedPlacement(m_color)) {
                const QRect preferred=preferredPanel(m_color,canvasBounds,{});
                const QRect geometry=MobileOverlayPlacement::nearest(
                    preferred,canvasBounds,wheelObstacles,QSize(96,96),true);
                if(geometry.isValid())m_color->setManualWheelPlacement(geometry);
                else m_color->clearWheelGeometry();
            } else {
                m_color->setManualWheelPlacement({});
                m_color->setWheelArea(wheelBounds,wheelObstacles);
            }
        }
        if(layerAnchor.isValid()) {
            if(railReservation.isValid())obstacles.removeOne(railReservation);
            if(m_color && m_color->isVisible())obstacles.append(m_color->geometry());
            m_layers->setRailGeometry(MobileLayerRailGeometry::expand(
                layerAnchor,railBounds,layerLength,m_layersHorizontal,layerReverse,obstacles),
                m_layersHorizontal,m_layersHorizontal?
                    layerAnchor.center().y()<canvasBounds.center().y():
                    layerAnchor.center().x()<canvasBounds.center().x());
        }
        if(m_layers && m_layers->isVisible())obstacles.append(m_layers->geometry());
        if(m_color && m_color->isVisible())obstacles.append(m_color->geometry());
        for(auto *button:{m_selectionDismiss.data(),m_cloneSourceButton.data()}) {
            if(!button || !button->isVisible())continue;
            const QSize size=button->size();
            const QRect preferred(QPoint(canvasBounds.right()-size.width()-3,canvasBounds.top()+5),size);
            placePanel(button,preferred,canvasBounds,obstacles,size);
        }

    }

    void raiseOverlays()
    {
        if(m_layers && m_layers->isVisible())m_layers->raise();
        if(m_showLayers && m_showLayers->isVisible())m_showLayers->raise();
        if(m_selectionDismiss && m_selectionDismiss->isVisible())m_selectionDismiss->raise();
        if(m_cloneSourceButton && m_cloneSourceButton->isVisible())m_cloneSourceButton->raise();
        for(QWidget *panel:{static_cast<QWidget*>(m_brush.data()),
                            static_cast<QWidget*>(m_opacityStrip.data()),
                            static_cast<QWidget*>(m_color.data()),
                            static_cast<QWidget*>(m_toolOptions.data()),
                            static_cast<QWidget*>(m_brushOptions.data()),
                            static_cast<QWidget*>(m_textFormatting.data())}) {
            if(panel && panel->isVisible())panel->raise();
        }
    }

    void showOverlay(QWidget *panel)
    {
        if(m_toolOptions && m_toolOptions.data()!=panel)m_toolOptions->hide();
        if(m_brushOptions && m_brushOptions.data()!=panel)m_brushOptions->hide();
        if(m_textFormatting && m_textFormatting.data()!=panel)m_textFormatting->hide();
        panel->show();
        placeDrawingOverlays();
        raiseOverlays();
    }

    void syncActiveToolOptions()
    {
        if(!m_toolOptions || !m_toolOptions->isVisible())return;
        auto *tools=KoToolManager::instance();
        if(!tools)return;
        auto *controller=tools->activeCanvasController();
        if(!controller)return;
        if(KoToolBase *tool=tools->toolById(
               controller->canvas(),tools->activeToolId())) {
            m_toolOptions->setOptionWidgets(tool->optionWidgets());
        }
    }
    static void checkpoint(const char *stage)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/nativeWorkspaceLastAction"),
                          QString::fromLatin1(stage));
        settings.sync();
    }
    QPointer<MobileLightweightCanvasHost> m_host;
    MobileEngineBridge m_engine;
    QPointer<KoToolDocker> m_toolOptions;
    QPointer<QDockWidget> m_textFormatting;
    QPointer<KritaMobileBrushWorkspace> m_brush,m_opacityStrip,m_brushOptions;
    std::array<int,2> m_sliderCorners{2,2},m_sliderWidths{168,168};
    int m_layerCorner=0,m_layerWidth=82,m_layerHeight=100;
    QString m_workspaceId;
    QSize m_workspaceSize;
    bool m_layoutPanelsDirty=false;
    bool m_layersHorizontal=false,m_layersCollapsed=false,m_legacyLayerCorner=false;
    QPointer<QToolButton> m_showLayers;
    bool m_fadePanels=false;
    int m_idleOpacity=35,m_fadeDelay=3;
    QElapsedTimer m_fadeClock;
    QTimer *m_fadeTimer=nullptr;
    QHash<QWidget*,qint64> m_lastPanelInput;
    std::array<QPointer<QWidget>,3> m_fadeHeld;
    QPointer<QAction> m_layersAction,m_colorAction,m_optionsAction,m_toolsAction;
    QPointer<QWidget> m_contextToolOpener,m_textFormatOpener;
    QList<QAction*> m_brushModes;
    quint64 m_brushRequest=0;
    bool m_switchingBrush=false;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
    QPointer<MobileContextToolBar> m_contextToolBar;
    QPointer<QToolButton> m_selectionDismiss,m_cloneSourceButton;
    QPointer<QAction> m_volumeUndo;
    QPointer<QAction> m_volumeRedo;
    QPointer<MobileToolbarDragPreview> m_toolbarPreview;
    QPointer<QAction> m_eyedropper,m_referenceShortcut;
    int m_referenceState=-1;
    QPointer<QWidget> m_eyedropperCanvas;
    QCursor m_eyedropperCursor;
    QEvent::Type m_eyedropperInput=QEvent::None;
    bool m_eyedropperArmed=false,m_eyedropperHadCursor=false,m_eyedropperMouseTracking=false;
    bool m_sampleActiveLayer=false,m_sampleLayerOnly=false;
    QTimer m_samplePreviewTimer;
    QPoint m_samplePoint;
    QString m_dragParameter;
    qreal m_dragBaseline=0,m_dragMaximum=1;
    bool m_keepLayers=true,m_keepColor=true,m_showSize=true,m_showOpacity=true;
    bool m_canvasOnly=false,m_canvasLayoutEditing=false;
    bool m_overlayPlacementQueued=false,m_overlayPlacementInProgress=false;
    QVariantMap m_panelPlacements;
};
#endif
