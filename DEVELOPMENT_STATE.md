# Krita Mobile Studio — current development state

**Updated:** November 11, 2026. **Branch:** `main` of `murrdusa/krita-android-custom-build`.

Read `AGENTS.md` first, then `PRODUCT-ROADMAP.md` and [local WSL build Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). This file records *current* status, not a commit-by-commit log. The preceding complete log is preserved verbatim in [DEVELOPMENT_HISTORY_2026-11-11.md](DEVELOPMENT_HISTORY_2026-11-11.md). More focused runbooks live under `mobile_core/`.

## Latest on-device regression report — round 2

**READ [DEVICE_FEEDBACK_2026-11-11_ROUND2.md](DEVICE_FEEDBACK_2026-11-11_ROUND2.md) FIRST** for latest Fold 6 testing against `f3c7d49`: 26 new passes, 9 failures, 22 earlier passes, 102 pending. New top engineering rule: **all scrollable mobile menus and future dialogs must use one shared touch-safe swipe-versus-tap interaction contract**, not separate per-panel workarounds. Layer-options sheet and blend picker accidentally click during scrolling; brush thumbnails waste room and show letter prefixes; brush customize controls need Size/Opacity/Flow together; group header and rename-layout redesign regressed; group borders too weak; clip arrows needed; transparency masks cannot be dragged (actual semantics unverified); brush labels remain unreadable after document switch; unexpected symmetry halo documented with screenshot; repeated boxed-X icon needs identification. Color wheel passes but corner placement and optional history below wheel requested. Group-delete checks currently **PASS** on this device despite earlier intermittent failure. Preserve passing gestures, pressure, numeric fields, clipping functionality and all 102 pending tests.

Round 2 implementation consolidates scroll/tap handling for configured Qt menus and item views, including Layers, Blend, Tools, Brush Presets, and gesture settings. Brush Settings now keeps the native Size/Opacity/compatible Flow together with a preset label that elides at the current width. The rail has compact folder headers, group borders, clipping arrows and valid transparency-mask drop targets. Layer rename remains in the sheet and More menu. Color options include an optional recent-color strip, and the wheel uses the selected corner unless another visible control occupies it. See the Round 2 record for device checks and the remaining symmetry investigation.

## Immediate device-feedback priority (November 11, 2026)

**READ [DEVICE_FEEDBACK_2026-11-11.md](DEVICE_FEEDBACK_2026-11-11.md) BEFORE EDITING MOBILE UI, LAYERS OR DOCUMENT NAVIGATION.** It records detailed Fold 6 user observations, all requested design changes, and priority ordering; no points may silently be dropped. The user has not tested checklist section 4 or later. First fix intermittently crashing group deletion and stale layer tree, missing top toolbar after Documents → Open, Tools touch-scroll regression, and adjust Home to autosave **named** projects while preserving unnamed-draft/explicit Discard semantics. Group and blending UI, brush grid, native mirror echo, gesture touch-vs-scroll, numeric px suffix editing, empty-group drop, first-row Alpha Lock, rename by name tap, and one-finger palette status are also documented.

## Source of truth and build gate

- Independent Android APK target `mobile_studio`, package ID `org.krita.mobilestudio.debug`, based on the actual Krita 6.0.4 paint/resource/tool/document engine. It runs through its own Qt native startup, `MobileLightweightCanvasHost`, `KisViewManager` and real `KisView`, **not** the desktop `KisMainWindow` or MDI. Native C++ engine extraction is in `mobile_core/krita_engine_initialization.cpp` and `apply_mobile_shell.py`.
- The local Windows/WSL2 watcher automatically builds commits to `main` and records the **exact** revision plus `local/android-wsl-build` status in Issue #1. Never infer a newer build from a previous SUCCESS. Do **not** run GitHub-hosted Android builds or upload APKs unless specifically instructed. No need to ask the user to manually pull/build while the watcher runs.
- Round 2 shared touch and layer/brush batches through `0790eac7fd8555e35969a6c9869d842e209d1fb2` passed WSL Android compilation (November 11, 17:41 UTC). Check Issue #1 for the exact build of later commits.
- Cross-compiled APK success **is not** a Fold 6 runtime pass. The `qt_saved_version_archive` regression source was extended and compiled through the Android integration where applicable, but its standalone Qt test binary has **not** been executed here.

## Current application and capabilities

- Normal cold launch enters Home unless opened by a file intent; Home has New Canvas, Browse Files, Recent, All Projects, folder grants and Recover Drafts. Document open/save/Save As/export/close use native Krita engine and Android SAF permissions. Krita owns Save/Discard/Cancel prompts. A separate working canvas uses actual Krita brushes, pen pressure, undo/redo and KRA serialization.
- Touch-first floating controls: configurable icon toolbar, Paint/Erase/Blend preset memory, tool selection/grid, native Tool Settings, round Color wheel, Layers rail, brush settings, canvas-only mode, and keyboard-free dialogs. Native Krita action collections retain menu and searchable command access. Toolbar and panel redesign is intentionally deferred.
- Native layer operations: selectable/drag-reorderable rows with finger and S Pen, visible masks/groups, native clipping via alpha inheritance and hidden wrapper groups, optional multi-layer targets, opacity/blending/visibility/locks, create/duplicate/rename/delete/merge/flatten, Undo/Redo and native image-stroke layer moves. Do not replace Krita's compositor, paint ops, or undo engine.
- Native drawing tools exposed: freehand smoothing, selection combinations and refinements, transform modes/parameters, Fill/Enclose and Fill, gradients, shapes/path Finish/Cancel, Assistants, Colorize, Smart Patch, Measure, Reference Images, SVG text, and Clone source selection. The Clone source action was newly exposed as `mobile_set_clone_source` in searchable Tools.
- Recovery: every dirty canvas has a private completed KRA draft, periodically refreshed and requested on app background. Previous completed saves/drafts have atomic private versions, SHA-256 checksum sidecars, duplicate suppression and up to 12 versions / 512 MiB per project. History is keyed by canonical local/SAF document identity and migrated on first Save As. Old copies open as a separate unsaved canvas; explicit Discard removes the working draft. Background process death cannot guarantee a final save beyond the most recently completed checkpoint.

