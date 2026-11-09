# Mobile Studio — Android painting and mode-switch smoke test

## v1.19 guarded native canvas selection and closing

- Create two or more disposable, unsaved Krita documents. Open
  **Canvases**, activate the intended document, then choose Close
  Current. Verify Krita's native Save / Discard / Cancel dialog refers
  to that drawing and does not close another one.
- Repeat using **More → Close current canvas** and Quick Actions'
  optional **Close Canvas** shortcut. All three paths must target the
  same current native view. Cancel must preserve the document, edits,
  modified state and other open views.
- With multiple views of the same Krita document, verify that only
  one MDI view is considered active. If Qt cannot identify one view,
  Close Current must be disabled, not silently redirected to a
  different file; the Canvases dialog explains how to select one.
- Switch and close documents rapidly, including while the Canvases
  popup is dismissing. Deferred actions must hold a guarded identity,
  not an integer MDI index. Destroyed/detached views must be rejected.
- Repeat after Back/reenter and in the opt-in persistent canvas host.
  No canvas is constructed or destroyed by the mobile selection logic;
  the native Krita close/save lifecycle remains authoritative.
- The **Qt-only MDI view identity smoke test** constructs synthetic
  subwindows, deletes one ahead of the target, removes another and
  destroys the saved target. Resolve must return only the original
  live view or no view. This does not simulate Krita's dialogs and
  must be paired with the Android manual tests above.
- The user-controlled 0–100% finger-pressure calibration and stylus
  input must remain unaffected by canvas navigation.

## v1.18 full 0–100% finger pressure and pad sampling

- Keep user-calibrated contact diameter minimum and maximum exactly as
  saved. Only the mapped *pressure output range* changes from 16–96% to
  the full 0–100% supported by the brush engine.
- At or below calibrated minimum, a synthetic finger touch must produce
  precisely 0.0 pressure; at/above maximum it must produce 1.0; a
  halfway contact must produce 0.5. The first dab uses the correct
  absolute-size target, not a relative per-stroke baseline.
- Smoothing continues to limit abrupt *changes* during a stroke but
  does not clamp initial or final output to the former 16–96% limits.
  An actual brush may leave no visible mark when pressure is 0.
- In the calibration dialog check **0% and 100%** labels, verify that
  Capture minimum/maximum use the median of valid contact samples,
  and that zero/unsupported contact ellipses do not overwrite values.
- Confirm device pen pressure, multitouch gestures, persisted
  calibration, and the disabled-by-default finger-pressure switch
  remain unchanged. Verify offscreen Qt tests and Android build
  separately; Android contact-area support remains device-dependent.

## v1.17 calibrated absolute finger pressure

- Confirm the existing finger-pressure toggle remains **off by default**
  and the separate persistent-host toggle is unaffected.
- In Mobile Studio → More, choose **Calibrate finger pressure
  (minimum / maximum)**. Check the 8px / 24px initial approximate
  defaults, capture a light small contact as the minimum and a wider
  contact as the maximum, and inspect the live diameter/pressure preview.
- Cancel the dialog: previously saved minimum/maximum must remain
  unchanged. Reopen, enter valid manual values and Save; reopen again to
  confirm both persist.
- Attempt an invalid range (minimum >= maximum): Save must be disabled.
  With unavailable/zero contact ellipses, the capture pad must explain
  that the user can edit values manually, not silently save 0px.
- Enable finger pressure, paint separate thumb and pinky strokes with
  a pressure-sensitive brush. Different **initial contact diameters**
  must yield different first-dab pressure; changing contact during
  a stroke should remain smoothed.
- Recalibrate between strokes. The next stroke must use the new range;
  an ongoing stroke must not change pressure due to a settings change.
- Check S Pen, mouse/pen buttons, touch gestures, and two-finger pan/zoom
  are unchanged in both UI modes. Disable the pressure toggle and confirm
  exact Krita native input behavior returns.
- Copy diagnostics: saved min/max must be present alongside observed
  contact range. If Qt does not report contact dimensions, the pressure
  proxy must fall back to native pressure without jitter.
- Qt CI must compile and run the headless pure-input mapping test and
  the Qt Widgets calibration-dialog persistence test. These do not
  replace Android device testing.

## v1.16 persistent canvas ownership vs visible mobile UI

- Open a saved disposable KRA and enter the persistent-host experiment.
  On Back, the native canvas should still be in Mobile Studio's central
  wrapper even though its controls are hidden. Krita's Android window
  flash workaround must remain suppressed for that embedded widget.
