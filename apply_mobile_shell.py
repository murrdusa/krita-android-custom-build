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
    standalone_krita_runtime=own/"mobile_core/krita_standalone_runtime.h"
    standalone_krita_documents=own/"mobile_core/krita_standalone_documents.h"
    native_engine_probe=own/"mobile_core/krita_native_engine_compile_probe.cpp"
    standalone_engine_pilot=own/"mobile_core/krita_standalone_pilot.h"
    deferred_view_command=own/"mobile_core/deferred_view_command.h"
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
               (shell,session,startup_policy,back_policy,transfer,native_chrome,native_pilot,lightweight,native_view_stack,native_krita_save_host,native_krita_factory,standalone_krita_runtime,standalone_krita_documents,native_engine_probe,standalone_engine_pilot,deferred_view_command,surface,switcher,bridge,document_service,native_document_rollback,native_document_path_policy,document_port,view_port,view_guard,mdi_view_adapter,command_port,desktop_adapters,host_services,mdi_identity,command_service,brush,color,layers,commands,quick,pressure_calibration,pressure_settings,
                reorder,tools,documents)):
        raise RuntimeError("Missing Mobile Studio source module")

    # Dependency boundary: only the backend services and temporary canvas
    # transfer controller may directly include Krita's desktop window.
    # Keep the mobile widgets/engine facade unaware of this legacy class,
    # so a future lightweight application host can replace it in one place.
    desktop_dependent=(desktop_adapters,mdi_view_adapter)
    qt_host_components=(session,back_policy,transfer,native_chrome,native_pilot,lightweight,native_view_stack,native_krita_save_host,surface)
    mobile_frontend=(deferred_view_command,shell,bridge,host_services,document_service,native_document_rollback,native_document_path_policy,document_port,view_port,view_guard,
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
    managercpp.write_text(mc,encoding="utf-8")

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

    mainfile=root/"libs/ui/KisMainWindow.cpp"
    s=mainfile.read_text(encoding="utf-8")
    s=one(s,'#include "tablet_compact_skin.h"',
          '#include "tablet_compact_skin.h"\n#include "canvas_session.h"\n#include "krita_standalone_pilot.h"\n#include "lightweight_canvas_host.h"\n#include "desktop_host_adapters.h"\n#include "mdi_view_host_adapter.h"\n#include <QSharedPointer>',
          "main window include")

    native='''    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);'''
    injected='''    // Independent window prototype is opt-in and never auto-starts.
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
        if(!MobileKritaStandalonePilot::launch())
            QMessageBox::warning(
                this,i18n("Standalone engine"),
                i18n("Could not initialize the standalone Krita engine. "
                     "Your existing drawing is unchanged."));
    });
#endif
    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);
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
    });'''
    s=one(s,native,injected,"pilot action initialization")

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
                 property("_mobileStudioPresentationActive").toBool())) {
        return;
    }
    if (!d->flashWindowHackInProgress) {
        d->flashWindowHackInProgress = true;
        QTimer::singleShot(0, this, [this] {
            if ((property("_mobileStudioCanvasTransferred").toBool() ||
                 property("_mobileStudioPresentationActive").toBool())) {
                d->flashWindowHackInProgress = false;
                return;
            }
            hide();
            QTimer::singleShot(0, this, [this] {
                if (!(property("_mobileStudioCanvasTransferred").toBool() ||
                 property("_mobileStudioPresentationActive").toBool())) {
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
    (root/"libs/ui/krita_standalone_runtime.h").write_text(
        standalone_krita_runtime.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_documents.h").write_text(
        standalone_krita_documents.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/mobile_native_engine_compile_probe.cpp").write_text(
        native_engine_probe.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_pilot.h").write_text(
        standalone_engine_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/deferred_view_command.h").write_text(
        deferred_view_command.read_text(encoding="utf-8"),encoding="utf-8")
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
      <Separator/>''',
        "Settings menu mobile recovery launcher")
    xmlfile.write_text(xml,encoding="utf-8")
    cmakefile=root/"krita/CMakeLists.txt"
    cmake=cmakefile.read_text(encoding="utf-8")
    cmake=one(cmake,
        'set(KRITA5_XMLGUI_VERSION 548)',
        'set(KRITA5_XMLGUI_VERSION 551)',
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
