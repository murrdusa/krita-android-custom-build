# First Android device smoke test — standalone Krita engine

This test uses the opt-in **Settings → Standalone Engine Test (experimental)**
window, not the default mobile UI. The Android debug app ID is
`org.krita.debug` to avoid replacing an existing Krita installation.

## Prerequisites

- Build/install a debug APK from the GitHub Actions `[android-build]` run.
- Use **only disposable drawings**. Never test on existing unsaved art.
- The standalone window is *experimental*: source and Qt tests do not prove
  that Android OpenGL, brush input, or SAF saving work on the phone.

## Minimal acceptance test (in order)

1. Open the debug app. Confirm it launches the existing working UI.
2. Open **Settings → Standalone Engine Test (experimental)**.
   A separate 512×512 blank document should appear. If it fails, note the
   warning and return to the original UI without modifying existing art.
3. Make a few strokes with a stylus and with a finger. Verify that the
   visible strokes and dirty document state change, without a blank canvas,
   crash, or unrecoverable rendering artifacts.
4. Use **Undo** and **Redo**. Verify the same Krita strokes are removed and
   restored.
5. Press **New**. Use **Previous canvas** and **Next canvas** to switch
   between the original and second drawings. Each drawing must retain its
   own strokes and history.
6. Use **Save As** to save one disposable drawing as a KRA file. Close that
   view, use **Open** to reopen the file, and verify the actual pixels and
   canvas size. A provider permission error or a file-picker path issue is
   a failed test, not a reason to discard the original document.
7. Make an unsaved stroke and press **Close view**. Test **Cancel** first,
   then retry with **Save** or **Discard** on disposable artwork.
8. Rotate the phone, background/foreground the app, switch documents again,
   and confirm painting still works. Return to Krita and reopen the saved
   KRA file.

## What to report on failure

- Which numbered step failed and whether the original interface still works.
- The exact message, a screenshot, or Android logcat around the crash.
- Phone model, Android version, and whether the action used finger or pen.
- Whether the document had unsaved changes and whether a file picker was open.

**Release gate:** do not switch the new host on by default until drawing,
document identity, save/reopen, and lifecycle tests pass on an actual device.
This smoke test only proves the minimum native integration needed for
continued development; it does not sign off on brushes, layers, masking,
gesture handling, or the new mobile UI.