- If Compact UI was active before Mobile Studio, it should reactivate
  after Back. The distinct `_mobileStudioPresentationActive` flag is
  false when mobile chrome is hidden; it is not the same as the native
  canvas transfer/ownership state.
- Immediately reenter and repeat ten times while checking the same
  document, layers, paint data and undo history. Repaint requests after
  the Qt layout pass must not recreate a document or canvas widget.
- While the default mode is active, switch the persistent-host preference
  ON through More, then Back and reenter *without restarting*. It must
  remain on the original default strategy during that process.
- After a real app restart, the persistent-host preference may take
  effect. Switch it OFF while the persistent host is already in use:
  it must continue that strategy until another real restart, without
  detaching or deleting the canvas mid-session.
- Check that the independent finger contact-pressure option persists
  across all these UI changes and that S Pen pressure is unchanged.
  Neither setting should reset the other.
- Test Krita's document-open and document-close dialogs from both
  visible UI modes, including Save / Discard / Cancel and zero-document
  welcome screens. No fabricated file operations or modal changes
  are part of the canvas-host code.

## v1.15 opt-in persistent canvas host (no repeated canvas moves)

- Keep the experimental finger contact-pressure setting unchanged. The
  persistent canvas experiment is independently disabled by default:
  `MobileStudio/persistentCanvasHostV1=false`.
- With disposable KRA artwork saved to disk, enable **More → Experimental
  persistent canvas host (next restart)**. Restart Krita, enter Mobile
  Studio, paint, press Back and reenter Mobile Studio repeatedly.
- After the first Mobile Studio attachment, every subsequent Back/enter
  must retain one identical native canvas widget. No
  `takeCentralWidget()`, `setCentralWidget()` or `setParent()` is
  permitted during those later mode switches. Only mobile header/quick
  actions/brush controls and native Krita menus/status/dockers change.
- Back must show the native Krita menu, toolbars, status bar and original
  canvas. It must **not** leave the mobile Quick Actions or brush controls
  over the desktop. Reentry must restore the mobile controls, including
  the experimental finger-pressure toggle state.
- Open, create, save and close drawings using both interfaces. Check
  native Save / Discard / Cancel; test multi-document selection, undo,
  eraser, layers and pen pressure. Confirm that mobile/desktop switching
  does not clear document modified state or lose painted pixels.
- Rotate the device and switch 10+ times. Compare before/after canvas
  snapshots, Qt input logs and whether the screen flashes or turns blank.
- The first entry still performs **one** native-parent transfer, so this
  does not yet remove every graphics-context risk. This milestone is
  gated on Android testing before becoming the default.
- To disable after using the persistent host, uncheck the toggle and
  **restart Krita**. Switching off a setting mid-session must not
  destroy its currently attached native canvas.
- The source-only Qt smoke test performs ten toggles in both the default
  and persistent paths, checking original canvas identity, retained
  properties, chrome restoration and zero parent changes after the
  persistent host is installed.

## v1.13 transition-safety diagnostics and Qt-only smoke tests

- The non-Android C++ smoke test uses the real
  `MobileCanvasTransfer` and `MobileCanvasSurface` with a synthetic
  QWidget canvas. It loops through ten enter/Back cycles and checks
  that the backend recovers the same widget and its synthetic document
  state, while desktop chrome visibility is restored.
- Tests must also reject an occupied mobile destination, an attachment
  refusal, and an occupied desktop central slot without destroying an
  unrelated widget or silently replacing the native canvas. Run the
  `qt-canvas-host-test` GitHub Actions job, or compile the test with
  Qt5 Widgets and `-DKRITA_MOBILE_QT_TEST` on a development machine.
- This offscreen test verifies Qt object ownership only; it does **not**
  test Android OpenGL/EGL context preservation, stylus pressure, Krita
  document internals, or system Back gestures.
- On Android, open an existing KRA document and perform multiple Back
  and re-enter cycles, including toggling Compact UI. Confirm original
  brush strokes, layers, undo stack and dirty flag persist.
- If a canvas or toolbar disappears, open Mobile Studio's existing
  More menu and select **Copy Mobile Studio diagnostics**, then paste
  the report into the bug report. It contains no document content or
  file paths. Note if Back was used before or after the failure.
- Monitor local WSL Android compilation separately: the source-only
  Qt smoke test does not generate or upload an APK.

## v1.12 Back button: single-window Android canvas

Run these tests against a disposable, unsaved or backed-up KRA drawing.
The source fix is not an on-device pass until every interaction is verified.

