#!/usr/bin/env python3
"""Install the opt-in independent mobile WINDOW into the Krita Android tree.

This is an incremental migration path, not yet a standalone executable.
Run AFTER existing patches. The window hosts Krita's native MDI canvas
while desktop KisMainWindow temporarily remains the engine service.
All edits are source-anchored and reversible by switching the mode off.
"""
from pathlib import Path
import hashlib
import re
import sys

def one(source, old, new, label):
    count=source.count(old)
    if count!=1:
        raise RuntimeError(f"Mobile shell {label}: expected 1 anchor, got {count}")
    return source.replace(old,new,1)

def main(root:Path):
    root=root.resolve()
    own=Path(__file__).resolve().parent
    shell=own/"mobile_core/standalone_host.h"
    session=own/"mobile_core/canvas_session.h"
    startup_policy=own/"mobile_core/mobile_startup_policy.h"
    back_policy=own/"mobile_core/android_back_policy.h"
    transfer=own/"mobile_core/canvas_transfer.h"
    native_chrome=own/"mobile_core/native_canvas_chrome.h"
    native_pilot=own/"mobile_core/native_canvas_pilot.h"
    lightweight=own/"mobile_core/lightweight_canvas_host.h"
    native_view_stack=own/"mobile_core/native_view_stack.h"
    native_krita_save_host=own/"mobile_core/native_krita_save_host.h"
    native_krita_factory=own/"mobile_core/krita_native_view_factory.h"
    canvas_resume_observer=own/"mobile_core/canvas_resume_observer.h"
    standalone_krita_runtime=own/"mobile_core/krita_standalone_runtime.h"
    standalone_krita_documents=own/"mobile_core/krita_standalone_documents.h"
    standalone_engine_session=own/"mobile_core/krita_engine_session.h"
    native_engine_probe=own/"mobile_core/krita_native_engine_compile_probe.cpp"
    standalone_engine_pilot=own/"mobile_core/krita_standalone_pilot.h"
    native_startup_policy=own/"mobile_core/native_startup_policy.h"
    native_boot_coordinator=own/"mobile_core/native_boot_coordinator.h"
    native_bootstrap_audit=own/"mobile_core/native_bootstrap_audit.h"
    engine_only_policy=own/"mobile_core/engine_only_startup_policy.h"
    engine_only_root=own/"mobile_core/engine_only_root.h"
    engine_only_bootstrap_header=own/"mobile_core/engine_only_bootstrap.h"
    engine_only_bootstrap_source=own/"mobile_core/engine_only_bootstrap.cpp"
    native_workspace=own/"mobile_core/krita_native_workspace.h"
    native_reorder_policy=own/"mobile_core/native_layer_reorder_policy.h"
    native_command_layout=own/"mobile_core/native_command_layout.h"
    deferred_view_command=own/"mobile_core/deferred_view_command.h"
    document_navigation=own/"mobile_core/document_navigation.h"
    surface=own/"mobile_core/canvas_surface.h"
    switcher=own/"mobile_core/document_switcher.h"
    bridge=own/"mobile_core/engine_bridge.h"
    document_service=own/"mobile_core/document_service.h"
    native_document_rollback=own/"mobile_core/native_document_rollback.h"
    native_document_path_policy=own/"mobile_core/native_document_path_policy.h"
    document_port=own/"mobile_core/document_host_port.h"
    view_port=own/"mobile_core/view_host_port.h"
    view_guard=own/"mobile_core/view_identity_guard.h"
    mdi_view_adapter=own/"mobile_core/mdi_view_host_adapter.h"
    command_port=own/"mobile_core/command_registry_port.h"
    desktop_adapters=own/"mobile_core/desktop_host_adapters.h"
    host_services=own/"mobile_core/host_services.h"
    mdi_identity=own/"mobile_core/mdi_view_identity.h"
    command_service=own/"mobile_core/command_service.h"
    brush=own/"mobile_core/brush_workspace.h"
    color=own/"mobile_core/color_workspace.h"
    layers=own/"mobile_core/layer_workspace.h"
    commands=own/"mobile_core/command_browser.h"
    quick=own/"mobile_core/quick_actions.h"
    pressure_calibration=own/"mobile_core/finger_pressure_calibration.h"
    pressure_settings=own/"mobile_core/finger_pressure_settings.h"
    reorder=own/"mobile_core/touch_reorder_list.h"
    tools=own/"mobile_core/tool_browser.h"
    documents=own/"mobile_core/document_workflow.h"
    if not all(p.is_file() for p in
               (shell,session,startup_policy,back_policy,transfer,native_chrome,native_pilot,lightweight,native_view_stack,native_krita_save_host,native_krita_factory,canvas_resume_observer,standalone_krita_runtime,standalone_krita_documents,standalone_engine_session,native_engine_probe,standalone_engine_pilot,native_startup_policy,native_boot_coordinator,native_bootstrap_audit,engine_only_policy,engine_only_root,engine_only_bootstrap_header,engine_only_bootstrap_source,native_workspace,native_command_layout,native_reorder_policy,deferred_view_command,document_navigation,surface,switcher,bridge,document_service,native_document_rollback,native_document_path_policy,document_port,view_port,view_guard,mdi_view_adapter,command_port,desktop_adapters,host_services,mdi_identity,command_service,brush,color,layers,commands,quick,pressure_calibration,pressure_settings,
                reorder,tools,documents)):
        raise RuntimeError("Missing Mobile Studio source module")

    # Dependency boundary: only the backend services and temporary canvas
    # transfer controller may directly include Krita's desktop window.
    # Keep the mobile widgets/engine facade unaware of this legacy class,
    # so a future lightweight application host can replace it in one place.
    desktop_dependent=(desktop_adapters,mdi_view_adapter)
    qt_host_components=(session,back_policy,transfer,native_chrome,native_pilot,lightweight,native_view_stack,native_krita_save_host,surface)
    mobile_frontend=(standalone_engine_session,engine_only_root,engine_only_policy,engine_only_bootstrap_header,engine_only_bootstrap_source,native_bootstrap_audit,native_boot_coordinator,native_command_layout,native_workspace,native_reorder_policy,native_startup_policy,canvas_resume_observer,deferred_view_command,document_navigation,shell,bridge,host_services,document_service,native_document_rollback,native_document_path_policy,document_port,view_port,view_guard,
                     command_service,command_port,switcher,brush,color,layers,
                     commands,quick,mdi_identity,pressure_calibration,pressure_settings,
                     reorder,tools,documents,*qt_host_components)
    for module in mobile_frontend:
        contents=module.read_text(encoding="utf-8")
        if ('#include "KisMainWindow.h"' in contents or
            '#include <KisMainWindow.h>' in contents or
            'legacyHost()' in contents):
            raise RuntimeError(
                f"Mobile Studio desktop UI dependency leaked into {module.name}")
    # Canvas transfer must stay a pure Qt implementation. Its only
    # dependency on the destination is the generic MobileCanvasSurface
    # contract, not MobileStandaloneWindow or any Krita-specific class.
    transfer_text=transfer.read_text(encoding="utf-8")
    surface_text=surface.read_text(encoding="utf-8")
    for forbidden in ('"standalone_host.h"','"engine_bridge.h"',
                      'MobileStandaloneWindow','KisMainWindow',
                      'KisDocument'):
        if forbidden in transfer_text or forbidden in surface_text:
            raise RuntimeError(
                f"Qt canvas transfer contract leaked concrete dependency: {forbidden}")
    for module in desktop_dependent:
        if not module.is_file():
            raise RuntimeError(
                f"Missing isolated Krita backend module: {module.name}")

    # Host-neutral native KisView teardown and close-time Save seams.
    # Changes are Android-only. The desktop Krita path is untouched.
    viewheader=root/"libs/ui/KisView.h"
    vh=viewheader.read_text(encoding="utf-8")
    vh=one(vh,"class KisViewManager;","class KisViewManager;\n#ifdef Q_OS_ANDROID\nclass MobileKritaNativeSaveHost;\n#endif",
           "KisView native save host forward declaration")
    vh=one(vh,"    KisViewManager *viewManager() const;",
           "    KisViewManager *viewManager() const;\n#ifdef Q_OS_ANDROID\n    void setMobileNativeSaveHost(MobileKritaNativeSaveHost *host);\n    MobileKritaNativeSaveHost *mobileNativeSaveHost() const;\n#endif",
           "KisView native save host access")
    viewheader.write_text(vh,encoding="utf-8")

    viewcpp=root/"libs/ui/KisView.cpp"
    vc=viewcpp.read_text(encoding="utf-8")
    vc=one(vc,'#include "KisViewManager.h"',
           '#include "KisViewManager.h"\n#ifdef Q_OS_ANDROID\n#include <KoCanvasSupervisor.h>\n#include "native_krita_save_host.h"\n#endif',
           "KisView native save host include")
    vc=one(vc,'''        , canvasController(_q, viewManager->mainWindow(), viewManager->actionCollection())''',
           '''#ifdef Q_OS_ANDROID
        , canvasController(_q,
              dynamic_cast<KoCanvasSupervisor*>(viewManager->qtMainWindow()),
              viewManager->actionCollection())
#else
        , canvasController(_q, viewManager->mainWindow(), viewManager->actionCollection())
#endif''',
           "KisView Qt-native canvas supervisor")
    vc=one(vc,'    KisViewManager *viewManager {nullptr};',
           '    KisViewManager *viewManager {nullptr};\n#ifdef Q_OS_ANDROID\n    QPointer<MobileKritaNativeSaveHost> mobileNativeSaveHost;\n#endif',
           "KisView guarded native save port")
    vc=one(vc,'        d->viewManager->mainWindow()->notifyChildViewDestroyed(this);',
           '        // An independent QMainWindow has no KisMainWindow hooks.\n        if (KisMainWindow *desktop=d->viewManager->mainWindow())\n            desktop->notifyChildViewDestroyed(this);',
           "KisView destructor desktop notification")
    vc=one(vc,
           '        if (KisMainWindow *desktop=d->viewManager->mainWindow())\n            desktop->notifyChildViewDestroyed(this);',
           '''        if (KisMainWindow *desktop=d->viewManager->mainWindow()) {
            desktop->notifyChildViewDestroyed(this);
        }
#ifdef Q_OS_ANDROID
        else {
            // Match native Krita's tracked canvas removal when a
            // lightweight QWidget owns a KisView instead of MDI.
            d->viewManager->inputManager()->removeTrackedCanvas(&d->canvas);
            if (d->viewManager->canvasBase()==&d->canvas)
                d->viewManager->setCurrentView(nullptr);
        }
#endif''',
           "KisView native input teardown")
    vc=one(vc,'KisMainWindow * KisView::mainWindow() const',
           '''#ifdef Q_OS_ANDROID
void KisView::setMobileNativeSaveHost(MobileKritaNativeSaveHost *host)
{
    d->mobileNativeSaveHost=host;
}
MobileKritaNativeSaveHost *KisView::mobileNativeSaveHost() const
{
    return d->mobileNativeSaveHost.data();
}
#endif

KisMainWindow * KisView::mainWindow() const''',
           "KisView native save host methods")
    vc=one(vc,'''            if (!viewManager()->mainWindow()->saveDocument(document(), !isNative, false))
                return false;''',
           '''#ifdef Q_OS_ANDROID
            if (!viewManager()->mainWindow()) {
                // Without a live injected host, refuse Save and retain
                // the modified painting. Never silently discard it.
                if (!d->mobileNativeSaveHost ||
                    !d->mobileNativeSaveHost->save(document(), !isNative))
                    return false;
            } else
#endif
            if (!viewManager()->mainWindow()->saveDocument(document(), !isNative, false))
                return false;''',
           "KisView native close Save")
    vc=one(vc,'''void KisView::dropEvent(QDropEvent *event)
{
    dbgUI << Q_FUNC_INFO;''',
           '''void KisView::dropEvent(QDropEvent *event)
{
#ifdef Q_OS_ANDROID
    if (!mainWindow()) {
        // Desktop drop operations directly manipulate KisMainWindow.
        // Refuse those operations in the independent host until they
        // route through its document/service ports.
        event->ignore();
        return;
    }
#endif
    dbgUI << Q_FUNC_INFO;''',
           "KisView guarded native drop")
    vc=one(vc,'''void KisView::closeView()
{
    d->subWindow->close();
}''',
           '''void KisView::closeView()
{
#ifdef Q_OS_ANDROID
    if (!d->subWindow) {
        // Native stack does not create desktop QMdiSubWindow wrappers.
        // The view still runs its own Save/Discard/Cancel closeEvent.
        if (close()) deleteLater();
        return;
    }
#endif
    d->subWindow->close();
}''',
           "KisView native close action")
    vc=one(vc,'''    config.setProperty("window", mainWindow()->windowStateConfig().name());''',
           '''#ifdef Q_OS_ANDROID
    if (KisMainWindow *desktop=mainWindow())
        config.setProperty("window", desktop->windowStateConfig().name());
#else
    config.setProperty("window", mainWindow()->windowStateConfig().name());
#endif''',
           "KisView standalone view state")
    viewcpp.write_text(vc,encoding="utf-8")

    partcpp=root/"libs/ui/KisPart.cpp"
    pc=partcpp.read_text(encoding="utf-8")
    pc=one(pc,'#include "KisView.h"',
           '#include "KisView.h"\n#ifdef Q_OS_ANDROID\n#include "native_krita_save_host.h"\n#endif',
           "KisPart native host include")
    pc=one(pc,'    KIS_ASSERT_RECOVER_RETURN(!view->mainWindow()->hackIsSaving());',
           '''    if (KisMainWindow *desktop=view->mainWindow()) {
        KIS_ASSERT_RECOVER_RETURN(!desktop->hackIsSaving());
    }
#ifdef Q_OS_ANDROID
    else if (auto *host=view->mobileNativeSaveHost()) {
        KIS_ASSERT_RECOVER_RETURN(!host->saving());
    }
#endif''',
           "KisPart host-neutral view removal guard")
    partcpp.write_text(pc,encoding="utf-8")

    managercpp=root/"libs/ui/KisViewManager.cpp"
    mc=managercpp.read_text(encoding="utf-8")
    mc=one(mc,'''QWidget *KisViewManager::mainWindowAsQWidget() const
{
    return mainWindow();
}''',
           '''QWidget *KisViewManager::mainWindowAsQWidget() const
{
    // The view manager already supports a plain QMainWindow host.
    // Do not lose the dialog parent by forcing KisMainWindow here.
    return qtMainWindow();
}''',
           "Qt-native lightweight dialog parent")
    mc=one(mc,'''int KisViewManager::viewCount() const
{
    KisMainWindow *mw  = qobject_cast<KisMainWindow*>(d->mainWindow);
    if (mw) {
        return mw->viewCount();
    }
    return 0;
}''',
           '''int KisViewManager::viewCount() const
{
    KisMainWindow *mw  = qobject_cast<KisMainWindow*>(d->mainWindow);
    if (mw) {
        return mw->viewCount();
    }
#ifdef Q_OS_ANDROID
    // A native Qt window has no MDI; count only registered Krita
    // engine views belonging to this specific view manager.
    int count=0;
    if (KisPart::instance()) {
        for (const QPointer<KisView> &candidate : KisPart::instance()->views()) {
            if (candidate && candidate->viewManager()==this)
                ++count;
        }
    }
    return count;
#else
    return 0;
#endif
}''',
           "Krita native view count")
    mc=one(mc,
           '    bool toggled = actionCollection()->action("view_show_canvas_only")->isChecked();',
           '''    // In the standalone QMainWindow, the desktop canvas-only
    // toggle action may not exist. Never crash while activating
    // the very first real KisView.
    QAction *canvasOnlyAction=actionCollection()?
        actionCollection()->action("view_show_canvas_only"):nullptr;
    bool toggled=canvasOnlyAction && canvasOnlyAction->isChecked();''',
           "Native KisViewManager missing desktop canvas-only action")
    # Keep diagnostic checkpoints inside KisViewManager as well as the
    # launcher. A crash in any of these initialization stages should not
    # require Android logcat to distinguish from the earlier null statusbar.
    mc=one(mc,'#include <QPrinter>',
           '#include <QPrinter>\n#ifdef Q_OS_ANDROID\n#include <QSettings>\n#endif',
           "KisViewManager Android diagnostic include")
    mc=one(mc,
           '''    createActions();
    setupManagers();''',
           '''#ifdef Q_OS_ANDROID
    auto mobileInitStage=[this](const char *name) {
        if (mainWindow()) return; // Desktop engine is unchanged.
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/standaloneLastPhase"),
                          QString::fromLatin1(name));
        settings.sync();
    };
    mobileInitStage("KisViewManager: creating actions");
#endif
    createActions();
#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up managers");
#endif
    setupManagers();''',
           "KisViewManager isolated creation checkpoints")
    mc=one(mc,'    d->statusBar.setup();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up status bar");
#endif
    d->statusBar.setup();''',
           "KisViewManager statusbar checkpoint")
    mc=one(mc,'    d->controlFrame.setup(parent);',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up brush controls");
#endif
    d->controlFrame.setup(parent);''',
           "KisViewManager brush control checkpoint")
    mc=one(mc,'    KoToolManager::instance()->initializeToolActions();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: initializing tool actions");
#endif
    KoToolManager::instance()->initializeToolActions();''',
           "KisViewManager tool action checkpoint")
    mc=one(mc,'    KisInputProfileManager::instance()->loadProfiles();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: loading input profiles");
#endif
    KisInputProfileManager::instance()->loadProfiles();''',
           "KisViewManager input profile checkpoint")
    mc=one(mc,'    new ImageSize(this);',
           '''    new ImageSize(this);
#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: constructor complete");
#endif''',
           "KisViewManager finished checkpoint")
    managercpp.write_text(mc,encoding="utf-8")

    # KisStatusBar::setup() runs inside KisViewManager's constructor.
    # It dereferences KisMainWindow, which is intentionally absent in
    # our independent QMainWindow host. Android uses the Qt-native
    # host's own status bar; desktop Krita keeps its original code.
    statusbarfile=root/"libs/ui/kis_statusbar.cc"
    sb=statusbarfile.read_text(encoding="utf-8")
    sb=one(sb,'#include <QStatusBar>',
           '#include <QStatusBar>\n#ifdef Q_OS_ANDROID\n#include <QMainWindow>\n#include <QDebug>\n#endif',
           "KisStatusBar Qt host includes")
    sb=one(sb,
           '    m_statusBar = m_viewManager->mainWindow()->statusBar();',
           '''#ifdef Q_OS_ANDROID
    // mainWindow() returns a KisMainWindow* and is null for the
    // independent native canvas. qtMainWindow() retains QMainWindow.
    QMainWindow *qtHost=m_viewManager->qtMainWindow();
    if (!qtHost) {
        qWarning() << "Krita native status bar: no Qt window";
        return;
    }
    m_statusBar = qtHost->statusBar();
#else
    m_statusBar = m_viewManager->mainWindow()->statusBar();
#endif''',
           "KisStatusBar native Qt host")
    sb=one(sb,
           '    connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));',
           '''#ifdef Q_OS_ANDROID
    // themeChanged is a desktop KisMainWindow signal, not a
    // QMainWindow signal. Do not connect to a null desktop host.
    if (m_viewManager->mainWindow())
        connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));
#else
    connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));
#endif''',
           "KisStatusBar desktop-only signal guard")
    statusbarfile.write_text(sb,encoding="utf-8")

    # Krita 6.0.4 canvas uses a desktop window for color-management
    # operations, although its manager already stores QMainWindow.
    canvasfile=root/"libs/ui/canvas/kis_canvas2.cpp"
    canvas=canvasfile.read_text(encoding="utf-8")
    canvas=one(canvas,
               '    connect(mainWindow, SIGNAL(guiLoadingFinished()), SLOT(bootstrapFinished()));',
               '''    if (mainWindow)
        connect(mainWindow, SIGNAL(guiLoadingFinished()), SLOT(bootstrapFinished()));''',
               "KisCanvas2 independent host deferred bootstrap")
    canvas=one(canvas,
               'QWindow *mainWindowNativeWindow = m_d->view->mainWindow()->windowHandle();',
               '''QWidget *nativeHost=m_d->view->viewManager()->
            mainWindowAsQWidget();
        QWindow *mainWindowNativeWindow=nativeHost?
            nativeHost->windowHandle():nullptr;''',
               "native canvas color-managed surface parent")
    canvas=one(canvas,
               'QWidget *mainWindow = m_d->view->mainWindow();',
               '''QWidget *mainWindow = m_d->view->viewManager()->
        mainWindowAsQWidget();''',
               "native canvas config parent")
    canvasfile.write_text(canvas,encoding="utf-8")

    # EXPERIMENTAL engine-only startup: after KisApplication has
    # initialized its global engines, brush resources, plugin registries
    # and KisPart, it can create a plain Android QMainWindow instead of
    # calling KisPart::startBlankSession() -> KisMainWindow.
    # The existing V1 desktop-backed native startup is unchanged, and
    # engine-only mode requires a separate explicit opt-in.
    appfile=root/"libs/ui/KisApplication.cpp"
    app=appfile.read_text(encoding="utf-8")
    app=one(app,
        '''#include "KisAndroidDonations.h"''',
        '''#include "KisAndroidDonations.h"
#include "engine_only_startup_policy.h"
#include "engine_only_bootstrap.h"''',
        "Android engine-only application bootstrap headers")
    app=one(app,
        '''    KisPart *kisPart = KisPart::instance();
    if (needsMainWindow) {''',
        '''    KisPart *kisPart = KisPart::instance();
#ifdef Q_OS_ANDROID
    // Do not skip Krita's engine/resource/plugin initialization. Only
    // substitute its original desktop *window/session bootstrap*.
    // External file intents, batch mode and explicit sessions must
    // remain on the tested KisMainWindow path until native routing.
    const bool compatibleEngineOnlyBoot =
        needsMainWindow && showmainWindow && !d->batchRun &&
        !doNewImage && !doTemplate &&
        args.filenames().isEmpty() && args.fileLayer().isEmpty() &&
        args.session().isEmpty() && args.workspace().isEmpty() &&
        args.windowLayout().isEmpty() &&
        !args.canvasOnly() && !args.fullScreen() &&
        d->earlyRemoteArguments.isEmpty() &&
        d->earlyFileOpenEvents.isEmpty();
    QSettings engineOnlySettings;
    const auto engineOnlyDecision=MobileEngineOnlyStartupPolicy::begin(
        engineOnlySettings,compatibleEngineOnlyBoot);
    bool engineOnlyBoot=
        engineOnlyDecision==MobileEngineOnlyStartupPolicy::Decision::Launch;
    if(engineOnlyBoot && !MobileEngineOnlyBootstrap::launch()) {
        // A synchronous Qt-root failure recovers to desktop in this
        // same process; asynchronous failures recover next launch.
        MobileEngineOnlyStartupPolicy::complete(engineOnlySettings,false);
        engineOnlyBoot=false;
        qWarning() << "Engine-only root creation failed; starting desktop";
    }
#else
    const bool engineOnlyBoot=false;
#endif
    if (needsMainWindow && !engineOnlyBoot) {''',
        "replace desktop KisPart session bootstrap when engine-only selected")
    app=one(app,
        '''    if (!d->batchRun) {
        checkAutosaveFiles();
    }''',
        '''    if (!d->batchRun && !engineOnlyBoot) {
        checkAutosaveFiles();
    }''',
        "defer desktop autosave manager while engine-only")
    # A native Android document intent might be queued after the
    # eligibility check. Never dereference a null desktop mainWindow.
    app=one(app,
        '''            d->mainWindow->openDocument(fileName, QFlags<KisMainWindow::OpenFlag>());''',
        '''            if (d->mainWindow)
                d->mainWindow->openDocument(
                    fileName,QFlags<KisMainWindow::OpenFlag>());
            else
                qWarning() << "Engine-only file intent requires native"
                           << " routing; reopen this file through Open:" << fileName;''',
        "guard late Android open intent without desktop window")
    app=one(app,
        '''    Q_FOREACH(const QByteArray &message, d->earlyRemoteArguments) {
        executeRemoteArguments(message, d->mainWindow);
    }''',
        '''    Q_FOREACH(const QByteArray &message, d->earlyRemoteArguments) {
        if (d->mainWindow)
            executeRemoteArguments(message,d->mainWindow);
        else
            qWarning() << "Engine-only remote intent requires native"
                       << " routing; use the independent Open command";
    }''',
        "guard late remote document intent without desktop window")
    appfile.write_text(app,encoding="utf-8")

    mainfile=root/"libs/ui/KisMainWindow.cpp"
    s=mainfile.read_text(encoding="utf-8")
    s=one(s,'#include "tablet_compact_skin.h"',
          '#include "tablet_compact_skin.h"\n#include "canvas_session.h"\n#include "krita_standalone_pilot.h"\n#include "lightweight_canvas_host.h"\n#include "desktop_host_adapters.h"\n#include "mdi_view_host_adapter.h"\n#include "native_startup_policy.h"\n#include "native_boot_coordinator.h"\n#include <QSettings>\n#include <QSharedPointer>',
          "main window include")

    # Decide on Android's native host as soon as the actual main-window
    # constructor begins, before allocating the desktop workspace model
    # or its view manager. This also arms crash recovery earlier than the
    # previous docker-only bootstrap gate.
    s=one(s,
          '''    KisScopedPerformanceLogger perfLog(QStringLiteral("KisMainWindow::KisMainWindow"));''',
          '''    KisScopedPerformanceLogger perfLog(QStringLiteral("KisMainWindow::KisMainWindow"));
#ifdef Q_OS_ANDROID
    QSettings nativeBootstrapSettings;
    const auto nativeBootDecision =
        MobileNativeStartupPolicy::beginOncePerProcess(nativeBootstrapSettings);
    setProperty("_mobileNativeBootDecision",int(nativeBootDecision));
    const bool mobileNativePreferred =
        nativeBootDecision==MobileNativeStartupPolicy::Decision::Launch;
    setProperty("_mobileNativeDesktopDeferred",mobileNativePreferred);
    qInfo() << "Krita native Qt shell selected:" << mobileNativePreferred;
#else
    const bool mobileNativePreferred = false;
#endif''',
          "early native host startup decision")

    # The Private constructor runs before KisMainWindow::KisMainWindow()
    # sets the process decision. Read the preference and the old pending
    # marker here so a recovery boot still constructs full desktop UI.
    s=one(s,
          '''        welcomeScroller = new QScrollArea();
        welcomeScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        welcomeScroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        welcomeScroller->setWidget(welcomePage);
        welcomeScroller->setWidgetResizable(true);

        widgetStack->addWidget(welcomeScroller);
        widgetStack->addWidget(mdiArea);
        mdiArea->setTabsMovable(true);
        mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
        mdiArea->setDocumentMode(true);
        mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation);
#ifdef HAVE_QMDIAREA_ALWAYS_SHOW_SUBWINDOW_TITLE
        mdiArea->setOption(QMdiArea::AlwaysShowSubwindowNameInTitleBar);
#endif /* HAVE_QMDIAREA_ALWAYS_SHOW_SUBWINDOW_TITLE */''',
          '''        bool nativeUiPreferred=false;
#ifdef Q_OS_ANDROID
        nativeUiPreferred=MobileNativeStartupPolicy::mayDeferDesktopUi(
            QSettings());
#endif
        if (nativeUiPreferred) {
            // Keep the original welcome widget, but not its desktop
            // scroll container or configured MDI tab presentation.
            widgetStack->addWidget(welcomePage);
        } else {
            welcomeScroller = new QScrollArea();
            welcomeScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            welcomeScroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            welcomeScroller->setWidget(welcomePage);
            welcomeScroller->setWidgetResizable(true);
            widgetStack->addWidget(welcomeScroller);
        }
        widgetStack->addWidget(mdiArea);
        if (!nativeUiPreferred) {
            mdiArea->setTabsMovable(true);
            mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
            mdiArea->setDocumentMode(true);
            mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation);
#ifdef HAVE_QMDIAREA_ALWAYS_SHOW_SUBWINDOW_TITLE
            mdiArea->setOption(QMdiArea::AlwaysShowSubwindowNameInTitleBar);
#endif /* HAVE_QMDIAREA_ALWAYS_SHOW_SUBWINDOW_TITLE */
        }''',
          "defer welcome scroll and MDI tab setup")

    # Kate's desktop command palette does not take part in rendering,
    # document saves, or the independent native action registry.
    # Its only entry point is KisMainWindow::openCommandBar(), so it can
    # be constructed lazily if the user returns to desktop commands.
    s=one(s,
          '''        commandBar = new KateCommandBar(parent);''',
          '''        if (!nativeUiPreferred)
            commandBar = new KateCommandBar(parent);''',
          "defer legacy desktop command palette on native bootstrap")
    s=one(s,
          '''void KisMainWindow::openCommandBar()
{
    QList<KisKActionCollection *> actionCollections;''',
          '''void KisMainWindow::openCommandBar()
{
    // A reduced Android startup does not allocate the desktop
    // Kate command palette. Restore it if an old command action
    // explicitly requests it after the native host is closed.
    if (!d->commandBar)
        d->commandBar = new KateCommandBar(this);
    QList<KisKActionCollection *> actionCollections;''',
          "restore desktop command palette only when opened")

    # Krita's detachable-canvas companion is disabled on Android,
    # but the desktop KisMainWindow constructor still allocates a
    # KisCanvasWindow and associates it with the full action registry.
    # The native Krita host has its OWN canvas and never needs this
    # extra window. Construct it only when a legacy caller requests
    # canvasWindow(), including during the original desktop close path.
    s=one(s,
          '''    d->canvasWindow = new KisCanvasWindow(this);
    actionCollection()->addAssociatedWidget(d->canvasWindow);''',
          '''    if (!mobileNativePreferred) {
        d->canvasWindow = new KisCanvasWindow(this);
        actionCollection()->addAssociatedWidget(d->canvasWindow);
    }''',
          "skip unused detached desktop canvas window on native startup")
    s=one(s,
          '''QWidget * KisMainWindow::canvasWindow() const
{
    return d->canvasWindow;
}''',
          '''QWidget * KisMainWindow::canvasWindow() const
{
#ifdef Q_OS_ANDROID
    if (!d->canvasWindow) {
        // Restore the legacy companion on demand. This is not used
        // for painting or KRA documents in the independent host.
        auto *self=const_cast<KisMainWindow*>(this);
        d->canvasWindow = new KisCanvasWindow(self);
        self->actionCollection()->addAssociatedWidget(d->canvasWindow);
    }
#endif
    return d->canvasWindow;
}''',
          "lazily recreate detached canvas companion when requested")
    s=one(s,
          '''        d->canvasWindow->close();''',
          '''        canvasWindow()->close();''',
          "preserve legacy desktop close semantics through lazy accessor")

    # Resource and plugin registries are loaded by Krita's application
    # startup and by the independent view manager. Desktop workspace
    # menu tracking is not needed when that window is just the Android
    # native top-level container.
    s=one(s,
          '''    d->workspacemodel = new KisResourceModel(ResourceType::Workspaces, this);
    connect(d->workspacemodel, SIGNAL(modelReset()), this, SLOT(updateWindowMenu()));
    connect(d->workspacemodel, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(updateWindowMenu()));
    connect(d->workspacemodel, SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(updateWindowMenu()));''',
          '''    if (!mobileNativePreferred) {
        d->workspacemodel = new KisResourceModel(ResourceType::Workspaces, this);
        connect(d->workspacemodel, SIGNAL(modelReset()), this, SLOT(updateWindowMenu()));
        connect(d->workspacemodel, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(updateWindowMenu()));
        connect(d->workspacemodel, SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(updateWindowMenu()));
    }''',
          "omit desktop workspace menu model on native bootstrap")

    # Native view activation belongs to MobileNativeViewStack, not the
    # desktop QMdiArea. Preserve the latter only for recovery.
    s=one(s,
          '''    d->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    d->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    d->mdiArea->setTabPosition(QTabWidget::North);
    d->mdiArea->setTabsClosable(true);
    d->mdiArea->setAcceptDrops(true);''',
          '''    if (!mobileNativePreferred) {
        d->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        d->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        d->mdiArea->setTabPosition(QTabWidget::North);
        d->mdiArea->setTabsClosable(true);
        d->mdiArea->setAcceptDrops(true);
    }''',
          "defer unused desktop MDI controls")

    # Keep the painting engine, resource registration, tool factories
    # and plugin setup intact. The style picker and help-menu widgets
    # are strictly desktop presentation and are restored on fallback.
    s=one(s,
          '''    // Style menu actions
    d->styleActions = new QActionGroup(this);''',
          '''    // Style menu actions
    if (!mobileNativePreferred) {
    d->styleActions = new QActionGroup(this);''',
          "native bootstrap omits style picker begin")
    s=one(s,
          '''    connect(d->styleActions, SIGNAL(triggered(QAction*)),
            this, SLOT(slotUpdateWidgetStyle()));''',
          '''    connect(d->styleActions, SIGNAL(triggered(QAction*)),
            this, SLOT(slotUpdateWidgetStyle()));
    } // Desktop style picker is not needed by the independent host.''',
          "native bootstrap omits style picker end")
    s=one(s,
          '''    if (isHelpMenuEnabled() && !d->helpMenu) {''',
          '''    if (!mobileNativePreferred && isHelpMenuEnabled() && !d->helpMenu) {''',
          "native bootstrap omits desktop help menu")

    # When independent KisDocuments are opened, Krita's original
    # documentOpened signal would otherwise rebuild a desktop workspace
    # menu and traverse its resource list unnecessarily.
    s=one(s,
          '''void KisMainWindow::updateWindowMenu()
{''',
          '''void KisMainWindow::updateWindowMenu()
{
#ifdef Q_OS_ANDROID
    if (property("_mobileNativeDesktopDeferred").toBool())
        return; // Native Qt document switcher owns the visible UI.
#endif''',
          "skip desktop workspace menu updates while native")

    # During opted-in independent startup, Krita still provides its
    # resource registries, actions and one Android Qt window, but does
    # not need to instantiate every legacy desktop docker widget.
    s=one(s,
          '''    KoToolBoxFactory toolBoxFactory;
    QDockWidget *toolbox = createDockWidget(&toolBoxFactory);''',
          '''    KoToolBoxFactory toolBoxFactory;
    QDockWidget *toolbox = mobileNativePreferred ? nullptr :
                                            createDockWidget(&toolBoxFactory);''',
          "independent startup desktop toolbox bypass")
    s=one(s,
          '    if (cfg.toolOptionsInDocker()) {',
          '    if (!mobileNativePreferred && cfg.toolOptionsInDocker()) {',
          "independent startup desktop tool options bypass")
    s=one(s,
          '    for (KoDockFactoryBase *factory : KoDockRegistry::instance()->sortedDockWidgetFactories()) {',
          '    if (!mobileNativePreferred)\n    for (KoDockFactoryBase *factory : KoDockRegistry::instance()->sortedDockWidgetFactories()) {',
          "independent startup legacy docker factory bypass")
    native='''    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);'''
    injected='''    // The legacy controller action is registered inside createActions(),
    // while the native startup decision is computed earlier in the
    // KisMainWindow constructor. Read the Qt property across that seam.
    const bool mobileNativePreferred =
        property("_mobileNativeDesktopDeferred").toBool();
    QAction *mobilePilotAction = new QAction(
        KisIconUtils::loadIcon("draw-freehand"),
        i18n("Mobile Studio (experimental)"),this);
    mobilePilotAction->setCheckable(true);
    mobilePilotAction->setChecked(false);
    mobilePilotAction->setToolTip(i18n(
        "Open the independent mobile shell with Krita's native canvas"));
    actionCollection()->addAction(
        QStringLiteral("tablet_mobile_engine_pilot"),mobilePilotAction);
#ifdef Q_OS_ANDROID
    // Separate true-engine proof, never wired to normal Mobile Studio
    // startup. It constructs new KisView/KisDocument instances without
    // stealing the desktop canvas, and must be launched explicitly.
    QAction *standaloneTestAction=new QAction(
        i18n("Standalone Engine Test (experimental)"),this);
    standaloneTestAction->setToolTip(i18n(
        "Open a separate native Krita drawing canvas for engine validation"));
    actionCollection()->addAction(
        QStringLiteral("tablet_standalone_engine_test"),standaloneTestAction);
    connect(standaloneTestAction,&QAction::triggered,this,[this]() {
        // Avoid reentering a modal mobile menu. This launches a child
        // engine overlay inside our EXISTING Android native window.
        QTimer::singleShot(0,this,[this]() {
            if(!MobileKritaStandalonePilot::launch(this))
                QMessageBox::warning(
                    this,i18n("Standalone engine"),
                    i18n("Could not initialize the standalone Krita engine. "
                         "Your existing drawing is unchanged."));
        });
    });
    QAction *nativeStartupAction=new QAction(
        i18n("Start Independent Krita Engine on Next Launch"),this);
    nativeStartupAction->setCheckable(true);
    nativeStartupAction->setChecked(QSettings().value(
        QStringLiteral("MobileStudio/nativeStartupV1"),false).toBool());
    actionCollection()->addAction(
        QStringLiteral("tablet_native_engine_autostart"),
        nativeStartupAction);
    connect(nativeStartupAction,&QAction::toggled,this,[](bool on) {
        QSettings settings;
        MobileNativeStartupPolicy::setEnabled(settings,on);
    });
    QAction *standaloneStatusAction=new QAction(
        i18n("Last Standalone Engine Test Status"),this);
    actionCollection()->addAction(
        QStringLiteral("tablet_standalone_engine_status"),
        standaloneStatusAction);
    connect(standaloneStatusAction,&QAction::triggered,this,[this]() {
        QSettings settings;
        const QString phase=settings.value(
            QStringLiteral("MobileStudio/standaloneLastPhase"),
            QStringLiteral("No previous test")).toString();
        const bool pending=settings.value(
            QStringLiteral("MobileStudio/standaloneLaunchPending"),
            false).toBool();
        const QString lastPanel=settings.value(
            QStringLiteral("MobileStudio/nativeWorkspaceLastAction"),
            QStringLiteral("None")).toString();
        const QString nativeAudit=settings.value(
            QStringLiteral("MobileStudio/nativeDesktopWidgetAudit"),
            QStringLiteral("No native desktop audit recorded")).toString();
        QMessageBox::information(
            this,i18n("Standalone engine diagnostics"),
            i18n("Last startup checkpoint: %1\\n"
                 "Startup unfinished: %2\\n"
                 "Last native control: %3\\n"
                 "Native desktop widgets: %4\\n"
                 "If the app closed, report this text.",
                 phase,pending?i18n("Yes"):i18n("No"),lastPanel,nativeAudit));
    });
#endif
    TabletCompactSkin *compactSkin = nullptr;
    if (mobileNativePreferred) {
        // The real engine owns all visible drawing controls. Do not
        // allocate a second Krita-bridged Mobile Studio controller,
        // compact UI widget tree, or MDI document service.
        mobilePilotAction->setEnabled(false);
        qInfo() << "Legacy mobile controller skipped on native startup";
    } else {
    compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);
    auto returnToCompact=QSharedPointer<bool>::create(false);
    // Release old compact overlays before transferring the native canvas.
    connect(mobilePilotAction,&QAction::toggled,this,
            [this,tabletCompact,mobilePilotAction,returnToCompact](bool enabled) {
        if(enabled) {
            *returnToCompact=tabletCompact->isChecked();
            if(*returnToCompact)tabletCompact->setChecked(false);
        } else if(*returnToCompact) {
            *returnToCompact=false;
            // Wait for the presentation transition, not just canvas
            // ownership. In persistent-host mode the native canvas stays
            // embedded on Back and the Android flash workaround must
            // remain enabled, but Compact UI can still reactivate after
            // the Mobile Studio controls are hidden.
            QTimer::singleShot(180,this,[this,tabletCompact,mobilePilotAction]() {
                if(!tabletCompact || mobilePilotAction->isChecked() ||
                   !centralWidget() ||
                   property("_mobileStudioPresentationActive").toBool())
                    return;
                if(!tabletCompact->isChecked())
                    tabletCompact->setChecked(true);
            });
        }
    });
    const MobileHostServices mobileServices=
        MobileDesktopEngineBootstrap::create(
            this,d->viewManager,d->mdiArea);
    // UI and canvas lifecycle consume a host-neutral service bundle.
    // All legacy desktop dependencies are confined to the adapter.
    MobileAppController *mobileStudio=nullptr;
    if(mobileServices.ready()) {
        mobileStudio=new MobileAppController(
            this,mobileServices.documentRoot,
            MobileEngineBridge(mobileServices,d->viewManager),
            mobilePilotAction);
    } else {
        // Leave native Krita functional if its document root or host
        // services are unavailable during Android initialization.
        mobilePilotAction->setEnabled(false);
        qWarning() << "Mobile Studio host unavailable; native Krita preserved";
    }
    Q_UNUSED(mobileStudio);
    connect(tabletCompact,&QAction::toggled,this,
            [mobilePilotAction](bool compact) {
        if(compact && mobilePilotAction->isChecked())
            mobilePilotAction->setChecked(false);
    });
    } // Legacy Mobile Studio and Compact Skin are not created in native mode.
#ifdef Q_OS_ANDROID
    // Opt-in Android launch into a distinct Krita host. Still uses
    // Krita's application startup libraries, but no desktop MDI view.
    QTimer::singleShot(0,this,[this]() {
        const auto choice=static_cast<MobileNativeStartupPolicy::Decision>(
            property("_mobileNativeBootDecision").toInt());
        if(choice==MobileNativeStartupPolicy::Decision::Skip)return;
        if(choice==MobileNativeStartupPolicy::Decision::RecoverDesktop) {
            qWarning() << "Interrupted independent-host startup recovered";
            return;
        }
        // Independent Qt scheduling no longer captures KisMainWindow
        // or accesses a desktop MDI widget; this is its only hand-off.
        MobileNativeBootCoordinator::schedule(
            this,choice,
            [](QMainWindow *qtHost,
               MobileNativeBootCoordinator::Completion finished) {
                return MobileKritaStandalonePilot::launch(
                    qtHost,std::move(finished));
            });
    });
#endif'''
    s=one(s,native,injected,"pilot action initialization")
    # The upstream Compact Skin patch also inserts a signal connection
    # directly after the allocation. A skipped allocation must not
    # attach a null receiver (or later dereference it).
    s=one(s,
          '''    connect(tabletCompact, &QAction::toggled, compactSkin,
            [compactSkin](bool enabled) { compactSkin->setCompactActive(enabled); });''',
          '''    if (compactSkin)
        connect(tabletCompact, &QAction::toggled, compactSkin,
                [compactSkin](bool enabled) { compactSkin->setCompactActive(enabled); });''',
          "skip compact skin connection without a receiver")

    # Leave the desktop main-window layout unrestored until the next
    # full desktop boot; Android window sizing/orientation still runs.
    s=one(s,
          '''    QTabBar *tabBar = d->findTabBarHACK();
    if (tabBar) {
        tabBar->setElideMode(Qt::ElideRight);
        // load customized tab style
        customizeTabBar();
        tabBar->setExpanding(true);
        tabBar->setAcceptDrops(true);
        tabBar->setChangeCurrentOnDrag(true);
    }

    applyMainWindowSettings(d->windowStateConfig);''',
          '''    if (!mobileNativePreferred) {
        QTabBar *tabBar = d->findTabBarHACK();
        if (tabBar) {
            tabBar->setElideMode(Qt::ElideRight);
            // load customized tab style
            customizeTabBar();
            tabBar->setExpanding(true);
            tabBar->setAcceptDrops(true);
            tabBar->setChangeCurrentOnDrag(true);
        }
        applyMainWindowSettings(d->windowStateConfig);
    }''',
          "defer desktop MDI window layout restoration")

    # Krita Android's legacy display refresh hides and RE-SHOWS
    # KisMainWindow after a document is opened or created.  In Mobile
    # Studio that window is intentionally hidden while its live canvas
    # belongs to our independent window. Re-showing the original window
    # steals focus and covers the canvas with an empty desktop shell.
    # Guard both the initial call AND its deferred show() callback, since
    # a flash may have been scheduled just before switching windows.
    flash_old='''void KisMainWindow::slotFlashWindowHack()
{
    if (!d->flashWindowHackInProgress) {
        d->flashWindowHackInProgress = true;
        QTimer::singleShot(0, this, [this] {
            hide();
            QTimer::singleShot(0, this, [this] {
                show();
                d->flashWindowHackInProgress = false;
            });
        });
    }
}'''
    flash_new='''void KisMainWindow::slotFlashWindowHack()
{
    if ((property("_mobileStudioCanvasTransferred").toBool() ||
                 property("_mobileStudioPresentationActive").toBool() ||
                  property("_mobileNativeHostActive").toBool())) {
        return;
    }
    if (!d->flashWindowHackInProgress) {
        d->flashWindowHackInProgress = true;
        QTimer::singleShot(0, this, [this] {
            if ((property("_mobileStudioCanvasTransferred").toBool() ||
                 property("_mobileStudioPresentationActive").toBool() ||
                  property("_mobileNativeHostActive").toBool())) {
                d->flashWindowHackInProgress = false;
                return;
            }
            hide();
            QTimer::singleShot(0, this, [this] {
                if (!(property("_mobileStudioCanvasTransferred").toBool() ||
                 property("_mobileStudioPresentationActive").toBool() ||
                  property("_mobileNativeHostActive").toBool())) {
                    show();
                }
                d->flashWindowHackInProgress = false;
            });
        });
    }
}'''
    s=one(s,flash_old,flash_new,"Android desktop refresh protection")
    mainfile.write_text(s,encoding="utf-8")

    # Compact mode is normally enabled at startup; expose the opt-in pilot
    # from its toolbar as well as from the classic Krita toolbar.
    oldheader=root/"libs/ui/tablet_compact_skin.h"
    header=oldheader.read_text(encoding="utf-8")
    header=one(header,
        '        if(m_hubAction)m_compactBar->addAction(m_hubAction);',
        '''        if(m_hubAction)m_compactBar->addAction(m_hubAction);
        if(QAction *pilot=collection->action(
               QStringLiteral("tablet_mobile_engine_pilot")))
            m_compactBar->addAction(pilot);''',
        "compact-mode launch action")
    oldheader.write_text(header,encoding="utf-8")
    (root/"libs/ui/standalone_host.h").write_text(
        shell.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_session.h").write_text(
        session.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/mobile_startup_policy.h").write_text(
        startup_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/android_back_policy.h").write_text(
        back_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_transfer.h").write_text(
        transfer.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_canvas_chrome.h").write_text(
        native_chrome.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_canvas_pilot.h").write_text(
        native_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/lightweight_canvas_host.h").write_text(
        lightweight.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_view_stack.h").write_text(
        native_view_stack.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_krita_save_host.h").write_text(
        native_krita_save_host.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_native_view_factory.h").write_text(
        native_krita_factory.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_resume_observer.h").write_text(
        canvas_resume_observer.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_runtime.h").write_text(
        standalone_krita_runtime.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_documents.h").write_text(
        standalone_krita_documents.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_session.h").write_text(
        standalone_engine_session.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/mobile_native_engine_compile_probe.cpp").write_text(
        native_engine_probe.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_pilot.h").write_text(
        standalone_engine_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_startup_policy.h").write_text(
        native_startup_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_boot_coordinator.h").write_text(
        native_boot_coordinator.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_bootstrap_audit.h").write_text(
        native_bootstrap_audit.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_startup_policy.h").write_text(
        engine_only_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_root.h").write_text(
        engine_only_root.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.h").write_text(
        engine_only_bootstrap_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.cpp").write_text(
        engine_only_bootstrap_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_native_workspace.h").write_text(
        native_workspace.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_layer_reorder_policy.h").write_text(
        native_reorder_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_command_layout.h").write_text(
        native_command_layout.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/deferred_view_command.h").write_text(
        deferred_view_command.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_navigation.h").write_text(
        document_navigation.read_text(encoding="utf-8"),encoding="utf-8")
    # The earlier standalone headers were copied but NEVER parsed by
    # Krita's C++ compiler. Make an Android-only source file part of
    # the real kritaui library. This catches API and include regressions
    # without enabling or launching the unfinished standalone window.
    ui_cmake_file=root/"libs/ui/CMakeLists.txt"
    ui_cmake=ui_cmake_file.read_text(encoding="utf-8")
    ui_cmake=one(ui_cmake,
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        KisAndroidDonations.cpp''',
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        mobile_native_engine_compile_probe.cpp
        engine_only_bootstrap.cpp
        KisAndroidDonations.cpp''',
        "real Android kritaui engine compilation gate")
    ui_cmake_file.write_text(ui_cmake,encoding="utf-8")
    (root/"libs/ui/canvas_surface.h").write_text(
        surface.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_switcher.h").write_text(
        switcher.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_bridge.h").write_text(
        bridge.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_service.h").write_text(
        document_service.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_document_rollback.h").write_text(
        native_document_rollback.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_document_path_policy.h").write_text(
        native_document_path_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_host_port.h").write_text(
        document_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/view_host_port.h").write_text(
        view_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/view_identity_guard.h").write_text(
        view_guard.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/mdi_view_host_adapter.h").write_text(
        mdi_view_adapter.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_registry_port.h").write_text(
        command_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/desktop_host_adapters.h").write_text(
        desktop_adapters.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/host_services.h").write_text(
        host_services.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/mdi_view_identity.h").write_text(
        mdi_identity.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_service.h").write_text(
        command_service.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/brush_workspace.h").write_text(
        brush.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/color_workspace.h").write_text(
        color.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/layer_workspace.h").write_text(
        layers.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_browser.h").write_text(
        commands.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/quick_actions.h").write_text(
        quick.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/finger_pressure_calibration.h").write_text(
        pressure_calibration.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/finger_pressure_settings.h").write_text(
        pressure_settings.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/touch_reorder_list.h").write_text(
        reorder.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/tool_browser.h").write_text(
        tools.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_workflow.h").write_text(
        documents.read_text(encoding="utf-8"),encoding="utf-8")

    xmlfile=root/"krita/krita5.xmlgui"
    xml=xmlfile.read_text(encoding="utf-8")
    xml=one(xml,
        '    <Action name="tablet_compact_ui"/>',
        '''    <Action name="tablet_compact_ui"/>
    <Action name="tablet_mobile_engine_pilot"/>''',
        "native toolbar action")
    # The normal toolbar can be hidden by a saved Krita workspace.
    # Keep Mobile Studio directly recoverable in Settings after a
    # previous failed/disabled automatic launch.
    xml=one(xml,
        '    <Menu name="settings">\n      <text>Setti&amp;ngs</text>',
        '''    <Menu name="settings">
      <text>Setti&amp;ngs</text>
      <Action name="tablet_mobile_engine_pilot"/>
      <Action name="tablet_standalone_engine_test"/>
      <Action name="tablet_standalone_engine_status"/>
      <Action name="tablet_native_engine_autostart"/>
      <Separator/>''',
        "Settings menu mobile recovery launcher")
    xmlfile.write_text(xml,encoding="utf-8")
    cmakefile=root/"krita/CMakeLists.txt"
    cmake=cmakefile.read_text(encoding="utf-8")
    cmake=one(cmake,
        'set(KRITA5_XMLGUI_VERSION 548)',
        'set(KRITA5_XMLGUI_VERSION 553)',
        "XMLGUI version")
    oldhash=re.findall(r'set\(krita5_xmlgui_hash_expected "[0-9a-f]+"\)',cmake)
    if len(oldhash)!=1:
        raise RuntimeError(f"XMLGUI checksum anchor: {len(oldhash)} matches")
    digest=hashlib.md5(''.join(xml.split()).encode()).hexdigest()
    cmake=cmake.replace(oldhash[0],
        f'set(krita5_xmlgui_hash_expected "{digest}")',1)
    cmakefile.write_text(cmake,encoding="utf-8")
    print(f"Installed opt-in independent mobile WINDOW; XMLGUI checksum {digest}")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_mobile_shell.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
