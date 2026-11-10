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
#include <QScrollArea>
#include "touch_scroll.h"
#include <QSpinBox>
#include <QObject>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
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
        m_showBrushSliders=settings.value(QStringLiteral("MobileStudio/showBrushSliders"),true).toBool();
        qApp->installEventFilter(this);
        auto *contextRefresh=new QTimer(this);
        contextRefresh->setSingleShot(true);
        connect(contextRefresh,&QTimer::timeout,this,
                [this]() {syncContextToolBar();});
        if(auto *tools=KoToolManager::instance()) {
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
                    placeLayerOverlay();
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
               tools->activeToolId()!=QStringLiteral("KritaShape/KisToolBrush"))return;
            auto *tool=tools->toolById(controller->canvas(),tools->activeToolId());
            bool armed=false;
            if(tool && QMetaObject::invokeMethod(tool,"mobileArmCloneSource",
                   Qt::DirectConnection,Q_RETURN_ARG(bool,armed)) && armed)
                view->showFloatingMessage(tr("Tap the canvas to set the Clone Brush source."),QIcon());
            syncBrushModes();
        });
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
            m_layers->setFixedWidth(94);
            m_layers->onContentHeightChanged=[this]() { placeLayerOverlay(); };
            m_layers->show();
            checkpoint("Layers: floating overlay ready");
        }
        else m_layers->setVisible(!m_layers->isVisible());
        placeDrawingOverlays();
        if(m_layers && m_layers->isVisible()) {
            placeLayerOverlay();
            m_layers->refreshNow();
            raiseOverlays();
        }
    }

    // Reuse the same KoToolBase option widgets shown by Krita's
    // desktop Tool Options docker. They are live engine controls,
    // not copies or reverse-engineered equivalents. This also exposes
    // selection, transform, perspective and fill configuration.
    void toggleToolOptions(bool keepOpen=false)
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
            auto *dismiss=new QPushButton(QObject::tr("Close Tool Options"),dock);
            dismiss->setMinimumHeight(44);
            dock->setTitleBarWidget(dismiss);
            connect(dismiss,&QPushButton::clicked,dock,&QWidget::hide);
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
        } else if(m_toolOptions->isVisible() && !keepOpen) {
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
        if(!m_color) {
            checkpoint("Color wheel: creating on native canvas root");
            m_color=new MobileColorWorkspace(m_engine,canvas,true);
            checkpoint("Color wheel: constructed");
        } else if(m_color->isVisible()) {
            m_color->hide();
            return;
        }
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
                    toggleToolOptions(true);
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
            QStringLiteral("MobileStudio/toolbarShortcut")).toString();
        for(int i=0;i<m_shortcuts.size();++i)
            if(m_shortcuts.at(i).id==saved)m_shortcutIndex=i;
        connect(slot,&QAction::triggered,this,[this]() {
            const auto target=m_shortcuts.at(m_shortcutIndex).action;
            if(target && target->isEnabled())target->trigger();
        });
        syncToolbarShortcut();
    }


    // Use the same settings and input profile as the earlier Android UI.
    // Qt's native brush input still decides which gestures are painting.
    void openInputSettings()
    {
        if(!m_host)return;
        QPointer<MobileLightweightCanvasHost> host=m_host;
        QTimer::singleShot(0,this,[host]() {
            if(!host)return;
            QDialog dialog(host);
            dialog.setWindowTitle(QObject::tr("Touch, Pressure & Buttons"));
            dialog.setMinimumWidth(300);
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
            auto *doubleInfo=new QLabel(QObject::tr(
                "Double tap is assigned to Mirror View in Krita's "
                "mobile input profile. Normal painting still uses Krita's "
                "native gesture recognizer."),body);
            doubleInfo->setWordWrap(true);
            form->addRow(doubleInfo);
            auto *pinch=new QCheckBox(QObject::tr(
                "Quick two-finger pinch: reset canvas view"),body);
            pinch->setChecked(settings.value(
                QStringLiteral("TabletUi/quickPinch"),true).toBool());
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
                "Keep panels open when you touch the canvas"),body);
            description->setWordWrap(true);
            layout->addWidget(description);
            auto *layers=new QCheckBox(QObject::tr("Layers"),body);
            auto *color=new QCheckBox(QObject::tr("Color wheel"),body);
            auto *brush=new QCheckBox(QObject::tr("Show Size and Opacity at the bottom"),body);
            layers->setChecked(m_keepLayers);
            color->setChecked(m_keepColor);
            brush->setChecked(m_showBrushSliders);
            layout->addWidget(layers);
            layout->addWidget(color);
            layout->addWidget(brush);
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
            m_showBrushSliders=brush->isChecked();
            QSettings settings;
            settings.setValue(QStringLiteral("MobileStudio/keepLayersOpen"),m_keepLayers);
            settings.setValue(QStringLiteral("MobileStudio/keepColorOpen"),m_keepColor);
            settings.setValue(QStringLiteral("MobileStudio/showBrushSliders"),m_showBrushSliders);
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
        if(m_toolOptions)m_toolOptions->hide();
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
        if(m_host && (!m_keepLayers || !m_keepColor) &&
           (event->type()==QEvent::MouseButtonPress ||
            event->type()==QEvent::TabletPress || event->type()==QEvent::TouchBegin)) {
            auto *stack=qobject_cast<QStackedWidget*>(m_host->nativeCanvas());
            auto *target=qobject_cast<QWidget*>(watched);
            QWidget *view=stack?stack->currentWidget():nullptr;
            if(view && target && (target==view || view->isAncestorOf(target))) {
                if(m_layers && !m_keepLayers)m_layers->hide();
                if(m_color && !m_keepColor)m_color->hide();
                placeDrawingOverlays();
            }
        }
        if(m_host && watched==m_host->nativeCanvas() &&
           (event->type()==QEvent::Resize ||
            event->type()==QEvent::Show)) {
            placeLayerOverlay();
            placeDrawingOverlays();
        }
        if(watched==m_brush && event->type()==QEvent::LayoutRequest)
            placeDrawingOverlays();
        return QObject::eventFilter(watched,event);
    }