- Enter Mobile Studio from normal Krita and from Compact UI. The Android
  activity must retain one top-level visible window, without Krita's
  File/Edit/View menu or status bar overlaying the mobile header/footer.
- Draw several pressure-sensitive strokes on at least two layers. Tap Back
  at top left: the original canvas must remain visible in Krita, not an
  empty dark-gray screen. Mobile Studio's blue + Add/Edit shortcut bar
  must be completely absent after the return.
- Repeat that cycle ten times. Zoom, pan, switch brushes, toggle layers,
  Undo/Redo, save as KRA, close and reopen. No view or document should be
  recreated, dropped, or left without its prior paint data.
- Return from Mobile Studio launched from Compact UI. The Compact UI
  toggle should restore its prior state; neither normal desktop chrome
  nor mobile shortcut controls should be ghosted in the bottom strip.
- Launch Mobile Studio, close its last document, enter the empty home,
  Back, then re-enter Mobile Studio and create another canvas. Ensure
  the Back handoff works identically with zero and multiple documents.
- Rotate the device in both directions before and after Back. Confirm
  the canvas fills Krita's central viewport with no orphaned black band
  or separately composed Quick Actions window.
- Rapidly alternate Back and re-enter while drawing is idle; avoid
  switching during active paint strokes. Modified documents must retain
  their unsaved-change state and saving/closing prompts.
- If desktop window ownership changes unexpectedly, Mobile Studio should
  refuse the handoff rather than deleting a canvas. Save before resetting
  or reinstalling.


## v1.11 functional regression: empty home, canvas close, Quick Actions

These are **on-device checks**, not claims of a passed Android test.

- Launch Mobile Studio without a document: only the Back/header and the
  large New Canvas/Open Drawing empty state should be needed. The
  paint-only Quick Actions footer and Add/Edit buttons must be absent.
- Create a blank canvas: the editable Quick Actions bar returns, with
  visible labels and no blank gray scroll section. Save, reopen and
  verify the bar still contains the configured shortcuts.
- From the Canvases dialog, close an unmodified document and verify
  the workspace returns to its empty state, including hiding shortcuts.
- Open and alter a KRA document, then select Close Current Canvas.
  Verify Krita's native Save / Discard / Cancel prompt appears *above*
  the mobile canvas. Cancel must retain every edit and the document;
  Save must preserve modifications on disk, and Discard must close
  without silently saving.
- Repeat the close test when no MDI subwindow is selected after moving
  between desktop, Compact UI and Mobile Studio. Close must still
  select Krita's current document, not a random other open file.
- With three open documents, ensure the Canvases chooser identifies the
  active canvas; switching, closing that canvas, and switching again
  must not accidentally close a different file.
- Tap Quick Actions + Add, select an action, confirm, and repeat Edit
  (reorder, remove, Defaults, Cancel, Save). After each accepted change
  the buttons must remain visible and swipeable. Cancel must leave
  saved shortcuts unchanged; an unchanged Save must not rebuild them.
- If an earlier build accidentally persisted an empty shortcut list,
  use Edit → Defaults → Save. Do not reset all Krita settings.

## v1.10 rejected-handoff and APK size auditing

- In a controlled Qt developer build, install a disposable second central
  widget on Krita's hidden backend before requesting Mobile Studio exit.
  The transition must be rejected: the second widget must NOT be deleted,
  the original painting canvas must remain inside Mobile Studio, and the
  launcher must return to checked. The user should see a warning and
  still be able to save the original document.
- Restore an empty backend central slot and retry the transition; the
  original native QWidget should return to Krita without a new document,
  new undo stack or changed modified state.
- Simulate a surface reporting a missing detached canvas when the desktop
  already owns the original QWidget. Recovery must not unparent it.
- Repeatedly toggle desktop → mobile → desktop with a modified KRA file.
  Verify the native app's document, layers and pressure-sensitive painting.
  Do not treat these checks as passed until compiled and tested on Android.
- Run `python3 -m unittest discover -s tests -p 'test_apk_size_report.py'`.
  Source-only CI must run the same suite without assembling/publishing APKs.
- After a local build, review `apk-size-report.json` and
  `apk-size-history.json` in the APK output folder. The latter stores
  the previous 20 distinct SHA-256 snapshots so the compressed native
  libraries and resource changes can be compared over time. APK and
  installed-app sizes are different metrics.

## v1.9 Android mode-switch and Quick Actions regression checks — source changes

APK publishing remains paused. Compile the source in WSL before installing,
and test against disposable KRA files rather than irreplaceable drawings.

