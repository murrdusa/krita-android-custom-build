# Mobile Studio product scope and agreed priorities

Decision recorded: 2026-11-09. **Planning only; do not implement these additions merely because this file changed.** Read alongside [AGENTS.md](AGENTS.md) and [DEVELOPMENT_STATE.md](DEVELOPMENT_STATE.md). Preserve the working standalone Krita engine and application.

## Product principle

- Preserve all **Krita drawing functionality that Infinite Painter offers**, preferably by adapting Krita's existing painting, document, tool, and layer APIs. Do not remove useful extra Krita capabilities solely because Infinite Painter lacks them.
- Prioritize actual useful functionality over interface polish now; later design a compact, efficient UI for finger and stylus on the Galaxy Z Fold.
- Avoid rewriting brush engines, the KRA format, layer rendering, animation engines or color management. The engine should remain a functional superset wherever practical.

## Three Infinite Painter-related requests that materially matter

1. **Project version history — highest priority of the three; a core document-safety requirement.**
   - Automatically checkpoint the **latest unsaved changes** when the app is backgrounded/minimized, ordinarily closed, or terminated without an explicit **Close → Don't Save** decision. The previous saved states must remain accessible as older versions, not overwritten.
   - On reopening, prefer restoring the latest state automatically with an understandable version-history browser; **avoid Krita's startup popup asking which autosave/recovery version to keep**.
   - An explicit **Close → Don't Save** intentionally discards the current unsaved edits for that document. It must *not* secretly create a new version of those discarded edits; previously stored history can remain.
   - Android may kill a backgrounded process without a close callback or adequate time to save. Therefore a reliable implementation needs **dirty-state/periodic or debounced checkpoints in addition to background/lifecycle notifications**. Never promise guaranteed capture at process death from an exit handler alone.
   - Preserve original project files, use recoverable/atomic snapshot writes, avoid duplicates when unchanged, and retain a bounded/manageable history (time/space retention and cleanup). Support unsaved new canvases as well as existing KRA files. Design checkpointing to avoid visible drawing latency.
   - This is **new functionality, not simply Krita's existing autosave/backup UI**. Build an underlying snapshot/recovery service before polishing the final version-browser presentation.
2. **Interactive fill/selection tolerance — strong priority.** Make tolerance adjustable directly by a finger/pen drag during the fill/selection workflow. Reuse Krita's existing fill/contiguous selection engines where possible; most new work is the gesture/control interaction and live preview.
3. **Convenient dedicated perspective modes — third priority.** Provide a mobile-accessible workflow for 1-, 2-, and 3-point perspectives and useful isometric/curvilinear aids. Reuse Krita's existing assistant geometry rather than reimplementing perspective projection if feasible.

Do **not** implement those three immediately; they are agreed future priorities. Of other Infinite Painter-only conveniences, automatic hold-to-correct shapes, hatching guide gestures, seamless pattern project workflow and timelapse recording/export may be valuable, but are **not requirements** and should not displace the three requests. Timelapse is optional/low priority; do not conflate it with version history.

## Krita features to retain

- All useful brush engines and presets (including specialized paint engines), brush customization and stylus pressure.
- Native layer types (paint, group, vector, fill, filter, clone, file, transform masks, etc.), masks, clipping, groups, blending and undo.
- Non-destructive transforms and robust selection/transform tools.
- Krita animation support should remain available eventually, **lower priority** than core drawing; not necessarily exposed in the first complete UI.
- Krita color/profile support is sufficient as is. Preserve it without major new color feature work.
- Krita drawing/perspective assistants and guides are sufficient as an engine baseline; improve access when building mobile perspective modes.

## Agreed development order

1. **Selection and transform functionality:** lasso, polygon/contiguous/color selection, move/scale/rotate, perspective, warp/liquify; first expose existing engine actions and tools correctly for pen/finger.
2. **Layer functionality:** groups, clipping/masks, opacity/blending, move/reorder, duplicate/merge, multi-selection and transformations, undoable when applicable.
3. **General Krita drawing tools:** fill, gradients, shapes, assistants, symmetry, stabilization, cloning and brush options. Adapt existing engine controls, not new renderer code.
4. **Automatic project checkpoints and version history (high user priority).** Include background/minimize/exit handling and crash recovery as specified above. Timelapse is optional and low priority despite appearing in an earlier comparison list.
5. **Mobile interface design/refinement** once the functionality foundation is complete; optimize for efficient Fold touchscreen/pen workflows.
6. **Additional Infinite Painter-style conveniences**, **after the interface** where practical: interactive fill tolerance and dedicated perspective modes first, then optional shape recognition, hatching, seamless pattern, etc. This category does need new mobile interaction code; some subfeatures reuse existing Krita engines.

The ordering is a roadmap, not an instruction to delay a small dependency that is required for safe document work. **User explicitly wanted the prior feature list items 4 and 5 swapped, and Infinite Painter convenience work deferred until after interface design.**

## Scope discipline

The user does **not** want any of these newly listed enhancements coded immediately. Continue the existing core-functionality development path. Use a future request or a logical milestone to schedule the specific work; do not interrupt the ongoing architecture-to-functionality transition for speculative extras.
