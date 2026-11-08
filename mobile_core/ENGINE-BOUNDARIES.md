# Krita Core / Mobile UI redesign

## Milestone v1.14 — unparenting audit and native-canvas protection

- Qt 5's `QMainWindow::takeCentralWidget()` calls
  `oldcentralwidget->setParent(nullptr)`. Thus the current embedded-host
  transition still briefly changes the canvas into a top-level widget on
  **entry**, even though the final mobile and desktop presentations share
  Krita's Android native window. This remains a graphics-context risk.
- The return/Back path now detaches the painting widget directly from
  Mobile Studio to the existing Krita parent, *before* taking the empty
  mobile presentation out of the central slot. The Qt smoke test records
  parent-change events and asserts Back does not temporarily unparent
  the real painting canvas.
- Inconsistent detach results now attempt to restore the exact original
  canvas into Mobile Studio, and the test injects that failure then
  retries safely. The controller uses a guarded QPointer to track a
  mobile presentation replaced by Qt.
- **Next architectural replacement:** leave the native Krita widgetStack
  permanently in the same parent, and switch the mobile chrome around it
  using ordinary child/dock/overlay widgets. That will remove the entry
  transition and the need to swap `QMainWindow::centralWidget()` during
  mode toggles. Keep the current Krita engine, document view, undo stack
  and stylus rendering; only mobile controls should change.
- Do not claim graphics-context stability from source compilation or
  offscreen QWidget tests. Validate actual Android EGL drawing, repeated
  Back/entry, and Save/Discard/Cancel before removing the current path.

## Milestone v1.12 — one Android top-level for Back/return stability

- Mobile Studio's UI remains a separate, testable Qt presentation component,
  but it is now embedded as Krita's central widget **inside the existing
  Android top-level window**. It no longer shows an independent full-screen
  Android window above Krita. This avoids the previously observed stale
  Mobile Studio footer underneath the desktop window on Back.
- `MobileCanvasTransfer` snapshots and hides Krita's legacy menu, status,
  toolbars and dockers while the embedded Mobile Studio presentation is
  active, then restores the original widget visibility on exit.
- The native Krita canvas is detached directly to the original window as
  a child before the embedded host is removed, avoiding an intermediate
  top-level graphics surface on each return. The controller schedules
  destruction of the empty mobile presentation host after Back; the canvas
  is never deleted or reconstructed.
- The old standalone canvas-window toggle is intentionally removed from
  the Android default path. Krita's engine still owns document I/O,
  brushes, editing history, animation, and paint/render services.
- A successful Android compilation does not establish graphics-context
  stability until repeated Back/enter and stylus tests pass on-device.
- This is a stabilization step rather than a size reduction: desktop
  runtime dependencies still exist and should not be stripped until
  standalone document lifecycle operations are fully reliable.


## Milestone v1.8 — abstract canvas surface interface

- `canvas_surface.h` introduces `MobileCanvasSurface`, a Qt-only
  contract that attaches, verifies and detaches **the same native
  QWidget canvas** without constructing any Krita canvas, document or
  brush objects.
- `MobileStandaloneWindow` implements the interface while keeping its
  existing canvas-slot, refresh and UI responsibilities unchanged.
- `MobileCanvasTransfer` now accepts the abstract surface rather than
  `MobileStandaloneWindow`; it no longer includes the UI host or
  engine facade. Its widget handoff has explicit guards for an absent
  desktop canvas, an occupied destination and incomplete attachment.
  Recovery reattaches the original QWidget to Krita's main Qt window.
- `canvas_session.h` is the only controller that knows the concrete
  mobile UI host and supplies it to the Qt-only transfer layer. The
  source patcher enforces both this separation and the existing ban
  on Krita desktop-window includes in the mobile UI.
- Krita still boots the underlying `KisMainWindow` and owns file I/O,
  native painting, color, image processing and document lifecycle.
  These changes reduce source coupling but **do not yet eliminate**
  the desktop application bootstrap.
- APK publishing remains explicitly paused.

## Milestone v1.7 — generic Qt canvas host boundary

- `canvas_transfer.h` owns physical transfer of the **same** native
  Krita QWidget stack between the backend Qt `QMainWindow` and
  `MobileStandaloneWindow`. It also isolates native MDI tab-strip
  hide/restore behavior, prevents the Android desktop-window refresh
  from reappearing over the mobile canvas, and guards failed transfers.