- On the Mobile Studio welcome screen, with a new blank canvas and with an
  existing drawing, inspect Quick Actions in portrait, landscape and folded
  layouts. Every button label and its bottom border must be fully visible;
  there should be no unused gray bar below the actions. Swipe the populated
  list horizontally; + Add and Edit must stay visible.
- Repeatedly switch Krita desktop → Compact UI → Mobile Studio → desktop/
  Compact UI, with and without a document. The same native canvas should
  remain visible, not turn into an empty desktop background.
- From Mobile Studio, create a new drawing and open a drawing. The legacy
  desktop menu/status bar must NOT rise on top of the mobile toolbar. This
  catches deferred Krita Android window-refresh callbacks.
- Test switching back to Compact UI at least ten times with a modified
  multilayer document. Compare native undo history, stylus pressure, layer
  thumbnails, tab titles, saved file bytes and unsaved-change prompts.
- Confirm that an ordinary desktop-only session still shows its native
  menu, main toolbar, status bar and canvas, and that Mobile Studio cannot
  suppress the desktop after mode exit.
- If a canvas still disappears, record the exact sequence, app orientation
  and whether tapping the Krita menu restores it; do not clear app data
  or save over the last working drawing.

The fixes avoid Android top-level overlap and delay Compact UI restoration
until the original QWidget has been reinstalled. They do not replace or
recreate a KisDocument or the native paint engine, and are not on-device
verified until an APK build and test complete.

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

## v0.6 — native document workflow and file-safety checks

This preview replaces the mobile New button's desktop dialog with a
dedicated New Canvas screen using `KisPart::createDocument` and
`KisDocument::newImage`. The mobile editor still owns neither the file
codec nor the save/undo machinery.

Use **disposable** test documents, preferably two separate filenames:

- Create a **transparent** 1024×1024 canvas; confirm one editable paint layer
  and transparent background. Paint and undo, then save as a KRA file.
- Create a **white** 2048×2048 canvas; confirm locked background plus a
  paintable layer, and that Krita's native background handling is correct.
- Switch between two open documents and verify the top-right title and
  modified marker refer to the active document.
- Open an existing KRA from the mobile Open button; wait for the native
  document load, confirm its layers and size, and save a separate copy.
- Use **More → Save As...** to create a second file. Check that cancelling
  does not rename or overwrite the original document.
- Save while a brush stroke is completing, and after Undo/Redo. Krita's
  delayed-save protection and file formats must behave as before.
- Rotate/unfold while the New Canvas dialog is open, and ensure the
  dialog remains usable. Attempt a 8192×8192 canvas; the preview should
  reject the oversized pixel count without attempting to allocate it.
- Close Mobile Studio to the prior Krita window and reenter; images and
  saved paths must be preserved.

There is deliberately **no new native file format** and no custom image
serializer. The old native file dialogs/actions remain in the compatibility
menus until Android storage and save reliability are proven.

## v1.8 generic canvas surface contract — source-only

This milestone keeps `UPLOAD_REQUEST_ID=none`; WSL compilation is
allowed, APK publication is not.

- Run the source patcher and confirm the Qt-only contract preflight:
  neither `canvas_transfer.h` nor `canvas_surface.h` may include
  `standalone_host.h`, `engine_bridge.h`, Krita document classes or
  Krita desktop-window types.
- With no document open, enter/exit Mobile Studio twice. Krita's
  original welcome page must return after exiting, and the mobile
  empty-workspace screen must still offer New/Open.
- With several modified documents, repeatedly enter, paint with stylus,
  change views, save, then return to Krita. The exact same native MDI
  canvas must follow the session, preserving pressure-sensitive
  strokes, file names, layer trees, and Undo/Redo histories.
- Creating a canvas in mobile mode must not raise the hidden desktop
  window (v0.8 regression). No document should be silently closed,
  recreated, or autosaved by entering/exiting.
- Open a disposable document and test switching between folded,
  portrait and unfolded landscape, including the layer drawer and
  movable color wheel. On returning to desktop, the native tab strip
  must regain its dimensions and visibility.
- Test a rejected entry (e.g. a destination already containing a
  canvas or a backend with no central widget, in a controlled dev
  environment). It should return false without replacing either
  canvas, and the launcher should return to unchecked.
- With a deliberately faulty test surface that detaches a different
  QWidget (or returns null), leave Mobile Studio and verify that Krita
  restores only the exact pre-transfer native canvas. An unrelated widget
  must never be installed as Krita's central widget; document history,
  active layers and unsaved content must remain unchanged.
- Test Save/Discard/Cancel for a modified document. The generic surface
  handoff must never intercept Krita's native unsaved-change workflow.

