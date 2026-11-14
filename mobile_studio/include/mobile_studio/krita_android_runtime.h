/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio's Android application bootstrap. The native executable
 * never invokes Krita's desktop application startup.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
#include "kritaui_export.h"
class KisApplication;
class KisApplicationArguments;
class KRITAUI_EXPORT MobileKritaAndroidRuntime final
{
public:
    enum class Result { Started, Failed };
    static Result launch(KisApplication &app,
                         const KisApplicationArguments &args);
    // Called by Krita's existing quick-pinch recognizer, including on
    // the independent host which does not have a KisMainWindow.
    static void resetCanvasView();
};
#endif
