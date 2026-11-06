#!/usr/bin/env python3
"""Android tablet v3 refinements. Run after v1 + v2 against KDE Krita v6.0.4."""
from pathlib import Path
import hashlib
import sys

def main(root: Path):
    changed = {}
    def get(path):
        if path not in changed:
            changed[path] = (root / path).read_text(encoding="utf-8")
        return changed[path]
    def sub(path, old, new, count=1):
        content = get(path)
        n = content.count(old)
        if n != count:
            raise RuntimeError(f"v3: {path}: expected {count} copies, found {n}: {old[:115]!r}")
        changed[path] = content.replace(old, new)
    def edit(path, fn):
        changed[path] = fn(get(path))

    # Defaults: 150ms double tap, rapid inward pinch enabled on new installs.
    p = "libs/ui/input/kis_shortcut_matcher.cpp"
    sub(p, 'QStringLiteral("TabletUi/doubleTapMs"), 250',
           'QStringLiteral("TabletUi/doubleTapMs"), 150', 2)
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, 'QStringLiteral("TabletUi/doubleTapMs"), 250',
           'QStringLiteral("TabletUi/doubleTapMs"), 150')
    sub(p, 'QStringLiteral("TabletUi/quickPinch"), false',
           'QStringLiteral("TabletUi/quickPinch"), true')
    p = "libs/ui/input/kis_input_manager.cpp"
    sub(p, 'QStringLiteral("TabletUi/quickPinch"), false',
           'QStringLiteral("TabletUi/quickPinch"), true')

    # Krita Default input profile: assign the appended OneFingerDoubleTap
    # gesture (enum value 12) to appended MirrorView action (index 15).
    # Existing profiles stored in user settings are not overwritten.
    p = "krita/data/input/kritadefault.profile"
    sub(p, '3={3;2;[];20;0;0}', '3={3;2;[];20;0;0}\n4={15;4;[];0;0;12}')

    # Compact UI: zero the QTabBar's height, not just its visibility, so
    # QMdiArea can reclaim the otherwise grey strip. Hide both the MDI and
    # current canvas scroll bars without changing the user's global settings.
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, "#include <QSettings>\n", """#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QAbstractScrollArea>
#include <QListWidget>
#include <QSizePolicy>
#include <KoToolManager.h>
#include <KoToolBase.h>
""")
    sub(p, """                tabs->setProperty("tabletWasVisible", tabs->isVisible());
                tabs->hide();""",
           """                tabs->setProperty("tabletWasVisible", tabs->isVisible());
                tabs->setProperty("tabletPrevMinH", tabs->minimumHeight());
                tabs->setProperty("tabletPrevMaxH", tabs->maximumHeight());
                tabs->setFixedHeight(0);
                tabs->hide();""")
    sub(p, """                tabs->setVisible(tabs->property("tabletWasVisible").toBool());""",
           """                tabs->setMinimumHeight(tabs->property("tabletPrevMinH").toInt());
                tabs->setMaximumHeight(tabs->property("tabletPrevMaxH").toInt());
                tabs->setVisible(tabs->property("tabletWasVisible").toBool());""")
    sub(p, 'if (QTabBar *tabs = d->findTabBarHACK()) tabs->hide();',
           'if (QTabBar *tabs = d->findTabBarHACK()) { tabs->setFixedHeight(0); tabs->hide(); }')
    # Apply scrollbar suppression to both the MDI and canvas controller.
    sub(p, """            KisConfigNotifier::instance()->notifyConfigChanged();
            menuBar()->setProperty("tabletWasVisible", menuBar()->isVisible());""",
           """            KisConfigNotifier::instance()->notifyConfigChanged();
            d->mdiArea->setProperty("tabletPrevHScroll", int(d->mdiArea->horizontalScrollBarPolicy()));
            d->mdiArea->setProperty("tabletPrevVScroll", int(d->mdiArea->verticalScrollBarPolicy()));
            d->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            d->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            if (d->viewManager->canvasBase()) {
                auto *ctl = d->viewManager->canvasBase()->canvasController();
                if (ctl) {
                    if (!ctl->property("tabletScrollSaved").toBool()) {
                        ctl->setProperty("tabletPrevHScroll", int(ctl->horizontalScrollBarPolicy()));
                        ctl->setProperty("tabletPrevVScroll", int(ctl->verticalScrollBarPolicy()));
                        ctl->setProperty("tabletScrollSaved", true);
                    }
                    ctl->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    ctl->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                }
            }
            menuBar()->setProperty("tabletWasVisible", menuBar()->isVisible());""")
    sub(p, """            KisConfigNotifier::instance()->notifyConfigChanged();
            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());""",
           """            KisConfigNotifier::instance()->notifyConfigChanged();
            d->mdiArea->setHorizontalScrollBarPolicy(static_cast<Qt::ScrollBarPolicy>(
                d->mdiArea->property("tabletPrevHScroll").toInt()));
            d->mdiArea->setVerticalScrollBarPolicy(static_cast<Qt::ScrollBarPolicy>(
                d->mdiArea->property("tabletPrevVScroll").toInt()));
            if (d->viewManager->canvasBase()) {
                auto *ctl = d->viewManager->canvasBase()->canvasController();
                if (ctl && ctl->property("tabletScrollSaved").toBool()) {
                    ctl->setHorizontalScrollBarPolicy(static_cast<Qt::ScrollBarPolicy>(
                        ctl->property("tabletPrevHScroll").toInt()));
                    ctl->setVerticalScrollBarPolicy(static_cast<Qt::ScrollBarPolicy>(
                        ctl->property("tabletPrevVScroll").toInt()));
                    ctl->setProperty("tabletScrollSaved", false);
                }
            }
            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());""")
    sub(p, """                if (QTabBar *tabs = d->findTabBarHACK()) { tabs->setFixedHeight(0); tabs->hide(); }
            });""",
           """                if (QTabBar *tabs = d->findTabBarHACK()) { tabs->setFixedHeight(0); tabs->hide(); }
                if (d->viewManager->canvasBase()) {
                    auto *ctl = d->viewManager->canvasBase()->canvasController();
                    if (ctl) {
                        if (!ctl->property("tabletScrollSaved").toBool()) {
                            ctl->setProperty("tabletPrevHScroll", int(ctl->horizontalScrollBarPolicy()));
                            ctl->setProperty("tabletPrevVScroll", int(ctl->verticalScrollBarPolicy()));
                            ctl->setProperty("tabletScrollSaved", true);
                        }
                        ctl->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                        ctl->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    }
                }
            });""")

    # Dense toolbar: all custom QAction controls use icons, not long labels.
    # For QMenu::menuAction() the default is MenuButtonPopup, where only the
    # tiny arrow works. InstantPopup makes the ENTIRE icon a menu trigger.
    sub(p, """    QTimer::singleShot(0, this, [tabletCompact]() {
        tabletCompact->setChecked(QSettings().value(QStringLiteral("TabletUi/compact"), true).toBool());
    });""",
           """    QTimer::singleShot(0, this, [this, tabletCompact]() {
        tabletCompact->setChecked(QSettings().value(QStringLiteral("TabletUi/compact"), true).toBool());
        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (toolbar->objectName() == QStringLiteral("mainToolBar") ||
                toolbar->objectName() == QStringLiteral("editToolBar")) {
                toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
                toolbar->setIconSize(QSize(24, 24));
            }
            if (QAction *menuAction = actionCollection()->action(QStringLiteral("tablet_quick_actions"))) {
                if (QToolButton *button = qobject_cast<QToolButton*>(toolbar->widgetForAction(menuAction))) {
                    button->setPopupMode(QToolButton::InstantPopup);
                    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    button->setMaximumWidth(40);
                    button->setMinimumWidth(32);
                }
            }
        }
    });""")
    sub(p, """    actionCollection()->addAction(QStringLiteral("tablet_pick_once"), oneShot);""",
           """    actionCollection()->addAction(QStringLiteral("tablet_pick_once"), oneShot);
    oneShot->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+P")));""")
    # Shorten the composite blend-mode selector without hiding erase or alpha.
    p = "libs/ui/kis_paintop_box.cc"
    sub(p, """    m_cmbCompositeOp = new KisCompositeOpComboBox();
    m_cmbCompositeOp->setFixedHeight(buttonsize);""",
           """    m_cmbCompositeOp = new KisCompositeOpComboBox();
#ifdef Q_OS_ANDROID
    m_cmbCompositeOp->setMinimumWidth(60);
    m_cmbCompositeOp->setMaximumWidth(116);
    m_cmbCompositeOp->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
#endif
    m_cmbCompositeOp->setFixedHeight(buttonsize);""")

    # Move gradient/pattern selection out of the permanent toolbar. Remove
    # the second injected Reload Preset control (stock has one already).
    p = "krita/krita5.xmlgui"
    sub(p, """    <Action name="gradients"/>
    <Action name="patterns"/>
    <Separator/>
    <Action name="dual"/>""",
           """    <Action name="dual"/>""")
    sub(p, """    <Action name="brushslider2"/>
    <Action name="reload_preset_action"/>
    <Action name="mirror_actions"/>""",
           """    <Action name="brushslider2"/>
    <Action name="mirror_actions"/>""")

    # Reduce Tool Options minimum-width pressure; preserve the ability to
    # scroll vertically. Individual third-party tool widgets may still need
    # their own responsive layouts.
    p = "libs/widgets/KoToolDocker.cpp"
    sub(p, """        housekeeperLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        housekeeperLayout->invalidate();""",
           """#ifdef Q_OS_ANDROID
        housekeeperLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
#else
        housekeeperLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
#endif
        housekeeperLayout->invalidate();""")
    sub(p, """    d->housekeeperLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);""",
           """#ifdef Q_OS_ANDROID
    d->housekeeperLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
#else
    d->housekeeperLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
#endif""")
    sub(p, """    d->scrollArea->setWidgetResizable(true);
    d->scrollArea->setFocusPolicy(Qt::NoFocus);""",
           """    d->scrollArea->setWidgetResizable(true);
#ifdef Q_OS_ANDROID
    d->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    d->scrollArea->setMinimumWidth(0);
    d->housekeeperWidget->setMinimumWidth(0);
#endif
    d->scrollArea->setFocusPolicy(Qt::NoFocus);""")

    # More relevant contextual selection actions, still attached to Krita's
    # undo-capable KisSelectionManager and shown only with selection context.
    p = "libs/ui/kis_selection_actions_panel.cpp"
    sub(p, """            {"duplicatelayer", i18n("Copy To New Layer"), &KisSelectionManager::copySelectionToNewLayer},
            {"tool_crop", i18n("Crop to Selection"), &KisSelectionManager::imageResizeToSelection},""",
           """            {"duplicatelayer", i18n("Copy To New Layer"), &KisSelectionManager::copySelectionToNewLayer},
#ifdef Q_OS_ANDROID
            {"edit-cut", i18n("Cut to New Layer"), &KisSelectionManager::cutToNewLayer},
            {"edit-copy", i18n("Copy Merged"), &KisSelectionManager::copyMerged},
            {"select-reselect", i18n("Reselect"), &KisSelectionManager::reselect},
#endif
            {"tool_crop", i18n("Crop to Selection"), &KisSelectionManager::imageResizeToSelection},""")

    # Finish by updating Krita's mandatory XMLGUI version and stripped MD5.
    p = "krita/CMakeLists.txt"
    sub(p, "set(KRITA5_XMLGUI_VERSION 545)", "set(KRITA5_XMLGUI_VERSION 546)")
    cmake = get(p)
    xml = get("krita/krita5.xmlgui")
    digest = hashlib.md5("".join(xml.split()).encode()).hexdigest()
    sub(p, 'set(krita5_xmlgui_hash_expected "a4656de215838ff1371d720f9c3d4b26")',
           f'set(krita5_xmlgui_hash_expected "{digest}")')

    for path, data in changed.items():
        (root/path).write_text(data, encoding="utf-8")
        print("v3 patched",path)
    print("Toolbar XMLGUI checksum",digest)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: apply_tablet_refinements.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