The `MobileCanvasSurface` interface is an abstract Qt widget ownership
contract. `MobileStandaloneWindow` implements it; the transfer service
never includes that window's concrete type. Krita's document/command
services remain the only modules needing its concrete desktop APIs.

## v1.7 Qt canvas-host transfer — upload remains paused

This milestone moves the reversible widget handoff into a **Qt-only**
module. The source is not a new independent Krita bootstrap.

- Compile the Android app with `UPLOAD_REQUEST_ID=none`. The patch
  preflight now rejects direct KisMainWindow headers in the canvas
  transfer/session classes as well as in ordinary mobile UI modules.
- Enter Mobile Studio from the compact UI with **no document open**.
  Its native empty workspace should appear; Back should restore the
  original Krita welcome widget. Repeat with an already-open canvas.
- Open three disposable documents, paint with pen pressure, and switch
  views. Confirm Krita continues to own each canvas, layer state,
  undo history and unsaved-change marker.
- Use New Canvas and Open. The Android legacy display-refresh hack
  must not re-show an empty desktop main window over Mobile Studio.
- Exit and reenter repeatedly. The *exact same* native Krita MDI
  widget must return to the original window, with document tabs at
  their prior sizes and all document views accessible.
- Fold/unfold while in Mobile Studio and while the layers bottom drawer
  is open. Return to the desktop interface and check that Qt's tabs,
  canvas layout and document activation are still functional.
- Interrupt entry when a desktop central widget is missing or the
  mobile host already has a canvas. The action must revert to off
  rather than taking or deleting a second canvas.
- Test Save As, close a modified document with Cancel, and Exit using
  system Back. No document should close or lose changes automatically.

A widget transfer failure now restores the original Qt central widget.
Qt widget ownership and tab restoration live only in
`canvas_transfer.h`, not the mode controller or painting UI.

## v1.6 native document and command service split — upload paused

This checkpoint changes **where** the desktop backend is accessed, not
how Krita implements painting, saving or document lifetime. Do not
publish an APK until explicitly requested by the user.

- Run `apply_mobile_shell.py`: its dependency preflight should reject
  any UI module that includes `KisMainWindow.h`, without rejecting the
  three intentional backend modules (document service, command service,
  canvas session). Build Android through the existing WSL watcher.
- Open and create disposable canvases. Verify Krita's real canvas
  appears, with pen pressure and normal undo/redo. Switch between
  several documents and confirm dirty markers and layer trees.
- Save, Save As, Cancel a save dialog, then re-open. File codecs,
  backup behavior, unsaved-change prompts and other Krita behaviors
  must match v1.5. In particular, Close Current Canvas must preserve
  Save / Discard / Cancel behavior.
- Search nested File/Edit/View menus and Quick Actions commands. All
  native QAction handlers must remain accessible and execute only
  once. Switch Brush / Eraser / Blender presets and native selection
  tools. Check Color and Layers panels still update correctly.
- Enter/exit Mobile Studio repeatedly, including folded/portrait
  layouts. The old Krita window must still return with its canvas and
  native tabs; new document creation must not make the desktop window
  resurface over Mobile Studio.
- If the app crashes or a document cannot be saved, revert to the
  preserved v1.1 downloaded APK. Source-build success is not device
  validation.

The only modules that intentionally retain a direct native desktop
`KisMainWindow` dependency are `canvas_session.h`,
`document_service.h`, and `command_service.h`. The UI and
`engine_bridge.h` delegate through service methods rather than
accessing desktop window controls.

## v1.5 responsive canvas panels and movable color controls

This checkpoint is source/build-only until the user explicitly resumes
APK publication. Keep `UPLOAD_REQUEST_ID=none`.

- With Layers closed, resize the app window between folded portrait,
  narrow portrait and unfolded landscape. The floating color wheel must
  remain fully inside the viewport. It must not move the real canvas.
- Press the color panel **resize** button: cycle between small, regular
  and large wheel sizes. On screens where the preferred wheel would not
  fit, it should gracefully use a smaller wheel or a compact swatch.
- Press and drag the **move handle** with pen and finger to place the
  wheel elsewhere on the canvas. Double-tap the handle to reset to
  bottom-right. Fold/unfold and check clamping to screen edges without
  losing the saved preferred placement.
- Expand/collapse color and restart Mobile Studio. Confirm preferred
  size, expansion, and position survive. With zero documents the panel
  must be hidden, then reappear after creating a document.
- At canvas viewport widths under 640 pixels, show Layers. It should
  appear as a **bottom drawer** rather than reducing the live canvas to
  a narrow vertical strip. Hide it and confirm full canvas access.
