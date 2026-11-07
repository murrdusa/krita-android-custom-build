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
    # The native popup must be initialized before KisViewManager and
    # KisPaintopBox are constructed; changing the setting later only leaves
    # the docker mode active for the current session.
    startup='''    KisScopedPerformanceLogger perfLog(QStringLiteral("KisMainWindow::KisMainWindow"));'''
    setup='''    KisScopedPerformanceLogger perfLog(QStringLiteral("KisMainWindow::KisMainWindow"));
#ifdef Q_OS_ANDROID
    if(!QSettings().value(QStringLiteral("TabletUi/nativeToolOptionsV1"),false).toBool()) {
        KisConfig tabletCfg(false);
        tabletCfg.setToolOptionsInDocker(false);
        QSettings().setValue(QStringLiteral("TabletUi/nativeToolOptionsV1"),true);
    }
#endif'''
    if s.count(startup)!=1:
        raise RuntimeError("Native Tool Options startup anchor mismatch")
    s=s.replace(startup,setup)
    old='''    QTimer::singleShot(0, this, [this, tabletCompact]() {'''
    new='''    // Separate presentation layer; Krita's normal dockers/actions are restored.
    auto *compactSkin = new TabletCompactSkin(this, d->mdiArea, d->viewManager);
    connect(tabletCompact, &QAction::toggled, compactSkin,
            [compactSkin](bool enabled) { compactSkin->setCompactActive(enabled); });
    QTimer::singleShot(0, this, [this, tabletCompact]() {'''
    if s.count(old)!=1:
        raise RuntimeError("v7: Compact UI initialization anchor mismatch")
    s=s.replace(old,new)
    # Previous customization stages modify the stock Krita toolbar XML
    # globally. Compact UI now has an entirely separate configurable toolbar,
    # so reset native Krita's default toolbar/menu layout and inject ONLY the
    # persistent mode toggle into the normal toolbar.
    import subprocess, hashlib, re
    source_xml=subprocess.check_output(
        ["git","-C",str(root),"show","HEAD:krita/krita5.xmlgui"],
        text=True)
    toolbar_anchor='''  <ToolBar name="mainToolBar" fullWidth="false" noMerge="1">
    <Text>File</Text>
'''
    if source_xml.count(toolbar_anchor)!=1:
        raise RuntimeError("Stock Krita toolbar XML anchor mismatch")
    native_xml=source_xml.replace(toolbar_anchor,toolbar_anchor+
        '    <Action name="tablet_compact_ui"/>\\n',1)
    (root/"krita/krita5.xmlgui").write_text(native_xml,encoding="utf-8")
    cmake_file=root/"krita/CMakeLists.txt"
    cmake_text=cmake_file.read_text(encoding="utf-8")
    version_pattern=r'set\\(KRITA5_XMLGUI_VERSION [0-9]+\\)'
    checksum_pattern=r'set\\(krita5_xmlgui_hash_expected "[0-9a-f]+"\\)'
    if len(re.findall(version_pattern,cmake_text))!=1 or len(
            re.findall(checksum_pattern,cmake_text))!=1:
        raise RuntimeError("Krita XMLGUI version/checksum anchor mismatch")
    cmake_text=re.sub(version_pattern,'set(KRITA5_XMLGUI_VERSION 547)',cmake_text)
    actual=hashlib.md5(''.join(native_xml.split()).encode()).hexdigest()
    cmake_text=re.sub(checksum_pattern,
        f'set(krita5_xmlgui_hash_expected "{actual}")',cmake_text)
    cmake_file.write_text(cmake_text,encoding="utf-8")
    print("v7 restored stock Krita normal toolbar + Compact toggle; XMLGUI",actual)
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
