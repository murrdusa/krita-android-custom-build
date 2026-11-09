# Krita Mobile Studio — core-first stabilization plan

## Development rule (2026-11-08)

**Freeze visual UI redesign and layout churn until the core functionality
meets the release gates below.** Preserve the user's existing mobile
controls and settings. Prioritize Krita engine integration and safe
document lifecycle over visual polish. Do not turn on experimental
stationary-canvas mode by default until on-device checks pass.

The goal is a mobile-first application using Krita's **real painting
engine**, not a visually different copy of Krita's desktop window.
Incremental isolation is safer than rewriting painting, undo, KRA
serialization or brushes.

## Current backend boundary — implemented

- `document_host_port.h` defines create-view, open and save operations
  in terms of Krita engine documents, not `KisMainWindow`.
- `command_registry_port.h` exposes real Qt/Krita actions and menu
  entries without assuming a desktop window class.
- `desktop_host_adapters.h` is the **single remaining legacy host
  adapter** for these operations; it forwards to Krita's existing
  desktop implementation, preserving its import/export and native
  Save/Discard/Cancel behavior.
- `document_service.h`, `command_service.h` and `engine_bridge.h`
  no longer store/require a `KisMainWindow`. Both host ports are held
  through copyable shared lifetime handles; the desktop adapters use
  guarded `QPointer` references to the actual window.
- The Android bootstrap creates the document and command adapters
  once. It now also injects a **view-host port**: `view_host_port.h`
  exposes guarded view identities, current selection, modified state,
  activation, and native close without requiring `QMdiArea`.
  `mdi_view_host_adapter.h` alone handles Krita's desktop MDI views.
- `document_service.h` and `engine_bridge.h` no longer take a
  `QMdiArea` or include `KisView`. The document service also no longer
  requires `KisViewManager`: the view-host port supplies a guarded
  current-document identity, allowing Open/Save/Close state to be
  calculated independently of Krita's desktop MDI and selection manager.
  The painting bridge still uses Krita's real resources, brushes and
  node manager. A mobile-first host can supply a new view model and
  document/action implementations without altering the UI.
- `view_identity_guard.h` rejects ambiguous active views, changed
  document identities, reused native indices and destroyed view
  pointers. This avoids saving/closing another canvas after deferred
  UI callbacks.
- The mobile frontend can later substitute new document, command and
  view hosts **without rewriting brushes, layers, Quick Actions or
  document dialogs**.
- `canvas_transfer.h` now takes a generic `QWidget` document root
  rather than `QMdiArea`, and `canvas_session.h` no longer needs the
  MDI type. On desktop Krita it still refreshes the native scroll-area
  viewport; a lightweight Qt canvas stack can use the same lifecycle
  code without implementing MDI. A Qt-only generic-document-root smoke
  test checks repeated entry/Back and painting-widget identity.
- `lightweight_canvas_host.h` now provides a **separate, Qt-only
  application-window prototype**, with immutable native canvas ownership
  and the same injected document/view/command host services. Its mobile
  chrome changes without moving the canvas, and its 12-cycle fake-engine
  test exercises entry/Back, exact widget identity and offline services.
  The prototype is compiled with Android but **not launched**: it does
  not yet initialize Krita's `KisPart`, painting resources or a live
  `KisView`. That is the next engine-bootstrap task.
- `native_view_stack.h` now implements a real multi-document Qt widget
  stack and `MobileViewHostPort` without MDI. Its lifecycle enforces
  exact view and document identities, supports selection and modified
  metadata, honors canceled native close callbacks, and invalidates
  views when their document is destroyed. A Qt-only test verifies twelve
  canvas-preserving UI transitions. This is a functional host boundary,
  **not yet live KisView construction or Android painting parity**.
- Krita 6.0.4's actual `KisView` teardown and modified-document
  close-save path both assumed a concrete `KisMainWindow`. Android
  source patching now guards those hard-coded dependencies, introduces a
  QPointer-guarded native save-host callback on `KisView`, and changes
  `KisViewManager::mainWindowAsQWidget()` to use its existing Qt-native
  parent accessor. `native_krita_save_host.h` delegates close-time
  Save/Save As to the injected real Krita document port, and has a Qt
  regression for backend failure, reentrancy and host destruction.
  Missing save callbacks fail closed; the working desktop fallback
  continues to use Krita's original native methods. This is a source
  compatibility seam, **not live Android `KisView` initialization**.
