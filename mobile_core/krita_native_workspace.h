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
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QSettings>

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
                QTimer::singleShot(0,this,[this]() { refresh(); });
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
        if(!m_layersDock) {
            checkpoint("Layers: creating");
            auto *dock=new QDockWidget(QObject::tr("Krita layers"),m_host);
            dock->setObjectName(QStringLiteral("_mobileNativeLayersDock"));
            dock->setAllowedAreas(Qt::LeftDockWidgetArea|
                                  Qt::RightDockWidgetArea);
            dock->setFeatures(QDockWidget::DockWidgetClosable);
            m_layers=new KritaMobileLayersPanel(m_engine,dock);
            m_layers->onRequestClose=[this]() {
                if(m_layersDock)m_layersDock->hide();
            };
            dock->setWidget(m_layers);
            m_host->addDockWidget(Qt::RightDockWidgetArea,dock);
            m_layersDock=dock;
            checkpoint("Layers: ready");
        } else m_layersDock->setVisible(!m_layersDock->isVisible());
        if(m_layersDock && m_layersDock->isVisible()) {
            m_layers->refreshNow();
            m_layersDock->raise();
        }
    }

    void toggleColor()
    {
        if(!m_host || !m_engine.hasDocument())return;
        if(!m_color) {
            checkpoint("Color wheel: creating");
            m_color=new MobileColorWorkspace(m_engine,m_host);
            checkpoint("Color wheel: ready");
            m_color->show();
        } else {
            m_color->setVisible(!m_color->isVisible());
        }
        if(m_color && m_color->isVisible()) {
            m_color->refresh();
            m_color->raise();
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
        if(m_brush && m_brush->isVisible())m_brush->refresh();
        if(m_layers && m_layers->isVisible())m_layers->refreshNow();
        if(m_color && m_color->isVisible())m_color->refresh();
    }

private:
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
    QPointer<QDockWidget> m_layersDock;
    QPointer<KritaMobileBrushWorkspace> m_brush;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
};
#endif
