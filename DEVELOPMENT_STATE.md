# Krita Mobile Studio — current development state

**Updated:** November 11, 2026. **Branch:** `main` of `murrdusa/krita-android-custom-build`.

Read `AGENTS.md` first, then `PRODUCT-ROADMAP.md` and [local WSL build Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). This file records *current* status, not a commit-by-commit log. The preceding complete log is preserved verbatim in [DEVELOPMENT_HISTORY_2026-11-11.md](DEVELOPMENT_HISTORY_2026-11-11.md). More focused runbooks live under `mobile_core/`.

## Source of truth and build gate

- Independent Android APK target `mobile_studio`, package ID `org.krita.mobilestudio.debug`, based on the actual Krita 6.0.4 paint/resource/tool/document engine. It runs through its own Qt native startup, `MobileLightweightCanvasHost`, `KisViewManager` and real `KisView`, **not** the desktop `KisMainWindow` or MDI. Native C++ engine extraction is in `mobile_core/krita_engine_initialization.cpp` and `apply_mobile_shell.py`.
- The local Windows/WSL2 watcher automatically builds commits to `main` and records the **exact** revision plus `local/android-wsl-build` status in Issue #1. Never infer a newer build from a previous SUCCESS. Do **not** run GitHub-hosted Android builds or upload APKs unless specifically instructed. No need to ask the user to manually pull/build while the watcher runs.
- Last build verified while preparing this compact handoff: `3883e2ae66562c482125c067bc03b8c1a4fa3381` **SUCCESS** (Issue #1, 2026-11-11 08:42:40 UTC). More commits followed; check Issue #1 again before saying current `main` compiled. In particular pending after that revision: native Clone action/catalog, SAF URI normalization/tests, brush-state family matching, and this documentation split.
- Cross-compiled APK success **is not** a Fold 6 runtime pass. The `qt_saved_version_archive` regression source was extended and compiled through the Android integration where applicable, but its standalone Qt test binary has **not** been executed here.

## Current application and capabilities

- Normal cold launch enters Home unless opened by a file intent; Home has New Canvas, Browse Files, Recent, All Projects, folder grants and Recover Drafts. Document open/save/Save As/export/close use native Krita engine and Android SAF permissions. Krita owns Save/Discard/Cancel prompts. A separate working canvas uses actual Krita brushes, pen pressure, undo/redo and KRA serialization.
- Touch-first floating controls: configurable icon toolbar, Paint/Erase/Blend preset memory, tool selection/grid, native Tool Settings, round Color wheel, Layers rail, brush settings, canvas-only mode, and keyboard-free dialogs. Native Krita action collections retain menu and searchable command access. Toolbar and panel redesign is intentionally deferred.
- Native layer operations: selectable/drag-reorderable rows with finger and S Pen, visible masks/groups, native clipping via alpha inheritance and hidden wrapper groups, optional multi-layer targets, opacity/blending/visibility/locks, create/duplicate/rename/delete/merge/flatten, Undo/Redo and native image-stroke layer moves. Do not replace Krita's compositor, paint ops, or undo engine.
- Native drawing tools exposed: freehand smoothing, selection combinations and refinements, transform modes/parameters, Fill/Enclose and Fill, gradients, shapes/path Finish/Cancel, Assistants, Colorize, Smart Patch, Measure, Reference Images, SVG text, and Clone source selection. The Clone source action was newly exposed as `mobile_set_clone_source` in searchable Tools.
- Recovery: every dirty canvas has a private completed KRA draft, periodically refreshed and requested on app background. Previous completed saves/drafts have atomic private versions, SHA-256 checksum sidecars, duplicate suppression and up to 12 versions / 512 MiB per project. History is keyed by canonical local/SAF document identity and migrated on first Save As. Old copies open as a separate unsaved canvas; explicit Discard removes the working draft. Background process death cannot guarantee a final save beyond the most recently completed checkpoint.

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
