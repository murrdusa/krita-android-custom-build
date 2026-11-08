#!/usr/bin/env python3
"""Install the opt-in Krita-engine mobile UI proof into the patched Android tree.

Run AFTER all legacy UI patches. It does not replace the canvas, paint engine,
document model or normal Krita defaults. Every edit is anchor-checked.
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
    shell=own/"mobile_core/mobile_shell.h"
    if not shell.is_file():
        raise RuntimeError("Missing mobile_core/mobile_shell.h")

    mainfile=root/"libs/ui/KisMainWindow.cpp"
    s=mainfile.read_text(encoding="utf-8")
    s=one(s,'#include "tablet_compact_skin.h"',
          '#include "tablet_compact_skin.h"\n#include "mobile_shell.h"',
          "main window include")

    native='''    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);'''
    injected='''    // Opt-in alternate shell, OFF by default. The existing compact mode
    // remains untouched until the user explicitly opens this pilot.
    QAction *mobilePilotAction = new QAction(
        KisIconUtils::loadIcon("draw-freehand"),
        i18n("Mobile Engine Pilot"),this);
    mobilePilotAction->setCheckable(true);
    mobilePilotAction->setChecked(false);
    mobilePilotAction->setToolTip(i18n(
        "Experimental mobile workspace using Krita's native canvas"));
    actionCollection()->addAction(
        QStringLiteral("tablet_mobile_engine_pilot"),mobilePilotAction);
    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);
    auto *mobilePilot = new KritaMobilePilot(
        this,d->mdiArea,d->viewManager,mobilePilotAction);
    connect(mobilePilotAction,&QAction::toggled,this,
            [tabletCompact,mobilePilot](bool enabled) {
        if(enabled && tabletCompact->isChecked())
            tabletCompact->setChecked(false);
        mobilePilot->setActive(enabled);
    });
    connect(tabletCompact,&QAction::toggled,this,
            [mobilePilotAction](bool compact) {
        if(compact && mobilePilotAction->isChecked())
            mobilePilotAction->setChecked(false);
    });'''
    s=one(s,native,injected,"pilot action initialization")
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
    (root/"libs/ui/mobile_shell.h").write_text(shell.read_text(encoding="utf-8"),
                                             encoding="utf-8")

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
    print(f"Installed opt-in Krita Mobile Pilot; XMLGUI checksum {digest}")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_mobile_shell.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
