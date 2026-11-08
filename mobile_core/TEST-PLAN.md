# Mobile Studio — first independent-window smoke test

This is an **architecture prototype**. It still initializes Krita's back-end
KisMainWindow for document lifecycle, resource management and native actions,
but transfers the real Krita canvas stack into an **independent top-level
mobile window**. The original desktop menu/toolbars and dockers are not
rendered inside Mobile Studio. The prototype is opt-in and reversible.

## Install / entry

- APK comes from the separate `krita-mobile-studio-preview` GitHub release.
- Keep an existing Krita APK or a backup of any drawings before experimenting.
- Start the application normally. From its Compact UI toolbar, choose
  **Mobile Studio (experimental)**. This opens the new native mobile window.
- Tap **Back** on the new top bar to return to the existing Krita UI.
  Do not interpret Android's hardware/system Back as an autosave operation.

## Required smoke checks

- **Canvas transfer:** Create/open a document, enter Mobile Studio,
  confirm the canvas remains visible with no black/blank surface,
  and draw with the pen and finger. The pilot does not alter stylus events.
- **Brushes:** Choose **Brushes...**; search and choose a Krita preset.
  Change size and opacity; confirm strokes change accordingly. Selecting
  Paint, Erase or Blend twice opens the preset picker for that category.
  No second brush engine is involved.
- **Documents:** Use Open and New, edit, Save, close and reopen. Never
  intentionally overwrite a unique drawing for the test.
- **Layer model:** Choose Layers and select a different paint layer.
  It must change Krita's actual active layer.
- **Undo:** Draw, undo, redo. Native Krita history must be preserved.
- **Mode restoration:** Press Mobile Studio Back and re-enter. The original
  Krita interface, canvas data and undo stack must survive both transitions.
- **Rotation/resizing:** Rotate or unfold the device while the new window
  is active. Neither canvas nor brush panel should disappear permanently.

## Known current limitations

- The window is **not yet a separate APK package or executable**. The next
  architecture gate is removing the remaining dependency on the hidden
  Krita desktop host after verifying the canvas can be moved safely.
- Krita's tool and file commands still originate from the existing backend
  action collection. If Android only permits one Qt top-level window, this
  may show a blank surface or fail to open a native dialog; those are
  integration bugs that this test is meant to discover.
- Initial layer picker is deliberately minimal; no mask, lock, drag,
  clipping, or group-edit controls yet.
- Brush preset category detection is a convenience filter, not Krita's
  full categorization/tags implementation.
- Desktop plugins and resources are still packaged. Debloating is a
  separate dependency audit, **not** removing DLLs/Qt components blindly.
- A successful C++/Android build is **not** a guarantee of Android runtime
  stability or drawing correctness.

## Recovery

If the new window is unusable, try returning with its **Back** button. If
it crashes immediately on entry, use the existing stable release, stored
under `krita-android-latest`; the preview does not overwrite that tag.
The source fallback is also at `compact-stable-2026-11-08`.

GitHub local compiler status: `local/android-wsl-build`, Issue #1.
