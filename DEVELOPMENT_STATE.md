# Mobile Studio development handoff — 2026-11-09

**Read [AGENTS.md](AGENTS.md) first.** Source of truth is current GitHub `main` plus [PC watcher Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1), not the chat. Check commit SHAs before making claims about a build.

## Verified milestones

- **Separate Android application:** native CMake target `mobile_studio` has its own generated `krita/mobile_studio_main.cc`. Qt Android loads `libmobile_studio.so`; manifest's `android.app.lib_name` and `mobile_studio_build_apk` packaging directory match. APK ID is `org.krita.mobilestudio.debug`, label Krita Mobile Studio, separate from Krita. The upstream desktop `krita/main.cc` is not modified.
- **No desktop startup in Android:** native entry calls `MobileKritaAndroidRuntime::launch` only; `KisApplication::start` and its fallback are never invoked by the Android app. Old startup preference switches and engine-status UI were removed from native controls. **Krita's Qt application object and engine libraries remain reused** for input, resources, brushes, tools, views, and KRA file formats. Do not rewrite these.
- **Native engine architecture:** `mobile_core/krita_engine_session.h` creates a `KisViewManager`, real `KisView` canvas and Krita-backed document/view/command ports inside one `MobileLightweightCanvasHost` top-level window. No normal `KisMainWindow` or desktop MDI. Resource/plugin registration has been extracted to `krita_engine_initialization.cpp` in `kritaui`.
- **Android document flows:** Cold and warm Android Open with / Share file intents reach the independent document service, including content URIs. Native `KisPart` / `KisDocument` provides painting, undo, saving, and KRA import/export. Normal Brushes/Layers/Color/Tools controls function with touch.
- **Cold-start grey screen fixed on device:** the initial GL surface sometimes stayed grey until background/foreground. `267c3aa` added a *one-shot* native window re-exposure after first real canvas attachment. User confirmed it resolved the problem. Preserve that behavior.

## Selection/transform and layer functionality

- Mobile Studio's **Tools** chooser already exposes native Krita registered tools. Added **More → Tool Options** using Krita's real `KoToolDocker` and `KoToolManager::toolOptionWidgetsChanged` signal. The panel displays native tool-specific controls for selections, transforms, fill, brush and other tools, and follows changes to the active Krita tool/canvas without restarting it. Source: `mobile_core/krita_native_workspace.h`, commit `5a6b478`. **Local PC APK build PASS.**
- Added **More → Selection** with real Krita selection manager actions (Select All, Deselect, Reselect, Invert, Edit Selection, convert to raster/vector when registered). No new selection engine and no custom undo model. Source: `mobile_core/krita_standalone_pilot.h`, commit `8024fb8`. **Local PC APK build PASS, reported 2026-11-09 22:30 UTC.**
- **Device feedback (Nov 9):** user confirmed selection/transform controls and Tool Options work on Fold 6 and supplied a screenshot. The **More menu text was unreadable (light-on-light) until highlighting**; several native Tool Options edit fields also appeared light-on-light. This initiated the following fixes.
- **Menu/Tool Options contrast** `ecf603c`: explicit dark/high-contrast palette for More and its nested Selection menu, and readable input-field colors for native tool options. **PC compile SUCCESS, device verification pending.**
- **Native layer operations:** `f7d7fba` adds **Opacity…** to the selected layer's context menu, invokes native `KisNodeManager::setNodeOpacity` so changes are undoable, and validates document identity through the dialog. `2851037` adds **Merge with layer below** and **Flatten layer to paint**, calling Krita's original layer manager actions. **Both PC-compiled SUCCESS, device test pending.**

## Test status

- **Last Fold 6 test: PASS** for separate native APK target at `7cf81bffcf5b84f483d0d92112c13a1e80a9f352`: app launched without grey screen, stylus drawing and pressure worked, KRA save/reopen worked, Android file opening/sharing worked.
- **Latest locally compiled/packaged Android revision: `2851037edeef586bd94ac87ef4dd01228e0931c6`, SUCCESS**, reported by WSL watcher in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) on 2026-11-09 at 22:45 UTC. Previous UI contrast revision `ecf603c` and opacity `f7d7fba` also passed. **Device verification still needed for menu contrast and layer opacity/merge.**
- Doc-only commits may not trigger another APK build. The compiled SHA above is the meaningful source baseline; always read the latest Issue #1 for newer work.

## Agreed product roadmap

Read [PRODUCT-ROADMAP.md](PRODUCT-ROADMAP.md) for approved preservation goals and feature priorities. **Most important future feature is project version history with automatic checkpoints on background/exit/crash recovery**, except an explicit Close → Don't Save. Next are interactive fill tolerance and convenient perspective modes. These are **not for immediate implementation**. Complete current selections/transforms, layers, and other Krita tool adaptations first; version history comes next, interface redesign follows, and additional Infinite Painter-style features come after the interface. Animation and timelapse are lower priority.

## Next work — prioritize functional development, not more architecture churn

1. **Next:** once practical, device-test the contrast fix (More and submenu text readable without hover) and new layer context operations (Opacity change/Undo, Merge Below, Flatten). Then proceed with additional native layer types, blending mode/mask access and selection/transform functions not yet exposed. Continue to use Krita's undo/action infrastructure. Project version history remains a later high-priority milestone per PRODUCT-ROADMAP; no interface redesign now.
2. **Do not** spend more time on desktop startup fallback infrastructure, rebuilding Krita's paint engine, redoing the main interface, or unused desktop dependency trimming unless it blocks functionality or meaningfully improves build/runtime speed. The shipped Android target may still link Krita desktop-oriented classes in `kritaui`; that does **not** mean the original desktop application is running.
3. PC watcher automatically compiles commits to `main`, publishes exact revision result in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) and `local/android-wsl-build`. Never use GitHub Actions to compile when PC is available. Work on independent tasks while it runs; inspect failure issues for exact errors. APK publishing is opt-in only, currently paused.
4. Ask for a short Fold 6 test checklist only when there is a meaningful device-only risk, new capability, or user-observable change. Keep controls finger- and pen-friendly with large targets and touch scrolling, but defer significant UI redesign until functionality is near complete.

## Important code map

- `apply_mobile_shell.py`: patches pinned upstream Krita C++/Android source and writes generated native entrypoint.
- `mobile_core/krita_android_runtime.cpp`, `krita_application_entry.cpp`, `engine_only_bootstrap.cpp`: direct Android startup.
- `mobile_core/krita_engine_session.h`, `krita_standalone_pilot.h`, `krita_standalone_documents.h`, `krita_native_view_factory.h`: engine session, native view, document and paint activation.
- `mobile_core/android_document_intents.cpp`, `document_service.h`, `layer_workspace.h`, `brush_workspace.h`: file and painting operations.
- `build-in-krita-container.sh`, `local-build.sh`: local WSL/Docker compile and package; do not ask user to run these while watcher active.
