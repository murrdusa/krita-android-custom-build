/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Pure Qt predicate for Android system Back while Mobile Studio is
 * showing. It does not synthesize keyboard events or close Krita views.
 * The calling controller owns the actual panel-dismiss/exit action.
 */
#pragma once
#include <QEvent>
#include <QMainWindow>
#include <QObject>
#include <QWidget>
#include <QWindow>
#include <Qt>

class MobileAndroidBackPolicy final
{
public:
    static bool targetsKrita(QMainWindow *backend,QObject *recipient)
    {
        if(!backend || !recipient)return false;
        const QWidget *widget=qobject_cast<QWidget*>(recipient);
        if(widget)return widget->window()==backend;
        const QWindow *window=qobject_cast<QWindow*>(recipient);
        return window && backend->windowHandle()==window;
    }

    static bool mayDismiss(QMainWindow *backend,QObject *recipient,
                           QEvent::Type eventType,int key,bool autoRepeat,
                           bool mobileActive,bool transitioning,
                           bool modalVisible,bool popupVisible)
    {
        return eventType==QEvent::KeyPress &&
               key==Qt::Key_Back && !autoRepeat &&
               mobileActive && !transitioning && !modalVisible &&
               !popupVisible && targetsKrita(backend,recipient);
    }
};
