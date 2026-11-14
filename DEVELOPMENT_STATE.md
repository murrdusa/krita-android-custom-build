# Krita Mobile Studio — current development state

## November 14 — one version history for symlinked Save As parents

- Local saved-version identity now resolves the closest existing ancestor for a not-yet-created project destination. Save As to `real-folder/future.kra`, a symlinked folder alias, or its local `file://` URL therefore shares one version-history identity before the new file exists.
- Added a Qt regression for canonical and symlinked parent paths, including a file URL alias.
- **Verification:** these commits await exact-SHA local WSL build and desktop Qt regression results. No APK upload.


## November 14 — unify file URL version-history identity

- Saved-version history now decodes local `file://` URLs before calculating project identity, reading modified timestamps, and opening archive input. A picker URL and filesystem path for the same drawing therefore share one version folder, rather than silently splitting backup history or failing to copy the KRA.
- Qt regression covers path-vs-URL history identity, checksum-backed deduplication across both forms, and percent-escaped filenames.
- **Verification:** new commits await exact-SHA Android WSL build and desktop Qt regression execution. No APK publishing.


## November 14 — guard Save As against local file URL aliases

- Open-document destination checks now normalize `file://` URLs to their canonical local filesystem path before collision comparisons. This prevents Save As / Export from treating a picker-provided URL as different from an already-open local document and also handles URL-escaped not-yet-existing draft targets.
- Native document open/save policy now resolves local file URLs for file existence checks and Save As picker starting paths instead of incorrectly treating a valid URL as missing.
- Extended the Qt native path-policy regression with existing/missing file URLs, percent-escaped new paths, and unrelated local/provider destinations.
- **Verification:** latest exact Android WSL compile and Qt regression status pending. No APK publication or phone testing.


## November 14 — bound abandoned-autosave recovery work per gallery refresh

- The project library now verifies at most two aged pending KRA exports per recovery listing, instead of parsing an arbitrary number of large ZIP archives in one UI-thread pass. A rotating scan offset ensures permanently malformed early files do not starve valid later recovery candidates. Further gallery refreshes continue recovery without deleting or overwriting remaining staged drafts.
- Qt regression stages five abandoned KRAs and verifies recovery in batches of two/two/one with preserved project associations. A second regression verifies that three invalid staged files cannot permanently block a valid later KRA.
- **Verification:** previous version-retention revision `f609ea9893d974942626e1afb3a0837bcf368f36` passed local Android WSL compilation. New source and Qt regression await their exact-SHA local verification. No APK upload.


## November 14 — overflow-safe version-history quota enforcement

- Version-history pruning now saturates its total byte tally once it exceeds the 512 MiB history quota, and uses subtraction-based capacity checks before retaining older checkpoints. This avoids overflow or accidental quota bypass when encountering oversized/sparse historical files, while preserving the newest save and verified older recovery entries.
- Qt regression creates an oversized sparse history file, verifies a new saved checkpoint still publishes, and confirms retention removes the out-of-budget older entry without discarding the current project bytes.
- **Verification:** earlier version-history revision `97f5986ac8e15d886ab941d70273318156cb0be5` passed the local WSL Android build. Latest source/test revisions await exact-SHA WSL and desktop Qt test verification. No APK uploaded.


## November 14 — preserve verified history over corrupted intermediate files

- Version-history pruning now prioritizes SHA-verified older KRAs before retaining corrupted or legacy files whenever history exceeds quotas, including when the latest saved version itself is healthy. This prevents a series of corrupt intermediate snapshots from evicting all usable older recovery points.
- Added Qt regression with a valid oldest checkpoint, a newer healthy save, and enough corrupted middle snapshots to trigger retention pruning. The oldest verified backup must survive.
- **Verification:** previous revision `b2d2542593592a6bd9de0a6d786ee39aa099502b` passed the local Android WSL build; this batch awaits exact-SHA WSL and host Qt test results. No APK uploaded.


## November 14 — bound checkpoint verification of oversized archives

- Published checkpoint validation rejects KRA archives outside the supported 8 GiB limit before hashing and stops if a file grows beyond the original scanned byte count during verification. This prevents an oversized/corrupted historical entry from forcing unbounded hashing or suppressing a new recoverable checkpoint during rate limiting.
- Added sparse oversized archive regression: a forged recent version must not prevent a valid saved-version snapshot.
- **Verification:** exact Android WSL build and host Qt test results for these changes pending; no APK published.


## November 14 — retain KRA version when checksum sidecar fails

- A completed saved-version KRA is no longer deleted if writing the separate SHA-256 metadata fails. The archive remains available as a legacy recovery entry; a later identical snapshot verifies the bytes and repairs the missing digest.
- Expanded Qt regression to verify recovery from an orphaned completed archive before a subsequent save repairs its checksum.
- **Verification:** latest source and regression committed; exact-SHA local WSL and desktop Qt test results pending. No APK published.


## November 14 — retain incomplete export data and draft titles

- A native exporter that fails before producing a readable KRA no longer removes partial staging bytes in the failure path. Successful exports with invalid staging retain their existing cleanup semantics; the previous completed private draft remains untouched.
- When a readable autosave is rescued under a new UUID after failed publication, its bounded, regular-file user title sidecar is copied alongside the recovered project association. Display title reads are length-bounded.
- Qt regression covers retained partial export bytes and title propagation to a rescued draft.
- **Verification:** source and regression committed; latest exact-SHA Android WSL build and desktop Qt test execution pending. No APK uploaded.


## November 14 — abandoned autosave staging freshness check

- Restart recovery verifies that a pending KRA's size and modification timestamp remain unchanged while Krita reads its ZIP structure. Staging files modified by a resumed/exporting writer are left in place, instead of being moved to a new recovery ID mid-write.
- Qt regression covers unchanged and concurrently rewritten staging metadata.
- **Verification:** exact Android WSL compilation and desktop Qt test status pending for these new commits. No APK publication.


## November 14 — restart recovery of abandoned autosave staging

- Recovery listing now scans old `.pending.kra` files left when the app exited between completing the native KRA export and publishing it. After a five-minute age threshold and successful project validation, it moves each completed staging copy into an independently listed recovery draft, preserving the old published draft and associated saved-project identity. Recent staging stays untouched to avoid racing an active export; invalid staging stays in place for manual investigation.
- Qt regression covers fresh staging being left alone and an aged completed staging file reappearing in the recovery list without overwriting the previous draft.
- **Verification:** previous failed-export rescue revision `6366d1ae1a06eff5ed2246ff6233bd9a5c339f1a` was still awaiting exact-SHA watcher verification at the start of this batch. Latest commits await WSL and Qt test results; no APK upload.


## November 14 — preserve readable draft after failed export

- Failed native autosave export no longer blindly deletes a readable KRA staging file. It preserves the last published draft and moves the readable staging archive to a new independently recoverable private draft, retaining the associated original project. Incomplete or invalid staging files are still cleaned up.
- Extended Qt regression to verify data and project association survive when export reports failure, without overwriting the previous published draft.
- **Verification:** preceding atomic-publication rescue revision `c3e54db38bc2b52bbb64001aadeb8a2b40cd576c` passed the exact local WSL Android build. Latest source/tests await exact-SHA WSL and Qt regression verification. No APK upload.


## November 14 — retain completed drafts after failed atomic publication

- A completed autosave export no longer gets deleted when the final rename into its private draft path fails. Instead it is renamed to a fresh private draft identity, keeping it visible in the recovery browser after app restart; its saved-project association is copied when available. If both renames fail, the completed staging file is retained rather than deleted.
- The Qt saved-version regression simulates a blocked destination directory and verifies the rescued KRA data and project association survive with an independently discoverable draft.
- **Verification:** earlier recovery revision `4c7fef2d10d9d2c24ed63127ef9d5fb7bb8bbb34` has an exact local WSL SUCCESS. This new batch awaits exact-SHA WSL/Qt regression verification. No APK upload.


## November 14 — native canvas registration identity after reentrant activation

- Canvas adoption now verifies the original registration token and document identity after Qt's synchronous first-tab activation; re-registering the same QWidget under a new document cannot falsely satisfy the earlier adopt request.
- Native tab activation similarly checks that the selected view retains its original document and registration token after `currentChanged` listeners run. This prevents a stale selection from being reported as successful when a callback swaps the backing canvas.
- Added desktop Qt regression cases for view re-registration inside selection notification and destruction of the first canvas's document during adoption. These tests are committed but not yet executed.
- **Verification:** last locally confirmed Android WSL build was `1ffe097bc1b5f32b976eae39d4917b94c40a78f2` (SUCCESS). Subsequent watcher statuses are pending/unreported at last check. This revision needs exact-SHA compile and host Qt tests. No APK uploaded or phone testing requested.


## November 14 — validate unique document owners before recovery

- The recovery workspace identity guard now rejects snapshots containing two native canvas views for one backing document, even if both the captured and current snapshots match. This prevents queued recovery actions from trusting malformed document ownership.
- Added host Qt regression cases for matching duplicate document ownership and duplicate native view identities.
- **Verification:** latest local WSL build status for the preceding deferred-command revision `bde29bf06dba92f005d17a3b4e65a92aebeb5028` was pending when checked. These new source/test changes require exact-SHA WSL verification. Host Qt tests were not executed; APK publishing remains paused.


## November 14 — deferred document action revalidation

- Deferred Save/Close dispatch now compares live view/document ownership on both sides of the native selected-document lookup. A reentrant Qt callback that replaces any canvas during that lookup invalidates the pending action rather than allowing it to run against a stale workspace.
- Qt regression simulates a replacement of an inactive document during the callback and checks that the original pending action is cancelled. Existing owner destruction, selection switching and view teardown tests remain.
- **Verification:** previous main commit `1ffe097bc1b5f32b976eae39d4917b94c40a78f2` had no exact local WSL status at session start; these new changes await their own watcher status and host Qt test execution. No phone test or APK upload.


## November 14 — enforce unique native document ownership

- Native Save/Close target validation now rejects cases where an inactive row shares the active row's document or view identity. Index lookup and active view selection also reject rows whose backing document has been destroyed.
- Fixed stale Qt host-port regression assertions that previously expected closing an unselected canvas to succeed; added duplicate-document and duplicate-view ownership cases.
- **Verification:** source and Qt regressions committed. Latest exact-SHA WSL build and host test results pending; no Fold 6 runtime testing or APK publication.


## November 14 — reject stale document-cycle targets

- Native previous/next canvas cycling now rejects duplicate document ownership and revalidates the full live view/document/selection snapshot before activating the neighboring canvas. This avoids cycling into a different or detached painting if Qt callbacks change the native tab list during a command.
- Qt navigation regression covers duplicate backing documents and a simulated canvas removal during the first document-identity query.
- **Verification:** source and tests committed. Exact WSL build status for this batch not yet reported; earlier canvas surface work remained pending at last check. Desktop regression and Fold 6 runtime execution not claimed. APK publishing paused.


## November 14 — Android surface recreation and canvas resume

- `MobileCanvasResumeObserver` now rebinds its exposure filter whenever the host widget's native window ID changes, including replacement of a still-live QWindow during Android surface recreation. It removes the old surface filter, installs the new one and requests guarded activation. This addresses a source-level scenario where the painting canvas could remain blank after a window recreation or fold transition.
- Extended host Qt regression to exercise repeated WinIdChange notifications and a subsequent native Expose event, while retaining hidden/deleted-canvas guards.
- **Verification:** previous archive batch `0c907d1f6abf4c215a2e9c52bab59cf198a632d1` did not yet have an exact watcher status when checked. These new commits also require their own WSL compile and desktop Qt test run. No device runtime pass claimed; APK publishing paused.


## November 14 — bounded saved-version deduplication and source copy

- The archive's unchanged-save deduplication no longer reads arbitrary-size SHA-256 sidecars or follows linked checksum files. It hashes at most the expected saved bytes from the previous version, so a corrupted/replaced archive cannot force unbounded disk reads before comparison. Tests exercise oversized and linked sidecar repair.
- During snapshot copying, an input that grows after its initial length check aborts the atomic transaction as soon as the expected byte count is exceeded; it no longer fills storage copying extra data that must be rejected.
- **Verification:** previous recovery batch `a7403cb8f3d828a46dc6f1bd816d764b4cbb4a53` has local WSL build SUCCESS. New changes await exact-SHA watcher status and desktop Qt execution. No Fold 6 runtime test or APK upload.


## November 14 — bounded startup recovery scanning

- Draft KRA header checks reject symlinks, special or empty files and archives over 8 GiB before opening with Krita KoStore. Project-association metadata rejects invalid negative file lengths.
- Host-side regression adds empty, linked and optionally sparse oversized archive checks without altering completed draft files.
- **Verification:** preceding callback-lifetime revision `a1a66758126a62f16924a3482ce6cfb9a2e7e533` received exact local WSL Android SUCCESS. The new recovery source/test revision is committed but has no reported exact-SHA build status yet. Desktop Qt test execution and Fold 6 behavior remain unverified. APK publishing paused.

## November 14 — native canvas callback lifetime hardening

- Native view adoption now verifies the registered view and document after synchronous Qt activation callbacks; it will not report success if those callbacks already detached the new canvas.
- Canvas activation revalidates the host, view, and row after Qt emits selection signals. Close handling no longer holds a member rollback guard across callbacks that may destroy the entire host, while still rejecting reentrant Save/Discard prompts. A desktop Qt regression checks host destruction and nested close.
- Thumbnail cache now rejects malformed, symlinked, oversized, and future-dated stale entries; superseded jobs skip encoding. Desktop regression coverage added.
- **Verification:** source and tests committed; local watcher has not yet posted an exact-SHA build status for these changes. Host Qt tests not executed in this session. APK publishing remains paused.


The first canvas is registered before Qt publishes its activation. Tab switching rechecks the selected document after native activation and avoids holding a row pointer across Qt callbacks. Deferred draft checkpoints tolerate document closure or queue changes during a save. Desktop view-stack regressions cover first-tab registration and synchronous tab removal.