private:
    void syncBrushModes()
    {
        int active=-1;
        if(m_engine.hasDocument() &&
           m_engine.activeTool()==QStringLiteral("KritaShape/KisToolBrush")) {
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
        if(!canvas || !m_host->nativeViewPort() ||
           !m_host->nativeViewPort()->currentDocument() || m_canvasOnly ||
           !controller || id.isEmpty() || id==QStringLiteral("KritaShape/KisToolBrush")) {
            if(m_contextToolBar && !m_contextToolBar->isHidden()) {
                m_contextToolBar->hide();
                placeDrawingOverlays();
            }
            return;
        }
        auto *tool=tools->toolById(controller->canvas(),id);
        if(!tool)return;
        if(!m_contextToolBar) {
            m_contextToolBar=new MobileContextToolBar(canvas);
            m_contextToolBar->chooseTool=[this]() {chooseTool();};
            m_contextToolBar->openOptions=[this]() {toggleToolOptions();};
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
            return m_contextToolBar->geometry().bottom()+6;
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
        m_shortcutSlot->setToolTip(choice.action->text());
        m_shortcutSlot->setIcon(choice.icon);
        m_shortcutSlot->setEnabled(choice.action->isEnabled());
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
        if(m_showBrushSliders && !m_canvasOnly &&
           m_engine.hasDocument() && ensureBrushControls()) {
            m_brush->show();
            m_brush->raise();
        } else if(m_brush) {
            m_brush->hide();
        }
        placeDrawingOverlays();
    }

    void placeLayerOverlay()
    {
        if(!m_layers)return;
        QWidget *canvas=m_layers->parentWidget();
        if(canvas->width()<140 || canvas->height()<180)return;
        const int inset=5;
        const int top=overlayTop(canvas);
        const int bottom=m_brush && m_brush->isVisible() ?
            m_brush->y() : canvas->height();
        const int panelHeight=qMin(bottom-top-inset,m_layers->contentHeight());
        m_layers->setGeometry(inset,top,m_layers->width(),qMax(0,panelHeight));
    }

    void placeDrawingOverlays()
    {
        QWidget *canvas=m_host ? m_host->nativeCanvas() : nullptr;
        if(!canvas)return;
        const int inset=5;
        if(m_contextToolBar && !m_contextToolBar->isHidden()) {
            const int toolbarWidth=qMin(760,qMax(0,canvas->width()-2*inset));
            m_contextToolBar->setGeometry((canvas->width()-toolbarWidth)/2,
                MobileNativeCommandLayout::panelTopInset(canvas),toolbarWidth,54);
        }
        const int top=overlayTop(canvas);
        const int width=canvas->width()-2*inset;
        const int height=canvas->height()-top-inset;
        if(width<=0 || height<=0)return;
        if(m_brush) {
            const int panelHeight=qMin(height,m_brush->sizeHint().height());
            m_brush->setGeometry(inset,canvas->height()-panelHeight-inset,
                                 qMin(340,width),panelHeight);
        }
        if(m_toolOptions || m_textFormatting) {
            const int besideLayers=width-(m_layers?m_layers->width()+5:0);
            const int available=m_layers && m_layers->isVisible() &&
                besideLayers>=240 ? besideLayers : width;
            const int optionsWidth=qMin(340,available);
            const QRect bounds(canvas->width()-optionsWidth-inset,top,
                               optionsWidth,qMin(440,height));
            if(m_toolOptions)m_toolOptions->setGeometry(bounds);
            if(m_textFormatting)m_textFormatting->setGeometry(bounds);
        }
        if(m_color) {
            m_color->avoidBottom(m_brush && m_brush->isVisible() ?
                                m_brush->height()+inset : 0);
            m_color->setOverlayClearance(top,0);
        }
        placeLayerOverlay();
    }

    void raiseOverlays()
    {
        if(m_layers && m_layers->isVisible())m_layers->raise();
        for(QWidget *panel:{static_cast<QWidget*>(m_brush.data()),
                            static_cast<QWidget*>(m_toolOptions.data()),
                            static_cast<QWidget*>(m_textFormatting.data()),
                            static_cast<QWidget*>(m_color.data())}) {
            if(panel && panel->isVisible())panel->raise();
        }
    }

    void showOverlay(QWidget *panel)
    {
        if(m_toolOptions && m_toolOptions.data()!=panel)m_toolOptions->hide();
        if(m_textFormatting && m_textFormatting.data()!=panel)m_textFormatting->hide();
        if(m_color && m_color.data()!=panel)m_color->hide();
        placeDrawingOverlays();
        panel->show();
        placeLayerOverlay();
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
    QPointer<KritaMobileBrushWorkspace> m_brush;
    QList<QAction*> m_brushModes;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
    QPointer<MobileContextToolBar> m_contextToolBar;
    QPointer<QAction> m_shortcutSlot;
    QPointer<QAction> m_volumeUndo;
    QPointer<QAction> m_volumeRedo;
    QVector<ToolbarShortcut> m_shortcuts;
    int m_shortcutIndex=0;
    bool m_keepLayers=true,m_keepColor=true,m_showBrushSliders=true;
    bool m_canvasOnly=false;
};
#endif
