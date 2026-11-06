#!/usr/bin/env python3
"""v7: integrate the reversible phone-friendly Compact UI overlays.

The UI widget code lives in tablet_compact_skin.h in this repository. Its
corresponding source header is copied into the patched Krita tree.
Only Android builds compile the new widget. This patch runs after v1..v6.
"""
from pathlib import Path
import shutil
import sys

def main(root: Path):
    root=root.resolve()
    upstream="libs/ui/KisMainWindow.cpp"
    path=root/upstream
    s=path.read_text(encoding="utf-8")
    old='#include <mutex>'
    new='#ifdef Q_OS_ANDROID\n#include "tablet_compact_skin.h"\n#endif\n\n#include <mutex>'
    if s.count(old)!=1 or 'tablet_compact_skin.h' in s:
        raise RuntimeError("v7: mainwindow include anchor mismatch")
    s=s.replace(old,new)
    old='''    QTimer::singleShot(0, this, [this, tabletCompact]() {'''
    new='''    // Separate presentation layer; Krita's normal dockers/actions are restored.
    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);
    connect(tabletCompact, &QAction::toggled, compactSkin,
            [compactSkin](bool enabled) { compactSkin->setCompactActive(enabled); });
    QTimer::singleShot(0, this, [this, tabletCompact]() {'''
    if s.count(old)!=1:
        raise RuntimeError("v7: Compact UI initialization anchor mismatch")
    s=s.replace(old,new)
    target=root/"libs/ui/tablet_compact_skin.h"
    src=Path(__file__).resolve().parent/"tablet_compact_skin.h"
    if not src.is_file():
        raise RuntimeError("Missing tablet_compact_skin.h in customization repository")
    target.write_text(src.read_text(encoding="utf-8"),encoding="utf-8")
    path.write_text(s,encoding="utf-8")
    print("v7 inserted Compact UI skin and copied",target)

if __name__=="__main__":
    if len(sys.argv)!=2: raise SystemExit("Usage: apply_compact_skin.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