- `canvas_session.h` is now just mode switching and opt-in startup.
  Its constructor accepts Qt `QMainWindow`, Qt `QMdiArea`, and a
  preconstructed `MobileEngineBridge`. It no longer includes or
  constructs Krita-specific `KisMainWindow` or `KisViewManager`
  objects. The source patcher builds the bridge at the Krita startup
  integration point.
- The Android patcher rejects concrete desktop window includes from
  both the mobile UI and the Qt canvas/session classes. Only
  `document_service.h` and `command_service.h` retain direct
  Krita-specific main-window dependencies.
- This is a reduction of **source coupling**, not a runtime removal of
  Krita's desktop `KisMainWindow`. Krita's core still initializes
  and owns the paint device, document lifetime, native tools and saves.
  A future engine-only startup can replace the remaining two services
  and the Qt host without modifying the mobile UI.
- APK uploads remain disabled by the user's explicit preference.

## Milestone v1.6 — document and command services

The mobile UI has moved closer to being independent of Krita's desktop
window **without replacing the Krita engine**.

- `document_service.h` is now the single gateway for creating and
  opening native Krita documents, safe saving, Save As, listing/switching
  native document views and closing with Krita's built-in unsaved
  changes checks. This replaces direct document lifecycle calls in
  `engine_bridge.h`, while preserving the same Krita APIs.
- `command_service.h` is the single gateway to native Krita
  `QAction`/menus, brush-preset selection, eraser mode and
  `KoToolManager` tools. It holds the desktop command-collection
  dependency behind a replaceable interface.
- `engine_bridge.h` delegates document and command operations to
  these two services and retains Krita's resource/node adapter.
  Mobile widgets continue using the same signatures; no new paint,
  compositing, undo, I/O or tool implementations were introduced.
- `canvas_session.h` alone handles physical widget transfer and
  restoring the desktop fallback. The source patcher now verifies
  that mobile UI headers and the facade do not directly include the
  desktop `KisMainWindow` type.
- **Limit:** This is dependency isolation, not an independent app
  bootstrap. Krita still constructs `KisMainWindow` at startup and
  the three backend adapters still call desktop-owned APIs.
  The following architectural milestone is to replace those adapters
  with Krita service-owned startup/view instances after more runtime
  validation. No APK publishing was requested.

## Milestone v1.5 — responsive overlay ownership

- `MobileColorWorkspace` now owns size presets, drag positioning,
  double-tap reset and clamped bottom-right positioning. Placement,
  preferred dimensions and expansion state persist as UI settings.
  Portrait and folded viewport constraints may temporarily compact the
  wheel without overwriting the user's chosen size/expanded preference.
- The independent host's layers panel becomes a bottom drawer when the
  painting area is narrower than 640 px, and a regular right dock on
  wider displays. The exact same Krita-backed layer widget is moved
  between presentation modes; no duplicate layer models or undo state.
- Color control geometry reserves space above the bottom layer drawer,
  automatically minimizing when little room remains, so it does not
  steal pointer input meant for layer rows.
- Only the independent mobile window controls geometry. Krita remains
  the authoritative owner of canvas input, color resource, layer nodes,
  compositing and document lifetime. APK publishing remains paused.

## Milestone v1.4 — foreground colors as a Krita service

- `color_workspace.h` provides a compact movable-size-independent
  overlay in the bottom-right corner of Mobile Studio's actual canvas
  viewport, rather than a new docker or a separate painting surface.
- The hue ring and saturation/value square are UI input only. All color
  changes are forwarded to Krita's existing
  `KisCanvasResourceProvider::setFGColor(KoColor(...))`, preserving the
  current document color space and Krita color management. A native
  advanced picker remains available for precise values.
- The last eight selected display colors are stored as UI preferences
  in QSettings. They are shortcuts to foreground color choices, not
  changes to document pixels. Minimize/expand state is also a setting;
  the collapsed swatch restores canvas space.
- `brush_workspace.h` now has a collapsible size/opacity detail row,
  retaining direct Paint/Erase/Blend preset buttons. Uninitialized Paint
  modes no longer inherit a current eraser or blender preset.
- The Krita engine, resource/model life cycles, file I/O and undo stacks
  remain authoritative. No additional color or brush engine is created.
- **APK publication remains paused** under `UPLOAD_REQUEST_ID=none`.

## Milestone v1.3 — mobile gesture and layer interaction

