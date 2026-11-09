# Mobile Studio development handoff — 2026-11-09

**Read [AGENTS.md](AGENTS.md) first.** Source of truth is current GitHub `main` plus [PC watcher Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1), not the chat. Check commit SHAs before making claims about a build.

## Verified milestones

- **Separate Android application:** native CMake target `mobile_studio` has its own generated `krita/mobile_studio_main.cc`. Qt Android loads `libmobile_studio.so`; manifest's `android.app.lib_name` and `mobile_studio_build_apk` packaging directory match. APK ID is `org.krita.mobilestudio.debug`, label Krita Mobile Studio, separate from Krita. The upstream desktop `krita/main.cc` is not modified.
- **No desktop startup in Android:** native entry calls `MobileKritaAndroidRuntime::launch` only; `KisApplication::start` and its fallback are never invoked by the Android app. Old startup preference switches and engine-status UI were removed from native controls. **Krita's Qt application object and engine libraries remain reused** for input, resources, brushes, tools, views, and KRA file formats. Do not rewrite these.
- **Native engine architecture:** `mobile_core/krita_engine_session.h` creates a `KisViewManager`, real `KisView` canvas and Krita-backed document/view/command ports inside one `MobileLightweightCanvasHost` top-level window. No normal `KisMainWindow` or desktop MDI. Resource/plugin registration has been extracted to `krita_engine_initialization.cpp` in `kritaui`.
- **Android document flows:** Cold and warm Android Open with / Share file intents reach the independent document service, including content URIs. Native `KisPart` / `KisDocument` provides painting, undo, saving, and KRA import/export. Normal Brushes/Layers/Color/Tools controls function with touch.
- **Cold-start grey screen fixed on device:** the initial GL surface sometimes stayed grey until background/foreground. `267c3aa` added a *one-shot* native window re-exposure after first real canvas attachment. User confirmed it resolved the problem. Preserve that behavior.

## Test status

- **Last Fold 6 test: PASS** for separate native APK target at `7cf81bffcf5b84f483d0d92112c13a1e80a9f352`: app launched without grey screen, stylus drawing and pressure worked, KRA save/reopen worked, Android file opening/sharing worked.
- **Latest locally compiled/packaged Android revision: `2123da4258137d757dea660a2a6afd450fc80f84`, SUCCESS**, reported by the WSL watcher in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) on 2026-11-09 at 21:57 UTC. Contains Android entrypoint with no desktop fallback (commit `789b694`) and removal of obsolete startup/debug menu choices (commit `2123da4`). This revision is **PC-compiled, not separately device-tested**; no painting engine code changed.
- Doc-only commits may not trigger another APK build. The compiled SHA above is the meaningful source baseline; always read the latest Issue #1 for newer work.

## Next work — prioritize functional development, not more architecture churn

1. The decoupling milestone is sufficient to begin building functionality back up. Identify concrete missing painting operations through existing Krita engine action/tool APIs, implement them using native service ports, and validate with the local PC watcher. Keep pen/touch parity. Useful priorities include selection/transform operations, layer management, document operations, and gesture behavior; choose implementation by current code gaps, not speculative UI polish.
2. **Do not** spend more time on desktop startup fallback infrastructure, rebuilding Krita's paint engine, redoing the main interface, or unused desktop dependency trimming unless it blocks functionality or meaningfully improves build/runtime speed. The shipped Android target may still link Krita desktop-oriented classes in `kritaui`; that does **not** mean the original desktop application is running.
3. PC watcher automatically compiles commits to `main`, publishes exact revision result in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) and `local/android-wsl-build`. Never use GitHub Actions to compile when PC is available. Work on independent tasks while it runs; inspect failure issues for exact errors. APK publishing is opt-in only, currently paused.
4. Ask for a short Fold 6 test checklist only when there is a meaningful device-only risk, new capability, or user-observable change. Keep controls finger- and pen-friendly with large targets and touch scrolling, but defer significant UI redesign until functionality is near complete.

## Important code map

- `apply_mobile_shell.py`: patches pinned upstream Krita C++/Android source and writes generated native entrypoint.
- `mobile_core/krita_android_runtime.cpp`, `krita_application_entry.cpp`, `engine_only_bootstrap.cpp`: direct Android startup.
- `mobile_core/krita_engine_session.h`, `krita_standalone_pilot.h`, `krita_standalone_documents.h`, `krita_native_view_factory.h`: engine session, native view, document and paint activation.
- `mobile_core/android_document_intents.cpp`, `document_service.h`, `layer_workspace.h`, `brush_workspace.h`: file and painting operations.
- `build-in-krita-container.sh`, `local-build.sh`: local WSL/Docker compile and package; do not ask user to run these while watcher active.
