# Current development handoff — 2026-11-09

Read [AGENTS.md](AGENTS.md) first. This file is updated as implementation progresses; it is **not** an instruction to repeat old work. Read GitHub `main` and the local build status for live state.

## Current implementation

- **Native drawing host:** `MobileLightweightCanvasHost` now launches directly as the Android top-level. Normal startup no longer creates Krita's desktop `KisMainWindow` or a second V2 wrapper window; diagnostic-only failure handling replaces the V2/V3 recovery-mode sequence. Code: `mobile_core/engine_only_bootstrap.cpp`, `engine_only_startup_policy.h`.
- **Krita engine session:** `mobile_core/krita_engine_session.h` creates the native view manager and document/view/command ports. It uses Krita's actual brush, canvas, layers, undo, and KRA file engine; it does not recreate those engines.
- **Native application entry module:** `mobile_core/krita_application_entry.cpp` now owns startup eligibility and the single-window launch decision. Krita's application class delegates to it instead of constructing the Android host directly. Still not a new process `main()`.
- **Independent Android install identity:** debug APK package is `org.krita.mobilestudio.debug`, launcher label **Krita Mobile Studio**. Configured by `apply_mobile_shell.py`, packaged by `build-in-krita-container.sh`. It is separate from the original Krita installation.
- **Engine initialization extraction underway:** resource type, resource loader/cache, paint-op/tool/plugin registry boot now lives in `mobile_core/krita_engine_initialization.cpp`, compiled separately in Android `kritaui`. `KisApplication` delegates to it. This removes concrete registry initialization from the desktop application class on Android without replacing Krita's real engine. **Still not fully independent:** the process enters through `KisApplication::start`; documents use `KisPart`. The independent app entrypoint is the next architectural boundary. External Android file intents / explicit sessions still use the legacy compatibility route.
- **Latest verified local compile at time of this note:** revision `65edc60bedac4274398a10afd0f96e9254ebf1ba`; **SUCCESS**, ~111 seconds, reported 2026-11-09 20:24 UTC on [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). This compiled and packaged the extracted engine registry and new Android startup module. It does **not** establish on-device startup/pressure parity. Always check Issue #1 for a newer SHA.
- **Last device-confirmed baseline:** earlier engine-only V2 worked with zero Krita desktop windows/MDI objects. New direct-host packaging/startup has not yet been confirmed by the user on Fold 6. No APK was automatically uploaded.

## Immediate next milestone

1. **Next boundary:** give the Android app its own process-level start routine instead of calling the large desktop-oriented `KisApplication::start`. Keep required input/stylus workarounds, resource/plugin registration, KRA document handling and normal Qt lifecycle. Avoid rewriting any painting engine. The native modules introduced in this milestone are the staging point.
2. Let the already-running local watcher compile automatically after code commits. Fix local failures by reading the exact revision in Issue #1 or `local/android-wsl-build` status. Work on independent tasks during the build.
3. Request **one short Fold 6 test checklist only when it is useful** to confirm the new direct-host/package behavior: opens; stylus pressure/draw; layers/color; undo/redo; KRA save/reopen; close. Don't ask for repetitive smoke tests on every small change.
4. After architecture is standalone enough, implement missing core features; postpone layout/polish. Design eventual UI explicitly for **finger touch on a compact phone**, not mouse/keyboard.

## Existing communication with the PC

The local WSL watcher is already polling GitHub and compiling commits; the companion reporter updates [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) and commit status `local/android-wsl-build`. See [BUILD-REPORTING.md](BUILD-REPORTING.md) for details. The GitHub code repository is the **command channel** for new app revisions, not the build server. Never assume a watcher report describes HEAD without comparing commit SHAs. `APK-UPLOAD-REQUEST.md` is an opt-in upload mechanism, currently paused. No need to ask the user to run builds unless the watcher has stopped or needs a one-time reconfiguration.