- `touch_reorder_list.h` distinguishes scroll gestures (movement before
  a press-and-hold threshold) from native Qt item drag/drop (movement after
  a 360 ms hold). It delegates actual model moves and edge autoscrolling
  to the Qt item view; it never touches Krita's document engine.
- The Quick Actions strip has a non-resizable inner content widget to
  preserve a real horizontal scroll range for overflow on narrow screens.
- The layer panel makes Eye/Lock direct row targets and exposes native
  layer options on a second tap of the active layer. Commands use
  `KisNodeManager`, keeping Krita's undo and document ownership.
- Source and WSL builds continue; APK publishing remains paused by
  `UPLOAD_REQUEST_ID=none`.

## Milestone v1.2 — native menu hierarchy

- `MobileEngineBridge::menuRootActions()` exposes Krita's live top-level
  QActions without leaking the hidden main-window pointer into mobile UI.
- `MobileCommandBrowser` now renders the exact nested menu tree with
  breadcrumb navigation. Typing in its search box searches **across all
  menus** and includes full action paths. Registered plugin commands
  absent from the desktop hierarchy remain under Other Krita commands.
- Actions retain native QAction pointers and are triggered after closing
  the browser; the mobile interface does not duplicate the handler
  implementation. Disabled commands stay disabled. Existing native
  desktop menus remain as a compatibility fallback.
- **APK publishing remains paused**: normal source commits and WSL
  compiler checks do not change `UPLOAD_REQUEST_ID=none`.

## Milestone v1.1 — configurable Quick Actions (UI owns chrome)

- `quick_actions.h` replaces the fixed Krita-like footer with a single
  horizontally scrollable bar, keeping an explicit **+ Add** and **Edit**
  control even if all shortcuts are removed.
- Native actions are discovered live via
  `MobileEngineBridge::availableActions()`; native tools via
  `MobileEngineBridge::tools()`. The UI stores only stable action/tool IDs
  in `QSettings`, not QAction pointers or document state. It updates
  enabled state against Krita's real action collection.
- User-selected mobile actions (New/Open/Save/Undo/Redo/Layers/Color/Tools,
  Canvases, and More) invoke existing mobile UI workflows which in turn
  call Krita's native engine and document services.
- The chooser offers frequent actions, all available commands/tools,
  a dedicated tool list, and free-text search; the editor provides drag
  reordering for pointer/stylus plus explicit Up/Down buttons, removal,
  Cancel/Save semantics, and a reset-to-defaults path.
- This milestone is **UI modernization**, not a second command engine or
  a reduction of Krita's binary dependencies. The same native image,
  selection, brush and document engines continue to run.

## Milestone v1.0 — mobile tools and canvas chrome isolation

- `MobileEngineBridge::tools()/selectTool()` enumerates the live
  `KoToolManager::toolActionList()` and activates real registered tool
  actions, retaining Krita's pen input manager, vector/raster selection,
  transform implementations and undo model.
- `tool_browser.h` is a searchable, touch-sized native Qt mobile tool
  chooser with recently used tools. It doesn't construct brushes or
  duplicate drawing tools. The old desktop toolbox remains registered
  only for legacy compatibility until the application bootstrap is
  replaced.
- `canvas_session.h` is now responsible for the visual tab chrome owned
  by Krita's `QMdiArea`. While the canvas widget belongs to Mobile
  Studio, its Qt `QTabBar` is hidden and compressed; each bar's original
  minimum/maximum height and visibility are restored on returning to
  the desktop host. A lightweight timer handles QTabBars created after
  a new or opened document. The mobile document switcher remains
  the independent visible navigation.
- We still **do not** claim to have a standalone application executable:
  Krita's `KisMainWindow` remains alive for initialization and for
  engine services. There is no unsafe removal of document lifecycle,
  brush/input, save or color-management code.

## Milestone v0.9 — safe document close and mobile empty canvas

- Document close now goes through
  `MobileEngineBridge::closeDocument/closeActiveDocument`. It activates
  the target native Krita MDI view, then calls its `QMdiSubWindow::close()`.
  Krita's `KisView::closeEvent` and `queryClose()` remain responsible for
  unsaved-change prompts, save completion, autosave cleanup, and closing.
  The mobile interface does not delete documents or bypass save checks.
- The canvas switcher gains a clearly labeled **Close current canvas**
  action; **More** exposes the same native operation.
