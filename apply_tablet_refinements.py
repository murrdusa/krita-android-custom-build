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
                auto *ctl = activeView() ? activeView()->canvasController() : nullptr;
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
                auto *ctl = activeView() ? activeView()->canvasController() : nullptr;
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
                    auto *ctl = activeView() ? activeView()->canvasController() : nullptr;
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

    # Configurable Quick Actions: curated actions, reordered by drag-and-drop;
    # direct gradient/pattern chooser actions replace permanent toolbar slots.
    p = "libs/ui/KisMainWindow.cpp"
    def quick_actions(s):
        start = s.index("    QMenu *quickMenu = new QMenu(i18n(\"Quick Actions\"), this);")
        stop = s.index("\n#endif\n\n    d->recentFiles", start)
        replacement = r'''    QMenu *quickMenu = new QMenu(i18n("Quick Actions"), this);
    quickMenu->menuAction()->setIcon(KisIconUtils::loadIcon("application-menu"));
    actionCollection()->addAction(QStringLiteral("tablet_quick_actions"), quickMenu->menuAction());
    const QStringList tabletQuickAvailable = {
        QStringLiteral("gradients"), QStringLiteral("patterns"),
        QStringLiteral("file_new"), QStringLiteral("file_open"),
        QStringLiteral("file_save_as"), QStringLiteral("file_export_file"),
        QStringLiteral("select_all"), QStringLiteral("invert_selection"),
        QStringLiteral("reselect"), QStringLiteral("deselect"),
        QStringLiteral("fill_selection_foreground_color"),
        QStringLiteral("cut_selection_to_new_layer"),
        QStringLiteral("duplicate_layer"), QStringLiteral("merge_layer"),
        QStringLiteral("quick_group"), QStringLiteral("edit_undo"),
        QStringLiteral("edit_redo"), QStringLiteral("tablet_reset_view"),
        QStringLiteral("tablet_settings")
    };
    const QStringList tabletQuickDefaults = {
        QStringLiteral("gradients"), QStringLiteral("patterns"),
        QStringLiteral("file_new"), QStringLiteral("file_open"),
        QStringLiteral("file_save_as"), QStringLiteral("file_export_file"),
        QStringLiteral("select_all"), QStringLiteral("invert_selection"),
        QStringLiteral("reselect"), QStringLiteral("deselect"),
        QStringLiteral("fill_selection_foreground_color"),
        QStringLiteral("duplicate_layer"), QStringLiteral("merge_layer"),
        QStringLiteral("quick_group"), QStringLiteral("edit_undo"),
        QStringLiteral("edit_redo"), QStringLiteral("tablet_reset_view"),
        QStringLiteral("tablet_settings")
    };
    QAction *customizeQuick = new QAction(i18n("Customize Quick Actions..."), this);
    connect(customizeQuick, &QAction::triggered, this,
            [this, tabletQuickAvailable, tabletQuickDefaults]() {
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Customize Quick Actions"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *info = new QLabel(i18n("Check actions to show. Drag rows to change their order."), &dialog);
        layout->addWidget(info);
        auto *list = new QListWidget(&dialog);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        layout->addWidget(list);
        QStringList selected = QSettings().value(
            QStringLiteral("TabletUi/quickActions"), tabletQuickDefaults).toStringList();
        QStringList ordered = selected;
        for (const QString &name : tabletQuickAvailable) {
            if (!ordered.contains(name)) ordered.append(name);
        }
        for (const QString &name : ordered) {
            if (!tabletQuickAvailable.contains(name)) continue;
            QAction *action = actionCollection()->action(name);
            QString label = action && !action->text().isEmpty() ? action->text() : name;
            auto *item = new QListWidgetItem(label.remove(QLatin1Char('&')), list);
            item->setData(Qt::UserRole, name);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            item->setCheckState(selected.contains(name) ? Qt::Checked : Qt::Unchecked);
        }
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            QStringList chosen;
            for (int i = 0; i < list->count(); ++i) {
                auto *item = list->item(i);
                if (item->checkState() == Qt::Checked)
                    chosen << item->data(Qt::UserRole).toString();
            }
            QSettings().setValue(QStringLiteral("TabletUi/quickActions"), chosen);
        }
    });
    connect(quickMenu, &QMenu::aboutToShow, this,
            [this, quickMenu, customizeQuick, tabletQuickDefaults]() {
        quickMenu->clear();
        const QStringList selected = QSettings().value(
            QStringLiteral("TabletUi/quickActions"), tabletQuickDefaults).toStringList();
        for (const QString &name : selected) {
            QAction *action = actionCollection()->action(name);
            if (action) quickMenu->addAction(action);
        }
        quickMenu->addSeparator();
        quickMenu->addAction(customizeQuick);
    });
'''
        return s[:start] + replacement + s[stop:]
    edit(p, quick_actions)
    sub(p, "#include <QListWidget>\n",
           "#include <QListWidget>\n#include <QLabel>\n#include <QAbstractItemView>\n")

    # Four independently configurable hardware key gestures. Single actions
    # dispatch on KEY UP; brush-size long actions start repeating only after
    # the long-press threshold. This keeps short-press latency near zero.
    p = "packaging/android/apk/src/org/krita/android/JNIWrappers.java"
    sub(p, "    public static native void volumeUndoRedo(boolean redo);",
           """    public static native void volumeUndoRedo(boolean redo);
    public static native int volumeKeyAction(boolean up, boolean longPress);
    public static native int volumeLongPressMs();
    public static native void volumeExecute(int action);""")
    p = "packaging/android/apk/src/org/krita/android/MainActivity.java"
    sub(p, "import android.os.Bundle;", """import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;""")
    def java_volume(s):
        start = s.index("    @Override\n    public boolean onKeyDown(final int keyCode, final KeyEvent event) {")
        stop = s.index("    @Override\n    public boolean onKeyUp(final int keyCode, final KeyEvent event) {", start)
        return s[:start] + r'''    // Tablet buttons: normal actions fire on release. Long press only
    // auto-repeats while held for brush size changes.
    private final Handler tabletVolumeHandler = new Handler(Looper.getMainLooper());
    private Runnable tabletVolumeRepeat;
    private int tabletVolumeHeldKey = -1;
    private long tabletVolumeStart = 0;
    private boolean tabletVolumeDidRepeat = false;

    @Override
    public boolean onKeyDown(final int keyCode, final KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            if (event.getRepeatCount() == 0) {
                if (tabletVolumeRepeat != null) tabletVolumeHandler.removeCallbacks(tabletVolumeRepeat);
                tabletVolumeHeldKey = keyCode;
                tabletVolumeStart = event.getEventTime();
                tabletVolumeDidRepeat = false;
                final int longAction = JNIWrappers.volumeKeyAction(
                    keyCode == KeyEvent.KEYCODE_VOLUME_UP, true);
                if (longAction == 5 || longAction == 6) {
                    tabletVolumeRepeat = new Runnable() {
                        @Override public void run() {
                            if (tabletVolumeHeldKey != keyCode) return;
                            tabletVolumeDidRepeat = true;
                            JNIWrappers.volumeExecute(longAction);
                            tabletVolumeHandler.postDelayed(this, 90);
                        }
                    };
                    tabletVolumeHandler.postDelayed(
                        tabletVolumeRepeat, JNIWrappers.volumeLongPressMs());
                }
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

''' + s[stop:]
    edit(p, java_volume)
    sub(p, """        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            return true;
        }""",
           """        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            if (tabletVolumeRepeat != null) {
                tabletVolumeHandler.removeCallbacks(tabletVolumeRepeat);
                tabletVolumeRepeat = null;
            }
            if (tabletVolumeHeldKey == keyCode && !tabletVolumeDidRepeat) {
                boolean longPress = event.getEventTime() - tabletVolumeStart
                                    >= JNIWrappers.volumeLongPressMs();
                int action = JNIWrappers.volumeKeyAction(
                    keyCode == KeyEvent.KEYCODE_VOLUME_UP, longPress);
                if (action != 0) JNIWrappers.volumeExecute(action);
            }
            tabletVolumeHeldKey = -1;
            tabletVolumeDidRepeat = false;
            return true;
        }""")
    p = "krita/main.cc"
    sub(p, """#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main""",
           r'''// Action IDs: 0 disabled, 1 undo, 2 redo, 3 eraser, 4 pick once,
 // 5 increase brush size, 6 decrease brush size.
extern "C" JNIEXPORT jint JNICALL
Java_org_krita_android_JNIWrappers_volumeKeyAction(JNIEnv*, jobject, jboolean up, jboolean longPress)
{
    QSettings s;
    if (longPress && !s.value(QStringLiteral("TabletUi/volumeLongEnabled"), true).toBool()) return 0;
    if (!longPress && !s.value(QStringLiteral("TabletUi/volumeShortEnabled"), true).toBool()) return 0;
    const QString key = up
       ? (longPress ? QStringLiteral("TabletUi/volumeUpLong") : QStringLiteral("TabletUi/volumeUpShort"))
       : (longPress ? QStringLiteral("TabletUi/volumeDownLong") : QStringLiteral("TabletUi/volumeDownShort"));
    const int fallback = up ? (longPress ? 3 : 2) : (longPress ? 4 : 1);
    return qBound(0, s.value(key, fallback).toInt(), 6);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_krita_android_JNIWrappers_volumeLongPressMs(JNIEnv*, jobject)
{
    return qBound(80, QSettings().value(QStringLiteral("TabletUi/volumeLongMs"), 150).toInt(), 1000);
}

extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_volumeExecute(JNIEnv*, jobject, jint code)
{
    if (code <= 0 || code > 6 || !KisPart::exists()) return;
    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (!window) return;
    QMetaObject::invokeMethod(window, [window, code]() {
        if (code == 1 || code == 2) {
            QMetaObject::invokeMethod(window, code == 1 ? "undo" : "redo",
                                      Qt::DirectConnection);
            return;
        }
        QString name;
        if (code == 3) name = QStringLiteral("erase_action");
        if (code == 4) name = QStringLiteral("tablet_pick_once");
        if (code == 5) name = QStringLiteral("increase_brush_size");
        if (code == 6) name = QStringLiteral("decrease_brush_size");
        if (QAction *action = window->actionCollection()->action(name))
            action->trigger();
    }, Qt::QueuedConnection);
}

#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main''')
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """        form->addRow(volumeKeys);
        QCheckBox *pinchEnabled""",
           r'''        form->addRow(volumeKeys);
        QCheckBox *volumeShortEnabled = new QCheckBox(i18n("Enable volume short presses"), &dialog);
        volumeShortEnabled->setChecked(stored.value(QStringLiteral("TabletUi/volumeShortEnabled"), true).toBool());
        form->addRow(volumeShortEnabled);
        QCheckBox *volumeLongEnabled = new QCheckBox(i18n("Enable volume long presses"), &dialog);
        volumeLongEnabled->setChecked(stored.value(QStringLiteral("TabletUi/volumeLongEnabled"), true).toBool());
        form->addRow(volumeLongEnabled);
        QSpinBox *volumeLongMs = new QSpinBox(&dialog);
        volumeLongMs->setRange(80, 1000);
        volumeLongMs->setSuffix(i18n(" ms"));
        volumeLongMs->setValue(stored.value(QStringLiteral("TabletUi/volumeLongMs"), 150).toInt());
        form->addRow(i18n("Long-press threshold"), volumeLongMs);
        auto makeVolumeSelector = [&dialog, &stored, form](const QString &label,
                                                            const QString &key, int defaultAction) {
            QComboBox *combo = new QComboBox(&dialog);
            combo->addItem(i18n("None"), 0);
            combo->addItem(i18n("Undo"), 1);
            combo->addItem(i18n("Redo"), 2);
            combo->addItem(i18n("Toggle Eraser"), 3);
            combo->addItem(i18n("Pick Color Once"), 4);
            combo->addItem(i18n("Increase Brush Size (repeat if held)"), 5);
            combo->addItem(i18n("Decrease Brush Size (repeat if held)"), 6);
            combo->setCurrentIndex(combo->findData(stored.value(key, defaultAction).toInt()));
            form->addRow(label, combo);
            return combo;
        };
        QComboBox *volDownShort = makeVolumeSelector(
            i18n("Volume down / short"), QStringLiteral("TabletUi/volumeDownShort"), 1);
        QComboBox *volUpShort = makeVolumeSelector(
            i18n("Volume up / short"), QStringLiteral("TabletUi/volumeUpShort"), 2);
        QComboBox *volDownLong = makeVolumeSelector(
            i18n("Volume down / long"), QStringLiteral("TabletUi/volumeDownLong"), 4);
        QComboBox *volUpLong = makeVolumeSelector(
            i18n("Volume up / long"), QStringLiteral("TabletUi/volumeUpLong"), 3);
        QCheckBox *pinchEnabled''')
    sub(p, """            stored.setValue(QStringLiteral("TabletUi/volumeKeys"), volumeKeys->isChecked());""",
           """            stored.setValue(QStringLiteral("TabletUi/volumeKeys"), volumeKeys->isChecked());
            stored.setValue(QStringLiteral("TabletUi/volumeShortEnabled"), volumeShortEnabled->isChecked());
            stored.setValue(QStringLiteral("TabletUi/volumeLongEnabled"), volumeLongEnabled->isChecked());
            stored.setValue(QStringLiteral("TabletUi/volumeLongMs"), volumeLongMs->value());
            stored.setValue(QStringLiteral("TabletUi/volumeDownShort"), volDownShort->currentData().toInt());
            stored.setValue(QStringLiteral("TabletUi/volumeUpShort"), volUpShort->currentData().toInt());
            stored.setValue(QStringLiteral("TabletUi/volumeDownLong"), volDownLong->currentData().toInt());
            stored.setValue(QStringLiteral("TabletUi/volumeUpLong"), volUpLong->currentData().toInt());""")
    sub(p, "#include <QAbstractItemView>\n", "#include <QAbstractItemView>\n#include <QComboBox>\n")

    # Optional Android all-files permission for a sideloaded personal gallery.
    # Android 11+ intentionally prevents folder picker authorization of the
    # Downloads ROOT. This special permission must be granted by the user in
    # Android Settings; it never grants access to other apps' private data.
    p = "packaging/android/apk/AndroidManifest.xml"
    sub(p, """    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />""",
           """    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE" />""")
    p = "packaging/android/apk/src/org/krita/android/MainActivity.java"
    sub(p, "import android.os.Looper;", """import android.os.Looper;
import android.os.Environment;
import android.provider.Settings;""")
    sub(p, """public class MainActivity extends QtActivity {

    private static final String TAG""",
           """public class MainActivity extends QtActivity {
    private static volatile MainActivity tabletPermissionActivity;

    public static boolean tabletHasAllFilesAccess() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager();
    }

    // Settings authorization is user initiated and can be revoked at any time.
    public static void tabletRequestAllFilesAccess() {
        final MainActivity activity = tabletPermissionActivity;
        if (activity == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return;
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + activity.getPackageName()));
            try {
                activity.startActivity(intent);
            } catch (android.content.ActivityNotFoundException e) {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        });
    }

    private static final String TAG""")
    sub(p, """        super.onCreate(savedInstanceState);
        Log.i(TAG, "TouchSlop:""",
           """        super.onCreate(savedInstanceState);
        tabletPermissionActivity = this;
        Log.i(TAG, "TouchSlop:""")

    p = "libs/ui/KisWelcomePageWidget.cpp"
    sub(p, "#include <QComboBox>\n",
           "#include <QComboBox>\n#ifdef Q_OS_ANDROID\n#include <QAndroidJniObject>\n#endif\n")
    sub(p, """        galleryControls->addWidget(folderButton);
        galleryControls->addWidget(refreshButton);""",
           """        galleryControls->addWidget(folderButton);
        auto *downloadsButton = new QPushButton(i18n("Downloads"), galleryPage);
        auto *allowFiles = new QPushButton(i18n("Allow Downloads Access"), galleryPage);
        galleryControls->addWidget(downloadsButton);
        galleryControls->addWidget(allowFiles);
        galleryControls->addWidget(refreshButton);""")
    sub(p, """        auto folder = std::make_shared<QString>(
            QSettings().value(QStringLiteral("CustomProjectGalleryFolder"), QDir::homePath()).toString());""",
           """        const QString downloadsPath = QStringLiteral("/storage/emulated/0/Download");
        auto folder = std::make_shared<QString>(
            QSettings().value(QStringLiteral("CustomProjectGalleryFolder"),
                downloadsPath).toString());""")
    sub(p, """        connect(refreshButton, &QPushButton::clicked, this, populate);""",
           """        connect(downloadsButton, &QPushButton::clicked, this, [folder, populate]() {
            *folder = QStringLiteral("/storage/emulated/0/Download");
            QSettings().setValue(QStringLiteral("CustomProjectGalleryFolder"), *folder);
            populate();
        });
        connect(allowFiles, &QPushButton::clicked, this, [this]() {
            if (QAndroidJniObject::callStaticMethod<jboolean>(
                    "org/krita/android/MainActivity", "tabletHasAllFilesAccess", "()Z")) {
                QMessageBox::information(this, i18n("Gallery Access"),
                    i18n("File access is already granted. Select Downloads and press Refresh."));
                return;
            }
            if (QMessageBox::question(this, i18n("Allow Gallery Downloads Access"),
                    i18n("Android requires special All Files Access to let this personal build "
                         "scan every Krita file in Downloads. This is optional and can be "
                         "revoked in Android Settings. Open the permission settings?"))
                    == QMessageBox::Yes) {
                QAndroidJniObject::callStaticMethod<void>(
                    "org/krita/android/MainActivity", "tabletRequestAllFilesAccess", "()V");
            }
        });
        connect(refreshButton, &QPushButton::clicked, this, populate);""")

    # Finger drag references without switching tools.  The shape manager owns
    # the geometry; the move is recorded as one undoable shape command.
    # Touches elsewhere remain regular pinch/pan/painting events.
    p = "libs/ui/input/kis_input_manager.cpp"
    sub(p, "#include <QSet>\n", """#include <QSet>
#include <KoShapeManager.h>
#include <KoShapeMoveCommand.h>
#include <KisReferenceImage.h>
""")
    sub(p, """QSet<KisInputManager*> tabletReferencePenPresses;
}""",
           """QSet<KisInputManager*> tabletReferencePenPresses;
struct TabletReferenceDrag {
    KoShape *shape = nullptr;
    KisCanvas2 *canvas = nullptr;
    QPointF startingDocPoint;
    QPointF startingShapePosition;
};
QHash<KisInputManager*, TabletReferenceDrag> tabletReferenceDrags;
}""")
    sub(p, """    tabletReferencePenPresses.remove(this);
#endif""",
           """    tabletReferencePenPresses.remove(this);
    tabletReferenceDrags.remove(this);
#endif""")
    sub(p, """        tabletPinches.remove(this);
#endif
        d->debugEvent<QTouchEvent, false>(event);""",
           """        tabletPinches.remove(this);
        QTouchEvent *referenceTouch = static_cast<QTouchEvent*>(event);
        if (QSettings().value(QStringLiteral("TabletUi/referenceFingerDrag"), true).toBool()
            && referenceTouch->touchPoints().size() == 1 && KisPart::exists()) {
            KisMainWindow *window = KisPart::instance()->currentMainwindow();
            KisCanvas2 *canvas = window && window->viewManager()
                ? window->viewManager()->canvasBase() : nullptr;
            if (canvas && window->activeView() &&
                canvas->referenceImagesDecoration() &&
                canvas->referenceImagesDecoration()->visible()) {
                KisReferenceImagesLayerSP refs =
                    window->activeView()->document()->referenceImagesLayer();
                if (refs) {
                    QPointF widgetPoint = referenceTouch->touchPoints().first().pos();
                    QPointF docPoint = canvas->coordinatesConverter()->widgetToDocument(widgetPoint);
                    KoShape *shape = refs->shapeManager()->shapeAt(docPoint);
                    if (dynamic_cast<KisReferenceImage*>(shape)) {
                        TabletReferenceDrag drag;
                        drag.shape = shape;
                        drag.canvas = canvas;
                        drag.startingDocPoint = docPoint;
                        drag.startingShapePosition = shape->absolutePosition();
                        tabletReferenceDrags.insert(this, drag);
                        d->blockMouseEvents();
                        event->accept();
                        return true;
                    }
                }
            }
        }
#endif
        d->debugEvent<QTouchEvent, false>(event);""")
    sub(p, """        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        d->debugEvent<QTouchEvent, false>(event);
#ifdef Q_OS_ANDROID
        if (QSettings().value(QStringLiteral("TabletUi/quickPinch"), true).toBool()""",
           """        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        d->debugEvent<QTouchEvent, false>(event);
#ifdef Q_OS_ANDROID
        if (tabletReferenceDrags.contains(this)) {
            auto &drag = tabletReferenceDrags[this];
            if (drag.canvas && drag.shape && !touchEvent->touchPoints().isEmpty()) {
                QPointF now = drag.canvas->coordinatesConverter()->widgetToDocument(
                    touchEvent->touchPoints().first().pos());
                drag.shape->update();
                drag.shape->setAbsolutePosition(
                    drag.startingShapePosition + now - drag.startingDocPoint);
                drag.shape->update();
            }
            event->accept();
            return true;
        }
        if (QSettings().value(QStringLiteral("TabletUi/quickPinch"), true).toBool()""")
    sub(p, """    case QEvent::TouchEnd:
    {
        d->cancelTouchHoldTimer();""",
           """    case QEvent::TouchEnd:
    {
#ifdef Q_OS_ANDROID
        if (tabletReferenceDrags.contains(this)) {
            TabletReferenceDrag drag = tabletReferenceDrags.take(this);
            if (drag.shape && drag.canvas) {
                QPointF finalPosition = drag.shape->absolutePosition();
                if (QLineF(drag.startingShapePosition, finalPosition).length() > 0.01) {
                    QList<KoShape*> shapes = {drag.shape};
                    QList<QPointF> initial = {drag.startingShapePosition};
                    QList<QPointF> final = {finalPosition};
                    drag.canvas->addCommand(new KoShapeMoveCommand(shapes, initial, final));
                }
            }
            d->allowMouseEvents();
            event->accept();
            return true;
        }
#endif
        d->cancelTouchHoldTimer();""")
    sub(p, """    case QEvent::TouchCancel:
    {
""",
           """    case QEvent::TouchCancel:
    {
#ifdef Q_OS_ANDROID
        if (tabletReferenceDrags.contains(this)) {
            TabletReferenceDrag drag = tabletReferenceDrags.take(this);
            if (drag.shape) {
                drag.shape->update();
                drag.shape->setAbsolutePosition(drag.startingShapePosition);
                drag.shape->update();
            }
            d->allowMouseEvents();
            event->accept();
            return true;
        }
#endif
""")
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """        form->addRow(pinchEnabled);
        layout->addLayout(form);""",
           """        form->addRow(pinchEnabled);
        QCheckBox *referenceFingerDrag = new QCheckBox(i18n("Finger drag moves reference images"), &dialog);
        referenceFingerDrag->setChecked(stored.value(QStringLiteral("TabletUi/referenceFingerDrag"), true).toBool());
        form->addRow(referenceFingerDrag);
        layout->addLayout(form);""")
    sub(p, """            stored.setValue(QStringLiteral("TabletUi/quickPinch"), pinchEnabled->isChecked());""",
           """            stored.setValue(QStringLiteral("TabletUi/quickPinch"), pinchEnabled->isChecked());
            stored.setValue(QStringLiteral("TabletUi/referenceFingerDrag"), referenceFingerDrag->isChecked());""")

    # A compact, context-sensitive transform strip only in Compact UI.
    # Use the actual transform tool's Qt slots/properties, so apply, reset and
    # cancel retain Krita's transaction / undo semantics.
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """    connect(quickMenu, &QMenu::aboutToShow, this,
            [this, quickMenu, customizeQuick, tabletQuickDefaults]() {
        quickMenu->clear();
        const QStringList selected = QSettings().value(
            QStringLiteral("TabletUi/quickActions"), tabletQuickDefaults).toStringList();
        for (const QString &name : selected) {
            QAction *action = actionCollection()->action(name);
            if (action) quickMenu->addAction(action);
        }
        quickMenu->addSeparator();
        quickMenu->addAction(customizeQuick);
    });
""",
           r'''    connect(quickMenu, &QMenu::aboutToShow, this,
            [this, quickMenu, customizeQuick, tabletQuickDefaults]() {
        quickMenu->clear();
        const QStringList selected = QSettings().value(
            QStringLiteral("TabletUi/quickActions"), tabletQuickDefaults).toStringList();
        for (const QString &name : selected) {
            QAction *action = actionCollection()->action(name);
            if (action) quickMenu->addAction(action);
        }
        quickMenu->addSeparator();
        quickMenu->addAction(customizeQuick);
    });

    QToolBar *tabletTransformBar = new QToolBar(i18n("Transform Actions"), this);
    tabletTransformBar->setObjectName(QStringLiteral("tabletTransformContextBar"));
    tabletTransformBar->setMovable(false);
    tabletTransformBar->setFloatable(false);
    tabletTransformBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *transformModeButton = new QToolButton(tabletTransformBar);
    transformModeButton->setText(i18n("Mode"));
    transformModeButton->setPopupMode(QToolButton::InstantPopup);
    auto *transformModeMenu = new QMenu(transformModeButton);
    auto getTransformTool = [this]() -> KoToolBase* {
        KisCanvas2 *canvas = d->viewManager->canvasBase();
        if (!canvas) return nullptr;
        return KoToolManager::instance()->toolById(canvas, QStringLiteral("KisToolTransform"));
    };
    const QStringList transformNames = {
        i18n("Free"), i18n("Warp"), i18n("Cage"),
        i18n("Liquify"), i18n("Perspective"), i18n("Mesh")};
    for (int mode = 0; mode < transformNames.size(); ++mode) {
        QAction *choice = transformModeMenu->addAction(transformNames.at(mode));
        connect(choice, &QAction::triggered, this, [getTransformTool, mode]() {
            if (KoToolBase *tool = getTransformTool())
                tool->setProperty("transformMode", mode);
        });
    }
    transformModeButton->setMenu(transformModeMenu);
    tabletTransformBar->addWidget(transformModeButton);
    QAction *resetTransform = tabletTransformBar->addAction(i18n("Reset"));
    connect(resetTransform, &QAction::triggered, this, [getTransformTool]() {
        if (KoToolBase *tool = getTransformTool())
            QMetaObject::invokeMethod(tool, "slotRestartTransform", Qt::DirectConnection);
    });
    QAction *cancelTransform = tabletTransformBar->addAction(i18n("Cancel"));
    connect(cancelTransform, &QAction::triggered, this, [getTransformTool]() {
        if (KoToolBase *tool = getTransformTool())
            QMetaObject::invokeMethod(tool, "slotCancelTransform", Qt::DirectConnection);
    });
    QAction *applyTransform = tabletTransformBar->addAction(i18n("Apply"));
    connect(applyTransform, &QAction::triggered, this, [getTransformTool]() {
        if (KoToolBase *tool = getTransformTool())
            QMetaObject::invokeMethod(tool, "applyTransform", Qt::DirectConnection);
    });
    addToolBar(Qt::BottomToolBarArea, tabletTransformBar);
    tabletTransformBar->hide();
    auto updateTabletTransformBar = [this, tabletCompact, tabletTransformBar]() {
        const bool active = d->viewManager->canvasBase() &&
            KoToolManager::instance()->activeToolId() == QStringLiteral("KisToolTransform");
        tabletTransformBar->setVisible(tabletCompact->isChecked() && active);
    };
    connect(KoToolManager::instance(), &KoToolManager::changedTool, this,
            [updateTabletTransformBar](KoCanvasController *) {
        updateTabletTransformBar();
    });
    connect(tabletCompact, &QAction::toggled, this,
            [updateTabletTransformBar](bool) { updateTabletTransformBar(); });
''')
    # Grow / Shrink / Feather are selection-specific operations from Krita's
    # own modify-selection extension. Show them only when that extension's
    # actions are registered rather than adding global, unrelated buttons.
    p = "libs/ui/kis_selection_actions_panel.cpp"
    sub(p, """    d->m_handleWidget = new KisSelectionActionsPanelHandle(BUTTON_SIZE, d->orientation, viewManager->canvas());""",
           """#ifdef Q_OS_ANDROID
    for (const auto &spec : {
         std::pair<const char*, const char*>("growselection", "select-grow"),
         std::pair<const char*, const char*>("shrinkselection", "select-shrink"),
         std::pair<const char*, const char*>("featherselection", "select-feather")}) {
        KisAction *action = viewManager->actionManager()->actionByName(QLatin1String(spec.first));
        if (!action) continue;
        auto *button = new KisSelectionActionsPanelButton(
            QLatin1String(spec.second), action->text(), BUTTON_SIZE, viewManager->canvas());
        connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
        connect(button, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
        d->m_buttons.append(button);
    }
    d->m_buttonCount = d->m_buttons.size() + 1;
#endif
    d->m_handleWidget = new KisSelectionActionsPanelHandle(BUTTON_SIZE, d->orientation, viewManager->canvas());""")
    # One-time migration for users who saved the old 250ms default in v2.
    # Preserve explicit non-default values and a deliberately disabled pinch.
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """    QAction *tabletCompact = new QAction(i18n("Compact UI"), this);""",
           """    {
        QSettings defaults;
        if (!defaults.value(QStringLiteral("TabletUi/v3DefaultsMigrated"), false).toBool()) {
            if (defaults.value(QStringLiteral("TabletUi/doubleTapMs"), 250).toInt() == 250)
                defaults.setValue(QStringLiteral("TabletUi/doubleTapMs"), 150);
            defaults.setValue(QStringLiteral("TabletUi/v3DefaultsMigrated"), true);
        }
    }
    QAction *tabletCompact = new QAction(i18n("Compact UI"), this);""")
    p = "libs/ui/kis_selection_actions_panel.cpp"
    sub(p, "#include <QMenu>\n", "#include <QMenu>\n#include <utility>\n")

    # Ensure icon-only toolbar buttons have actual icons in Krita's bundled
    # Android icon resources rather than desktop-theme fallbacks.
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, 'KisIconUtils::loadIcon("color-picker")',
           'KisIconUtils::loadIcon("krita_tool_color_sampler")', 2)
    sub(p, 'KisIconUtils::loadIcon("application-menu")',
           'KisIconUtils::loadIcon("view-list-text")')

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
