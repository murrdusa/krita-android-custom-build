# Native retained-canvas Mobile Studio migration

## Goal

**Do not move Krita's painting canvas at all.** Keep Krita's original
QMainWindow central widget, MDI/KisView structure, brush engines, undo,
document file handling, and tablet input attached for the entire session.
Switch only the mobile chrome around it.

## Implemented architecture proof

- `native_canvas_chrome.h` is a Qt-only dock controller.
  It installs top and bottom mobile panels and optional layers panel in
  ordinary `QMainWindow` dock areas. It never uses
  `takeCentralWidget()`, `setCentralWidget()` or
  `m_nativeCanvas->setParent()`.
- Desktop menu, status, toolbars, dockers and native MDI tab visibility
  are snapshotted per entry and restored on Back. On a replaced native
  central canvas, the adapter refuses reentry and restores desktop UI
  without overwriting the replacement.
- `native_canvas_pilot.h` assembles existing **real mobile widgets**
  around that adapter: Paint/Erase/Blend brush workspace, configurable
  Quick Actions, native Krita document chooser, layers panel, color
  wheel, menu browser, and finger-pressure calibration. It uses
  `MobileEngineBridge` and the same Krita native actions rather than
  reproducing any painting or document model.
- The stationary pilot is **source-compiled but NOT yet connected to the
  normal Mobile Studio launch action**. The existing mobile presentation
  and persistent-host option remain unchanged, as does the full-range
  finger contact-area pressure calibration.
- `tests/qt_native_canvas_chrome.cpp` exercises **25 enter/Back cycles**
  without reparenting, including toggling a layers dock, tab restoration,
  mobile dock cleanup and an unexpected native central widget replacement.
  Source CI compiles and runs this offscreen, separately from Android
  APK compilation.

## Remaining integration milestones

1. Confirm Qt5 Android build compiles the pilot headers and verify
   offscreen Qt tests. Correct any compile/runtime issues before making
   a user-facing toggle.
2. Refine native-host quick/brush/side-dock sizing in portrait/folded
   layouts, accessibility, and modal menu behavior. Keep native Krita
   document Save / Discard / Cancel as the only close implementation.
3. Add a separate **opt-in no-reparent mode** to the MobileAppController
   with safe startup fallback. Do not change the existing default until
   on-device evidence shows native EGL and touch/stylus painting survive
   repeated mode switches.
4. Test real brush strokes, pen pressure, finger-pressure proxy, canvas
   transformations, undo/redo, layers/groups, document switching, save,
   open, close, orientation changes, and interruption/resume. Check that
   no canvas object is constructed or deleted in a mode transition.
5. Promote the no-reparent mode once it reaches functional parity, then
   remove the older transfer paths only after a tested migration.

The old *persistent canvas host* still performs a one-time canvas move
on initial entry. The new dock architecture avoids even that first move.

## Known limits

A successful C++/Android compile alone does not prove mobile OpenGL
rendering, correct screen geometry, touch routing, or reliable device
behavior. This pilot remains disabled until there is on-device coverage.

APK publishing remains paused via `APK-UPLOAD-REQUEST.md`.
