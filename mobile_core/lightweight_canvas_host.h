/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Lightweight Qt window host for an existing native Krita painting
 * widget. This module has no KisMainWindow, QMdiArea, or Krita UI types.
 * It is deliberately not the Android default until real Krita engine
 * view creation, Open/Save and lifecycle behavior are validated.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QMainWindow>
#include <QPointer>
#include <QWidget>
#include "host_services.h"
#include "native_canvas_chrome.h"

class MobileLightweightCanvasHost final : public QMainWindow
{
public:
    explicit MobileLightweightCanvasHost(QWidget *parent=nullptr)
        : QMainWindow(parent),m_mobileChrome(this) {}

    // Adopt a *newly created* painting widget once, before a mobile UI
    // is installed. The host owns that widget for its entire lifetime.
    // This method never takes a live canvas from another QMainWindow.
    bool installNativeCanvas(QWidget *canvas)
    {
        if(!canvas || centralWidget() || m_canvas || m_uiInstalled ||
           canvas->parentWidget() || canvas->window()!=canvas)
            return false;
        setCentralWidget(canvas);
        m_canvas=canvas;
        return retainsOriginalCanvas();
    }

    bool attachServices(const MobileHostServices &services)
    {
        if(!retainsOriginalCanvas() || !services.ready() ||
           services.documentRoot!=m_canvas)return false;
        m_services=services;
        return true;
    }

    bool installMobileChrome(QWidget *top,QWidget *bottom,
                             QWidget *layers=nullptr)
    {
        if(!ready() || m_uiInstalled)return false;
        m_uiInstalled=m_mobileChrome.install(top,bottom,layers);
        return m_uiInstalled && retainsOriginalCanvas();
    }

    bool setMobileVisible(bool enabled)
    {
        if(!ready() || !m_uiInstalled)return false;
        const bool changed=enabled?
            m_mobileChrome.activate():m_mobileChrome.deactivate();
        return changed && retainsOriginalCanvas();
    }

    bool ready() const
    {
        return retainsOriginalCanvas() && m_services.ready() &&
               m_services.documentRoot==m_canvas;
    }
    bool retainsOriginalCanvas() const
    {
        return m_canvas && centralWidget()==m_canvas &&
               m_canvas->window()==this;
    }
    QWidget *nativeCanvas() const {return m_canvas.data();}
    MobileHostServices services() const {return m_services;}
    bool mobileVisible() const {return m_mobileChrome.active();}

private:
    QPointer<QWidget> m_canvas;
    MobileHostServices m_services;
    MobileNativeCanvasChrome m_mobileChrome;
    bool m_uiInstalled=false;
};
#endif
