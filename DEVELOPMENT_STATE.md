# Current development handoff — 2026-11-09

Read [AGENTS.md](AGENTS.md) first. This file is updated as implementation progresses; it is **not** an instruction to repeat old work. Read GitHub `main` and the local build status for live state.

## Current implementation

- **Dedicated Android native build target — PC + Fold 6 VERIFIED:** Android CMake builds native target `mobile_studio` from its generated `krita/mobile_studio_main.cc`; desktop Krita `main.cc` remains unmodified. Deployment uses `libmobile_studio.so`, manifest `android.app.lib_name=mobile_studio`, and export directory `mobile_studio_build_apk`; Java JNI namespace and APK applicationId unchanged. Full PC compile **and APK packaging SUCCESS** at `7cf81bffcf5b84f483d0d92112c13a1e80a9f352` (120.6s); **device validation PASS** after native library rename: no grey startup, pressure, KRA save/reopen and Android Open/Share. Krita painting engine libraries remain shared.
- **Native drawing host:** `MobileLightweightCanvasHost` now launches directly as the Android top-level. Normal startup no longer creates Krita's desktop `KisMainWindow` or a second V2 wrapper window; diagnostic-only failure handling replaces the V2/V3 recovery-mode sequence. Code: `mobile_core/engine_only_bootstrap.cpp`, `engine_only_startup_policy.h`.
- **Krita engine session:** `mobile_core/krita_engine_session.h` creates the native view manager and document/view/command ports. It uses Krita's actual brush, canvas, layers, undo, and KRA file engine; it does not recreate those engines.
- **Independent Android entry source:** generated `krita/mobile_studio_main.cc` now **always** calls `MobileKritaAndroidRuntime::launch`, never `KisApplication::start`. Mobile Android no longer has an explicit desktop fallback for launch flags or settings; desktop-only launch flags are logged/ignored while Android document Open/Share uses the native queue. This removal is **new, PC build pending**. The separate native target retains Krita Qt input/event infrastructure and actual engine libraries (`kritaui`, `KisPart`).
- **Native application entry module:** `mobile_core/krita_application_entry.cpp` now owns startup eligibility and the single-window launch decision. Krita's application class delegates to it instead of constructing the Android host directly. Still not a new process `main()`.
- **Independent Android install identity:** debug APK package is `org.krita.mobilestudio.debug`, launcher label **Krita Mobile Studio**. Configured by `apply_mobile_shell.py`, packaged by `build-in-krita-container.sh`. It is separate from the original Krita installation.
- **Engine initialization separated:** resource type, resource loader/cache, paint-op/tool/plugin registry setup lives in `mobile_core/krita_engine_initialization.cpp`, compiled separately in Android `kritaui`. Normal launches bypass `KisApplication::start` but retain the `KisApplication` Qt object for event handling; documents still use the real Krita `KisPart` service. Explicit sessions/batch operations retain legacy startup for now.
- **Latest PC Android APK build:** `7cf81bffcf5b84f483d0d92112c13a1e80a9f352` — **SUCCESS**, 120.6 seconds, reported 2026-11-09 21:42 UTC on [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). **Latest device-confirmed:** `267c3aa087f1bb66349ec3bfd82b4ef25a38e201` (including grey-screen fix). Compare source SHA and real local result; docs-only commits reuse cached APK.
- **Latest device-confirmed baseline (2026-11-09):** on Galaxy Z Fold 6, user marked PASS for independent startup, pressure/drawing, touch Brushes/Layers/Color, undo/redo, KRA save/reopen, Android Open with and Share. **Cold-start grey screen resolved** by the user-tested `267c3aa` one-shot window re-exposure; the earlier `09fee651` redraw-only change was insufficient. Other functionality unaffected. No APK was automatically uploaded.

- **Android external document intents:** `mobile_core/android_document_intents.{h,cpp}` queues cold-start files and forwards Java/Qt/remote Open requests to the existing native Krita importer. Opens queued files before creating a blank canvas. Handles content:// and file:// URLs. Java/manifest now accepts Android single-file Share via EXTRA_STREAM. **Open with / Share passed on Fold 6 before the native library target rename; retest only as needed.**

- **Cold-start grey screen RESOLVED on Fold 6:** `267c3aa` performs a one-time Qt top-level hide/show after attaching the first real GL canvas. User confirmed it fixes the grey screen. Preserve while separating packaging.

## Immediate next milestone

1. **Next:** let the PC watcher compile the mobile-only Android entrypoint without desktop fallback. Once successful, prioritize filling missing core painting functionality; avoid repeated startup/fallback refactors and do not redesign the UI until core features work.
2. Let the already-running local watcher compile automatically after code commits. Fix local failures by reading the exact revision in Issue #1 or `local/android-wsl-build` status. Work on independent tasks during the build.
3. Request **one short Fold 6 test checklist only when it is useful** to confirm the new direct-host/package behavior: opens; stylus pressure/draw; layers/color; undo/redo; KRA save/reopen; close. Don't ask for repetitive smoke tests on every small change.
4. After architecture is standalone enough, implement missing core features; postpone layout/polish. Design eventual UI explicitly for **finger touch on a compact phone**, not mouse/keyboard.

## Existing communication with the PC

The local WSL watcher is already polling GitHub and compiling commits; the companion reporter updates [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) and commit status `local/android-wsl-build`. See [BUILD-REPORTING.md](BUILD-REPORTING.md) for details. The GitHub code repository is the **command channel** for new app revisions, not the build server. Never assume a watcher report describes HEAD without comparing commit SHAs. `APK-UPLOAD-REQUEST.md` is an opt-in upload mechanism, currently paused. No need to ask the user to run builds unless the watcher has stopped or needs a one-time reconfiguration.

## Device result (2026-11-09)

User explicitly confirmed all phone checks after migrating to a separate `mobile_studio` native Android target: no grey screen, stylus pressure/drawing, KRA save/reopen, and Android Open/Share. This validates the `7cf81bf` native build, not later code commits. Retain the first-canvas one-shot surface refresh validated in `267c3aa`.
