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
#include <QCloseEvent>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QStatusBar>
#include <QWidget>
#include "host_services.h"
#include "native_canvas_chrome.h"
#include "native_view_stack.h"
#ifdef Q_OS_ANDROID
#include <KoCanvasSupervisor.h>
#endif

class MobileLightweightCanvasHost final : public QMainWindow
#ifdef Q_OS_ANDROID
    , public KoCanvasSupervisor
#endif
{
public:
    explicit MobileLightweightCanvasHost(QWidget *parent=nullptr)
        : QMainWindow(parent),m_mobileChrome(this) {}

    ~MobileLightweightCanvasHost() override
    {
#ifdef Q_OS_ANDROID
        // Native drawing views require their engine manager to outlive them.
        // Destroy the Krita runtime first, while its native view stack
        // and this Qt window still exist.
        if(QObject *runtime=findChild<QObject*>(
               QStringLiteral("_mobileKritaRuntime"),
               Qt::FindDirectChildrenOnly))
            delete runtime;
#endif
        if(m_embeddedParent) {
            // Native painting must not be interrupted by Krita's
            // Android hide/show refresh workaround. Release the guard
            // and re-enable desktop menus when leaving this host.
            m_embeddedParent->setProperty("_mobileNativeHostActive",false);
            m_embeddedParent->setProperty("_mobileNativeDesktopDeferred",false);
            QMetaObject::invokeMethod(m_embeddedParent.data(),
                                      "updateWindowMenu",Qt::QueuedConnection);
        }
        // Restore exactly the original desktop fallback visibility.
        if(m_desktopStatusBar)
            m_desktopStatusBar->setVisible(m_desktopStatusWasVisible);
    }

    // Android must keep a SINGLE native top-level window. Embed the
    // independent engine as an ordinary child of Krita's existing
    // visible window; never create a second QWindow or move its canvas.
    bool embedInExistingWindow(QWidget *desktop)
    {
        if(!desktop || desktop==this || desktop->window()!=desktop ||
           parentWidget() || m_canvas || centralWidget())return false;
        setParent(desktop,Qt::Widget);
        m_embeddedParent=desktop;
        desktop->setProperty("_mobileNativeHostActive",true);
        // Do not expose the original Krita footer behind our child host.
        // Do not create a new status bar merely to hide it.
        if(auto *desktopStatus=desktop->findChild<QStatusBar*>(
               QString(),Qt::FindDirectChildrenOnly)) {
            m_desktopStatusBar=desktopStatus;
            m_desktopStatusWasVisible=desktopStatus->isVisible();
            desktopStatus->hide();
        }
        setGeometry(desktop->rect());
        desktop->installEventFilter(this);
        return parentWidget()==desktop && !isWindow() &&
               window()==desktop;
    }

#ifdef Q_OS_ANDROID
    // Krita 6.0.4's KisCanvasController needs a KoCanvasSupervisor
    // even when there are no traditional desktop dockers. Returning
    // an empty list is correct until native observer widgets exist.
    // Do not borrow desktop-window docker instances.
    QList<KoCanvasObserverBase*> canvasObservers() const override
    {
        return {};
    }
#endif

    // Build a native multi-document root before engine view creation.
    // This is the future Krita bootstrap entrypoint, distinct from
    // adopting an already-existing real engine canvas widget.
    MobileNativeViewStack *createNativeDocumentRoot()
    {
        if(m_canvas || centralWidget())return nullptr;
        auto *root=new MobileNativeViewStack;
        if(!installNativeCanvas(root)) {
            delete root;
            return nullptr;
        }
        m_nativeViewPort=QSharedPointer<MobileNativeViewPortAdapter>::create(root);
        return root;
    }

    QSharedPointer<MobileViewHostPort> nativeViewPort() const
    {
        return m_nativeViewPort;
    }

    // The independent Krita manager attaches its rotation and zoom
    // widgets to a QStatusBar in this very QMainWindow. Preserve
    // those engine-owned widgets, but hide their desktop presentation.
    // Never call statusBar() here: it would allocate a new footer.
    bool suppressNativeStatusChrome()
    {
        QStatusBar *nativeStatus=findChild<QStatusBar*>(
            QString(),Qt::FindDirectChildrenOnly);
        if(!nativeStatus)return false;
        nativeStatus->hide();
        return nativeStatus->isHidden();
    }

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
               m_canvas->parentWidget()==this &&
               m_canvas->window()==window();
    }
    QWidget *nativeCanvas() const {return m_canvas.data();}
    MobileHostServices services() const {return m_services;}
    bool mobileVisible() const {return m_mobileChrome.active();}

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_embeddedParent.data() && event &&
           (event->type()==QEvent::Resize ||
            event->type()==QEvent::Show)) {
            setGeometry(m_embeddedParent->rect());
            if(isVisible())raise();
        }
        return QMainWindow::eventFilter(watched,event);
    }

    void closeEvent(QCloseEvent *event) override
    {
        // Native documents must receive their own Save/Discard/Cancel
        // transaction before the Qt host can destroy their paint views.
        // An unavailable/destroyed view or a canceled dialog fails closed.
        if(m_nativeViewPort) {
            const auto pending=m_nativeViewPort->openViews();
            for(const auto &entry:pending) {
                if(!entry.nativeView ||
                   !m_nativeViewPort->closeView(entry.nativeView)) {
                    event->ignore();
                    return;
                }
            }
        }
        QMainWindow::closeEvent(event);
    }

private:
    QPointer<QWidget> m_embeddedParent;
    QPointer<QStatusBar> m_desktopStatusBar;
    bool m_desktopStatusWasVisible=false;
    QPointer<QWidget> m_canvas;
    QSharedPointer<MobileViewHostPort> m_nativeViewPort;
    MobileHostServices m_services;
    MobileNativeCanvasChrome m_mobileChrome;
    bool m_uiInstalled=false;
};
#endif
