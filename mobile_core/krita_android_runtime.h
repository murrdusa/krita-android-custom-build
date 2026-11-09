/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio's process-level Android application bootstrap.
 * Uses Krita's Qt application and actual engine, but does not invoke
 * KisApplication::start for a normal launcher session.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
class KisApplication;
class KisApplicationArguments;
class MobileKritaAndroidRuntime final
{
public:
    enum class Result { UseLegacyInvocation, Started, Failed };
    static Result launch(KisApplication &app,
                         const KisApplicationArguments &args);
};
#endif
