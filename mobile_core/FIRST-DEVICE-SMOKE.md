# Independent Krita Android host — device acceptance test

The independent MDI-free drawing engine already passed initial device
smoke testing for painting, Undo/Redo, saving, and reopening KRA files.
The newer opt-in native-startup path and direct brush/layer/color/tool
panels still require validation. **Only use disposable artwork.**

## Getting the build

- Let the running **WSL local build watcher** compile the latest commit.
  Confirm `BUILD SUCCESS` for the exact head SHA in its console or
  in GitHub's `local/android-wsl-build` status. Do not install an
  older APK after a later code change.
- Find the local Android debug APK in
  `/home/pokem/krita-local-android/persistent/krita-custom-apks`
  from WSL or `\\wsl$\Ubuntu\home\pokem\krita-local-android\persistent\krita-custom-apks`
  in Explorer, adjusting the distribution name if different.
- APK publishing to GitHub Releases stays paused. The debug app uses a
  different package identifier from regular Krita.

## A. Manual standalone smoke test

1. Open the debug app using its existing Krita or Mobile Studio UI.
2. Select **Open independent Krita engine now** from Mobile Studio
   More, or **Settings → Standalone Engine Test (experimental)**.
3. Confirm the drawing view appears *inside the same Android window*,
   with a functioning 512×512 initial Krita canvas.
4. Paint with pen and finger; Undo and Redo the strokes.
5. Press **New** and choose a custom size/DPI and background. Create
   two or more canvases and mark each with different strokes.
6. Switch with **Previous canvas**, **Next canvas**, and **Canvases**.
   Confirm each drawing's image, active layer, and undo history stay
   independent.
7. Use **Save As**, **Close view**, **Open** to round-trip a disposable
   KRA. Verify strokes, canvas size and layers survive reopening.
8. Make an unsaved stroke, then **Close view**. Test Cancel, Save and
   Discard on disposable documents. Cancel must keep the drawing open.
9. Try **Brushes**, **Layers**, **Color**, and **Tools**. Verify the
   controls can select actual Krita presets, nodes, colors, and tools.
   Check strokes still work afterward, then hide the panels.
10. Rotate the device, background/resume the app, switch canvases,
    then try painting again. Use **Exit test** and confirm the original
    Krita interface and any untouched documents are intact.

## B. Native startup (separate opt-in gate)

1. Save all documents. In the independent host, enable
   **Start here next launch**, or enable the corresponding item under
   Krita Settings or Mobile Studio More.
2. Fully close and relaunch the debug app. The independent engine
   should appear without opening the older Mobile Studio overlay.
3. Test painting, undo, Open, Save, canvas switching, and the optional
   brush/layer/color/tool controls.
4. Use **Exit test** to return to the desktop fallback. Disable
   independent startup if you prefer the old interface next launch.
5. If the native startup crashes, reopen the app. It must disable
   independent startup automatically and recover to original Krita
   instead of looping. Under **Settings → Last Standalone Engine Test
   Status**, report the last checkpoint and last native control.

## Reporting

Provide the failing step, screenshot or exact error message, and
whether it was a pen/finger interaction, dialog, window rotation, or
background/resume event. If the app terminates, retrieve **Settings →
Last Standalone Engine Test Status** on the next launch. An Android
logcat trace is useful but not required to report a useful checkpoint.

**Release gate:** The native startup and its direct brush/layer/color
controls are still experimental. Desktop Krita initialization
continues to run behind the independent shell for resource/plugin
bootstrap and recovery. Do not remove that backend until these
on-device flows pass across multiple documents and lifecycle events.
