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
- **Native layer operations:** `f7d7fba` adds **Opacity…** using native `KisNodeManager::setNodeOpacity`; `2851037` adds **Merge with layer below** and **Flatten layer to paint** using Krita's native layer manager. `ac97eba` adds **Add layer / mask…** with 7 native layer classes (paint, group, vector, fill, filter, file, clone) and 6 native mask classes (transparency, filter, transform, selection, colorize, fast overlay); adds **Blending mode…** from Krita's color-space-filtered mode registry, applied via `KisNodeManager::setNodeCompositeOp`. `4ceba2c` adds **Alpha lock** and **Inherit alpha (clipping)** through Krita's undoable property commands. **All PC-compiled SUCCESS; device test pending.**

## Most recent compact-layer iteration (November 9 local evening)

- Commits `570bc95` through `4875051` restored the older Infinite Painter-like thumbnail rail: full-size clipped rows, group/mask distinctions, inline eye/lock/alpha controls, pen/finger tap-select and second-tap options, hold-drag with edge scrolling, pinch grouping, and flat logical reordering. Native hidden Krita alpha-inheritance groups implement the displayed clipping relationships; the engine/compositor is **not** replaced. **PC build PASS** through `4875051`.
- Commit `d7b7b85` corrected a concrete menu bug: checked **Clip to layer below** had used `alphaChannelDisabled()` instead of the actual inherit-alpha property; it now reflects the correct layer's clipping state. Removed a duplicate clip command. Added current-document guards to the menu, drag/drop and pinch grouping. Follow-up `3b306fd` keeps the view pointer mutable for Krita ABI compatibility. **PC build PASS** for exact revision `3b306fd`.
- **Next device regression:** verify clip/unclip affects the intended layer and attaches to the first unclipped layer below, test reordering across clip boundaries and nested folders, check Alpha Lock and inline eye/lock on pen/finger, test Undo/Redo plus KRA save/reopen. Do not infer correctness of clip group/undo behavior from a successful compile. Keep APK uploads paused.

## General drawing access — next functional stage

- Commit `5569eb9` adds **More → Drawing** backed by Krita's native QAction registry. Exposes fill and stroke selection, canvas painting assistants and previews, guides/grids, and snapping controls. Missing/unregistered native actions are omitted rather than simulated. **PC Android build PASS** for this exact commit (2026-11-10 00:22 UTC); device test pending.
- Commit `6164aac` extends that menu to real Krita clipboard, clear and selection-to-layer operations. Commit `e61b9f7` refreshes the live KoToolDocker options when switching between native canvases. **Both changes are included in the successful `2dc4981` Android compilation**, but their on-device behavior has not been checked.
- Keep new UI design and the independent painting engine unchanged. Next functional priorities: validate new drawing commands on device after their PC build, complete gaps in the existing Krita tool integrations, then progress to the approved automatic project history subsystem after the core tools/layers gate.

## Native tool functionality expansion (2026-11-09 local evening)