- `krita_native_view_factory.h` now contains an unlaunched real-engine
  attachment path using `KisPart::createView`, `KisViewManager`,
  input/tool tracking, and native close-time deletion. The Android
  `KisView` destructor's non-desktop path now also unregisters tracked
  canvases and clears active view state. The new factory is copied
  into Krita's source tree but is **not instantiated, compiled as an
  Android APK or verified on a device**. It remains isolated until
  standalone manager construction and GL lifetime are validated.
- `krita_standalone_runtime.h` now stages a separate real Krita
  `KisKActionCollection` and `KisViewManager` owned by a lightweight
  `QMainWindow`, without creating a desktop `KisMainWindow`. Its
  destruction order retains the manager until all native `KisView`
  widgets have been released. The Android source adapter fixes native
  view counting (previously always zero without desktop MDI) and uses
  the generic Qt host in Krita's canvas surface/color configuration.
  This engine bootstrap remains **unlaunched and not APK-compiled**:
  standalone save/open adapters and real Android GL drawing require
  further implementation and device validation.
- `krita_standalone_documents.h` now stages the real-engine
  `MobileDocumentHostPort` implementation for a non-desktop host.
  Its create/open flow delegates to `KisPart`/`KisDocument::openPath`,
  rolls back failed views, and retains Krita's autosave-recovery
  dialog and file import filters. Its Save/Save As uses Krita's
  render-completion safety dialog, Android-aware `KoFileDialog`,
  native export filters and `KisDocument::save/saveAs`; it rechecks
  registered document identity after dialogs and refuses unknown MIME
  types. The bootstrap bundles the native document, view, and command
  ports without a `KisMainWindow`. Native view title/dirty metadata
  now follows Krita's document signals. **This is source-staged only**:
  neither a standalone Android launch nor real file round trips have
  been verified.
- The user-observed blank desktop-style startup is now recoverable
  through **Settings → Mobile Studio (experimental)**, independent of
  whether a saved Krita workspace hides the toolbar. The native Settings
  menu includes the pilot QAction and the XMLGUI version/checksum are
  updated. Automatic Mobile Studio entry waits for a visible canvas and
  retries boundedly, immediately clearing the incomplete-boot marker
  after success. A genuinely failed or interrupted start continues to
  fall back safely to Krita; existing installs may need to manually
  reenable the startup preference. Full Android device verification
  is still pending.
- Mobile-first startup now defaults on for **new installations**
  while respecting an explicit saved opt-out or the existing crash guard.
  The Qt-only `mobile_startup_policy.h` prevents repeated reparenting
  on a failed startup: readiness can be retried, but the actual
  painting-widget handoff is attempted only once. A new Qt regression
  validates settings persistence, failure recovery and readiness.
  Existing APKs require rebuilding; saved opt-outs remain authoritative.
- The upstream 6.0.4 `KisView::Private` constructor requires
  `KoCanvasSupervisor` for its real `KisCanvasController`, not
  a plain `QMainWindow`. The lightweight host now implements the
  real observer interface and the Android-only Krita patch routes
  canvas initialization through it. Until we implement native docker
  observers, it correctly returns an empty observer list. The Android
  canvas constructor no longer attempts connecting a missing desktop
  `guiLoadingFinished` signal; native view activation supplies its
  own bootstrap callback. Other desktop-only native view handlers
  (dropping files, window state, closing without QMdiSubWindow)
  now fail safely or use Qt-native document close events.
  These are real-engine source boundaries, still awaiting full Android
  compilation and actual stroke validation.
- **Compiler coverage gap closed in source.** The independent engine
  headers used to be copied into Krita's tree but were never included by
  any built target. `mobile_native_engine_compile_probe.cpp` now imports
  all the native Krita view/runtime/document interfaces, and
  `apply_mobile_shell.py` adds it to the Android `kritaui` library
  source list. The next real Android `kritaui` compile will therefore
  type-check these engine APIs, exposing compilation problems that
  static source tests cannot find. This is not a claim that a real
  compile already passed.
- Android compiler-only CI now reads CMake's actual
  `compile_commands.json` and first runs a `-fsyntax-only` check
  against `mobile_native_engine_compile_probe.cpp`, reusing the
  genuine Android Clang flags and Krita include directories.
  A standalone API mismatch therefore fails before the much slower
  full `kritaui` compilation. If that preflight passes, the complete
  native UI target is still compiled. APK packaging and upload stay
  disabled in compile mode.
