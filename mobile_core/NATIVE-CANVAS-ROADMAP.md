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
- The stationary pilot is now a **separate opt-in preview**:
  `MobileStudio/nativeDockCanvasV1=false` by default. Enable it under
  Mobile Studio **More → Experimental stationary canvas UI (next
  restart)**, then restart Krita. The launcher selects the native
  dock path only after restart. The older persistent-host option and
  finger contact-area pressure calibration remain independent.
- This is still **not the default** and requires device validation.
  The native canvas is never moved even on the first entry; all
  painting, layers and documents continue to use Krita's engine.
- `tests/qt_native_canvas_chrome.cpp` exercises **25 enter/Back cycles**
  without reparenting, including toggling a layers dock, tab restoration,
  mobile dock cleanup and an unexpected native central widget replacement.
  Source CI compiles and runs this offscreen, separately from Android
  APK compilation.

## Remaining integration milestones

1. Confirm Qt5 Android build compiles the launcher integration and verify
   offscreen Qt tests. Correct any compile/runtime issues before claiming
   the optional path is usable on a phone.
2. Test the separate opt-in mode with **disposable artwork** only. Enable
   the new checkbox in Mobile Studio's More menu, restart Krita, enter
   the new mode from the existing launcher, make a stroke, switch Back
   and reenter. Confirm the document is not recreated and the canvas
   remains a child of the same QMainWindow.
3. Refine native-host quick/brush/side-dock sizing in portrait/folded
   layouts, accessibility, and modal menu behavior. Keep native Krita
   document Save / Discard / Cancel as the only close implementation.
4. Test real brush strokes, pen pressure, finger-pressure proxy, canvas
   transformations, undo/redo, layers/groups, document switching, save,
   open, close, orientation changes, and interruption/resume.
5. Promote the no-reparent mode once it reaches functional parity, then
   remove the older transfer paths only after a tested migration.

The old *persistent canvas host* still performs a one-time canvas move
on initial entry. The new dock architecture avoids even that first move.

## Known limits

A successful C++/Android compile alone does not prove mobile OpenGL
rendering, correct screen geometry, touch routing, or reliable device
behavior. The new mode is opt-in and disabled by default. If an Android
test exposes broken dock sizing or gestures, switch it off in the pilot's
More menu and restart Krita. Never test it first on an unsaved drawing.

APK publishing remains paused via `APK-UPLOAD-REQUEST.md`.
