/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Mobile drawing application entrypoint, independent of KisMainWindow.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
class KisApplicationArguments;
class MobileKritaApplicationEntry final
{
public:
    static bool start(const KisApplicationArguments &args,
                      bool needsWindow,bool showWindow,bool batchRun,
                      bool doNewImage,bool doTemplate,bool queuedIntents);
};
#endif