- Native view attachment failure now uses a `QPointer<KisDocument>`
  rollback guard in both new-canvas and file-open paths. A failing
  `KisPart::createView` may synchronously delete the document through
  last-view teardown. The guarded path checks surviving registration
  before removing an orphan and never double-removes a freed document.
  A new Qt test covers destroyed identities, orphan cleanup, repeated
  rollback, and unregistered documents.
- The standalone engine has now passed a **real Android `kritaui` library
  compile**, including the native view, manager and document backend
  headers. A separate **Standalone Engine Test (experimental)** action
  in Krita Settings now launches a new `MobileLightweightCanvasHost`
  and `KisViewManager` with its own 512×512 white canvas using the
  original Krita painting engine. Its temporary debugging toolbar
  exposes New, Open, Save, Save As and Close view. It never reparents
  existing desktop canvases or auto-launches, and the current app
  interface remains unchanged. This new launch path must pass a fresh
  Android compile and device painting/file round-trip tests before
  being considered functional.
- The separate standalone engine pilot exposes **Undo** and **Redo**
  through each active `KisDocument::undoStack()` directly, in
  addition to create/open/save/close. This provides the smallest
  useful end-to-end drawing proof without implementing new mobile
  history or brush engines. The initial observed Android compile error
  was a Qt `QPointer<KisView>` to `QPointer<QObject>` conversion;
  that native-view identity conversion has been made explicit.
- The opt-in stationary canvas header was found consuming hundreds of
  pixels of height on a real device. The top dock is now capped at 76
  logical pixels and its title/header widget is height-fixed; a Qt
  regression check prevents recurrence. This is a defect fix, not a UI
  redesign.
- Deferred Save/Save As from the stationary-canvas path revalidates
  the exact native MDI view captured at tap time before delegating to
  Krita. This rejects ambiguous or changed document selection instead
  of saving the wrong file.

## Functionality release gates

| Area | Acceptance criteria | Current status |
|---|---|---|
| Krita engine | Native brush presets, pen pressure, zoom, canvas transforms, blending, eraser, color space, undo/redo | Engine reused; on-device comprehensive regression pending |
| Documents | Create; open KRA and supported formats; save/save as; correct dirty state; unsaved changes prompt; multiple canvases | Native Krita backend; end-to-end device tests pending |
| Layers | Paint, group, move, clip, visibility, masks, rename and undo/redo native operations | Existing integrations; device regression pending |
| Navigation | Back/reentry without deleting/reparenting a document; portrait/landscape, suspend/resume | Generic Qt document-root lifecycle implemented; stationary Qt docks opt-in; device regression pending |
| Input | Stylus and S Pen, multi-finger gestures, opt-in calibrated finger pressure without interfering with pen/multitouch | Implemented prototype; real touch-size hardware validation pending |
| Persistence | Save/reopen KRA preserving paint, groups, masks, brush resources and canvas coordinates | Uses Krita serializer; round-trip validation pending |
| Host independence | Mobile document, command and view services build without a desktop-window/MDI dependency | **Three backend ports integrated; Android build validation and Qt regression checks ongoing** |
| Replacement host | Launch/paint/save via a lightweight host that initializes Krita engines without constructing the full desktop `KisMainWindow` | **Not yet implemented** |
| APK | Build deployable Android package; no regressions on target phone | Android builds running; on-device signoff pending; automatic upload paused |

None of the above on-device gates can be marked passed merely because
an APK compiled, and no test should use irreplaceable artwork.

## Next milestones (core first)

1. Keep the new document/action host contracts stable. Run the Qt fake
   host-port test and monitor local Android compilation after every
   backend change.
2. Complete canvas-session ownership isolation beyond the now
   MDI-independent document-root interface. Keep verifying repeated
   Back/reentry, invalidated views and interruption with Qt tests and
   the Android build watcher.
3. Introduce a **separate opt-in lightweight Krita host bootstrap**
   that wires the same host ports and existing KisDocument, KisPart,
   KisView and brush resources. Keep the old launcher as rollback.
4. Verify real Android drawing, touch, multi-doc Save/Discard/Cancel,
   document round-trips and background/foreground lifecycle.
5. Promote the lightweight host only once parity tests pass. Then
   revisit visual UI revisions against a stable functional backend.

**Out of scope during this phase:** new color-wheel layouts, animation,
toolbar rearrangement, visual styling, or replacing Krita's actual
painting and document engines.

APK publishing stays paused via `APK-UPLOAD-REQUEST.md` unless the user
explicitly requests a test artifact.
