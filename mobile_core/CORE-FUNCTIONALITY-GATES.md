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
- Native document open/save now recognizes Android Storage Access
  Framework `content://` identities. It no longer rejects the
  document-picker's provider URI based only on `QFile::exists`, or
  forces Save As on an already-open provider document merely because
  ordinary filesystem checks cannot inspect it. Krita remains the
  authority for permission validation, import and overwrite. The
  native path policy has focused Qt regression coverage for local
  files, missing paths, provider URIs, explicit Save As, autosave
  recovery and unsupported formats. It is an opt-in prototype change.
- Native standalone runtime now mirrors Krita 6.0.4 desktop's
  paint-tool initialization boundary by registering each
  `KoToolFactoryBase` action through `KoToolRegistry` before
  `KisView::setViewManager()` requests the default brush. This
  prevents a valid canvas from having a missing tool action registry.
  The pilot remains opt-in and requires device validation.
- The independent engine pilot now has previous/next canvas controls
  backed by the MDI-free `MobileViewHostPort` and live guarded view IDs.
  Navigation rejects ambiguous active views, duplicate identities and
  out-of-sync Krita document selections, and wraps among multiple canvases.
  The new Qt regression checks these cases; actual Android painting and
  switch-state behavior still require a device test.
- Standalone Open runs after the initiating toolbar event, and native
  Save / Save As / Close now defer against the exact captured view and
  document. The deferred command refuses a changed selection or an
  invalidated window instead of acting on another drawing.
- Android canvas activation now retries on the independent window or
  canvas root becoming visible and when the window is activated. A
  deferred activation that ran while the host was hidden remains safe
  for OpenGL and can recover on foregrounding instead of leaving the
  active Krita view uninitialized. Qt regression exercises hide/show
  and deleted-root safety; device GL validation remains pending.
- Native view activation now uses one deferred event for the first
  canvas and one per later document switch. The
  `KisCanvas2::bootstrapFinished` call is single-shot per view,
  mirroring Krita's desktop GUI-load event rather than running
  again on tab switching. Tool state is refreshed on selection.
- Corrected a first-canvas crash in upstream
  `KisViewManager::showHideScrollbars()`: `setCurrentView()`
  unconditionally read the desktop-only
  `view_show_canvas_only` action, which does not exist in the
  standalone action collection. Android now defaults that flag to
  false when the action is absent; the existing desktop action's
  behavior remains unchanged.
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
| Replacement host | Launch/paint/save via a lightweight host using Krita engines and MDI-free views | **Independent host verified on Android for paint, Undo/Redo and save/reopen; optional startup and panels need device validation; old desktop bootstrap remains built** |
| APK | Build deployable Android package; no regressions on target phone | WSL incremental builds and tests operational; initial independent-engine device smoke successful; broader device signoff pending; automatic upload paused |

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


## First Android device result — 2026-11-09

The first live launch of **Standalone Engine Test (experimental)**
terminated the Android app before the test canvas appeared. Relaunch
returned to desktop Krita. This is **not** a passing painting gate and
does not establish a particular native crash location.

The next attempt embeds the distinct Krita engine host as a **child
widget of the existing Android top-level window**. The older pilot had
created a second Qt top-level window, which Android Krita does not
normally support. The original Krita canvas is left in place. The
test host has an explicit Exit test action and follows host resize.
Automated Qt tests cover top-level identity and canvas preservation;
device rendering remains unverified.


## Standalone crash checkpoint — 2026-11-09

An Android device test terminated during the `Creating independent
Krita view manager` phase, before native document creation. Source
inspection of Krita 6.0.4 found a specific null dereference during
`KisViewManager` construction:
`KisStatusBar::setup()` dereferences
`m_viewManager->mainWindow()->statusBar()`, but this independent
`QMainWindow` is intentionally not a desktop `KisMainWindow`.

The Android-only patch uses `qtMainWindow()->statusBar()` in the
independent host and retains upstream desktop behavior. The desktop-only
`themeChanged` connection is guarded. This addresses a concrete
crash path; another Android launch is still required to validate it.


## Native host startup migration (opt-in)

