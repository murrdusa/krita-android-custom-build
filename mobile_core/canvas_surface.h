/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Canvas attachment contract for Mobile Studio.
 *
 * This is a Qt-only interface. The session and transfer classes do not
 * need to know the Mobile Studio window class or any Krita paint types.
 * They move exactly one existing QWidget canvas stack between windows.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QWidget>

class MobileCanvasSurface
{
public:
    virtual ~MobileCanvasSurface() = default;

    // Returns the host top-level widget, or nullptr if unavailable.
    virtual QWidget *canvasSurfaceWindow() = 0;

    // Adopt an existing native Qt canvas stack. Must not recreate or
    // destroy the native paint device. Returns true only when attached.
    virtual bool attachNativeCanvas(QWidget *nativeCanvas) = 0;

    // Detach and return the exact same QWidget. The caller resumes
    // ownership; the surface must not delete it.
    virtual QWidget *detachNativeCanvas() = 0;

    // Verify ownership of a particular native widget, not just whether
    // some canvas is present. Used to detect partial/failed transfers.
    virtual bool ownsNativeCanvas(QWidget *nativeCanvas) const = 0;

    // Refuse entry if the destination already has a native canvas.
    // Do not detach an unrelated widget merely to recover from failure.
    virtual bool hasNativeCanvas() const = 0;

    virtual void refreshCanvasSurface() = 0;
};

#endif // Q_OS_ANDROID