- When the bottom drawer is open, the color panel should stay above
  it and automatically minimize if insufficient space remains.
  Closing Layers should restore the preferred wheel state.
- At wider canvas widths, Layers should return to a right-side dock.
  Toggle Layers open/closed across orientation changes and verify the
  same layer selection and native node actions remain intact.
- Paint with pressure-sensitive pen, switch documents, Undo/Redo,
  save and reload. Native Krita drawing/canvas input and file formats
  must remain unchanged.

## v1.4 floating Krita color workspace and compact brush controls

**Do not upload an APK** until explicitly requested. The WSL compilation
check can run normally while `UPLOAD_REQUEST_ID=none`.

- With an open document, verify a compact **color wheel in the bottom-right
  of the canvas**. Drag the hue ring, then the inner saturation/value
  square with a pen and finger. The actual Krita brush color must update
  and strokes should use the selected foreground color.
- Select **Advanced color** to adjust precise color/alpha. The Qt dialog
  should appear over the mobile window and return to the Krita canvas.
- Switch documents with different active color settings; the wheel should
  track the active Krita resource rather than retaining its own color.
- Choose multiple colors. The eight recent swatches should update, and
  tapping one should restore that color through Krita's resource manager.
  Restart and confirm the recent choices persist.
- Tap the color swatch to minimize the wheel and again to expand it.
  Also use the Quick Actions **Color** shortcut to toggle its state.
  The minimized swatch must remain clickable and the preference persist.
- Open Layers beside the canvas, rotate, fold, unfold, and resize the
  painting area. The wheel should stay inside the bottom-right of the
  *canvas*, without overwriting the layer sidebar's hit targets. On
  narrow screens the wheel may cover artwork; minimizing must recover
  that space, not resize the actual Krita paint device.
- Collapse the detailed brush size/opacity row using the new +/−
  control. Paint/Erase/Blend mode buttons and Brushes remain available.
  Expand again and confirm brush size and opacity retain their values.
- Select an eraser or blender preset, then switch back to Paint. The
  first Paint preset chosen should not accidentally reuse an eraser.
  Each mode should still remember its last selected Krita preset.
- Repeat a paint/erase stroke, Undo/Redo, Save/Save As and New Canvas.
  The new color UI must not alter Krita's native input/brush/undo engine.

## v1.3 touch and stylus interaction checks — upload paused

This is a WSL compilation milestone, not an APK publication request.
Leave `UPLOAD_REQUEST_ID=none`.

**Quick Actions**
- Open Edit; touch and drag a row immediately. This should scroll the
  list **without reordering**. Hold a row for approximately 360 ms,
  then drag: the item should reorder with Qt's drop indicator.
- Repeat with a pressure-sensitive pen. Move to the upper/lower edge
  while dragging; the list should autoscroll toward the edge.
- Use Up/Down as a reliable alternative, verify Cancel restores the
  previous layout, and Save persists after app restart.
- Add more actions than fit horizontally. The quick strip should really
  scroll sideways without squeezing/clipping all button labels.
  Defaults must still fit the compact layout and retain Add/Edit.

**Layers**
- Tap an inactive layer once: it selects that layer. Tap the already
  active layer again: a menu should open with Rename, Properties,
  Duplicate, Move, Group, Clipping Group, Ungroup (for groups), Delete.
  Test with both finger and pen.
- Tap **Shown** or **Lock** in the row of a non-active layer. Only
  that layer should be modified, and native Undo/Redo should work.
- In multi-select mode, toggle rows and group them; nested groups
  should remain visually distinct. Expand/collapse a nested group
  and verify the expansion survives list refresh.
- Draw, Undo, Save, open another document and restore the original
  Krita window. No operations should alter Krita's native data model
  except through native node-manager commands.

## v1.2 native mobile menus — no APK upload

APK publishing is intentionally paused. Run the WSL compile and record
the commit status, but leave `UPLOAD_REQUEST_ID=none`.

- Open **More → Menus — File, Edit, View and more** (or add **Menus**
  to Quick Actions). The first screen should show the same top-level
  categories and ordering as Krita's native desktop menu bar.
- Tap File or Layer and navigate nested submenus. **Back** should return
  one level at a time. Selecting a native command should close the
  browser and execute Krita's existing handler exactly once.
- From within a nested submenu, type a search query such as
  "duplicate", "crop", "mirror", "save" or "layer". Results should search
  **all** menu categories, not only the current menu, and show the
  entire menu path beside each result.