New Canvas focuses the name and requests the Android keyboard on open. Numeric edits commit before validation. The form is destroyed before native canvas creation, repeated creation is blocked, and the first canvas uses the native exposure observer without cycling the Android window. This addresses source-level crash risks. A device crash trace was unavailable.

Home drops stale project menu actions after a list rebuild and cancels queued thumbnail work when hidden. A timed partial scan can retry after ten minutes instead of setting the full-scan daily timestamp. Starred sorting uses cached row state to avoid resolving project paths repeatedly.

Closing the app now starts with the active canvas and follows the live tab selection after each close. The native view manager is synchronized before the next Save/Discard/Cancel prompt. Home stops its directory scan when hidden and keeps projects already found. Sorting keeps the selected project and the top visible tile when possible.

Leaving Home or switching its tabs cancels pending version or duplicate opens. Home thumbnail requests check cached revisions, so older previews cannot block a newer save from appearing. Toolbar gestures cancel when a canvas or tool changes or Android backgrounds the app.

Rapid brush picks supersede queued selections and update the Recent list without rebuilding unrelated categories. Lighting settings persist across restarts. HSV adjustments retain the selected hue on neutral colors. Transient panels close when their canvas or Home surface hides.

Brush preset changes from the picker run after the item-view event, with active-view and panel checks. Android's native preset switch rejects incomplete presets and reentrant changes, and brush UI observers refresh after native preset activation. This addresses crash risks found in source. No device crash trace was available. Brush-size drag defaults to **Tool Settings** and migrates the former Tools and Paint defaults once.

## November 13 follow-up — Color picker and lighting sphere

Color opens an anchored picker with Wheel and Lighting tabs plus recent stroke colors. The Wheel tab has gradient HSV sliders, hex input and a temporary before/after swatch during wheel drags. Lighting has individual object, highlight and shadow controls, plus shadow hardness. Floating Color Wheel and Lighting Sphere are separate optional toolbar commands and can be positioned through Edit UI Layout. All three select Krita's native foreground color. Both sphere views share the same lighting settings.

## November 13 follow-up — toolbar gestures and Advanced Brush Settings

Brush-size drag defaults to Tools. A one-time migration treats a stored Paint assignment as the previous default. The gesture pickers use buttons on currently visible toolbars and retain bindings when a workspace hides their button. Advanced Brush Settings uses a frameless dark editor with touch-sized tabs and keeps Krita's brush-engine controls.

## November 13 follow-up — project menus and reference images

Project actions on Home use the shared nonmodal menu and close when another control is pressed. The Reference Images toolbar switches between references and can pin the selected image to the viewport or reattach it to the canvas. Pinning is undoable and stored with the native reference in KRA. Android uses taps, finger drag and pinch/rotation in place of desktop transform handles. The compact toolbar omits Reference Set and Lock Ratio.

## November 13 follow-up — New Canvas and touch controls

New Canvas uses the shared compact dialog and anchored preset and background menus. Transparent creates one paint layer. Solid colors create a locked native background under an empty paint layer. The background preference migrates from the previous white/transparent toggle. Toolbar opacity and Studio's standard menu sliders use a common touch control. Brush sliders keep Krita's native precision and sizing curves. The Android keyboard is requested after a deliberate tap on an editable field.

## November 13 follow-up — save actual in-profile canvas size after toolbar edits

- Commits `d99aa01c85f79e7277a8baff88501e6be0459510` and `a75ac7a19d302b0ed395ceedd492a0344ecb33b1`: the toolbar save path no longer always passes the stale viewport cached at the most recent workspace switch. When the canvas has resized *within* the same profile/orientation, `saveBars` persists the current viewport dimensions; for an incompatible folded/landscape/split-screen size awaiting a delayed profile switch, it preserves the old profile's dimensions instead of overwriting them. This completes the preceding existing-profile `remember()` update without mixing independent layouts. Commit `19a2edc4acfb40941f473942805775ec7ca8f302` tests small resize acceptance and cross-profile rejection.
- **Verification:** preceding profile persistence source `ecc5d2970d5a631261a431a309194a4b6917ecfd` received exact WSL Android SUCCESS (164.76 MiB, 172,766,266 bytes). New toolbar save source requires exact WSL build and host Qt regression execution. Fold 6 runtime checking remains pending; APK publishing paused.

## November 13 follow-up — persist updated dimensions for customized workspace profiles

- Commit `1565d4cdcea92e8ce03288db9600f06f612b5b82`: `MobileWorkspaceProfiles::remember` no longer ignores the current viewport size for an existing workspace ID. Saving the layout after modest foldable/split-screen/inset changes now refreshes the profile's size while keeping its ID and toolbar/panel settings, so future layout matching does not strand customizations at old dimensions or generate unnecessary new profiles. Invalid sizes and blank IDs do not change persistent settings. Commit `d1bcbde5abc52fc2403064a2e886308547db47c2` extends the Qt workspace regression to cover existing profile size updates, preservation of per-profile settings and rejected invalid size.
- **Verification:** prior checksum checkpoint source `8f3dee120ecba9c9ebd21ac60788d22d9b471057` received exact WSL Android SUCCESS (164.76 MiB, 172,765,842 bytes). New profile source requires matching WSL build; desktop Qt regression committed but not run. Fold 6/split-screen runtime behavior remains unverified; APK publishing paused.

## November 13 follow-up — apply bounded checksum policy to background checkpoint verification

- Commit `7288ba54c6bb81c7dcce7759368975340b27f551`: `hasPublishedCheckpoint` now rejects an oversized SHA-256 sidecar before reading it, using the same bounded policy as historical KRA recovery. This verifier is used by autosave rate-limiting and retention; corrupt or replaced metadata no longer entails an unbounded read or an opportunity to treat a damaged checkpoint as safely published. Commit `b861d0c75f406cb8399301e8257b30b6f6eb2f88` extends the host Qt archive regression: a 129-byte sidecar must not suppress a subsequent fresh routine checkpoint within the normal spacing interval.
- **Verification:** the previous live preview ABA corrections up through `6e400f781eb57a5286b90fb0f0f22531b663aad6` need exact watcher check; checkpoint verifier source requires separate matching WSL Android build. Host Qt regression added but not executed. APK publishing paused.

## November 13 follow-up — reject live preview ABA completion after tolerance returns to same value

- Source commits `38bd4b316c73c0b29283df23469cbee55e0f63dd`, `9af3ad4c29d8f7219ace0fdb79427e01b3050e63`, and `d8c839c7705cebbde3da3cd405fab609763c37dd` require both **request generation and tolerance value** to match before applying completed Fill/contiguous-selection preview masks or accepting a null result. An old background flood-fill may finish after the user drags away from a tolerance and back; matching value alone formerly accepted obsolete data. Old completions now schedule the newest preview. `88563fd8a8e3148491f2c7ed6b96d168b4457619` adds a Qt host regression of the ABA identity policy. Native tool commits and Undo remain unchanged.
- **Verification:** previous preview cancellation `06ee97872f57787a0f2f54bf50e77085e51179e1` received exact local WSL Android SUCCESS (164.76 MiB, 172,765,222 bytes). The new preview source and added desktop regression require validation; no Fold 6 runtime pass claimed. APK publishing paused.

## November 13 follow-up — cancel obsolete preview computations on gesture end and mid-job

- Commits `5b6a82e69e82e04828add6a329d3a5f52796d7cc` and `15609e1e7defa2a766df7efe2487b830b82ddb9c`: the live Fill/contiguous-selection preview invalidates its shared worker-generation when destroyed (gesture release, cancellation, tool switch). Workers check staleness after reconstructing source data, after the native flood-fill call, and while producing overlay rows; they stop outdated follow-up work. When a **live** preview changes threshold, all stale exits notify the UI to reset the running flag and request the latest mask, avoiding a stalled preview. Does not change native Fill/selection commit, original seed, or Undo semantics.
- **Verification:** previous preview optimization `d3c750b5fb1d34c7a6a72807e22a090d604467c9` received exact local WSL Android SUCCESS (164.76 MiB, 172,764,894 bytes). New cancellation changes need exact watcher SHA build confirmation. Fold 6 runtime performance/cancellation test outstanding. APK publishing remains paused.

## November 13 follow-up — skip stale queued live Fill/selection preview requests

- Commit `bfc0c9a49aa0921c59b7b324f328d3406dce02b7`: the shared non-destructive `MobileFillTolerancePreview` increments an atomic request generation whenever the finger/pen changes the tolerance. A background worker still queued in the global thread pool checks that generation **before** creating paint devices and running a costly flood fill; if its tolerance is obsolete, it returns to the UI thread, clears the running flag and schedules the latest preview. This avoids computing stale live masks when the pool is busy and the user drags rapidly. The native Fill and contiguous-selection transactions, including release/Undo, are unchanged.
- **Verification:** pending exact WSL Android build confirmation for the new source. No desktop Qt regression for the worker was executable in this session; Fold 6 gesture/performance validation remains pending. APK publishing paused.

## November 13 follow-up — keep live Fill/contiguous selection preview aligned during viewport resize

- Commit `5b7fdb8f7cfa305ec26ea6948ccac6ecc19b33a2`: the existing shared `MobileFillTolerancePreview` widget now watches its canvas-widget resize events and updates its transparent overlay geometry and painting. Fold/unfold, split-screen resizing, or canvas panel layout changes during an active tolerance drag no longer leave the affected-region preview badge or mask clipped to the canvas's former viewport size. Native Fill and contiguous selection activation, native Undo, and the bounded 384×384 snapshot remain unchanged.
- **Verification:** preceding Fill preview geometry code `25656d320aabdac3d27cde0823c03661546d08b0` received exact local WSL Android SUCCESS (164.76 MiB, 172,762,266 bytes). This widget resize fix needs exact watcher confirmation; no device runtime testing performed. APK publishing paused.

## November 13 follow-up — retain full live Fill preview near image edges

- Commit `a636e7e7e403183940fd387ba3106aa74735698b`: the existing throttled 384×384 non-destructive live Fill tolerance preview now **shifts its sample window inside the image** when the seed lies near a border, instead of cropping the region to 192×192 at a corner. It still caps sampled pixels, preserves image-space offsets, supports smaller canvases and does not change actual Krita Fill or Undo behavior. Commit `4b7a5a10bf3e058e193d2d6fddbef9f90fb3a86c` updates host Qt geometry regressions for center, both opposite corners, out-of-bounds seed, small offset canvas and narrow canvas.
- **Verification:** latest preview source requires exact WSL Android build confirmation and desktop Qt execution. Prior archive recovery fixes are committed and need status recheck. APK publishing paused.

## November 13 follow-up — reject oversized checkpoint SHA-256 metadata

- Commit `8799cfca779171c6d5aca7f53b8815df72c04dc0`: historical KRA recovery now rejects an oversized SHA-256 checksum sidecar before reading its contents. The reader requires a bounded (at most 128 bytes) hex digest with optional whitespace instead of unbounded `readAll()` from a potentially corrupt or replaced metadata file. A damaged sidecar cannot cause large memory use during restoration; no archive or original project is modified. Commit `ba00d918bf950c6c9643f7cb0a0277a7d429b930` adds a desktop Qt regression for 129-byte checksum rejection and restores the valid metadata for subsequent tests.
- **Verification:** source committed, exact WSL Android build pending. The Qt test is committed but not run in this session. APK publishing paused.

## November 13 follow-up — bound historical KRA recovery streaming against file growth

- Commit `8f729eb1d6bf41dd0c81a61eecae3aceaaa470b3`: `materializeRecovery` now enforces both the **initial opened KRA size** and the 8 GiB archive ceiling during each read/write iteration, not only before opening and after the copy completes. If another process grows an archive during recovery, Studio aborts the `QSaveFile` transaction before writing excess data; the existing end-of-copy exact byte count and SHA-256 checks remain in place. The original archive and prior versions are untouched.
- **Verification:** the preceding exact-size recovery source `924553fe8ae6be9827bec43d98ae1e208cf512ac` passed local WSL Android SUCCESS (164.76 MiB, 172,762,214 bytes). The new source needs exact matching WSL build confirmation. Native KRA recovery and Fold 6 Undo tests remain pending; APK publishing paused.

## November 13 follow-up — exact byte count for legacy KRA recovery copies

- Commit `924553fe8ae6be9827bec43d98ae1e208cf512ac`: version-history `materializeRecovery` now checks that the **entire size observed from the opened archive** was copied before publishing its private recovery KRA, even for old entries with no recorded `Version::bytes` or SHA-256 sidecar. If the archive shrinks or grows while streaming, the QSaveFile transaction is rejected rather than opening a partial copy. `2cfb07e6ea1a1d94bab2a64afa1989857d08e09d` adds a desktop Qt regression for successfully recovering legacy archives without stored size or checksum.
- **Verification:** previous clipping callback-generation source `7595099b76606fa7b75ca6630df38b6786907c95` passed local Android WSL build (164.76 MiB, 172,762,078 bytes). New archive source needs exact-SHA WSL build; desktop Qt regression committed but not executed. APK publication paused.

## November 13 follow-up — prevent stale clipping rebuild timers from affecting later edits

- Commit `7595099b76606fa7b75ca6630df38b6786907c95`: the compact-clip async state machine now advances a transaction generation at the start and end of every rebuild. Timers for sequential ungroup, move settlement, native group completion, and continuation each capture the generation and do nothing if the rebuild was cancelled/finished and another has begun. This closes a race where queued callbacks from a prior failed operation could inspect or mutate a new rebuild on the **same document**, despite image-identity checks passing. Existing Krita Undo and layer commands are unchanged.
- **Verification:** previous exact-group validation `92853fec51560176027bd20fc5db5293baaff6a4` has local WSL Android SUCCESS (164.76 MiB, 172,762,166 bytes). New generation-guard source requires its own matching watcher build. Native Fold 6 nested clipping, Undo and KRA reopen remain untested. APK publication paused.

## November 13 follow-up — verify exact children after asynchronous compact clip grouping

