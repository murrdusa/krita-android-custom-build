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

def redirect_android_engine_method(source:str, return_type:str, name:str, android_body:str):
    """Move Android resource/registry setup into a standalone engine TU.

    Keep the original upstream body for non-Android builds; the Android
    compiler only uses MobileKritaEngineInitialization's implementation.
    """
    signature=f"{return_type} KisApplication::{name}()"
    if source.count(signature)!=1:
        raise RuntimeError(f"Expected one Krita {name} function")
    at=source.index(signature)
    brace=source.index("{",at+len(signature))
    if source[at+len(signature):brace].strip():
        raise RuntimeError(f"Unexpected Krita {name} function syntax")
    depth=0
    end=brace
    while end<len(source):
        if source[end]=="{": depth+=1
        elif source[end]=="}":
            depth-=1
            if depth==0: break
        end+=1
    if depth!=0:
        raise RuntimeError(f"Unbalanced Krita {name} body")
    body=source[brace+1:end]
    old=source[at:end+1]
    replacement=(signature+"\n{\n#ifdef Q_OS_ANDROID\n"+
                 android_body+"\n#else"+body+"#endif\n}")
    return one(source,old,replacement,f"Android engine {name} extraction")


def main(root:Path):
    root=root.resolve()
    own=Path(__file__).resolve().parent
    native_chrome=own/"mobile_core/native_canvas_chrome.h"
    lightweight=own/"mobile_core/lightweight_canvas_host.h"
    native_view_stack=own/"mobile_core/native_view_stack.h"
    native_krita_save_host=own/"mobile_core/native_krita_save_host.h"
    native_krita_factory=own/"mobile_core/krita_native_view_factory.h"
    canvas_resume_observer=own/"mobile_core/canvas_resume_observer.h"
    standalone_krita_runtime=own/"mobile_core/krita_standalone_runtime.h"
    standalone_krita_documents=own/"mobile_core/krita_standalone_documents.h"
    saved_version_archive=own/"mobile_core/saved_version_archive.h"
    standalone_engine_session=own/"mobile_core/krita_engine_session.h"
    standalone_engine_pilot=own/"mobile_core/krita_standalone_pilot.h"
    engine_only_policy=own/"mobile_core/engine_only_startup_policy.h"
    engine_only_bootstrap_header=own/"mobile_core/engine_only_bootstrap.h"
    engine_only_bootstrap_source=own/"mobile_core/engine_only_bootstrap.cpp"
    engine_init_header=own/"mobile_core/krita_engine_initialization.h"
    engine_init_source=own/"mobile_core/krita_engine_initialization.cpp"
    app_entry_header=own/"mobile_core/krita_application_entry.h"
    app_entry_source=own/"mobile_core/krita_application_entry.cpp"
    android_runtime_header=own/"mobile_core/krita_android_runtime.h"
    android_runtime_source=own/"mobile_core/krita_android_runtime.cpp"
    android_intents_header=own/"mobile_core/android_document_intents.h"
    android_intents_source=own/"mobile_core/android_document_intents.cpp"
    native_workspace=own/"mobile_core/krita_native_workspace.h"
    native_reorder_policy=own/"mobile_core/native_layer_reorder_policy.h"
    native_command_layout=own/"mobile_core/native_command_layout.h"
    deferred_view_command=own/"mobile_core/deferred_view_command.h"
    document_navigation=own/"mobile_core/document_navigation.h"
    switcher=own/"mobile_core/document_switcher.h"
    bridge=own/"mobile_core/engine_bridge.h"
    document_service=own/"mobile_core/document_service.h"
    native_document_rollback=own/"mobile_core/native_document_rollback.h"
    native_document_path_policy=own/"mobile_core/native_document_path_policy.h"
    document_port=own/"mobile_core/document_host_port.h"
    view_port=own/"mobile_core/view_host_port.h"
    view_guard=own/"mobile_core/view_identity_guard.h"
    command_port=own/"mobile_core/command_registry_port.h"
    host_services=own/"mobile_core/host_services.h"
    command_service=own/"mobile_core/command_service.h"
    brush=own/"mobile_core/brush_workspace.h"
    color=own/"mobile_core/color_workspace.h"
    layers=own/"mobile_core/layer_workspace.h"
    commands=own/"mobile_core/command_browser.h"
    pressure_calibration=own/"mobile_core/finger_pressure_calibration.h"
    pressure_settings=own/"mobile_core/finger_pressure_settings.h"
    tools=own/"mobile_core/tool_browser.h"
    documents=own/"mobile_core/document_workflow.h"
    if not all(p.is_file() for p in
               (standalone_krita_runtime,standalone_krita_documents,native_krita_factory)):
        raise RuntimeError("Missing Mobile Studio engine module")

    # Studio widgets use the host-neutral engine and document ports.
    mobile_frontend=(standalone_engine_session,standalone_engine_pilot,engine_only_policy,
                     engine_only_bootstrap_header,engine_only_bootstrap_source,engine_init_header,
                     engine_init_source,app_entry_header,app_entry_source,android_runtime_header,
                     android_runtime_source,android_intents_header,android_intents_source,
                     native_command_layout,native_workspace,native_reorder_policy,canvas_resume_observer,
                     deferred_view_command,document_navigation,bridge,host_services,document_service,
                     native_document_rollback,native_document_path_policy,document_port,view_port,
                     view_guard,command_service,command_port,switcher,brush,color,layers,commands,
                     pressure_calibration,pressure_settings,saved_version_archive,tools,documents,
                     native_chrome,lightweight,native_view_stack,native_krita_save_host)
    for module in mobile_frontend:
        contents=module.read_text(encoding="utf-8")
        if ('#include "KisMainWindow.h"' in contents or
            '#include <KisMainWindow.h>' in contents or
            'legacyHost()' in contents):
            raise RuntimeError(
                f"Mobile Studio desktop UI dependency leaked into {module.name}")
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
#include "krita_engine_initialization.h"
#include "krita_application_entry.h"
#include "android_document_intents.h"''',
        "Android engine-only application bootstrap headers")
    # Preserve Android display-density scaling for the independent
    # process startup. Its singleton pointer lives on KisApplication;
    # expose only the small initialization operation to the new entry.
    appheader=root/"libs/ui/KisApplication.h"
    ah=appheader.read_text(encoding="utf-8")
    ah=one(ah,"class KisApplicationArguments;",
           "class KisApplicationArguments;\nclass KisConfig;",
           "Android scaling config forward declaration")
    ah=one(ah,
        "    KisAndroidScaling *androidScaling();",
        "    KisAndroidScaling *androidScaling();\n    void initializeAndroidScaling(KisConfig &cfg);",
        "Android scaling bootstrap API")
    appheader.write_text(ah,encoding="utf-8")
    scaling_anchor='''    d->androidScaling = new KisAndroidScaling(cfg, this);'''
    app=one(app,scaling_anchor,
        "    initializeAndroidScaling(cfg);",
        "use shared Android scaling init")
    marker='''KisAndroidScaling *KisApplication::androidScaling()'''
    if app.count(marker)!=1:
        raise RuntimeError("Missing Android scaling getter")
    start=app.index(marker)
    brace=app.index("{",start)
    depth=0
    end=brace
    while end<len(app):
        if app[end]=="{": depth+=1
        elif app[end]=="}":
            depth-=1
            if depth==0: break
        end+=1
    if depth!=0: raise RuntimeError("Unbalanced Android scaling getter")
    new_scaling='''

void KisApplication::initializeAndroidScaling(KisConfig &cfg)
{
#if KRITA_QT_HAS_ANDROID_QPLATFORMSCREEN_DENSITY_ADJUSTMENT
    if (!d->androidScaling)
        d->androidScaling = new KisAndroidScaling(cfg, this);
#else
    Q_UNUSED(cfg);
#endif
}
'''
    app=app[:end+1]+new_scaling+app[end+1:]
    # Engine registries are now compiled in their own Android module,
    # not as methods implemented by the desktop application's class.
    app=redirect_android_engine_method(
        app,"void","addResourceTypes",
        "    MobileKritaEngineInitialization::addResourceTypes();")
    app=redirect_android_engine_method(
        app,"void","loadPlugins",
        "    MobileKritaEngineInitialization::loadPlugins();")
    app=redirect_android_engine_method(
        app,"bool","registerResources",
        "    const bool ready=MobileKritaEngineInitialization::registerResources();\n"
        "    connect(KisResourceLocator::instance(), SIGNAL(progressMessage(const QString&)),\n"
        "            this, SLOT(setSplashScreenLoadingText(const QString&)));\n"
        "    return ready;")
    app=one(app,
        '''    KisPart *kisPart = KisPart::instance();
    if (needsMainWindow) {''',
        '''    KisPart *kisPart = KisPart::instance();
#ifdef Q_OS_ANDROID
    // Native Android startup is owned by the application entry module.
    const bool engineOnlyBoot=MobileKritaApplicationEntry::start();
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
    # Android's MainActivity forwards onNewIntent() through
    # KisApplication::fileOpenRequested. Send it directly to the native
    # Krita document host rather than an absent desktop main window.
    app=one(app,
        '''void KisApplication::fileOpenRequested(const QString &url)
{''',
        '''void KisApplication::fileOpenRequested(const QString &url)
{
#ifdef Q_OS_ANDROID
    if (MobileAndroidDocumentIntents::enqueue(url))
        return;
#endif''',
        "route Android file open to native engine")
    # The existing QtSingleApplication signal delivers files to an
    # already-running instance through remoteArguments().
    app=one(app,
        '''void KisApplication::remoteArguments(const QString &message)
{''',
        '''void KisApplication::remoteArguments(const QString &message)
{
#ifdef Q_OS_ANDROID
    if (MobileAndroidDocumentIntents::active()) {
        QByteArray serialized=QByteArray::fromBase64(
            message.toLatin1());
        const KisApplicationArguments incoming=
            KisApplicationArguments::deserialize(serialized);
        if(incoming.filenames().isEmpty())
            qWarning() << "Mobile Studio: remote request has no file paths";
        for(const QString &path:incoming.filenames())
            MobileAndroidDocumentIntents::enqueue(path);
        return;
    }
#endif''',
        "route remote file launch to native engine")
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

    # Studio starts directly in its own host. Leave Krita's main-window
    # constructor and compact toolbar free of the obsolete embedded pilot.
    filter_file=root/"libs/ui/kis_filter_manager.cc"
    filters=filter_file.read_text(encoding="utf-8")
    filters=one(filters,
        "    if (!d->view->activeNode()->isEditable()) {",
        """    const KisNodeSP node = d->view->activeNode();
    if (!node || !d->view->image()) return;
    if (!node->isEditable()) {""",
        "guard native filters without an active drawing")
    filters=filters.replace("d->view->mainWindow()", "d->view->mainWindowAsQWidget()")
    filter_file.write_text(filters,encoding="utf-8")

    (root/"libs/ui/native_canvas_chrome.h").write_text(
        native_chrome.read_text(encoding="utf-8"),encoding="utf-8")
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
    (root/"libs/ui/saved_version_archive.h").write_text(
        saved_version_archive.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_session.h").write_text(
        standalone_engine_session.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_pilot.h").write_text(
        standalone_engine_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_startup_policy.h").write_text(
        engine_only_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.h").write_text(
        engine_only_bootstrap_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.cpp").write_text(
        engine_only_bootstrap_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_initialization.h").write_text(
        engine_init_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_initialization.cpp").write_text(
        engine_init_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_application_entry.h").write_text(
        app_entry_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_application_entry.cpp").write_text(
        app_entry_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_android_runtime.h").write_text(
        android_runtime_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_android_runtime.cpp").write_text(
        android_runtime_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/android_document_intents.h").write_text(
        android_intents_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/android_document_intents.cpp").write_text(
        android_intents_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_native_workspace.h").write_text(
        native_workspace.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_layer_reorder_policy.h").write_text(
        native_reorder_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_command_layout.h").write_text(
        native_command_layout.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/context_tool_bar.h").write_text(
        (own/"mobile_core/context_tool_bar.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/deferred_view_command.h").write_text(
        deferred_view_command.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_navigation.h").write_text(
        document_navigation.read_text(encoding="utf-8"),encoding="utf-8")
    # Compile the live Studio entry and its engine into kritaui on Android.
    ui_cmake_file=root/"libs/ui/CMakeLists.txt"
    ui_cmake=ui_cmake_file.read_text(encoding="utf-8")
    ui_cmake=one(ui_cmake,
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        KisAndroidDonations.cpp''',
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        engine_only_bootstrap.cpp
        krita_engine_initialization.cpp
        krita_application_entry.cpp
        krita_android_runtime.cpp
        android_document_intents.cpp
        KisAndroidDonations.cpp''',
        "real Android kritaui engine compilation gate")
    ui_cmake_file.write_text(ui_cmake,encoding="utf-8")
    (root/"libs/ui/document_switcher.h").write_text(
        switcher.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_home.h").write_text(
        (own/"mobile_core/document_home.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/mobile_draft_recovery.h").write_text(
        (own/"mobile_core/mobile_draft_recovery.h").read_text(encoding="utf-8"),
        encoding="utf-8")
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
    (root/"libs/ui/command_registry_port.h").write_text(
        command_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/host_services.h").write_text(
        host_services.read_text(encoding="utf-8"),encoding="utf-8")
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
    (root/"libs/ui/finger_pressure_calibration.h").write_text(
        pressure_calibration.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/finger_pressure_settings.h").write_text(
        pressure_settings.read_text(encoding="utf-8"),encoding="utf-8")
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
    # Android gets its own real native application target and entry file.
    # KDE's Android deploy tool names the entry JNI shared library using
    # the native CMake target. Keep target, library, manifest and APK
    # packaging folder all aligned to mobile_studio. The Java package
    # namespace stays org.krita for the existing JNI bridge.
    cmake=one(cmake,
        'set(krita_SRCS main.cc)',
        '''if(ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    set(krita_SRCS mobile_studio_main.cc)
    set(KRITA_APPLICATION_TARGET mobile_studio)
    set(QTANDROID_EXPORTED_TARGET mobile_studio CACHE STRING
        "Android native application target" FORCE)
    # Parent CMake creates create-apk after add_subdirectory(krita).
    # CACHE FORCE does not replace an inherited normal-scope variable.
    set(QTANDROID_EXPORTED_TARGET mobile_studio PARENT_SCOPE)
else()
    set(krita_SRCS main.cc)
    set(KRITA_APPLICATION_TARGET krita)
endif()''',
        "Native Android entrypoint source target")
    cmake=one(cmake,
        '''if (ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    kis_add_library(krita SHARED ${krita_SRCS})
    target_link_libraries(krita PRIVATE Qt5::AndroidExtras)
elseif (WIN32)''',
        '''if (ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    kis_add_library(mobile_studio SHARED ${krita_SRCS})
    target_link_libraries(mobile_studio PRIVATE Qt5::AndroidExtras)
elseif (WIN32)''',
        "Native Mobile Studio Android build target")
    cmake=one(cmake,
        '''target_link_libraries(krita
    PRIVATE''',
        '''target_link_libraries(${KRITA_APPLICATION_TARGET}
    PRIVATE''',
        "Link real native application target")
    cmake=one(cmake,
        '''    install(TARGETS krita ${INSTALL_TARGETS_DEFAULT_ARGS})
endif()
install(FILES org.kde.krita.desktop''',
        '''    install(TARGETS ${KRITA_APPLICATION_TARGET} ${INSTALL_TARGETS_DEFAULT_ARGS})
endif()
install(FILES org.kde.krita.desktop''',
        "Install native Mobile Studio artifact")
    cmakefile.write_text(cmake,encoding="utf-8")
    # The Android process now enters Mobile Studio's own startup
    # routine directly from main(), skipping KisApplication::start on
    # ordinary launches. Krita's Qt event dispatcher remains in place.
    mainfile=root/"krita/main.cc"
    entry=mainfile.read_text(encoding="utf-8")
    entry=one(entry,
        '#include <KisApplication.h>',
        '''#include <KisApplication.h>
#include <krita_android_runtime.h>''',
        "Mobile-only process bootstrap include")
    entry=one(entry,
        '''    if (!app.start(args)) {
        KisUsageLogger::log("Could not start Krita Application");
        return 1;
    }''',
        '''    // Compiled only by the native mobile_studio Android target.
    // Never enter the original desktop session or recovery path.
    if (MobileKritaAndroidRuntime::launch(app,args) !=
        MobileKritaAndroidRuntime::Result::Started) {
        KisUsageLogger::log("Could not start Mobile Studio");
        return 1;
    }''',
        "Mobile-only native entry without desktop fallback")
    # The original desktop main.cc remains untouched; Android's native
    # library compiles an independent app-specific translation unit.
    (root/"krita/mobile_studio_main.cc").write_text(entry,encoding="utf-8")

    # The original KDE packaging script hardcodes the default CMake
    # export directory "krita_build_apk". Our actual native app target
    # creates "mobile_studio_build_apk" instead.
    packagefile=root/"build-tools/ci-scripts/build-android-package.py"
    package=packagefile.read_text(encoding="utf-8")
    package=one(package,
        "repackagePath = os.path.join(buildPath, 'krita_build_apk')",
        "repackagePath = os.path.join(buildPath, 'mobile_studio_build_apk')",
        "Use independent Android deploy/Gradle directory")
    packagefile.write_text(package,encoding="utf-8")

    # Package Mobile Studio as an independently installable Android app.
    # Keep Krita's Qt/Java namespace; changing only applicationId avoids
    # rewriting the JNI bindings and Java activity class paths.
    gradlefile=root/"packaging/android/apk/build.gradle"
    gradle=gradlefile.read_text(encoding="utf-8")
    gradle=one(gradle,
        '    defaultConfig {\n        compileSdk androidCompileSdkVersion.toInteger()',
        '    defaultConfig {\n        applicationId "org.krita.mobilestudio"\n'
        '        compileSdk androidCompileSdkVersion.toInteger()',
        "Mobile Studio app identifier")
    gradlefile.write_text(gradle,encoding="utf-8")

    manifestfile=root/"packaging/android/apk/AndroidManifest.xml"
    manifest=manifestfile.read_text(encoding="utf-8")
    manifest=one(manifest,
        'android:label="Krita"\n                 android:icon=',
        'android:label="Krita Mobile Studio"\n                 android:icon=',
        "Mobile Studio application label")
    manifest=one(manifest,
        'android:value="krita"/>',
        'android:value="mobile_studio"/>',
        "Qt Android loads independent entrypoint shared library")
    manifest=one(manifest,
        'android:label="Krita"\n                  android:screenOrientation=',
        'android:label="Krita Mobile Studio"\n                  android:screenOrientation=',
        "Mobile Studio activity label")
    manifest=one(manifest,
        'android:name=".android.DocumentSaverService"',
        'android:name="org.krita.android.DocumentSaverService"',
        "fully qualified save service")
    # Android ACTION_VIEW/EDIT uses Intent.getData(); sharing through
    # ACTION_SEND uses EXTRA_STREAM. Enable both without new desktop UI.
    manifest=one(manifest,
        '''            <intent-filter>
                <action android:name="com.samsung.android.support.REMOTE_ACTION" />''',
        '''            <intent-filter>
                <action android:name="android.intent.action.SEND" />
                <category android:name="android.intent.category.DEFAULT" />
                <data android:mimeType="image/*" />
                <data android:mimeType="application/x-krita" />
                <data android:mimeType="application/octet-stream" />
            </intent-filter>
            <intent-filter>
                <action android:name="com.samsung.android.support.REMOTE_ACTION" />''',
        "Android Share single-document intent filter")
    manifestfile.write_text(manifest,encoding="utf-8")

    # The older quick-pinch recognizer still runs in Krita's input manager,
    # but its view-reset callback only searched for a desktop KisMainWindow.
    # Keep the exact gesture thresholds and native desktop behavior, while
    # routing reset to the standalone view manager when no desktop window
    # exists. Input events remain owned by Krita's existing recognizer.
    pinchfile=root/"libs/ui/input/kis_input_manager.cpp"
    pinch_source=pinchfile.read_text(encoding="utf-8")
    pinch_source=one(pinch_source,
        "#include <KisMainWindow.h>\n",
        '#include <KisMainWindow.h>\n#include "krita_android_runtime.h"\n',
        "standalone quick pinch include")
    pinch_source=one(pinch_source,
        """                KisMainWindow *window = KisPart::instance()->currentMainwindow();
                if (!window) return;
                QAction *rotation = window->actionCollection()->action(""",
        """                KisMainWindow *window = KisPart::instance()->currentMainwindow();
                if (!window) {
                    MobileKritaAndroidRuntime::resetCanvasView();
                    return;
                }
                QAction *rotation = window->actionCollection()->action(""",
        "standalone quick pinch reset")
    pinchfile.write_text(pinch_source,encoding="utf-8")

    # Preserve MainActivity's native Qt/JNI intent delivery; only widen
    # URI extraction to include Android's single-file Share operation.
    activityfile=root/"packaging/android/apk/src/org/krita/android/MainActivity.java"
    activity=activityfile.read_text(encoding="utf-8")
    activity=one(activity,
        '''            Uri fileUri = intent.getData();
            if (fileUri != null) {''',
        '''            Uri fileUri = intent.getData();
            if (fileUri == null && Intent.ACTION_SEND.equals(intent.getAction())) {
                Object stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (stream instanceof Uri) fileUri = (Uri) stream;
            }
            if (fileUri != null) {''',
        "Android single Share URI extraction")
    # SAF URIs are not filesystem paths. Extract only Krita's embedded
    # preview PNG from a document stream, instead of asking the desktop
    # welcome page to open the entire project for a thumbnail.
    activity=one(activity,
        "    private static final String TAG",
        r'''    public static String tabletProjectRoots() {
        org.json.JSONArray roots = new org.json.JSONArray();
        roots.put(Environment.getExternalStorageDirectory().getAbsolutePath());
        MainActivity activity = tabletPermissionActivity;
        if (activity != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.os.storage.StorageManager storage =
                activity.getSystemService(android.os.storage.StorageManager.class);
            if (storage != null) for (android.os.storage.StorageVolume volume : storage.getStorageVolumes()) {
                java.io.File directory = volume.getDirectory();
                if (directory != null && !volume.isPrimary()) roots.put(directory.getAbsolutePath());
            }
        }
        return roots.toString();
    }

    public static void tabletRememberProjectAccess(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return;
        Uri uri = Uri.parse(location);
        if (!android.provider.DocumentsContract.isDocumentUri(activity, uri)) return;
        try {
            activity.getContentResolver().takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some providers offer only temporary access.
        }
    }

    public static boolean tabletRememberProjectFolder(Intent result) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null || result == null || result.getData() == null) return false;
        Uri tree = result.getData();
        if (!DocumentsContract.isTreeUri(tree)) return false;
        int flags = result.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if ((flags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) return false;
        try {
            activity.getContentResolver().takePersistableUriPermission(tree, flags);
            return true;
        } catch (SecurityException denied) {
            return false;
        }
    }

    private static boolean tabletIsFolderGrant(Uri uri) {
        return DocumentsContract.isTreeUri(uri) && uri.getPathSegments().size() == 2;
    }

    public static String tabletProjectFolders() {
        org.json.JSONArray folders = new org.json.JSONArray();
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return folders.toString();
        for (android.content.UriPermission grant : activity.getContentResolver().getPersistedUriPermissions()) {
            Uri uri = grant.getUri();
            if (!grant.isReadPermission() || !tabletIsFolderGrant(uri)) continue;
            try {
                org.json.JSONObject folder = new org.json.JSONObject();
                folder.put("uri", uri.toString());
                folder.put("name", DocumentsContract.getTreeDocumentId(uri) + "  (" + uri.getAuthority() + ")");
                folders.put(folder);
            } catch (Exception ignored) {
                // Stale grant.
            }
        }
        return folders.toString();
    }

    public static boolean tabletForgetProjectFolder(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null || location == null) return false;
        Uri tree = Uri.parse(location);
        if (!tabletIsFolderGrant(tree)) return false;
        android.content.ContentResolver resolver = activity.getContentResolver();
        for (android.content.UriPermission grant : resolver.getPersistedUriPermissions()) {
            if (!tree.equals(grant.getUri())) continue;
            int flags = 0;
            if (grant.isReadPermission()) flags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
            if (grant.isWritePermission()) flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            try {
                resolver.releasePersistableUriPermission(tree, flags);
                return true;
            } catch (SecurityException denied) {
                return false;
            }
        }
        return false;
    }

    private static final String[] tabletProjectColumns = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };

    private static void tabletAppendProject(org.json.JSONArray projects,
            java.util.Set<String> seen, Uri uri, android.database.Cursor row,
            String folder) throws org.json.JSONException {
        String name = row.getString(1);
        if (name == null) return;
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        if (!lower.endsWith(".kra") && !lower.endsWith(".krz") &&
            !"application/x-krita".equals(row.getString(2))) return;
        if (!seen.add(uri.getAuthority() + "/" + row.getString(0))) return;
        org.json.JSONObject item = new org.json.JSONObject();
        item.put("uri", uri.toString());
        item.put("name", name);
        item.put("folder", folder);
        item.put("modified", row.getLong(3));
        if ("com.android.externalstorage.documents".equals(uri.getAuthority())) {
            String id = row.getString(0);
            int colon = id.indexOf(':');
            if (colon > 0) {
                String volume = id.substring(0, colon);
                java.io.File root = "primary".equals(volume) ?
                    Environment.getExternalStorageDirectory() : new java.io.File("/storage", volume);
                item.put("localPath", new java.io.File(root, id.substring(colon + 1)).getPath());
            }
        }
        projects.put(item);
    }

    private static void tabletScanProjectTree(android.content.ContentResolver resolver,
            Uri tree, org.json.JSONArray projects, java.util.Set<String> seen) {
        java.util.ArrayDeque<android.util.Pair<String, String>> folders = new java.util.ArrayDeque<>();
        java.util.HashSet<String> visited = new java.util.HashSet<>();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        String rootName = tree.getAuthority();
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        try (android.database.Cursor row = resolver.query(root, tabletProjectColumns, null, null, null)) {
            if (row != null && row.moveToFirst() && row.getString(1) != null) rootName = row.getString(1);
        } catch (Exception ignored) {}
        folders.add(new android.util.Pair<>(rootId, rootName));
        while (!folders.isEmpty()) {
            android.util.Pair<String, String> folder = folders.removeFirst();
            if (!visited.add(folder.first)) continue;
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder.first);
            try (android.database.Cursor rows = resolver.query(children, tabletProjectColumns, null, null, null)) {
                if (rows == null) continue;
                while (rows.moveToNext()) {
                    String id = rows.getString(0);
                    if (id == null) continue;
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(rows.getString(2))) {
                        String name = rows.getString(1);
                        if (!".krita-gallery-trash".equals(name))
                            folders.add(new android.util.Pair<>(id, folder.second + "/" + name));
                    } else {
                        tabletAppendProject(projects, seen,
                            DocumentsContract.buildDocumentUriUsingTree(tree, id), rows, folder.second);
                    }
                }
            } catch (Exception ignored) {
                // An unavailable child folder does not invalidate the whole tree.
            }
        }
    }

    public static String tabletGrantedProjects() {
        org.json.JSONArray projects = new org.json.JSONArray();
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return projects.toString();
        android.content.ContentResolver resolver = activity.getContentResolver();
        java.util.Set<String> seen = new java.util.HashSet<>();
        for (android.content.UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (!permission.isReadPermission()) continue;
            Uri uri = permission.getUri();
            try {
                if (tabletIsFolderGrant(uri)) {
                    tabletScanProjectTree(resolver, uri, projects, seen);
                } else if (DocumentsContract.isDocumentUri(activity, uri)) {
                    try (android.database.Cursor row = resolver.query(uri, tabletProjectColumns, null, null, null)) {
                        if (row != null && row.moveToFirst())
                            tabletAppendProject(projects, seen, uri, row, uri.getAuthority());
                    }
                }
            } catch (Exception ignored) {
                // Grants may be stale or the provider may be offline.
            }
        }
        return projects.toString();
    }

    public static byte[] tabletKraPreview(String location) {
        MainActivity activity=tabletPermissionActivity;
        if(activity==null || location==null ||
           !location.startsWith("content://"))return null;
        try(java.io.InputStream input=activity.getContentResolver()
                .openInputStream(Uri.parse(location))) {
            if(input==null)return null;
            try(java.util.zip.ZipInputStream zip=new java.util.zip.ZipInputStream(
                    new java.io.BufferedInputStream(input,32768))) {
                for(int entries=0;entries<96;++entries) {
                    java.util.zip.ZipEntry entry=zip.getNextEntry();
                    if(entry==null)return null;
                    String name=entry.getName();
                    if("preview.png".equals(name) ||
                       "Thumbnails/thumbnail.png".equals(name)) {
                        java.io.ByteArrayOutputStream preview=
                            new java.io.ByteArrayOutputStream(32768);
                        byte[] buffer=new byte[16384];
                        int length;
                        while((length=zip.read(buffer))!=-1) {
                            if(preview.size()+length>8*1024*1024)return null;
                            preview.write(buffer,0,length);
                        }
                        return preview.toByteArray();
                    }
                    // Do not decompress huge merged-image entries just to
                    // reach a later thumbnail. Preview is normally early.
                    if(entry.getSize()>24L*1024*1024)return null;
                    zip.closeEntry();
                }
            }
        }catch(Exception ignored) {
            // URI access can be revoked or providers can expose bad ZIPs.
        }
        return null;
    }

    private static final String TAG''',
        "Android SAF embedded KRA thumbnail helper")
    if "import android.provider.DocumentsContract;" not in activity:
        activity=one(activity,"import android.net.Uri;",
            "import android.net.Uri;\nimport android.provider.DocumentsContract;",
            "Android document tree import")
    activityfile.write_text(activity,encoding="utf-8")

    debugManifest=root/"packaging/android/apk/flavors/debug/AndroidManifest.xml"
    debugXml=debugManifest.read_text(encoding="utf-8")
    debugXml=one(debugXml,'android:label="Krita Debug"',
                 'android:label="Krita Mobile Studio"',
                 "Mobile Studio debug label")
    debugManifest.write_text(debugXml,encoding="utf-8")
    text_tool_file=root/"plugins/tools/svgtexttool/SvgTextTool.cpp"
    text_tool=text_tool_file.read_text(encoding="utf-8")
    text_tool=one(text_tool,"#include <QMenuBar>",
        "#include <QMenuBar>\n#include <QPushButton>\n#include <QVBoxLayout>",
        "mobile SVG text controls includes")
    text_tool=one(text_tool,
        '    if (canvas2) {\n        canvas2->setCurrentShapeManagerOwnerShape(nullptr);',
        '    if (canvas2 && canvas2->viewManager()) {\n        canvas2->setCurrentShapeManagerOwnerShape(nullptr);',
        "SVG text view manager guard")
    text_tool=one(text_tool,
        '        QDockWidget *docker = canvas2->viewManager()->mainWindow()->dockWidget("TextProperties");\n        if (docker && m_optionManager) {\n            m_optionManager->setTextPropertiesOpen(docker->isVisible());\n        }',
        '        auto *mainWindow = canvas2->viewManager()->mainWindow();\n        auto *docker = mainWindow ? mainWindow->dockWidget("TextProperties") : nullptr;\n        if (m_optionManager) {\n            m_optionManager->setTextPropertiesOpen(docker && docker->isVisible());\n        }',
        "SVG text activation without a desktop window")
    text_tool=one(text_tool,
        '    if (optionWidget->errors().isEmpty()) {\n        optionWidget->rootObject()->setProperty("manager",',
        '    if (optionWidget->rootObject()) {\n        optionWidget->rootObject()->setProperty("manager",',
        "SVG text options QML root guard")
    text_tool=one(text_tool,
        '    if (canvas2 && canvas2->viewManager()->mainWindow()) {',
        '    if (canvas2 && canvas2->viewManager() && canvas2->viewManager()->mainWindow()) {',
        "SVG text advanced desktop controls guard")
    text_tool=one(text_tool,
        '    slotTextTypeUpdated();\n\n\n    return optionWidget;',
        '''    slotTextTypeUpdated();

#ifdef Q_OS_ANDROID
    if (canvas2 && canvas2->viewManager() && !canvas2->viewManager()->mainWindow()) {
        auto *panel = new QWidget;
        panel->setObjectName(toolId() + " option widget");
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QPushButton(i18n("Edit selected text"), panel);
        edit->setObjectName(QStringLiteral("mobileEditSvgText"));
        edit->setMinimumHeight(44);
        connect(edit, &QPushButton::clicked, this, &SvgTextTool::showEditor);
        layout->addWidget(edit);
        auto *glyphs = new QPushButton(i18n("Glyphs"), panel);
        glyphs->setObjectName(QStringLiteral("mobileSvgGlyphs"));
        glyphs->setMinimumHeight(44);
        connect(glyphs, &QPushButton::clicked, this, &SvgTextTool::showGlyphPalette);
        layout->addWidget(glyphs);
        if (optionWidget->rootObject()) {
            layout->addWidget(optionWidget);
        } else {
            delete optionWidget;
        }
        return panel;
    }
#endif
    return optionWidget;''',
        "standalone SVG text native editor controls")
    text_tool_file.write_text(text_tool,encoding="utf-8")

    text_dock_file=root/"plugins/dockers/textproperties/TextPropertiesDock.cpp"
    text_dock=text_dock_file.read_text(encoding="utf-8")
    text_dock=one(text_dock,
        '    if (!m_quickWidget->errors().empty()) {',
        '    setProperty("_mobileTextFormattingReady", m_quickWidget->rootObject() != nullptr);\n    if (!m_quickWidget->rootObject()) {',
        "text formatting QML root guard")
    text_dock_file.write_text(text_dock,encoding="utf-8")

    text_observer_file=root/"plugins/dockers/textproperties/TextPropertiesCanvasObserver.cpp"
    text_observer=text_observer_file.read_text(encoding="utf-8")
    text_observer=one(text_observer,
        "        // This initializes the docker to an empty entry;",
        '''        if (!kisview->mainWindow()) {
            // Studio opens the formatting panel after text may already be selected.
            // Copy the current values instead of writing an empty selection back.
            slotCanvasTextPropertiesChanged();
            slotCanvasCharacterPropertiesChanged();
            return;
        }
        // This initializes the docker to an empty entry;''',
        "initialize standalone text formatting without changing the selection")
    text_observer_file.write_text(text_observer,encoding="utf-8")
    transform_dir=root/"plugins/tools/tool_transform2"
    transform_header=transform_dir/"kis_tool_transform.h"
    transform=transform_header.read_text(encoding="utf-8")
    transform=one(transform,"#include <QPointer>","#include <QPointer>\n#include <optional>",
                  "pending Transform mode include")
    transform=one(transform,"    KisStrokeId m_strokeId;",
                  "    KisStrokeId m_strokeId;\n    std::optional<ToolTransformArgs::TransformMode> m_pendingTransformMode;",
                  "pending Transform mode field")
    transform_header.write_text(transform,encoding="utf-8")

    transform_source=transform_dir/"kis_tool_transform.cc"
    transform=transform_source.read_text(encoding="utf-8")
    transform=one(transform,"    if( mode != m_currentArgs.mode() ) {",
        "    if (m_optionsWidget && (mode != m_currentArgs.mode() || m_pendingTransformMode)) {",
        "allow replacing a queued Transform mode")
    transform=one(transform,
        "    ToolTransformArgs *config = m_transaction.currentConfig();\n    const ToolTransformArgs::TransformMode previousMode",
        """    // Initial stroke setup runs on the image worker. Its result would
    // replace any mode written before the transaction has arrived.
    if (m_strokeId && m_transaction.rootNodes().isEmpty()) {
        m_pendingTransformMode = mode;
        return;
    }
    ToolTransformArgs *config = m_transaction.currentConfig();
    const ToolTransformArgs::TransformMode previousMode""",
        "defer Transform mode changes while the stroke starts")
    transform=one(transform,"    initGuiAfterTransformMode();\n\n    if (m_transaction.hasInvisibleNodes())",
        """    initGuiAfterTransformMode();

    const auto requestedMode = m_pendingTransformMode;
    m_pendingTransformMode.reset();
    if (requestedMode && *requestedMode != m_currentArgs.mode()) {
        slotResetTransform(*requestedMode);
    }

    if (m_transaction.hasInvisibleNodes())""",
        "apply latest mode to the initialized Transform stroke")
    for method in ("endStroke","cancelStroke"):
        anchor=f"void KisToolTransform::{method}()\n{{"
        transform=one(transform,anchor,anchor+"\n    m_pendingTransformMode.reset();",
                      f"clear queued Transform mode on {method}")
    transform_source.write_text(transform,encoding="utf-8")
    draft_document_file=root/"libs/ui/KisDocument.cpp"
    draft_document=draft_document_file.read_text(encoding="utf-8")
    draft_document=one(draft_document,"#include <QFileInfo>",
        "#include <QFileInfo>\n#include <cstdio>","draft atomic rename include")
    draft_document=one(draft_document,
        "    if (!d->modified || !d->modifiedAfterAutosave) return;\n    const QString autoSaveFileName = generateAutoSaveFileName(localFilePath());",
        """    if (!d->modified || !d->modifiedAfterAutosave) return;
    const QString draftPath = property("_mobileDraftPath").toString();
    const QString autoSaveFileName = draftPath.isEmpty()
        ? generateAutoSaveFileName(localFilePath())
        : draftPath + QStringLiteral(".pending.kra");""",
        "write Studio autosave to a private staging file")
    draft_document=one(draft_document,
        "    Q_UNUSED(job);\n    Q_UNUSED(warningMessage);\n\n    const QString fileName = QFileInfo(job.filePath).fileName();",
        """    Q_UNUSED(warningMessage);

    const QString draftPath = property("_mobileDraftPath").toString();
    if (!draftPath.isEmpty() && job.filePath == draftPath + QStringLiteral(".pending.kra")) {
        if (!status.isOk() ||
            std::rename(QFile::encodeName(job.filePath).constData(),
                        QFile::encodeName(draftPath).constData()) != 0) {
            QFile::remove(job.filePath);
            d->modifiedAfterAutosave = true;
            if (status.isOk()) {
                setEmergencyAutoSaveInterval();
                qWarning() << "Could not commit Studio draft";
                return;
            }
        }
    }

    const QString fileName = QFileInfo(job.filePath).fileName();""",
        "atomically publish a completed private draft")
    draft_document=one(draft_document,
        "    // Eliminate any auto-save file\n    QString asf = generateAutoSaveFileName(autosaveBaseName);",
        """    const QString draftPath = property("_mobileDraftPath").toString();
    if (!draftPath.isEmpty()) {
        QFile::remove(draftPath);
        QFile::remove(draftPath + QStringLiteral(".pending.kra"));
    }
    // Eliminate any auto-save file
    QString asf = generateAutoSaveFileName(autosaveBaseName);""",
        "clear private draft on native Save or Discard")
    draft_document_file.write_text(draft_document,encoding="utf-8")
    print(f"Installed independent Mobile Studio app; XMLGUI checksum {digest}")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_mobile_shell.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
