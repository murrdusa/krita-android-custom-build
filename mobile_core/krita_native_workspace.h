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
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QSignalBlocker>
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
#include <QTouchEvent>
#include <QTabWidget>
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

#include "KisDocument.h"
#include "input/KisTouchGestureAction.h"
#include "input/kis_input_manager.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_shortcut_configuration.h"
#include "engine_bridge.h"
#include "brush_workspace.h"
#include "layer_workspace.h"
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
    struct ToolbarShortcut {
        QString id;
        QPointer<QAction> action;
        QIcon icon;
    };

    explicit MobileKritaNativeWorkspace(
        MobileLightweightCanvasHost *host,
        const MobileHostServices &services,KisViewManager *manager)
        : QObject(host),m_host(host),m_engine(services,manager)
    {
        setObjectName(QStringLiteral("_mobileNativeEngineWorkspaces"));
        const QSettings settings;
        m_keepLayers=settings.value(QStringLiteral("MobileStudio/keepLayersOpen"),true).toBool();
        m_keepColor=settings.value(QStringLiteral("MobileStudio/keepColorOpen"),true).toBool();
        const bool oldSliders=settings.value(QStringLiteral("MobileStudio/showBrushSliders"),true).toBool();
        m_showSize=settings.value(QStringLiteral("MobileStudio/showSizeSlider"),oldSliders).toBool();
        m_showOpacity=settings.value(QStringLiteral("MobileStudio/showOpacitySlider"),oldSliders).toBool();
        m_layerCorner=qBound(0,settings.value(QStringLiteral("MobileStudio/layerCorner"),0).toInt(),3);
        m_layerWidth=qBound(82,settings.value(QStringLiteral("MobileStudio/layerWidth"),82).toInt(),120);
        m_layerHeight=qBound(25,settings.value(QStringLiteral("MobileStudio/layerHeight"),100).toInt(),100);
        for(int i=0;i<2;++i) {
            const QString key=sliderLayoutKey(i);
            m_sliderCorners[i]=qBound(0,settings.value(key+QStringLiteral("Corner"),2).toInt(),3);
            m_sliderWidths[i]=qBound(120,settings.value(key+QStringLiteral("Width"),168).toInt(),360);
        }
        m_fadePanels=settings.value(QStringLiteral("MobileStudio/fadePanels"),false).toBool();
        m_idleOpacity=qBound(15,settings.value(QStringLiteral("MobileStudio/idlePanelOpacity"),35).toInt(),90);
        m_fadeDelay=qBound(1,settings.value(QStringLiteral("MobileStudio/panelFadeDelay"),3).toInt(),30);
        m_fadeClock.start();
        m_fadeTimer=new QTimer(this);
        m_fadeTimer->setInterval(160);
        connect(m_fadeTimer,&QTimer::timeout,this,[this]() {updatePanelFades();});
        if(m_fadePanels)m_fadeTimer->start();
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
                             [this](int) {
                QTimer::singleShot(0,this,[this]() {
                    refresh();
                    placeDrawingOverlays();
                    raiseOverlays();
                });
            });
        }
        auto *poll=new QTimer(this);
        poll->setInterval(600);
        QObject::connect(poll,&QTimer::timeout,this,[this]() {
            if(m_layers && m_layers->isVisible())
                m_layers->refreshFromEngine();
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
        if(!ensureBrushControls())return;
        if(!m_brush->isActiveMode(slot)) {
            m_brush->activateMode(slot);
            syncBrushModes();
            return;
        }
        syncBrushModes();
        QTimer::singleShot(0,this,[this,slot]() {
            if(m_brush && m_engine.hasDocument() && m_brush->isActiveMode(slot))
                m_brush->browsePresets(slot);
            syncBrushModes();
        });
    }

    void pickCloneSource()
    {
        if(!m_engine.hasDocument())return;
        QTimer::singleShot(0,this,[this]() {
            auto *view=m_engine.manager();
            if(!view || !m_engine.hasDocument() || !ensureBrushControls())return;
            if(!m_brush->activateCloneBrush()) {
                view->showFloatingMessage(tr("No Clone Brush preset is available."),QIcon());
                return;
            }
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
        });
    }

    void bindPanelActions(QAction *layers,QAction *color,QAction *options)
    {
        m_layersAction=layers;
        m_colorAction=color;
        m_optionsAction=options;
        for(auto *action:{layers,color,options})action->setCheckable(true);
        syncPanelActions();
    }

    void toggleToolSettings()
    {
        if(!m_host || !m_engine.hasDocument() || m_canvasOnly)return;
        if(m_engine.activeTool().section(QLatin1Char('/'),-1)!=QStringLiteral("KisToolBrush")) {
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

    void toggleLayers()
    {
        if(!m_host || !m_engine.hasDocument())return;
        QWidget *canvas=m_host->nativeCanvas();
        if(!canvas)return;
        if(!m_layers) {
            checkpoint("Layers: creating canvas overlay");
            // Docking the layer rail resized Krita's actual viewport.
            // A clipped child overlay leaves the native canvas geometry
            // unchanged, just like the existing floating color panel.
            m_layers=new KritaMobileLayersPanel(m_engine,canvas,true);
            // Keep the panel's own object name: its stylesheet uses
            // QFrame#MobileStudioLayersPanel for the dark floating rail.
            m_layers->setFixedWidth(KritaMobileLayersPanel::compactRailWidth());
            m_layers->installEventFilter(this);
            m_layers->onContentHeightChanged=[this]() { placeLayerOverlay(); };
            m_layers->show();
            checkpoint("Layers: floating overlay ready");
        }
        else m_layers->setVisible(!m_layers->isVisible());
        placeDrawingOverlays();
        if(m_layers && m_layers->isVisible()) {
            m_layers->refreshNow();
            raiseOverlays();
        }
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

        showOverlay(m_toolOptions);
        syncActiveToolOptions();
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

    void chooseTool()
    {
        if(!m_host || !m_engine.hasDocument())return;
        QPointer<MobileLightweightCanvasHost> host=m_host;
        QTimer::singleShot(0,this,[this,host]() {
            if(host) {
                checkpoint("Tool chooser: opening");
                if(MobileToolBrowser::open(m_engine,host,true) && host)
                    toggleToolSettings();
                checkpoint("Tool chooser: closed");
            }
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

    void chooseFillResource(bool gradient)
    {
        if(!m_host || !m_engine.hasDocument())return;
        QTimer::singleShot(0,this,[this,gradient]() {
            if(!m_host || !m_engine.hasDocument())return;
            auto *manager=m_engine.manager();
            QPointer<KisCanvasResourceProvider> provider=m_engine.brushResources();
            QPointer<KisDocument> document=manager?manager->document():nullptr;
            if(!provider || !document)return;

            QDialog dialog(m_host);
            dialog.setWindowTitle(gradient?tr("Gradients"):tr("Patterns"));
            dialog.setStyleSheet(QStringLiteral(
                "QDialogButtonBox QPushButton{min-height:44px;padding:0 14px;}"
                "QTabBar::tab{min-height:36px;padding:0 10px;}"));
            auto *layout=new QVBoxLayout(&dialog);
            std::function<KoResourceSP()> selected;
            KoResourceSP customPattern;
            if(gradient) {
                auto *chooser=new KisGradientChooser(&dialog,nullptr,false);
                chooser->setCanvasResourcesInterface(
                    provider->resourceManager()->canvasResourcesInterface());
                chooser->setItemSize(KisGradientChooser::ItemSize_Large);
                chooser->setCurrentResource(provider->currentGradient());
                layout->addWidget(chooser,1);
                selected=[chooser]() {return chooser->currentResource();};
            } else {
                auto *tabs=new QTabWidget(&dialog);
                auto *chooser=new KisPatternChooser(tabs);
                chooser->setPreviewOrientation(Qt::Vertical);
                if(auto pattern=provider->currentPattern())
                    chooser->setCurrentPattern(pattern);
                tabs->addTab(chooser,tr("Patterns"));
                auto *custom=new KisCustomPattern(tabs,"mobileCanvasPattern",
                                                  tr("From Canvas"),manager);
                tabs->addTab(custom,tr("From Canvas"));
                connect(custom,&KisCustomPattern::activatedResource,&dialog,
                        [&dialog,&customPattern](KoResourceSP pattern) {
                    if(!pattern)return;
                    customPattern=pattern;
                    dialog.accept();
                });
                connect(custom,&KisCustomPattern::patternAdded,
                        chooser,&KisPatternChooser::setCurrentPattern);
                selected=[chooser,tabs]() {
                    return tabs->currentIndex()==0?chooser->currentResource():KoResourceSP{};
                };
                layout->addWidget(tabs,1);
            }
            auto *buttons=new QDialogButtonBox(
                QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&dialog);
            auto *apply=buttons->button(QDialogButtonBox::Apply);
            connect(apply,&QPushButton::clicked,&dialog,[&]() {
                if(selected())dialog.accept();
            });
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            layout->addWidget(buttons);
            const QSize size=m_host->size();
            dialog.resize(qMin(640,size.width()),qMin(680,size.height()));
            if(dialog.exec()!=QDialog::Accepted || !provider || !document ||
               !m_engine.manager() || m_engine.manager()->document()!=document)return;
            const auto resource=customPattern?customPattern:selected();
            if(!resource)return;
            if(gradient)provider->slotGradientActivated(resource);
            else provider->slotPatternActivated(resource);
        });
    }

    void configureToolbarShortcut(QAction *slot,const QVector<ToolbarShortcut> &choices)
    {
        m_shortcutSlot=slot;
        for(const auto &choice:choices) {
            if(!choice.action)continue;
            m_shortcuts.append(choice);
            connect(choice.action,&QAction::changed,this,
                    [this]() {syncToolbarShortcut();});
        }
        if(m_shortcuts.isEmpty())return;
        const QString saved=QSettings().value(
            QStringLiteral("MobileStudio/toolbarShortcut"),QStringLiteral("fit")).toString();
        for(int i=0;i<m_shortcuts.size();++i)
            if(m_shortcuts.at(i).id==saved)m_shortcutIndex=i;
        connect(slot,&QAction::triggered,this,[this]() {
            const auto target=m_shortcuts.at(m_shortcutIndex).action;
            if(target && target->isEnabled())target->trigger();
        });
        syncToolbarShortcut();
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
        dialog.resize(qMin(440,qMax(1,m_host->width()-24)),
                      qMin(620,qMax(1,m_host->height()-48)));
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
            combo->addItem(QObject::tr("Krita profile default"));
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
            // Do not demand a width greater than the folded phone viewport.
            dialog.setMinimumWidth(qMin(300,qMax(1,host->width()-24)));
            dialog.setStyleSheet(QStringLiteral(
                "QDialog,QScrollArea,QWidget#mobileInputSettingsBody{"
                "background:#252529;color:#f4f4f5;}"
                "QLabel,QCheckBox{color:#f4f4f5;}"
                "QCheckBox{min-height:44px;spacing:9px;}"
                "QCheckBox::indicator{width:22px;height:22px;}"
                "QComboBox,QSpinBox{color:#172530;background:#eef3f7;"
                "border:1px solid #8f9ca6;border-radius:6px;"
                "min-height:42px;padding:3px;}"
                "QComboBox QAbstractItemView{background:#eef3f7;color:#172530;}"
                "QPushButton{background:#34343a;color:#f4f4f5;"
                "border:1px solid #55555c;border-radius:8px;min-height:44px;}"));
            auto *outer=new QVBoxLayout(&dialog);
            outer->setContentsMargins(6,6,6,6);
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

            auto *doubleTap=new QSpinBox(body);
            doubleTap->setRange(120,600);
            doubleTap->setSuffix(QObject::tr(" ms"));
            doubleTap->setValue(qBound(120,settings.value(
                QStringLiteral("TabletUi/doubleTapMs"),150).toInt(),600));
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
                QStringLiteral("TabletUi/volumeLongMs"),150).toInt(),1000));
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

    void openSettings()
    {
        if(!m_host)return;
        QTimer::singleShot(0,this,[this]() {
            if(!m_host)return;
            QDialog dialog(m_host);
            dialog.setWindowTitle(QObject::tr("Workspace Settings"));
            dialog.setStyleSheet(QStringLiteral(
                "QDialog{background:#252529;color:#f4f4f5;}"
                "QLabel,QCheckBox,QComboBox{color:#f4f4f5;}"
                "QComboBox{background:#34343a;border:1px solid #55555c;"
                "border-radius:8px;padding:4px;min-height:44px;}"
                "QComboBox QAbstractItemView{background:#34343a;color:#f4f4f5;}"
                "QCheckBox{min-height:44px;}"
                "QCheckBox::indicator{width:22px;height:22px;}"
                "QPushButton{background:#34343a;color:#f4f4f5;"
                "border:1px solid #55555c;border-radius:8px;min-height:44px;}"));
            auto *outer=new QVBoxLayout(&dialog);
            auto *scroll=new QScrollArea(&dialog);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            MobileTouchScroll::configure(scroll);
            auto *body=new QWidget(scroll);
            auto *layout=new QVBoxLayout(body);
            scroll->setWidget(body);
            outer->addWidget(scroll,1);
            dialog.resize(qMin(400,m_host->width()),qMin(380,m_host->height()));
            auto *description=new QLabel(QObject::tr(
                "Keep these panels open while working elsewhere"),body);
            description->setWordWrap(true);
            layout->addWidget(description);
            auto *layers=new QCheckBox(QObject::tr("Layers"),body);
            auto *color=new QCheckBox(QObject::tr("Color wheel"),body);
            auto *size=new QCheckBox(QObject::tr("Show Size slider"),body);
            auto *opacity=new QCheckBox(QObject::tr("Show Opacity slider"),body);
            layers->setChecked(m_keepLayers);
            color->setChecked(m_keepColor);
            size->setChecked(m_showSize);
            opacity->setChecked(m_showOpacity);
            layout->addWidget(layers);
            layout->addWidget(color);
            layout->addWidget(size);
            layout->addWidget(opacity);
            auto *panelLayout=new QPushButton(QObject::tr("Panel placement and fading…"),body);
            layout->addWidget(panelLayout);
            connect(panelLayout,&QPushButton::clicked,&dialog,[this,&dialog]() {
                openPanelLayout(&dialog);
            });
            auto *shortcut=new MobileTouchComboBox(body);
            if(!m_shortcuts.isEmpty()) {
                layout->addWidget(new QLabel(QObject::tr("Toolbar shortcut"),body));
                for(const auto &choice:m_shortcuts)
                    shortcut->addItem(choice.icon,choice.action->text());
                shortcut->setCurrentIndex(m_shortcutIndex);
                layout->addWidget(shortcut);
            } else {
                shortcut->hide();
            }
            layout->addStretch(1);
            auto *done=new QPushButton(QObject::tr("Done"),&dialog);
            connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
            outer->addWidget(done);
            if(dialog.exec()!=QDialog::Accepted)return;
            m_keepLayers=layers->isChecked();
            m_keepColor=color->isChecked();
            m_showSize=size->isChecked();
            m_showOpacity=opacity->isChecked();
            QSettings settings;
            settings.setValue(QStringLiteral("MobileStudio/keepLayersOpen"),m_keepLayers);
            settings.setValue(QStringLiteral("MobileStudio/keepColorOpen"),m_keepColor);
            settings.setValue(QStringLiteral("MobileStudio/showSizeSlider"),m_showSize);
            settings.setValue(QStringLiteral("MobileStudio/showOpacitySlider"),m_showOpacity);
            if(shortcut->currentIndex()>=0) {
                m_shortcutIndex=shortcut->currentIndex();
                settings.setValue(QStringLiteral("MobileStudio/toolbarShortcut"),
                                  m_shortcuts.at(m_shortcutIndex).id);
                syncToolbarShortcut();
            }
            updateBrushSliders();
        });
    }

    void refresh()
    {
        if(!m_host)return;
        syncPanelActions();
        if(!m_engine.hasDocument()) {
            syncBrushModes();
            hidePanels();
            return;
        }
        updateBrushSliders();
        syncContextToolBar();
        // Switching between native canvases changes the active tool's
        // KoCanvasController. Rebind the existing Tool Options dock to
        // that canvas immediately instead of leaving stale controls.
        if(m_toolOptions && m_toolOptions->isVisible())
            syncActiveToolOptions();
        if(m_brush && m_brush->isVisible())m_brush->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshNow();
        if(m_color && m_color->isVisible())m_color->refresh();
    }

    void hidePanels()
    {
        if(m_layers)m_layers->hide();
        if(m_brush)m_brush->hide();
        if(m_opacityStrip)m_opacityStrip->hide();
        if(m_toolOptions)m_toolOptions->hide();
        if(m_brushOptions)m_brushOptions->hide();
        if(m_textFormatting)m_textFormatting->hide();
        if(m_color)m_color->hide();
        if(m_contextToolBar)m_contextToolBar->hide();
    }

    void setCanvasOnly(bool enabled)
    {
        m_canvasOnly=enabled;
        if(enabled)hidePanels();
        else {
            updateBrushSliders();
            syncContextToolBar();
        }
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if((watched==m_layers || watched==m_color || watched==m_toolOptions ||
            watched==m_brushOptions) &&
           (event->type()==QEvent::Show || event->type()==QEvent::Hide)) {
            syncPanelActions();
            QTimer::singleShot(0,this,[this]() {placeDrawingOverlays();});
        }
        const auto type=event->type();
        if(m_host && (type==QEvent::MouseButtonPress || type==QEvent::MouseButtonDblClick ||
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
            placeDrawingOverlays();
        }
        if((watched==m_brush || watched==m_opacityStrip || watched==m_brushOptions) &&
           event->type()==QEvent::LayoutRequest)
            placeDrawingOverlays();
        return QObject::eventFilter(watched,event);
    }

private:
    static QString sliderLayoutKey(int index)
    {
        return index==0?QStringLiteral("MobileStudio/sizeSlider"):
                        QStringLiteral("MobileStudio/opacitySlider");
    }

    void openPanelLayout(QWidget *parent)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Panel placement and fading"));
        auto *outer=new QVBoxLayout(&dialog);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        MobileTouchScroll::configure(scroll);
        auto *body=new QWidget(scroll);
        auto *form=new QFormLayout(body);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        scroll->setWidget(body);
        outer->addWidget(scroll,1);
        const auto corner=[form,body](const QString &label,int current) {
            auto *choice=new MobileTouchComboBox(body);
            choice->addItems({QObject::tr("Top left"),QObject::tr("Top right"),
                              QObject::tr("Bottom left"),QObject::tr("Bottom right")});
            choice->setCurrentIndex(current);
            form->addRow(label,choice);
            return choice;
        };
        const auto range=[form,body](const QString &label,int value,int low,int high,
                                     const QString &suffix=QString()) {
            auto *field=new QSpinBox(body);
            field->setMinimumHeight(44);
            field->setRange(low,high);
            field->setValue(value);
            field->setSuffix(suffix);
            form->addRow(label,field);
            return field;
        };
        auto *colorLayout=new QPushButton(QObject::tr("Color wheel…"),body);
        colorLayout->setMinimumHeight(44);
        connect(colorLayout,&QPushButton::clicked,&dialog,[this]() {openColorOptions();});
        form->addRow(colorLayout);
        auto *layerCorner=corner(QObject::tr("Layers position"),m_layerCorner);
        auto *layerWidth=range(QObject::tr("Layers width"),m_layerWidth,82,120);
        auto *layerHeight=range(QObject::tr("Layers maximum height"),m_layerHeight,25,100,QStringLiteral("%"));
        auto *sizeCorner=corner(QObject::tr("Size slider position"),m_sliderCorners[0]);
        auto *sizeWidth=range(QObject::tr("Size slider width"),m_sliderWidths[0],120,360);
        auto *opacityCorner=corner(QObject::tr("Opacity slider position"),m_sliderCorners[1]);
        auto *opacityWidth=range(QObject::tr("Opacity slider width"),m_sliderWidths[1],120,360);
        auto *fade=new QCheckBox(QObject::tr("Fade idle persistent panels"),body);
        fade->setChecked(m_fadePanels);
        form->addRow(fade);
        auto *opacity=range(QObject::tr("Idle opacity"),m_idleOpacity,15,90,QStringLiteral("%"));
        auto *delay=range(QObject::tr("Fade after"),m_fadeDelay,1,30,QObject::tr(" s"));
        opacity->setEnabled(fade->isChecked());
        delay->setEnabled(fade->isChecked());
        connect(fade,&QCheckBox::toggled,opacity,&QWidget::setEnabled);
        connect(fade,&QCheckBox::toggled,delay,&QWidget::setEnabled);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        outer->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.resize(parent->size());
        if(dialog.exec()!=QDialog::Accepted)return;

        m_layerCorner=layerCorner->currentIndex();
        m_layerWidth=layerWidth->value();
        m_layerHeight=layerHeight->value();
        m_sliderCorners={sizeCorner->currentIndex(),opacityCorner->currentIndex()};
        m_sliderWidths={sizeWidth->value(),opacityWidth->value()};
        m_fadePanels=fade->isChecked();
        m_idleOpacity=opacity->value();
        m_fadeDelay=delay->value();
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/layerCorner"),m_layerCorner);
        settings.setValue(QStringLiteral("MobileStudio/layerWidth"),m_layerWidth);
        settings.setValue(QStringLiteral("MobileStudio/layerHeight"),m_layerHeight);
        for(int i=0;i<2;++i) {
            const QString key=sliderLayoutKey(i);
            settings.setValue(key+QStringLiteral("Corner"),m_sliderCorners[i]);
            settings.setValue(key+QStringLiteral("Width"),m_sliderWidths[i]);
        }
        settings.setValue(QStringLiteral("MobileStudio/fadePanels"),m_fadePanels);
        settings.setValue(QStringLiteral("MobileStudio/idlePanelOpacity"),m_idleOpacity);
        settings.setValue(QStringLiteral("MobileStudio/panelFadeDelay"),m_fadeDelay);
        if(m_fadePanels)m_fadeTimer->start();
        else m_fadeTimer->stop();
        for(auto *panel:fadeTargets())wakePanel(panel);
        placeDrawingOverlays();
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

    static QWidget *pressTarget(QObject *watched,QEvent *event)
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
        if(action && action==m_shortcutSlot && !m_shortcuts.isEmpty())
            action=m_shortcuts.at(m_shortcutIndex).action;
        const auto dismiss=[target,action](QWidget *panel,QAction *opener) {
            if(panel && panel->isVisible() && panel!=target &&
               !panel->isAncestorOf(target) && action!=opener)panel->hide();
        };
        if(!m_keepLayers)dismiss(m_layers,m_layersAction);
        if(!m_keepColor)dismiss(m_color,m_colorAction);
        dismiss(m_brushOptions,m_optionsAction);
        dismiss(m_toolOptions,m_optionsAction);
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

    void syncContextToolBar()
    {
        syncBrushModes();
        if(m_layers)m_layers->syncSelectionContext();
        QWidget *canvas=m_host ? m_host->nativeCanvas() : nullptr;
        auto *tools=KoToolManager::instance();
        auto *controller=tools ? tools->activeCanvasController() : nullptr;
        const QString id=tools ? tools->activeToolId() : QString();
        if(m_textFormatting && m_textFormatting->isVisible()) {
            if(id==QStringLiteral("SvgTextTool") && controller && m_host &&
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
            m_contextToolBar->chooseTool=[this]() {chooseTool();};
            m_contextToolBar->chooseFillResource=[this](bool gradient) {
                chooseFillResource(gradient);
            };
            if(KoDockRegistry::instance()->value(QStringLiteral("TextProperties")))
                m_contextToolBar->openTextFormatting=[this]() {toggleTextFormatting();};
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
           m_engine.activeTool()!=QStringLiteral("SvgTextTool") ||
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

    int overlayTop(QWidget *canvas) const
    {
        if(m_contextToolBar && !m_contextToolBar->isHidden())
            return m_contextToolBar->geometry().bottom()+3;
        return MobileNativeCommandLayout::panelTopInset(canvas);
    }

    void syncToolbarShortcut()
    {
        if(!m_shortcutSlot || m_shortcuts.isEmpty())return;
        const auto &choice=m_shortcuts.at(m_shortcutIndex);
        if(!choice.action) {
            m_shortcutSlot->setEnabled(false);
            return;
        }
        m_shortcutSlot->setText(choice.action->text());
        m_shortcutSlot->setToolTip(choice.action->toolTip());
        m_shortcutSlot->setIcon(choice.action->icon().isNull()?choice.icon:choice.action->icon());
        m_shortcutSlot->setEnabled(choice.action->isEnabled());
        m_shortcutSlot->setVisible(choice.action->isVisible());
        m_shortcutSlot->setCheckable(choice.action->isCheckable());
        m_shortcutSlot->setChecked(choice.action->isChecked());
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

    QRect placeSliders(QRect available)
    {
        const std::array<QWidget*,2> panels{m_brush.data(),m_opacityStrip.data()};
        for(int band=0;band<2;++band) {
            std::array<int,2> widths{0,0};
            int total=0;
            int height=0;
            int count=0;
            for(int i=0;i<2;++i) {
                if(!panels[i] || !panels[i]->isVisible() || m_sliderCorners[i]/2!=band)continue;
                widths[i]=qMin(m_sliderWidths[i],available.width());
                height=qMax(height,panels[i]->sizeHint().height());
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
                panels[i]->setGeometry(x,y,widths[i],height);
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
        if(m_layers && m_layers->isVisible())placeDrawingOverlays();
    }

    void placeDrawingOverlays()
    {
        QWidget *canvas=m_host?m_host->nativeCanvas():nullptr;
        if(!canvas)return;
        QRect viewport=MobileNativeCommandLayout::overlayBounds(canvas);
        if(m_contextToolBar && !m_contextToolBar->isHidden())
            m_contextToolBar->setGeometry(viewport.left(),viewport.top(),
                qMin(760,qMax(0,viewport.width())),48);
        viewport.setTop(overlayTop(canvas));
        if(!viewport.isValid())return;
        viewport=placeSliders(viewport);
        if(!viewport.isValid())return;

        QRect besideLayers=viewport;
        if(m_layers && m_layers->isVisible()) {
            const int width=qMin(m_layerWidth,qMax(82,viewport.width()-90));
            const bool resized=width!=m_layers->width();
            if(resized)m_layers->setFixedWidth(width);
            const int height=qMin(viewport.height()*m_layerHeight/100,m_layers->contentHeight());
            const bool right=m_layerCorner%2;
            const int x=right?viewport.right()-width+1:viewport.left();
            const int y=m_layerCorner>=2?viewport.bottom()-height+1:viewport.top();
            m_layers->setGeometry(x,y,width,height);
            if(right)besideLayers.setRight(x-2);
            else besideLayers.setLeft(x+width+2);
            if(resized)QTimer::singleShot(0,m_layers.data(),[this]() {
                if(m_layers && m_layers->isVisible())m_layers->refreshNow();
            });
        }
        if(m_toolOptions || m_textFormatting || m_brushOptions) {
            const QRect room=besideLayers.width()>=240?besideLayers:viewport;
            const int optionsWidth=qMin(340,room.width());
            const QRect bounds(room.right()-optionsWidth+1,room.top(),
                               optionsWidth,qMin(440,room.height()));
            if(m_toolOptions)m_toolOptions->setGeometry(bounds.x(),bounds.y(),bounds.width(),
                qMin(room.height(),qBound(44,m_toolOptions->sizeHint().height(),440)));
            if(m_brushOptions)m_brushOptions->setGeometry(bounds.x(),bounds.y(),bounds.width(),
                qMin(room.height(),m_brushOptions->sizeHint().height()));
            if(m_textFormatting)m_textFormatting->setGeometry(bounds);
        }
        if(m_color)m_color->setWheelArea(besideLayers);
    }

    void raiseOverlays()
    {
        if(m_layers && m_layers->isVisible())m_layers->raise();
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
    bool m_fadePanels=false;
    int m_idleOpacity=35,m_fadeDelay=3;
    QElapsedTimer m_fadeClock;
    QTimer *m_fadeTimer=nullptr;
    QHash<QWidget*,qint64> m_lastPanelInput;
    std::array<QPointer<QWidget>,3> m_fadeHeld;
    QPointer<QAction> m_layersAction,m_colorAction,m_optionsAction;
    QList<QAction*> m_brushModes;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
    QPointer<MobileContextToolBar> m_contextToolBar;
    QPointer<QAction> m_shortcutSlot;
    QPointer<QAction> m_volumeUndo;
    QPointer<QAction> m_volumeRedo;
    QVector<ToolbarShortcut> m_shortcuts;
    int m_shortcutIndex=0;
    bool m_keepLayers=true,m_keepColor=true,m_showSize=true,m_showOpacity=true;
    bool m_canvasOnly=false;
};
#endif