- Commit `71f8668cb02a7d7f80f1d6b3e26405070eedda23`: after Krita's native `createQuickGroup()` finishes, `waitCompactClipGroup` now requires the new group's actual direct children to equal the intended **entire ordered** clipping run, with no extra or missing layer/mask, before it renames the group `[Compact Clip]`. This prevents an intervening tree edit from silently converting the wrong group into a Studio-managed clipping wrapper. Existing timeout/abort handling remains in place; no UI redesign.
- **Verification:** source committed, exact WSL build pending. Nested group and mask runtime/Undo/KRA reopening have not yet been verified on Fold 6; APK publishing paused.

## November 13 follow-up — deduplicate overlapping compact clip wrapper traversals

- Commit `e136f30e1160c93e4f2f28d1a7baad6997148d5e`: compact clipping rebuild's recursive wrapper collection now deduplicates wrappers when the source and destination parent trees overlap (nested group or ancestor moves). The serial native ungroup queue continues child-before-parent but will not try to ungroup the same wrapper twice after the first command already detached it. No changes to actual Krita Undo commands.
- **Verification:** native Android WSL builder for the serial ungroup source `ab2ead134da7de2fe9553462592be79ac062fc3d` needs exact watcher result; the deduplication commit also requires its own build confirmation. Qt layer runtime/KRA reopen tests remain pending and APK publication stays paused.

## November 13 follow-up — serialize native clipping wrapper ungroup strokes

- Commit `ab2ead134da7de2fe9553462592be79ac062fc3d`: compact clip flatten/rebuild now executes native `quickUngroup()` **one wrapper at a time, innermost first**, checking the actual active image for detachment of each wrapper before dispatching the next asynchronous Krita stroke. Previously, all nested ungroup commands were enqueued back-to-back even though the native layer hierarchy had not settled, risking stale selection and incorrect regrouping. Each command still checks exact wrapper selection, image ownership and node manager. The same existing timeout aborts if one wrapper fails to detach. Native Undo ownership remains Krita's.
- **Verification:** previous selection-integrity code through `12473523c6239c170f310c9f4c529a1fadc7ab3b` received local WSL Android SUCCESS (164.76 MiB, 172,761,674 bytes). This new sequencing commit requires exact SHA build confirmation. Fold 6 nested clip/Undo and KRA round-trip runtime tests still pending; APK publishing paused.

## November 13 follow-up — bind native compact clip ungroup to intended wrappers

- Commit `6878157be6c4f8a443460cef1fc19cf86fc28bd3`: during compact clipping-group flatten/rebuild, every queued native `quickUngroup()` now verifies that its target is a live compact wrapper in the original image and that Krita's active node and full native selection still contain **only that wrapper** after native activation callbacks. If reentrant selection changes the target, the rebuild stops instead of ungrouping an unrelated user group. Native Krita still owns the ungroup and Undo implementation.
- **Verification:** source change committed; must confirm exact WSL build SHA from local watcher. The previous detached layer fix `f68e6058312161bc18f4e5d8c635b79c05ad20b1` has not yet been assumed verified. Clipping Undo and KRA save/reopen on Fold 6 remain untested; APK publishing paused.

## November 13 follow-up — clipping rebuild does not select detached layers

- Commit `f68e6058312161bc18f4e5d8c635b79c05ad20b1`: on completion or abort of a compact clip regroup, the layer workspace now restores the previously active layer only when it is **still attached to the original image tree**, not merely when its retained node pointer has some parent. It also checks that Krita actually activated the layer before restoring its native selection. The asynchronous move-completion check likewise refuses to treat a move as settled when the moving layer or expected parent has detached from the image, even if parent/sibling pointers still match. This prevents stale native UI selections after cancel/failure of a clipping-group move. No user-facing UI redesign.
- **Verification:** previous Save As change `40453b1ea63c3932efc221be7ad41a4ec92aeefa` received exact local WSL Android SUCCESS (164.76 MiB, 172,760,094 bytes). The new source commit needs matching watcher confirmation. Native Fold 6 clipping/Undo/KRA round-trip remains untested. APK publishing paused.

## November 13 follow-up — fail closed on incomplete native view stack during Save As

- Commit `db3f43e6e88cc1a64960a84bd3715a90139a23c4`: the native Save / Save As / Export Copy transaction now requires **exactly one active native view, belonging to the document being saved**, plus exactly one matching owner view in the host's open view list. It aborts on a missing view identity or an uninspectable backing document rather than skipping that entry during cross-canvas collision checks. This prevents a synchronous tab transition or temporarily incomplete view stack from authorizing a write into another canvas's project; it retains canonical SAF and local destination collision checks and Krita's save-completion handling.
- **Verification:** prior multi-canvas recovery guard `6b1bda5c31b215fd1fc3ed5e041c44d88982459c` has exact PC WSL Android SUCCESS (164.76 MiB, 172,759,934 bytes). This new Save As code requires exact-SHA build confirmation, with native multi-canvas Save As / Export and Fold 6 runtime testing pending. APK publishing remains paused.

## November 13 follow-up — reject recovery during native view activation gaps

- `6b1bda5c31b215fd1fc3ed5e041c44d88982459c`: `MobileViewIdentityGuard::sameRecoveryWorkspace` requires **exactly one active native canvas** whenever any canvases are open. It still permits an empty view stack. This prevents delayed historical Open Copy from starting during no-active-view tab transitions, or when the view host ambiguously marks more than one canvas active. `dda6ebca5be3251fda4f00e94fb205971e2510a5` adds Qt view-stack regression cases for both states.
- **Verification:** previous caller fix `1fcae40a370926060698520f45633dc5dc7372bf` has exact PC WSL Android SUCCESS. New guard and Qt regression require new exact-SHA watcher confirmation / host test execution. No phone test or APK publishing.

## November 13 follow-up — recovery safeguards backing document identity as well as view identity

- Commits `8c984ac8b91da7f28a435c11262dc70ad4cb564e`, `1fcae40a370926060698520f45633dc5dc7372bf`, and `cb9cd0c9acc8ef639e81b11968a4a412f4c16c32`: delayed historical **Open copy** captures the complete Qt-native `ViewSnapshot` set. The shared `sameRecoveryWorkspace` policy now verifies both native view identity and its attached document identity, as well as matching active-view status. This prevents recovery into a reused view widget displaying a different drawing while a large KRA checkpoint is materialized. The Qt view-stack regression now also rejects reused native views with new documents and missing/detached documents; stable multi-canvas, tab reorder and empty workspace remain permitted.
- **Verification:** the first iteration of multi-canvas recovery `a5b5677a93edbf397675d482277ab5ad5373b64c` received exact WSL Android SUCCESS (164.76 MiB, 172,761,378 bytes); this additional revision requires its own matching watcher result. Qt regression is committed but not executed in this environment. APK publishing paused.

## November 13 follow-up — historical KRA recovery alongside multiple canvases

- Commits `a5b5677a93edbf397675d482277ab5ad5373b64c`, `1ec541f2523ab38dfcf6edfe7428e01d56a7c8c1`, `bb37fb9833a7512016c35d3b8729768a817e6bd1`, `04a92632de124d4c4ce5de5455988e1155d65067` and `ccb6d19de143edf9cfb93eb01b7d75677a01eb53`: fixed the version-history **Open copy** workflow that previously refused every recovery whenever two or more native canvases were open. Async recovery now captures all native view identities plus the active view. On the UI callback it allows opening a recovered KRA only when the complete set of views remains unchanged (regardless of tab order) and the active view is unchanged; closing/replacing a canvas or switching documents aborts the delayed open. Consolidated validation in `MobileViewIdentityGuard::sameRecoveryWorkspace`. Added desktop Qt view-stack regression for stable two-canvas recovery, tab reorder, active-view switch, closed/replaced view, and empty workspace. Native recovered documents still reset their path and require Save As; private materialized copy is deleted after the synchronous native open finishes.
- **Verification:** Qt view-stack regression has been committed but not executed here; Android exact-SHA build remains pending for new changes. Previous clip-ungroup fix `b06e500695c8dc368591d4b0ac6ca6074fcff438` was last confirmed by local WSL watcher. APK publishing paused.

## November 13 follow-up — compact clip ungroup completion checks live tree membership

- Commit `fe84b39c1951535743e8fc366da6242bfa57d543`: async `waitCompactClipUngroup` now regards a wrapper as pending **only while its node is still attached to the active image tree**, instead of checking whether the retained shared pointer has a non-null parent. Krita can detach a group while its parent pointer still references an orphaned subtree; the previous check could spin for the full timeout and abandon clipping regrouping even though the wrapper is no longer in the document. The new check avoids that false timeout while leaving native ungroup/Undo ownership unchanged.
- **Verification:** source change committed; Android WSL build needs exact commit SHA confirmation from Issue #1. Actual Android clipping-group regroup/Undo and KRA save/reopen still require runtime tests. APK publishing paused.

## November 13 follow-up — compact clip rebuild target integrity

- Commit `72baabbe424d6e8301441b1482cccbe16b6fafab`: rebuilding hidden compact clipping groups now revalidates the **entire adjacent clip run** after native `slotSetSelectedNodes(cluster)`, before `createQuickGroup()`. The guard checks the active image/node manager, each cluster member's parent and image membership, contiguous sibling order, native move eligibility and exact selected node set. If synchronous native callbacks changed the cluster, rebuilding aborts rather than grouping unintended layers. Krita retains native grouping, clipping behavior and Undo.
- **Verification:** previous drag reorder source `6e8ca14ae140af70a1e8f170508b43d73a98b54c` passed the PC WSL Android build (164.76 MiB, 172,758,282 bytes). This new clip-rebuild code requires an exact-SHA watcher confirmation. No Fold 6 clipped group/Undo/KRA reopen test; APK publishing paused.

## November 13 follow-up — validate drag reorder after source activation

- Commit `6e8ca14ae140af70a1e8f170508b43d73a98b54c`: the native, undoable drag-reorder path for paint layers and ordinary masks rechecks the active image and node manager, source/target membership, actual source parent, destination parent, eligibility, below-sibling identity and parent-cycle safety **after** activating the source layer. Previously these checks ran before activation; a synchronous native selection callback could invalidate the command's destination between validation and Krita's stroke. The selection-mask direct-move path and async clipping wrapper path retain their existing separate handling.
- **Verification:** version-history changes through `4b4847e7861cc3e9c602cd05e55c07ea3f431b50` have exact watcher Android SUCCESS (164.75 MiB, 172,757,754 bytes). New drag-reorder commit requires its own watcher confirmation. Qt archive regression and Fold 6 mask movement/KRA+Undo runtime remain untested; APK publishing paused.

## November 13 follow-up — prioritize all verified recovery checkpoints during pruning

- Commits `36ee661bc108496177a73f881a049ed3a6ae669e` and `c8d6d26cb514483a33b8f9a94f2ddbcaa65d3c57`: under a full version-history retention quota with a corrupt newest checkpoint, pruning now protects the newest entry for inspection, reserves the latest verified fallback, **then prioritizes other SHA-256-verified older KRAs before using any remaining slots for damaged or legacy entries**. This prevents corrupt intermediate copies from displacing multiple good checkpoints. Quota-free snapshots still skip expensive hashing. Updated Qt archive regression migrates two distinct intact donor histories into a full corrupted archive and asserts both can be recovered after pruning; recovered temporary copies are cleaned up.
- **Verification:** new Qt archive regression is committed, not executed here (Qt development libraries unavailable). Check Issue #1 for the new source's exact WSL build SHA. Fold 6 version history UI and recovery not tested; APK publishing paused.

## November 13 follow-up — retain verified recovery history under corruption

- Commits `9d34f0a6f3d31881eb28dc69c0cefe75b5fd3723`, `f33bf291bb5ffcfe432c27bf41b75a0afcd9bbde`, and `2db2c72ae091ec3867723423cc63ff1ac3a2a67a`: history pruning now preserves the newest archive for diagnosis **and** reserves a retention slot for the latest SHA-256-verified older checkpoint if the newest archive is corrupt. A corrupted newest checkpoint can no longer consume the mandatory retention slot while the quota removes the last intact historical version. Checksum sidecars are deleted only when their matching KRA file was removed. Expensive integrity checks are skipped entirely when the archive is within count and byte quotas. Added Qt archive regression that fills the quota with corrupted versions, merges an older valid donor, triggers prune, and verifies the donor remains recoverable while the bad newest entry remains inspectable.
- **Verification:** new Qt test is committed but has not been executed in this environment; exact Android WSL watcher build for new source remains pending. Previous reorder code has not been assumed validated unless watcher reports its SHA. No phone recovery test or APK upload; publishing paused.

## November 13 follow-up — one-step reorder sibling anchor identity

- Commit `46b67e816e140394aaf551f8fb625505373b506e`: native Up/Down now records its original sibling count and moving layer index before exiting Transform. Immediately before queuing Krita's undoable layer-move stroke, the code confirms that the child count and moving index are unchanged and the originally computed sibling anchor is **still at its exact expected index**. This closes a race where a native node notification could reorder/insert another sibling during the tool transition while leaving the old anchor attached to the same parent, causing the wrong move. Group/clip reordering keeps its separate native path.
- **Verification:** previous Clear/Add Mask/Duplicate source `8a2a919111ed7bdab99525191b3723a764b53233` has PC WSL Android SUCCESS at latest watcher check. The new reorder change requires a matching exact SHA build; native Android reorder/Undo and Fold 6 testing not executed. APK publishing paused.

## November 13 follow-up — Clear, Add Mask and Duplicate target binding

- Commit `8a2a919111ed7bdab99525191b3723a764b53233`: in the tapped-layer actions menu, **Clear**, **Add Mask** and **Duplicate** recheck Krita's active image, node-manager identity, target's image membership, active node and exact single-node native selection after invoking `slotSetSelectedNodes({node})`, before any command runs. The guard prevents synchronous native selection notifications from redirecting destructive Clear, mask attachment or duplication onto a different layer. Krita retains native implementation and Undo.
- **Verification:** source committed; matching PC watcher build confirmation pending. Earlier native Merge/Flatten/Ungroup source build must not be conflated with this commit. No phone-based mask/duplicate/clear Undo or KRA round-trip tests performed. APK publishing paused.