The device-validated separate Krita host now has an optional first
launch route (`MobileStudio/nativeStartupV1`, off by default). Krita
still initializes its resource and plugin registries, but the user can
launch directly into an independent MDI-free view and document host
rather than the legacy overlay. Settings, mobile More, and the
independent host itself expose opt-in controls. Existing Mobile Studio
autostart is suppressed when this mode is enabled. An interrupted
or failed native launch automatically disables the mode and restores
the standard Krita fallback on the next launch. The old desktop host
is still compiled and retained for recovery; this is not yet a binary
removal of the full KisMainWindow implementation.


## Native engine workspace controls (experimental, opt-in)

The independent Qt host can now construct the existing Mobile Studio
brush, color, layer and tool interfaces directly against its own
`MobileEngineBridge`, `KisViewManager`, and document host ports.
The controls are initialized lazily, after at least one native
document is available; there is no second Krita document model,
desktop docker borrowing, or shadow brush implementation.
The engine-test toolbar exposes Brushes / Layers / Color / Tools.
The controls must pass device interaction and portrait/landscape
tests before they replace the previous Mobile Studio UI.


The independent host's New button now invokes the existing native
configurable canvas dialog (size, DPI, background) instead of always
creating a fixed 512x512 test document. A Canvases button reuses the
mobile document switcher directly with the new engine host's live
view identities for activation and guarded Close Current. File Open,
Save, Save As and KRA writing continue to use Krita's existing
import/export stack.


## Confirmed native controls and UI exposure (2026-11-09)

On-device, the user confirmed independent startup and native
Brushes, Layers, Color and Tools controls are functional when
opened through the Qt toolbar overflow. The next iteration
keeps the *same* QAction callbacks and Krita services but
reorganizes them into a compact document row at the top
(Canvases, Undo, Redo, Save, More) and a dedicated bottom
workspace row (Brushes, Layers, Color, Tools). New/Open,
previous/next, Save As, Close, startup preference and Exit
remain available through the explicit More menu rather than
an undiscoverable chevron. Qt tests guard action routing,
canvas ownership and dual-toolbar visibility; actual compact
device interaction still requires verification.


## Qt-only native startup coordinator extracted

The delayed independent-engine launch previously lived directly inside
KisMainWindow and captured a KisMainWindow pointer, while checking its
desktop central view and starting a retry timer. That scheduling has
been moved into `native_boot_coordinator.h`, a Qt-only QMainWindow
component with no Krita desktop/MDI dependencies. It consumes the
precomputed crash-safe startup decision and invokes the existing native
engine pilot through an injected callback; it handles readiness,
bounded retries, host deletion and failure recovery. Qt regression
verifies successful launch and unavailable/hidden host fallbacks.

Krita still builds one KisMainWindow as an Android native window,
resource/bootstrap provider and desktop recovery route. The native
painting host itself does not adopt a desktop MDI canvas.


## Native-shell bootstrap reduction after 7b254de device acceptance

Device testing of 7b254de passed independent startup, pen drawing,
Undo/Redo, Brushes/Layers/Color/Tools, custom multi-canvases, KRA
save/reopen, unsaved close cancel, background/resume and restoration
of full desktop Krita after opt-out. A visually unexpected desktop UI
remains unclassified pending screenshots; do not remove or reparent
those widgets by inference.

The native-startup decision now runs at the start of the main-window
constructor, before allocating its view manager or desktop workspace
menu model. On opted-in native startup, unused desktop-only workspace
menu tracking, style chooser actions and help menu widget are skipped,
along with the previously removed docker factories and legacy
Mobile Studio controller. Krita's painting engine, resource/plugin
registration, main Android top-level widget, native view manager,
and document services remain intact. On the next launch after a
crash or explicit opt-out, the full desktop shell is restored.


## Native Qt status footer suppression (2026-11-09)

Device tests of f6323df passed drawing, documents, lifecycle and fallback,
but the independent UI still showed a Krita-style rotation/zoom/status
footer. Source tracing confirmed KisStatusBar::setup() attaches native
Qt QStatusBar widgets to the independent QMainWindow. The new host now
hides this native footer after engine initialization and first view
attachment, preserving those engine-owned controls in memory. It hides
the underlying original desktop status bar during embedding and restores
its previous visibility on Exit. Automated Qt tests cover the footer,
widget ownership, canvas preservation, and fallback restoration.
Device testing is still required to confirm the footer stays hidden.


