# Krita Core / Mobile UI redesign

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