- A mobile-owned empty canvas panel lives beside the live canvas in a
  stacked layout. With no document, it hides Krita's desktop welcome
  page and offers New and Open. Opening a document reveals the native
  Krita canvas again; leaving Mobile Studio still returns the entire
  untouched widget stack to Krita.

The hidden desktop main window is still an engine lifecycle dependency,
not a finished independent executable. On-device tests must establish
that closing the last view and restoring a new view work before any
desktop-widget removal.

## Milestone v0.7 — canvas-session boundary and document navigation

- `canvas_session.h` owns widget-stack transfer between the Krita backend
  window and Mobile Studio, plus the opt-in startup transition. There is
  now one replaceable integration point for eventually eliminating the
  original desktop window. The independent `standalone_host.h` no longer
  needs a direct MDI pointer or desktop window reference.
- `document_switcher.h` presents a mobile-native view of the documents
  exposed by `MobileEngineBridge::openDocuments()`; switching dispatches
  `QMdiArea::setActiveSubWindow` inside the bridge. Krita continues to
  own the actual document objects, views, undo stacks and dirty markers.
- `More → Start in Mobile Studio on next launch` is an optional, OFF-by-
  default preference. A pending-start marker disables the preference
  after an early startup failure to avoid a repeated crash loop. The
  fallback/legacy UI remains available through Back.

**Limit:** Startup still constructs `KisMainWindow` to register Krita
actions, tools, resources and load completion. v0.7 moves references to
that code into `engine_bridge.h` and `canvas_session.h`, rather than
claiming to remove it. Package size and runtime engine dependencies are
unchanged. A future executable/bootstrap migration must retain autosave,
color management, display canvas and Android storage semantics.

## Milestone v0.6 — document workflows

Mobile Studio now owns the New Canvas dialog (presets, dimension and DPI
selection, transparent or white background). `MobileDocumentWorkflow` is
a UI controller, and `MobileEngineBridge` is the only mobile code that
constructs or opens Krita documents using `KisPart`, `KisDocument` and
`KisMainWindow`. Saving and saving-as continue through Krita's existing
safe-save implementation; no custom serializer, no bypass of render locks,
KRA backup policy or format conversion.

The Open chooser belongs to the mobile shell but uses Krita's existing
`KoFileDialog` provider and native document loader, preserving Android
storage access. The old desktop main window still owns loading completion,
active canvas view and delayed-save handling; **this milestone does not
yet eliminate that backend**. The newly introduced document service gives
us a single place to replace that backend in future milestones without
rewriting the mobile UI.

## Milestone v0.5 — interface/service boundary

The `MobileEngineBridge` is the only mobile module that accesses the hidden
Krita main-window action collection directly. Brush and layer UIs now accept
an engine service adapter instead of a `KisMainWindow*`. Tool/preset selection,
eraser state, native command execution and action discovery use that bridge.

The new **Search all Krita commands** interface is built from native QAction
objects and searches their live labels and identifiers. It uses Krita's own
handlers without mirroring the desktop menu hierarchy. The old menus are
retained behind a clearly labeled compatibility item until all necessary
commands have mobile equivalents.

**This is dependency isolation, not dependency removal**: Krita still starts
with a `KisMainWindow`, and that window still owns the document lifecycle and
canvas before we transfer them. We have not reduced the APK size or created a
separate executable. Next is to migrate document bootstrap and file action
ownership behind independent services, then remove unused desktop chrome
and packaging only after runtime verification.

## Engineering state — independent window v0.1 (November 2026)

The current test milestone is no longer a painter toolbar over Krita's
existing window. **Mobile Studio owns a separate top-level window, its
navigation, panels and layout**, and temporarily hosts the **actual** Krita
widgetStack containing the live MDI canvas. It does not reimplement painting,
pressure delivery, saving, undo or pixel formats.

Code boundaries in this milestone:

- `mobile_core/engine_bridge.h`: one-way access to native image, node, tool
  and action APIs. No new paint engine.
- `mobile_core/standalone_host.h`: own top-level application window and
  reversible canvas-widget transfer; the original Krita window is hidden
  as the backend service.
- `mobile_core/brush_workspace.h`: independent brush preset discovery,
  size, opacity, and Paint/Erase/Blend controls, using Krita's registry.
- `apply_mobile_shell.py`: an integration shim into the existing Android
  executable. This shim and the still-created desktop host are **temporary**.
- `mobile_core/TEST-PLAN.md`: required on-device checks before replacing
  the desktop startup path or deleting legacy modules.