- Search by an action identifier. Krita commands that are registered
  but do not appear in a standard desktop menu should be accessible
  under **Other Krita commands**.
- Disabled commands must remain visibly unavailable and not execute.
  After changing document or selection state, reopening the browser
  should reflect Krita's latest enabled/disabled states.
- After leaving Mobile Studio, Krita's desktop menus must still work.
  Confirm native New/Open/Save, selection, layers, Undo/Redo and tool
  activation are unchanged. Test with multiple documents.
- Test a menu item that opens a file dialog to confirm the dialog is
  not obscured by the hidden desktop host.

The menu browser only references Krita's own QAction objects and never
constructs a parallel command implementation. It does not save or
manipulate document data. The raw legacy menu remains available as a
compatibility fallback until Android runtime testing is complete.

## v1.1 configurable Quick Actions regression tests

This preview replaces the old fixed footer with one **horizontally
scrollable strip** of shortcuts backed by Krita's native action and tool
registries. The configuration persists in `QSettings` and never stores
document state.

- On first launch confirm the default strip contains New, Open, Save,
  Undo, Redo, Layers, Color and More, and **+ Add** and **Edit**.
  Scroll sideways with a finger; button taps should activate only the
  intended command (and never paint on the canvas).
- Tap **+ Add**. Browse **Frequently used** and **All commands and tools**,
  and search by command label or internal action ID; choose one. The
  shortcut should appear in the strip and execute Krita's native command.
- With a document open add the native Krita Brush/Selection/Move tools
  under the **Krita tools** filter. Switching them must use Krita's own
  tool manager. Confirm pen pressure, transformations, and native Undo.
- Tap **Edit**. Hold and drag an entry above another with **stylus**
  and with **finger**. If platform touch drag does not initiate reliably,
  use the Up/Down controls; both should produce the same saved order.
- Remove an entry, cancel editing and verify it was **not** removed.
  Repeat and press Save; the change should persist after leaving and
  reentering Mobile Studio and after restarting Krita.
- Add several commands and test horizontal scrolling in portrait. Use
  **Defaults** in Edit, then Save, to recover the eight original commands.
  Confirm + Add/Edit remain available even if all shortcuts are removed.
- Add an action currently disabled in Krita (e.g. one that requires a
  selection). It should stay visible but refuse activation until Krita
  enables it; do not silently synthesize a disabled native command.
- Verify New/Open/Save/Undo/Redo/Color/Layers/More keep working without
  regressions from v1.0. Confirm v0.8 new-canvas desktop resurfacing fix
  and v0.9 safe document closing behavior.

There is no parallel quick-command interpreter: mobile-only workflow IDs
dispatch to `MobileStandaloneWindow`, native QAction IDs route to Krita's
own action collection, and tool IDs route to `KoToolManager`. If an action
is absent due to a missing plugin, the shortcut is safely disabled and
remains removable in Edit.

## v1.0 canvas presentation and native tool picker

This milestone advances the **mobile-first interface**, without replacing
Krita's brush/selection/transform engines or Qt MDI document ownership.

- Use **More → Tools — search Krita's tool registry**. Search for brush,
  rectangle, selection, move, and transform. Choose a result; the real
  Krita tool should activate with pen pressure/input and Undo intact.
- Reopen Tools. The current tool is highlighted, and recently used tools
  appear first. Search also accepts internal tool identifiers.
- Open multiple documents, including creating a new one from Mobile
  Studio. Krita's extra **desktop document tab strip** should not be
  visible inside the mobile canvas; use the mobile document-title
  switcher to move between drawings. Check that strokes appear on the
  intended document, not an inactive one.
- Leave Mobile Studio using Back. Krita's normal desktop MDI tabs
  should reappear at their original size and remain clickable.
- Reenter Mobile Studio, close the last document, then create or open
  another. The tab strip should remain hidden even when Qt creates a
  fresh native MDI tab widget.
- Rotate/fold/unfold, move between documents, and test
  Save/Save As/Undo/Redo/pressure-sensitive pen strokes.
- If hiding the native tab bar causes a canvas-rendering failure,
  use the preserved previous release and report orientation and
  document count; this must be reverted rather than risking data.

The mobile host's own document switcher replaces the visible MDI tabs,
but the underlying QMdiArea and native Krita views remain intact.
The canvas-session controller is the only place that changes native
MDI tab presentation and records original geometry for restoration.

## v0.9 document lifecycle and empty workspace

This batch moves **document closing** into the mobile engine adapter,
while leaving Krita responsible for unsaved-change confirmation, saving,
autosave cleanup, and document/view destruction.

