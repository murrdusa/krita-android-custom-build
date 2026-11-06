# Krita Android tablet customization (experimental)

Custom Krita 5.3.4 ARM64 for Android, patched from KDE tag `v6.0.4`. Rebuild locally using WSL2 and Docker with `git pull --ff-only && bash local-build.sh build`.

## Current release status

- **v1:** compiled and tested; working double-tap mirror and narrow Layers docker.
- **v2:** compiled locally into an APK; user confirmed Compact UI, one-shot picker, reference sampling, native volume Undo/Redo, gallery and key controls largely work.
- **v3 refinements:** code written in `apply_tablet_refinements.py` and source patch validation passed. **Not yet compiled into a new APK or tested on Android.** The v2 APK remains the last verified build.

## v3 changes for testing

- Compact UI attempts to reclaim hidden document-tab height instead of leaving a gray strip; hides canvas and MDI scrollbars and restores their previous policies on exit.
- Default top toolbar is icon-dense instead of large text labels. Quick Actions opens by tapping the entire button. Removes a duplicate brush preset reset button; narrows the brush blending-mode selector.
- Moves Gradient and Pattern selectors to Quick Actions. Quick Actions is now configurable via a checklist and reorderable list.
- One-finger double-tap defaults to Mirror View in Krita Default input profile on new profiles. The interval defaults to **150 ms**; previously stored nondefault intervals remain unchanged. One-shot sampler gets **Ctrl+Alt+Shift+P** by default.
- Quick two-finger inward pinch reset defaults to **enabled** for users without a saved preference; timing remains configurable.
- Native volume key gestures: separate short/long bindings for both volume buttons. Default: Down short Undo, Up short Redo, Down long Pick Color Once, Up long Toggle Eraser. Long-press threshold defaults to **150 ms**. Normal actions trigger on key release; brush size Up/Down may be selected as a repeating action while held.
- Direct finger dragging of visible reference images from any tool; movement is recorded as an undoable shape command. Can be disabled in Tablet Controls.
- Selection Actions Bar enlarged with additional selection-relevant actions. Transform tool gets its own bottom contextual bar (mode, reset, cancel, apply) in Compact UI.
- Gallery defaults to shared Downloads and has a Downloads button plus **opt-in All Files Access** settings route. Android permission must be explicitly granted; the permission does not allow access to other apps' private Android/data files. Gallery rename/move/advanced organization is still deferred.
- More flexible narrow Tool Options layout constraints. Individual tool widgets may still require specialized responsive work.

## v4 UI correction patch (source validated; APK not yet built)

Run `git pull --ff-only` and `bash local-build.sh build` in Ubuntu to compile the new APK incrementally. This patch is implemented in `apply_tablet_polish.py`, after v3.

- **Compact UI:** Qt QMdiArea still reserved its document tab height after simply hiding QTabBar. v4 switches to frameless MDI subwindows only while Compact UI is active, then restores the original MDI view mode and document tab styling when switched off. This does not change Android immersive/fullscreen state. Standard document tabs should become visible again, including the filename and unsaved marker.
- **Welcome:** Hide the image-specific toolbar buttons on the welcome screen, and restore the toolbar when a document opens.
- **Icons:** Sample Visible Layers gets a distinct icon from Pick Color Once; reference visibility gets a compact reference icon instead of long text. The brush blend-mode selector maximum width is lowered from 116 to 90.
- **Volume settings:** Remove redundant short/long enable checkboxes. Keep one master volume-key toggle and the four individually assignable actions (each can be `None`). Long-press threshold remains configurable.
- **Double-tap:** Assign Mirror View (150 ms default) to the saved/current input profile on a one-time migration **only when the gesture is not already bound**. A customized double-tap is preserved.
- **Reference image:** Add two-finger move, resize and rotate gestures with a single undoable shape-transform command. This is experimental and needs on-device testing.
- **Deferred:** Screen-anchored/pinned reference overlays and a complete relocation of every special tool's options into the bottom strip. Transform and selection context bars remain enabled.

**Validation caveat:** The GitHub workflow checks patch application and whitespace, not C++ or Java compilation. Please send the first `error:` line if a local incremental build fails.

## v7 Compact UI phone layout — source validated, not yet compiled

This experimental layer uses the new `tablet_compact_skin.h` and `apply_compact_skin.py`. All new controls appear **only when Compact UI is enabled**; on exit, Krita's QMainWindow docker state is restored.

