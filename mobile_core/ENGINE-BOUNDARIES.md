# Krita Core / Mobile UI redesign

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
