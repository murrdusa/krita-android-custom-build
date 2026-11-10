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
#include <QObject>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QSettings>
#include <KoCanvasController.h>
#include <KoToolBase.h>
#include <KoToolDocker.h>
#include <KoToolManager.h>

#include "engine_bridge.h"
#include "brush_workspace.h"
#include "layer_workspace.h"
#include "color_workspace.h"
#include "tool_browser.h"
#include "lightweight_canvas_host.h"

class MobileKritaNativeWorkspace final : public QObject
{
public:
    explicit MobileKritaNativeWorkspace(
        MobileLightweightCanvasHost *host,
        const MobileHostServices &services,KisViewManager *manager)
        : QObject(host),m_host(host),m_engine(services,manager)
    {
        setObjectName(QStringLiteral("_mobileNativeEngineWorkspaces"));
        // The factory installs the native activation router as each
        // document is attached. Refresh after, not before, that turn.
        if(auto *root=host ? dynamic_cast<MobileNativeViewStack*>(
                                 host->nativeCanvas()) : nullptr) {
            QObject::connect(root,&QStackedWidget::currentChanged,this,
                             [this](int) {
                QTimer::singleShot(0,this,[this]() {
                    refresh();
                    if(m_color && m_color->isVisible())m_color->raise();
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

    void toggleBrushes()
    {
        if(!m_host || !m_engine.hasDocument())return;
        if(!m_brushDock) {
            checkpoint("Brush controls: creating");
            auto *dock=new QDockWidget(
                QObject::tr("Krita brush controls"),m_host);
            dock->setObjectName(QStringLiteral("_mobileNativeBrushDock"));
            dock->setAllowedAreas(Qt::BottomDockWidgetArea);
            dock->setFeatures(QDockWidget::DockWidgetClosable);
            m_brush=new KritaMobileBrushWorkspace(m_engine,dock);
            dock->setWidget(m_brush);
            m_host->addDockWidget(Qt::BottomDockWidgetArea,dock);
            m_brushDock=dock;
            checkpoint("Brush controls: ready");
        } else m_brushDock->setVisible(!m_brushDock->isVisible());
        if(m_brushDock && m_brushDock->isVisible()) {
            m_brush->refresh();
            m_brushDock->raise();
        }
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
            m_layerCanvas=canvas;
            canvas->installEventFilter(this);
            m_layers=new KritaMobileLayersPanel(m_engine,canvas);
            m_layers->setObjectName(
                QStringLiteral("_mobileNativeFloatingLayers"));
            m_layers->setFixedWidth(124);
            m_layers->onRequestClose=[this]() {
                if(m_layers)m_layers->hide();
            };
            placeLayerOverlay();
            m_layers->show();
            checkpoint("Layers: floating overlay ready");
        }
        else m_layers->setVisible(!m_layers->isVisible());
        if(m_layers && m_layers->isVisible()) {
            placeLayerOverlay();
            m_layers->refreshNow();
            m_layers->raise();
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

        if(!m_toolOptions) {
            auto *dock=new KoToolDocker(m_host);
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
            dock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                  Qt::RightDockWidgetArea);
            dock->setFeatures(QDockWidget::DockWidgetClosable);
            m_host->addDockWidget(Qt::RightDockWidgetArea,dock);
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
            checkpoint("Krita native tool options: dock connected");
        } else {
            m_toolOptions->setVisible(!m_toolOptions->isVisible());
        }

        if(!m_toolOptions || !m_toolOptions->isVisible())return;
        syncActiveToolOptions();
        m_toolOptions->raise();
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
            m_color=new MobileColorWorkspace(m_engine,canvas);
            checkpoint("Color wheel: constructed");
            m_color->show();
        } else {
            m_color->setVisible(!m_color->isVisible());
        }
        if(m_color && m_color->isVisibleTo(canvas)) {
            m_color->refresh();
            m_color->raise();
            checkpoint("Color wheel: visible on native canvas root");
        } else {
            checkpoint("Color wheel: visible state not reached");
        }
    }

    void chooseTool()
    {
        if(!m_host || !m_engine.hasDocument())return;
        QPointer<MobileLightweightCanvasHost> host=m_host;
        QTimer::singleShot(0,this,[this,host]() {
            if(host) {
                checkpoint("Tool chooser: opening");
                MobileToolBrowser::open(m_engine,host);
                checkpoint("Tool chooser: closed");
            }
        });
    }

    void refresh()
    {
        if(!m_host)return;
        // Switching between native canvases changes the active tool's
        // KoCanvasController. Rebind the existing Tool Options dock to
        // that canvas immediately instead of leaving stale controls.
        if(m_toolOptions && m_toolOptions->isVisible())
            syncActiveToolOptions();
        if(m_brush && m_brush->isVisible())m_brush->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshNow();
        if(m_color && m_color->isVisible()) {
            m_color->refresh();
            m_color->raise();
        }
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_layerCanvas &&
           (event->type()==QEvent::Resize ||
            event->type()==QEvent::Show)) {
            placeLayerOverlay();
        }
        return QObject::eventFilter(watched,event);
    }

private:
    void placeLayerOverlay()
    {
        if(!m_layers || !m_layerCanvas)return;
        QWidget *canvas=m_layerCanvas.data();
        if(canvas->width()<140 || canvas->height()<180)return;
        const int inset=5;
        const int panelWidth=qMin(124,qMax(110,canvas->width()-2*inset));
        const int panelHeight=qMin(canvas->height()-2*inset,
                                   qMax(240,(canvas->height()*4)/5));
        m_layers->setGeometry(
            canvas->width()-panelWidth-inset,inset,
            panelWidth,panelHeight);
        if(m_layers->isVisible())m_layers->raise();
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
    QPointer<QDockWidget> m_brushDock;
    QPointer<QWidget> m_layerCanvas;
    QPointer<KoToolDocker> m_toolOptions;
    QPointer<KritaMobileBrushWorkspace> m_brush;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
};
#endif