## Fold 6 stability batch (November 11)

- Group/layer removal targets the live image and lets Krita finish its native remove/Undo stroke before rebuilding and validating rail selection. Group deletion uses Krita's subtree removal. Document activation restacks the command toolbar and dismisses its old menu. Home saves named native projects and writes a complete private KRA draft for unsaved drawings before closing. Explicit Close still uses native Discard. Tools uses a normal touch-scrolling dialog. Check Issue #1 for the exact WSL build. Fold validation remains pending.

- Tools and noneditable drop-downs distinguish a tap from a drag. Numeric spin boxes select the editable value on first focus. The preset picker has a denser grid, elided names and live Size, Opacity and compatible Flow controls. Layer options provide direct rename and visible Alpha Lock state, a larger opacity slider and native color-space-filtered blend categories. The compact rail has folder headers, nested group guides and an outlined target for dropping into groups. Clipped layers identify their closest base in layer options and accessibility text.
- The app currently keeps one active drawing open. The native view host can manage several views, but changing the single-canvas workflow needs a deliberate document-switching and recovery design.

- On canvas activation Studio refreshes brush mode, floating size/opacity and open Brush Settings. Hold **+** for native layer and mask types. The selected layer sheet exposes grouping and names the clipping base next to its clip action. The Fold feedback record includes [native layer and mask checks](DEVICE_FEEDBACK_2026-11-11.md#native-layer-and-mask-checks).
- The default Krita input profile maps a one-finger tap to the native popup palette while finger painting is off. The binding is also available under **Touch gestures → One-finger tap → More actions → Show popup palette**. Symmetry actions use Krita's native axes; the reported halo still needs a controlled preset comparison on the same painted layer and on a separate layer, including stock Krita.

## Latest changes (November 11)

- Archive restoration and version listing now serialize with archive pruning/migration; migrated filename collisions preserve both documents. Restore verifies ZIP header, byte count and stored SHA-256; damaged copies cannot suppress fresh checkpoints. Missing valid checksum sidecars can be repaired atomically.
- Reopening a named project skips a damaged newest draft and can use an older valid draft. Startup auto-restore rejects invalid headers; damaged drafts remain manually accessible. Qt tests include same-length checksum corruption, collision migration and newest-draft fallback.
- More layer actions validate the active canvas/target after dialog returns; drag/drop and deferred clip moves validate parents and refuse unsafe switches while a transform is pending.
- Native text/assistant/reference tool ID namespace variants, numeric popup focus, Transform menu callbacks, and Paint/Erase/Blend active-state identity have been hardened. Clone source is now a registered/searchable native Studio command. Provider URI query/fragment differences no longer fork canonical version-history keys.

## Real Fold 6 test evidence and outstanding gates

- **Previously device-PASS:** independent Android app launch, stylus drawing and pressure, KRA save/reopen, Android file sharing; basic selection; finger and S Pen layer drag, Undo/Redo, compact rail overlay, nearest-base clipping, grouping, lock/alpha/opacity, merge/flatten and many core layer interactions. See archived development log for test dates and exact APK revisions.
- **Do not mark device-PASS yet:** latest Transform and multi-layer actions after historical crash/empty-layer bug; native masks and mask KRA round-trip; new automatic crash/background draft recovery/explicit discard; restored-version behavior and SAF URI variations; full native Fill/clone/shape/assistant tool handling; popup focus and current native tool state.
- Next practical *disposable drawing* device batch after a confirmed APK: (1) paint → marquee → Transform Free/Perspective → commit/cancel → layer reorder and Undo; (2) two layers clipped to painted base, mask edits, save/reopen KRA; (3) dirty untitled and named drawings → background/relaunch → history/Save As, then explicit Don't Save; (4) Clone source in Tools → source tap → paint; (5) picker direct/tree URI variants.
- No need to interrupt current source work solely for device tests. Prefer grouped requests, and never test on irreplaceable artwork.

## Development priorities and code map

Keep native selections/transforms and layers/masks reliable; then complete native painting tools and the automatic version-history workflow. Home, brush/input and performance fixes can accompany those phases. Interactive fill tolerance and other Infinite Painter-inspired conveniences remain deferred. Do not redesign the interface prematurely.

- `apply_mobile_shell.py` — patch pinned upstream Krita and wire Android source target.
- `mobile_core/krita_standalone_runtime.h`, `krita_standalone_pilot.h`, `krita_standalone_documents.h`, `engine_bridge.h` — engine, canvas and document actions.
- `mobile_core/layer_workspace.h`, `brush_workspace.h`, `context_tool_bar.h`, `tool_browser.h` — mobile layer, brush, and native-tool access.
- `mobile_core/mobile_draft_recovery.h`, `saved_version_archive.h`, `document_home.h` — private recovery/checkpoints and Home.
- `tests/qt_saved_version_archive.cpp`, `mobile_core/CORE-FUNCTIONALITY-GATES.md`, `mobile_core/TEST-PLAN.md` — regression and release gates.
