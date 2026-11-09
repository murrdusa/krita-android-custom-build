/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Android window bootstrap policy, separate from KisApplication.
 */
#include "krita_application_entry.h"
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QSettings>
#include "KisApplicationArguments.h"
#include "engine_only_startup_policy.h"
#include "engine_only_bootstrap.h"

bool MobileKritaApplicationEntry::start(
    const KisApplicationArguments &args,
    bool needsWindow,bool showWindow,bool batchRun,
    bool doNewImage,bool doTemplate,bool queuedIntents)
{
    // Ordinary launches use only the independent native painting root.
    // External file and explicit session invocations need native
    // document-intent support before the legacy routing can be removed.
    const bool compatible=needsWindow && showWindow && !batchRun &&
        !doNewImage && !doTemplate && !queuedIntents &&
        args.filenames().isEmpty() && args.fileLayer().isEmpty() &&
        args.session().isEmpty() && args.workspace().isEmpty() &&
        args.windowLayout().isEmpty() &&
        !args.canvasOnly() && !args.fullScreen();

    QSettings settings;
    if(MobileEngineOnlyStartupPolicy::begin(settings,compatible)!=
       MobileEngineOnlyStartupPolicy::Decision::Launch)
        return false;

    if(MobileEngineOnlyBootstrap::launch())
        return true;

    MobileEngineOnlyStartupPolicy::complete(settings,false);
    qCritical() << "Mobile Studio: native drawing window failed";
    return false;
}
#endif
