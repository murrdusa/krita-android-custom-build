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
| Navigation | Back/reentry without deleting/reparenting a document; portrait/landscape, suspend/resume | Stationary Qt docks are opt-in; Qt synthetic tests added; Android device regression pending |
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
2. Extend the completed view-host separation to canvas-session
   ownership, including interruption, invalidated views, and mode
   switching. Run fake-port Qt tests and check Android build results.
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