## Passive desktop MDI on independent boot

Independent Android startup now leaves the retained desktop QMdiArea
unconfigured for tab UI, skips the welcome QScrollArea and desktop
MDI window layout restoration, and does not rebuild Krita's desktop
workspace/document menu on each native document-open notification.
The independent KisViews still use their own native document stack.
The original QMdiArea and welcome widget remain allocated for safety;
this is not yet a separate executable without KisMainWindow.
The independent child host also disables the underlying desktop
hide/show refresh workaround while painting, then clears that guard
when exiting. Full desktop setup remains available on the next
opt-out or failed-startup recovery launch. Device verification pending.


## Device regressions after f12a6ff (reported 2026-11-09)

The user confirmed the native status footer is gone and painting still
works, but reordering layers crashes Android and tapping Color no
longer displays its wheel. This repair routes single-layer Up/Down and
context-menu moves through Krita's indexed undoable
`KisNodeManager::moveNodeAt`, after verifying the current image,
node parent, editable parent, and destination index. The compressed
asynchronous multi-node reorder followed by an immediate tree reload
has been removed from these controls; a short pending guard prevents
the panel's polling from traversing the tree during a move.
Multi-selected reorders are temporarily rejected rather than risking
an unsafe operation.

The color wheel is now a raised child of the live
`MobileNativeViewStack` canvas root, rather than a floating child of
the outer QMainWindow that can be covered by its central layout.
Persisted diagnostic checkpoints distinguish missing document/canvas
from a color widget that fails to become visible. These are
suspected causes, not a device-proven fix. Native compilation and
device testing are required.


## Single-layer reordering fix after 665c56b device feedback

The user confirmed the Color wheel is restored and downward
reordering works, but upward reordering does nothing, and changes
do not appear in Undo. Krita's actual KisNodeFacade::moveNode
(quint32 index) resolves the insertion anchor as `at(index-1)`
*before removing the moving node*, and immediately rejects
`aboveThis == node`. That explains the failed one-slot upward
move, which previously selected itself.

The new native controls use explicit bottom-to-top sibling anchors
with `KisImageLayerMoveCommand` and a
`KisProcessingApplicator` processing stroke, rather than
`KisNodeManager::moveNodeAt` / legacy synchronous undo adapter.
The stroke's completion future gates panel refresh and subsequent
reorders, protecting image tree consistency. Both upward/downward
plans and simulated reverse moves are covered by Qt tests, though
actual Android history integration requires on-device verification.
The layer menu and Up/Down buttons share the same command path.
Color wheel code is unchanged. Multi-selected reorder remains
unsupported until a safe grouped undo transaction is validated.


The native layer Rename action is also routed through the same
`KisProcessingApplicator` undoable document stroke; its former
`KisNodeManager::setNodeName` legacy direct-command path was removed.
The panel now awaits completion of the rename or reorder stroke
before reconstructing the node tree. Add/delete and visibility/lock
remain on Krita's existing mechanisms and should be validated
separately if their undo history is also affected.


## Deferred desktop command palette (native boot)

The desktop KateCommandBar was previously always constructed in
KisMainWindow::Private, even when the Qt-only native command controls
are selected. Opt-in native startup now skips this widget and creates
it lazily in KisMainWindow::openCommandBar() if the user explicitly
opens Krita's old desktop command palette. Fallback/desktop startup
remains eager as before. This does not change the native action
registry, painting, the layer undo stack, or document persistence.


## Android desktop-widget audit after native canvas activation

The independent pilot now persists a short Qt-only inventory of
legacy UI objects actually allocated after the first native canvas
becomes usable: direct desktop QDockWidgets, remaining QMdiAreas,
Kate command palettes, native host activation and visible desktop
status chrome. Settings > Last Standalone Engine Test Status shows
this audit to diagnose hidden desktop components vs ones no longer
constructed. This audit does not remove any canvas, object, or
command; it runs only after the independent document successfully
attaches. Desktop commands (Kate) are also lazily constructed only
on-demand in the normal Krita fallback.
