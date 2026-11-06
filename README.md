# Krita Android tablet customization (experimental)

Source patches and a GitHub Actions workflow for **Krita 5.3.4 Android** (KDE source tag `v6.0.4`).

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
