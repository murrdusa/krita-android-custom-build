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
    transfer=own/"mobile_core/canvas_transfer.h"
    native_chrome=own/"mobile_core/native_canvas_chrome.h"
    native_pilot=own/"mobile_core/native_canvas_pilot.h"
    surface=own/"mobile_core/canvas_surface.h"
    switcher=own/"mobile_core/document_switcher.h"
    bridge=own/"mobile_core/engine_bridge.h"
    document_service=own/"mobile_core/document_service.h"
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
               (shell,session,transfer,native_chrome,native_pilot,surface,switcher,bridge,document_service,mdi_identity,command_service,brush,color,layers,commands,quick,pressure_calibration,pressure_settings,
                reorder,tools,documents)):
        raise RuntimeError("Missing Mobile Studio source module")

    # Dependency boundary: only the backend services and temporary canvas
    # transfer controller may directly include Krita's desktop window.
    # Keep the mobile widgets/engine facade unaware of this legacy class,
    # so a future lightweight application host can replace it in one place.
    desktop_dependent=(document_service,command_service)
    qt_host_components=(session,transfer,native_chrome,native_pilot,surface)
    mobile_frontend=(shell,bridge,switcher,brush,color,layers,
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

    mainfile=root/"libs/ui/KisMainWindow.cpp"
    s=mainfile.read_text(encoding="utf-8")
    s=one(s,'#include "tablet_compact_skin.h"',
          '#include "tablet_compact_skin.h"\n#include "canvas_session.h"\n#include <QSharedPointer>',
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
    auto *mobileStudio = new MobileAppController(
        this,d->mdiArea,
        MobileEngineBridge(this,d->viewManager,d->mdiArea),
        mobilePilotAction);
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
    (root/"libs/ui/canvas_transfer.h").write_text(
        transfer.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_canvas_chrome.h").write_text(
        native_chrome.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_canvas_pilot.h").write_text(
        native_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_surface.h").write_text(
        surface.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_switcher.h").write_text(
        switcher.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_bridge.h").write_text(
        bridge.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_service.h").write_text(
        document_service.read_text(encoding="utf-8"),encoding="utf-8")
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
    xmlfile.write_text(xml,encoding="utf-8")
    cmakefile=root/"krita/CMakeLists.txt"
    cmake=cmakefile.read_text(encoding="utf-8")
    cmake=one(cmake,
        'set(KRITA5_XMLGUI_VERSION 548)',
        'set(KRITA5_XMLGUI_VERSION 549)',
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
