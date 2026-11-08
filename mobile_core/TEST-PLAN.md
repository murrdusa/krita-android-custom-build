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

## v0.5 tests — independent commands and bridge

- In Mobile Studio, open **More → Search all Krita commands**. Search for
  "save", "undo", a tool name and a command identifier. Results should update
  without opening the desktop menu bar.
- Invoke Undo/Redo or Save from the search results. Native Krita history and
  file dialogs must work correctly, including when the Layers panel is open.
- Open **More → Legacy menus (compatibility)** and confirm older commands
  remain accessible where the browser does not yet expose them.
- Switch Paint, Erase and Blend presets and alter size/opacity. The brush
  selection popup should appear over the mobile window (not behind the hidden
  desktop main window).
- Draw, save, close and reopen a disposable document. Group layers and undo.
  The previous v0.4 behavior must be unchanged.
- Leave and reenter Mobile Studio, rotate or unfold, and confirm the live
  canvas, layers and tool state survive. Input pressure is still Krita-owned.

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

## Layer workspace v0.4 — inline, nonmodal canvas companion

The Layers control now toggles a nonmodal panel on the side of the live
painting canvas. This UI is new; all operations still delegate to Krita's
original undo-enabled node manager.

Test on an expendable canvas:
- Tap Layers, draw a stroke with the panel open, and press main-window
  Undo/Redo without closing the panel. Neither should be blocked.
- Create, duplicate, delete, group and reorder layers while the panel is open.
  The tree should update after structural Undo/Redo and external changes.
- Change tabs/documents, then open Layers and confirm only current document
  layers appear, with no stale names from the previous document.
- Tap Close on the panel and then reopen it; verify it occupies no canvas
  space while hidden and doesn't reset the painting state.
- Fold/unfold or rotate the phone: panel should remain usable and canvas
  should receive pen pressure with the panel open. If the canvas becomes too
  narrow in portrait mode, that is a UI layout issue to fix next.
- Expand nested groups, collapse all, and use Undo or another native
  change: group expansion state should not unexpectedly reset.
- Tap multi-select and verify the highlighted node selection matches
  duplicate/group/delete operations. Confirm Undo history is native.
- Back out to the legacy window with the panel open, then return to
  Mobile Studio. The canvas and document must remain intact.

This version does not yet attempt touch drag/drop reordering or a miniature
thumbnail catalog, and does not remove the legacy Krita backend service.
The engine-first boundary is unchanged.

## Layer workspace v0.2 checks

The new Layers panel now reads Krita's real node tree, preserving nested
groups and masks. Please test the following with a **disposable** document:

- Create paint layers and groups; check the order and indentation.
- Tap one layer to activate it; toggle Multi-select and include two layers.
  The highlighted rows and selected-count label should agree.
- Duplicate a selected layer; use Up/Down; verify Krita undo/redo reverses
  mutations correctly.
- Show/Hide and Lock/Unlock should update the rows and the canvas.
- Group and Ungroup should change the native layer hierarchy, not just its
  appearance. Note that a layer may refuse invalid operations under Krita's
  rules; the UI intentionally delegates those checks.
- Delete requires confirmation and uses Krita's undoable remove operation.
- A selected paint layer should remain selected after exiting the panel.
- Switch in/out of Mobile Studio and reopen the layer panel to verify no
  missing/duplicate layers or loss of document edits.

This is a native-model-backed pilot, **not yet a completed layer interface**.
The next phases add drag and nesting, thumbnail previews, clipping UX, and
a model-driven rather than modal panel. Layer-type-specific availability and
multi-layer selection also need testing on Android.

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
