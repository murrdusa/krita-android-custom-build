#!/usr/bin/env python3
"""Second-stage Android tablet UX customization for Krita v6.0.4.

Run *after* apply_customizations.py. Patches the checked-out Krita tree,
not the copy in the Play Store. The new controls are Android-only.
"""
import sys
from pathlib import Path

def main(root: Path):
    modified = {}
    def change(path, old, new, expected=1):
        src = modified.get(path)
        if src is None:
            src = (root / path).read_text(encoding="utf-8")
        count = src.count(old)
        if count != expected:
            raise RuntimeError(f"{path}: expected {expected} instances of {old[:85]!r}, got {count}")
        modified[path] = src.replace(old, new)

    # Separate, persistent timing control for the existing one-finger
    # double-tap matcher. Default 250ms instead of the Android/Qt interval.
    path = "libs/ui/input/kis_shortcut_matcher.cpp"
    change(path, "#include <QTimer>\n", "#include <QTimer>\n#include <QSettings>\n")
    change(path, "QApplication::doubleClickInterval()",
           'qBound(120, QSettings().value(QStringLiteral("TabletUi/doubleTapMs"), 250).toInt(), 600)',
           expected=2)

    # The normal sampler can still be used continuously. Our new action
    # selects the sampler for precisely one stylus contact, then returns to
    # the freehand brush; the tap never paints a brush stroke.
    path = "plugins/tools/basictools/kis_tool_colorsampler.cc"
    change(path, "#include <QSignalBlocker>\n",
           "#include <QSignalBlocker>\n#include <QSettings>\n#include <QTimer>\n#include <kactioncollection.h>\n")
    change(path, """    m_isActivated = true;
    m_config->load();

    updateOptionWidget();""",
           """    m_isActivated = true;
    m_config->load();
#ifdef Q_OS_ANDROID
    m_config->sampleMerged = QSettings().value(QStringLiteral("TabletUi/sampleMerged"), true).toBool();
#endif

    updateOptionWidget();""")
    change(path, """    m_helper.deactivate();
    requestUpdateOutline(event->point, event);

}""",
           """    m_helper.deactivate();
    requestUpdateOutline(event->point, event);

#ifdef Q_OS_ANDROID
    QSettings tabletSettings;
    if (tabletSettings.value(QStringLiteral("TabletUi/oneShotSampler"), false).toBool()) {
        tabletSettings.setValue(QStringLiteral("TabletUi/oneShotSampler"), false);
        QTimer::singleShot(0, this, [this]() {
            KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas());
            if (!kisCanvas || !kisCanvas->viewManager()) return;
            QAction *brushAction = kisCanvas->viewManager()->actionCollection()->action(
                QStringLiteral("KritaShape/KisToolBrush"));
            if (brushAction) brushAction->trigger();
        });
    }
#endif
}""")

    # The custom actions use Krita's existing command implementations.
    # We do not replace window state/Android immersive mode when Compact UI
    # is toggled, avoiding the status bar regression in Canvas Only mode.
    path = "libs/ui/KisMainWindow.cpp"
    change(path, "#include <QActionGroup>\n", """#include <QActionGroup>
#ifdef Q_OS_ANDROID
#include <QSettings>
#include <QTimer>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QVBoxLayout>
#endif
""")
    change(path, """    d->fullScreenMode = actionManager->createStandardAction(KStandardAction::FullScreen, this, SLOT(viewFullscreen(bool)));

    d->recentFiles""",
           """    d->fullScreenMode = actionManager->createStandardAction(KStandardAction::FullScreen, this, SLOT(viewFullscreen(bool)));

#ifdef Q_OS_ANDROID
    QAction *tabletCompact = new QAction(i18n("Compact UI"), this);
    tabletCompact->setIcon(KisIconUtils::loadIcon("view-fullscreen"));
    tabletCompact->setCheckable(true);
    tabletCompact->setToolTip(i18n("Show or hide menu and document tabs; keep the current workspace and Android immersive mode"));
    actionCollection()->addAction(QStringLiteral("tablet_compact_ui"), tabletCompact);

    connect(tabletCompact, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("TabletUi/compact"), enabled);
        if (enabled) {
            menuBar()->setProperty("tabletWasVisible", menuBar()->isVisible());
            menuBar()->hide();
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setProperty("tabletWasVisible", tabs->isVisible());
                tabs->hide();
            }
            if (statusBar()) {
                statusBar()->setProperty("tabletWasVisible", statusBar()->isVisible());
                statusBar()->hide();
            }
        } else {
            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setVisible(tabs->property("tabletWasVisible").toBool());
            }
            if (statusBar()) {
                statusBar()->setVisible(statusBar()->property("tabletWasVisible").toBool());
            }
        }
    });
    connect(d->mdiArea, &QMdiArea::subWindowActivated, this,
            [this, tabletCompact](QMdiSubWindow *) {
        if (tabletCompact->isChecked()) {
            QTimer::singleShot(0, this, [this, tabletCompact] {
                if (!tabletCompact->isChecked()) return;
                if (QTabBar *tabs = d->findTabBarHACK()) tabs->hide();
            });
        }
    });
    QTimer::singleShot(0, this, [tabletCompact]() {
        tabletCompact->setChecked(QSettings().value(QStringLiteral("TabletUi/compact"), true).toBool());
    });

    QAction *oneShot = new QAction(i18n("Pick Color Once"), this);
    oneShot->setIcon(KisIconUtils::loadIcon("color-picker"));
    oneShot->setToolTip(i18n("Next stylus tap samples a color and returns to the brush"));
    actionCollection()->addAction(QStringLiteral("tablet_pick_once"), oneShot);
    connect(oneShot, &QAction::triggered, this, [this]() {
        QAction *picker = actionCollection()->action(QStringLiteral("KritaSelected/KisToolColorSampler"));
        if (!picker) return;
        QSettings().setValue(QStringLiteral("TabletUi/oneShotSampler"), true);
        picker->trigger();
    });

    QAction *sampleMerged = new QAction(i18n("Sample Visible Layers"), this);
    sampleMerged->setToolTip(i18n("Color picker samples all visible layers when checked, current layer otherwise"));
    sampleMerged->setIcon(KisIconUtils::loadIcon("color-picker"));
    sampleMerged->setCheckable(true);
    sampleMerged->setChecked(QSettings().value(QStringLiteral("TabletUi/sampleMerged"), true).toBool());
    actionCollection()->addAction(QStringLiteral("tablet_sample_merged"), sampleMerged);
    connect(sampleMerged, &QAction::toggled, this, [](bool checked) {
        QSettings().setValue(QStringLiteral("TabletUi/sampleMerged"), checked);
    });

    QAction *resetTabletView = new QAction(i18n("Reset Canvas View"), this);
    resetTabletView->setToolTip(i18n("Reset rotation and fit the entire image"));
    resetTabletView->setIcon(KisIconUtils::loadIcon("zoom-fit-best"));
    actionCollection()->addAction(QStringLiteral("tablet_reset_view"), resetTabletView);
    connect(resetTabletView, &QAction::triggered, this, [this]() {
        QAction *rotation = actionCollection()->action(QStringLiteral("reset_canvas_rotation"));
        QAction *fit = actionCollection()->action(QStringLiteral("zoom_to_fit"));
        if (rotation) rotation->trigger();
        if (fit) fit->trigger();
    });

    QAction *tabletSettings = new QAction(i18n("Tablet Controls..."), this);
    tabletSettings->setIcon(KisIconUtils::loadIcon("configure"));
    actionCollection()->addAction(QStringLiteral("tablet_settings"), tabletSettings);
    connect(tabletSettings, &QAction::triggered, this, [this]() {
        QSettings stored;
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Tablet Controls"));
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QFormLayout *form = new QFormLayout;
        QSpinBox *doubleTapMs = new QSpinBox(&dialog);
        doubleTapMs->setRange(120, 600);
        doubleTapMs->setSuffix(i18n(" ms"));
        doubleTapMs->setValue(stored.value(QStringLiteral("TabletUi/doubleTapMs"), 250).toInt());
        form->addRow(i18n("Double-tap interval"), doubleTapMs);
        QSpinBox *pinchMs = new QSpinBox(&dialog);
        pinchMs->setRange(120, 500);
        pinchMs->setSuffix(i18n(" ms"));
        pinchMs->setValue(stored.value(QStringLiteral("TabletUi/pinchMs"), 250).toInt());
        form->addRow(i18n("Quick-pinch interval (experimental)"), pinchMs);
        QCheckBox *volumeKeys = new QCheckBox(i18n("Volume down: Undo / Volume up: Redo"), &dialog);
        volumeKeys->setChecked(stored.value(QStringLiteral("TabletUi/volumeKeys"), true).toBool());
        form->addRow(volumeKeys);
        QCheckBox *pinchEnabled = new QCheckBox(i18n("Enable quick-pinch reset (experimental)"), &dialog);
        pinchEnabled->setChecked(stored.value(QStringLiteral("TabletUi/quickPinch"), false).toBool());
        form->addRow(pinchEnabled);
        layout->addLayout(form);
        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            stored.setValue(QStringLiteral("TabletUi/doubleTapMs"), doubleTapMs->value());
            stored.setValue(QStringLiteral("TabletUi/pinchMs"), pinchMs->value());
            stored.setValue(QStringLiteral("TabletUi/volumeKeys"), volumeKeys->isChecked());
            stored.setValue(QStringLiteral("TabletUi/quickPinch"), pinchEnabled->isChecked());
        }
    });

    QMenu *quickMenu = new QMenu(i18n("Quick Actions"), this);
    quickMenu->menuAction()->setIcon(KisIconUtils::loadIcon("application-menu"));
    actionCollection()->addAction(QStringLiteral("tablet_quick_actions"), quickMenu->menuAction());
    connect(quickMenu, &QMenu::aboutToShow, this, [this, quickMenu]() {
        quickMenu->clear();
        auto add = [this, quickMenu](const char *name) {
            if (QAction *action = actionCollection()->action(QLatin1String(name))) {
                quickMenu->addAction(action);
            }
        };
        add("file_new"); add("file_open"); add("file_save_as");
        add("file_export_file");
        quickMenu->addSeparator();
        add("select_all"); add("invert_selection"); add("reselect");
        add("deselect"); add("fill_selection_foreground_color");
        quickMenu->addSeparator();
        add("duplicate_layer"); add("merge_layer"); add("quick_group");
        quickMenu->addSeparator();
        add("edit_undo"); add("edit_redo");
        add("tablet_reset_view");
        add("tablet_settings");
    });
#endif

    d->recentFiles""")

    # Keep brush opacity and size sliders in their original tool bar. The
    # Android preset makes common actions visible without crowding those sliders.
    path = "krita/krita5.xmlgui"
    change(path, """  <ToolBar name="mainToolBar" fullWidth="false" noMerge="1">
    <Text>File</Text>
    <Action name="file_new"/>
    <Action name="file_open"/>
    <Action name="file_save"/>
  </ToolBar>
  <ToolBar name="editToolBar" fullWidth="false" noMerge="1">
    <Text>Edit</Text>
    <Action name="edit_undo"/>
    <Action name="edit_redo"/>
  </ToolBar>""", """  <ToolBar name="mainToolBar" fullWidth="false" noMerge="1">
    <Text>File</Text>
    <Action name="file_save"/>
    <Action name="deselect"/>
    <Action name="toggle_layer_alpha_lock"/>
    <Action name="tablet_compact_ui"/>
    <Action name="tablet_quick_actions"/>
  </ToolBar>
  <ToolBar name="editToolBar" fullWidth="false" noMerge="1">
    <Text>Edit</Text>
    <Action name="tablet_pick_once"/>
    <Action name="tablet_sample_merged"/>
    <Action name="view_toggle_reference_images"/>
    <Action name="tablet_reset_view"/>
    <Action name="tablet_settings"/>
  </ToolBar>""")
    change(path, """    <Action name="brushslider2"/>
    <Action name="mirror_actions"/>""",
                 """    <Action name="brushslider2"/>
    <Action name="reload_preset_action"/>
    <Action name="mirror_actions"/>""")

    # Native Android hardware buttons, implemented on Qt's GUI thread.
    path = "packaging/android/apk/src/org/krita/android/JNIWrappers.java"
    change(path, "    public static native boolean hasMainWindowLoaded();",
           """    public static native boolean hasMainWindowLoaded();
    public static native boolean volumeMappingEnabled();
    public static native void volumeUndoRedo(boolean redo);""")
    path = "packaging/android/apk/src/org/krita/android/MainActivity.java"
    change(path, """    @Override
    public boolean onKeyUp(final int keyCode, final KeyEvent event) {""",
           """    @Override
    public boolean onKeyDown(final int keyCode, final KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            if (event.getRepeatCount() == 0) {
                JNIWrappers.volumeUndoRedo(keyCode == KeyEvent.KEYCODE_VOLUME_UP);
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(final int keyCode, final KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            return true;
        }""")
    path = "krita/main.cc"
    change(path, """#ifdef Q_OS_ANDROID
#include <QtAndroid>""",
           """#ifdef Q_OS_ANDROID
#include <QSettings>
#include <QtAndroid>""")
    change(path, """#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main""",
           """extern "C" JNIEXPORT jboolean JNICALL
Java_org_krita_android_JNIWrappers_volumeMappingEnabled(JNIEnv*, jobject)
{
    return QSettings().value(QStringLiteral("TabletUi/volumeKeys"), true).toBool();
}

extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_volumeUndoRedo(JNIEnv*, jobject, jboolean redo)
{
    if (!KisPart::exists()) return;
    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (window) {
        QMetaObject::invokeMethod(window, redo ? "redo" : "undo", Qt::QueuedConnection);
    }
}

#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main""")

    # Increase Android selection bar touch targets; pin it to the bottom in
    # compact mode. Uses Krita's existing selection actions and undo logic.
    path = "libs/ui/kis_selection_actions_panel.cpp"
    change(path, """static constexpr int BUTTON_SIZE = 30;""",
           """#ifdef Q_OS_ANDROID
static constexpr int BUTTON_SIZE = 44;
#else
static constexpr int BUTTON_SIZE = 30;
#endif""")
    # Krita already has a fixed-bottom behavior. Keep its users' normal
    # settings when not in Compact UI.
    change(path, """    configChanged(true);

}""",
           """    configChanged(true);

}""") if False else None

    for path, contents in modified.items():
        (root / path).write_text(contents, encoding="utf-8")
        print(f"tablet UI patch: {path}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: apply_tablet_update.py /path/to/krita")
    main(Path(sys.argv[1]))