Use disposable or backed-up files:

- Enter Mobile Studio with zero documents. It should show the mobile
  **Your canvas workspace** home, rather than Krita's desktop welcome
  screen. New Canvas and Open Drawing should both work here.
- Create two canvases. Draw on each, switch using the top document name.
  The top title and dirty indicator must track the selected document.
- With a modified canvas active, choose **Canvases → Close current canvas**.
  Verify the native **Save / Discard / Cancel** prompt appears *over the
  mobile workspace*. Cancel must leave the drawing untouched. Save
  must retain the drawing on disk; Discard must not overwrite anything.
- Close an unmodified canvas. The other canvas should become active.
  Close the last canvas: the mobile empty workspace should appear, and
  Layers/brush controls should stop offering document-only operations.
- From the empty workspace create another canvas, paint, undo, save,
  and open another KRA. The native canvas must reappear (not gray).
- Repeat Close from **More → Close current canvas**. If the Save dialog
  is cancelled, the document must stay open and appear in the switcher.
- Use **Back** to restore the original Krita interface, then enter
  Mobile Studio again with no document and with one document open.
  The original Krita canvas, welcome screen and Android refresh must
  continue functioning.
- Repeat the Android v0.8 bug regression: opening or creating a file
  must not bring the hidden desktop window over Mobile Studio.

This still uses Krita's existing KisMainWindow lifecycle, but all
mobile-facing close/open/save operations are routed through one adapter.
The mobile empty state hides Krita's desktop welcome page without deleting
that page or bypassing any native image machinery.

## v0.8 urgent regression — new canvas must not reveal desktop shell

This addresses the v0.7 screenshot showing desktop File/Edit menus and a
blank gray view after using Mobile Studio's New Canvas dialog. The upstream
Android `KisMainWindow::slotFlashWindowHack()` helper schedules
`hide(); show();` whenever a new Krita view loads. It must not re-show
the desktop host while its canvas belongs to Mobile Studio.

- Start with Mobile Studio in front and use **New** to create a white
  2048×2048 document. After pressing Create, the *same mobile header,
  canvas, brush panel, and footer* should remain in front; a white canvas
  should be visible and respond to a pressure-sensitive pen.
- Repeat for transparent canvas and for opening an existing KRA. Confirm
  Krita's desktop menu bar does **not** appear over Mobile Studio.
- Create three documents in succession, switch among them, paint and undo.
  All document views should remain reachable; no gray screen.
- Press **Back** to return to the original Krita interface. Create a
  document there; Krita's existing Android display workaround must still
  operate normally when the mobile canvas is no longer transferred.
- Repeat v0.7 with the opt-in direct-mobile startup preference. The
  fallback path must remain usable after an interrupted test.

The fix changes only the Android desktop-window visibility refresh and
canvas ownership marker. It does not bypass Krita's real document creation,
raster canvas or file format handlers.

## v0.7 batch test — independent canvas-session and document navigator

This is a **batched, not yet device-verified** milestone. Krita still
initializes the backend; only the canvas transfer is contained in
`canvas_session.h`. Mobile Studio's own window has no desktop MDI
dependency; open-document listing/switching is through the engine adapter.

- Open two different `.kra` documents and create one new canvas. Tap the
  **document name in the top bar** to show all three canvases; choose each
  and verify that its drawing, layer tree, brush settings, and undo history
  correspond to that document. The switcher never closes documents.
- Modify an unsaved document and open the switcher. An asterisk should
  appear beside the correct document. Switch away and back; no changes lost.
- Use **New** and **Open** directly from the switcher, then save the new
  document. File dialogs must stay in the mobile window.
- Repeatedly enter/exit Mobile Studio using the top-left **Back** action;
  verify the original desktop/compact canvas and menus return correctly.
- In More, opt in to **Start in Mobile Studio on next launch** and restart
  the app. It should enter the independent mobile window directly.
  Turning the option off should restore the normal Krita startup.
- If an automatic launch fails/crashes before its startup check completes,
  the next launch should restore normal Krita UI rather than trap you in
  a recurring startup crash. This setting is OFF by default.
- In portrait/folded orientation, verify that tapping the document title is
  possible without accidentally pressing Back. If labels are clipped,
  report the screen orientation and screenshot.
- Re-run the v0.6 new/open/save tests, pressure-sensitive pen strokes,
  brush switching, native layer undo, and canvas rotation.

**Do not test on the only copy of an important drawing.** v0.7 is an
architectural preview; it deliberately does not expose native Close
Document until unsaved-change confirmation has been reviewed.

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