## November 13 follow-up — native Merge/Flatten/Ungroup selection integrity

- Commit `b1041e94d1ed37692552bdfc65e8bf2082cfcf9f`: the tapped-layer Merge, Flatten and Ungroup commands now recheck the active Krita image, node-manager identity, layer membership, active node and **exact native single-layer selection** immediately after `slotSetSelectedNodes({node})`. Ungroup also confirms the target still inherits `KisGroupLayer`. If reentrant callbacks retarget the selection, the destructive/hierarchy-modifying native action is aborted rather than applying to another layer. Krita continues to own operation and Undo semantics.
- **Verification:** previous multi-delete protection at `8d7ac695a53ab939aed53d3126da2d0994ad07ce` is WSL Android SUCCESS (164.75 MiB, 172,755,750 bytes). New merge/flatten/ungroup revision needs its own exact watcher build. Native Android/phone operation, Undo and KRA reload behavior remain untested; APK publishing paused.

## November 13 follow-up — native multi-layer Delete target integrity

- Commit `8d7ac695a53ab939aed53d3126da2d0994ad07ce`: multi-layer Delete now verifies Krita's **actual selected-node set** after `slotSetSelectedNodes(targets)`, before invoking the native `removeNode()` operation. It also rechecks active image, node manager, membership, parent attachment, and Krita's native edit eligibility. If synchronous native selection callbacks changed the target list, Delete aborts instead of deleting a different selection. Single-layer deletion continues to use Krita's `removeSingleNode` path. Native Undo remains Krita-owned.
- **Verification:** the WSL watcher result must match the new source SHA; a prior SUCCESS is not sufficient. No device multi-layer delete/Undo/KRA round-trip test in this session. APK publishing stays paused.

## November 13 follow-up — pinch-to-group range revalidation

- Commit `b20a6e210d7f8bc9c748022591edc3e434736c94`: two-finger layer-rail pinch grouping validates the **entire contiguous chosen range**, not only gesture endpoints, after leaving Transform and again after activating and selecting the native nodes. It checks every member's image membership, unchanged parent, layer type and immediate sibling order, alongside Krita's move eligibility. It also compares the actual native selection against the gesture's chosen nodes before `createQuickGroup()`, aborting a stale/incomplete grouping request. Native Krita owns the resulting grouping and Undo.
- **Verification:** the prior manual Group Layers validation at code commit `fb4735d8b22c992f70b05226731e8b4e74df3465` received PC WSL Android SUCCESS (164.75 MiB, 172,755,162 bytes). The new pinch-group commit requires its own exact-SHA watcher confirmation. No device pinch grouping, native Undo or KRA reopen run in this session. APK publication remains paused.

## November 13 follow-up — manual Group Layers validation after Transform

- Commits `fb4735d8b22c992f70b05226731e8b4e74df3465` and `fe85e12a4e4e6066522198064d88a6abe455c689`: the manual **Group layers** dialog revalidates all selected native layers immediately after leaving an active Transform tool and again after applying the native multi-layer selection, before `createQuickGroup()`. It ensures the image/node manager still match, the parent remains in the image, all chosen nodes remain editable siblings in that parent, Krita still permits moving the chosen layers, and Krita's actual selected-node set matches the dialog's intended set. Reentrant selection/canvas changes now abort grouping rather than grouping different nodes. Root-level grouping remains supported.
- **Verification:** previous root-sentinel transform selection revision `3125d3fe04a645e4795ddd8a71755a865b198bc4` reported exact PC WSL Android build SUCCESS (164.75 MiB, 172,753,890 bytes). New grouping code requires its own exact SHA from Issue #1. No Qt/Krita runtime or Fold 6 grouping/Undo/KRA test performed this session. APK publishing paused.

## November 13 follow-up — image root is not an explicit transform selection

- Commits `078b0b1774a1a2d9fd4707a189827c687479a363` and `fc12f75a3d7a277da0213b03f62d6095dead1136`: the shared `MobileLayerIdentity::transformTargets` policy no longer treats Krita's image root sentinel as a real explicit transform target. If only root/hidden entries are selected, a validated visible active layer can be used instead. An explicit child selection still takes precedence; stale nodes from outside the image continue blocking fallback. Added three host regression cases for root, root+child, root+hidden mask.
- **Verification:** host-side C++17 regression built with `-O2 -Wall -Wextra -Werror` and passed **22/22** cases in this session. The PC watcher last confirmed `dc18f17162221c7add1d4f9c0bd1f9c7d46f4814` SUCCESS (164.75 MiB, 172,754,114 bytes); the new source/test revisions require a matching exact-SHA Android build. No native Transform commit/cancel/Undo or Fold 6 runtime test performed. APK publishing remains paused.

## November 13 follow-up — keep active Transform/Move target on empty native selection

- Commits `88c465cf59f9a62cbdecd88d95f34db708598f27` and `b446355428d00e6e2dcf7a12171d4df0a2cc4985`: the layer rail's native selection `reload()` now passes its validated visible active node to `MobileLayerIdentity::transformTargets`, matching the Transform/Move tool-entry policy. When native selected nodes are temporarily empty (or only hidden global-mask entries), a refresh restores the active layer as a native target rather than leaving Move/Transform with an empty selection. When actual visible selections exist, group-and-child de-duplication is unchanged. Added Qt-free regression cases for empty and hidden-only selection fallback.
- **Verification:** the previous nested group refresh revision `dc18f17162221c7add1d4f9c0bd1f9c7d46f4814` has exact PC watcher SUCCESS, 164.75 MiB (172,754,114 bytes). The new change still needs its own exact-SHA build. Qt-free regression is committed but not executed in this session; Fold 6 native Move/Transform commit/cancel/Undo still requires device verification. APK publishing stays paused.

## November 13 follow-up — native selection reread in Transform/Move

- Commits `a2cf7998e50d4e81a1bb228b98c403fa786c739f` and `2aae57ac6c15b49b59e30fce6baf1f5942033311`: the layer rail `reload()` now normalizes incoming native Transform/Move selection using the existing outermost-node policy, preventing an engine notification or rail refresh from reintroducing both a selected group and its descendants into Krita's transform target list. If the active node is a covered child, the rail activates one of the retained outermost nodes. Explicit global Edit Selection is not altered. Added three Qt-free hierarchy regression cases modeling reread order, masked nodes, and nested groups.
- **Verification:** source and regression committed; host C++ regression has not been rerun in this session and native transform commit/cancel/Undo remains a device test. Confirm the precise WSL build SHA in Issue #1. APK publishing stays paused.

## November 13 follow-up — prevent deleting projects with recovery drafts

- Commits `8b35245503d870d9c9c57bdef16a5f09cac17279`, `4e17492d041945d41dea61125683f2d89213bd5c`, and `0c85324c3328d0cdad898677b473ddba96fff837`: project gallery deletion now rejects an open canvas whose private draft is associated with the selected project, and blocks deleting any named KRA that still has an associated private recovery draft (including a draft not currently open). The gallery informs the user to recover or discard draft edits first. A new `MobileDraftRecovery::hasProjectDraft` helper compares canonical KRA/SAF project identities; Qt regression verifies association detection and clearance after removing the association.
- **Verification:** watcher previously confirmed `afd190a5135572f75b7ecf5858631999d5361ded` SUCCESS at 164.75 MiB (172,751,030 bytes); the gallery protection requires its own exact-SHA build. Qt regression added but not executed in this environment; Fold 6 actual project deletion/recovery still unverified. No APK publishing.

## November 13 follow-up — draft title sidecar confinement

- Commits `f5618555610f4f59f51b19a267e778d5296602e3` and `370eefcc6c3782f902a55eef60ba40566487a331`: `MobileDraftRecovery::displayTitle` now reads stored labels only for UUID-named private drafts, rejecting symlinked `.title` sidecars. A substituted link is ignored in favor of a generated title, preventing an external file from determining the draft gallery label. Added Qt archive regression for an external title symlink and restoring a normal title afterward.
- **Verification:** test is committed but not executed; this environment lacks Qt5 development libraries. PC watcher last verified baseline and exact current compilation status are in Issue #1; latest source changes require their own exact-SHA confirmation. Real Fold 6 draft gallery and recovery remain untested; APK publishing paused.

## November 13 follow-up — private draft metadata identity

- Commits `75265d42d75e2635fd357d301d175a49b88ff8d0` and `cde03dc0f76aa319a0853af85676ad348bd28b12`: `MobileDraftRecovery::projectFor` refuses metadata lookups outside its UUID-named app-private draft directory and refuses symlinked `.project` sidecars. This prevents a substituted project-association file from causing an automatic recovery attempt under the wrong project identity. Qt archive regression checks an external path, symlinked sidecar and restored legitimate sidecar.
- **Verification:** previous own-recovery Save As revision `02d112512ec7ef59661ef1b45cad5fb97939d948` reported PC Android SUCCESS, 164.75 MiB (172,752,098 bytes). The new draft metadata source commit requires exact watcher build confirmation. Desktop Qt regression has been added but not executed here; no Fold 6 KRA/device checks. APK publishing remains paused.

## November 13 follow-up — protect current recovered canvas on Save As

