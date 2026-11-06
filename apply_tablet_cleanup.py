#!/usr/bin/env python3
"""v5 focused cleanup: stock tool actions, pin-only reference toggle, frameless compact documents.

Apply AFTER v1/v2/v3/v4 patches to KDE Krita v6.0.4. Deliberately leave
gallery, brush controls, touch gestures and other tested features intact.
"""
from pathlib import Path
import subprocess
import sys

def main(root: Path):
    modified = {}
    def get(path):
        if path not in modified:
            modified[path] = (root / path).read_text(encoding="utf-8")
        return modified[path]
    def replace(path, old, new, occurrences=1):
        data = get(path)
        found = data.count(old)
        if found != occurrences:
            raise RuntimeError(f"v5 {path}: expected {occurrences}, got {found}: {old[:100]!r}")
        modified[path] = data.replace(old, new)
    def remove_range(path, beginning, end):
        data = get(path)
        if data.count(beginning) != 1:
            raise RuntimeError(f"v5 {path}: expected one start of range: {beginning[:80]!r}")
        left = data.index(beginning)
        if data.count(end, left) != 1:
            raise RuntimeError(f"v5 {path}: ambiguous end of range: {end[:80]!r}")
        right = data.index(end, left)
        modified[path] = data[:left] + data[right:]

    # Entire selection context bar back to Krita's unmodified implementation.
    # v2/v3 only modified this one file; baseline comes from pinned KDE tag.
    p = "libs/ui/kis_selection_actions_panel.cpp"
    original = subprocess.check_output(
        ["git", "-C", str(root), "show", f"HEAD:{p}"]).decode("utf-8")
    if get(p) == original:
        raise RuntimeError(f"{p} unexpectedly stock before the rollback; check patch order")
    modified[p] = original

    # No forced enabled/fixed-bottom selection bar in compact mode. Krita's
    # own settings decide whether its native action bar appears.
    p = "libs/ui/KisMainWindow.cpp"
    remove_range(p, """            const KisConfig previous(true);
            setProperty("tabletPrevSelectionEnabled", previous.selectionActionBar());""",
                 """            d->mdiArea->setProperty("tabletPrevHScroll", int(d->mdiArea->horizontalScrollBarPolicy()));""")
    remove_range(p, """            {
                KisConfig settings(false);
                settings.setSelectionActionBar(property("tabletPrevSelectionEnabled").toBool());""",
                 """            d->mdiArea->setHorizontalScrollBarPolicy(static_cast<Qt::ScrollBarPolicy>(""")

    # Remove all custom transform context controls; Krita's Transform tool
    # and default Tool Options remain untouched.
    remove_range(p, """    QToolBar *tabletTransformBar = new QToolBar(i18n("Transform Actions"), this);""",
                 """\n#endif\n\n    d->recentFiles""")

    # Reference-image visibility should be a tiny pin icon with no displayed
    # words, while keeping the longer action text in menus and as tooltip.
    replace(p, """            ref->setIconText(i18n("References"));""",
           """            ref->setIconText(QString());""")
    replace(p, """        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (toolbar->objectName() == QStringLiteral("mainToolBar")""",
           """        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (QAction *ref = actionCollection()->action(QStringLiteral("view_toggle_reference_images"))) {
                if (QToolButton *pin = qobject_cast<QToolButton*>(toolbar->widgetForAction(ref))) {
                    pin->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    pin->setIcon(KisIconUtils::loadIcon("krita_tool_reference_images"));
                    pin->setIconSize(QSize(24, 24));
                    pin->setMaximumWidth(36);
                    pin->setMinimumWidth(30);
                    pin->setToolTip(i18n("Show or hide reference images"));
                }
            }
            if (toolbar->objectName() == QStringLiteral("mainToolBar")""")

    # Qt's MDI subwindow uses a titlebar style even when the tabbar is gone
    # unless WindowTitleHint is explicitly suppressed. Enforce a *customized*
    # frameless subwindow both when switching mode and after activation.
    replace(p, """subwin->setWindowFlags(subwin->windowFlags() | Qt::FramelessWindowHint);""",
           """subwin->setWindowFlags(Qt::SubWindow | Qt::CustomizeWindowHint |
                                       Qt::FramelessWindowHint);""", occurrences=2)
    # Reassert after Qt's deferred subwindow-activation/layout work; changing
    # MDI view modes can rebuild internal tab/titlebar decorations.
    replace(p, """                if (QMdiSubWindow *subwin = d->mdiArea->activeSubWindow()) {
                    if (!subwin->property("tabletFrameSaved").toBool()) {""",
           """                if (QTabBar *tabs = d->findTabBarHACK()) {
                    tabs->hide();
                }
                if (QMdiSubWindow *subwin = d->mdiArea->activeSubWindow()) {
                    if (!subwin->property("tabletFrameSaved").toBool()) {""")
    replace(p, """                    if (!subwin->isMaximized()) subwin->showMaximized();""",
           """                    subwin->setWindowFlags(Qt::SubWindow | Qt::CustomizeWindowHint |
                                           Qt::FramelessWindowHint);
                    subwin->showMaximized();""")
    replace(p, """            for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                if (!subwin->property("tabletFrameSaved").toBool()) {
                    subwin->setProperty("tabletPrevFlags", int(subwin->windowFlags()));
                    subwin->setProperty("tabletFrameSaved", true);
                }
                subwin->setWindowFlags(Qt::SubWindow | Qt::CustomizeWindowHint |
                                       Qt::FramelessWindowHint);
                subwin->showMaximized();
            }
            d->mdiArea->setProperty("tabletPrevHScroll", int(d->mdiArea->horizontalScrollBarPolicy()));""",
           """            for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                if (!subwin->property("tabletFrameSaved").toBool()) {
                    subwin->setProperty("tabletPrevFlags", int(subwin->windowFlags()));
                    subwin->setProperty("tabletFrameSaved", true);
                }
                subwin->setWindowFlags(Qt::SubWindow | Qt::CustomizeWindowHint |
                                       Qt::FramelessWindowHint);
                subwin->showMaximized();
            }
            QTimer::singleShot(0, this, [this]() {
                if (d->mdiArea->viewMode() != QMdiArea::SubWindowView) return;
                if (QTabBar *tabs = d->findTabBarHACK()) tabs->hide();
                for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                    subwin->setWindowFlags(Qt::SubWindow | Qt::CustomizeWindowHint |
                                           Qt::FramelessWindowHint);
                    subwin->showMaximized();
                }
            });
            d->mdiArea->setProperty("tabletPrevHScroll", int(d->mdiArea->horizontalScrollBarPolicy()));""")

    for p, contents in modified.items():
        (root / p).write_text(contents, encoding="utf-8")
        print("v5 cleanup patched", p)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_tablet_cleanup.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
