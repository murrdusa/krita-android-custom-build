/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Resume a native canvas activation after its Qt window becomes visible.
 * A deferred activation may run while Android has paused/hidden the
 * window; never lose that activation permanently.
 *
 * Qt-only: no Krita engine, desktop MDI, or additional painting state.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <functional>
#include <utility>

class MobileCanvasResumeObserver final : public QObject
{
public:
    static MobileCanvasResumeObserver *attach(
        QWidget *window,QWidget *canvasRoot,QObject *owner,
        std::function<void()> activate)
    {
        if(!window || !canvasRoot || !owner || !activate)return nullptr;
        auto *observer=new MobileCanvasResumeObserver(
            window,canvasRoot,owner,std::move(activate));
        window->installEventFilter(observer);
        if(canvasRoot!=window)canvasRoot->installEventFilter(observer);
        // On Android the first QWidget::Show can precede exposure of
        // the native surface. That first exposure must activate and
        // repaint the real OpenGL canvas just like a resume does.
        if(QWindow *surface=window->windowHandle()) {
            observer->m_surface=surface;
            surface->installEventFilter(observer);
        }
        observer->request();
        return observer;
    }

    // Coalesce bursts of Show/WindowActivate into one event-loop turn.
    // The caller revalidates the actual selected view before activating.
    void request()
    {
        if(m_queued)return;
        m_queued=true;
        QTimer::singleShot(0,this,[this]() {
            m_queued=false;
            if(m_window && m_root && m_window->isVisible() &&
               m_root->isVisible() && m_activate)
                m_activate();
        });
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==m_surface.data() &&
           event->type()==QEvent::Expose) {
            request();
        } else if((watched==m_window.data() ||
                   watched==m_root.data()) &&
                  (event->type()==QEvent::Show ||
                   event->type()==QEvent::WindowActivate)) {
            request();
        }
        if(watched==m_window.data() &&
           event->type()==QEvent::WinIdChange && !m_surface) {
            if(QWindow *surface=m_window->windowHandle()) {
                m_surface=surface;
                surface->installEventFilter(this);
            }
        }
        return QObject::eventFilter(watched,event);
    }

private:
    MobileCanvasResumeObserver(
        QWidget *window,QWidget *root,QObject *owner,
        std::function<void()> activate)
        : QObject(owner),m_window(window),m_root(root),
          m_activate(std::move(activate)) {}

    QPointer<QWidget> m_window;
    QPointer<QWidget> m_root;
    QPointer<QWindow> m_surface;
    std::function<void()> m_activate;
    bool m_queued=false;
};
#endif