- Commit `1ffe7cf` expands **More → Selection** to include select opaque / add / subtract / intersect and selection refinement actions (color range, feather, grow, shrink, border, smooth), only when Krita registers those native actions. It also adds **Drawing → Canvas view** (zoom/fit, canvas rotation, mirror/reset) and **Drawing → Image and layer transforms** (native scale/rotate/mirror/offset/image size commands). **Local WSL Android compile PASS** for this exact commit. UI/device behavior remains unverified; native actions are not reimplemented.
- Commits `1a3f08b` and `d1ebc20` connect the existing touch-searchable `MobileCommandBrowser` to **More → All Krita Commands** in the independent Android app. It enumerates the available action collection, including non-menu native commands, while preserving Krita's action handlers. The standalone host has no desktop menubar, so the browser's **Other Krita commands** category is the fallback. **Local WSL Android compile PASS** for exact integrated revision `d1ebc200723c42d0f2d2314a5c9ac6c57e221892`, reported 2026-11-10 00:56 UTC. Real device behavior pending.
- Do not modify the pending Fold 6 clipping/drag prototype until the user reports on it. Once general Krita functionality and device regressions stabilize, move to automatic project checkpoint/version history design (the user's most important deferred priority), with atomic files and explicit Discard semantics.

## Device feedback: layer rail and clipping (2026-11-09 evening)

- Fold 6 feedback after the first compact-tile iterations: **drag reordering did not work**, clipping did **not** behave like attaching to the nearest unclipped layer (or a whole underlying group), and the docked layer panel **resized/shrank the canvas**. The user provided visual examples of a narrow floating thumbnail rail and a simple clip menu. Treat this report as a failed device gate; earlier compiler SUCCESS did not validate those interactions.
- New changes: `e89bb94` floats Layers above the native canvas (no QDockWidget); `5f9d859` explicitly shows it on first open; `da05a6e` preserves its dark stylesheet. `3dbc461` follows finger/pen gestures across row targets and empty row backgrounds; `23abc7e` keeps pressed state and avoids unintended group insertion; `2dc4981` sends ordinary layer drag moves through a native undoable image stroke, bypassing unnecessary clip rebuilds. These changes aim at the reported failure; **real device verification is required**.
- Clipping: `320f0d9` waits for the native inherit-alpha property stroke to apply before rearranging hidden Krita wrapper groups. `d0b33cd` resolves/displays the nearest unclipped target in the menu; `e2f2a59` allows a normal group to be clipped as one composite. **All included in verified WSL compilation `2dc4981`, on-device behavior not yet checked.**
- Next disposable-art test: open Layers and confirm it overlays the canvas without reducing painting width; hold-drag paint layers up/down with finger AND pen; drag across a group without accidentally nesting; create painted transparent base, clip two painted layers above, reorder their positions, and verify they follow the nearest unclipped base; try a group as base and group as clipped layer; then Undo/Redo and KRA save/reopen. Do not promote clipping behavior to verified until this passes. APK auto-publishing stays off.

## Fold 6 layer feedback and stylus drag fix (November 9 local evening)

- **Device confirmed:** the compact layer system now works with finger input. Preserve existing clipping, grouping, native undo behavior, and the floating rail. The user wants the UI design refined later, not during current functional development.
- **Remaining issue:** S Pen hold-drag reordering did not work even though finger drag did. Commit `081db9e` keeps the layer engine/reorder implementation unchanged and routes TabletPress from the entire row/viewport to the correct layer, avoids null-thumbnail rejection, tracks tablet moves across widgets, and prevents synthesized mouse releases from ending a live stylus drag. It also supports mouse-only input on the viewport as a fallback. **Check the PC build status for this exact SHA before saying compiled.** Device test of stylus still pending.
- Other newer general Krita drawing/selection features are awaiting user feedback. Do not infer on-device results from PC compile.

## Test status

- **Last Fold 6 test: PASS** for separate native APK target at `7cf81bffcf5b84f483d0d92112c13a1e80a9f352`: app launched without grey screen, stylus drawing and pressure worked, KRA save/reopen worked, Android file opening/sharing worked.
- **Latest locally compiled/packaged Android code revision: `d1ebc200723c42d0f2d2314a5c9ac6c57e221892`, SUCCESS**, reported by WSL watcher in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) at 2026-11-10 00:56 UTC. Includes the prior floating layer/drag/clipping work, native view/transform/selection actions, and standalone **All Krita Commands** browser integration. **Fold 6 verification is still pending.**
- Doc-only commits may not trigger another APK build. The compiled SHA above is the meaningful source baseline; always read the latest Issue #1 for newer work.

## Agreed product roadmap

Read [PRODUCT-ROADMAP.md](PRODUCT-ROADMAP.md) for approved preservation goals and feature priorities. **Most important future feature is project version history with automatic checkpoints on background/exit/crash recovery**, except an explicit Close → Don't Save. Next are interactive fill tolerance and convenient perspective modes. These are **not for immediate implementation**. Complete current selections/transforms, layers, and other Krita tool adaptations first; version history comes next, interface redesign follows, and additional Infinite Painter-style features come after the interface. Animation and timelapse are lower priority.

## Next work — prioritize functional development, not more architecture churn

1. **Next device gate:** finger-controlled layer system is confirmed working on Fold 6. Test only the **S Pen hold-and-drag** fix introduced in `081db9e` (after its PC build succeeds), then let the user report other tools independently. Keep clipping/reorder logic and floating rail unchanged until a specific regression is reported. UI redesign and APK uploading remain paused.
2. **Do not** spend more time on desktop startup fallback infrastructure, rebuilding Krita's paint engine, redoing the main interface, or unused desktop dependency trimming unless it blocks functionality or meaningfully improves build/runtime speed. The shipped Android target may still link Krita desktop-oriented classes in `kritaui`; that does **not** mean the original desktop application is running.
3. PC watcher automatically compiles commits to `main`, publishes exact revision result in [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1) and `local/android-wsl-build`. Never use GitHub Actions to compile when PC is available. Work on independent tasks while it runs; inspect failure issues for exact errors. APK publishing is opt-in only, currently paused.
4. Ask for a short Fold 6 test checklist only when there is a meaningful device-only risk, new capability, or user-observable change. Keep controls finger- and pen-friendly with large targets and touch scrolling, but defer significant UI redesign until functionality is near complete.

## Important code map

- `apply_mobile_shell.py`: patches pinned upstream Krita C++/Android source and writes generated native entrypoint.
- `mobile_core/krita_android_runtime.cpp`, `krita_application_entry.cpp`, `engine_only_bootstrap.cpp`: direct Android startup.
- `mobile_core/krita_engine_session.h`, `krita_standalone_pilot.h`, `krita_standalone_documents.h`, `krita_native_view_factory.h`: engine session, native view, document and paint activation.
- `mobile_core/android_document_intents.cpp`, `document_service.h`, `layer_workspace.h`, `brush_workspace.h`: file and painting operations.
- `build-in-krita-container.sh`, `local-build.sh`: local WSL/Docker compile and package; do not ask user to run these while watcher active.