- **Paint / Erase / Blend slots** (bottom-left): first tap switches to the last-used native Krita brush preset for that category; second tap opens a touch-oriented icon grid of presets. Each category remembers its last preset independently, including after restarts. Paint disables eraser mode; Erase enables it; Blend selects a normal Krita smudge/blender preset with eraser mode off. This is *not* a universal conversion of all paint engines into smear engines.
- Presets are initially grouped by names (erase/eraser, blend/smudge/blur; others Paint), with user overrides stored privately in `TabletUi/presetGroup`. In the preset grid, enable **Show all Krita brushes** and choose a brush to put it in a different group; **Assign current brush to this category** is another option. Built-in Krita preset tags and the native brush selector remain unchanged.
- **Layer thumbnails rail** (left): scrollable thumbnail list of top-level layers; tap to select, tap the selected layer again to open a compact controls panel. It offers blending mode, opacity, visibility, duplicate, clear, lock, merge, delete, clipping group, opaque selection and transparency mask, delegating to Krita's existing commands where possible. An Add button creates a paint layer. It shows an abbreviated active-layer name.
- **Color selector** (bottom-right): Krita's existing Advanced Color Selector docker is reused as a resizable floating wheel. Tap the color button to minimize/restore it; adjacent small button cycles three wheel sizes; recent color swatches remain in a narrow row. Original docker layout is restored on exiting Compact UI.
- **Top popup buttons:** native full brush preset chooser and Tool Options. When Tool Options are in docker mode, its button shows the existing docker; it does not forcibly rewrite Krita's global preference.
- Source-only validation passed; this patch contains substantial Qt/C++ code, so a first local incremental compilation may expose errors. Do not assume a working APK before testing.
- Limitations: only top-level layers are shown in the first experimental rail, and color docker floating behavior on Android remains untested. There is no automatic conversion of any arbitrary brush to a true smudge engine.

Build: `cd ~/krita-tablet-customizations && git pull --ff-only && bash local-build.sh build`.

## Patch/build workflow

1. `apply_customizations.py`: v1 base changes.
2. `apply_tablet_update.py`: v2 changes.
3. `apply_tablet_refinements.py`: v3 changes.
4. `local-build.sh`: fetch source, apply all three patches, incrementally compile APK inside KDE Docker container. **Keep its persistent working directories** to avoid recompiling everything.
5. GitHub Actions `validate` mode checks that source edits apply and the XMLGUI checksum matches; it does **not** compile C++ or Java.

Detailed Windows setup instructions: [LOCAL-BUILD.md](LOCAL-BUILD.md).

## Installation and permissions

This is an unofficial sideloaded build. Keep Play Store Krita installed and back up critical drawings. The APK is intended to install separately as `org.krita.debug`, but verify Android's prompt instead of uninstalling anything. Gallery All Files Access is optional, broader than the usual document picker, and must be granted through Android's Settings. It may not be appropriate for Play Store distribution.

---

## APK history

- **v1 / run #4:** successful Android debug build; user verified Mirror View on one-finger double tap and compactable Layers docker.
- **v2:** source patch applied and textually validated by GitHub Actions; **Android compilation and on-device behavior not yet verified**. No v2 APK is published until the new build succeeds.

## v2 source changes in this repository

- **Compact UI:** hides the menu bar, document tabs and bottom status bar without changing Android's immersive mode or the arrangement of current dockers/toolbars; toggled from the top toolbar.
- **Quick Actions** and common actions in the default top toolbars: Save, Deselect, selected-layer Alpha Lock, reference visibility, Reload Preset, reset canvas view, one-shot color picker and sampling-source toggle. Brush size and opacity controls remain.
- **Tablet Controls dialog** to adjust double-tap delay and optional quick-pinch reset timing; enable/disable volume-button undo/redo.
- **Volume Down = Undo; Volume Up = Redo**, handled natively in the Android activity with a configurable on/off switch.
- **One-shot color picker:** use the new toolbar action to switch to sampler for one pen press, then return to the freehand brush. **Sample Visible Layers** toggle currently applies to the dedicated color-sampler tool.
- **Direct pen-tap color pickup from visible reference images**, intended to avoid a brush stroke when the pen touches a reference.
- **Optional quick inward pinch reset**, initially **off** to avoid gesture conflicts; normal pinch gestures retained. Uses existing Fit Canvas and Reset Rotation actions.
- **Selection Actions Bar** enlarged on Android, enabled and anchored at the bottom in Compact UI; restored to the user's original selection-bar settings when exiting.
- **Welcome screen:** replaces news column with more space; integrates gallery in the Recent Images area with a toggle, sorting and local-folder selection. Recent-document content URIs remain a fallback.

## Deferred / not represented as complete

These require deeper Android and Krita integration and are **not yet implemented** in v2: finger-dragging reference images with any tool; complete Android SAF folder traversal for files saved by other apps; a transform-specific bottom action bar; automatic adaptation of every Tool Options control at extremely narrow docker widths; and an exact new default workspace matching an image (existing user workspace settings are preserved).

## Build

**Validation** applies source patches and checks the diff, **not C++ compilation**.
To manually build, visit GitHub Actions → Krita Android custom build → Run workflow → select `build`.
A special tagged repository push may also start a build automatically.

1. Clone Krita at tag `v6.0.4`.
2. Run `python3 apply_customizations.py /path/to/krita`.
3. Run `python3 apply_tablet_update.py /path/to/krita`.
4. The GitHub workflow provisions KDE Android build tools and produces an ARM64 debug APK artifact when successful.

## Safety and limitations

This is an unofficial, experimental fork. **Do not uninstall Play Store Krita or test with your only copy of important art.** The v1 build was generated as `krita-arm64-v8a-5.3.4-...-debug.apk`, intended to be a separate app; verify the Android package ID if installing. Android SAF access still requires explicit permission handling; normal directory scanning is insufficient for arbitrary cross-app Downloads visibility.