- Commits `4824dbdd2a10045e2c11c02b9fbef352385aa7fa`, `7b5b0ea9716df2fd6e11bed422d2236eba849171`, `c3d47cfca608bfa60424dde2b5ce8a56dcbefd08`, `d1b6b85c04700f1444fe256b04b833d650bd3771` and `736cffb5bb10432fdb205b5e9eba51739f0b3f67`: Save As now refuses to write to its **own** private recovered draft/checkpoint path (previously only another tab's backing file and Export Copy were protected). The shared destination policy retains ordinary Save for existing named projects while blocking private draft overwrite for explicit Save As and preventing Export Copy from overwriting the current project. Extended the Qt archive regression with mode-specific positive/negative cases for the shared predicate.
- **Verification:** the PC watcher has confirmed the preceding recovery cross-canvas protection at `6ecce421b36ce3b31fab1b9af2ad65c934e09acf`: SUCCESS, 164.75 MiB (172,751,198 bytes). This newly committed own-draft Save As patch requires a matching Android watcher result. The Qt archive regression was not executed here, and real Save As / provider / on-device KRA reopen behavior still needs testing. APK publishing remains paused.

## November 13 follow-up — protect open recovery drafts from Save As collisions

- Commits `eb614cbde9c792563e298ba67a57ce24c4bb5486`, `3355ad7f778172a34994af81283314d553d2a492`, `e8ee106fe21fc514835c4e2e1c12a277a5bc18eb`, and `e62fb204eab0a81e6f0c601611143ed1ace85f8c`: Save As and Export Copy now compare their destination against both a second canvas's visible project path and its private `_mobileDraftPath` recovery backing file. A recovered/untitled canvas may have `resetPath()` and no visible project filename, but its private copy must remain protected from overwrites. Export Copy also refuses to overwrite the current canvas's own backing draft. Added Qt archive regression for a blank visible path with a live recovered backing path and a noncolliding destination. Fixed its test symbol naming before verification.
- **Verification:** watcher confirmed the previous Save As active-canvas revision `6d34f4746eb1fe9dc1eead6e7335d909cf7ffc59` SUCCESS, 164.75 MiB (172,752,078 bytes). The new recovery-backing collision revision still needs its exact watcher build; Qt archive test has not been executed here because Qt5 dev libraries are unavailable. On-device Save As, Export Copy, recovered KRA reopen and SAF permissions remain unverified. APK publishing paused.

## November 13 follow-up — Save As dialog active-canvas binding

- Commit `53b4bc4af7c7f70e1722780b841d18be101a07c5` strengthens `MobileKritaStandaloneDocuments::writeDocument`: Save, Save As and Export Copy require the target document to be the currently active native canvas at invocation, after the image-idle dialog, after the file picker, and just before invoking Krita's writer. Ownership alone was insufficient: another tab could become active during a dialog while the original document remained open, permitting unintended writes to the previously selected project. Canvas switches now cancel that pending save attempt instead of writing to an inactive document.
- **Verification:** source committed; no device Save As dialog/canvas-switch test in this session. Last PC watcher-confirmed success before this patch was `14562d19bdff7d3bd2e1a0530164ae564032b0c0`, 164.75 MiB (172,750,414 bytes), confirming the previous async clipping batch. Wait for exact watcher SHA on the new Save As patch; native runtime/SAF behavior remains unverified. APK publication paused.

## November 13 follow-up — asynchronous clip-group hierarchy validation

- Commit `0e50408648f2f1c4f60edf444b3f91decd9adae6` adds document/tree revalidation to the asynchronous compact clipping regroup operation. Before native grouping, it checks that the parent and all selected child nodes still belong to the current image, that node-manager identity and Krita move eligibility still hold after activation, and that the children retain their expected parent. Its delayed group rename also refuses a detached parent, child, or group. This is a preventative fix for stale hierarchy mutations during rapid editing or canvas changes.
- **Verification:** local Qt-free `mobile_layer_identity_regression` repeated: PASS, 14/14 tests using the C++17 compiler (pure transform-selection policy, not Krita's native clipping engine). PC watcher confirmed SUCCESS for previous baseline `1b0a9640e2eedf1a2b24efb5d23e0290aa0b7354` (164.75 MiB, 172,748,498 bytes). The new clipping commit has not yet been confirmed by the PC Android watcher or tested on Fold 6. Native mask/clip KRA round trips, transform Undo and live interactions are still unverified. APK publishing stays paused.

## November 13 follow-up — executable Qt-free transform hierarchy regression

- Commits `086af028f5ed65a31f54f211762d37c501f3e667` and `466bbd78c17d986f37c05acedb807dbe25639986` add a standalone C++17 test and `tests/run_mobile_layer_identity_regression.sh`. The test exercises 14 selected group/child/mask cases against `MobileLayerIdentity::transformTargets`, including overlapping group selection, three-level nesting, independent sibling layers, duplicate selected nodes, stale nodes outside the image and Edit Selection behavior. **Executed locally:** PASS, 14 cases, using `c++ -std=c++17 -O2 -Wall -Wextra -Werror`, no Qt or Android build needed.
- **Verification scope:** this exercises the pure target policy only, *not* native Krita transform commit, cancel, Undo, KRA serialization, mask attachment or on-device responsiveness. Read Issue #1 for the latest exact compiled SHA and APK size. No APK publishing or hardware tests.

## November 13 follow-up — mask drag validation after Transform

- Commit `1a059796c1b0170f3a5d0a298015e3fe8815cbad`: mask/layer rail drag moves recheck the active image, node manager, source's original parent, target editability/membership, Krita's move eligibility, and sibling anchor after leaving Transform. Revalidates the plain undoable move path, including cross-layer selection masks, and the asynchronous compact clipping move callback. Invalid/stale targets are refused rather than being queued as a native stroke.
- **Verification:** code committed; exact PC watcher result still pending at time of entry. Last known SUCCESS was `de94172384f19fe8ccb1a8c0ba0f9680f80e6298` at 164.74 MiB (172,747,442 bytes) unless Issue #1 shows a newer SHA. This session did not execute standalone Qt or Fold 6 mask/KRA/Undo tests; do not claim runtime success. Publishing remains paused.

## November 13 follow-up — native layer/mask reorder target revalidation

- Commit `e05a38ee83ac8d368376692fffe22c67a13c0bed`: the one-step native reorder path rechecks active image, source membership and original parent, parent editability, and sibling-anchor identity **after** leaving Transform and selecting the row, just before constructing the native undoable move. This prevents a reentrant selection/node change from queuing a move against detached/stale nodes. Applies to native layer/mask sibling reorders without replacing Krita's stroke or KRA storage.
- **Verification:** change committed; no device runtime or desktop Qt execution in this session. Check Issue #1 for the exact build SHA and APK size (prior watcher successes are not proof of this revision). Outstanding hardware checks: mask attachment and reordering, multi-layer transforms commit/cancel/Undo, clipped-group KRA roundtrip and canvas switching during active operations. APK publishing remains paused.

## November 13 follow-up — pinch-to-group transaction safety

- Commit `521f54184fb84c480f8fee9d8f41b3e364c5396a`: the two-finger pinch-to-group gesture now obeys the same pending-transform guard as native reorder. Immediately before native grouping, it rechecks image identity, node-manager identity, sibling parentage and move eligibility. Its delayed collapse/reload callback is scoped to the original image and still-attached nodes, so a canvas switch cannot apply group-presentation updates to the wrong painting.
- **Verified baseline:** PC watcher reports SUCCESS for `09c588840166ff9e9841d061249b8ac6134199a6` at 164.74 MiB (172,745,246 bytes). The new pinch-to-group commit needs exact-SHA compilation confirmation; no standalone Qt execution or Fold 6 runtime test was performed. Hardware checks remain native transform commit/cancel/Undo, mask attachment/reorder, clipped-group KRA roundtrip and rapid canvas switches during edits. Keep APK publishing paused.

## November 13 follow-up — live multi-layer rail selection

- Commits `463c26ea25227a736f48f8b47484f79797d3aa3b` and `5a04b77a821bb0d5a7ab03820ecb9ee6c7a70bde`: layer rail target selection in an already-active Transform/Move session now runs the same group/child de-duplication as tool entry; the displayed selected rows are synchronized with the actual native target list. This prevents newly tapped nested layers from submitting duplicate transform targets without changing Krita rendering or undo commands. Extended the identity regression for a live-selection update.
- **Verification:** baseline `dab9b1650f9dd6476f6d0fb54b57e6bce877255c` has confirmed PC Android SUCCESS with 164.74 MiB APK (172,743,814 bytes). The new source commit needs an exact SHA watcher build; Qt-only tests have not been executed in this environment. Fold 6 transform commit/cancel/Undo, mask attachment/ordering, clip groups and KRA roundtrip still need device verification. Publishing paused.

## November 13 follow-up — nested multi-layer transform targets

- Commits `420341d579763221ef0df56084e6cf14607a88cc` and `0f2575aff11daec5ee7f41d33839d2037762e2f8`: native Transform/Move target normalization now drops selected descendants whenever their containing group/ancestor is also selected. This avoids submitting the same subtree twice to Krita. Independent selected sibling layers remain separate targets. Pure identity regression covers both parent/child orders, nested hidden mask, and siblings.
- **Verification:** last watcher-confirmed Android build at review start was `dab9b1650f9dd6476f6d0fb54b57e6bce877255c` (164.74 MiB); the new transform revision requires exact-SHA watcher confirmation. Desktop Qt regression not executed (Qt5 development packages unavailable here). Fold 6 hardware checks needed for multi-layer Free/Perspective transform commit/cancel/Undo, mask attachment, clipped groups and KRA reopen. No APK release/publishing.

## November 13 follow-up — failed history destination regression

- Commit `9f04ac90dde4b2038aee229f47fcb6800d8f07ee` adds a Qt-only archive regression using a blocked history-directory path. It asserts that checkpoint failure never modifies the original KRA or the last good historical copy, and verifies snapshotting resumes using an accessible destination. This models destination failure, not real disk exhaustion.
- **PC build:** prior native-view-close/source batch `9e3636729c09ab5874d37329604dd944b36c65d4` reported SUCCESS in Issue #1 (November 13 at 10:44:19 UTC). The new test revision has not yet received confirmed desktop Qt regression execution or an exact matching Android watcher SUCCESS. Check Issue #1 before claiming a new APK size.
- **Outstanding on-device gates:** true low-disk / provider failures, background recovery and explicit discard, SAF Save As, multi-layer transform commit/cancel/Undo, mask/group KRA round trip, rapid switching during save, UI geometry. Keep APK publishing paused.

## November 13 follow-up — stale native Close after document switches

- Commits `fd219322ab090efe997d270aca07d1f8cd658f07` and `07427846911bd3c278998782ad2a59b98ced4b1e`: the native view stack now rejects a Close on a view that is no longer the active painting canvas. Added standalone Qt regression coverage verifying that a stale background view Close cannot invoke its save/discard callback or remove the document; a freshly selected view can still close normally.
- **Verification:** regression added but not executed locally (no Qt5 development libraries in this session). Await exact watcher result for the new commit; the most recently confirmed APK must be taken from Issue #1 and must not be attributed to the new patch. Fold 6 tests still needed for rapid switching while Save/Close dialogs are open, KRA recovery, SAF document access, transforms/masks, and panel geometry. Publishing remains paused.

## November 13 follow-up — Save As collision identity

- Commits `d48c80a6c74fd9c860ae4f7fe64a30a9e374b34d` and `a036770850aba43d92fe398abd8800b3001ff2bf` on `main`: Save As/open-document collision comparisons canonicalize the nearest existing filesystem ancestor for missing target files. This prevents a symlinked parent directory from disguising a collision with a second open destination even when intermediate directories/files have not been created. Added a Qt archive regression using a linked folder and a still-missing target, plus a distinct-target negative control.
- **Verification:** change committed and code reviewed; desktop Qt executable not run in this environment. Await exact PC watcher result for `a036770850aba43d92fe398abd8800b3001ff2bf` and newer commits; do not infer a build from a previous result. Device checks remain: SAF Save As/provider permissions, interrupted saves/recovery, multi-layer transforms and masks/KRA, rapid switches and geometry/keyboard. APK publishing paused.

## November 13 follow-up — draft-path publication protection

- Commits `4bfea1f04fb2f36f43401c62ff960527fa905fab` and `99e1c97784292dbb85e9a7ebe8dd6a90aefd8e22`: private draft publication and its project sidecar now reject paths outside the UUID-named app-private draft directory. The staging file must not be a symlink. A new Qt archive regression verifies a synthetic external project cannot be replaced by accidental draft publication.
- **Verification:** source reviewed and changes committed; local build for `99e1c97784292dbb85e9a7ebe8dd6a90aefd8e22` is not yet confirmed, and the desktop Qt regression has not been executed in this session. Last confirmed APK remains `b24360b76ae2d757954672ae5bace1594360a8c2` at **164.74 MiB**. Recheck Issue #1 for exact PC build SHA; no device or runtime PASS implied.
- Remaining hardware gates unchanged: interrupted/background saves, SAF provider write, Save As collisions, multiselection transforms, masks and clipping KRA reopen, UI/layout on Fold 6. APK publishing stays paused.

## November 13, 2026 — checkpoint integrity hardening

- Source commits `c04f51d7fad607649825b4bfd511ea4a66aba134` and `75b9dd642f5527ef869cd42eabe9fc05631cbac8` on `main`: rate-limited history checkpoint requests now verify the *actual archived SHA-256*, not only the syntax of the sidecar digest. A corrupted newest archive (including same-size byte changes) cannot suppress a replacement checkpoint. Added a desktop Qt archive regression specifically for same-size archive corruption during the minimum snapshot interval.
- **Verification:** last confirmed PC Android watcher SUCCESS is `b24360b76ae2d757954672ae5bace1594360a8c2` at **164.74 MiB** (172,741,650 bytes). The new source/test commit `75b9dd642f5527ef869cd42eabe9fc05631cbac8` reported **pending** when checked. Do not attribute the old APK size to the new commit. `tests/run_mobile_archive_regression.sh` and `tests/run_mobile_native_view_stack_regression.sh` still require execution on a machine with Qt5 development libraries; this session cannot execute them. The local watcher compiles Android but does not establish a desktop Qt test PASS.
- **Still requires Fold 6 verification:** background/kill recovery and explicit discard; SAF `content://` and Save As; multiple selected layer transform commit/cancel/Undo; masks/group clipping and KRA roundtrip; rapid canvas switches, layout cutouts, and native Fill/Clone previews. No APK uploaded or published.

**Updated:** November 12, 2026. **Branch:** `main` of `murrdusa/krita-android-custom-build`.

Read `AGENTS.md` first, then `PRODUCT-ROADMAP.md` and [local WSL build Issue #1](https://github.com/murrdusa/krita-android-custom-build/issues/1). This file records *current* status, not a commit-by-commit log. The preceding complete log is preserved verbatim in [DEVELOPMENT_HISTORY_2026-11-11.md](DEVELOPMENT_HISTORY_2026-11-11.md). More focused runbooks live under `mobile_core/`.

The Android package excludes the obsolete Krita launcher left by earlier incremental builds and third-party manuals, host build data, and gettext command-line libraries and catalogs. Dependency licenses and README files are retained. The previous arm64 debug APK was 177.04 MiB. Use Issue #1 for the size of the new build. The unused in-process shell prototype was removed. Layer thumbnails skip scrollbar callbacks during row rebuilds, the rail enforces its existing 500-card budget, and Delete checks that the original canvas is still active after confirmation.

Settings includes a searchable Keyboard shortcuts editor for native and Studio commands with saved overrides, conflict checks, clearing and reset. Touch, Pressure & Buttons includes optional Android Back protection for the drawing canvas. Android draws around camera cutouts by default and uses cutout margins for toolbar and Home controls. The APK also omits unversioned OpenEXR library aliases while retaining the runtime libraries. Device verification is pending.

## Brush picker and contextual controls

Contextual tool options use compact lists or sliders anchored to their toolbar buttons. Gradient and pattern choosers also open as dismissible panels while retaining the native resource controls. The brush drawer has a scrolling category rail and a separate searchable list with full-width preset rows, previews, stars, and per-mode recents. A toolbar tap switches modes or toggles the drawer for the active mode. Tapping an inactive preset selects it and keeps the drawer open. Tapping the active preset or the pending Blend conversion row again closes it. Each mode remembers its last category. Custom presets can be moved into built-in brush families or named categories from the menu beside the favorite star. Quick Brush Settings includes tip angle for compatible tips, Color Smudge strength and merged-layer sampling, and radius for dulling presets. The row menu also opens duplicate, revert, rename, and delete actions. Factory originals are protected from rename and deletion in the Android resource model and can be restored from shipped settings. Favorites and Recent respect the active mode’s preset capabilities. Paint and Erase omit blend-only presets, while paint-capable wet brushes remain available in both. Blend includes native blenders, Filter Brush presets using Krita’s blur filters, and brush families with compatible tips. Native blur presets appear in Blenders. Quick Brush Settings stacks Size above Opacity and exposes blur radius or motion length and direction for supported filters. Blend can create a separate Color Smudge preset from a compatible native brush tip. The conversion copies shared size, opacity, rotation, and scatter dynamics when present, and specialized engine effects remain specific to their original preset.

The brush picker links directly to Advanced Brush Settings. Move to Category lists built-in families and custom categories in a scrollable dialog. The optional One-shot eyedropper toolbar command samples the next canvas press or a drag release and switches between visible colors and the active layer on hold. Holding Reference images toggles the native reference decoration.

The horizontal Layers rail uses narrow rotated cards with rotated previews, badges, and inline controls. Group cards have separate eye and open/closed folder buttons. The folder uses Krita’s native collapsed state. The Filters menu applies to the active layer. Use a filter mask on a group or a filter layer above the layers it should affect for non-destructive multi-layer filtering.

The toolbar Add picker defaults to Common Tools, including Studio commands, with a searchable All Tools tab. The Add dialog focuses search and requests the Android keyboard.

Brush capability checks are cached across picker instances and run on demand. Automatic preset warmup on canvas entry was removed after a reported delayed crash. An Android crash log was unavailable to confirm the cause. The picker paints its shell before attaching the native list, and queued opens are tied to the latest mode request and active document. Advanced Brush Settings is a fixed action below the scrollable quick controls. Workspace Settings separates panels from toolbar gestures. Studio settings and command lists share dark menu styling and screen-bounded sizing.

New Canvas includes a current-display pixel preset and more phone, tablet, square, and print sizes. The toolbar selector opens beside its field. The armed eyedropper previews pen hover, Reference Images indicates visibility, and the Tools picker accepts pen swipes starting between buttons. Clone Brush is discoverable as Clone Brush · Set source in Tools. A small Set Clone Source button appears on canvas while the clone preset is active.

## Compact Tools picker

Tools is a nonmodal canvas panel. Pressing its opener again closes it, as with the brush drawer and contextual menus. A press on the canvas or another control dismisses it and continues to the original target. The picker displays all of its tool and command groups in one scroll area. Each tile can be starred independently of activating it, and favorites are saved by native ID and pinned above the groups. Advanced Tool Options remains available from the contextual gear and its existing toolbar command. Back or Escape also dismisses the panel.

## Native multi-document workspace and regression coverage (November 12)

Independent Studio now supports multiple real Krita documents in one native view stack. The runtime's single-document guard was removed in `f1421d0`, and the **document service** was updated in `9d7c50f` so New/Open **does not close prior canvases** and reopening a project already open in an inactive tab activates the existing view, preserving its unsaved edits (including equivalent Android document URIs). Both revisions passed the user's WSL Android compilation; neither was device-tested. The native view stack refuses duplicate document ownership, and its destruction observer clears closed views. Provider document names now display in the canvas switcher (`d4190eb`, pending verification at handoff).

Regression test sources and runners cover view-stack registration, switching, modified titles, cancellation, external destruction/reopening (`tests/mobile_native_view_stack_regression.cpp`) and native layer reorder positioning (`tests/mobile_layer_reorder_regression.cpp`). These desktop Qt/C++ tests have not been executed through the WSL Android watcher; they must not be claimed as passing. Prior isolated reorder algorithm simulations passed but do not validate the integrated Qt test. Document navigation now exposes a live **Documents → Switch Canvas** submenu with selected/modified indicators, plus **Previous Canvas** and **Next Canvas** commands available to customizable Quick Actions (`660cb4e`, `c36b8c2`). Workspace refresh after switching is sequenced behind Krita's queued native activation and checks the selected view again before updating overlays (`a8d49b4`). These follow-up changes were not yet WSL-verified at handoff. Check the exact latest commit status, then investigate per-document Save/Close and selection/transform persistence at source level. Do not request device testing or publish an APK without a separate user request.

## November 12 active-view and archive checkpoint hardening

After the missing-header build fix compiled successfully, the local WSL watcher reported a later failure for `36c2d6a` (Issue #81), **after all C++/native targets built** and during `androiddeployqt` packaging. The short diagnostic excerpts show many dependency lookup warnings but do not establish the final packaging root cause. Do not confuse this with a C++ compile failure or call the full Android build successful. Recheck Issue #1 on the latest exact SHA and inspect a new failure's full diagnostics before modifying packaging scripts.

Native close safety now refuses fallback to inactive document identities when no Qt canvas is selected (`6848cdb`) and has regression cases for zero/multiple active canvases (`cdd4869`). Periodic version archiving now rate-limits only against a recent structurally complete KRA/checksum pair (`4e2bd3b`, `c603d76`); an invalid/missing digest does not suppress a new recoverable checkpoint. The archive regression source includes the damaged-checksum/spacing scenario (`0014391`). These changes have not yet been independently compiled or host-test executed. Continue checking WSL build results, then assess transform/selection stability.

## November 12 build recovery and close/dispatch safeguards

Local WSL Android build `2e35a9f` failed because the source installation pipeline omitted the new `open_document_destination_policy.h`; diagnostics are in GitHub Issue #80. Commit `fe011c8` installs and validates that header in `apply_mobile_shell.py` and **passed WSL compilation**, confirming the build blocker was resolved. Subsequent `view_identity_guard.h` changes reject native Close when the active Qt view and Krita document identity disagree (including when only one view remains but the engine has no current document). `tests/mobile_native_view_stack_regression.cpp` now covers that transition plus deferred document commands during rapid tab switching. These later tests have not yet been executed via a desktop Qt runner; the latest code commit requires its own WSL verification. Continue inspecting native Save/Close/Undo identity binding and non-destructive transform tool switching, without APK upload or phone tests.

## Multi-document Save As and Export collision protection (November 12)

The native multi-canvas document service now rejects Save As or Export Copy destinations already backing a *different open canvas* before any native disk write. The shared `mobile_core/open_document_destination_policy.h` compares canonical local paths (including symlink aliases and unsaved export paths) and Android SAF document identities (including equivalent tree/direct URIs). Exporting onto the current drawing is also rejected by the same policy. `document_service.h` shows provider-aware project names in title and open failures. Commits `d06d19d`, `f4ec114`, `c652da3`, `dde1ebc`, and `70a4ccf` implement the guard and regression cases. These revisions are **not verified by WSL** until their exact SHA appears with success under local build Issue #1. Desktop archive policy tests still require a separate host Qt execution. Next: verify the Android build and any compiler diagnostics; continue native multi-document save, close and transform integrity audit.

## Background version archive concurrency and safety (November 12)

Version history now coalesces concurrent periodic/background archive requests and repeated explicit Saves without locking the UI against large KRA copies. Previously discarded or invalid drafts do not satisfy rate-limited snapshots; new KRA copies require committed SHA-256 checksums. History migration keeps KRA/sidecar pairs intact and handles filename conflicts. Recovery rejects symlinked KRA or checksum entries. Tests under `tests/mobile_archive_regression.cpp` cover deduplication, corruption, retention, identity normalization, migration, and collisions. These desktop regression tests have not yet been executed; the WSL watcher compiles Android code only. Continue investigating any WSL compiler failures and validate multi-document native activation/closure and selection/transform behavior at source level.
## S Pen contextual scrolling and brush editor/browser improvements (November 12)

User reported horizontal contextual tools drag correctly with fingers/pen on buttons but S Pen drags on empty grey background did not pan; `context_tool_bar.h` now handles mouse/pen drags from the scroll viewport and empty content background, modifying **only the horizontal scrollbar** while preserving button clicks. Existing finger QScroller remains in place. Native Advanced Brush Settings now displays a larger ~26px checkbox with a ~40px hit zone in each option row; pressing it changes the **real checkable Krita model** directly without opening the detailed option page. Pressing the rest of the row continues to open the native settings. This patch is in `apply_mobile_shell.py` (Krita UI source installation), not a duplicate settings engine.

Brush chooser `mobile_core/brush_workspace.h` now has **Favorites**, per-Paint/Erase/Blend **Recent** lists (bounded to 16), previous Painting/Erasers/Blending/Clone and All categories, inline star toggle on each brush tile, and a Favorite current brush action. Recent and Favorite categories follow meaningful saved ordering and names remain native Krita presets. Selecting a preset in Paint/Erase uses the existing engine with eraser mode **off/on**, respectively; Blend remains **native Color Smudge compatible only**. Because ordinary Paint and Color Smudge have incompatible engine-specific preset settings, do **not** claim an arbitrary Paint preset can safely be converted automatically; Blend offers "Create blending brush…" using Krita's native Color Smudge editor and Save As instead. Native Clone Brush engine remains a separate tool. Changes `cb3063b7`, `890d3ac5`, `bebbe5e7`, `6e1b78e3`, `90ae035e`, `64330644`, `24079054`, `a96258fa` (refresh favorite stars and clear tap suppression after event dispatch). Exact Android build should be checked on Issue #1 (the initial scroll and checkbox commits have passed). Device gesture, star hit detection, and native editor behavior are not yet verified.

## Selection bar replacement and compact Transform cleanup (November 12)

User reported the Krita 6 floating Selection Action Bar obscuring canvas, low perceived native transform frame rate, redundant rotation command, unhelpful Cancel actions, and wanted the full tool options shortcut back. `mobile_core/krita_native_workspace.h` disables Krita's `selectionActionBar` through `KisConfig(false)` + config notifier and creates an 85x36 selection-aware Deselect overlay (native `deselect` action) at a collision-aware canvas edge. It shows only when the current view has a nonempty native selection and hides on document closure/canvas-only mode. `mobile_core/context_tool_bar.h` restores the shortcut to native Full Krita Tool Options, removes redundant Rotate menu from compact Transform, keeps native Mode/Flip/Reset/Done, uses native button-box Cancel only if available on Transform, and retains actual Finish/Cancel for path-building tools. Generic selection Cancel was removed because `requestStrokeCancellation()` after a completed selection is normally a no-op. Selection menus now include Fill foreground and Copy to new layer in addition to invert, selection adjustments and conversion. Context options refresh timer is relaxed from 180ms to 400ms and redundant same-tool refresh traversal removed; **this reduces UI polling but is not proof of higher Krita transform rendering FPS**. Source commits `c20dcea8`, `eb495f3a`, `db2ba22a`, `a5047985`. Intermediate WSL build failures reported in GitHub Issues #78/79; constructor, `KisConfig` argument, and nonconst `selection()` issues were corrected in later commits. Verify exact latest SHA in build Issue #1 before reporting compiled success; Fold touch and transform responsiveness are still unverified.

## Rail header alignment fix (November 12)

The Layers Add/Minimize controls now share the rail's short dimension instead of being permanently vertical: **vertical rail = side-by-side buttons; horizontal rail = vertically stacked buttons**. In every case Minimize is smaller and resides on the screen-edge side of the Add button (right rail: right; left rail: left; top rail: top; bottom rail: bottom). Add fills the remaining short-axis space. `mobile_core/layer_workspace.h` updates the same header layout when orientation, thickness, or facing side changes via `setRailGeometry`; existing Add/hold-for-types and collapse behavior are preserved. Commit `dca48350`. Check build Issue #1 against exact SHA before claiming Android compilation.

## Compact horizontal contextual tools follow-up (November 12)

User requests after comparing ReveriePaint's Transform bar: default toolbar swipe assignments **Paint → brush size** and **Color → brush opacity**; Color's perpendicular drag-to-sample remains intact. In the on-canvas contextual subtool bar, remove the redundant native Tool Options gear (full native options remain in the main toolbar), keep the QScrollArea **one fixed-height row with horizontal scrolling only**, reduce chip size and text to phone-friendly 11px, and style critical Cancel/Done actions distinctly. Transform modes remain one nested, touch-safe menu (Standard/Perspective/Warp/Cage/Liquify/Mesh); Flip and Rotate each get a nested action menu rather than separate buttons, followed by Reset, Cancel, and native Done/Apply at the end. Mode chips show just the active choice, with full context retained as tooltip/accessibility text. The transform mode popup uses the same touch-safe picker as the other nested options, avoiding narrow desktop-style QMenu popups. Selection/path use Krita's own native stroke cancellation and completion, no parallel command implementation. Changes in `mobile_core/toolbar_drag_gesture.h`, `context_tool_bar.h`, `krita_native_workspace.h`: `34a10f25`, `8ab18fb9`, `a615a69f`, `9ada717e`, `f9098e37`. New defaults apply only if users have not saved another assignment. No Fold runtime verification yet: check local build Issue #1 against exact final SHA.

## ReveriePaint-inspired touch tool UI (November 12)

Adapted design patterns from open-source ReveriePaint's All Tools and bottom tool-option panels without copying its Compose UI or replacing our Qt/Krita integration. `mobile_core/tool_browser.h`: categorized drawing tools first (Paint and Fill, Shapes, Selection, Transform, View and Guides, Other drawing tools), compact two-column icon/label rows instead of tall four-column tiles, elided names with full tooltips/accessibility, retained search, plus expandable "More Krita tools and commands" for every less-used native action. The existing "Krita Tool Options" button still opens the complete native KoToolDocker. `krita_native_workspace.h`: default contextual options bar is bottom-anchored inside the usable window; user-edited placements remain unchanged. `context_tool_bar.h`: selection/transform Cancel invokes native `requestStrokeCancellation()` (does not replace Krita history), and the full Krita options gear is labeled accessibly. Existing nested categorized selection/transform/fill menus remain grouped, not expanded into many independent buttons. Source commits `89cc04e8`, `9e9f2119`, `60b69b87`, `a8174230`. Check exact revision in WSL build Issue #1, and verify on Android when user is ready; do not claim mobile interaction PASS from compilation alone.

## Stability and optimization pass (November 12)

Source fixes after `d95daa6`: `87e68858` coalesces overlay LayoutRequest/resize invalidations and blocks reentrant geometry placement, reducing repeated panel solving during screen changes; `41016c82` stops hidden brush-control refresh timers and avoids rebuilding brush blending-mode presentation when the native color space/mode did not change; `a021137b` drops stale live Fill preview masks on threshold change and handles a null native flood-selection result safely. \`70898841\` also suspends panel-fade timer wakeups when Android is backgrounded and resumes them on foreground activation. These changes are narrow and preserve actual Krita documents, strokes, gestures and storage. **Check Issue #1 for the exact WSL Android compile/test result**; no physical Fold verification is claimed. \`2829b187\` extends pure geometry regression coverage to portrait phone, landscape phone, square foldable-like, and tablet viewports for both rail directions. Continue auditing actual performance bottlenecks before making risky threading or document-lifecycle changes.

## Compact Layers and brush blend follow-up (November 12)

User device screenshot showed horizontal Layers Add/Minimize adjacent, excessive short-axis gutter and reversed name orientation. The compact Layers header now keeps Add above a smaller Minimize button regardless of rail orientation, rotates the horizontal active-layer name counterclockwise, narrows outer margins and scales tiles closer to the short axis. Brush Settings replaces the flat blending-mode combo with a touch-scrolling category tree matching Layer Blending Mode: Favorites first, nested native color-space-compatible categories, checked current mode and native brush resource assignment. Android default two-finger tap now maps to Undo, while one-finger double-tap continues to mirror/flip the view around the tap. Explicit saved gesture bindings remain unchanged. Source commit sequence `6f8c6736`, `acacda70`, `41187f3c`, `e355d3f6`; check build Issue #1 for the exact compiled SHA. Fold 6 verification pending; do not request manual tests until user is ready.

## Adaptive Android workspace

The built-in portrait workspace puts the ten essential commands along the bottom and Layers at the top. Landscape and near-square windows use the left toolbar and right Layers rail. The toolbar fits a single strip by adapting icon and touch-target sizes, with scrolling as a fallback when fewer than about 308 logical pixels are available. Layers expand with content, collapse at their anchor, and place the tile controls inward. The horizontal rail uses a rotated name at the end. Edit UI Layout adjusts rail thickness and offers per-screen reset and layout copying.

Screen profiles match tolerant logical dimensions and orientation. Keyboard transitions are excluded from profile switching while Android reports the input method visible. Existing geometry is migrated into the first matched screen profile and the old settings are retained. Layer collapse, floating slider visibility, toolbar layout, and panel placements are restored per screen. Input preferences remain global. Android folding, insets, and keyboard timing require runtime verification.

New defaults enable stroke-color history, Paint size drag, Brush Settings opacity drag, Color sampling, and panel fading to 30% after three seconds. The persistent wheel and floating sliders start off. One-finger double-tap mirrors the view around the tap location, while two-finger tap defaults to Undo. Native pinch continues to change the view. Finger pressure retains its existing 1–10 calibration and volume uses Undo/Redo with a 200 ms hold threshold. Explicit Studio preference overrides remain in effect.

## Mirror anchor and brush color history

Studio Mirror View keeps the latest visible pen or brush point fixed using Krita's native view transform. A one-finger double tap keeps the tapped point fixed. The anchor can be disabled in Touch, Pressure & Buttons. The default single tap no longer opens the popup palette, which is still assignable. Existing explicit gesture choices remain in effect.

The wheel and toolbar share the last eight colors used in completed freehand brush strokes. Color picking, swatch taps, Clone source picking, and erasing do not add entries. The new history starts fresh because old entries were recorded on foreground changes. Android input and painting behavior still require device verification.

## Checkpoint lifecycle audit

Studio coalesces foreground loss and waits for the current native save before requesting another checkpoint. Native autosave records the project association before export, validates the staged KRA, and archives only after atomic publication. A failed export or rename keeps the last completed draft. On reopen, Krita tries structurally readable checkpoints newest first before the original file. The 15-second timer remains the primary recovery opportunity because Android can terminate without delivering shutdown callbacks.

Home thumbnails are keyed by source and revision. Work from an older Home view cannot publish over a refreshed preview, the preview queue is capped, and Version History loads its archive list and cached thumbnails off the UI thread.

## Native layer target audit

Transform and Move preserve explicit layer selections across activation, reject targets from another document or removed nodes, and keep Edit Selection available as an explicit native mask target. Native Krita still controls stroke cancellation, Undo, and the eligibility of hidden and locked layers. Layer drops validate ancestry and sibling anchors after clipping groups change. All native mask types use Krita's parent eligibility rules for drag and drop.

## Native Fill tolerance and guide access

Standard Fill and Contiguous Area Selection each offer opt-in **Drag tolerance**. The gesture keeps its original seed, shows the threshold and a tinted affected-region preview, and applies one native undoable operation on release. Contiguous selection captures the native combine action at press. The preview uses a bounded 384 × 384 pixel window and coalesces threshold changes while the native flood engine computes on a worker. Current-layer and all-layer sources are supported. Color-labeled references and wrapped Fill show numeric feedback only. The committed operation always uses the full native source and settings, so pixels connected through paths outside the preview window may only appear after release. The gesture does not change the saved tolerance. Android input and overlay rendering have not been verified on the Fold 6.

The Assistant tool labels the native one- and two-point choices explicitly. **3-point perspective** places three native Vanishing Point assistants in one undoable action and saves them in the KRA as normal guides. Placement rejects coincident or nearly collinear points and can be cancelled before any guide is added. Completed points use Krita's existing assistant editor. Dedicated isometric geometry remains open.

Layer Options → More, Select, Mask, and long-press Add use touch-scrolling sheets. Selection combination and common contextual action menus use the same picker. Native action availability and layer/document target checks still apply. Clone source picking is scoped to the active document and preset and samples on release without painting. Duplicate tool-options notifications no longer force a full Layers rail rebuild when the layer state and selection mode match.

## Android app-exit automatic checkpoint (November 11)

User wants drawings autosaved when Android closes/terminates the app, without a new Save prompt when merely pressing system Home; no additional device testing requested now. `mobile_core/krita_standalone_runtime.h` commit `1aa7b7778adb6513a2e4d850c7ea775b2a959df1` reduces the dirty-document private KRA checkpoint timer from 30 seconds to 15 seconds and requests a final checkpoint on Qt `aboutToQuit`, in addition to the existing non-Active Android application state hook. This is *best effort*: Android can kill a process abruptly, so the final event may not run or allow asynchronous save completion. The latest **completed** periodic/background private snapshot is the guaranteed fallback; do not promise saving the very last stroke during a force-kill. Preserve atomic pending-file publication, version archive deduplication and explicit app Home Save/Cancel/2-second Discard. Prioritize engineering-only unit/regression and source audits over new Fold test requests. Check Issue #1 for exact build SHA before claiming compile success.

## Fold 6 handoff follow-up

The prior Fold 6 pass covered responsive layout, Layers resizing and rotation, Workspace Settings, reference gestures, toolbar swipes, native Tool Options and document safety. Advanced Brush Settings was not tested. The next batch makes editable fields request Android's keyboard on finger or pen taps, gives floating size and opacity compact translucent tracks, and allows the Color button to adjust size or opacity along the toolbar while retaining perpendicular sampling and tap-to-open.

In-app Home and Close now share the native save transaction. A dirty canvas offers Save, Cancel, or a two-second hold with progress to discard. Android backgrounding still checkpoints independently. Recent and All Projects include readable recovery checkpoints. Named projects keep their identity, and unnamed checkpoints get a stable, editable display name. Recover Drafts remains available until the unified flow is verified on-device. Home refreshes previews after completed saves or newer checkpoints and bounds its disk cache. Version History shows timestamped thumbnails and opens versions as separate unsaved canvases. Existing archive deduplication and retention apply.

The contextual strip prioritizes the active tool’s common controls. Detailed settings remain accessible through Advanced Tool Options. Test the shortcuts for Paint, Transform, Selection, Fill, Shapes, Assistants, and Reference Images.

Pending Fold 6 checks include finger and pen keyboard activation, both toolbar orientations and Color gesture axes, floating panel reflow, and Advanced Brush Settings. For documents, verify short and interrupted holds, Cancel, successful and failed Save, system Home versus in-app Home, crash recovery for named and unnamed drawings, draft rename across restart, and thumbnail/version freshness after repeated saves. Use Issue #1 for the exact WSL Android build status.

## Responsive canvas and phone brush editor

The canvas reflows toolbars and floating controls on resize, favoring saved anchors and keeping items apart. Toolbars grow to fit commands where possible and scroll on narrower displays. Canvas Only is under **Canvas View**. Fade delay and opacity use touch sliders. Color history shares its toolbar background.

The Android **Advanced Brush Settings** action opens Krita's native editor in a screen-sized dialog with Brush, Test, Presets, and Behavior tabs. Brush categories and native controls use full-width touch views, with a live stroke preview and native preset save/reload actions.

## Layers rail layout

The compact Layers rail grows to fit its visible tiles until it reaches a toolbar, another panel, or the canvas edge. Edited layouts keep the chosen anchor and thickness, with overflow scrollable. **Edit UI Layout → Rotate Layers** switches between upright tiles in a vertical rail and a horizontal strip. A small read-only footer shows the active layer name, with rename still in Layer Options.

## Toolbar color history

The gear menu's **Toolbars** submenu has a persistent **Toolbar color history** toggle, on by default. The strip spans each toolbar's inward edge, including custom side rails when space permits. It shares the wheel’s eight-color history of completed brush strokes. Tap a swatch to apply it in the current Krita foreground color space. Canvas Only hides the strips with the toolbars.

## Follow-up UI debloat (November 11; verified source / build pending)

Continue from prior Codex consolidation at `62d6f0b` (exact WSL Android **SUCCESS**). The original on-canvas Edit UI Layout remains the **only resizing/repositioning/rotation surface**; there is no need to reinstate modal numeric corner/size dialogs. Additional commits now simplify `Workspace Settings` without altering persisted preferences: a **Keep panels open** choice (Neither / Layers / Color Wheel / Both) replaces two checkboxes; a **Floating brush sliders** choice (Hidden / Size / Opacity / Both) replaces another two checkboxes. Native preferences `MobileStudio/keepLayersOpen`, `keepColorOpen`, `showSizeSlider`, `showOpacitySlider` are still independently read/written for compatibility. Toolbar adjustment heading now says “Swipe along toolbar” rather than suggesting only vertical bars.

`Customize toolbar commands` now includes a compact, expandable **Properties** section (toolbar name, button spacing, background opacity) in the existing dialog, **removing a redundant second modal Appearance dialog**. Property edits stay on the currently selected toolbar when switching. New/Edit/Delete toolbar commands, all command IDs, Reset defaults, touch scrolling, and stored `MobileStudio/toolbars` layout remain intact; none of these controls duplicates the on-canvas resize surface. **Check WSL Android Issue #1 for exact build on `2c49a8fd14225ea7d8f07a24e2ffcd41ae811231` or later.** Fold 6 regression: verify all four persistence/slider combinations, inline toolbar Properties including switching between toolbars, Save/Cancel, custom bar rotation/resizing via canvas editor and survival after restart/fold. No APK uploaded.

## Menu consolidation

**Edit UI Layout** is the canvas entry for moving or resizing panels, the wheel and toolbars. Older numeric size and corner menus were removed while existing placements remain readable. Panel fading is in **Settings → Workspace Settings**. Toolbar customization, history, and fallback commands are grouped under **Toolbars**. The command editor has a single **Restore default toolbars** action.

## Reference image double-tap flip regression (November 11, 2026)

User reported **double-tap-to-flip stopped working again**. The legacy `apply_reference_touch_fix.py` gesture updated the reference-image shape on every minor `TouchUpdate`, including sub-12-pixel contact jitter. This changed the exact transform and invalidated its own “stationary double tap” eligibility. Commit `020a17b` stops mutating the shape for movements below the tap/drag slop and expands the two-tap interval default to 350ms with a 36px spatial tolerance. Commit `9d8ed5c` adds explicit **Touch, Pressure & Buttons → Double-tap reference image to flip horizontally** (default **ON**) plus adjustable interval. Commits `2062c7d`/`d0e4c51` decouple flip from the **Finger drag** enable flag, allowing flip even when drag is switched off; they also prevent disabled dragging from accidentally moving the shape. Two-finger resize/rotate still independent. All work continues to use the native reference KoShape and Krita Undo, regardless of currently selected drawing tool. **Source changes require exact WSL Android build verification at `d0e4c5146e4f96f45d6da1de843d37b91f66084a` or newer; Fold 6 runtime test pending.** Device test: add an asymmetric reference image, select Paint, double tap it with slight finger motion; only reference flips horizontally; double tap again restores; Undo/Redo works. Turn Finger drag OFF and verify flip remains functional, then turn flip OFF and verify tap no longer flips. Test ordinary canvas gestures outside reference. No APK upload.

## Toolbar drag previews and direct layout editing

Brush-size and opacity drags are enabled by default and configurable under **Workspace Settings**. Moving the pointer updates a temporary value overlay and releasing applies one native brush-property change. Color sampling remains enabled by default and shows a live ring around the pointer, with the final color sampled on release. Toolbar color history uses a 22 px strip. The separate toolbar shortcut slot has been retired.

**Edit UI Layout** opens an on-canvas editing overlay for visible toolbars and panels. Drag a preview to move it, drag a corner to resize proportionally, and use **Rotate toolbar** after selecting a toolbar. Borders and other elements provide snap targets. **Done** saves relative placements and **Cancel** restores the prior layout. **Customize toolbar commands** is the separate command editor. Verify finger and S Pen gestures and saved placements after folding on the Fold 6.

## New Fold 6 popup regression — additional Layer Options (device report)

User reports **additional options menus open as a thin, narrow, unusable bar** on Fold 6 after the latest touch/menu changes. Scope of which exact menu(s) has not yet been confirmed; likely the `More…` branch of the Layer Options sheet and/or its nested Select/Mask menus. In `mobile_core/layer_workspace.h`, `showLayerActions()` presents a sized `QDialog sheet(this)`, but `result==2` delegates to `QMenu menu(this); menu.exec(point)` and nested `select->exec(point)` / `mask->exec(point)` using a global point from the layer thumbnail. Investigate Android QMenu popup geometry, wrong screen/anchor, parent window, style size hints, and event handling. Ensure menus are properly sized, touch-scrollable and positioned on folded/unfolded layouts; preferably use the shared mobile touch-safe dialog architecture rather than Android QMenu if that is the cause. Preserve native actions, Cancel, submenus, masks and the separate compact sheet. **Not fixed or verified yet**. Device screenshot of affected popup would further narrow the scope; never claim a fix from a source guess.

## Reference-image gestures restored in independent canvas (pending Fold verification)

In the legacy Krita UI overlay, finger touching a reference image could drag it without selecting Reference Images, two fingers could resize/rotate it, and a quick double tap flipped only that reference horizontally; Krita recorded undoable shape transformations and invalidated image overlay cache after manipulation. All this older machinery still runs from `apply_tablet_refinements.py`, `apply_tablet_polish.py`, `apply_reference_touch_fix.py`, **but was inactive in the independent Studio**, because `libs/ui/input/kis_input_manager.cpp` looked for `KisPart::currentMainwindow()` which is null without Krita desktop. Source patch `9cac2a9` in `apply_mobile_shell.py` now looks up `this->canvas()` and `canvas->imageView()->document()` directly. Existing hit testing ensures only contacts beginning on actual reference-image shapes are intercepted; normal canvas gesture/painting elsewhere remains unchanged. Commit `41cc077` exposes `TabletUi/referenceFingerDrag` and `TabletUi/referenceTwoFinger` toggles (both default true) in Studio → More → Touch, Pressure & Buttons. Double tap to flip retains prior 280ms threshold. **Check WSL Android build Issue #1 for the exact code commit `41cc07791172571eebb63a9e5142954ba5650f72` or later. Device-check on disposable drawing: Tools → Reference images → From file; return to Paint; finger drag image, two-finger pinch/rotate image, quick double-tap flips image horizontally; Undo/Redo, KRA save/reopen; ordinary gestures and painting elsewhere unaffected.** No APK published; not device verified.

## Parallel version-history correctness batch
**Additional recovery preflight:** `0c318c5` verifies a chosen saved-version's real byte length (bounded to 8 GiB) and destination disk free space before materializing a separate recovered copy; `78363eb` adds a stale-metadata rejection regression. The latest source SHA requiring WSL validation is now `78363eba66eb7511b9a5f653b39478ea9e536088` or later. This is separate from the advanced Tool Options device test.\n\n
While the Fold 6 user tests the advanced Tool Options height fix, recovery code was improved independently in `mobile_core/saved_version_archive.h`: `bcc2510` verifies actual archived bytes even when a previous SHA-256 sidecar has a *different valid-looking digest*, repairing corrupt metadata instead of creating an unnecessary duplicate. It also rechecks that a private draft has not disappeared/changed during deduplication. `821d1af` makes checksum repair failure a **reported checkpoint failure** rather than claiming success with unusable recovery metadata. `7862574` extends `tests/qt_saved_version_archive.cpp` to assert that corrupt digest metadata is repaired and an intact old copy can still be recovered. **Code compile validation pending: check Issue #1 at `821d1afa15e8995d5f3c5dd108da790d4922fe54` or descendant.** The Qt archive test source is not independently executed until a Qt-capable test runner is available. No user APK upload.

## Native advanced Tool Options collapsed panel — device bug

User reports that opening the **additional/advanced options menus for drawing tools** renders a thin, narrow unusable bar rather than native controls. Investigation found `mobile_core/krita_native_workspace.h::placeDrawingOverlays` set `KoToolDocker` height from `sizeHint()`, which may be only the collapsed title frame (44 px) before Qt lays out native option widgets. `f69ad270` / `cc093efa` change the overlay to use the allocated 440px-or-available viewport height and populate the active tool's widgets before showing it. **Requires exact WSL Android build verification at `cc093efac21bc01a4b7487fd4da6b5a5908936ab` or newer and Fold 6 test**: Tools → Transform → advanced Tool Options gear, then Fill, selections, and assistants. Full panel must be large enough, scroll and display each tool's actual settings. No APK uploaded. Do not claim device fixed until confirmed.

## Immediate tap regression after shared scroll handler

The Fold 6 still missed taps in **Brush presets** and **Home → Recent / All Projects** on `4701864`, despite replaying mouse presses and releases. A mouse-only replay cannot recover a touch sequence consumed before Android/Qt supplies a mouse press. Shared item-view handling now accepts raw touch directly, passes only drags to QScroller, and delivers one native viewport click for a completed tap. Native mouse pairs go through Qt normally and a drag clears the view's pressed index before kinetic scrolling. Tests cover raw Qt touchscreen events, synthesized mouse-only input, duplicate Android-style mouse input, and ordinary mouse input for both view types. The offscreen raw-touch test failed against `4701864` and passed with this implementation. On the Fold, tap and swipe each of the three affected lists and verify layer-options and blend-menu scrolling. Check Issue #1 for the exact build.

## Latest on-device regression report — round 2

**READ [DEVICE_FEEDBACK_2026-11-11_ROUND2.md](DEVICE_FEEDBACK_2026-11-11_ROUND2.md) FIRST** for latest Fold 6 testing against `f3c7d49`: 26 new passes, 9 failures, 22 earlier passes, 102 pending. New top engineering rule: **all scrollable mobile menus and future dialogs must use one shared touch-safe swipe-versus-tap interaction contract**, not separate per-panel workarounds. Layer-options sheet and blend picker accidentally click during scrolling; brush thumbnails waste room and show letter prefixes; brush customize controls need Size/Opacity/Flow together; group header and rename-layout redesign regressed; group borders too weak; clip arrows needed; transparency masks cannot be dragged (actual semantics unverified); brush labels remain unreadable after document switch; unexpected symmetry halo documented with screenshot; repeated boxed-X icon needs identification. Color wheel passes but corner placement and optional history below wheel requested. Group-delete checks currently **PASS** on this device despite earlier intermittent failure. Preserve passing gestures, pressure, numeric fields, clipping functionality and all 102 pending tests.

Round 2 implementation consolidates scroll/tap handling for configured Qt menus and item views, including Layers, Blend, Tools, Brush Presets, and gesture settings. Brush Settings now keeps the native Size/Opacity/compatible Flow together with a preset label that elides at the current width. The rail has compact folder headers, group borders, clipping arrows and valid transparency-mask drop targets. Layer rename remains in the sheet and More menu. Color options include an optional recent-color strip, and the wheel uses the selected corner unless another visible control occupies it. See the Round 2 record for device checks and the remaining symmetry investigation.

## Immediate device-feedback priority (November 11, 2026)

**READ [DEVICE_FEEDBACK_2026-11-11.md](DEVICE_FEEDBACK_2026-11-11.md) BEFORE EDITING MOBILE UI, LAYERS OR DOCUMENT NAVIGATION.** It records detailed Fold 6 user observations, all requested design changes, and priority ordering; no points may silently be dropped. The user has not tested checklist section 4 or later. First fix intermittently crashing group deletion and stale layer tree, missing top toolbar after Documents → Open, Tools touch-scroll regression, and adjust Home to autosave **named** projects while preserving unnamed-draft/explicit Discard semantics. Group and blending UI, brush grid, native mirror echo, gesture touch-vs-scroll, numeric px suffix editing, empty-group drop, first-row Alpha Lock, rename by name tap, and one-finger palette status are also documented.

## Source of truth and build gate

- Independent Android APK target `mobile_studio`, package ID `org.krita.mobilestudio.debug`, based on the actual Krita 6.0.4 paint/resource/tool/document engine. It runs through its own Qt native startup, `MobileLightweightCanvasHost`, `KisViewManager` and real `KisView`, **not** the desktop `KisMainWindow` or MDI. Native C++ engine extraction is in `mobile_core/krita_engine_initialization.cpp` and `apply_mobile_shell.py`.
- The local Windows/WSL2 watcher automatically builds commits to `main` and records the **exact** revision plus `local/android-wsl-build` status in Issue #1. Never infer a newer build from a previous SUCCESS. Do **not** run GitHub-hosted Android builds or upload APKs unless specifically instructed. No need to ask the user to manually pull/build while the watcher runs.
- Round 2 shared touch and layer/brush batches through `0790eac7fd8555e35969a6c9869d842e209d1fb2` passed WSL Android compilation (November 11, 17:41 UTC). Check Issue #1 for the exact build of later commits.
- Cross-compiled APK success **is not** a Fold 6 runtime pass. The `qt_saved_version_archive` regression source was extended and compiled through the Android integration where applicable, but its standalone Qt test binary has **not** been executed here.

## Current application and capabilities

- Normal cold launch enters Home unless opened by a file intent; Home has New Canvas, Browse Files, Recent, All Projects, folder grants and Recover Drafts. Document open/save/Save As/export/close use native Krita engine and Android SAF permissions. Krita owns Save/Discard/Cancel prompts. A separate working canvas uses actual Krita brushes, pen pressure, undo/redo and KRA serialization.
- Touch-first floating controls: configurable icon toolbar, Paint/Erase/Blend preset memory, tool selection/grid, native Tool Settings, round Color wheel, Layers rail, brush settings, canvas-only mode, and keyboard-free dialogs. Native Krita action collections retain menu and searchable command access. Toolbar and panel redesign is intentionally deferred.
- Native layer operations: selectable/drag-reorderable rows with finger and S Pen, visible masks/groups, native clipping via alpha inheritance and hidden wrapper groups, optional multi-layer targets, opacity/blending/visibility/locks, create/duplicate/rename/delete/merge/flatten, Undo/Redo and native image-stroke layer moves. Do not replace Krita's compositor, paint ops, or undo engine.
- Native drawing tools exposed: freehand smoothing, selection combinations and refinements, transform modes/parameters, Fill/Enclose and Fill, gradients, shapes/path Finish/Cancel, Assistants, Colorize, Smart Patch, Measure, Reference Images, SVG text, and Clone source selection. The Clone source action was newly exposed as `mobile_set_clone_source` in searchable Tools.
- Recovery: every dirty canvas has a private completed KRA draft, periodically refreshed and requested on app background. Previous completed saves/drafts have atomic private versions, SHA-256 checksum sidecars, duplicate suppression and up to 12 versions / 512 MiB per project. History is keyed by canonical local/SAF document identity and migrated on first Save As. Old copies open as a separate unsaved canvas; explicit Discard removes the working draft. Background process death cannot guarantee a final save beyond the most recently completed checkpoint.

## Fold 6 stability batch (November 11)

- Group/layer removal targets the live image and lets Krita finish its native remove/Undo stroke before rebuilding and validating rail selection. Group deletion uses Krita's subtree removal. Document activation restacks the command toolbar and dismisses its old menu. Home saves named native projects and writes a complete private KRA draft for unsaved drawings before closing. Explicit Close still uses native Discard. Tools uses a normal touch-scrolling dialog. Check Issue #1 for the exact WSL build. Fold validation remains pending.

- Tools and noneditable drop-downs distinguish a tap from a drag. Numeric spin boxes select the editable value on first focus. The preset picker has a denser grid, elided names and live Size, Opacity and compatible Flow controls. Layer options provide direct rename and visible Alpha Lock state, a larger opacity slider and native color-space-filtered blend categories. The compact rail has folder headers, nested group guides and an outlined target for dropping into groups. Clipped layers identify their closest base in layer options and accessibility text.
- The app currently keeps one active drawing open. The native view host can manage several views, but changing the single-canvas workflow needs a deliberate document-switching and recovery design.

- On canvas activation Studio refreshes brush mode, floating size/opacity and open Brush Settings. Tap **+** for the anchored Layers and Masks list. Mask tiles identify the mask type, and their options expose the owner, native properties, and Move Up/Down. Hold briefly and drag onto a compatible layer to attach the mask or onto another mask to reorder it. The selected layer sheet exposes grouping and names the clipping base next to its clip action. The Fold feedback record includes [native layer and mask checks](DEVICE_FEEDBACK_2026-11-11.md#native-layer-and-mask-checks).
- The default Krita input profile maps a one-finger tap to the native popup palette while finger painting is off. The binding is also available under **Touch gestures → One-finger tap → More actions → Show popup palette**. Symmetry actions use Krita's native axes; the reported halo still needs a controlled preset comparison on the same painted layer and on a separate layer, including stock Krita.

## Latest changes (November 11)

- Archive restoration and version listing now serialize with archive pruning/migration; migrated filename collisions preserve both documents. Restore verifies ZIP header, byte count and stored SHA-256; damaged copies cannot suppress fresh checkpoints. Missing valid checksum sidecars can be repaired atomically.
- Reopening a named project skips a damaged newest draft and can use an older valid draft. Startup auto-restore rejects invalid headers; damaged drafts remain manually accessible. Qt tests include same-length checksum corruption, collision migration and newest-draft fallback.
- More layer actions validate the active canvas/target after dialog returns; drag/drop and deferred clip moves validate parents and refuse unsafe switches while a transform is pending.
- Native text/assistant/reference tool ID namespace variants, numeric popup focus, Transform menu callbacks, and Paint/Erase/Blend active-state identity have been hardened. Clone source is now a registered/searchable native Studio command. Provider URI query/fragment differences no longer fork canonical version-history keys.

## Real Fold 6 test evidence and outstanding gates

- **Previously device-PASS:** independent Android app launch, stylus drawing and pressure, KRA save/reopen, Android file sharing; basic selection; finger and S Pen layer drag, Undo/Redo, compact rail overlay, nearest-base clipping, grouping, lock/alpha/opacity, merge/flatten and many core layer interactions. See archived development log for test dates and exact APK revisions.
- **Do not mark device-PASS yet:** latest Transform and multi-layer actions after historical crash/empty-layer bug; native masks and mask KRA round-trip; new automatic crash/background draft recovery/explicit discard; restored-version behavior and SAF URI variations; full native Fill/clone/shape/assistant tool handling; popup focus and current native tool state.
- Next practical *disposable drawing* device batch after a confirmed APK: (1) paint → marquee → Transform Free/Perspective → commit/cancel → layer reorder and Undo; (2) two layers clipped to painted base, mask edits, save/reopen KRA; (3) dirty untitled and named drawings → background/relaunch → history/Save As, then explicit Don't Save; (4) Clone source in Tools → source tap → paint; (5) picker direct/tree URI variants.
- No need to interrupt current source work solely for device tests. Prefer grouped requests, and never test on irreplaceable artwork.

## Development priorities and code map

Keep native selections/transforms and layers/masks reliable; then complete native painting tools and the automatic version-history workflow. Home, brush/input and performance fixes can accompany those phases. Interactive fill tolerance and other Infinite Painter-inspired conveniences remain deferred. Do not redesign the interface prematurely.

- `apply_mobile_shell.py` — patch pinned upstream Krita and wire Android source target.
- `mobile_core/krita_standalone_runtime.h`, `krita_standalone_pilot.h`, `krita_standalone_documents.h`, `engine_bridge.h` — engine, canvas and document actions.
- `mobile_core/layer_workspace.h`, `brush_workspace.h`, `context_tool_bar.h`, `tool_browser.h` — mobile layer, brush, and native-tool access.
- `mobile_core/mobile_draft_recovery.h`, `saved_version_archive.h`, `document_home.h` — private recovery/checkpoints and Home.
- `tests/qt_saved_version_archive.cpp`, `mobile_core/CORE-FUNCTIONALITY-GATES.md`, `mobile_core/TEST-PLAN.md` — regression and release gates.