This is **not yet a fully standalone binary or a smaller packaged app**.
The next gate is to validate canvas transfer and file dialogs on actual
Android, then separate engine bootstrap/document management from desktop
`KisMainWindow`. Delete old interface code only after native replacements
survive the required tests.


## Objective

A mobile-first drawing application **powered by Krita's native engine**, not an imitation
painting engine and not another set of desktop-docker overlays. This work is experimental.
Keep the proven Android build on `main` functional and installable throughout migration.

The initial "mobile pilot" is deliberately a small Qt Widgets shell. It embeds/reuses the
live Krita canvas and routes commands into native Krita, so pressure-sensitive painting,
image compositing, selections, document saving and undo remain engine-owned. This is
an integration proof, not the final UI framework. Qt Quick/QML is a candidate for the
production front end after confirming Android toolchain support, canvas embedding,
tablet events and accessibility; avoid assuming it is already linked into the APK.

## Boundaries

| Engine-owned: retain | UI-owned: redesign |
| --- | --- |
| paint operations, presets, stylus pressure | brush/erase/blend workspace controls |
| Krita image / pixel formats / projection | touch/pen navigation and responsive layout |
| layer tree, masks, alpha inheritance | visible layer tree, clip interactions, multi-select |
| pixel selections and transform strokes | selection and transform user workflow |
| color conversion / gamut handling | color wheel/history/swatches |
| native undo commands | undo/redo affordances and feedback |
| KRA and other format import/export | project gallery and file workflow |
| all existing action handlers | menus, search, Quick Actions and keyboard shortcuts |

**Never directly manipulate paint devices for a visual shortcut.** UI sends a
command through an adapter that owns the appropriate native action, tool or
node manager call. Reuse Krita's undo transaction APIs where mutations occur.

## Architecture

```
mobile_core/
  mobile_shell.h       Small opt-in in-process canvas integration proof
  ENGINE-BOUNDARIES.md This specification and incremental migration plan
apply_mobile_shell.py Copies the pilot into an Android Krita source checkout
                      and adds a native toolbar toggle

Krita 5.3.4 (KDE source v6.0.4)
  KisMainWindow
  KisViewManager / KisCanvas2 / paint operations
  KisNodeManager / KisSelectionManager / Transform tools
  KisDocument / native action collection
```

The pilot reuses the real QMdiArea canvas, *not* a texture screenshot, new
drawing surface or alternative brush system. It must not call canvas painting
APIs from the UI layer. QWidget pilot controls remain outside painting input
so Android pen/finger events still reach Krita.

## Migration gates

1. **Core proof:** enable and exit pilot without a crash; open/save KRA via native
   actions; stylus pressure and drawing remain correct; undo/redo work; canvas is
   not reparented or replaced.
2. **Document lifecycle:** safe new/open/close/recovery; unsaved confirmation;
   canvases and tabs survive UI changes without state loss.
3. **Tools:** paint/eraser/blend preset memories and responsive controls using
   native paint-op registry; no duplicate paint engine.
4. **Layers:** model-backed thumbnail tree, masks/clips/groups, native selectedNodes,
   pen/finger touch handling, drag-reorder and undo; do not copy old gesture hacks.
5. **Selection/transform:** preserve selection masks; native transform stroke
   lifecycle; multi-layer targeting must not discard in-progress work.
6. **Color:** native color spaces/conversion; correct wheel geometry; history.
7. **Gallery/settings/quick commands:** pluggable modules behind a searchable
   command registry. Never fork an independently maintained list of Krita commands.
8. **Debloat audit:** remove/disable unneeded shell features **after** dependency
   tracing and functional coverage. Do not blindly drop plugins, KRA format
   handlers, color-management libraries, or brush presets.

Each gate requires a successful **local WSL Android compile**, followed by
on-device testing. GitHub Actions patch validation is not a compile. Local
`local/android-wsl-build` statuses and Issue #1 provide the compiler results.

## Pilot constraints

- Off by default, opt-in and reversible, no project migration.
- It must use the user's existing paint engine and real canvas.
- It may initially use a plain native color picker, layer selection list and
  standard Krita menus as integration placeholders.
- It must not silently toggle persistent compact-mode settings.
- It must not alter Krita's normal UI when off.
- Do not publish or replace the existing APK automatically.
- Do not treat a successful build as touch or stylus verification.

## License

Krita-derived code remains subject to Krita's applicable GPL and third-party
licenses. Preserve notices and publish corresponding source for distributions.
