# Development instructions — Krita Mobile Studio

**Read this file at the start of every development session, including a new ChatGPT chat.** Then read [DEVELOPMENT_STATE.md](DEVELOPMENT_STATE.md), the latest GitHub commits, and [local build Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). These are the project handoff, not earlier chat transcripts. Before changing code, check the latest branch state.

## Highest priority: time and iteration speed

- **Be autonomous.** Take reasonable engineering decisions and implement as much as possible without requesting user input or approval. Ask only when essential information, device access, credentials, or a consequential decision requires the user. Don't wait idly for a compile: work on the next independent, relevant task while the PC builds.
- **Use the user's Windows/WSL2 PC for all normal Android compilation and testing.** It is faster than GitHub Actions. Do not trigger GitHub-hosted builds, preflights, or test workflows when the PC watcher is available. Keep cloud Actions manual-only for situations where the PC is genuinely unavailable.
- **Use the existing GitHub <-> PC workflow.** A code commit to `main` is picked up by the existing local WSL build watcher. The watcher runs the incremental Android build and the companion reporter publishes its result under commit status `local/android-wsl-build` and [Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). Read the **reported revision SHA**, not just the word SUCCESS; results for older commits do not validate newer commits. If a new commit is building, check again when useful, and continue other work meanwhile.
- No manual `git pull` or `local-build.sh build` instruction to the user when the watcher is running. The watcher script itself is installed locally and may not be tracked here. Do not pretend to execute commands on the PC: communicate via GitHub commits and read its GitHub results. If the watcher isn't responding, diagnose from available GitHub evidence before asking for intervention.
- **Batch related changes** into useful compilable increments. Avoid repeated source-only check cycles, long speculative testing plans, reimplementing existing Krita engine behavior, gratuitous abstractions, and large tool-call/commentary overhead. Fix actionable compiler errors from local logs first.
- **Avoid layered crash fallback and redundant safeguards** for this low-stakes early prototype. Retain enough Qt/logcat logging, clear compiler diagnostics and a usable failure report to fix faults. Don't create elaborate user-facing logging menus. Do not compromise essential document/save correctness or silently lose user content.
- **Do not polish or redesign the UI now.** First finish independent startup/application architecture, then fill in core functionality; design the final interface when functional foundations stop moving. Preserve currently working controls rather than rewriting them for aesthetics.
- **Do not upload/release APKs** unless the user explicitly asks. `APK-UPLOAD-REQUEST.md` has intentionally paused automatic publishing.

## Mobile interaction requirements

The target is a **phone, especially a Galaxy Z Fold**, not a mouse/keyboard desktop. Every normal action must be discoverable and operable with finger touch, and should behave consistently with a pen. Use comfortably sized tap targets, touch scrolling and drag interactions; never require hover, right-click, keyboard shortcuts or microscopic scrollbars. When scrollbars are necessary make their grab area wide. Avoid wasted screen area, large headers, redundant text, permanent controls that can be contextual, and desktop-style docks. These requirements are for later UI decisions; do not spend time building a complex UI before the engine is stable.

- Use `MobileTouchScroll::configure` or `configureList` from `mobile_core/touch_scroll.h` for scrollable mobile menus. Use `MobileTouchComboBox` for custom touch selectors. The shared handler distinguishes taps from swipes starting on controls. Keep canvas, layer drag and slider input in their specialized handlers.
- **Toolbar gesture contract:** Toolbar button drag behavior belongs in `mobile_core/toolbar_drag_gesture.h`, not one-off mouse handlers. A short tap must still trigger the original QAction. The initial movement locks either the **inline axis** (adjust a user-configured brush parameter) or the **perpendicular axis** (Color button samples the canvas/reference on release). The inline axis is determined from each toolbar's edge/orientation, never hard-coded to horizontal. Settings keys under `MobileStudio/toolbarDrag/` control both assignments and enablement. Do not send these gestures through generic menu-click replay or accidentally paint on the canvas.

## Project state and next work

- [PRODUCT-ROADMAP.md](PRODUCT-ROADMAP.md) records **user-approved feature priorities** and the automatic version-history semantics. Read it before adding painting functionality; do not implement deferred extras merely because they are documented.
- [DEVELOPMENT_STATE.md](DEVELOPMENT_STATE.md) is the **concise persistent handoff**: current working architecture, latest locally compiled revision, known limitations, next actionable milestone, and how to read the watcher. Update it when meaningful development progress changes these facts; do not continually append a historical essay.
- `apply_mobile_shell.py` installs the current native engine/Android bootstrap patch into the KDE Krita `v6.0.4` source used by the local builder. `mobile_core/` contains the app's independent host, engine session and UI components. Keep license notices.
- On each new session, first inspect current code and latest local result; **continue implementation**, not just produce a plan or ask the user to rerun already-running tools. Avoid spawning cloud test jobs.

## Reporting to the user

Keep reports **short and nontechnical**: what changed, whether the **PC** compiled that exact commit (or pending/failed), and one next step. If phone testing is genuinely needed, provide a **brief checkable checklist** with only the most informative actions. Don't claim device testing from source checks or local compilation. Avoid unnecessary dialogs, extensive logs, or repeated status messages.
