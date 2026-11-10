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
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QObject>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QVBoxLayout>
#include <KoCanvasController.h>
#include <KoToolBase.h>
#include <KoToolDocker.h>
#include <KoToolManager.h>

#include "engine_bridge.h"
#include "brush_workspace.h"
#include "layer_workspace.h"
#include "color_workspace.h"
#include "tool_browser.h"
#include "command_browser.h"
#include "lightweight_canvas_host.h"
#include "native_command_layout.h"

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
        m_showBrushSliders=settings.value(QStringLiteral("MobileStudio/showBrushSliders"),false).toBool();
        qApp->installEventFilter(this);
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

    void toggleBrushes(int slot)
    {
        if(!m_host || !m_engine.hasDocument())return;
        if(!ensureBrushControls())return;
        if(!m_brush->isActiveMode(slot)) {
            m_brush->activateMode(slot);
            return;
        }
        QTimer::singleShot(0,this,[this,slot]() {
            if(m_brush && m_engine.hasDocument() && m_brush->isActiveMode(slot))
                m_brush->browsePresets(slot);
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
            auto *layout=new QVBoxLayout(&dialog);
            auto *description=new QLabel(QObject::tr(
                "Keep panels open when you touch the canvas"),&dialog);
            description->setWordWrap(true);
            layout->addWidget(description);
            auto *layers=new QCheckBox(QObject::tr("Layers"),&dialog);
            auto *color=new QCheckBox(QObject::tr("Color wheel"),&dialog);
            auto *brush=new QCheckBox(QObject::tr("Show Size and Opacity at the bottom"),&dialog);
            layers->setChecked(m_keepLayers);
            color->setChecked(m_keepColor);
            brush->setChecked(m_showBrushSliders);
            layout->addWidget(layers);
            layout->addWidget(color);
            layout->addWidget(brush);
            auto *shortcut=new QComboBox(&dialog);
            if(!m_shortcuts.isEmpty()) {
                layout->addWidget(new QLabel(QObject::tr("Toolbar shortcut"),&dialog));
                for(const auto &choice:m_shortcuts)
                    shortcut->addItem(choice.icon,choice.action->text());
                shortcut->setCurrentIndex(m_shortcutIndex);
                layout->addWidget(shortcut);
            } else {
                shortcut->hide();
            }
            auto *done=new QPushButton(QObject::tr("Done"),&dialog);
            connect(done,&QPushButton::clicked,&dialog,&QDialog::accept);
            layout->addWidget(done);
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
            hidePanels();
            return;
        }
        updateBrushSliders();
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
        if(m_color)m_color->hide();
    }

    void setCanvasOnly(bool enabled)
    {
        m_canvasOnly=enabled;
        if(enabled)hidePanels();
        else updateBrushSliders();
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
        const int top=MobileNativeCommandLayout::panelTopInset(canvas);
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
        const int top=MobileNativeCommandLayout::panelTopInset(canvas);
        const int width=canvas->width()-2*inset;
        const int height=canvas->height()-top-inset;
        if(width<=0 || height<=0)return;
        if(m_brush) {
            const int panelHeight=qMin(height,m_brush->sizeHint().height());
            m_brush->setGeometry(inset,canvas->height()-panelHeight-inset,
                                 qMin(340,width),panelHeight);
        }
        if(m_toolOptions) {
            const int besideLayers=width-(m_layers?m_layers->width()+5:0);
            const int available=m_layers && m_layers->isVisible() &&
                besideLayers>=240 ? besideLayers : width;
            const int optionsWidth=qMin(340,available);
            m_toolOptions->setGeometry(canvas->width()-optionsWidth-inset,
                                       top,optionsWidth,
                                       qMin(440,height));
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
                            static_cast<QWidget*>(m_color.data())}) {
            if(panel && panel->isVisible())panel->raise();
        }
    }

    void showOverlay(QWidget *panel)
    {
        if(m_toolOptions && m_toolOptions.data()!=panel)m_toolOptions->hide();
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
    QPointer<KritaMobileBrushWorkspace> m_brush;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
    QPointer<QAction> m_shortcutSlot;
    QVector<ToolbarShortcut> m_shortcuts;
    int m_shortcutIndex=0;
    bool m_keepLayers=true,m_keepColor=true,m_showBrushSliders=false;
    bool m_canvasOnly=false;
};
#endif
