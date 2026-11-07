#!/usr/bin/env python3
"""v4: Fix Krita Android compact chrome, welcome toolbar, gesture defaults and controls.
Apply after apply_customizations.py, apply_tablet_update.py, apply_tablet_refinements.py.
Target is KDE/Krita v6.0.4, Qt5/Android. Source replacement is strictly checked.
"""
from pathlib import Path
import hashlib
import re
import sys

def main(root):
    changed = {}
    def get(path):
        if path not in changed:
            changed[path] = (root / path).read_text(encoding="utf-8")
        return changed[path]
    def sub(path, old, new, n=1):
        data = get(path)
        found = data.count(old)
        if found != n:
            raise RuntimeError(f"v4 {path}: expected {n} occurrences, got {found}: {old[:120]!r}")
        changed[path] = data.replace(old, new)
    def span(path, before, after, new):
        data = get(path)
        if data.count(before) != 1:
            raise RuntimeError(f"v4 {path}: ambiguous start {before[:100]!r}")
        pos = data.index(before)
        if data.count(after, pos) != 1:
            raise RuntimeError(f"v4 {path}: ambiguous end {after[:100]!r}")
        end = data.index(after, pos)
        changed[path] = data[:pos] + new + data[end:]
    def edit(path, fn):
        changed[path] = fn(get(path))

    p = "libs/ui/KisMainWindow.cpp"
    # Qt's QMdiArea retains a viewport top margin based on QTabBar::sizeHint
    # even when the tab is hidden or forced to height 0. We switch its internal
    # view to frameless MDI subwindows instead, then return to tabbed view.
    # This is distinct from Android immersive/fullscreen and preserves dockers.
    sub(p, """            const KisConfig previous(true);
            setProperty("tabletPrevSelectionEnabled", previous.selectionActionBar());""",
           """            if (d->mdiArea->viewMode() != QMdiArea::SubWindowView) {
                d->mdiArea->setProperty("tabletPrevMdiMode", int(d->mdiArea->viewMode()));
                d->mdiArea->setViewMode(QMdiArea::SubWindowView);
            }
            for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                if (!subwin->property("tabletFrameSaved").toBool()) {
                    subwin->setProperty("tabletPrevFlags", int(subwin->windowFlags()));
                    subwin->setProperty("tabletFrameSaved", true);
                }
                subwin->setWindowFlags(subwin->windowFlags() | Qt::FramelessWindowHint);
                subwin->showMaximized();
            }
            const KisConfig previous(true);
            setProperty("tabletPrevSelectionEnabled", previous.selectionActionBar());""")
    sub(p, """            menuBar()->hide();
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setProperty("tabletWasVisible", tabs->isVisible());
                tabs->setProperty("tabletPrevMinH", tabs->minimumHeight());
                tabs->setProperty("tabletPrevMaxH", tabs->maximumHeight());
                tabs->setFixedHeight(0);
                tabs->hide();
            }
            if (statusBar()) {""",
           """            menuBar()->hide();
            if (statusBar()) {""")
    sub(p, """            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setMinimumHeight(tabs->property("tabletPrevMinH").toInt());
                tabs->setMaximumHeight(tabs->property("tabletPrevMaxH").toInt());
                tabs->setVisible(tabs->property("tabletWasVisible").toBool());
            }
            if (statusBar()) {""",
           """            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());
            for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                if (subwin->property("tabletFrameSaved").toBool()) {
                    subwin->setWindowFlags(Qt::WindowFlags(
                        static_cast<Qt::WindowType>(subwin->property("tabletPrevFlags").toInt())));
                    subwin->setProperty("tabletFrameSaved", false);
                }
            }
            const auto originalViewMode = d->mdiArea->property("tabletPrevMdiMode").isValid()
                ? static_cast<QMdiArea::ViewMode>(d->mdiArea->property("tabletPrevMdiMode").toInt())
                : QMdiArea::TabbedView;
            d->mdiArea->setViewMode(originalViewMode);
            if (QTabBar *tabs = d->findTabBarHACK()) {
                customizeTabBar();
                tabs->setExpanding(true);
                tabs->setElideMode(Qt::ElideRight);
                tabs->setAcceptDrops(true);
                tabs->setChangeCurrentOnDrag(true);
            }
            for (QMdiSubWindow *subwin : d->mdiArea->subWindowList()) {
                subwin->showMaximized();
            }
            if (statusBar()) {""")
    sub(p, """                if (QTabBar *tabs = d->findTabBarHACK()) { tabs->setFixedHeight(0); tabs->hide(); }
                if (d->viewManager->canvasBase()) {""",
           """                if (QMdiSubWindow *subwin = d->mdiArea->activeSubWindow()) {
                    if (!subwin->property("tabletFrameSaved").toBool()) {
                        subwin->setProperty("tabletPrevFlags", int(subwin->windowFlags()));
                        subwin->setProperty("tabletFrameSaved", true);
                        subwin->setWindowFlags(subwin->windowFlags() | Qt::FramelessWindowHint);
                    }
                    if (!subwin->isMaximized()) subwin->showMaximized();
                }
                if (d->viewManager->canvasBase()) {""")

    # Welcome: stock Krita leaves mainToolBar visible. The custom buttons are
    # image-specific and should not appear before a document is open.
    sub(p, """        if (toolbar->objectName() != "mainToolBar") {
            toolbar->hide();
        }""",
           """#ifdef Q_OS_ANDROID
        if (toolbar->objectName() == QStringLiteral("mainToolBar"))
            toolbar->setProperty("tabletHiddenForWelcome", true);
        toolbar->hide();
#else
        if (toolbar->objectName() != "mainToolBar") {
            toolbar->hide();
        }
#endif""")
    sub(p, """    else {
        setAutoSaveSettings(d->windowStateConfig, false);
        statusBar()->setVisible(KisConfig(true).showStatusBar());
    }

    QList<QAction *> actions""",
           """    else {
        setAutoSaveSettings(d->windowStateConfig, false);
#ifdef Q_OS_ANDROID
        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (toolbar->property("tabletHiddenForWelcome").toBool()) {
                toolbar->setProperty("tabletHiddenForWelcome", false);
                toolbar->show();
            }
        }
#endif
        statusBar()->setVisible(KisConfig(true).showStatusBar());
    }

    QList<QAction *> actions""")

    # The menu action is a real icon-only button. Krita's reference action
    # arrives without an icon and Qt otherwise shows its long text.
    sub(p, """        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (toolbar->objectName() == QStringLiteral("mainToolBar")""",
           """        if (QAction *ref = actionCollection()->action(QStringLiteral("view_toggle_reference_images"))) {
            ref->setIcon(KisIconUtils::loadIcon("krita_tool_reference_images"));
            ref->setIconText(i18n("References"));
        }
        for (QToolBar *toolbar : findChildren<QToolBar*>()) {
            if (toolbar->objectName() == QStringLiteral("mainToolBar")""")
    sub(p, """    sampleMerged->setIcon(KisIconUtils::loadIcon("krita_tool_color_sampler"));""",
           """    sampleMerged->setIcon(KisIconUtils::loadIcon("layer-visible-off"));""")
    # Allow a distinct compact checkable icon, preserving the existing tooltip.
    # Normal brush blend mode selector shouldn't consume an entire toolbar segment.
    p = "libs/ui/kis_paintop_box.cc"
    sub(p, """    m_cmbCompositeOp->setMinimumWidth(60);
    m_cmbCompositeOp->setMaximumWidth(116);""",
           """    m_cmbCompositeOp->setMinimumWidth(56);
    m_cmbCompositeOp->setMaximumWidth(90);""")

    # Simplify volume settings: the 4 mappings already support "None", so
    # separate "enable short" and "enable long" checkboxes were redundant.
    p = "libs/ui/KisMainWindow.cpp"
    span(p, """        QCheckBox *volumeShortEnabled = new QCheckBox(""",
         """        QSpinBox *volumeLongMs = new QSpinBox(&dialog);""", "")
    sub(p, """        QCheckBox *volumeKeys = new QCheckBox(i18n("Volume down: Undo / Volume up: Redo"), &dialog);""",
           """        QCheckBox *volumeKeys = new QCheckBox(i18n("Use volume buttons for Krita shortcuts"), &dialog);""")
    sub(p, """            stored.setValue(QStringLiteral("TabletUi/volumeShortEnabled"), volumeShortEnabled->isChecked());
            stored.setValue(QStringLiteral("TabletUi/volumeLongEnabled"), volumeLongEnabled->isChecked());""",
           """            stored.setValue(QStringLiteral("TabletUi/volumeShortEnabled"), true);
            stored.setValue(QStringLiteral("TabletUi/volumeLongEnabled"), true);""")
    p = "krita/main.cc"
    sub(p, """    if (longPress && !s.value(QStringLiteral("TabletUi/volumeLongEnabled"), true).toBool()) return 0;
    if (!longPress && !s.value(QStringLiteral("TabletUi/volumeShortEnabled"), true).toBool()) return 0;
""","")
    # Install a mirror double-tap binding in users' existing profiles only
    # once, and only when the gesture is unassigned. Never erase a user binding.
    p = "libs/ui/input/kis_input_manager.cpp"
    sub(p, '#include "kis_shortcut_configuration.h"\n',
           '#include "kis_shortcut_configuration.h"\n#ifdef Q_OS_ANDROID\n#include "KisTouchGestureAction.h"\n#endif\n')
    sub(p, """    KisInputProfile *profile = KisInputProfileManager::instance()->currentProfile();
    if (profile) {
        const QList<KisShortcutConfiguration*> shortcuts = profile->allShortcuts();""",
           """    KisInputProfile *profile = KisInputProfileManager::instance()->currentProfile();
    if (profile) {
#ifdef Q_OS_ANDROID
        QSettings tabletSettings;
        if (!tabletSettings.value(QStringLiteral("TabletUi/mirrorProfileMigrationV4"), false).toBool()) {
            bool assigned = false;
            for (KisShortcutConfiguration *entry : profile->allShortcuts()) {
                if (entry->type() == KisShortcutConfiguration::GestureType &&
                    entry->gesture() == KisShortcutConfiguration::OneFingerDoubleTap) {
                    assigned = true;
                    break;
                }
            }
            if (!assigned) {
                for (KisAbstractInputAction *input : KisInputProfileManager::instance()->actions()) {
                    if (dynamic_cast<KisTouchGestureAction*>(input)) {
                        auto *binding = new KisShortcutConfiguration();
                        binding->setAction(input);
                        binding->setType(KisShortcutConfiguration::GestureType);
                        binding->setGesture(KisShortcutConfiguration::OneFingerDoubleTap);
                        binding->setMode(KisTouchGestureAction::MirrorView);
                        profile->addShortcut(binding);
                        KisInputProfileManager::instance()->saveProfiles();
                        break;
                    }
                }
            }
            tabletSettings.setValue(QStringLiteral("TabletUi/mirrorProfileMigrationV4"), true);
        }
#endif
        const QList<KisShortcutConfiguration*> shortcuts = profile->allShortcuts();""")

    # Reference-image two-finger manipulation. The first finger establishes
    # ownership of the reference. The second finger moves/scales/rotates just
    # that KoShape (not the canvas). A single undo command stores the original
    # local transformation and the final local transformation.
    p = "libs/ui/input/kis_input_manager.cpp"
    sub(p, "#include <KoShapeMoveCommand.h>\n",
           "#include <KoShapeMoveCommand.h>\n#include <KoShapeTransformCommand.h>\n#include <QTransform>\n#include <QLineF>\n")
    sub(p, """    QPointF startingShapePosition;
};
QHash<KisInputManager*, TabletReferenceDrag> tabletReferenceDrags;""",
           """    QPointF startingShapePosition;
    QTransform initialTransform;
    QTransform twoFingerBase;
    QPointF startCenter;
    qreal startDistance = 1.0;
    qreal startAngle = 0.0;
    bool inTwoFinger = false;
};
QHash<KisInputManager*, TabletReferenceDrag> tabletReferenceDrags;""")
    sub(p, """&& referenceTouch->touchPoints().size() == 1 && KisPart::exists())""",
           """&& !referenceTouch->touchPoints().isEmpty() && KisPart::exists())""")
    sub(p, """                        drag.startingShapePosition = shape->absolutePosition();
                        tabletReferenceDrags.insert(this, drag);""",
           """                        drag.startingShapePosition = shape->absolutePosition();
                        drag.initialTransform = shape->transformation();
                        tabletReferenceDrags.insert(this, drag);""")
    sub(p, """            if (drag.canvas && drag.shape && !touchEvent->touchPoints().isEmpty()) {
                QPointF now = drag.canvas->coordinatesConverter()->widgetToDocument(
                    touchEvent->touchPoints().first().pos());
                drag.shape->update();
                drag.shape->setAbsolutePosition(
                    drag.startingShapePosition + now - drag.startingDocPoint);
                drag.shape->update();
            }""",
           """            if (drag.canvas && drag.shape && !touchEvent->touchPoints().isEmpty()) {
                auto toDoc = [&drag](const QPointF &point) {
                    return drag.canvas->coordinatesConverter()->widgetToDocument(point);
                };
                const auto &points = touchEvent->touchPoints();
                if (points.size() >= 2 &&
                    QSettings().value(QStringLiteral("TabletUi/referenceTwoFinger"), true).toBool()) {
                    const QPointF first = toDoc(points.at(0).pos());
                    const QPointF second = toDoc(points.at(1).pos());
                    const QPointF mid = 0.5 * (first + second);
                    const QLineF line(first, second);
                    if (!drag.inTwoFinger) {
                        drag.inTwoFinger = true;
                        drag.twoFingerBase = drag.shape->transformation();
                        drag.startCenter = mid;
                        drag.startDistance = qMax(qreal(1.0), line.length());
                        drag.startAngle = line.angle();
                    } else {
                        const qreal factor = qBound(qreal(0.15),
                             line.length() / drag.startDistance, qreal(6.0));
                        const qreal angle = drag.startAngle - line.angle();
                        QTransform gesture;
                        gesture.translate(mid.x(), mid.y());
                        gesture.rotate(angle);
                        gesture.scale(factor, factor);
                        gesture.translate(-drag.startCenter.x(), -drag.startCenter.y());
                        drag.shape->update();
                        drag.shape->setTransformation(drag.twoFingerBase);
                        drag.shape->applyAbsoluteTransformation(gesture);
                        drag.shape->update();
                    }
                } else {
                    const QPointF now = toDoc(points.first().pos());
                    if (drag.inTwoFinger) {
                        drag.inTwoFinger = false;
                        drag.startingShapePosition = drag.shape->absolutePosition();
                        drag.startingDocPoint = now;
                    }
                    drag.shape->update();
                    drag.shape->setAbsolutePosition(
                        drag.startingShapePosition + now - drag.startingDocPoint);
                    drag.shape->update();
                }
            }""")
    sub(p, """                QPointF finalPosition = drag.shape->absolutePosition();
                if (QLineF(drag.startingShapePosition, finalPosition).length() > 0.01) {
                    QList<KoShape*> shapes = {drag.shape};
                    QList<QPointF> initial = {drag.startingShapePosition};
                    QList<QPointF> final = {finalPosition};
                    drag.canvas->addCommand(new KoShapeMoveCommand(shapes, initial, final));
                }""",
           """                const QTransform finalTransform = drag.shape->transformation();
                if (drag.initialTransform != finalTransform) {
                    drag.canvas->addCommand(new KoShapeTransformCommand(
                        {drag.shape}, {drag.initialTransform}, {finalTransform}));
                }""")
    sub(p, """                drag.shape->setAbsolutePosition(drag.startingShapePosition);
                drag.shape->update();""",
           """                drag.shape->setTransformation(drag.initialTransform);
                drag.shape->update();""")
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """        form->addRow(referenceFingerDrag);
        layout->addLayout(form);""",
           """        form->addRow(referenceFingerDrag);
        QCheckBox *referenceTwoFinger = new QCheckBox(i18n("Two-finger resize and rotate reference images"), &dialog);
        referenceTwoFinger->setChecked(stored.value(QStringLiteral("TabletUi/referenceTwoFinger"), true).toBool());
        form->addRow(referenceTwoFinger);
        layout->addLayout(form);""")
    sub(p, """            stored.setValue(QStringLiteral("TabletUi/referenceFingerDrag"), referenceFingerDrag->isChecked());""",
           """            stored.setValue(QStringLiteral("TabletUi/referenceFingerDrag"), referenceFingerDrag->isChecked());
            stored.setValue(QStringLiteral("TabletUi/referenceTwoFinger"), referenceTwoFinger->isChecked());""")

    # Compact UI hold tuning and optional haptic response.
    p = "libs/ui/KisMainWindow.cpp"
    sub(p, """        form->addRow(referenceTwoFinger);
        layout->addLayout(form);""",
           """        form->addRow(referenceTwoFinger);
        QSpinBox *tabletHoldMs = new QSpinBox(&dialog);
        tabletHoldMs->setRange(160,600);
        tabletHoldMs->setSuffix(i18n(" ms"));
        tabletHoldMs->setValue(stored.value(QStringLiteral("TabletUi/longPressMs"),260).toInt());
        form->addRow(i18n("Compact UI hold delay"),tabletHoldMs);
        QCheckBox *hapticLongPress = new QCheckBox(
            i18n("Vibrate when a hold action is triggered"),&dialog);
        hapticLongPress->setChecked(stored.value(
            QStringLiteral("TabletUi/hapticLongPress"),false).toBool());
        form->addRow(hapticLongPress);
        QSpinBox *compactToolbarIcons=new QSpinBox(&dialog);
        compactToolbarIcons->setRange(20,72);
        compactToolbarIcons->setSingleStep(2);
        compactToolbarIcons->setSuffix(i18n(" px"));
        compactToolbarIcons->setValue(qBound(20,stored.value(
            QStringLiteral("TabletUi/compactToolbarIconSize"),26).toInt(),72));
        form->addRow(i18n("Compact toolbar icon size"),compactToolbarIcons);
        layout->addLayout(form);""")
    sub(p, """            stored.setValue(QStringLiteral("TabletUi/referenceTwoFinger"), referenceTwoFinger->isChecked());""",
           """            stored.setValue(QStringLiteral("TabletUi/referenceTwoFinger"), referenceTwoFinger->isChecked());
            stored.setValue(QStringLiteral("TabletUi/longPressMs"),tabletHoldMs->value());
            stored.setValue(QStringLiteral("TabletUi/hapticLongPress"),hapticLongPress->isChecked());
            stored.setValue(QStringLiteral("TabletUi/compactToolbarIconSize"),
                            compactToolbarIcons->value());""")

    p = "packaging/android/apk/src/org/krita/android/MainActivity.java"
    sub(p, """    public static int getLongPressTimeout() {""",
           """    // Optional low-amplitude feedback for Compact UI long-press menus.
    public static void vibrateHold() {
        final Activity activity = QtNative.activity();
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            android.os.Vibrator vibrator = (android.os.Vibrator)
                activity.getSystemService(android.content.Context.VIBRATOR_SERVICE);
            if (vibrator == null || !vibrator.hasVibrator()) return;
            if (Build.VERSION.SDK_INT >= 26) {
                vibrator.vibrate(android.os.VibrationEffect.createOneShot(
                    25, android.os.VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                vibrator.vibrate(25);
            }
        });
    }

    public static int getLongPressTimeout() {""")
    p = "packaging/android/apk/AndroidManifest.xml"
    sub(p, """    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />""",
           """    <uses-permission android:name="android.permission.VIBRATE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />""")

    # Use the same user-adjustable hold time for Krita's native Android
    # context-menu long press, not just the Compact UI custom buttons.
    p = "libs/ui/KisLongPressEventFilter.cpp"
    sub(p, "#include <QTimer>\n", "#include <QTimer>\n#include <QSettings>\n")
    sub(p, """            int longPressInterval = m_longPressTimeout;""",
           """            int longPressInterval=qBound(160,QSettings().value(
                QStringLiteral("TabletUi/longPressMs"),260).toInt(),600);""")
    sub(p, """    if (!m_handlingEvent && isContextMenuTarget(target)) {
        QScopedValueRollback rollback(m_handlingEvent, true);""",
           """    if (!m_handlingEvent && isContextMenuTarget(target)) {
        QScopedValueRollback rollback(m_handlingEvent, true);
#ifdef Q_OS_ANDROID
        if(QSettings().value(QStringLiteral("TabletUi/hapticLongPress"),false).toBool())
            QAndroidJniObject::callStaticMethod<void>(
                "org/krita/android/MainActivity","vibrateHold","()V");
#endif""")

    # The XML GUI configuration is identical to v3, but moving to v4 bumps
    # Krita's XMLGUI version so the corrected toolbar is reloaded on upgrades.
    p = "krita/CMakeLists.txt"
    sub(p, "set(KRITA5_XMLGUI_VERSION 546)", "set(KRITA5_XMLGUI_VERSION 547)")
    # no xml hash change this pass; keep current validated checksum.

    for path, contents in changed.items():
        (root / path).write_text(contents, encoding="utf-8")
        print("v4 patched",path)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: apply_tablet_polish.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
