/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android entrypoint for Krita Mobile Studio. Krita's Qt application
 * object still handles input/event dispatch, while engine setup and
 * the drawing window no longer enter KisApplication::start.
 */
#include "krita_android_runtime.h"
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include "KisApplication.h"
#include "KisApplicationArguments.h"
#include "KisAndroidDonations.h"
#include "KisPart.h"
#include "kis_config_notifier.h"
#include "kis_config.h"
#include "kis_document_aware_spin_box_unit_manager.h"
#include "kis_spin_box_unit_manager.h"
#include "krita_application_entry.h"
#include "android_document_intents.h"
#include <config-qt-patches-present.h>

MobileKritaAndroidRuntime::Result MobileKritaAndroidRuntime::launch(
    KisApplication &app,const KisApplicationArguments &args)
{
    // This APK has one application: the native Mobile Studio painting
    // host. External files are queued below; there is no desktop UI
    // to fall back into on any Android launch.
    if(!args.fileLayer().isEmpty() || !args.session().isEmpty() ||
       !args.workspace().isEmpty() || !args.windowLayout().isEmpty() ||
       args.exportAs() || args.exportSequence() ||
       !args.exportFileName().isEmpty() || args.canvasOnly() ||
       args.fullScreen() || args.doNewImage() || args.doTemplate()) {
        qWarning() << "Mobile Studio: desktop-only launch flags ignored";
    }

    qInfo() << "Mobile Studio: standalone Android process startup";
    MobileAndroidDocumentIntents::activate();
    for(const QString &file:args.filenames())
        MobileAndroidDocumentIntents::enqueue(file);

    KisConfig cfg(false);
    const QString opengl=cfg.canvasState();
    if(opengl==QStringLiteral("OPENGL_NOT_TRIED"))
        cfg.setCanvasState("TRY_OPENGL");
    else if(opengl!=QStringLiteral("OPENGL_SUCCESS") &&
            opengl!=QStringLiteral("TRY_OPENGL"))
        cfg.setCanvasState("OPENGL_FAILED");

    app.initializeGlobals(args);
    app.initializeAndroidScaling(cfg);
    app.addResourceTypes();
    app.loadPlugins();
    if(!app.registerResources()) {
        qCritical() << "Mobile Studio: resource registration failed";
        return Result::Failed;
    }
    // The document factory is an engine service, not a desktop window.
    KisPart::instance();

    KisSpinBoxUnitManagerFactory::setDefaultUnitManagerBuilder(
        new KisDocumentAwareSpinBoxUnitManagerBuilder());
    QObject::connect(&app,&KisApplication::aboutToQuit,
                     &KisSpinBoxUnitManagerFactory::clearUnitManagerBuilder);

    // Keep Krita's long-press handling and Android stylus platform flags
    // intact while replacing desktop startup.
    auto *notifier=KisConfigNotifier::instance();
    QObject::connect(notifier,SIGNAL(sigLongPressChanged(bool)),
                     &app,SLOT(slotSetLongPress(bool)));
    QMetaObject::invokeMethod(&app,"slotSetLongPress",
                              Q_ARG(bool,cfg.longPressEnabled()));
#if KRITA_QT_HAS_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_PAGE_UP_DOWN
    auto setPageKeys=[](bool enabled) {
        QCoreApplication::setKritaAttribute(
            KRITA_QATTRIBUTE_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_PAGE_UP_DOWN,
            enabled);
    };
    QObject::connect(
        notifier,
        &KisConfigNotifier::sigUsePageUpDownMouseButtonEmulationWorkaroundChanged,
        &app,setPageKeys);
    setPageKeys(cfg.usePageUpDownMouseButtonEmulationWorkaround());
#endif
#if KRITA_QT_HAS_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_HIGH_FUNCTION_KEYS
    QCoreApplication::setKritaAttribute(
        KRITA_QATTRIBUTE_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_HIGH_FUNCTION_KEYS,
        true);
#endif
#if KRITA_QT_HAS_ANDROID_IGNORE_HISTORIC_TABLET_EVENTS
    auto setHistoric=[](bool enabled) {
        QCoreApplication::setKritaAttribute(
            KRITA_QATTRIBUTE_ANDROID_IGNORE_HISTORIC_TABLET_EVENTS,enabled);
    };
    QObject::connect(
        notifier,
        &KisConfigNotifier::sigUseIgnoreHistoricTabletEventsWorkaroundChanged,
        &app,setHistoric);
    setHistoric(cfg.useIgnoreHistoricTabletEventsWorkaround());
#endif

    // Hide the Krita splash and launch the only native drawing window.
    // An initialization failure is an error, not an automatic UI switch.
    app.hideSplashScreen();
    if(!MobileKritaApplicationEntry::start())
        return Result::Failed;

    app.verifyMetatypeRegistration();
    qInfo() << "Mobile Studio: independent Android startup ready";
    return Result::Started;
}
#endif
