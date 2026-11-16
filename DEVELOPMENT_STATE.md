# Krita Mobile Studio — current development state

## November 16 — guard failed native root rollback against host teardown and canvas transfer

- Audited `MobileLightweightCanvasHost::discardEmptyNativeDocumentRoot()`. Its `takeCentralWidget()` reparents the central canvas and can notify Qt observers that close the host, destroy the candidate, or adopt it in another window. The old implementation immediately accessed host members, cleared the adapter, and deleted the returned raw widget after that operation. The rollback now captures `QPointer` identities before the Qt call, verifies the original host/root survive and the detached root is still unparented, then clears the adapter and destroys only that still-unowned canvas. Source `156a7f3`.
- Added Qt-only coverage for real central-widget detachment followed by transfer to another parent, confirming the old host cannot retire it and its adapter becomes inert once deleted; plus host/root destruction lifetime checks (`bde6f50`). These tests exercise Qt ownership and guard policy, not a synthetic destructive event filter inside `takeCentralWidget()`. Exact-revision PC build/test execution pending; no APK uploaded.

## November 16 — avoid destroyed Open-dialog parent after Android display-name lookup

- Audited `MobileDocumentService::openFiles()`' failed-Open warning. It previously evaluated the raw `window.data()` and `MobileSavedVersionArchive::projectName(path)` in the same `QMessageBox::warning()` call. Android SAF/provider display-name resolution can enter Qt callbacks that destroy the originating window before the modal warning is created; the argument evaluation order is not guaranteed. The code now resolves the name separately and rechecks `QPointer<QWidget> window` after the lookup before passing it to `QMessageBox::warning`. Source `f15e313`.
- Added Qt-only regression checks for a name resolver that destroys the parent and a surviving-parent control (`54b7b01`). These validate callback ordering/lifetime policy without opening a modal Android warning. Exact-revision PC build and test execution pending; no APK uploaded.

## November 16 — ignore expired first-swap deadlines from replaced GL surfaces

- Audited `MobileKritaNativeViewFactory::scheduleActivation()` first-frame recovery. The delayed timeout retained a guarded `QOpenGLWidget` and validated the selected native view/document, but not whether the view's `canvasWidget()` was still the original watched surface. Android can replace the GL widget while keeping the old QObject alive; a timeout belonging to the stale surface could then request redundant activation of the replacement canvas. The timeout now captures the selected canvas and verifies its current QWidget matches the watched GL surface before rescheduling activation. Source `4545a7c`.
- Added a Qt-only first-swap regression modeling an old live surface with a separate current replacement: the stale timeout leaves the new surface's wait state untouched and skips activation, while a matching timeout allows activation (`e5b952b`, cleanup `3c4923e`). This tests callback policy, not real Android EGL context replacement. Exact-revision PC build/test execution pending; no APK uploaded.

## November 16 — retain native view backend through switch/close callbacks

- Audited `MobileDocumentService::activateDocument()`, `closeDocument()`, and `closeActiveDocument()`. These methods previously read an open-view snapshot and then called a second service method or `m_viewHost`. If a Qt/Krita selection callback synchronously destroyed the owning UI service during snapshot/current-document lookup, the second member access could use freed memory. Switch and close operations now take one local `QSharedPointer<MobileViewHostPort>` snapshot, use it for selection and action, and retain its lifetime across the native Save/Discard/Cancel callback. The direct view-ID paths and `openDocuments()` also consistently retain the port. Source `411e772`.
- Added a Qt-only integration regression with a real `MobileNativeViewStack` and `MobileNativeViewPortAdapter` where the native close handler releases the owning UI service and destroys its canvas. The local port stays live through close and releases after the transaction (`4634b98`, assertion follow-up `2aeb416`). Exact-revision PC build and tests pending; no APK uploaded.

## November 16 — keep Save and Export backends alive across UI teardown

- Audited `MobileDocumentService::saveCurrentDocument()`, `saveDocumentView()`, `exportDocumentView()`, and `documentForView()`. These queried the backend and native selection, which can deliver reentrant Qt/Krita close callbacks, then dereferenced `m_host` or `m_viewHost` through the original service. If the originating UI/service was destroyed inside a callback, that was a use-after-free risk. Save, Save As, and Export Copy now snapshot both `QSharedPointer` backends before external calls and the selection helper is static, using only those retained backends; the selected `KisDocument` is checked through a `QPointer` instead of a raw pointer. Source `b8174b8`.
- Added Qt-only lifetime-pattern tests demonstrating retained document/view backend objects after a simulated service owner teardown and suppression of Save after its selected document is destroyed (`3266b76`). These model the ownership boundaries, not the Android-only document service itself. Exact-revision PC compilation and tests pending; no APK uploaded.

## November 16 — guard native Open after Android provider URI canonicalization

- Audited `MobileKritaStandaloneDocuments::openFile()`. The native Open path called `MobileSavedVersionArchive::canonicalIdentity(path)` (which can contact an Android SAF provider and dispatch Qt lifecycle callbacks), then immediately dereferenced `m_runtime->documentRoot()`. If that callback destroyed the runtime, the `QPointer` member was null and this was a direct null dereference. Open now captures its runtime and original root before the provider call and revalidates both identities after canonicalization before inspecting existing views or loading a document. Source `43e5f27`.
- Added Qt-only regression patterns for a runtime/root destroyed by canonicalization and a surviving workspace (`19a280a`). These model QObject lifetime after the provider callback; they do not instantiate Android SAF. Exact-revision PC build and test execution pending; no APK uploaded.

## November 16 — retire orphaned Krita images after failed canvas attachment

- Audited both the native `Open` path in `MobileKritaStandaloneDocuments::openFile()` and the `New Canvas` path in `MobileDocumentService::createCanvas()`. If adding a document to `KisPart` succeeded but a subsequent view attachment failed, a reentrant observer could unregister the document without destroying its QObject. The existing `MobileFailedDocumentAttach::rollback()` would skip the now-unregistered document, retaining a loaded image and its pixel/layer buffers despite having no registered owner. Added `disposeUnregistered()` to retire only a still-live, confirmed-unregistered import; both failed-attachment paths call it after normal rollback while `KisPart` remains available. Helper `ef9fd4a`, Open `4c3f1ed`, New Canvas `e2f771d`.
- Extended `qt_native_document_rollback` to cover disposal of unregistered imports, preservation of documents still registered to another owner, and a registry callback that has already destroyed its candidate (`1ebb0da`). The tests exercise the Qt-only guard and deletion semantics, not Android Krita document loading. Exact-revision PC compilation and test execution pending; no APK uploaded.

## November 16 — release partially constructed engine runtime on failed startup

- Audited `MobileKritaStandaloneRuntime::create()`. Its plugin action setup and manager initialization have several early failure returns after allocating a runtime under the still-live host. Those paths previously returned null without deleting the surviving runtime, leaving its action collection, view manager, diagnostic timer, or draft checkpoint machinery allocated until host shutdown and potentially preventing subsequent native startup. A `qScopeGuard` now deletes a still-live partial runtime on any failed return; successful construction dismisses that cleanup before returning ownership to the host. Source `02b7db6`.
- Added Qt-only regression tests checking that unsuccessful initialization immediately destroys runtime QObject children, successful construction retains its host-owned runtime, and host destruction during initialization does not double-delete it (`baaf5d5`). These validate scoped QObject ownership, not the Android-only Krita constructor itself. Exact-revision PC Android build and test execution pending; no APK uploaded.

## November 16 — guard native central widget installation and failed-root cleanup

- Audited `MobileLightweightCanvasHost::installNativeCanvas()` and `createNativeDocumentRoot()`. The former passed an unguarded widget to `setCentralWidget()`, then assigned that raw pointer to `m_canvas`. Qt can issue synchronous ownership/parenting notifications during central-widget installation; an observer can destroy the candidate or host. The caller's failed-installation branch also unconditionally deleted the original `root` raw pointer, risking a second deletion. Installation now tracks the host and candidate with `QPointer` before reparenting and only publishes a surviving central widget; failed-root cleanup deletes a still-live, unowned candidate, and preserves a widget already reparented to its host. Source `fbab3b5`.
- Extended the Qt-only native stack test with rejected external-widget ownership, single-root installation, and empty-root rollback with no retained adapter (`6ea64e0`). These tests exercise normal and rejected ownership paths, not synthetic deletion inside `setCentralWidget()` (which would itself risk invalidating Qt internals). Exact-revision PC compilation/test execution pending; no APK uploaded.

## November 16 — reject closing an obsolete native view adapter

- Audited `MobileLightweightCanvasHost::closeEvent()`. It snapshots `m_nativeViewPort` and closes each document through that shared backend, but native Save/Discard callbacks can run nested Qt event loops that replace or clear the host's native root and its view adapter. A still-live host could finish closing every view from the old adapter and then accept window closure even though the current adapter owned different artwork. The host now verifies that its installed port still matches the original snapshot after `openViews()`, `closeView()`, and the refreshed view list; a replacement cancels window shutdown. Source `8e6307c`.
- Added a Qt-only shared-port identity regression modeling a replacement during a successful close and a stable-port control (`786e2e4`). The test models identity policy; the integration test continues to exercise canceled Close prompts and native view teardown. Exact-revision PC compilation/test execution pending; no APK uploaded.

## November 16 — guard host before native canvas root installation

- Audited `MobileKritaEngineSession::attach()`. The original flow queried the host, called `createNativeDocumentRoot()` (which installs a central QWidget and can dispatch Qt notifications), then created a `QPointer` from the original raw host. If a notification destroyed the window during root creation, even constructing that post-call guard would access a dangling QObject pointer. The host is now guarded before any QWidget queries; each readiness check revalidates the host, and root creation retains a guarded `MobileNativeViewStack` identity before runtime initialization. Source `a3040bd`, initial readiness recheck `43ea8cc`.
- Added a Qt-only test modeling host destruction during root installation and an intact root installation with explicit cleanup (`bf8ddf7`). The teardown case models the lifetime boundary, not synchronous deletion within Qt's real `setCentralWidget()`. Exact-revision local build and test execution pending; no APK uploaded.

## November 16 — guard native workspace initialization after engine/status callbacks

- Audited `MobileKritaStandalonePilot::launch()` immediately after successful `MobileKritaEngineSession::attach()`. Suppressing the Krita status bar and writing the next startup checkpoint can dispatch synchronous Qt notifications that destroy the host or its QObject child session. The pilot previously continued dereferencing raw `window` and `engineSession` pointers to acquire services, view manager, and construct mobile workspace. It now guards the original host, engine session, and view manager through those boundaries and returns without constructing workspace if any dependency is destroyed. Source `86cd123`.
- Added Qt-only lifetime-pattern tests for host/session destruction during the chrome handoff and the surviving-host path (`bb30a23`). These tests model the QObject lifetime check, not the full Android Krita pilot. Exact-revision PC compilation/test execution pending; no APK uploaded.

## November 16 — protect parent-owned Save adapter during native services bootstrap

- Audited `MobileKritaStandaloneDocumentsBootstrap::create()`. It constructed a `MobileKritaNativeSavePortAdapter` as a QObject child of the Krita runtime, then used raw runtime/window pointers through service acquisition and attachment, and unconditionally `delete saver` on failure. If a nested Qt callback destroyed the runtime, its child adapter would be auto-deleted first; the failure cleanup could double-delete that stale pointer. The bootstrap now tracks runtime, original window/root and Save adapter with `QPointer`, rechecks them after acquiring ports and attaching services, and deletes only a still-live adapter on failure. Source `60e30c3`.
- Added Qt-only parent/child QObject tests for runtime teardown during failed attachment and a surviving parent requiring adapter cleanup (`d34ef7c`). These model the QObject ownership boundary rather than Android Krita session initialization. Exact-revision local build and regression execution pending; no APK uploaded.

## November 16 — avoid double-delete on failed engine-session startup

- Audited `MobileKritaStandalonePilot::launch()` against `MobileKritaEngineSession::attach()`. Session attachment creates the native root, Krita runtime and documents; several calls can invoke Qt callbacks and destroy the original host before attachment returns null. The failure branch previously unconditionally executed `delete window` on the raw pointer, potentially deleting an already destroyed QWidget. Pilot now captures `QPointer<MobileLightweightCanvasHost>` before attachment, returns if the host was destroyed, and only deletes a surviving host on failure. Source `4be9590`.
- Added Qt-only native host cleanup tests for synchronous destruction during a failed bootstrap and ordinary failure with a surviving host, checking exactly one destructor notification (`b91a0e3`). These model ownership control flow rather than constructing Android Krita session. Exact-revision local build and regression test execution pending; no APK uploaded.

## November 16 — guard first canvas readiness after native root rollback

- Audited the deferred `firstCanvas` callback in `MobileKritaStandalonePilot::launch()`. It previously dereferenced `alive->nativeViewPort()->currentDocument()` without checking whether the view port still existed. A failed independent bootstrap may discard its empty native root before a previously queued startup/intent completion runs, leaving a surviving host with a null view port. The callback now snapshots and checks the port and current document, and checks the host after Qt property/status operations before marking launch ready. Source `9723e0c`.
- Added a Qt native-stack test that creates and then discards an empty root before deferred first-canvas notification, plus a surviving-root normal path (`99503cb`). The test exercises actual Qt native host/port lifecycle, not Krita's Android-only pilot. Exact Android build and test execution pending; no APK uploaded.

## November 16 — cancel deferred Saved versions gallery after Home teardown

- Audited the `Saved versions…` QAction in `MobileKritaStandalonePilot::launch()`. It previously fetched the active `KisDocument` as a raw pointer, looked up the path (potential Android provider callback), then queued `home->showSavedVersions()` using a raw Home capture. If the provider or a close callback destroyed Home or the document, later dereferences could be invalid. The action now retains `QPointer` identities for document and Home, verifies them after the path lookup, and binds the timer to the guarded Home context so Qt cancels it on destruction. Source `b051ebc`.
- Added Qt-only tests for synchronous document/Home destruction during a path query, cancellation of a queued gallery callback when Home is deleted, and normal callback delivery while Home survives (`c1a23ac`). Tests model Qt lifetime and timer behavior, not Android's Krita gallery. Exact-revision PC build/test results pending; no APK uploaded.

## November 16 — protect Home and Android intent callbacks after document teardown

- Audited `MobileKritaStandalonePilot::launch()`. The Home command's deferred close handler called `home->showHome()` using a raw widget pointer after `documents->closeDocumentView()`. Native Save/Discard/Cancel can synchronously destroy the window and child Home during the close transaction, producing a use-after-free. The startup Android-intent callback similarly used raw Home immediately after `attachAndOpen()`, which can dispatch Qt/Android lifecycle callbacks. Both paths now capture `QPointer<MobileDocumentHome>`, validate the window and Home after each destructive call, and suppress stale boot completion. Source `ae6a5a7`.
- Added Qt-only tests modeling window destruction during deferred Home document close and Android-intent attachment, plus the surviving-window close path (`df01bdf`). The tests validate the Qt lifetime boundary, not Android's native Krita dialogs. Exact-revision PC Android build and test execution pending; no APK uploaded.

## November 16 — resolve interrupted native readiness instead of hanging startup

- Followed up on the new `MobileNativeBootCoordinator::poll()` root-identity check. If `centralWidget()` changed or disappeared during the final readiness query, the code returned without calling `finish()` or queuing another poll. The boot attempt therefore remained pending indefinitely despite the coordinator/window still being alive. It now explicitly calls `finish(false)` to clear the pending state and preserve the existing safe desktop fallback. Source `d3f885e`.
- Existing Qt native boot coordinator tests cover normal launch, failed launch and absent host; this specific mid-query widget replacement is difficult to induce deterministically using QWidget's nonvirtual `centralWidget()` access. No synthetic success claim or new test added. Exact-revision Android build pending; no APK uploaded.

## November 16 — guard native bootstrap readiness during window teardown

- Audited `MobileNativeBootCoordinator::poll()`: its readiness test previously chained `QMainWindow::isVisible()`, `centralWidget()`, and the active-modal query, then launched the engine without guarding the original Qt window/root against nested lifecycle callbacks. It now tracks the original coordinator/window/central-widget identities and verifies them between the queries and before the launch callback. Source `0dd31a7`, final readiness recheck `32973f5`.
- Added Qt-only lifecycle-pattern tests for window teardown during readiness and a replaced central widget (`6a5773d`). Existing native coordinator integration tests continue to cover successful, failed, and deferred startup. This does not demonstrate an actual device crash fix. Exact-revision PC compilation and Qt test execution pending; no APK uploaded.

## November 16 — validate recovery checksum before staging an output

- Audited `MobileSavedVersionArchive::materializeRecovery()` after its bounded copy/sidecar checks. It opened a new `QSaveFile` temporary recovery output before validating the SHA-256 metadata. Malformed history sidecars caused avoidable temporary filesystem work on every recovery attempt. Moved metadata validation ahead of destination creation, retaining the same checksum rules, cancellation, streaming copy and publication semantics. Source `5861953`.
- Extended the Qt-only saved-version regression to repeat recovery attempts with malformed checksum metadata, verify no published copies appear, and restore the healthy digest afterward (`3672fb9`). This reduces unnecessary recovery staging activity, not a demonstrated crash or memory leak. Exact-revision PC compilation and Qt test execution pending; no APK uploaded.

## November 16 — cap automatic project-recovery inventory

- Audited `MobileDraftRecovery::checkpointsFor()`, called during project Open. It previously appended and sorted all matching saved-project recovery drafts in memory. A project with a large history of autosave/recovery entries could allocate an unbounded candidate list and make startup recovery less responsive. It now streams the directory with a newest-first 32-entry heap, and skips opening older KRA archives that cannot enter that candidate set. Files excluded from automatic Open remain intact and accessible through the unrestricted manual recovery gallery. Source `7f8a757`.
- Extended the Qt-only saved-version test using the existing out-of-order 192-draft stress fixture. Associate 50 drafts with one project and assert the automatic Open list has exactly the newest 32 in order and the oldest draft still exists. Test `578c460`. This caps candidate-list memory, not total directory scanning or parsing time. Exact Android build and Qt test execution pending; no APK uploaded.

## November 16 — prevent deferred canvas activation after synchronous view teardown

- Audited `MobileKritaNativeViewFactory::scheduleActivation()`, which compared `manager->canvasBase()` against `selected->canvasBase()` in a single expression. A Qt/Krita observer triggered by the first getter could destroy the selected view before the second getter. The deferred activation now captures its selected `KisCanvas2` in a `QPointer`, obtains the manager canvas separately, and revalidates the selected view and original root before any view switch. Source `cfcf937`.
- Added a Qt-only lifecycle regression that deletes the selected QWidget during the modeled manager query and verifies no switch, plus an intact-canvas case (`b0f8955`, fixture ownership correction `a892e1d`). This models the Qt callback boundary rather than running Android Krita's view activation. Exact-revision local compilation and Qt test results pending; no APK uploaded.

## November 16 — avoid stale canvas switch during native Close

- Audited `MobileKritaNativeViewFactory::attach()`'s close callback. It compared `manager->canvasBase()` to `guarded->canvasBase()`, then immediately called `setCurrentView()` when they differed. A synchronous Qt/Krita callback during either getter could destroy the closing view, its canvas or manager before switching to it. The callback now holds the closing canvas in a `QPointer` and checks view, manager and canvas after querying the current manager canvas and before switching. Source `53ec042`.
- Extended the Qt-only native stack regression with a callback that destroys the closing canvas during manager lookup; it must not attempt a view switch, while a surviving canvas may still be selected. Regression `c1c854a`, strengthened in `304d28e`. This models the lifecycle predicate, not Krita's Android Close flow. Exact-revision PC compilation and Qt test execution pending; no APK uploaded.

## November 16 — guard native canvas adoption after Android project title lookup

- `MobileKritaNativeViewFactory::attach()` previously called `MobileSavedVersionArchive::projectName(initialPath)`, which can query Android's document provider, and then used raw `stack`, `view`, and `document` pointers to adopt the native canvas. If the provider or nested Qt observer closed that document/window, the first `adoptView()` call could access freed objects. After title lookup, verify guarded view, document, manager, root, and native Save host identities; adopt through those surviving guards. Source `ea163aa`.
- Added a Qt-only lifecycle-pattern regression that destroys the host during title resolution and verifies no adoption occurs, plus a surviving-path adoption test (`97356e7`). The test does not exercise Android provider JNI or the Krita-only view factory. Exact-revision PC build and test execution remain pending; no APK uploaded.

## November 16 — guard native view attachment after image/registration callbacks

- `MobileKritaStandaloneDocuments::attachCanvas()` previously read `document->image()` and queried Krita registration before passing the original raw document pointer to the native view factory. A reentrant Qt observer could destroy the imported document or replace the native view root during either operation. The attach path now holds `QPointer` identities for the original document, runtime and root, and checks them after both queries before attempting view creation. Code `84818b6`.
- Added Qt-only lifetime-pattern tests for document destruction during the image query, root destruction during registry lookup, and ordinary surviving attachment (`333f4e2`). These tests model guard behavior; they do not instantiate Android Krita document attachment. Exact-revision PC build and regression execution pending. No APK uploaded.

## November 16 — guard Save canvas after reentrant ownership query

- Audited `MobileKritaStandaloneDocuments::writeDocument()`. Several Save/Save As/Export Copy authorization expressions invoked `ownsDocument()` then read `originalRoot->currentDocument()` without checking that the runtime, document and original view root survived the ownership query. Qt registry callbacks can synchronously tear down or replace that root. A shared `stillOwnedSelected()` guard now validates the original root both before and after querying ownership, including the render-dialog, picker, prewrite and save-completion boundaries. Source `c16e1ec`.
- Added a Qt-only model test for synchronous root deletion during an ownership query and the intact-root path (`3b90e80`). It validates the lifetime-guard pattern, not the Android-only Krita Save implementation. Local Android compilation and Qt test execution pending; no APK uploaded.

## November 16 — avoid quadratic preview revision cleanup on Save

- Followed up on the 128-entry revision-map cap from `d423b07`. Trimming one oldest entry at a time repeatedly scanned the entire restored map, which is quadratic if settings contain a large legacy backlog and could stall the UI during Save. `recordPreviewRevision()` now ranks entries once, retaining the newest 128 with deterministic tie ordering; the normal under-cap path is unchanged.
- Expanded the Qt-only regression from 300 to 4096 legacy entries, asserting the current Save and exact most-recent boundary survive. Source `0092715`, regression `8c2c2f5`. This prevents an avoidable Save-time latency spike, not a demonstrated memory leak. Exact Android build and Qt test pending; no APK uploaded.

## November 16 — stop native bootstrap after reentrant host teardown

- Audited `MobileKritaStandaloneRuntime::create()`. Tool-factory `createActions()`, the Qt main-window association, and startup diagnostics can execute Qt callbacks while the runtime and its host are still being initialized. Previously, bootstrap continued dereferencing raw runtime and host pointers without revalidating ownership after these callbacks. It now captures guarded runtime, host and canvas-root identities and exits if they are destroyed or replaced, including between tool registrations. Source commit `d5bee05`.
- Added a Qt-only callback-lifetime regression checking that a destroyed host prevents subsequent registration and a surviving host proceeds normally (`20be7c5`). The fixture models the Qt lifetime boundary; it does not instantiate the Android-only Krita runtime or establish crash-frequency improvement. Exact Android compilation and regression execution pending. No APK uploaded.

## November 16 — bound restored preview revision metadata

- `MobileSavedVersionArchive::recordPreviewRevision()` previously removed only one old entry after inserting a new preview revision. If settings were restored from an older or oversized revision map, hundreds of metadata entries persisted in memory and settings after each Save. It now trims until the existing 128-entry cap is restored in a single pass, preserving the newest timestamps.
- Added a Qt-only regression that seeds 300 legacy entries, records a new project Save, verifies the 128-entry cap and newest/current identities, then restores the prior settings. Code `d423b07`; regression `8dc7365`. No UI or save-file changes. Exact-revision PC build and Qt test execution pending; no APK uploaded.

## November 16 — recheck opened recovery archive size

- Recovery discovery checked an archive's path metadata before opening it, then probed the header without verifying the opened file's size. A file replaced between these operations could bypass the original 8 GiB limit. `MobileDraftRecovery::hasKraHeader()` now also checks the opened `QFile` and its metadata before reading the four-byte ZIP header. This reduces exposure to oversized/stale recovery entries without allocating a full archive. Source `d6de27c`.
- Added a Qt-only sparse-file regression verifying that an archive larger than 8 GiB is rejected without materializing its bytes (`86e636f`). The test does not simulate the replacement race. Exact-revision Android build and Qt execution pending; no APK uploaded.

## November 16 — preserve unvalidated recovery staging on reported export success

- Audited `MobileDraftRecovery::publish()`. Its invalid-KRA branch deleted `.pending.kra` whenever the exporter reported success, even though validation may have observed an incomplete flush or a concurrent writer. This could destroy the only remaining partial unsaved artwork while providing no benefit to the last published checkpoint.
- Preserve invalid staging for inspection/recovery regardless of exporter success, without publishing it. A subsequent export can replace the same pending pathname. Updated `tests/qt_saved_version_archive.cpp` to assert retained staging bytes and an untouched published checkpoint. Source `4312d65`; regression `6d17285`. This is a data-retention safeguard, not proof of a crash-frequency reduction. Exact Android source build and Qt regression execution pending; no APK uploaded.

## November 16 — retire detached documents from deferred autosave watchdog

- Audited the pending-draft retry added in `136ad7c4` and refined by `710c8561`. `MobileKritaStandaloneRuntime::continueDrafts()` previously called `document->isSaving()` *before* checking whether it still belonged to the native runtime. If a live KisDocument had been detached (e.g., moved to another host) but continued saving, its queued identity could remain indefinitely and repeatedly schedule delayed retries despite no longer being owned by the mobile canvas.
- Deferred checkpoint processing now validates `ownsDocument()` first, rechecks QObject/root identities after that potentially reentrant query, and removes any unowned document from `m_pendingDrafts`. Owned documents still wait until their current save finishes. Backoff/timer behavior and the actual autosave operation are unchanged.
- Added a Qt-only view-stack regression modeling a document that remains alive after its view is forgotten; a pending retry snapshot drops the unowned identity even when it still reports saving. The test exercises stack ownership and a model of queue retirement, not the Android-only runtime method directly. Runtime commit `93f70aa5b3bdf0aa43bb147c75a95eded4d3ce07`; test commits `04a2041f910001a452cd03066611689b86b36f76` and `9f0daa43d6fb58e01143d237bb1130025dbae3ac`. Exact-revision compilation and regression execution pending; no APK uploaded.

## November 16 — back off pending background-draft retries during lengthy saves

- Followed up on deferred autosave retry (`136ad7c4`). Android's watchdog checked `m_pendingDrafts` every two seconds until a document's current Krita save finished. This was safe from uncontrolled timer allocation because of `m_draftRetryQueued`, but a long or stuck save still woke the app and rechecked the same document every two seconds.
- Added Qt-only `MobilePendingDraftRetryPolicy` with clamped 2, 4, 8, and 16-second retry delays, remaining at 16 seconds thereafter. `MobileKritaStandaloneRuntime::continueDrafts()` increases this backoff only when a pending document remains and resets it once the pending queue drains. The existing `sigCompleteBackgroundSaving` connection continues to trigger an immediate continuation when Krita reports completed saving; QObject-owned timers are still cancelled on runtime teardown.
- `tests/qt_saved_version_archive.cpp` verifies delay progression, saturation for extreme positive counts, negative input handling, and reset to the first delay. Policy commit `e22e379a678caed35f43ef6b2ae6031726cd9174`, runtime integration `710c8561c09f85c16b0f99699a987f2b6bf01fe6`, regression `f9df338808c2bc3155c40933ae547a473399244e`. The last watcher-confirmed Android build was successful at `1a24b341`; these latest revisions still require compilation and Qt test execution. No APK uploaded.

## November 16 — retry deferred background autosaves after a concurrent save

- Audited `MobileKritaStandaloneRuntime::checkpointDrafts()` and `continueDrafts()`. If Android backgrounds the app while a document's Krita save is already in progress, the runtime records that document in `m_pendingDrafts` and queues one immediate continuation. The continuation can still see `isSaving()==true` and leave the document pending without any further wakeup. That can miss the intended private background checkpoint until another app event.
- `continueDrafts()` now schedules one QObject-owned two-second retry when a pending document remains, guarded by `m_draftRetryQueued` to avoid duplicate timers. When the existing save finishes, a later retry rechecks document ownership and modified state before invoking the usual autosave. If the runtime is deleted, Qt discards the timer automatically. This is a bounded polling cadence, not an immediate recursive retry; the timer may continue while a save remains active.
- Added a Qt-only model regression to `tests/qt_native_view_stack.cpp` checking that a deferred checkpoint does not run during a save, resumes when saving finishes, and does not fire after its owner is destroyed. It models the retry lifecycle; it does not execute the Android-only `MobileKritaStandaloneRuntime` directly.
- Source commit `136ad7c4f852fa2e8191fd15bb0c4fc2653aecd4`; regression commit `6d2ca149559c145162eab102519533b91e0596ca`. The last watcher-confirmed successful Android source was `c86d80ec`; exact-revision compilation and Qt test execution for this change are pending. No APK uploaded.

## November 16 — revalidate autosave staging before publishing recovered artwork

- Audited `MobileDraftRecovery::publish()` after the project-specific streaming optimization. It checked that a `.pending.kra` was readable, then could rename it over the last published draft without checking if another exporter replaced or modified the staging file during ZIP validation. This is particularly dangerous when a large export and Android lifecycle callbacks overlap: an unverified staging file must not overwrite the most recent known-good recovery.
- `publish()` now captures `QFileInfo` for the staged export before validation and requires `unchangedStaging(before)` (same size, modification time, regular file, not a symlink) immediately after validation and before any rename, including the separate failed-export rescue path. If the staging file changed, it is preserved for later inspection instead of risking overwrite of the published draft.
- Extended `tests/qt_saved_version_archive.cpp` to mutate a completed staging file after capturing its metadata, verify the stability check detects the change while the published draft remains intact, and confirm a stable subsequent export still publishes normally. Source commit `d0e6e8f7623e9fc96c9d69b2fa329b4e8039e4fb`; test commit `54643d48271b915ab2135191892ef182759bd307`. This covers the detection predicate, not a fully concurrent export race. Exact-revision compilation and Qt test execution remain unverified. No APK uploaded.
- The watcher confirmed Android source commit `7bf62797` successful at 10:48 UTC (November 16).

## November 16 — stream project-specific recovery without sorting unrelated drafts

- `MobileDraftRecovery::checkpointsFor()` previously invoked `list(0, savedModified)`, which collected and sorted every newer draft in the recovery directory before retaining only those associated with one project. This could allocate a large `QFileInfoList` when a device had many drafts from unrelated paintings.
- It now performs the same bounded orphan `.pending.kra` rescue, iterates completed drafts once, and retains only files with a matching canonical project association, a modification time newer than the saved artwork, and a readable KRA. Only those matched checkpoints are sorted newest-first with the existing filename tie-breaker.
- Expanded `tests/qt_saved_version_archive.cpp` using the 192-draft stress fixture: two matching project checkpoints and one other-project checkpoint test newest-first ordering and the modification cutoff. Source commit `7bf62797e210967f988cd25e8788dd52319115fc`; regression commit `97b7fa6ab9747188a55db649600a007cde9a5ebb`. Exact-revision Android compilation and Qt regression execution remain unverified; no APK uploaded.

## November 16 — bound growing recovery association sidecars

- Audited `MobileDraftRecovery::projectFor()`, which checks an untrusted `.project` sidecar is at most 16 KiB but previously then invoked `QFile::readAll()`. If an external/failed writer expands a sidecar between those operations, the read may allocate based on the changed file size while gallery or project deletion scans many drafts.
- The reader now calls `read(16385)` and refuses a result larger than 16 KiB, preserving the existing `setProject()` write limit and current normal metadata behavior. The extra byte detects growth past the allowed boundary, without allocating an unbounded buffer.
- Extended `tests/qt_saved_version_archive.cpp` with normal association round-trip, 16385-byte rejection, and exact 16384-byte acceptance. Source commit `9ebebfa13ffd767bf05da642de5671aa125e4055`; test commit `a7af074dcf7a19444ecf6acc6f1ca8b0f5803867`. Exact-revision Android build and test execution remain unverified. No APK uploaded.
- Prior cache fix: `c6c66e49` corrected an Android compile error caused by redeclaring the `source` argument as a local `QFile`. The build watcher has so far reported only failures for older revisions `6c284e8e` and `0a16c584`, not the corrected revision.

## November 16 — fix Android build regression in stable thumbnail-file reads

- The local Android watcher reported a **failed build** for `6c284e8e` (GitHub issue #94): the new `QFile source` inside `MobileProjectPreviewCache::load(const QString &source,...)` redeclared the existing method parameter, producing three clang errors and stopping `kritaui` compilation. This was a build-only regression introduced by the preceding cache-prune/read race patch.
- Renamed the local reader device to `cachedFile` and updated its `open()` and `QImageReader` references, leaving the stable open-file-handle behavior intact. Source fix `c6c66e49d6e679b5952ec9a2163a00c516d8930e`. The existing Qt regression for unlinking an already-open PNG remains in `tests/qt_project_preview_cache.cpp`. Exact-revision compile and test results must be checked in the watcher; no APK uploaded.

## November 16 — keep gallery cache PNG decodes stable across concurrent pruning

- Audited `MobileProjectPreviewCache::load()` and `pruneLocked()`. The cache loader previously validated a PNG with `QFileInfo`, then passed the original path to `QImageReader`. Concurrent background gallery cache retention can unlink that file between the two operations, causing a cache miss and unnecessary re-extraction of the source KRA preview (and temporary image allocations).
- `load()` now opens `QFile` before constructing `QImageReader(&source, "png")`. The decoder reads through the already-open file descriptor, which remains usable if a concurrent sweep unlinks the name on Android/Linux. The existing size, symlink and decoder dimension guards, cancellation checks, and retention quotas are unchanged.
- Added a Qt regression in `tests/qt_project_preview_cache.cpp` that opens a PNG, unlinks its pathname where the filesystem permits, then decodes through the still-live file handle. Source commit `6c284e8e436cf0875d656fb23662f3ea6d590999`; regression commit `8030aa65895868f120c09d0e0135e4d6d94ee5fa`. The preceding wide-thumbnail cache fix compiled successfully at `0d1db51a`; exact-revision compilation and test execution for this change remain pending. No APK uploaded.

## November 16 — cache safe wide-canvas gallery previews without repeated full-size decoding

- Audited `MobileProjectPreviewCache` memory and cache admission. Preview decode accepts dimensions up to 8192 pixels with at most 16 megapixels, but `store()` separately rejected any already-decoded image wider or taller than 4096 pixels **before** downscaling to a 124 px gallery icon. A valid 8192×256 source therefore could not be cached, causing repeated full-preview extraction and temporary bitmap allocations on future gallery visits.
- `store()` now reuses `safePreviewDecodeSize(image.size())` to accept only the same bounded 8192 px/16 MP input dimensions that extraction permits. All accepted images are still downscaled to at most 124×124 before PNG encoding, subject to the existing 32 MiB file and 128 MiB cache quotas.
- Expanded `tests/qt_project_preview_cache.cpp` with an 8192×256 input, verifying the cached 124×4 result and color, and adjusted the oversized-input regression to 8193×1 (outside the actual decode policy). Source commit `b35d7181bd43668d435fee101b98003399be7312`; regression commit `0e4e411b3ae8a374932972dec5c9eb1fcbdc0b17`. Source-only and Qt tests require exact-revision execution; no APK uploaded.

## November 16 — guard native first-frame retry across document ownership queries

- Reviewed the first-frame OpenGL timeout path following the earlier reentrant surface-destruction fix. It checked `selected->document()`, then queried `root->currentDocument()`, then read `selected->document()` a second time. Krita host/document observers can close or replace the selected document while resolving ownership, so the second raw lookup could use stale view/document state during graphics recovery.
- The timeout callback now captures the original `KisDocument` as a `QPointer`, validates it and the selected root/view after the first lookup, obtains the host's current document once, and revalidates everything before scheduling canvas activation. Normal retry delays and the GL context backoff policy remain unchanged.
- Added a Qt-only native view-stack regression modeling synchronous document closure while reading the current host document and checking the retry does not activate a destroyed identity. Source commit `42a29d8a2109b6a30549b85f8d79f86f1f8be6a4`; test commit `e1eba68ca073eda78d720bcde2987e28ad98cad3`. The preceding `a59aff0d` Android build was successful; exact-revision compilation and execution of the new regression remain pending. No APK uploaded.

## November 16 — prevent GL first-frame callback null dereference after property notification

- Inspected native first-swap GL callback lifetime. The `frameSwapped` handler cleared `_mobileWaitingForFirstSwap` on the `QPointer<QOpenGLWidget>` and immediately dereferenced `watched->isValid()` without rechecking the guarded pointer. Qt dynamic-property notifications can synchronously tear down the watched surface. In that case the QPointer clears during `setProperty()`, and the subsequent dereference could crash.
- The frame-swap completion path now checks `watched && watched->isValid()` after disarming the flag before touching the retry count. Existing first-frame retry behavior, renderer ownership and backoff policy remain unchanged. This fix is confined to the property-change reentrancy boundary.
- Added a Qt-only regression to `tests/qt_canvas_resume_observer.cpp` that deletes a surface from its `DynamicPropertyChange` event filter when the first-frame flag is cleared, then verifies the guarded follow-up retry reset is skipped. Source commit `eba641bd78c578724a53a798fcd6cb911f6d2d4a`; test commit `226bd7a721ac689bb99a7ff556357bc73fc85185`. Exact-revision compilation and Qt test execution remain pending. No APK uploaded.

## November 16 — remove redundant recovery enumeration on project deletion

- Reviewed `MobileDraftRecovery::hasProjectDraft()` after the prior streaming-lookup optimization. To preserve orphaned, completed `.pending.kra` rescue it called `list(1)` first, which still enumerated all published drafts and performed bounded heap selection. It then enumerated the same published drafts again for the actual existence check. On an unusually large recovery directory, the unnecessary first traversal increases main-thread disk work and latency.
- Extracted the existing orphan-candidate cursor and rescue logic unchanged into `rescueOrphanStaging(folder)`. Full `list()` and `hasProjectDraft()` both invoke it, but the latter now performs only a single streaming enumeration of published drafts, ending at the first matching canonical project association. There is no change to rescue batch limits, artwork data, or the contents of recovery history.
- Added a Qt regression creating an old interrupted `.pending.kra` associated with a project and checking `hasProjectDraft()` still rescues and discovers it, then cleans up the staged and rescued fixtures. Source commit `673e2f0828898093833b01e66b3b0cf8b50ec522`; regression commit `526d5427bbe6b12ade669fb04b14148d1e4f61d1`. Latest exact-revision Android compile and Qt test outcomes need watcher confirmation; no APK uploaded.

## November 16 — saturate saved-version retention accounting for corrupt sparse archives

- Audited asynchronous saved-version checkpoint requests and retention. The request queues coalesce duplicate saves, while `MobileSavedVersionArchive::prune()` could add the file size of an older verified fallback to an already oversized newest archive using unchecked signed `qint64` arithmetic. Abnormal sparse files can report very large sizes; overflowing the accumulator is undefined behavior and could misapply the history size quota.
- `prune()` now caps the initial retained-byte count at `maxBytes()+1` and saturates rather than adds when reserving an older verified fallback would exceed the quota. Full historical enumeration and the explicit guarantee to retain a valid older recovery when the newest is corrupt are unchanged.
- Extended `tests/qt_saved_version_archive.cpp` with a sparse newest archive and older verified fallback; creating another checkpoint triggers retention and verifies the fallback survives. Source commit `07282eaf1a140c719ae826fa9e848d659aed55af`; test commit `e89b0d4bdf24b779378f6a84f34380436ab464c1`. Exact-revision Android build and regression execution are pending. No APK uploaded.

## November 16 — avoid stale document reads during save metadata publication

- Reviewed `MobileKritaStandaloneDocuments::writeDocument()` after completed background-save confirmation. It previously called `MobileSavedVersionArchive::recordPreviewRevision()` and `rememberPath()` before reading `guardedDocument->nativeFormatMimeType()` and its private draft property. Qt recent-file/metadata observers can synchronously close the document or replace the native root, so those later reads risk stale ownership during a completed Save.
- The save-completion path now rechecks the original native root and selected document after `ownsDocument()`, captures native MIME and draft identity **before** publishing preview/Recent Files metadata, and revalidates ownership after each publication step. If the canvas is closed/replaced, history enqueue is skipped rather than reading a destroyed document. Successful Save As/Export Copy behavior is otherwise unchanged.
- Added Qt-only `tests/qt_native_view_stack.cpp` regression modeling synchronous document/root closure during save metadata publication, checking no later document read or history publication proceeds. Source commit `f147dfa352968a97f71c3f73a65cbb38e7cb92de`; test commit `ee2f6bf729cadf556f5d1cf91d40b88fa2fb1661`. Exact-revision Android compilation and test execution remain pending; no APK uploaded.

## November 16 — guard native save ownership before render dialog construction

- Audited `MobileKritaStandaloneDocuments::writeDocument()`, including the save render-completion dialog. The initial conditional previously called `m_runtime->ownsDocument(document)` then dereferenced `m_runtime->documentRoot()` and its current document through raw/unrevalidated identities. If Krita registration observers tear down the canvas during the ownership query, that second dereference risks a stale-runtime or stale-root access.
- The save path now captures `QPointer` identities for the imported document and runtime, validates them after the ownership query, then captures and checks the original native root before creating the save transaction/render dialog. It retrieves the document image through a guarded identity and verifies runtime/root/current-document ownership again after the image query. The existing heap-allocated, guarded delayed-save dialog remains intact.
- Added a Qt-only native-view-stack regression simulating document and root destruction during a save ownership query; the guard must prevent access to the destroyed document image. Source commit `5ae2a3167a959b829493df1a4a954b5e8adbc14a`; test commit `3ac2883f9d48f1d710c9d06ebf026158633a4392`. Exact-revision compilation and Qt test execution are not yet confirmed. No APK uploaded.

## November 16 — verify original canvas root after import MIME callbacks

- The prior document guard in `openProjectSource()` handled an imported `KisDocument` being destroyed during Android provider-name or Krita MIME callbacks, but those callbacks could also destroy/replace the original native canvas root while leaving the document alive. A still-live document alone is not authorization to load a potentially large KRA into a replaced workspace.
- The helper now receives a `QPointer<MobileNativeViewStack>` for the original root and checks document, runtime, root identity, and `KisPart` both **before and after** MIME setup, immediately before `openPath()`. The same check is shared by normal imports and recovery-checkpoint imports; existing caller cleanup disposes of surviving unregistered documents on failure.
- Extended `tests/qt_native_view_stack.cpp` with a Qt-only reentrant MIME callback simulation that replaces the root while preserving the import document and verifies no load starts. Source commit `af483cca3180ce36e5cddfcbfd1095d9d17017a6`, test commit `81a70faba19bedf0daaae69869d888451b4c7d50`. This is an ownership/lifetime guard, not a demonstrated improvement in measured crash frequency. Exact-revision build and regression execution remain unverified at commit time. No APK uploaded.

## November 16 — guard document lifetime during MIME setup before KRA loading

- Audited the native import transaction after the earlier `createDocument()` host-lifetime checks. `openProjectSource()` previously invoked `applyProjectMime(document,path)` followed by `document->openPath()` through the original raw pointer. Android provider-name resolution and Krita MIME notification may reenter Qt callbacks and destroy the unregistered document, so this was a potential use-after-free at the handoff to KRA loading.
- `applyProjectMime()` now returns whether its `QPointer<KisDocument>` survived Android filename resolution, native MIME lookup, and `setMimeType()`. `openProjectSource()` retains a guarded identity and refuses `openPath()` after invalidation. Recovery metadata setup also stops if the MIME helper fails, using its existing pre-registration cleanup. Neither painting UI nor successful file-open semantics are changed.
- Added a Qt-only regression in `tests/qt_native_document_rollback.cpp` modeling an import destroyed synchronously during metadata setup and checking that no subsequent KRA load is attempted. The Qt fixture validates the lifetime pattern; full Krita callback behavior requires device/integration testing. Source commit `b7e5fa1156dfad9b9fe2c276eaeabc528797bc03`, test commit `81de441984f5b69f00c656cfc1f1e244199f7347`. Exact-revision build/test results pending. No APK uploaded.

## November 16 — abort loading if native host changes during document construction

- Reviewed pending Qt callbacks, native view-stack retirement, and the new first-swap observer diagnostics. The view-stack already checks detached-widget ownership before destruction and the recovery worker guards its request queues. No confirmed unbounded leak was established from those paths.
- Identified a document-open lifecycle gap in `MobileKritaStandaloneDocuments::openFile()`: after `KisPart::createDocument()` returned, both recovery-checkpoint and normal-open paths called `openProjectSource()` directly. Document construction can deliver engine/plugin callbacks that close or replace the Android native root before the expensive KRA load begins. Both paths now revalidate the original root/runtime and Krita singleton, disposing a surviving unregistered document before reading when its original host is gone.
- Added a Qt-only native-view-stack regression modeling document creation that synchronously replaces its root; the code must discard the new unregistered document without starting import. Source commit `f5a31bccb0a5c5c71d511147f1baffb79d0fde7e`; test commit `5066d8e1982904c99c5c63adf33ce0b4ca7d63b5`. This is a targeted teardown/stale-host safety change; exact-revision Android compilation and test execution remain unverified at commit time. No APK uploaded.

## November 16 — track pending OpenGL swap observers in opt-in memory diagnostics

- The existing native close-churn regression already exercises 128 consecutive view/document retirements, and `MobileNativeViewStack` uses deferred deletion with ownership checks. The archive history migration also uses a recursive mutex intentionally. No additional confirmed orphan-view leak was established in this pass.
- To distinguish a genuine retained first-frame callback from normal GL/allocator caching, named the short-lived QObject created for each native `frameSwapped` retry `_mobileFirstSwapObserver` and added `pendingFirstSwapObservers` to the existing opt-in `MobileStudioMemory` snapshots. Counts come from objects that remain under the native document root, so they can be compared to `nativeViews`, `retainedKisViews`, and `detachedKisViews` after the existing deferred close-snapshot grace period.
- Diagnostics remain **off by default** under `MobileStudio/memoryDiagnosticsV1`; no image content, file paths, or art data are collected. Normal observer connection, timeout, deletion, and rendering behavior are unchanged. Source commits: `c070177710fbf450935c97332ab2a80d0af2e109`, `2777e65fe52a541675a8b2920cf67530b63c88d9`. Compilation and device memory sampling of these revisions remain pending; no APK uploaded.

## November 16 — stream draft-association lookups to avoid large snapshots

- `MobileDraftRecovery::hasProjectDraft()` previously called unrestricted `list()`, allocating and sorting every private recovery draft, before doing an existence test for one project. That check can run in deletion workflows and may create a large, unnecessary temporary allocation if storage contains many drafts.
- The check now invokes `list(1)` to retain the existing bounded rescue of old publishable `.pending.kra` files, then scans published drafts directly through `QDirIterator`, returning on the first matching canonical project identity. The result no longer requires a complete in-memory/sorted `QFileInfoList`. No drafts are deleted, and full list/checkpoint discovery remains unchanged. This trades a second streaming directory traversal for bounded peak memory.
- Extended the existing 192-draft Qt stress test to associate one non-newest draft with a project, verify the streamed existence check finds it, remove that association, then verify absence. Source commit `b1381989914b0a03da2c59417f791202fd03f71e`, test commit `e414f7560a0d160417faeed149bed8f6ac26c8d2`. Exact-revision compilation and test execution remain pending; no APK uploaded.

## November 16 — bound routine saved-version checks to newest archive

- Investigated autosave memory pressure in `MobileSavedVersionArchive::snapshotFrom()`. Its rate-limit check and deduplication pass previously each called `scan(folder)`, materializing and sorting the entire history even though each uses only the newest completed KRA. A damaged/interrupted history directory can contain far more files than normal retention permits, causing avoidable temporary allocations on an autosave path.
- Added optional `maxResults` to the streaming `scan()` helper. The two latest-only autosave calls now use `scan(folder,1)`, with a one-entry heap and filename-based ordering identical to full history enumeration. Retention pruning and user-visible history still use unrestricted `scan(folder)`; no backups are deleted by the new optimization.
- Added a Qt regression in `tests/qt_saved_version_archive.cpp` that publishes a real checkpoint, stages 48 older archive entries, and confirms a rate-limited snapshot succeeds without changing the version inventory. Code commit `ebbaee5c92d9ae7a26f60232fa37aea90c34aa57`; test commit `f26af092373a194f516123f21518aaed1c814d03`. Exact-revision compilation and test execution remain pending, and no APK was uploaded.

## November 16 — stress regression for recovery-list memory bounds

- Verified the native build watcher had reported a successful Android build at `be3f07e8`, covering the initial 20k gallery bound; the subsequent checkpoint time cutoff still lacked a matching build report during this pass.
- Expanded the Qt-only saved-version regression from a handful of fixtures to a **192-file** published-draft directory with deterministic, out-of-order modification times. It compares the bounded newest-seven results with the unrestricted sorted list, then repeats that comparison with a saved-project timestamp cutoff. Fixtures are dated after unrelated preexisting test drafts so the assertions actually exercise the generated entries. Cleanup removes every generated draft.
- Test commits `07ae87653b0cfd2ea7e9825a15cc97214846e6d5` and `eb5ed66d773d3ba14e600e4ae6d81afeadf034a7`. This change adds correctness coverage rather than changing Android runtime behavior. Execution of the new regression remains pending; no APK uploaded.

## November 16 — discard obsolete recovery entries before allocation

- Reviewed the newest-first bounded heap in `MobileDraftRecovery::list()` and confirmed that the most recent Android watcher report compiled regression source commit `bfe269eb`. The design still left `checkpointsFor()` collecting and sorting every published private draft before rejecting entries at or before the saved project's modification time.
- Added an optional `newerThanMs` cutoff to `list(maxResults,newerThanMs)`. The directory walk now skips older `QFileInfo` entries **before** adding them to the result container. `checkpointsFor()` supplies its saved-project cutoff, while default `list()`, gallery `list(20000)` and `hasProjectDraft()` behavior remain unchanged. No draft files are removed.
- Extended `tests/qt_saved_version_archive.cpp` to compare the new filtered list against an unrestricted list filtered by the same timestamp, including newest-first ordering and a combined count+time bound. Code commit `91c9f48dfaecbbc86878b632f99bb45683e44810`; test commit `ba3452f3798d6066f2790e5934031ddb5140cd3c`. Exact-revision compilation and test execution remain pending; no APK uploaded.

## November 16 — cap Android gallery recovery-draft enumeration

- Audited the native painting view lifetime and recovery workers. The home gallery's ordinary thumbnail and project index queues are already bounded, but `MobileDraftRecovery::list()` still materialized and sorted **every** published private draft before `MobileDocumentHome::showList()` could apply its 20,000-project UI limit. An anomalously large recovery directory could transiently allocate a very large `QFileInfoList` and trigger Android memory pressure.
- `MobileDraftRecovery::list(int maxResults=0)` now supports a bounded newest-first snapshot via a streaming maxResults-sized heap. The default remains **unlimited**, preserving complete data for `checkpointsFor()`, `hasProjectDraft()` and other recovery searches. The Android home gallery requests only the newest 20,000 drafts; older files are **not deleted**.
- `tests/qt_saved_version_archive.cpp` now creates five timestamped published draft fixtures and checks that `list(2)` retains the two newest files from the full list, before cleaning up the fixtures. Code commits: `2a87f4755819a501ceccf339d5903039784bd73d`, `ca76516cf2a90fb73890ef358a1d662f7023cbba`; regression `bfe269ebb7f5182ab9076fd20beeefbbbf467b53`. Exact-revision compilation and test execution remain pending. No APK uploaded.

## November 16 — reset GL retry backoff on confirmed context recovery

- The first-frame backoff counter was reset on `frameSwapped` but not when `QOpenGLWidget::isValid()` became true before a frame signal. A recovered surface could therefore retain a saturated 16-second delay and use it during a later, unrelated OpenGL context loss.
- `MobileKritaNativeViewFactory::scheduleActivation()` now resets the retry count when it sees a valid GL widget, only writing the Qt dynamic property if nonzero. The view/root/canvas identity is rechecked after the property update. `MobileFirstSwapRetryPolicy::afterSurfaceReady()` exposes the reset transition to Qt-only regression coverage, with an assertion that the next delay starts at two seconds.
- Changes: retry policy `6bceefb855d086342ba02cc3de55c48cb9449481`, runtime wiring `ddf6c87f76b86c79dc9a0af05769fc18c547afe8`, regression `bcfe0db3995cebda37cd34336e6f5c0483beefee`. At this point, exact-revision Android compilation and test execution have not been verified. No APK uploaded.

## November 16 — Qt regression for bounded GL first-swap retries

- Audited the Android home gallery and background index writers for unbounded memory growth. Gallery decode size, in-memory icons, pending requests, scanner delivery and index-worker queues already have limits. No confirmed resource leak was found in that audit, so no speculative cache changes were made.
- Extracted the native canvas's first-swap exponential retry calculation into the Qt-only `MobileFirstSwapRetryPolicy` in `canvas_resume_observer.h`, and wired `MobileKritaNativeViewFactory` to use that shared implementation. The policy clamps stored attempt counts to 0–3 and returns successive retry delays of 2, 4, 8 and capped 16 seconds. This avoids undefined or excessive shifts if a corrupt/stale Qt property contains an out-of-range count.
- Added assertions to `tests/qt_canvas_resume_observer.cpp` for the delay progression, saturation, negative counts and very large counts. This regression runs without Krita and does not require a phone. Changes: helper `9f39a5f7fd0f2680b7d550a42eb58fff211cfbf5`, Android use `98fa24dedaaab075c383dbdacaa9df74967e9ce5`, regression `4668b3429dc9625848cb044783423ff837b36bec`. Exact-revision compilation and test execution remain unverified at commit time. No APK uploaded.

## November 16 — back off OpenGL retries while the surface is unavailable

- `MobileKritaNativeViewFactory::scheduleActivation()` formerly retried every two seconds indefinitely when an Android OpenGL surface remained invalid and never emitted `frameSwapped`. Each timeout released its one-shot Qt receiver but could immediately schedule another, generating unnecessary callback, repaint and allocation churn during prolonged context loss.
- The first-swap observer deadline now backs off per OpenGL widget through 2, 4, 8 and capped 16 second intervals. A valid `frameSwapped` resets the widget's retry counter. Qt QObject lifetime still cancels timers when the view is destroyed, and property-change teardown is guarded before arming the timeout. Interrupted setup disarms the surface's waiting flag.
- Code commits `0c66d25b72388727bd85ccd163a5e652cf9e3394`, `5d46713b8511f86bd67eba8166c1211ac2cddc60`, `78709e48b0f4de5198ec09c2c85af2236bb762df`. This reduces unsuccessful retry scheduling in the code path; it has not been benchmarked, Android compilation for this revision has not been confirmed, and no APK was uploaded.

## November 16 — disarm GL retries if observer setup is interrupted

- During `MobileKritaNativeViewFactory::scheduleActivation()`, the first-frame logic sets `_mobileWaitingForFirstSwap=true` on the GL surface **before** creating its one-shot `frameSwapped` listener and two-second timeout. If Qt property observers synchronously close/switch the selected view, the immediate identity check previously returned early with that flag still set. Because no listener/timer existed, future activations on that live surface could never retry.
- Interrupted observer installation now clears `_mobileWaitingForFirstSwap` on any surviving owning surface before returning. The exceptional case where no QOpenGLWidget watcher can be acquired also clears the flag after scheduling receiver deletion. Normal successful listener handling remains unchanged. This addresses a stuck-retry state, not a measured heap leak or crash frequency.
- Source commit `f37138d548143e66ccfcaeb55c10dd3a23a14552`. Exact-revision Android compilation and device graphics-resume testing remain pending; no APK uploaded.

## November 16 — bound stalled OpenGL first-swap observers

- A native `QOpenGLWidget` could remain invalid and never emit `frameSwapped` after Android backgrounding or context loss. The first-frame handler used to retain its one-shot QObject receiver and keep the surface's `_mobileWaitingForFirstSwap` flag true until the entire view was destroyed, preventing a replacement retry on the same surface.
- The first-frame observer now has a two-second Qt-owned deadline. If no frame signal arrives, its timeout disconnects the original watched widget from the observer, schedules receiver deletion, and clears the waiting flag on the actual surface. It only schedules native activation when the original document/view/root remain selected and visible, and never invokes GL painting directly from the timeout. If the surface or view was destroyed, QPointers and the receiver's QObject lifetime prevent stale callbacks. No new timers run without a pending invalid first-frame GL surface.
- Source commit `78438fb995a49f6657572bc148c0f828d10c839b`. Exact-revision Android compilation, context-loss/device validation, and memory measurements remain pending; no APK uploaded. This bounds listener lifetime, not a demonstrated heap leak reduction.

## November 16 — clear first-swap retry state after every signal

- The first-frame `QOpenGLWidget::frameSwapped` one-shot callback in `MobileKritaNativeViewFactory::scheduleActivation()` previously disconnected its receiver but only cleared the surface's `_mobileWaitingForFirstSwap` flag when the GL surface was invalid. A valid swap followed by hidden-window or superseded deferred activation could leave the now-unobserved surface permanently marked as waiting, preventing subsequent retry registration.
- The callback now clears the waiting flag on the **owning QOpenGLWidget** for *every* frame-swapped signal, before checking the selected view and initial-projection state. This makes each observer's bookkeeping symmetrical: it arms once, disconnects on the swap, disarms once, and may queue the existing guarded activation. The destroyed-surface path remains handled by QObject destruction. No painting-state, zoom, or toolbar changes.
- Source commit `59bb30b9ab0fd544fc3b8e8a293bb90c8ff7b6bf`. This is a logic correction in Android/Krita GL integration; exact-revision Android compilation and GL-resume tests remain pending, and no APK was uploaded.

## November 16 — fix GL first-frame retry flag ownership

- Identified an inconsistent state transition in `MobileKritaNativeViewFactory::scheduleActivation()`: `_mobileWaitingForFirstSwap` is set and checked on the QOpenGLWidget surface, but the invalid-frame and surface-destruction callbacks were clearing a property of the same name on the parent `KisView`. On a still-live but invalid GL surface, this leaves the original surface's retry flag set and prevents a new `frameSwapped` listener from being armed after context loss.
- The invalid-frame callback now clears the flag **on the watched surface** before requesting a new activation. A destroyed surface needs no reset (its dynamic properties disappear with the QWidget); the destruction handler now only retires its one-shot QObject receiver. Existing guarded QPointers, per-surface first-swap observer disconnection and Android first-projection logic are preserved.
- Source commit `6a2f682477085622e3ed38ad4969422eadf8429f`. This corrects a specific logical bug in first-frame recovery; exact-revision Android compilation and device GL context recreation tests are pending. No APK uploaded.

## November 16 — reject duplicate-open scans after document teardown

- `MobileKritaStandaloneDocuments::openFile()` previously read the path and `_mobileDraftPath` of another open `KisDocument` through a raw pointer. When a Qt/Krita observer closes the document during either read, the next access can target a destroyed QObject. The scan now uses `QPointer<KisDocument>`, checks the original native root and view survival after each metadata lookup, and aborts without importing or activating a tab when an identity disappears.
- The scan captures its native workspace before enumeration and compares it against the current rows before activating an existing matching drawing or beginning a new import. This prevents a stale duplicate-open decision if a document closes/changes during canonical path comparison. The check uses the existing `MobileViewIdentityGuard::sameRecoveryWorkspace()` policy.
- `tests/qt_native_view_stack.cpp` adds a two-canvas regression where another document is destroyed after the duplicate-open scan's snapshot. The workspace must be rejected. Source commit `ab25fa9354959a32a561d22e024d8cc59fb99869`, regression commit `e5b9e25c6c9c460fb055c87db7edfdcd20f579e1`. Compilation and execution at these revisions remain pending; no APK uploaded.

## November 16 — activation router setup teardown checks

- `MobileKritaNativeViewFactory::attach()` now validates the original Krita view, document, manager and native root after querying the `_mobileStudioKritaSelectionWired` dynamic property and after writing it following resume-observer installation. If any of those objects were invalidated during setup, attachment takes the existing guarded rollback instead of continuing into `setCurrentWidget()` and activation.
- Source commit `c4af1fccdd8efde3a9f231334d4a0c4dfab8eeef`. This is a narrow preventative check, not a measured crash fix. Native Krita/Android build for this revision and device regression testing remain pending. No APK uploaded.

## November 16 — guard queued native canvas bootstrap after Qt property callbacks

- `MobileKritaNativeViewFactory::scheduleActivation()` now revalidates the selected `KisView`, original view manager, canvas root, current document and active tab immediately after querying `_mobileStudioKritaCanvasBootstrapped`. Previously a synchronous Qt property observer could destroy/switch the selected canvas, and the next line would access its `canvasBase()`.
- The direct `bootstrapFinished` invocation now uses a guarded `QPointer<QObject>` canvas identity. Following the synchronous invocation and the `_mobileStudioKritaCanvasBootstrapped` property write, attachment identities are rechecked before tool initialization resumes. This addresses possible native view teardown in the first-frame/Android resume path without restarting tools or changing painting behavior.
- Commit `43f8ccfc624e22272c6b06fd6045fad747ec428d`. Exact-revision Android compilation and actual crash reduction remain unverified; the latest reported successful build at the start of this batch was `fe90e1dc`. No APK uploaded.

## November 16 — protect native canvas title/dirty refresh from document teardown

- `MobileKritaNativeViewFactory::attach()` now queries the initial document path only once and checks the original document, manager, view and stack lifetimes before passing the view into the native stack. Previously a reentrant document close during the first `path()` read could lead to a second read on a destroyed `KisDocument`.
- The engine's title/modified-state refresh lambda now guards its document, native view and stack after `path()`, title conversion and `isModified()`. Qt path/dirty signal delivery can close the canvas while native tab labels are refreshed; the lambda must not dereference a stale source document or stack after such callbacks.
- Source commit `89541944d4a1a21e508b4a6884a2b81a7a67bab7`. Native Krita integration has not been compiled at this exact revision or runtime-tested on Android. The last reported successful local build was `7f08cdd5`. No APK uploaded.

## November 16 — native draft metadata attachment lifetime guard

- In `MobileKritaStandaloneRuntime::attachDocument()`, early recovery-draft setup now captures the original runtime, document, manager and canvas root before reading `_mobileDraftPath`. It revalidates those identities between reading the document path, accessing document title metadata, allocating the private draft name and writing the resulting property. A synchronous teardown during any of those operations now aborts attachment instead of using a destroyed `KisDocument`. An unavailable `documentInfo()` is treated as an empty title, avoiding a null dereference.
- Commit `d5bc72ed3573e2f24a23be61150adb50df588a54`. This is a narrowly scoped preventative native Krita lifecycle change; source review is complete but exact-revision Android compilation, regression execution and on-device behavior are pending. No APK uploaded.

## November 16 — coalesce memory samples after closing multiple canvases

- `MobileKritaStandaloneRuntime` no longer schedules a separate zero-delay memory snapshot for every document destruction. In a bulk close, each document's signal can arrive before the corresponding Qt native view retirement timer and `DeferredDelete` cleanup have finished, producing misleading `detachedKisViews` counts and a burst of `/proc/self/status` reads.
- The optional `MobileStudio/memoryDiagnosticsV1` path now coalesces all such closures into one runtime-owned `QTimer::singleShot(250ms)` sample. It resets its queued flag before logging; the runtime's QObject lifetime cancels the callback if the native host is destroyed. This reduces diagnostic overhead and makes measurements more comparable after canvas cleanup. Normal document close, autosave, and painting flows are unchanged; no timer is scheduled with diagnostics disabled.
- Source commit `f6b229a74c3c22786e6be0308a38148f3b3dfbf8`. The earlier memory-instrumentation build has been reported successful at `190f671c`; exact-revision compilation, measured memory data and device behavior for this change remain unverified. No APK uploaded.

## November 16 — distinguish registered canvases from retained KisView objects

- The opt-in `MobileStudioMemory` log now includes `retainedKisViews` (actual `KisView` QObject widgets below the independent native root) and `detachedKisViews` (those not registered in its `QStackedWidget`). This distinguishes a stale canvas QWidget from process RSS retained by ordinary caches. The `registeredDocuments` counter now counts unique live document identities, not one per view row; `pendingDrafts` provides context about background-save continuations.
- No new background work, allocations, file writes, or logging occurs while `MobileStudio/memoryDiagnosticsV1` is disabled. When enabled, compare these counts at steady state after repeated open/close cycles. A nonzero `detachedKisViews` at the immediate `closed` sample may be transient during Qt cleanup; persistent values across later `periodic` samples warrant investigation.
- Commit `d20b6a61f298415e209db534162bd635ab34bba3`. This improves diagnostics only; it does not prove or fix a leak. Exact-revision Android compilation and memory measurements are not yet verified, and no APK was uploaded.

## November 16 — opt-in native memory retention telemetry

- `MobileKritaStandaloneRuntime` now accepts the disabled-by-default `QSettings` key `MobileStudio/memoryDiagnosticsV1`. When enabled, the independent engine emits structured `MobileStudioMemory` lines to Qt/Android logs on startup, immediately after successful document attachment, on the next event-loop turn after document destruction (allowing queued view retirement to finish), and every 60 seconds. There is no extra timer or file I/O when disabled.
- Snapshots read process `VmRSS`, `VmHWM`, `RssAnon`, and `VmSwap` from `/proc/self/status`, in KiB; they include counts of live native view registrations and associated document identities. They do not record filenames or drawing contents, write new log files, or allocate GPU resources. Missing proc fields report -1. For diagnostic runs, enable the Qt setting before engine initialization and filter Android logcat for `MobileStudioMemory`. Compare repeated steady-state cycles with the same canvas size and open-view count; RSS growth alone is not proof of a leak because allocators and drivers retain caches.
- Code commit `f39601aab632fa35e214c16c47b73fcef5f9ee87`. This is instrumentation, **not** a demonstrated memory leak fix. Compilation for this exact revision, automated stress measurements, and device results remain pending. No APK uploaded.

## November 16 — revalidate document metadata before native Save As picker

- `MobileKritaStandaloneDocuments::writeDocument()` now records an original-document/root identity guard immediately after the image render-wait phase, and rechecks it between source path, MIME type, supported-format registry and recovered-state queries. It also rechecks after reading `nativeFormatMimeType()` before configuring the Save As/Export Copy chooser. These Krita/Qt operations can synchronously notify observers; without revalidation, a closed or switched canvas could be accessed in the next metadata read or used to initialize a picker.
- Source commit `1749ba0afdfb0ff647593f95fc697568f983d9cd`. The patch preserves the existing save policy, collision checks, and file formats. This path is native Krita integration rather than a pure Qt helper, so there is no executable engine-level regression in this batch. Exact-revision Android build and on-device Save As behavior remain pending; no APK uploaded.

## November 16 — fail closed on changing Save As workspace

- `MobileKritaStandaloneDocuments::writeDocument()` now protects each other-document path/draft metadata lookup with `QPointer<KisDocument>` and checks that the originating document, runtime and selected native root still exist. Previously a close or host replacement while collecting other drawings' backing paths could leave a stale `KisDocument*` in the overwrite scan.
- The scan now compares the original native view/document/active-selection snapshot with a fresh snapshot using `MobileViewIdentityGuard::sameRecoveryWorkspace()`. If a second canvas disappears, appears or changes identity during destination validation, the write is rejected even if the original selected canvas remains live; this avoids silently skipping an unsafe overwrite comparison. The own-draft path check also revalidates the selected host before continuing.
- `tests/qt_native_view_stack.cpp` adds a two-canvas case where another document closes during a Save As scan. The unchanged selected canvas must not authorize saving against a changed document set. No changes to Krita file encoding, drawing behavior or legitimate Save/Discard prompts.
- Source commits `7ae91846ffb580a1f7d7b8a1541ba8bf96833b07`, `d8b9f3c6e15b81278589931d7b968999921a50f8`, `84f17b949d72f8d7462d0f175b85ac942fbac227`; Qt regression commit `a00146081c11b52fe24b15a51819b0c302a63a74`. Exact-revision Android compilation, regression execution, and device behavior remain pending; no APK uploaded.

## November 16 — close the detached canvas retirement ownership gap

- `MobileNativeViewStack::retireDetachedView()` already deferred its ownership check until Qt stack removal and selection listeners had unwound, but then called `deleteLater()`. That left a second interval in which a still-live detached canvas could be adopted by a new owner before its already-posted `DeferredDelete` executed. The guarded zero-delay timer now destroys the orphan synchronously **only after** confirming it still has the original host as parent and is no longer registered there. This closes the check-to-delete gap without deleting any canvas during `QStackedWidget::removeWidget()`.
- `tests/qt_native_view_stack.cpp` now exercises a document being destroyed, its detached QWidget transferred and adopted into a second `MobileNativeViewStack` before the first host's retirement callback, and then retirement by its final owner after the new document closes. Existing 128-cycle orphan/accepted-close regressions remain.
- Source commit `4125e296b405d18b24b27dca2b561b6ac66ccf89`; regression commit `b3907380f634b3727452ed2e09cb786d2b5c9918`. The previous watcher report was successful at `1ee985a7`; the exact revision of this patch has not been compiled or executed yet. No APK uploaded.

## November 16 — guard recovery hooks during independent canvas attachment

- `MobileKritaStandaloneRuntime::attachDocument()` now revalidates the runtime, document, view root and view manager after each recovery signal connection. A native Qt/Krita callback can replace the host or destroy the document during attachment; subsequent signal registration must not use freed QObject pointers. The final return also verifies that the original root and manager still belong to the runtime.
- Background-save completion and document-destruction callbacks capture a guarded `QPointer<MobileKritaStandaloneRuntime>` rather than a raw `this`, so callbacks do not dereference an already-destroyed runtime. The existing QObject receiver still disconnects these callbacks during teardown.
- Commit `706ad8343728b17f68346e6e46f4abc3f1c8e0a3`. This is an Android/Krita engine integration path; no host-only Qt regression fixture can instantiate the required Krita view manager and document signals. Exact-revision Android compilation and device behavior remain unverified; no APK uploaded.

## November 16 — protect independent background draft checkpoints from document teardown

- `MobileKritaStandaloneRuntime::checkpointDrafts()` now retains each candidate document as a `QPointer<KisDocument>` and checks that the native root, runtime and document still exist between ownership, modified-state, private-draft metadata and active-saving queries. A synchronous document/Qt callback can close the canvas while a background/quit checkpoint is traversing the document list; later operations must not dereference the deleted original pointer.
- `continueDrafts()`, which retries checkpoints queued behind active background saves, now performs the same lifetime/ownership checks before invoking `slotAutoSave` on an eligible document. Queued draft identities are removed when ready, and no view activation or user-visible Save/Discard behavior is changed.
- Commit `09edd83137df9d3ad7a3a406ca4e3951a57d4af8`. This native Krita runtime path is not directly covered by the standalone Qt-only tests; exact-revision Android compilation, on-device background/resume behavior and measured crash reduction remain unverified. No APK uploaded.

## November 16 — tighten GL surface-destruction and zoom initialization guards

- `MobileKritaNativeViewFactory::scheduleActivation()` now rechecks the selected view after reading its projection-ready Qt property in the one-shot GL surface destruction callback. The property lookup may dispatch QObject observers; a callback that destroys the view must not be followed by a write through its stale pointer.
- Before invoking native Zoom-to-Fit after calculating the image rectangle, the factory now revalidates the active Krita view, manager, canvas widget, and native root. An engine notification during converter geometry calculation may tear down or switch the selected view. Do not subsequently query its zoom manager without verifying identity.
- Source commit `557f3edaa97b6087f075fbd87d58fb2086ebe7d5`. These are narrow Android/Krita OpenGL lifecycle safeguards with no changes to document contents, drawing behavior or background saving. Exact-revision Android compilation and on-device testing are pending; host-only Qt regressions cannot reproduce Krita's converter and GL callbacks. No APK uploaded.

## November 16 — independent engine first-frame OpenGL lifecycle hardening

- `MobileKritaNativeViewFactory::scheduleActivation()` now checks that the selected Krita view, native document root, manager, and GL canvas are still the same live objects after Qt dynamic-property changes and repaint requests. A first-frame property notification may synchronously close a drawing or replace a canvas while the Android window is being recreated; previously the code could continue creating a swap receiver or reading a destroyed document/surface.
- The invalid-context retry revalidates the surface immediately after marking `_mobileWaitingForFirstSwap`, before allocating or connecting its one-shot `frameSwapped` receiver. Its callback also checks the selected view after each property read/write before scheduling another activation. Initial projection readiness, `_mobileBlankCanvas`, and pending-swap metadata are read with intervening lifetime/identity checks.
- The final paint/update path rechecks current view ownership after `surface->update()` and guards the native top-level `QWindow` before `requestUpdate()`. No additional OpenGL contexts or duplicated canvas buffers are introduced. No changes to Krita's painting algorithms, undo history, or KRA saving.
- Source commits `1f2b8183ba04665177afdba6971bae1960ed624c`, `e1d6d827442af6ba3bbfa68a2f2e08bd1dab7fd5`, `19ed87c4a25d008fcbc349f443def375d8d1eaac`. This Android/Krita GL path cannot be exercised by the existing standalone Qt widget fixtures. Exact-revision WSL Android compilation, device first-frame behavior and crash reduction are **not yet verified**; no APK uploaded.

## November 16 — guard periodic stationary canvas refresh from synchronous teardown

- `MobileNativeCanvasPilot::refresh()` now snapshots its engine bridge and tracks its member timer with a `QPointer` throughout UI and native-document updates. Krita queries and Qt widget methods (including visibility, title, layout and panel refresh changes) can synchronously notify observers that destroy the pilot or replace its workspace. Each potentially reentrant operation is followed by a lifetime check, and each panel is separately guarded before further use. A destroyed document title/panel aborts that refresh rather than dereferencing an invalid QObject. No Krita document, painting, recovery, or Save/Discard behavior changed.
- `runQuickAction()` now uses a local engine-bridge copy to enumerate Save/Close targets and stops if that enumeration tears down the pilot before scheduling a deferred command. Previously it could try to access the pilot-owned timer after synchronous destruction.
- Commits `45d19f82f2291febc6a0cd54b1bef9c479463e4f` and `7a4c060ae859d6a8e23298f168299c8e1caeb53b`. This is the optional Android stationary UI, not the independent engine's active painting loop. No standalone Qt test can instantiate this pilot without Krita. Exact-revision Android compilation and Fold 6 runtime validation are pending; no APK was uploaded.

## November 16 — prevent stationary canvas pilot use-after-free during navigation

- `MobileNativeCanvasPilot`'s Canvases button previously called `MobileDocumentSwitcher::open()` and then unconditionally called `refresh()`. The nested switcher can synchronously replace the native workspace or destroy the pilot while closing/opening a drawing, leaving that final refresh as a potential use-after-free. It now copies the engine bridge out of the pilot before entering the dialog, holds a `QPointer` to its lifetime-owned timer, and refreshes only while that timer remains live.
- The shared deferred mobile-command dispatcher now likewise uses a local engine-bridge copy and guarded window identity for its New/Open/Save/Close/switcher/tool/menu actions. A synchronous native engine callback may destroy the initiating pilot; the local copy prevents its member bridge storage from being freed while its own operation executes. The existing timer guard still controls refresh after dispatch.
- The Back button copies its exit callback before invocation because exiting Mobile Studio may destroy the pilot that owns the `std::function`.
- Source commits `044c39a59d79f10c6101834616206d3e72be97af` and `93cebc3d610c802f67e5aed9f2560ee624c95d8b`. This is an Android-only legacy stationary-pilot path; no directly executable host-only Krita fixture was added. Exact-revision Android compilation and Fold 6 behavior remain unverified. No APK uploaded.

## November 16 — revalidate canvas lifetime across deferred layout refresh

- `MobileCanvasTransfer` now rechecks the original native painting widget and its owning QMainWindow after `show()` and `updateGeometry()` in its deferred desktop-return refresh, and after `updateGeometry()` in the persistent-host refresh. Qt geometry updates can notify Krita/host observers synchronously; if they replace the canvas or remove the original central view, subsequent `update()` must not target the stale widget.
- `refreshDocumentRoot()` now accepts a `QPointer<QWidget>`, rechecks its lifetime after `updateGeometry()` and `update()`, and guards a scroll area's viewport before repainting. This prevents accessing a deleted document-root widget during layout-induced lifecycle transitions without changing any Krita canvas ownership or save behavior.
- `tests/qt_widget_document_root.cpp` now exercises deleting the native document root while a mobile canvas remains active, then returning the identical canvas to the desktop host after processing pending Qt events. This is a Qt-only ownership/refresh regression, not an actual OpenGL memory measurement.
- Source commit `43fac771e3fe59036b7e64891c78561903830756`; test commit `1f29ec1a1dc3b7671d92483ae3f2275ecc959bb5`. Exact-revision Android compilation and host test execution are not yet confirmed. No APK uploaded.

## November 16 — abort deferred Save/Close after reentrant window destruction

- `MobileDeferredViewCommand::schedule()` now captures a `QPointer` to the initiating context **before** its first backend query and checks the owner again after each potentially reentrant `openViews()` / `currentDocument()` lookup, including the queued dispatch path. Previously a synchronous engine observer could destroy the initiating native window during a lookup, yet subsequent code would continue consulting the backend or queue an action using the destroyed context. The updated dispatch refuses to call the captured Save/Close command if the owner, view or document vanishes during the lookup.
- `tests/qt_deferred_view_command.cpp` now exercises owner destruction during both initial and queued view/document reads, in addition to existing view-switch, stale-identity, and normal owner-teardown tests. The initial new test fixture explicitly selects the still-live second canvas after its earlier deliberate first-canvas deletion.
- Source commit `ad31196d1a448c9c96e93c199654d95d7a2ea349`; regression commits `07e75f55fd7f01039d76e4ca86e31d132c817355` and `3aa020801e6247a43a7ee4eef1eecc71f2069a04`. Local Android exact-revision compilation, standalone Qt regression execution, and Fold 6 behavior are **not yet confirmed**. No APK uploaded.

## November 15 — safely unwind touch gesture modals during Android teardown

- `MobileKritaNativeWorkspace::openTouchGestures()` used a stack `QDialog` parented to the native window; its nested "More actions" picker was another stack dialog parented to the combo box. Window teardown during either `exec()` could destroy a stack dialog through QObject parenting, then resume code referencing destroyed controls. Both dialogs now use independent stack ownership. Native-window destruction rejects the outer dialog, whose rejection also rejects any running inner picker; the combo's destruction directly rejects its picker as well. `QPointer` checks prevent accessing the disappeared combo or committing settings when the workspace/owner no longer exists. A call with no parent still supports accepted changes.
- Source commits `29bbe203f823642bd0b3d174eda6a492918129bf`, `1f8a4f88403895b52ec4075c18ea5b24a97efa14`, and `108d9f850ad319c6af8ef5ce6612c3c65b06631e`. The nested-modal destruction regression in `tests/qt_native_command_layout.cpp` covers the analogous Qt ownership/event-loop pattern, not the Krita input profile itself. Exact-revision Android compilation and device behavior unverified; no APK uploaded.

## November 15 — prevent toolbar customization modal teardown crashes

- `MobileNativeCommandLayout::editToolbar()` and its nested `chooseCommand()` previously used stack `QDialog`s parented to a window or editor dialog. Destroying the Android host during `exec()` could make Qt parent ownership delete those C++ stack objects before their functions returned. The outer toolbar editor is now parentless and rejected on native-window destruction; the nested picker is independently stack-owned and rejected if its enclosing toolbar editor is rejected or destroyed. The controller's scoped edit flag now uses a `QPointer`-guarded scope exit, so it does not write to a destroyed controller if the window disappears during a modal operation. A completed dialog commits toolbar changes only while both the owner and controller remain live.
- `tests/qt_native_command_layout.cpp` adds a Qt-only nested-modal regression that deletes the window during the inner picker and verifies that both modal loops reject while their stack objects remain intact. Source commit `3ec5eabcc8632171c204f296c40355efef15698a`; regression commit `742d3867ff21708311a7135a97819552b09753ae`. Exact-revision Android compilation and host Qt execution not yet verified; no APK uploaded.

## November 15 — avoid stack calibration dialog deletion on native window close

- `MobileNativeCanvasPilot` opened `MobileFingerPressureCalibrationDialog dialog(m_backend); dialog.exec()` as a stack variable with the Android native window as its QObject parent. If that window was destroyed while the modal dialog's nested Qt loop was running, Qt could delete the child dialog before stack unwinding, leaving a double-delete/use-after-free risk. The calibration dialog is now a parentless stack object, sized relative to the original window and rejected on its owner's `destroyed()` signal. The calibration controls and saved pressure settings are unchanged.
- Source commit `0ed3510a8bff75dafc677be01cf5f604ed2c37bd`. The existing `qt_native_canvas_chrome.cpp` modal-teardown regression covers the same parentless stack-dialog rejection ownership pattern, but it does not instantiate the Android-only calibration UI. Exact-revision Android compilation and device behavior are not yet confirmed; no APK uploaded.

## November 15 — dispose of unregistered documents after synchronous KisPart notifications

- Native Open and New Canvas both call `KisPart::addDocument()`, which can invoke observers synchronously. When an observer removes the newly added document from `KisPart` without destroying its `KisDocument` QObject, later host attachment fails its `isRegistered` check; the existing `MobileFailedDocumentAttach::rollback()` deliberately processes *only* still-registered documents, so the now-unregistered, loaded image remained allocated with no canvas. Both paths now check registration immediately after `addDocument()` returns and delete a still-live unregistered document before returning failure. A document that was already destroyed by the observer is ignored via `QPointer`; a normally registered document proceeds through the unchanged attach and rollback logic. No changes to save/recovery files.
- Source commits `101dd2b0b1dedb467d2fd3394fc6fc91b455d661` (`mobile_core/krita_standalone_documents.h`) and `1435cc50f5a83c1580d5161dac92f3727a459c84` (`mobile_core/document_service.h`). No standalone Qt fixture for real `KisPart::addDocument()` observer behavior. Exact-revision Android compilation and measured RAM effects not yet verified; no APK uploaded.

## November 15 — guard recovery metadata setters against synchronous document destruction

- In `MobileKritaStandaloneDocuments::openFile()`, successful private draft and saved-project checkpoint imports updated `_mobileDraftPath`, path, MIME, recovery and modified flags before first constructing a `QPointer<KisDocument>` lifetime guard. Document path/property notifications can synchronously destroy a pending import or replace its native root, making the subsequent raw-pointer setter a use-after-free. The entire recovery metadata sequence now checks document/root/runtime identity after every setter, and refuses to register the imported document after teardown. An interrupted import still owned by this code is deleted if it has not been registered with `KisPart`, avoiding an orphaned loaded KRA image; no registered document is deleted by this abort path.
- Added a Qt-only regression to `tests/qt_native_view_stack.cpp` simulating an imported QObject destroyed synchronously in a metadata change signal and verifying that a second setter is not attempted. This tests the Qt reentrancy pattern, not the Krita document implementation. Source commits `3abc86a6789a131a693f847a8eb03866062ac352`, `87dd34b7b22122e3f7914b7ae5026d0853b92ae2`; test commit `b593a5b8e2da6d60914cdca7ad8dabe7c4630b9d`. Exact-revision Android build and host test execution not yet confirmed. No APK uploaded.

## November 15 — complete late-attachment rollback after resume observer setup

- Reviewed the new `MobileKritaNativeViewFactory::attach()` rollback added in `2d01005` against every post-`adoptView()` failure branch. The resume-observer installation branch still returned `nullptr` directly if a synchronous Android/window callback destroyed the candidate manager, canvas or document, bypassing the new registered-canvas cleanup. That branch now calls `abortRegistered()` as well; unrelated tabs remain registered.
- Added a host Qt regression to `tests/qt_native_view_stack.cpp` with two canvases: it forgets only the simulated failed attachment by view identity, deletes its detached widget, and verifies that the other view/document remains registered and selected. The test covers Qt stack ownership, not the Krita observer itself. Source commit `bb22001b6a4c44b8f11f6b8736e145698f253a8d`; test commit `a6652e0d1ab03cfef9692d9e6f67db3ef65c5786`. Exact-revision Android compilation and host test execution not yet confirmed; no APK uploaded.

## November 15 — roll back a registered native canvas after failed late attachment

- `MobileKritaNativeViewFactory::attach()` now removes the candidate's original native-stack registration when a later synchronous selection, document-state, or resume-observer callback invalidates the document, canvas, root or Krita manager. Before this change, several late `return nullptr` paths occurred *after* `adoptView()` succeeded, leaving a registered partially initialized canvas behind although the caller was told attachment failed. The rollback verifies the original view/document registration before `forgetView()`, and deletes the detached `KisView` only if its native manager and original Qt owner are still live. If another host has adopted/reparented the widget, it is left untouched. This does not change Save/Discard/Cancel semantics or delete unrelated canvases.
- Code commit `2d010055f4cfd60b0e6fa619b686910102fa1292`. This is Krita engine-specific attachment code with no standalone Qt-only regression. Exact-revision Android compilation and device behavior have not yet been confirmed; no APK uploaded.

## November 15 — rearm native OpenGL first-frame retry after surface teardown

- `MobileKritaNativeViewFactory::scheduleActivation()` now resets `_mobileWaitingForFirstSwap` if Android destroys the watched `QOpenGLWidget` before the initial valid frame, and also if a `frameSwapped` callback arrives while its context is still invalid. Previously this flag was set before registering a one-shot callback and was never cleared on failure. Replaced or reinitialized GL surfaces could therefore remain unable to register a new first-frame retry, leaving the native projection uninitialized until another lifecycle event or canvas recreation. The one-shot receiver remains scoped to the native view; the ordinary successful first-frame path is unchanged and still requests a full projection upload for a waiting canvas.
- Source commit `85ecdc7dd5f340ba896777f0920df73461acfeab`. This requires real Android OpenGL/Krita lifecycle conditions and has no meaningful Qt-only mock fixture. Exact-revision WSL compilation and on-device recovery remain unverified; no APK uploaded.

## November 15 — clear layer rail nodes when the Krita manager disappears

- Fixed `KritaMobileLayersPanel::reload()` to use its established no-image cleanup when the native `KisNodeManager` or `KisViewManager` is unavailable. Previously an early `if(!m || !view)return` bypassed cleanup of `m_nodes`, `m_selected`, `m_active`, thumbnail widgets and layer-tree rows. If a document closed while its rail remained visible, those strong `KisNodeSP` references could retain the closed document's paint nodes until another valid view initialized. Now the no-manager state releases them and records the `no-view` signature, allowing a later manager to repopulate the rail normally.
- Source commit `07e4139630f7759186c1faa27c7ce2f060fa3c0c`. This Krita-integrated panel cannot be exercised by existing host-only tests; exact-revision WSL Android compilation and RAM reduction are not yet verified. No APK uploaded.

## November 15 — release fill-tolerance preview memory on canvas hide

- `MobileFillTolerancePreview` now frees its sampled paint-device `QByteArray`, sampled selection bytes, and rendered `QImage` overlay whenever Android hides the canvas. These buffers are only temporary copies for interactive tolerance feedback, but previously remained resident for the lifetime of the widget even while another document was open. Hide invalidates the active worker generation and stops its delay timer; `requestMask()` now refuses hidden work, worker completion callbacks avoid rescheduling hidden previews, and Show requests a fresh sample once the previous worker completes (or immediately if idle). Original Krita paint devices, undo history, and document data are unchanged.
- Source commits `3cb2c4d93e46576adc0321819333d84a117e0288` and `c67030aeec1c4ced2faf1c155e05d7a711aa96d1`. This engine-specific preview widget is not exercised by host Qt regressions; exact-revision WSL Android compilation, low-memory behavior and actual RSS reduction remain unverified. No APK uploaded.

## November 15 — test deferred retirement after accepted native closes

- Expanded `tests/qt_native_view_stack.cpp` with another 128-cycle cleanup regression. Every iteration opens a canvas, cancels a Close (verifying its canvas and document stay registered), then accepts a Close callback which deliberately does not destroy the QWidget. After draining Qt's queued/deferred events the test checks that the now-detached painting widget has been destroyed, its view registration cleared, and no child widgets accumulate in the native stack. This complements the prior 128-cycle test where the document itself is destroyed before its canvas.
- Test commit `d55fa30bfee68c2c9a02cabfd4291a30d5c6ef38`. This tests native Qt ownership and deferred retirement rather than Krita GL memory usage. Host test execution and exact-revision Android build not yet independently verified; no APK uploaded.

## November 15 — regression for native canvas destruction churn

- Added a 128-cycle Qt-only regression to `tests/qt_native_view_stack.cpp`. Each cycle attaches a fresh canvas widget to a new document, destroys that document while the native stack remains alive, drains queued/deferred Qt events, and checks that its view registration, current document and widget have all been released. The test also counts actual `QObject::destroyed` notifications and checks the stack has no accumulated child canvases. This is aimed at catching retained canvas/OpenGL widget lifetimes as the Android app repeatedly opens and closes drawings.
- Test commit `220a09774ed8702d8c1cdf98dce34efeb402d4cf`. It uses plain Qt widgets rather than actual Krita GL contexts; it does not measure device memory. Host-test execution and exact-revision WSL Android compilation are not yet confirmed. No APK uploaded.

## November 15 — release hidden layer rail's retained Krita node references

- `KritaMobileLayersPanel::hideEvent()` now releases cached `KisNodeSP` entries (`m_nodes`, active and selected nodes), layer thumbnail widgets/icons and tree rows when the panel becomes hidden. Previously the panel's refresh path skipped hidden widgets, so closing or switching a canvas while its layer rail was hidden left the rail holding strong references to the previous document's paint nodes until the rail was reopened. Those references could delay destruction of layer pixel data and keep memory resident during subsequent canvases.
- Release is deferred for active drag, reorder, clip, delete, or reload transactions so this cleanup never clears nodes that an in-flight Krita operation still needs. On `showEvent`, the rail requests a fresh `reload()` from the current engine document rather than reusing old node references. Source commit `23004dbccc7d74932fdbf2cd5ca747ce3113fd68`. Android build and actual heap/RSS reduction are not yet verified; no APK uploaded. This Qt/Krita-integrated widget does not have an executable host-only test fixture.

## November 15 — handle lighting color-picker raster allocation failure

- `MobileLightingSphere::paintEvent()` now checks `QImage::isNull()` after allocating its 192-by-192 ARGB drawing buffer. Qt can return a null raster when Android denies a memory allocation under pressure; the previous loop unconditionally wrote through `scanLine(y)` and could dereference null. If allocation fails, only this decorative color-sphere paint is skipped for that frame; its raster is retried on the next repaint, with no effect on artwork or document saving.
- Source commit `a9dc442ce10d6d571d9d1855b0511393854d5a27`. This Android/Krita color-workspace paint path has no practical host-only allocation-failure fixture; exact-revision local build and on-device low-memory behavior are unverified. No APK uploaded.

## November 15 — prevent layer-modal stack object deletion during Android teardown

- `KritaMobileLayersPanel` no longer QObject-parents its stack-allocated layer-options `QMenu`, layer options sheet, opacity dialog, blend-mode dialog, or group-layer dialog to the native panel/window. Android can destroy that parent during `QDialog::exec()` or nested selection dialogs, previously causing parent-owned deletion of the C++ stack object before its enclosing function unwound. Each unparented modal dialog is rejected on layer-panel destruction; the panel uses `QPointer` checks after modal returns, including after nested action selection, before accessing Krita layer state. No layer commands, undo actions, or save semantics changed.
- `tests/qt_native_canvas_chrome.cpp` adds a Qt-only regression in which a modal sheet is rejected by its destroyed-panel signal and survives until normal stack unwinding. Source commit `1d4708f0dd8874a37b66628f72fc26cb7292db0b`, test commit `c9585fd95c045164f755a65555c5256ebcc9a188`. Exact-revision Android build and Qt test execution not yet verified; no APK uploaded.

## November 15 — prevent quick-action refresh after native pilot teardown

- `MobileNativeCanvasPilot::runQuickAction()` now tracks the pilot's member `QTimer` through a `QPointer` while dispatching native actions, tool selections, Undo/Redo, and panel changes. In particular, native commands can synchronously close the final canvas or replace the mobile workspace; previously execution continued to `refresh()` on a destroyed pilot. The deferred dialog and Save/Close dispatcher now makes the same lifetime check after its engine command returns. The timer is a member destroyed with the pilot, so no separate QObject ownership scheme was added. Existing document identity guards and Save/Discard/Cancel behavior are unchanged.
- Source commit `f7f839936a93ef8f2e12acbe853ff2755d3758f9`. This pilot dispatch path requires Android/Krita integration; no standalone Qt-only fixture was added. Exact-revision local Android build and on-device behavior remain unverified. No APK uploaded.

## November 15 — guard asynchronous crash-report delivery at Android shutdown

- `MobileCrashReport::copy()` now captures a `QPointer<QCoreApplication>` before submitting the Android JNI exit-history worker. If the application is destroyed while the query is pending or executing, the worker skips posting its report instead of calling `QMetaObject::invokeMethod(qApp, ...)` through a potentially stale global pointer. The normal clipboard/report dialog path is unchanged while the application remains active; the queued receiver context also prevents delivery after application destruction.
- Code commit `ac2a6bc2348a659db69c685db6903458a0001d2a`. This is an Android JNI/lifecycle path without a standalone Qt fixture; exact-revision local Android compile and runtime results remain unverified. No APK uploaded.

## November 15 — guard native view attachment after draft setup

- `MobileKritaStandaloneRuntime::attachDocument()` now captures `QPointer` identities for its runtime, Krita document, native stack, and view manager before reading/setting the document's private draft-path property. A QObject dynamic-property notification can synchronously close the originating window or document; previously the factory was then invoked using raw pointers from the possibly destroyed runtime. The method now confirms all identities remain valid and still belong to the original runtime before invoking `MobileKritaNativeViewFactory::attach()`. This does not change draft naming, close behavior, or painting resources.
- Code commit `8d4224cea7231ef18b3b620171e48df06b48ffad`. The property-notification-to-KisView attachment path requires Krita engine integration and has no standalone Qt-only test; exact-revision local Android compilation and runtime behavior remain unverified. No APK uploaded.

## November 15 — preserve canvas-create backend across Krita reentrancy

- `MobileDocumentService::createCanvas()` now retains its `QSharedPointer<MobileDocumentHostPort>` in a local variable throughout `KisDocument::newImage()`, `KisPart::addDocument()` and native view attachment. Krita's synchronous document registration and view creation callbacks may destroy the originating mobile document service; accessing its `m_host` afterward previously risked a use-after-free. The newly created `KisDocument` is also guarded before image initialization and dynamic property callbacks, so a callback that destroys it does not leave a dangling pointer for cleanup or registration. Existing failed-attach rollback and Save/Discard/Cancel ownership remain unchanged.
- Source commit `9e5e7bd99e2a3ede5dd8241f9a5323956e4a07c2`. This code requires the Android Krita engine and has no standalone Qt-only integration fixture; exact-revision WSL Android build/device results remain unverified. No APK uploaded.

## November 15 — guard native document-open picker against host teardown

- `MobileDocumentService::openFiles()` now holds a local `QSharedPointer` to its document backend across `KoFileDialog::filename()` and checks a `QPointer<QWidget>` to the original window after that potentially nested Android picker. If the app window was destroyed during the picker, the selected file is not opened into a stale host; if `openFile()` subsequently destroys the window or service, the result is still returned without dereferencing their stale members. A failed Open only displays an error dialog while its owner remains live.
- Source commit `275c5a9021f8963982cd50cd7edfacce92e141ca`. The native KoFileDialog path is Android/Krita-specific and has no standalone Qt regression fixture. Exact-revision WSL Android compile and device behavior are unverified; no APK uploaded.

## November 15 — avoid stack popup deletion during toolbar teardown

- The stationary native canvas pilot's More menu now uses an unparented stack `QMenu`. Its `exec()` enters a nested Qt event loop, and previously the menu was QObject-parented to the toolbar anchor (or main window). Destroying that owner during a workspace replacement could delete the C++ stack object before `showMore()` unwound, leading to invalid stack destruction/use-after-free. The menu remains scoped to `showMore()` and is automatically destroyed after `exec()` returns; deferred QAction callbacks already target the pilot's context timer.
- `tests/qt_native_canvas_chrome.cpp` adds an offscreen nested-menu fixture that destroys its anchor while the popup is executing and checks the independent menu survives. Source commit `117fff2f401e7ef0269f80db299cbe0860783f20`; regression commit `6f1c32435022f2bbde65286d1137dd9aab5ca979`. Exact-revision WSL Android build and host Qt test execution not yet confirmed. No APK uploaded.

## November 15 — isolate version-history browsing from paint workers

- `MobileDocumentHome::showSavedVersions()` now sends history enumeration and KRA thumbnail decoding through the existing one-worker `recoveryPool()`, instead of Qt's global worker pool. Previously only recovery materialization used that isolated pool, while the history dialog's potentially long file scan and preview extraction still occupied a global worker shared with interactive drawing/fill operations. This also serializes history reads with recovery copies; gallery cancellation, queued-preview limit (four), and temporary-file ownership remain unchanged.
- Source commit `2d68d7482253b9254b1f012ffa44592173507ba3`. Confirmed the gallery contains no remaining global thread-pool submissions. Exact-revision Android build and on-device responsiveness have not yet been verified; no APK uploaded.

## November 15 — non-allocating native chrome discovery

- `MobileNativeCanvasChrome` now finds existing direct-child menu and status bars without invoking `QMainWindow::menuBar()` or `statusBar()`, which create widgets when none exist. The adapter's periodic 750 ms discovery previously could instantiate and retain unused desktop UI on a minimal native host simply to hide it. Existing desktop controls still retain their original visibility restoration semantics.
- `tests/qt_native_canvas_chrome.cpp` now exercises 20 enter/scan/exit cycles on a host with no menu/status bar and asserts neither widget gets allocated. Source commit `cad2cb8941c74efbc2b42209de98ced724a2cf57`, regression commit `a5cb37d398ff39709422baba5e0c38c156624238`. Android compile and Qt host regression for this revision are not yet verified. No APK uploaded.

## November 15 — guard toolbar event forwarding after gesture cancellation

- `MobileToolbarGestureButton::event()` now checks whether the button survived cancellation triggered by Hide, UngrabMouse, WindowDeactivate or disabling. Cancellation callbacks may synchronously rebuild the Android workspace and delete the button; the old event handler then forwarded the event to `QToolButton::event()` on deleted memory. Drag preview also rechecks the button after `setDown(false)` before accessing its state.
- `tests/qt_mobile_toolbar_drag.cpp` adds an active size-drag fixture whose Cancel callback deletes the button during WindowDeactivate delivery. Code commit `ebae2a7f0305697f2e99bda29d55f3d34b9f7db3`, regression commit `d1c4d2847740ee8426c05a413fcc77856b9abe52`. Exact-revision Android compilation and Qt host regression results remain unverified; no APK uploaded.

## November 15 — isolate saved-version recovery I/O from painting workers

- `MobileDocumentHome::openCopy()` now submits potentially large KRA recovery materialization to a dedicated single-thread Qt pool instead of Qt's global pool. This prevents recovery disk I/O from consuming the workers shared with interactive drawing/fill tasks and limits concurrent recovery materializations to one. Existing cancellation tokens, weak gallery/application references, and temporary copy ownership are unchanged; the gallery continues to refuse overlapping recovery requests.
- Source commit `88a3334d92cda6021a379d19ba028fa7978150fa`. Android exact-revision compile and real-device responsiveness have not yet been verified; no APK uploaded.

## November 15 — bound gallery source image decoding

- KRA embedded PNG previews and imported PNG/JPEG gallery items now use one Qt-only `MobileProjectPreviewCache::safePreviewDecodeSize` guard, rejecting dimensions exceeding 8192 on either side or 16 megapixels total before `QImageReader::read()`. Image plugins may ignore `setScaledSize()`: the previous 40-megapixel allowance could allocate about 160 MiB per worker for a 32-bit source bitmap, versus at most 64 MiB now. Two simultaneous workers amplify the transient memory impact. This affects thumbnails only; the original artwork remains openable and the gallery displays its placeholder for unsupported source dimensions.
- `tests/qt_project_preview_cache.cpp` checks accepted 4096x4096 and 8192x2048 dimensions, and rejected oversize/invalid sizes. Helper commit `1d20aedb5a63a0c53fa9499775ed013495f5f68b`, gallery integration `b9e20e3e9d2361e0913050878db92a852e5f9ccd`, regression `13dbb9b14fd4e9b911aa2878817488cdc60f1aef`. Exact-revision local Android build and Qt test results remain unverified; no APK uploaded.

## November 15 — cap legacy gallery thumbnail decoder allocations

- The thumbnail disk-cache reader now rejects PNGs exceeding 1024 px in either dimension or 1 megapixel total. The prior allowance of 4096x4096 could allocate approximately 64 MiB per image on Qt plugins that ignore scaled-decode requests; two concurrent gallery workers could compound transient Android memory pressure. New cache writes remain downscaled to 124 px, and older smaller cached previews remain readable. Oversized legacy cache entries miss and are regenerated from original projects.
- `tests/qt_project_preview_cache.cpp` now confirms that a 1100x1100 legacy PNG is rejected while its existing 640x480 legacy PNG remains readable. Code commit `7ca121406e6d09f64863e86523aed18a24f7ef5b`, regression commit `7b3cdde78e07329434555fbaa728fde62f2fb5e2`. Exact revision Android build and Qt regression have not yet been verified; no APK uploaded.

## November 15 — coalesced recent-project index writes

- The gallery's `KisRecentFilesManager::fileAdded` callback previously enqueued an independent JSON index load/merge/save job for every recent-file notification. Rapid file opens could accumulate redundant disk transactions and queued worker allocations while one serialized index pool was busy. The callback now accumulates unique pending paths in a shared, mutex-protected set and schedules only one drainer; each batch merges into the existing persistent index, and notifications arriving during a write are processed by the same worker's next pass. Project deletion and full-scan index work remain serialized on the original pool.
- Source commit `14ad29e4b27ee729504ef132f93aaf5567587903`. Exact-revision Android build is not yet confirmed; Android recent-notification burst behavior has not been measured on device. No APK uploaded.

## November 15 — native canvas input tracking on failed attachment

- Native KisView factory now unregisters a canvas from the Krita input manager when view initialization aborts after `addTrackedCanvas()` but before `setViewManager()` establishes the view's manager. The existing native KisView destructor deregisters canvases with an assigned manager; without this partial-initialization cleanup, the tracker could retain a stale canvas pointer after the failed view was deleted. The fallback does not duplicate cleanup for fully attached views and does not alter document save/close behavior.
- Source commit `7b6bf1b40323a63468dc83a8c474bababa78345a`. This path depends on the Android Krita engine and has no standalone Qt fixture; exact-revision local Android build and runtime behavior remain unverified. No APK uploaded.

## November 15 — bounded native chrome snapshot lifetime

- `MobileNativeCanvasChrome` now prunes dead `QPointer` entries from desktop toolbar/dock and tabbar visibility snapshots during its existing active-mode discovery pass. Replacing widgets while Mobile Studio remains active previously retained an entry for every destroyed Qt control until mode exit, allowing memory growth over extended document/workspace churn. Live controls keep their original restoration state.
- `tests/qt_native_canvas_chrome.cpp` exercises 120 sequential native toolbar, docker, and tabbar replacements in a single mobile session and checks restoration of the original canvas and desktop UI afterward. Source commit `ac4b981e7c110057e44474d25143d54da543d468`, test commit `ab6e32e0f354e887194d6881c78ce1c5b4b2c46d`. Exact Android compile and Qt host regression execution have not yet been confirmed; no APK uploaded.

## November 15 — Quick Actions dispatch and refresh lifetime

- Quick Actions button dispatch now copies its external command callable and checks the toolbar's QObject lifetime before refreshing. A command that closes a document, changes workspaces, or tears down mobile chrome can synchronously delete its own Quick Actions strip; the old clicked handler unconditionally called `refresh()` after dispatch.
- `MobileQuickActions::refresh()` uses a snapshot of guarded button pointers and checks the containing strip after show/enabled-state callbacks, so a synchronous toolbar rebuild does not invalidate the iteration or continue accessing deleted widgets.
- Code commit `ee3d3702d2585f23558efd3b7029c12c53c473e1`. This Android-only quick-action path has no standalone Qt regression in the current suite; exact SHA local build and Fold 6 runtime remain unverified. No APK uploaded.

## November 15 — toolbar gesture callback teardown

- `MobileToolbarGestureButton` now checks its lifetime after drag-preview callbacks, synthetic completion transitions and down-state changes; the gesture's inline-adjustment, color-sampling and long-hold callables are invoked from copies instead of potentially destroyed member function storage. A workspace rebuild during a preview/commit/hold must not continue dereferencing its deleted toolbar button.
- `tests/qt_mobile_toolbar_drag.cpp` now exercises buttons synchronously deleted from preview, commit, and hold callbacks. Code commit `2f18d8afe1b28c39be48bfa23a5163cb9a9d055c`, regression commit `1cc1c0da03adf339829c0d08456be13c534e3afa`. Android build and host regression execution for exact revision remain unverified; no APK uploaded.

## November 15 — touch scroll filter synchronous teardown

- The application-wide `MobileTouchScroll::MenuPointerFilter` now checks its QObject lifetime after scrollbar value changes and synchronous synthetic mouse-event delivery. Scrollbar listeners, button actions, or stylus gap dragging can destroy the filter during its own event handler; the old handler accessed members after these callbacks and could use freed memory.
- `tests/qt_mobile_touch_scroll.cpp` now simulates a scrollbar valueChanged listener deleting the active menu input filter during a drag. Code commit `df3fcc21f3f7380a6d8f6f619aca90f5d38a06d2`; regression commit `75b86125ea58def7e5851283aeb0db0b31ae0d2f`. Exact Android compile and host Qt regression execution remain to be verified; no APK uploaded.

## November 15 — Screen Layouts picker lifecycle

- The screen-layout picker now saves the selected layout and target profile before closing its editor. Since `closeEditor(false)` invokes workspace-completion callbacks that can synchronously destroy the `MobileNativeCommandLayout`, the picker checks its owning controller before replacing the profile or reloading the workspace; it no longer reads freed controller state after teardown.
- Source commit `2e08eead66778c3ecadd8044e0af0f7d19ff82ef`. The existing toolbar/controller Qt regression covers synchronous owner destruction on editor completion, but the picker branch is not yet separately exercised; exact-revision Android compile remains pending. No APK uploaded.

## November 15 — toolbar layout controller callback lifetime

- `MobileNativeCommandLayout::editCanvasLayout()` now checks its QObject lifetime after external editing/target callbacks. On layout-editor completion it guards the owning controller across `saveBars()`, toolbar `rebuild()`/`position()`, and the external `m_layoutEditing(false)` callback, avoiding use of freed state when a workspace rebuild or callback destroys the controller.
- Extended `tests/qt_native_command_layout.cpp` to destroy the toolbar controller synchronously from the editor-completion callback. Code commit `f8212795782189a6e08bc0a2b040a38b8dd9a14c`, regression commit `e3d999656ea5417e7e30ba99a7c08509a158a24e` (test type adjustment `cb71e5761333350d1cbe391d06bef3d3b61ebf2f`). Exact-revision Android build and Qt test execution are not yet verified. No APK uploaded.

## November 15 — layout editor synchronous callback teardown

- `MobileCanvasLayoutEditor::closeEditor()` now checks the editor's lifetime after hiding/restoring widgets and invoking placement and completion callbacks. These callbacks may rebuild the current workspace and destroy the editor synchronously; the old loop could continue reading freed editor members or call `deleteLater()` on a destroyed QObject. The completion callback is copied before external calls.
- `tests/qt_native_command_layout.cpp` now exercises a completion callback that deletes the editor during close. Code commit `0af161df1005269d945078808ebd115a16d0ceda`, test commit `d803233669a10616f48b52c235af946f861e2fa2`. Local Qt test and exact-revision Android build are not yet verified; no APK published.

## November 15 — preserve Android file intents during window replacement

- An Android document open that fails while its registered Qt window or document host is replaced now retains the path for the replacement host. The old implementation removed the path before `openFile()` and could silently lose a user-requested document during activity recreation. Retried delivery is deferred to the replacement window after the current draining transaction ends; genuine failures with an unchanged host still report once and are discarded. Successful openings are never retried.
- Code commit `d04b634bdcd95e5e10b1609b7959f2b0bcb99c4a`. Android-specific lifecycle regression remains to be checked on device; exact-revision WSL compile pending. No APK uploaded.

## November 15 — disabled thumbnail cache when app-private storage is unavailable

- Gallery preview loading now passes an empty cache directory if `QStandardPaths::AppDataLocation` is missing. The cache already treats an empty root as disabled; the previous caller accidentally built a CWD-relative `project-thumbs` directory and could write thumbnails outside app-private storage. Preview extraction remains available without persistent disk caching.
- Source commit `c3bde22fd0c53850119dd969507c8c42d4d71bc4`. Existing `tests/qt_project_preview_cache.cpp` covers empty-cache-root no-write/no-read behavior. Exact Android compile for this commit remains unverified; no APK uploaded.

## November 15 — native startup callback lifetime

- The independent native boot coordinator invokes a local copy of its launcher function rather than calling `m_launch` from its own storage. A synchronous window teardown during startup can delete the coordinator while the launch callback runs; the local callable survives until its invocation ends.
- `tests/qt_native_boot_coordinator.cpp` exercises a launcher that deletes its parent window before returning. Code commit `4c076d0ea07865ed6ab3b975ef9b7ac3062f5b33` and test commit `915cfa9bcd2ef251b7ba2b1383449b2ab192bedd`. Exact local build/test results remain unverified; no APK uploaded.

## November 15 — Android file-intent window ownership

- Android file-intent registration now tags window ownership by generation. A delayed `destroyed()` callback from a previous activity/window can no longer clear a newer window's document host. Reattaching the same window also avoids stacking redundant destroyed observers.
- Document-intent draining retains a local `QSharedPointer` for each `openFile()` call so synchronous window teardown cannot release the host while its method is running. Pending path deduplication and normal file-open behavior are unchanged.
- Source commit `82aa98298a7cf4167a347927c4778fe3d8ac37b1`. Local Android build and device-intent reentry are not yet validated for this exact revision; no APK uploaded.

## November 15 — touch dropdown model lifetime

- Touch combo initial-selection scrolling now captures a `QPersistentModelIndex` and verifies it still belongs to the list's displayed model before a queued scroll executes. A resource/model reset while a popup opens invalidates the saved row instead of passing a stale index into Qt's item view.
- `tests/qt_mobile_touch_scroll.cpp` now covers invalidation of the previously selected row after `QStringListModel::setStringList()` resets the model. Source commits `09af5dfbaad7c29e0fd3dd68e049d9a8189e5230` and `1fa6477e905443b5491072dfad4658c003fffd32`; exact-revision WSL compile and host Qt test execution still need confirmation. No APK uploaded.

## November 15 — tool browser refresh teardown guards

- Tool browser panel and scroll references are guarded during menu reconstruction. Showing the panel, replacing its tile tree, and writing favorite-tool settings can dispatch synchronous Qt layout/window callbacks; refresh now stops when its owning panel or scroll area has been destroyed, instead of accessing stale widget pointers. Existing favorites, tool selection, scrolling, and active-document validation are unchanged.
- Source revision `ae36e9cf6b7358eb6c909267bdc5856a32f66098`. Android compile and Fold 6 runtime for this revision remain unverified until the local watcher reports its exact SHA. No APK uploaded.

## November 15 — New Canvas IME and panel teardown safety

- Creating a canvas now guards the transient form across Android input-method dismissal before closing it. Showing the New Canvas panel also checks that both the panel and button box survived synchronous window/focus callbacks before assigning focus. Prevents stale QWidget dereferences without changing document creation, settings persistence, or canvas rendering.
- Source commit `ffa6dc1e9a1fd557a72f435138cd4a30f4abcdc9`. Exact-revision local Android compile and Fold 6 runtime are not yet verified; no APK uploaded.

## November 15 — deferred tool menu lifetime

- The tool browser now captures its parent canvas/window with `QPointer` before closing the transient tool panel. It cancels deferred tool activation if closing that panel synchronously destroys the parent; a surviving parent remains the Qt timer context, and the existing active document check still prevents retargeting tool actions to a different painting.
- Source commit `d3dfc0717b45c9465cbe676d2d1dde0ab6fb878c`. Android compilation and device behavior for this change remain unverified; no APK uploaded.

## November 15 — one-shot first GL frame activation

- The first-frame OpenGL `frameSwapped` listener is now a one-shot child receiver. It disconnects before scheduling the existing deferred activation and deletes itself after the callback, avoiding perpetual startup checks on every subsequent painting frame. The listener is also destroyed with its owning view if startup aborts.
- Change committed as `8a2169c6f47bf7014c95ff488fad02e4258dea7a`. Exact-revision local Android compile and Fold 6 runtime remain to be verified; no APK published.

## November 15 — canvas creation and brush/menu lifetime

- Native views size to the canvas stack before loading and rely on the stack for visibility. New blank canvases use Krita’s first projection on a ready surface, with a refresh retained if layout or surface initialization needs it.
- Brush capability checks run in short batches while the picker is visible. Popup refresh, touch replay, preset actions and native brush activation guard against teardown during callbacks. Canvas removal emits selection notifications after Qt’s layout removal completes, fixing a locally reproduced teardown crash. Device-specific crash reports remain needed to identify other failure paths.
- Item-view scrolling tolerates synchronous filter teardown when a tap stops a fling. Brush mode changes verify the original resource provider after native actions, and the canvas switcher safely unwinds if its owner closes during the modal dialog.

## November 15 — Android launch splash

- Android shows the Krita launcher icon on Studio’s dark background during cold startup. The native splash fades out when Home or the initial canvas is ready, with no added minimum wait.

## November 15 — Settings grouping, presets, and command browser

- The gear lists Settings then Filters, with layout editing, toolbar setup, preferences and the floating wheel history toggle under Settings. All Krita Commands is available from the gear and is excluded from new toolbar assignments.
- The command browser uses an in-window panel and validates its document and command before dispatch. Settings presets save preferences and a screen layout, and applying one updates the active screen. Artwork, brush resources, and stored project history are independent.

## November 15 — compact three-tab Color picker

- Wheel, Sliders (Hex and HSV), and Lighting have separate tabs and popup sizes. The shared picker history shows up to 24 stroke colors across three rows. The toolbar history stays compact.
- In Lighting, enable **Adjust light direction** and drag on the sphere. The option starts inactive, updates both sphere previews, and stores the direction alongside the lighting settings.

## November 15 — compact toolbars, preset import, and lighting controls

- Settings action lists size to their rows within the available canvas. Main and custom toolbars fit their visible commands and scroll if they run out of space.
- Paint, Erase and Blend share a native .kpp importer. Their Import category lists presets imported through Studio that are compatible with the current mode. Existing presets are protected from filename collisions.
- The lighting sphere follows external foreground picks by default and keeps its base when sampled. Lighting includes intensity alongside hardness, and hold the floating sphere to open the same controls. A tap or drag samples the sphere.

## November 15 — minimize Layers from the Add menu

- The rail footer displays the active layer name as a label. The **+** menu contains **Minimize Layers Panel**, and the small Layers icon reopens the minimized rail.

## November 14 — create canvases inside the existing Android window

- New Canvas uses an in-window touch panel. Create closes the form and queues native document creation after the input event completes. This removes the separate modal window transition from the first canvas reveal.
- Android rendering still requires device verification.

## November 14 — restore tool tile height and fit Color to its preview

- Tool grid cells own their 40 px row height, independent of the shared button stylesheet. The scroll content retains the full grid height.
- The Color popup trims its width to the active wheel or lighting sphere and the controls below it. Color comparison swatches sit in the wheel's corner, and the popup still avoids toolbar strips.

## November 14 — shared touch menu presentation and canvas name entry

- Tool, brush and contextual popovers share a palette and control styling with command lists and mobile dialogs. The canvas More menu and project sort use the same transient touch list, including nested commands and outside dismissal.
- The canvas name editor is outside the kinetic scroll viewport. Editable fields request Android input after Qt commits focus on a stationary tap. Brush and eraser hover outlines and cursors are hidden on the first launch with this change.
- Android keyboard, cursor, and menu behavior await device verification.

## November 14 — copy crash reports and initialize the first canvas frame

- **Copy crash report** is available on Home and in the canvas More menu. It copies the previous launch checkpoint and Android's recent exit reasons, with a bounded native crash or ANR trace when the OS supplies one. Capture and copy are local.
- The first visible native view now activates its layout and refreshes Krita's image projection instead of retrying an ordinary repaint on a timer. The Android viewport updates the canvas geometry on viewport-only resizes. Fold behavior and reported random crashes remain unverified.

## November 14 — canvas entry, picker responsiveness, and tool stability

- New canvases request a repaint as Home closes, with one guarded follow-up for Android surface transitions. Tool lookup and option handling now validate the active canvas and document before applying controls.
- Brush lists lay out in batches and keep capability cache entries for unaffected presets. Double-tap mirror allows more time and finger movement between taps. The legacy 150 ms setting migrates once to the 350 ms default.
- Home shows recovered work through Recent and All Projects. The Color popup fits around toolbar history strips and gives the wheel more room. Device behavior remains unverified.

## November 14 — isolate archive snapshots from interactive Qt workers

- Audited the background version-history tasks: both `enqueueDraftCheckpoint()` and `enqueueAfterSave()` dispatched work into `QThreadPool::globalInstance()`, but each then serialized all archive I/O under the same recursive archive mutex. Multiple document saves could occupy global-pool threads blocked on that lock, reducing the worker capacity available to fill-tolerance previews and other interactive operations.
- Both archive queues now use a dedicated, function-local static `QThreadPool` with `maxThreadCount(1)`. Only one archive copy/adoption worker executes at once; queued distinct-project requests do not consume global thread-pool workers. Existing per-project generation/coalescing, archive mutex, atomic QSaveFile publication, and save-history behavior are unchanged.
- **Verification:** most recent WSL Android SUCCESS reported at `d81b42421232e8b4057abd6f7d8af48320e1712e`. This change awaits an exact-SHA Android build and sustained multi-document save/resume runtime checks. No APK uploaded.


## November 14 — revalidate document ownership after native tool activation/repaint

- Audited `MobileKritaNativeViewFactory::scheduleActivation()` during Android resume and tab switching. `initializeCurrentToolForCanvas()` and `canvas->updateCanvas()` can dispatch engine/Qt callbacks that close a document or change the active native document registration while retaining a live QWidget.
- After each call, activation now requires the selected `KisView` to have a live document that still matches the current document registered in `MobileNativeViewStack`, in addition to the existing manager/root/view and canvas identity guards. This prevents continuing to repaint a view that no longer owns the active document.
- **Verification:** prior WSL build reported `79319e5693b954dc730607f36e472ad3a6ec4ef6` SUCCESS; this engine-only change awaits its exact-SHA Android compile and runtime exercising of close/switch/resume reentrancy. No APK uploaded or phone test requested.


## November 14 — guard version-history root when app storage is unavailable

- `MobileSavedVersionArchive::folderFor()` previously constructed the default history root with `QDir(QStandardPaths::writableLocation(AppDataLocation))`. If Android returned an empty application-data path, Qt could interpret it relative to the current working directory, causing snapshots or history migrations to target an unintended location.
- Added `privateVersionRoot()` to explicitly return empty for an unavailable app-data directory; default `folderFor()` now fails closed. Existing nonempty private roots and explicit test root overrides retain their behavior.
- Extended the Qt saved-version archive regression to verify both empty-root rejection and valid private-root resolution. No existing archive or artwork files were deleted.
- **Verification:** latest reported local Android build SUCCESS at `5b0590758b00e5a705c2f26b9171ea97ee38c71b`. The new source and test await exact-SHA build verification; no APK uploaded.


## November 14 — fail closed on missing thumbnail cache directory

- Audited Android gallery thumbnail-cache filesystem handling. If `AppDataLocation` failed to resolve, `MobileProjectPreviewCache` could be constructed with an empty root. `pruneLocked()` would then enumerate relative `*.png` entries, risking deletion of unrelated images in the process working directory. `load()` could likewise resolve a relative hashed cache filename.
- `load()`, `store()`, and `pruneLocked()` now short-circuit when the configured directory is empty. The cache is disabled until an explicit storage root is available; no artwork or external files are touched by cache maintenance.
- Extended `qt_project_preview_cache.cpp` to change into a disposable temporary working directory, create an unrelated PNG, invoke empty-root prune/store/load, and confirm that PNG remains intact. The test restores its original working directory before continuing.
- **Verification:** latest reported Android build SUCCESS at `59f209cb115117af4e29ad2c7b3285f363ff1eb9`. The empty-root guard and Qt regression await exact-revision compilation and test execution. No APK uploaded.


## November 14 — bound all decoded gallery preview sources

- The on-disk thumbnail-cache decoder had a 124 px post-decode limit, but direct PNG/JPEG imports and embedded KRA/SAF PNG previews used `QImageReader::setScaledSize()` without bounding the actual returned `QImage`. Image plugins that ignore the scaled-size request could leave oversized bitmaps retained in gallery worker results and image icon caches.
- Added a common `MobileProjectPreviewCache::boundedThumbnail()` helper and applied it to cached PNGs, direct imported image previews, and decoded KRA/SAF previews. It preserves valid small images, downscales unexpected larger results to a maximum 124 px in either dimension, and returns null on decode/scale failure. Cancellation is checked around the direct decoders.
- Extended `qt_project_preview_cache.cpp` with an intentionally oversized decoded image (900×600) and asserts the 124×83 output, unchanged 90×60 small preview, and null-input handling.
- **Verification:** latest reported local Android build SUCCESS at `309e6b88e4b2e8b92f3dda1ba9f07d1ca4c911a7`. These changes await exact-SHA Android build and standalone Qt test. No APK uploaded.


## November 14 — keep project-index writes independent of scan cancellation

- Investigated the previous All Projects scan coalescing change. `startScan()` calls `m_scanner.clear()` to discard old scan runnables, but the same thread pool also carried recent-file index saves, project deletion index updates, scheduled index snapshot persistence, and startup maintenance. Refreshing All Projects could therefore silently discard a queued durable index update.
- Created a separate single-threaded `m_indexWorkers` pool for those four non-scan job types; the existing `m_scanner` now contains only cancellable directory scans. Refresh still cancels stale scans without cancelling index changes, while index operations remain serial and their ordering is preserved. Gallery destruction now waits for index workers as well as scan and thumbnail workers.
- **Verification:** previously reported local Android build SUCCESS at `3523b2a8a614cfae39abffcf8edbc296b3ae670d`. The new queue separation awaits exact-SHA compilation and runtime refresh/delete/persistence checks. No APK uploaded.


## November 14 — rescue pending drafts without skipping eligible files

- Audited the bounded startup draft-rescue cursor. `MobileDraftRecovery::list()` advanced the shared `QDirIterator` over up to 128 pending files but only validated two qualifying old orphans. The other candidates had already been consumed and could remain hidden from recovery until an entire directory traversal wrapped.
- The iterator now stops as soon as it has selected two age/path-eligible staging files, still examining at most 128 entries per gallery visit. Slow KoStore validation and publication remain outside the cursor mutex; the earlier size/mtime recheck and non-overwriting rescue semantics remain intact.
- Expanded the native-free `qt_saved_version_archive.cpp` regression to stage six old `.pending.kra` exports and assert that repeated bounded scans rescue all of them without skipping the candidates following the first two. Test fixtures are removed after recovery.
- **Verification:** previous reported WSL Android build SUCCESS at `bd9366aa02431ca95725b3a708047fb3fe1d8f77`. This fix and its standalone Qt test await exact-SHA verification. No APK uploaded.


## November 14 — enforce gallery decoded-thumbnail memory bounds

- `MobileProjectPreviewCache::load()` requested a 124×124 scaled PNG decode, but returned `QImageReader::read()` without verifying the decoded dimensions. A plugin that ignored the requested size could allow a large legacy cache PNG to persist in the gallery's in-memory icon cache.
- Post-decode validation now rejects failed/cancelled reads and downsizes any returned image whose width or height exceeds 124 px. This preserves the scaled-decoder fast path while bounding the retained icon dimensions.
- Expanded `qt_project_preview_cache.cpp` with a genuine legacy full-resolution PNG written directly to the cache path. The regression verifies its 124×93 decoded preview and pixel content; the existing small thumbnail and cache-retention tests remain unchanged.
- **Verification:** most recent local Android build reported SUCCESS at `a5cfe6725c87940fe7d97b3f5d2016a5bba5f284`. These two changes await exact-SHA Android compilation and standalone Qt regression execution; no APK was uploaded.


## November 14 — retire canvas resume observers after watched root/window destruction

- A native canvas resume observer is owned by the longer-lived view manager, not necessarily by its watched document stack or Android top-level QWidget. When a native root/window is replaced while its manager remains alive, the old observer (including its surface event filter and activation closure) could survive until manager teardown.
- `MobileCanvasResumeObserver::attach()` now schedules observer deletion when **either** watched canvas root or top-level window emits `destroyed`. Qt's normal QObject event-filter teardown and guarded timer receiver semantics remain in use; future replacement creates a fresh observer for the new root.
- Extended `qt_canvas_resume_observer.cpp` to check prompt deferred destruction both when the original root is deleted and when a separate owner/manager survives that deletion.
- **Verification:** last reported local Android build SUCCESS `2c35f8f7e419c47e7621f34c3a559902e27d5f5b`. This lifetime change and standalone Qt regression await exact-SHA build/test verification. No APK uploaded.


## November 14 — coalesce repeated All Projects scans

- Audited the single-threaded `MobileDocumentHome::m_scanner` queue. Repeated Refresh taps could enqueue multiple already-obsolete full scans behind an active scan, retaining runnable captures and delaying the latest requested results. Cancellation tokens alone stopped old work once it began but did not remove queued runnables.
- `startScan()` now cancels the previous scan token and calls `m_scanner.clear()` before enqueuing the new scan. The active scan observes cancellation; older pending scans are discarded, so only the newest refresh remains scheduled. The gallery index and saved artwork remain unchanged.
- **Verification:** prior reported WSL Android SUCCESS at `00fb668e07d233fd49a969f9c448dd9cf11c36b3`. This queue change awaits exact-SHA compilation and refresh-under-load testing. No APK uploaded.


## November 14 — reject incomplete Android provider thumbnail transfers

- Audited the Android SAF thumbnail path in `MobileDocumentHome::preview()`. It receives a Java byte array of compressed KRA preview data and copies it with JNI `GetByteArrayRegion()` before decoding. A failed JNI transfer left an exception pending and the decoder could proceed with incomplete data.
- The gallery now verifies the bounded `QByteArray` allocation, checks cancellation before the copy, and detects/clears JNI copy exceptions. Failed provider previews return empty instead of being decoded or cached.
- This path is Android-only, so existing host-side Qt thumbnail tests do not exercise Java/JNI exception behavior. **Verification:** last local WSL build was `5c8184f3ed7c390cb3041549f52f6d11efd890fb` (SUCCESS); current change awaits its own exact-SHA Android build. No APK uploaded or device test requested.


## November 14 — avoid queued fill-preview workers retaining image buffers

- Audited `MobileFillTolerancePreview::requestMask()` after the latest memory-allocation safeguards. Each preview submission captured the sampled pixel and selection buffers into a `QThreadPool::globalInstance()->start()` closure. If Krita/gallery workers saturated the global pool, superseded previews could remain queued and retain buffers while their associated tool or canvas was already closing.
- Switched this path to Qt 5.15 `QThreadPool::tryStart()`. If no worker slot is immediately available, Qt does not enqueue the closure; Studio clears the in-flight flag and schedules a single 55 ms widget-owned retry. A retry uses the current generation/threshold rather than holding a stale worker snapshot. Running workers still use the existing generation cancellation and guarded Qt callback.
- **Verification:** the previous WSL Android build passed `ce636391f7a397650913deedcef38089bcd32207`. The non-queuing preview change awaits its own exact-SHA compilation; no device test or APK upload was performed. Check pool-saturation and rapid tool-switch behavior when a disposable device test batch is next available.


## November 14 — guard fill-preview byte-array allocations before pixel reads

- A further audit of interactive Fill/Select preview sampling found unchecked `QByteArray::resize()` outcomes for the source pixels and optional selection coverage. Passing an undersized buffer to Krita's `readBytes()` under Android low-memory conditions could corrupt memory. The generated flood-coverage byte array had the same assumption.
- Source and selection buffers are now sized and checked **before** either engine read; failed allocations clear any partial cached buffers and release the image barrier lock before returning. The worker validates its generated coverage allocation, reports completion safely, and retries only when a newer tolerance request supersedes it.
- Existing bounded dimension/stride regressions remain in place. Allocation failure is not directly reproduced by the lightweight Qt tests; exact-SHA Android compilation and device-level low-memory behavior remain unverified.
- **Verification:** last reported WSL Android build: `unknown`; this patch awaits its own build report. No APK uploaded.


## November 14 — validate computed flood-selection mask stride

- The interactive Fill/Select preview already rejected unsupported *input selection* pixel strides, but its generated flood mask was still read unconditionally into a one-byte-per-pixel `QByteArray`. Unexpected engine mask formats could write beyond the allocation.
- `MobileFillTolerancePreview::requestMask()` now validates the generated mask's actual `pixelSize()` using the bounded alpha8 helper before `readBytes()`. Unsupported output masks follow the existing no-result completion path without an unsafe read or tight retry loop.
- Clarified the standalone Qt pixel-stride regression's coverage of both incoming and generated selection-mask allocations.
- **Verification:** local WSL Android build SUCCESS for previous thumbnail change `006ad56d8b444a0b7cf8ea3b10a0de79c244d633`. The newest generated-mask check and regression comment await exact-SHA local build; no APK uploaded.


## November 14 — validate fill-preview selection coverage before reading

- Audited the asynchronous Fill/Select tolerance overlay after its source sampling buffer limit was added. Selection coverage was allocated as width × height bytes, but `KisPixelSelection::readBytes()` was called without checking its pixel stride. An unexpected non-alpha8 source format could write beyond that buffer during tool/canvas transitions.
- Added a Qt-only `mobileFillPreviewSelectionBytes()` guard requiring exactly one byte per pixel and the existing 384×384/16 MiB preview limits. The sampling path refuses unsupported selection formats or missing source color spaces before acquiring its engine read barrier and copying bytes.
- Extended `qt_fill_tolerance_drag.cpp` regression cases for alpha8, 2-/4-byte unexpected strides, empty and oversized selection windows. No drawing or persistent project data is modified.
- **Verification:** exact-SHA local Android build and standalone Qt regression for these commits are pending. Last reported build: `unknown`. No APK uploaded.


## November 14 — reduce gallery thumbnail encoding memory

- `MobileProjectPreviewCache::store()` previously PNG-encoded full-size gallery source images (up to 4096×4096) before applying a disk-size cap. Large noisy previews could consume substantial temporary encoder memory on Android even though the UI displays at most 124×124 pixels.
- The cache now downscales to the actual gallery display size **before** encoding, skips a failed scale allocation or cancelled request, and retains the existing encoded-size and cache-retention guards. The legacy load-side downscale remains to handle old oversized PNG entries.
- Updated the Qt thumbnail regression to check that a 3500×3500 noisy source produces a readable, bounded 124×124 cached preview instead of spending memory encoding an unused full-resolution image.
- **Verification:** latest reported WSL Android SUCCESS remains `ceba8f66c5378aa7af8e4355dc1e72257780e1e1`. New preview allocation and thumbnail encoder commits await exact-SHA local Android build and standalone Qt regression execution. No APK uploaded.


## November 14 — bound interactive fill-tolerance preview sample allocations

- Audited the asynchronous Fill/Select tolerance preview while waiting for canvas cleanup builds. `MobileFillTolerancePreview::requestMask()` directly computed and resized its raw pixel sampling buffer with `width * height * pixelSize` using implicit integer arithmetic and assumed engine image/source were available. An unexpected colorspace stride or stale/empty preview source could cause excessive allocations or null dereferences during tool/canvas transitions.
- Introduced a Qt-only `mobileFillPreviewBufferBytes()` helper that validates preview geometry against the 384-pixel window limit, uses 64-bit multiplication, and caps a single raw sample at 16 MiB. `requestMask()` skips unavailable sources/invalid tolerances and refuses unsafe byte counts before taking an image barrier lock or allocating the source byte array.
- Added `qt_fill_tolerance_drag.cpp` regression coverage for normal 4/16-byte pixels, empty/oversized preview dimensions, zero stride, and an unexpectedly large pixel stride.
- **Verification:** local WSL Android build passed for canvas retirement source commit `0a4f9d2be541fffc93bd23433e484abb92d2fcef`. The fill-preview change and its standalone Qt regression require exact-SHA compilation/test execution. No APK uploaded.

## November 14 — revalidate canvas ownership when deferred retirement executes

- Follow-up audit of native canvas teardown found a gap in the ownership checks: document destruction and accepted-close callbacks checked whether a detached QWidget still belonged to the old stack before calling `deleteLater()`. A different host could reparent the widget **after** that check but **before** Qt delivered the already-posted `DeferredDelete` event, causing the old owner to destroy a now-transferred painting canvas.
- Consolidated both retirement paths into `MobileNativeViewStack::retireDetachedView()`. It defers the **ownership decision** to a zero-delay Qt callback bound to the original stack, revalidates that the widget still exists, is still parented to that stack, and is not re-registered, and only then calls `deleteLater()`. Teardown of the original stack automatically cancels its pending callback. The previous close/reentrancy guard behavior is preserved.
- Extended offscreen Qt lifecycle regressions to transfer a canvas **after** document deregistration but before deferred cleanup; verified that it survives under its new owner. Updated existing deferred-deletion fixtures to pump the zero-delay ownership-check callback before asserting cleanup.
- **Verification:** last reported exact-SHA local WSL Android build SUCCESS at `c38816694302edd488b862fb4efd7ab3bcc335e4`. These changes await exact-SHA WSL compilation and execution of the standalone Qt regressions. No APK uploaded.

## November 14 — preserve transferred canvas visibility during native tab removal

- Inspected the recently added reparenting safety guards after exact-SHA Android compilation. `MobileNativeViewStack::forgetToken()` removed a widget and then unconditionally called `view->hide()`. `removeWidget()` emits `currentChanged` synchronously; a listener can reparent and show the detached widget before the old stack resumes. The unconditional hide then incorrectly hides the newly owned canvas, potentially presenting a blank drawing even though no document was closed.
- `forgetToken()` now hides only a still-live widget that is still parented to the old stack and is no longer registered there. Views transferred to a different owner or synchronously re-registered retain their new owner's visibility.
- Strengthened the standalone Qt two-owner lifecycle regression: during `currentChanged`, the listener reparents and explicitly shows the transferred canvas; after document teardown the canvas must still exist and remain unhidden.
- **Verification:** local WSL Android build SUCCESS for `c38816694302edd488b862fb4efd7ab3bcc335e4` (2026-11-14 16:31:46 UTC). Current visibility fix and enhanced Qt regression await exact-SHA Android compilation and standalone Qt execution. No APK uploaded.

## November 14 — prevent external document teardown from deleting transferred canvases

- During engine-owned document destruction, the native stack's `documentDestroyed` callback deregistered the associated view and scheduled `deleteLater()` if it was still alive. That path lacked the ownership checks already present after accepted explicit closes. `removeWidget()` emits `currentChanged` synchronously; one of its observers can reparent or re-adopt the view, in which case the original stack must not delete the newly owned widget.
- `MobileNativeViewStack::adoptView()` document-destruction observer now schedules deferred canvas deletion only if the registration was actually removed, the stack/view survived, the view is still parented to that stack, and its widget is no longer registered there. Existing orphan cleanup remains in effect for genuinely unowned canvases.
- Added Qt lightweight-host regression that transfers the removed canvas to an external QWidget from `currentChanged`, destroys the original document, processes deferred deletions, and verifies the moved canvas survives while the original stack retains only its unaffected document.
- **Verification:** prior WSL Android build passed exact commit `e071e04dee348cdcb32332f4667eb0d5872a7ad8`. The new ownership guard and Qt regression await exact-SHA Android compilation and standalone Qt test execution. No APK uploaded.

## November 14 — prevent recursive native canvas closes during tab removal

- `MobileNativeViewStack::closeImpl()` previously reset `m_closing` immediately after an accepted native close callback, **before** calling `forgetToken()`. Removing the selected widget can emit `QStackedWidget::currentChanged`, which reaches Krita view activation and other Qt observers synchronously. A reentrant close could therefore target the newly selected, still-open canvas while the first transaction was being torn down.
- The close transaction now leaves `m_closing` set until deregistration and surviving-widget retirement have finished, while still resetting it on rejected callbacks. The existing `QPointer` stack identity guard covers synchronous destruction during removal.
- Added a standalone Qt regression that registers two canvases, closes the active second canvas, tries to recursively close the surviving first canvas from `currentChanged`, and asserts the nested call is denied and the survivor remains registered.
- **Verification:** most recent reported WSL Android SUCCESS at exact commit `db077496d19079479e52619078187aa617450ed6`. This transaction-guard fix and Qt regression await exact-SHA Android compilation and standalone Qt execution. No APK uploaded.

## November 14 — retire native canvases after accepted close callbacks

- An accepted `MobileNativeViewStack::closeImpl()` transaction removed the widget registration but delegated destruction entirely to the close callback. While the Krita view factory normally deletes its `KisView`, another adapter/callback may accept without deleting, leaving a detached native widget/GL surface parented to the root until app exit.
- `closeImpl()` now captures the closing QWidget as a `QPointer`, completes the existing synchronous close contract, and after successful deregistration schedules `deleteLater()` only if the widget remains alive, still belongs to the original native stack, and is no longer registered. Already-destroyed/reparented widgets are left alone. Reentrant host destruction remains guarded.
- Added standalone Qt lightweight-host regression where an approved close callback intentionally leaves its QWidget alive; the row must disappear and the widget must be deleted upon processing deferred deletes. Existing explicit save/discard cancellation and multi-document tests are unchanged.
- **Verification:** the previous local WSL Android build was SUCCESS at exact source SHA `f7cab92d758da016bff49db6313b7dbdd3c57db9`. Current accepted-close cleanup and Qt regression require exact-SHA local compilation/tests. No APK uploaded.

## November 14 — isolate native orphan-canvas cleanup from other open tabs

- Reviewed the previously committed native orphan-view cleanup after its exact-SHA Android build passed. `MobileNativeViewStack` could schedule `deleteLater()` on a guarded view even if its registration token had already been removed by a reentrant close. The document-destruction callback now schedules canvas deletion only when `forgetToken(token)` actually removed that registration and the stack still exists, preventing accidental deletion of a view under another owner.
- Expanded `qt_lightweight_canvas_host.cpp` lifecycle regression to attach two independent documents, destroy one externally, verify only its orphaned canvas is deleted after deferred events, ensure the surviving document remains active and registered, then destroy the second document and check its canvas is released.
- **Verification:** exact-SHA local WSL Android build SUCCESS for `a4ce0952fdd0f940e0353812734bff9d908769d6` at 16:19:36 UTC. Current narrow guard and expanded regression await their own exact-SHA WSL compilation and standalone Qt test result. No APK upload requested.

## November 14 — release orphan native painting widgets on engine document destruction

- Audited the independent native canvas/document lifecycle. `MobileNativeViewStack::adoptView()` connected document destruction to `forgetToken(token)`, which removes the Qt stacked-widget registration and hides a still-existing view, but does not delete it. If Krita destroys a document before its painting QWidget, that detached, hidden view and its GL/canvas resources could remain parented to the root until the entire window is destroyed, progressively retaining memory across repeated document closes.
- The document-destruction observer now captures a guarded canvas widget. After forgetting its registration, it calls `deleteLater()` for a still-live orphan only if the view stack survived synchronous `currentChanged` callbacks. The normal explicit close callback remains responsible for native view teardown; the view-destruction observer still only removes registration.
- Added a Qt lightweight-host regression that destroys a registered document while its view remains alive, verifies the native row is removed, processes deferred deletion, and verifies the orphaned QWidget is freed.
- **Verification:** last WSL Android success was for `9d054eac82be590f991e58d585eac1562761394c`; these new native-view fixes and tests still require their exact-SHA compilation/test results. No APK uploaded.

## November 14 — cancel gallery image decoding before allocating thumbnail buffers

- Audited native canvas teardown paths and the gallery thumbnail workers. The thumbnail pool cancellation was checked before and after `preview(path)`, but `preview()` itself could continue JNI byte-array transfer, KoStore embedded-preview extraction, and PNG decoding after the gallery had been dismissed. These are potentially substantial CPU and memory allocations on a foldable device while another drawing is being painted.
- Threaded the existing per-gallery atomic cancel token into `preview()` and its PNG decoder. Checks now run before JNI bytes are copied, before local KRA/ZIP access and each embedded-preview lookup, after reading embedded bytes and before decoding PNG data. `MobileProjectPreviewCache::load()` accepts the same optional token and checks it before starting cached image decoding. No thumbnail, recovery document, or drawing file is deleted by cancellation.
- Added Qt regression for a pre-canceled cached PNG load followed by a successful retry after clearing cancellation. Native Krita/SAF thumbnail behavior still needs device verification.
- **Verification:** preceding local WSL Android build succeeded for `b806c9fa1146b7b8f53705cf07d906b77da210d7`. New changes await exact-SHA WSL Android compilation and standalone Qt tests. No APK uploaded.

## November 14 — cancel oversized Version History scans after dialog dismissal

- A Version History dialog was already limited to the newest 256 archive rows, but its background `listForBrowser()` could still continue iterating a huge interrupted-pruning history folder after the dialog was dismissed. The scan holds the archive I/O mutex, so unnecessary enumeration could also defer new checkpoints or Save As migration.
- Added an optional atomic cancellation token to `listForBrowser()`, checked before acquiring the archive mutex, after folder resolution, during directory streaming, and before returning results. `showSavedVersions()` now passes its existing dialog-lifetime cancellation token. Canceled listing returns no partial results, does not delete or modify any archived artwork, and can be retried immediately.
- Extended the Qt archive regression to cover pre-canceled listing (all 300 source KRAs remain), followed by a successful fresh scan returning the newest 256.
- **Verification:** previous exact-SHA local WSL Android build passed `ca8c24353cbecbd97dd69ea65195e47eae4cc2e1`; this cancellation path and regression await exact-SHA Android compilation and standalone Qt test. No APK uploaded.

## November 14 — bound Version History browser rows after interrupted cleanup

- A large history folder left behind by interrupted retention could make the Version History dialog call the unbounded `list()`, then create a `QListWidgetItem` and queued preview for **every** archived KRA. This risks main-thread freezes or out-of-memory failures even though normal retention aims for only 12 versions.
- Added `MobileSavedVersionArchive::listForBrowser()`: streams KRA metadata under the existing archive mutex, selects the latest 256 by canonical timestamp/filename using a bounded oldest-first heap, and sorts those entries newest first. The Version History dialog uses this new method; `list()` remains complete for internal retention and tests, and no disk files are removed by browsing.
- Added Qt archive regression with 300 files: browser must return 256 newest in correct order, full listing remains 300, and oldest KRA stays on disk.
- **Verification:** last reported local WSL Android SUCCESS was exact commit `3e770b21cca0da4d37d525cac00a9ba6f777eb72`. These commits require new compilation and standalone Qt regression verification. No APK uploaded.

## November 14 — preflight local KRA previews before native ZIP decoding

- The gallery's background `preview()` path previously passed any readable local `*.kra` to `KoStore::createStore()`, including symlinks, truncated files, and unexpectedly large archives. This can cause unnecessary archive parsing, memory pressure, or long worker stalls when shared storage includes damaged documents. Introduced the Qt-only `MobileProjectPreviewCache::safeLocalKraSource()` preflight (regular non-linked file, 4 bytes–8 GiB, ZIP local-file header) and call it before opening a local KRA with KoStore. Non-KRA image preview dimensional bounds remain unchanged.
- Added `qt_project_preview_cache.cpp` checks accepting a normal ZIP header and rejecting invalid header, symlink, and an oversized sparse input file; this is a cheap safety preflight, not full ZIP validation.
- **Verification:** the last exact-SHA WSL Android build passed for `eec46dada594268c13ce7608820adb4bf7a88be4`. The new preview checks and tests await their own Android compile and standalone Qt test result. No APK uploaded.

## November 14 — release gallery lists and thumbnail icons when hidden

- `MobileDocumentHome::hideEvent()` previously canceled pending thumbnails but kept all `QListWidgetItem` project rows (up to 20,000), the per-row icon pixmaps, the decoded preview icon cache, draft metadata, and saved preview revision maps alive while the user painted. `showHome()` already rebuilds the gallery with `showList()`, so holding these hidden UI allocations has no functional benefit.
- After snapshotting the All Projects index and canceling thumbnail requests, gallery hide now clears the row/index maps and `QListWidget` items, draft/index preview metadata and thumbnail icon caches. The background index writer owns its copied snapshot, and old queued scan/thumbnail completions are rejected by existing cancellation/generation checks. Tab selection and on-disk project history are unchanged.
- **Verification:** preceding recovery-cancellation revision `69607ffb3e550caaeabae8399eb6fb76ab4f0a8f` passed local WSL Android compilation at 15:56:56 UTC. New hidden-gallery cleanup awaits exact-SHA Android compilation and device memory profiling. No APK uploaded.

## November 14 — cancel long Version History recovery copies on gallery teardown

- `MobileSavedVersionArchive::materializeRecovery()` could copy up to 8 GiB of archived data into a private recovered KRA while Android destroyed the gallery. Even though the eventual temporary file was cleaned up, the worker continued disk I/O, hash calculation, and disk-space consumption until completion.
- Added an optional cancellation token checked before the copy, during each 256 KiB read/write chunk and again before `QSaveFile::commit()`. On cancellation, the staged recovery copy is discarded and the historical archive remains untouched.
- `MobileDocumentHome::openCopy()` passes a **per-request** cancellation token; gallery hide/destruction invalidates it. Per-request allocation prevents a later recovery from accidentally reactivating an older canceled worker. Closing the gallery also clears its restoration-in-progress flag, so reopening it does not remain blocked. Existing same-workspace validation still prevents a canceled copy opening in a replaced canvas.
- Added Qt archive regression for cancellation before copying and successful retry with a fresh/uncancelled request.
- Fixed a follow-up stale-completion race: an older canceled worker's queued completion no longer clears `m_restoringVersion` for a newer recovery after the gallery is reopened. Only the active cancellation token can change that state.
- **Verification:** await exact-SHA WSL Android compilation and standalone Qt archive regression execution; no APK uploaded.

## November 14 — repair thumbnail-cache retention regression fixture

- The new early PNG-signature filter correctly rejects three-byte fake files. The existing 620-entry newest-first cache retention regression, however, populated its fixture with precisely these fake `"png"` bytes, so that test no longer exercised the bounded 512-entry heap after the signature change.
- Updated the historical retention fixture to write the eight-byte PNG signature into each file. The test continues to verify that a 620-file backlog retains the newest 512 entries, while separate tests cover empty, oversized, and invalid-signature files being evicted. The fixture intentionally tests cache retention, not full PNG decoding.
- **Verification:** the local WSL Android watcher reported SUCCESS for exact commit `4cb252212cf7c23d755d102c6ee0edddcba7c6e9` at 2026-11-14 15:47:22 UTC. The watcher reports Android compilation, not standalone Qt regression execution. No APK upload requested in this development turn.

## November 14 — reject corrupt nonempty PNGs before thumbnail retention selection

- Audited the new size-filtered 512-entry thumbnail retention heap. Nonempty corrupt files smaller than the 32 MiB per-file limit could still occupy the newest candidates, evicting older valid thumbnails during startup cleanup. `MobileProjectPreviewCache::pruneLocked()` now performs a bounded eight-byte PNG signature check for candidates before admitting them to the top-512 heap. The second streaming pass removes the rejected cache PNGs. No image decode is performed during pruning; 128 MiB quota remains unchanged.
- Added a Qt regression with 520 newer nonempty invalid-signature PNG files and an older valid preview. The valid preview must remain loadable and the corrupt files must be removed. The earlier zero-length/oversized file tests remain intact.
- **Verification:** previous reported local WSL Android build succeeded for `d88f8ffe39700c4298639f3b24e4f8a969b29e56`; the PNG-signature change and regression await their exact-SHA build and test. No APK uploaded.

## November 14 — bound queued Version History image previews

- `MobileDocumentHome::showSavedVersions()` streamed archive previews sequentially from a worker but queued a separate decoded `QImage` for every historical version. An interrupted archive-retention pass can leave far more entries than the usual 12-version quota, and if the Android dialog's event loop stops draining, the queued images could accumulate memory before cancellation propagates.
- Version History now permits at most four queued preview-image updates at a time. The background worker waits briefly while the UI is processing them, checks the dialog cancellation and Qt application lifetime, and exits when the dialog closes rather than waiting indefinitely. The queued-count decrement occurs on delivery or invoke failure. Existing archive listing, checksums, and version-selection behavior are unchanged.
- **Verification:** the last independently reported local Android WSL build succeeded for `184accd2f3dd6134ce9d872f60510fe2551a5015` (project-index coalescing). This preview-queue cap requires its own exact-SHA compilation and runtime verification. No APK upload requested in this task.

## November 14 — coalesce large project-index writes during gallery refreshes

- `MobileDocumentHome::persistIndex()` previously queued a separate captured `QVector<MobileProjectIndexEntry>` of up to 20,000 records for every gallery refresh/hide/scan finish. Since the single-thread scanner also performs storage traversal, repeated refreshes while scanning could retain many redundant snapshots in queued work items.
- A mutex-protected shared pending state now holds only the newest not-yet-written snapshot. At most one background index writer is enqueued; if a refresh arrives during its file I/O, that same worker saves the next snapshot before exiting. The pre-existing disk merge preserves indexed documents omitted by a partial scan, and no UI thread waits for disk writes.
- **Verification:** the preceding local WSL Android build passed exact commit `3f1b688cb6837f045c4bce2a18d2592e04977a65`. The index coalescing change is not yet compiled/tested at its own SHA. No APK upload or phone testing was initiated.

## November 14 — prevent invalid thumbnails evicting valid previews

- Audited the bounded 512-entry preview-cache heap. Zero-byte and over-32-MiB PNG files counted among the newest 512 candidates despite being unopenable by `MobileProjectPreviewCache::load()`. An interrupted cache with hundreds of recent invalid entries could therefore evict an older valid thumbnail. `pruneLocked()` now discards entries outside the existing cache file-size limits before selecting newest candidates. Its second streaming pass removes invalid PNGs; 512 entries and 128 MiB remain the final quotas.
- Added a Qt regression with 520 newer zero-byte PNGs and an older valid stored preview. A startup prune must remove all invalid files and retain/decode the older usable preview.
- **Verification:** last reported local WSL build was SUCCESS for `c08a296ebbfd918df64db50c9c2086e9ccaa8c45`. Latest preview-cache optimization and tests await exact-SHA local WSL compilation/regression result. No APK uploaded as part of this development task.

## November 14 — bound thumbnail-cache retention memory independently of folder size

- `MobileProjectPreviewCache::pruneLocked()` previously called `QDir::entryInfoList("*.png", QDir::Time)`, allocating and sorting metadata for every file in the cache during gallery startup and periodic publications. The cache retains no more than 512 entries and 128 MiB, so very large interrupted cache directories need not consume unbounded metadata RAM.
- Replaced the full sorted directory snapshot with a streaming `QDirIterator` and a 512-element oldest-first heap that selects the newest entries by file modification time (filename tie-break). Applies the existing 128 MiB byte quota to the selected candidates, then uses a second streaming pass to remove unretained PNGs. The publication mutex still serializes concurrent pruning and writes. Other files, including KRA drawings, remain untouched.
- Added a Qt cache regression with 620 PNG files and strictly increasing modification times; pruning must leave exactly the newest 512, not depend on directory enumeration order. Existing sparse 28 MiB cache and startup-quota tests remain.
- **Verification:** preceding WSL Android result passed for `48f07dfa04f69e16c261434a684c08c0cc502902`; new code and tests await exact-SHA build/regression verification. No APK uploaded.

## November 14 — remove whole-folder history move that bypassed checksum validation

- Found a high-impact integrity bypass in `MobileSavedVersionArchive::migrateFolder()`: when the Save As destination version folder did not yet exist, it attempted `QDir().rename(from,to)` and returned immediately. That fast path skipped every size, symlink, SHA format and actual KRA-byte verification added to the normal migration loop. Removed the whole-directory shortcut; all archive pairs now migrate through the same validated, collision-safe path, even for brand-new saved projects.
- Added a Qt regression with one valid and one checksum-mismatched KRA under a source history folder, and **no pre-existing destination folder**. The valid historical version must migrate; the damaged KRA and its checksum must remain in the source folder. This protects a normal first Save As, not only merges into an existing project timeline.
- **Verification:** last reported local WSL Android build SUCCESS was for `46e0a218a12d230cb4f906033488b4657eecb843`. New changes and test require their own exact-SHA build/regression result; no APK uploaded.

## November 14 — verify saved-version bytes against sidecars before Save As migration

- The Save As/legacy-history migration already rejects oversized, linked, and malformed SHA-256 sidecars. A remaining integrity hole was a syntactically valid 64-hex-digit checksum that did not match the KRA contents: the source pair would be moved to the new project despite being unrecoverable under normal checksum validation. The migration now calls the existing bounded `hasPublishedCheckpoint()` verifier before moving any source KRA that has a checksum. Rejected pairs stay in the original history folder for repair/retry; legacy archives with no sidecar still follow existing behavior.
- Added a Qt regression that creates an otherwise valid KRA with a well-formed but incorrect SHA-256 checksum. Save As must leave the source pair untouched and must not report that damaged version in the destination.
- **Tradeoff:** checked archives are streamed and hashed during history migration, adding disk I/O to Save As. Migration already runs off the touch UI through the background archive queue. This prioritizes the integrity of historical artwork over migration speed.
- **Verification:** last exact-SHA local WSL Android build reported SUCCESS for `330e66667b2ec135c891628a59c08295ab98271c`; this code and test await their own compile/Qt-regression report. No APK uploaded.

## November 14 — validate checksum contents before migrating saved history

- `MobileSavedVersionArchive::migrateFolder()` now validates that any existing checksum sidecar contains a properly formatted SHA-256 hexadecimal digest (64 characters, optional surrounding whitespace, at most 128 bytes), not merely that it is a small regular file. A malformed sidecar causes the paired KRA to stay in the source history folder rather than being published as an unusable destination version. Archive files with no sidecar retain legacy migration behavior.
- Added a Qt archive regression with a short malformed sidecar in the source migration folder. The failed migration must preserve the original KRA and digest unchanged and leave destination history empty. Prior oversized and symlink checks remain covered.
- **Verification:** previous exact-SHA local WSL Android build passed for `1168f39446f2ace6122f34e01cad7a6543f561d4`. The malformed-digest change and regression await their own exact-SHA compile/Qt test result. No APK uploaded.

## November 14 — preserve KRA/checksum atomic ownership on failed history migration

- Audited `MobileSavedVersionArchive::migrateFolder()` after the streamed Save As migration change. Checksum sidecars were copied without checking for replaced symlinks or excessive size, allowing a corrupt/unexpectedly large sidecar to be propagated. The paired-archive pass now rejects a linked/non-file sidecar or one larger than 128 bytes, keeping the source KRA and checksum untouched for manual recovery/repair.
- Found a more serious fallback path: when the archive migration failed or was rejected, the "miscellaneous metadata" pass could still move that `*.kra` file alone and leave its `*.kra.sha256` behind. The fallback pass now skips both KRAs and their sidecars so only the paired-archive pass can move them. This preserves failed migrations for retry instead of splitting a recoverable pair.
- Extended the Qt archive tests with oversized and linked sidecar fixtures; a rejected migration must leave both original files intact and must not publish that archive to the destination.
- **Verification:** source and regression tests pushed; awaiting exact-SHA WSL compile/Qt test verification. No APK uploaded.

## November 14 — stream Save As history migration without duplicate directory snapshots

- Inspected the background archive request queues: repeated saves are already coalesced by canonical document identity, so no new queueing layer was added. Identified a narrower memory problem in `MobileSavedVersionArchive::migrateFolder()`: Save As and legacy-history migration previously used two complete `QFileInfoList` snapshots (KRA/checksum pairs and miscellaneous leftovers). Replaced both with `QDirIterator` passes that retain only each current file's metadata and keep the existing collision-safe rename/checksum logic.
- Added a Qt archive regression with seven checked KRA versions plus unrelated metadata. It forces the existing-destination merge path and checks that all versions are recoverable, checksum sidecars remain paired, metadata is preserved, and the emptied source folder disappears.
- **Verification:** last exact-SHA WSL Android compile result must be matched in Issue #1; the new code and Qt-only regression await separate exact-revision verification. No APK uploaded.

## November 14 — stream version archive directory metadata instead of copying a sorted snapshot

- `MobileSavedVersionArchive::scan()` now uses a `QDirIterator` rather than Qt's full sorted `QFileInfoList` plus a second `QVector<Version>`. Sorting the retained small metadata records preserves descending generated filename order, including the existing rate-limit, deduplication, recovery and retention semantics. Compare full paths directly since the entries all share one directory; no new `QFileInfo` allocations during sorting.
- Added a Qt version-archive regression with 80 synthetic historical KRA files, checking that every entry remains discoverable and in descending name order after streaming.
- **Verification:** prior WSL Android build is confirmed for `9246dca9bed1ce291fce35b099f5eb15a7f5de9e`. New changes await exact-SHA build and Qt regression execution. No APK uploaded.

## November 14 — make draft discard notices safe during Android window teardown

- The gallery's `discardDraft()` branch still used stack-backed static Qt information/warning dialogs for both an open drawing and a failed private-draft deletion. These nested modal event loops can outlive their Android parent window. Both messages now use heap-owned `QMessageBox` instances with `QPointer` post-exec cleanup, matching the existing guarded confirmation dialog.
- Extended the lightweight Qt host regression to destroy the gallery parent while each of the information/warning dialog severities is executing. Tests the Qt ownership pattern; not a real Android file delete.
- **Verification:** the prior Fill preview worker completion code and this follow-up await an exact-SHA WSL Android result. No APK uploaded.

## November 14 — protect Fill tolerance worker completion during app teardown

- The non-destructive Fill/Select tolerance preview launches background flood-fill work on the global Qt pool. Its stale-result and completed-result paths previously called `QMetaObject::invokeMethod(qApp,...)` from that worker. Android shutdown could destroy the application receiver before completion. The preview now captures a guarded `QPointer<QCoreApplication>` before submission and checks its lifetime before dispatching all three categories of results; callbacks continue to guard the preview itself.
- Added a Qt host regression demonstrating that a queued UI completion targeting an already-destroyed receiver is discarded without executing the callback. It tests Qt's queued-receiver lifetime behavior, not the actual Krita flood-fill algorithm.
- **Verification:** the latest separately reported local WSL Android build passed for `f5f405b50fafec6a29d3be77a1564ff750a3e22f`. New Fill worker code and Qt test are awaiting exact-SHA compilation and test execution. No APK uploaded.

## November 14 — preserve gallery object lifetime across native Open and recovery warnings

- The All Projects/Recent gallery Open action now captures a `QPointer<MobileDocumentHome>` before entering `m_files->openFile()`. A native Open can synchronously dispatch window teardown; the callback no longer calls `hide()` or constructs a warning against the destroyed gallery. On a live gallery, the existing success/hide and failed-open messaging remain intact.
- Converted three remaining gallery Open/Rename/Version History failure warnings to guarded, heap-owned `QMessageBox` objects. Parent destruction during a nested modal event loop can safely invalidate the warning without Qt deleting a stack-allocated QObject.
- Extended Krita-independent lightweight Qt host coverage to simulate a successful Open destroying the gallery and to destroy a gallery while its recovery warning is running.
- **Verification:** most recent independently reported WSL Android build passed for `a17f5d01df6d439831c3cc08d20feb10f6deb698`. New changes and regressions await exact-SHA build and test execution. No APK uploaded.

## November 14 — safely handle Android file-provider teardown during drawing deletion

- Reviewed gallery background index and recovery-copy paths. Identified a concrete native lifecycle gap in `MobileDocumentHome::deleteProject()`: Android's `tabletDeleteProject` provider operation may dispatch activity/window teardown. The gallery now checks its original `QPointer<MobileDocumentHome>` after that call and stops before touching the recent-files registry, star list, index queue, thumbnail widgets or status text if the gallery was destroyed.
- Converted the three delete-related information/error notices from static `QMessageBox::information/warning` to guarded heap-owned `QMessageBox` instances parented to the gallery. If Android destroys the window during their nested modal loops, Qt does not delete a dialog still allocated on the C++ stack.
- Added Qt-only lightweight-host regressions for synchronous gallery destruction during a successful deletion callback and destruction of a parent while its warning dialog is open. These check lifetime patterns, not a real SAF provider.
- **Verification:** local WSL Android build last reported SUCCESS for `a17f5d01df6d439831c3cc08d20feb10f6deb698`. These new gallery deletion changes and tests await their own exact-SHA compile/test result. No APK uploaded.

## November 14 — stream completed recovery drafts without duplicate directory arrays

- `MobileDraftRecovery::list()` now enumerates finished `*.kra` files with `QDirIterator` rather than constructing a full `QDir::entryInfoList()` snapshot and copying the filtered entries into another list. Only canonical, valid published draft files are retained, reducing peak directory-listing memory for large app-private recovery folders. Completed drafts remain fully discoverable rather than being capped or silently omitted.
- Sorted the retained completed drafts newest-first (with a deterministic filename tie-break), preserving `checkpointsFor()`'s early-stop assumption when comparing autosaves to project modification times.
- Extended `qt_saved_version_archive` with three completed draft fixtures at distinct modification times, asserting each remains listed in the expected order. The previous 260 pending-file regression continues to cover the independent orphan cursor.
- **Verification:** preceding exact-SHA local WSL Android build passed for `68af945462433e9ee8245b05afeeb7d74268123d`; newer streaming changes and the new regression await their own build/test result. No APK uploaded.

## November 14 — stream interrupted draft candidates instead of materializing a full file list

- Followed up the per-visit orphan-recovery limit by replacing the unbounded `QDir::entryInfoList("*.pending.kra")` allocation with a mutex-protected, persistent `QDirIterator` cursor. Each `MobileDraftRecovery::list()` invocation now retains at most 128 pending `QFileInfo` candidates, and resumes at the next filesystem entry on subsequent gallery visits. At the end of a traversal, the cursor resets for the **next** visit, not in the middle of the same batch.
- Retained the limit of two potentially expensive ZIP checks per visit, the five-minute staging age requirement, and unchanged-file validation before rescue. Completed draft listing is unchanged, so finished artwork is not hidden by an arbitrary cap.
- The preceding 260-pending-file/one-old-orphan regression already exercises multi-visit progress and is applicable to the streaming cursor; exact-SHA Qt regression execution remains to be verified. This reduces the pending-file candidate memory and enumeration work, not the cost of listing all completed drafts.
- **Verification:** last exact-SHA WSL Android build SUCCESS was `68af945462433e9ee8245b05afeeb7d74268123d`. New iterator code awaits its own build/test result; no APK uploaded.

## November 14 — bound gallery scans of pending draft staging files

- `MobileDraftRecovery::list()` previously bounded expensive ZIP validation to two pending files but would still iterate every pending candidate when most were too recent to rescue. The recovery browser now inspects a maximum of 128 staging candidates per invocation, rotating its starting window across refreshes so older recoverable files are not permanently starved.
- Existing safety remains unchanged: only files aged at least five minutes, matching a canonical private draft path, and passing ZIP/readability and unchanged-file checks are considered; normal completed drafts are still listed.
- Extended the Qt archive regression with 260 recent staging files and an older recoverable orphan. Repeated refreshes must reach and rescue the older file; test fixtures are removed afterward.
- This change bounds **per-visit candidate inspection**, not the initial Qt directory enumeration or completed-draft listing. The new source/test commits await exact-SHA local build and Qt regression results. No APK uploaded.

## November 14 — validate document lifetime through final native canvas presentation

- The native view factory now validates the original document identity, in addition to the view/manager/stack, after installing the Android window/surface resume observer, after `view->show()`, and after setting the active native canvas. Qt Show and WindowActivate callbacks can synchronously close a document without destroying its QWidget, so final attachment must not return a superficially live but documentless view.
- Extended the independent Qt native-view-stack regression to simulate a destroyed document while its former view widget remains live. The original document identity must be invalid, and the stack must not report the detached painting view as open.
- **Verification:** the local WSL Android build last reported SUCCESS for `1f8f2a83126e94dee32570bd007a50ab3b762171`. New changes await exact-SHA build/test verification; no APK uploaded.

## November 14 — stop native factory initialization after synchronous manager teardown

- `MobileKritaNativeViewFactory::attach()` now revalidates its original guarded view, document, manager, and stack immediately after `adoptView()`, and again after initial document-state refresh. Native `addWidget()` emits `currentChanged` synchronously; listeners can remove or destroy the Krita manager even when the registration itself returns success. Subsequent signal wiring/activation must not dereference that invalid manager.
- Added a Qt-only native-view-stack regression that destroys the manager on first `currentChanged` while leaving the stack and view live, demonstrating why successful registration does not imply a surviving manager.
- **Verification:** new code and regression pushed to `main`, but awaiting exact-SHA local WSL build/test result. No APK uploaded.

## November 14 — validate native view dependencies immediately after Krita createView

- Native first-canvas and additional-canvas attachment now establishes guarded identities for the document, manager and native view stack **before** invoking `KisPart::createView()`. That factory may dispatch synchronous Qt callbacks that destroy its dependencies; the returned view is no longer configured or given to the input tracker until all three original objects are confirmed alive. An incomplete input manager or canvas also aborts attachment, releasing a still-live unparented view only while its manager survives.
- Added Qt-only native-view-stack regression simulating manager/root destruction during a view factory call. It checks the same pre-initialization lifetime gate, not full Krita GL construction.
- **Verification:** latest separately reported local Android build passed for the previous thumbnail-cache change `9e73b1538a68c3aa4be0d7aef52c5a3ac8fdf3e6`; these native view source changes await exact-SHA WSL compilation and runtime testing. No APK uploaded.

## November 14 — reclaim over-quota thumbnail cache after interrupted shutdown

- The existing thumbnail PNG cache enforces a 512-entry/128 MiB quota during thumbnail publication. However, if Android terminates the process with an already-overquota cache, a later session that merely browses cached drawings might never trigger new writes and therefore never prune the excess. Factored the existing byte/count retention sweep into a lock-protected `MobileProjectPreviewCache::prune()` entrypoint.
- Gallery startup now invokes that sweep once from its existing single-thread background scanner (after recovery-copy cleanup), avoiding any UI-thread directory walk and preserving cache correctness during concurrent thumbnail writes.
- Extended the Qt preview-cache regression to stage six 28 MiB legacy PNGs, prune without generating any new thumbnails, and verify the 128 MiB limit while leaving an unrelated KRA untouched.
- **Verification:** preceding KRA recovery revision `a6d1b2579725b7617850178621582bfea3a9bcbd` passed the local WSL Android compile; the new startup cleanup and regression await an exact-SHA build/test result. No APK uploaded.

## November 14 — validate native host ownership after Save completion and canvas attachment

- Standalone Save/Save As/Export now revalidates the originally captured QPointer document stack **after** Krita's asynchronous disk write and `waitForSavingToComplete()`. A save completion signal alone is insufficient to report success or enqueue a new Version History snapshot if Android replaced the original native root during Qt event processing. A completed write itself is not reversed; only stale post-save publication is suppressed.
- After successful native Open/attach, confirm the opened document still belongs to the original active stack before adding it to Recent or remembering Android SAF permission. This prevents stale project bookkeeping after reentrant host replacement.
- Added a Krita-independent Qt view-stack regression simulating a successful I/O completion delivered after host replacement. This covers the late-completion identity gate, not actual Android SAF saving.
- **Verification:** most recent WSL build result must be matched to the exact commit SHA in Issue #1; the new code and regression await their own compile/test result. No APK published.

## November 14 — abort checkpoint recovery retry after failed Open destroys native host

- The KRA recovery Open path now rechecks the initiating native view-stack identity and Krita part registry before each checkpoint attempt and after each `openPath()` call. A failed or successful import can dispatch lifecycle callbacks; if those callbacks destroy/replace the original stack, the operation stops instead of attempting the next checkpoint or original file on a stale host. A still-live, not-yet-registered document is released. Ordinary unreadable checkpoints continue falling back normally while the original host remains valid.
- Extended the independent Qt native-view-stack regression with a failed-first-checkpoint simulation that destroys the original root, ensuring no later checkpoint attempt proceeds. This is a lifecycle-pattern test, not an Android/KRA recovery test.
- **Verification:** source committed to main; the latest reported Android WSL build result is recorded separately under Issue #1. These new changes require exact-SHA compilation and runtime testing. No APK uploaded.

## November 14 — prevent stale autosave traversal across native host replacement

- Native periodic, background, and shutdown autosave passes now capture the original QPointer document-stack identity before iterating open canvases. If a synchronous Krita `slotAutoSave` callback destroys or replaces the stack while Qt processes nested events, the pass stops instead of continuing through a stale list of other drawings. The pending background-save continuation loop applies the same rule.
- Added a Qt-only native view-stack regression with two open canvases: replacing the stack during the first checkpoint must prevent processing the second stale document. This exercises the guard pattern, not real Krita autosave/Android lifecycle callbacks.
- **Verification:** preceding interrupted-Open code revision `7e6edfce56d764d85cfc052b727ecf500d679010` passed its exact-SHA local WSL Android compilation. New autosave code and Qt regression await their own build/test result. No APK uploaded.

## November 14 — reject interrupted Open into a replacement native document stack

- Native project Open now captures the original QPointer view-stack identity. A nested Krita `openPath()` call may process close/activity callbacks, destroying or replacing the root even if the runtime service survives. Before mutating a recovered document or registering an imported canvas, the Open path rejects a missing/replaced root and releases the not-yet-registered import. Already-open project activation also uses the guarded original root.
- A Qt-only native stack regression stages a pending imported QObject, replaces the root during a modal event loop, and checks that the stale document is discarded rather than adopted into the replacement host. This is lifetime-pattern coverage; actual Android/KRA import remains untested.
- **Verification:** exact-SHA WSL Android compile passed for earlier Save guard `33f9f49273567740141802872c1a9ee4526d99e9`. The new Open code and Qt regression await their own exact-SHA build/test results. No APK uploaded.

## November 14 — reject native Save after document-stack replacement

- The standalone Krita Save/Save As/Export transaction now captures the native view-stack QObject identity with QPointer. After the render-wait modal and again after the Android Save As file picker, it checks that the original stack remains alive and is still the runtime's active root before querying current document or proceeding with disk writes. This prevents null dereferences and stale-canvas writes if the activity destroys/replaces the stack during nested Qt events.
- Added a Qt-only native-view-stack regression simulating root destruction and replacement while a modal picker is running. This validates the lifetime guard pattern, not a real Android SAF save.
- **Verification:** latest reported local WSL Android build passed for `2ae496f1bf1398feb9ea33945789047031266ba0`; the new Save guard and regression await exact-SHA WSL compilation and test execution. No APK uploaded.

## November 14 — safely destroy unsaved-document close prompt on Android activity teardown

- The native Save / Hold to discard / Cancel prompt no longer constructs a parented stack QDialog, QVBoxLayout, QLabel, buttons or progress bar. The modal and all its child controls now have heap ownership guarded by QPointer, and the hold timer is a dialog child so it is canceled along with the dialog. If the host vanishes while the modal loop is active, the result is forced to Cancel rather than Save or Discard. The existing two-second hold requirement and translated labels remain unchanged.
- Added a Qt-only lightweight host regression covering parent destruction during an equivalent close-modal event loop. This is an ownership-pattern test, not an instrumented Android close-flow test.
- **Verification:** source pushed to main; latest local WSL compiler SUCCESS report before these changes was for `c963b27e0ad080f5c8d8a85c7d123243ea8397f0`. The new close-modal changes and Qt regression await exact-SHA WSL build/test confirmation. No APK uploaded.

## November 14 — harden Android storage-permission dialog lifecycle

- The All Projects storage-access prompt now uses a QPointer-guarded heap-owned QMessageBox. The Android activity can destroy its parent window during the nested confirmation event loop; the permission request is only dispatched if the owning gallery survives and remains visible. The existing default Yes choice and optional access behavior are preserved.
- Extended the Krita-independent Qt host regression with destruction during QMessageBox and QInputDialog nested event loops to verify the guarded child/parent ownership pattern used by gallery deletion and rename.
- **Verification:** exact-SHA local WSL Android compile reported SUCCESS for preceding gallery dialog revision `9b5fbc19a79dcc463090568d4498de592b55b573`; newer permission prompt and regression source await exact-SHA build/test validation. No APK uploaded.

## November 14 — protect destructive gallery prompts during Android window teardown

- Gallery Delete Drawing and Discard Draft confirmation prompts now use heap-owned, QPointer-guarded QMessageBox instances instead of parented stack helpers. The owning gallery is revalidated after the nested dialog returns before touching files, preventing callbacks from continuing against a destroyed host.
- Rename Draft likewise uses a guarded heap-owned QInputDialog and verifies that the gallery remains visible and the original project-list generation is current. This avoids stale rename actions following an asynchronous project-list rebuild or window teardown.
- **Verification:** source committed to `main`; the latest reported PC result remains a successful exact-SHA Android compile for `bf86717a787b9d5a5da2926ce1bf04844299de71`. These newest fixes await an exact-SHA WSL build result. No APK uploaded or phone testing requested.

## November 14 — bound thumbnail attempt revisions and tighten recovery-copy filename validation

- Gallery preview-attempt bookkeeping now stores only the latest attempted revision for each project path instead of retaining every historic (path, timestamp) pair. On repeated edits or failed preview generation, the in-memory table is bounded by distinct projects rather than the number of revisions encountered in a session. Revision comparisons preserve retry behavior on modification, removal, or icon eviction; missing revisions are not confused with a sentinel timestamp.
- Temporary Version History recovery-copy recognition now requires the canonical `Recovered-{UUID}.kra` filename length, matching stale-file reclamation. Expanded cleanup regression to preserve an aged, noncanonical filename even when it contains a valid UUID prefix.
- **Verification:** latest separately reported local WSL Android build succeeded for `4aa7bac9f2d218e65452c5cd2979491894212b5c`. New source revisions await exact-SHA WSL build verification; no Android runtime profiling or APK upload.

## November 14 — reclaim stale recovery copies after hard Android process termination

- Version History already cleans its temporary recovery copy when queued Qt delivery succeeds or is canceled; an abrupt OS process kill still bypasses all in-process destructors. A new `cleanupStaleRecoveryCopies()` streams only app-generated `Recovered-{UUID}.kra` entries in the private `recovered-versions` directory, removes files older than 24 hours, skips links/invalid names/recent copies, and shares the archive I/O mutex. It never targets saved version archives, active draft checkpoints or other files.
- The gallery submits cleanup to its existing scanner thread pool at startup, keeping directory traversal and deletion off the touch UI thread. Added a Qt archive regression that stages stale, fresh and unrelated files and checks selective, idempotent cleanup.
- **Verification:** prior Version History queued-copy cleanup revision `8b71ce4dc197b5ba6cb1e407c99639024ef9e826` passed exact-SHA local WSL Android compilation. The startup cleanup and new archive regression await exact-SHA build/test results. No APK uploaded.


## November 14 — clean up canceled Version History recovery copies

- The gallery's `openCopy()` materializes an archive to a separate private KRA before posting its native-open callback to the GUI thread. Previously the temporary file was removed only inside that callback, so a receiver/app shutdown before delivery could leave full-sized KRA copies on disk indefinitely. A shared cleanup owner captured by the Qt event now removes the copy whether the callback runs, the app dies before it runs, or posting is impossible. A guarded Qt application identity also avoids dispatching to a destroyed `qApp`.
- Added a Qt host regression that queues a cleanup-owning callback, destroys the receiver before dispatch, and checks the temporary KRA was deleted. This validates the lifetime pattern but not a real Krita archive restore.
- **Verification:** previous shared touch action picker revision `acf5292c7c1b0e0dd1518caaf7e406c4909b863c` passed exact-SHA local WSL Android compilation. Latest recovery-copy cleanup and regression await exact-SHA WSL compilation and Qt test execution. No APK uploaded.


## November 14 — shared touch action picker survives host teardown

- Replaced the stack-allocated, parented `QDialog` in `mobilePickAction()` with a `QPointer`-guarded heap child and scoped cleanup. Android may destroy the parent while the picker processes nested modal events; Qt would previously try to delete the picker from the parent's child list even though it was still allocated on the C++ caller's stack.
- Action selection now checks that both the dialog and owning widget survived `exec()` before returning an action or opening nested submenus. This applies across the mobile touch UI wherever `mobilePickAction()` is used, not just Version History.
- Added a Qt lightweight-host regression that destroys the picker's parent window during its modal loop and confirms a canceled selection with no surviving parent. The preceding gallery memory revision `f4f1e1c3f4383e333aa6ce9be1cf6119a824e63a` passed exact-SHA WSL Android compilation; the action picker fix and new regression await their own verification. No APK uploaded.


## November 14 — bound gallery widget icon memory and viewport scan costs

- Confirmed the gallery's separate `m_cache` icon cache was already capped at 96 entries. However, individual `QListWidgetItem` rows also retained decoded `QIcon` references after cache eviction; browsing many drawings could therefore accumulate thousands of resident icons. The gallery now keeps at most 128 decoded row icons, replacing the oldest visited rows with a placeholder and allowing their disk-cached thumbnails to reload when revisited. The residency ledger is cleared on gallery reconstruction and updated on project deletion/preview replacement.
- Visible thumbnail discovery no longer walks all 20,000 gallery rows on every scroll or preview completion. It starts near the first on-screen list item, includes an overscan margin and stops when subsequent items have passed the viewport. This reduces UI-thread work during large-library scrolling.
- **Verification:** previous native tab teardown revision `790b12f6532b03b224eefcd544113e11e725109b` passed exact-SHA local WSL Android compilation. Gallery memory and viewport changes await exact-SHA WSL compilation and device-level large-list profiling. No APK uploaded.


## November 14 — native tab removal teardown and Save callback regression

- When `MobileNativeViewStack::forgetToken()` removes the active view, Qt emits `currentChanged` synchronously. A listener can destroy the entire stack and its child views. Cleanup now tracks its host through `QPointer` across `removeWidget()` and avoids touching a freed host/view afterward.
- Added a desktop Qt regression that destroys a native view stack from its view-removal selection-change signal. Strengthened the asynchronous Save regression to assert that a queued callback owns its state beyond the initiating stack frame, and that destroying its QObject context cancels pending delivery.
- **Verification:** Save regression changes through `f4229a77aa1ffc273e347d01e3016e9cdc6d3901` passed the local WSL Android compilation. Native tab teardown fix and new Qt regression await exact-SHA build/test verification. No APK uploaded.


## November 14 — native Save completion callback lifetime

- Native `writeDocument()` no longer connects `sigSavingFinished` with a lambda capturing stack-owned `completed` and `target` by reference. Qt can dispatch queued signals that were already posted even after a connection is disconnected. The callback now owns an atomic completion flag and a copied target path, so late delivery cannot access expired Save stack memory.
- Extended the independent Qt document rollback test with a queued completion-style signal that is disconnected before the event queue is processed, matching the critical lifetime pattern. This is structural regression coverage rather than a real Krita asynchronous save test.
- **Verification:** preceding autosave coalescing revision `d935d8c5ca29091d2a536323e150b5edf6395e77` passed exact-SHA local WSL Android compilation. The Save callback and Qt regression revisions await their own WSL build/test results. No APK uploaded.


## November 14 — preserve in-flight autosave requests and coalesce completion delivery

- Native autosave no longer drops checkpoint triggers that arrive while `checkpointDrafts()` is already executing in a nested Krita event loop. It records one coalesced follow-up request and queues a new checkpoint pass only after the running pass unwinds, preserving changes made or background events received during that interval.
- A single `scheduleDraftContinuation()` now coalesces background-save completion signals and pending-pass cleanup into one queued Qt callback. This prevents many canvases completing background saves simultaneously from generating duplicate event-queue work; if delivery happens inside a nested checkpoint, exit handling reschedules remaining work.
- **Verification:** the first single-flight checkpoint commit `c4de377edc142ee5ab6724aa04c8c3310cb52249` passed exact-SHA WSL Android compilation. Follow-up coalescing revisions await exact-SHA verification. No native runtime test or APK upload.


## November 14 — serialize independent runtime autosave passes

- The native standalone runtime now treats each `checkpointDrafts()` invocation as a single-flight operation. The periodic 15-second timer, foreground-to-background transition and about-to-quit handler can all fire while Krita's synchronous `slotAutoSave` processes nested Qt events; without a guard they could recursively launch overlapping canvas checkpoints and consume extra memory/IO.
- A Qt scope guard clears the in-flight flag only if the runtime survives callbacks that may close its final view or window. If a background save completes during checkpoint processing, its pending-draft continuation is deferred until the current pass unwinds, rather than running recursively; pending draft identities remain preserved.
- **Verification:** previous native render-wait dialog revision `39fe1dd87f4d9e4393cfb38879575ff448331536` passed exact-SHA WSL Android compilation. Latest autosave scheduling changes await their own exact-SHA compile result; no device runtime testing or APK upload.


## November 14 — safe native Save render-wait modal ownership

- The native save pipeline's `KisDelayedSaveDialog` (which can block and process Qt events while the image finishes rendering) is now heap-owned and tracked by `QPointer`. Previously it was a stack-allocated dialog parented to the Android window, so window destruction during the nested render-wait event loop could delete an object still residing on the native Save call stack.
- Completion status is captured only while the guarded dialog survives, then the dialog is explicitly released before further document checks. The existing document and runtime ownership checks remain in effect. The prior Qt parent-destruction modal test covers this Qt ownership pattern, but no device-specific render-wait test has been performed.
- **Verification:** the preceding shared-document-backend lifetime revision `bf8fb6638b132dacf949798fce7e7e7731b3727e` passed exact-SHA local WSL Android compilation. This dialog fix awaits its own exact-SHA compilation status. No APK uploaded.


## November 14 — retain native document backend during reentrant operations

- The independently hosted `MobileKritaStandaloneDocuments` backend now uses `QEnableSharedFromThis` to retain its own shared ownership during Open, New-canvas attachment, Save, and Export. These operations run Krita callbacks and native modal dialogs which can synchronously close the last window and release the host's service ports. Keeping the document backend alive until the operation unwinds prevents its `QScopedValueRollback` members from referencing freed memory.
- Extended the independent Qt document-rollback regression with a synchronous external-owner release during an in-flight shared-backend operation, verifying that destruction occurs only after the call has finished.
- **Verification:** preceding merged-index revision `67420c9b3ce2bf0fee8f732fc951f2fe4f255b92` passed local WSL Android compilation. New backend-lifetime code and Qt test await exact-SHA build/test verification. No APK uploaded.


## November 14 — bound merged project lists and index retention

- All Projects now caps the actual Qt list/widget inventory at 20,000 distinct project identities, including when a previously saturated persisted index is combined with a new scan. Existing identities still refresh their paths, timestamps and previews; extra discoveries do not allocate additional rows. The gallery reports that its safety limit was reached.
- The persistent index merge also trims back to 20,000 entries before writing; previously the merge could exceed the JSON writer's hard limit, silently suppressing the entire updated index. Newly opened/refreshed entries are retained before stale historical entries, even when an SAF document has no modification timestamp.
- **Verification:** preceding full-scan safety limits revision `2956856a8b5e9d60364eee987b851cd459bae01e` passed exact-SHA local WSL Android compilation. Latest merged-index changes await exact-SHA WSL compilation and realistic high-volume stress testing. No APK uploaded.


## November 14 — cap Android All Projects scan memory

- The gallery's full storage refresh now limits discovery to 20,000 projects (matching the persisted JSON index ceiling) and 50,000 traversed/pending directories. Large shared-storage trees can otherwise grow the scanner's in-memory `roots`, `visited` and `seen` sets without bound, especially during explicit full scans.
- A capped scan stops traversing immediately, still publishes its already-discovered batch and Android granted-project entries when space remains, and marks its result incomplete. The gallery differentiates a safety-limit result from an ordinary 12-second timeout, and directs users to Browse Files for missing artwork rather than promising Refresh will bypass the limit.
- **Verification:** previous Version History dialog revision `11976bcd5964b886f067c421f4cc390e9d731f50` passed exact-SHA WSL Android compilation. This scan budget revision awaits local WSL compilation; no Android storage stress test yet, no APK uploaded.


## November 14 — parent-safe Version History modal lifecycle

- Replaced the stack-allocated, window-parented Version History modal dialog with a guarded heap-owned QDialog. Previously Android could destroy the host window during the nested `exec()` event loop, and Qt's parent cleanup would attempt to `delete` a child dialog still residing on the caller's C++ stack. The dialog and home identities are now checked after the modal loop; successful selection is copied before deleting the dialog and opening the selected version.
- Added a Qt lightweight-host regression that destroys a window while its history-style modal event loop runs and verifies both guarded dialog and parent identities clear without double deletion.
- If the gallery is destroyed while its outer Qt host remains alive, the completed modal dialog is deleted explicitly rather than left as an orphan until host shutdown.
- **Verification:** preceding shutdown-aware gallery worker revision `cb2e9a95631c6d1f3c4b3ef586fbf37e75ca95f5` passed exact-SHA WSL Android compilation. The new dialog lifetime code and Qt regression await an exact-SHA build/test result. No APK uploaded.


## November 14 — Qt shutdown-aware gallery background work

- The All Projects storage scanner, gallery thumbnail workers, and Version History loader now retain a guarded Qt application identity before queuing UI callbacks. They stop publishing results if the application has begun object teardown, rather than posting to a stale global `qApp` pointer.
- The scanner's bounded-delivery backpressure loop also exits if the Qt application no longer exists. Otherwise queued callbacks cannot drain after event-loop shutdown and a worker can hold up process teardown indefinitely. Recursive scanning and SAF project enumeration likewise stop when the application is gone.
- Canceled thumbnail work now exits after cached PNG decoding or source extraction before doing expensive KRA/SAF extraction or publishing a stale icon.
- **Verification:** preceding Version History revision `855c270b795fa93ffc74391e05a4bd225c128a70` passed exact-SHA WSL Android compilation. Current shutdown/cancellation changes await exact-SHA WSL compilation and device testing. No APK uploaded.


## November 14 — cancel-safe Version History thumbnail callbacks

- Version History now tracks its asynchronous result label and Open button with QPointer rather than capturing raw child widget pointers. A queued worker result is discarded when the dialog was dismissed or its controls have been destroyed.
- Individual preview delivery now checks cancellation and verifies that its target row still exists before setting the icon. Prevents null dereference if the list was reset/closed while thumbnails were being decoded.
- **Verification:** preceding native attach/manager-lifetime revision `6e1396ca3ff0982f79a49422a876cef922ec82a3` passed local WSL Android compilation. The Version History callback change awaits exact-SHA local WSL compilation; runtime dialog-close tests were not performed. No APK uploaded.


## November 14 — native attachment and manager teardown safety

- Standalone runtime now retains QPointer identities for itself and the imported KisDocument across the real Krita view factory's synchronous loading/selection callbacks. If attachment destroys its owner or document, callback connections are not installed into dead objects and the attach reports failure.
- Native view Close now checks that its KisViewManager survived after switching to the view and again after calling KisView::close(). It does not explicitly destroy a still-live view when its manager has already been destroyed; failed adoption follows the same rule. This avoids KisView destructor callbacks into freed manager memory.
- **Verification:** preceding native import/GL activation revision `6cdcbd168bf8058bb6ed9874ce9d6fda0721fe26` passed exact-SHA local WSL Android compilation. Latest native ownership changes await exact-SHA WSL compilation; no direct Krita-native runtime test performed. No APK uploaded.


## November 14 — native view activation and document import lifetime

- Native view-close routing now rechecks its QPointer view manager after switching the current canvas and before invoking `KisView::close()`. Canvas repaint activation revalidates the selected view, manager and stack after `updateCanvas()` callbacks before using the GL surface, preventing access to a destroyed canvas.
- Native project import (including recovered checkpoint retries) now guards newly created KisDocument objects across `openPath()` and only deletes a candidate that survived its failed open callback. This avoids potential duplicate destruction when Krita's import callbacks already released it.
- The imported document identity is captured before `KisPart::addDocument()`; if registration observers synchronously close it, native attachment is abandoned without using a dangling document pointer.
- **Verification:** prior independent-host bootstrap regression revision `f43501d5269e3a4f8931d4dd421f960e4f756883` passed local WSL Android compilation. Latest native changes await exact-SHA WSL verification; they do not have Krita-native unit coverage. No APK uploaded.


## November 14 — failed-startup compiler repair and teardown lifetime

- Fixed the exact Android WSL compile failure at `762a0b0b9f44f4f139fb3b63e83f6ceb3e416c63` (Issue #90): `MobileNativeViewStack` does not declare Qt's `Q_OBJECT` macro, so the new failed-root rollback must use the project's existing RTTI `dynamic_cast` rather than `qobject_cast`. No engine interfaces were changed.
- The engine-session bootstrap now tracks host and runtime with `QPointer` during failure cleanup. Destroying a partially initialized runtime may recursively tear down its host; only discard an empty root if the host survived.
- Expanded Qt lightweight-host regressions to assert that pre-document cleanup cannot remove a fully configured engine with active services, in addition to the previous tests for empty-root retry and live-canvas preservation.
- **Verification:** corrected code is queued for exact-SHA WSL Android build and desktop Qt test execution; no APK uploaded.


## November 14 — failed independent engine startup resource cleanup

- A failed native engine-session bootstrap now destroys its partially initialized Krita runtime (including view manager and action collection) immediately, then releases its still-empty native document root and shared view adapter. A missing runtime also rolls back the unused root. This avoids retaining native widgets/GL resources and blocking another engine startup attempt for the rest of the window lifetime.
- Rollback deliberately refuses an installed document service, active drawing, child painting widget or mobile chrome. Open artwork cannot be discarded by a startup cleanup path.
- Added desktop Qt lightweight-host regression verifying empty-root removal, clean second native-root creation, and refusal to remove a root with a live drawing.
- **Verification:** preceding native close transaction revision `4918ecb5898bd06c393a6026064b51e611e23733` passed exact-SHA WSL Android compilation. The current bootstrap cleanup and new Qt regression still require exact-SHA build/test results. No APK uploaded.


## November 14 — teardown-safe canvas resume and host close

- Canvas resume dispatch now invokes a local copy of the activation function. If activating the selected native canvas destroys its own observer, Qt no longer executes the observer's already-freed member std::function. Desktop Qt regression explicitly destroys the observer from its own callback and confirms no later activation.
- The lightweight native window close handler retains a local shared view port and tracks the host with QPointer instead of a scoped rollback on a destructible QWidget. When a Save/Discard callback tears down the host, the close path stops rather than accessing freed window members; ordinary rejected closes reset the reentrancy flag explicitly.
- Extended the Qt lightweight host regression to request a nested Close while closing a second canvas, verifying that reentrant dismissal is rejected without losing the outer transaction.
- **Verification:** previous thumbnail retention revision `b623f0c1605853c1720dec2e338a5e3b73642c23` passed local WSL Android compilation. New native lifetime commits and Qt regressions await exact-SHA build/test verification. No APK uploaded.


## November 14 — aggregate gallery preview cache budget

- The gallery's persistent PNG preview cache now enforces an aggregate 128 MiB disk budget in addition to the existing 512-entry count and 32 MiB per-image limit. Oldest previews are evicted first; it never deletes KRA drawings or non-PNG files.
- Large newly published previews trigger immediate quota enforcement rather than waiting for the normal 32-write maintenance interval. Small icons keep batched maintenance to avoid scanning the directory unnecessarily.
- Extended Qt cache regression with six sparse 28 MiB legacy PNG files, triggering retention and verifying the byte budget, recent icon usability, and preservation of unrelated KRA files.
- **Verification:** prior revision `b2af292622038a0477b4a90b803f399b864fdfe3` passed exact-SHA local WSL Android build. This new batch's WSL compile and Qt regression results remain pending; no APK uploaded.


## November 14 — reduce legacy thumbnail decode memory

- The gallery preview-cache loader now requests a 124 × 124 maximum decoded image instead of loading a potentially 4096 × 4096 legacy cache PNG into memory at full size. Aspect ratio is preserved and existing small icons stay unchanged.
- Extended the Qt preview-cache test with a 2048 × 1536 stored image; the gallery loads a 124 × 93 icon while verifying its pixel contents and the unchanged 8 × 8 icon path.
- **Verification:** earlier scan-backpressure revision `26e257a11e3f6aada8751a97183bf39afcbcc4ee` passed local WSL Android compilation. The new thumbnail change and Qt regression await an exact-SHA local build/test report. No APK published.


## November 14 — bound All Projects scan backlog and thumbnail memory

- Background shared-storage scans now permit at most eight outstanding 32-project delivery batches on the UI event queue. If Qt cannot consume them promptly, the scanner briefly waits on its worker thread and continues when the backlog drains; cancellation interrupts this wait immediately. This avoids unbounded queued allocations when indexing huge folder trees while the home/gallery UI is busy.
- Thumbnail decoders now reject embedded KRA preview and external image dimensions exceeding 40 million pixels (in addition to existing per-axis limits) before attempting a potentially large underlying decode; valid previews still scale to the compact gallery size.
- **Verification:** source changes await exact-SHA local WSL Android compilation. Desktop Qt test coverage for the Android-only scanner is not available in this batch. No APK uploaded.


## November 14 — cascading native shutdown and autosave lifetime guards

- Standalone native runtime teardown captures Qt-guarded KisView identities rather than raw pointers before destroying child views. Krita can synchronously close multiple views when one is removed; sibling entries that have already been deleted are skipped instead of accessed after free.
- The periodic/background autosave pass and queued completion pass now stop immediately if a direct `slotAutoSave` callback destroys the runtime or its view root, instead of continuing through stale runtime memory.
- Expanded Qt native-view regression to simulate deletion of one tab triggering synchronous destruction of its sibling while a shutdown list remains active.
- Failed initial native canvas attachment now deletes its still-live, unparented KisView when its manager survived the aborted callback, avoiding an abandoned view and GL-resource leak. If Krita already destroyed that view, the QPointer suppresses a second delete.
- **Verification:** the previous native view lifecycle revision `efc47233ea5be190354ea232fb2dfd3bf2dbaf8e` passed the local WSL Android compilation. Exact-SHA WSL and desktop Qt regression testing for this new batch remain pending. No APK upload.


## November 14 — native canvas lifetime hardening

- Guarded real KisView, KisDocument, view manager and native stack pointers across synchronous Krita view-manager hooks, document loading, first-show/currentChanged callbacks, and native GL/tool activation. A callback that closes a canvas must no longer lead to raw KisView dereferences in the factory.
- Failed native adoption only deletes the candidate when its QPointer remains live, preventing a second delete after a currentChanged handler already destroyed it. Deferred activation now stops after view-manager or tool initialization if the view/manager/root has disappeared.
- Extended desktop Qt native-view regression to exercise view deletion during first adoption and during tab activation. This verifies native stack identities and rejection of stale activations; live Krita-specific callbacks still require local Android compilation/device evidence.
- **Verification:** previous baseline `f3ef2ac11eb08dac8be28fb3f7c05fce4cf5f872` passed WSL Android build. Latest native lifetime changes and Qt regression await exact-SHA compilation/testing. No APK uploaded.


## November 14 — reject directories masquerading as native project files

- Native Open no longer treats an existing directory (including a `file://` URL naming that directory) as a valid drawing merely because `QFile::exists()` succeeds. Save As correctly requests a file destination and Recent Files will not record a directory as a project.
- Added desktop Qt regression covering a folder named `project-folder.kra`, its local URL, and their open/save/recent behavior. Android SAF `content://` support remains unchanged.
- **Verification:** earlier URL-helper compilation failure occurred in an intermediate commit before the helper was added; the watcher later reported success at `4f586425bfd2f53b766ddbd43c9bbe04a4fd7f19`. This batch's exact-SHA WSL compilation and Qt regression are pending; no APK uploaded.


## November 14 — native open path and recent-file URL consistency

- The independent Krita native document host decodes local `file://` URLs into filesystem paths before handing them to the engine's openPath() method, while keeping Android `content://` SAF identities intact.
- Recent Files now records the same canonical local URL whether the incoming project used a filesystem path or a local file URL. Nonlocal `file://remote-host/...` authorities are rejected by native open, version-history identity and Save As collision logic rather than accidentally targeting a different path.
- Save As and Export Copy picker targets are decoded to local paths before passing them to Krita's native writer. Invalid file URLs fall back to Pictures as the initial Save As directory. Extended Qt path policy regression for all these cases.
- An intermediate commit (`59a39808fd`) failed Android compilation because the native Recent helper was referenced before its companion header commit; the helper was added in `39b22e5a92`. A later exact-SHA build is required before the batch can be considered verified.
- **Verification:** earlier preview-cache batch awaits a reported exact SHA; this source/test batch also awaits local Android WSL build and host Qt test execution. No APK published.


## November 14 — preview cache writer/reader size consistency

- Cached project previews now reject an encoded PNG larger than the reader's existing 32 MiB maximum before publishing the QSaveFile. This avoids creating permanent on-disk thumbnails that the gallery can never open.
- Desktop Qt regression generates a high-entropy 3500 × 3500 image that exceeds the encoded byte budget and confirms the cache does not publish a rejected file. The existing revision, cancellation, clock-skew and malformed-image regressions remain.
- **Verification:** previous project-index revision `30fff273ccac432e63a7c8c818a8002592e53986` passed WSL Android compilation. Latest source and regression await exact-SHA build/test verification; no APK upload.


## November 14 — bounded persistent project index loading

- The All Projects index cache now rejects JSON files larger than 16 MiB and arrays over 20,000 records before parsing or allocating the gallery list. Reads are byte-limited and substituted/symlinked index files are ignored, avoiding unbounded startup memory use from a malformed or damaged cache.
- Index saves enforce the same size and entry limits, retaining the last good atomic index when a new payload would exceed them. The index location now requires a valid app-private data directory rather than falling back to the filesystem root. The KRA projects themselves remain untouched.
- **Verification:** exact-SHA Android WSL compilation pending for these source changes; no host Qt regression executed or APK published.


## November 14 — native Save adapter teardown safety

- The standalone native Krita Save adapter no longer uses a scoped rollback that writes to its own `m_saving` flag after its document backend synchronously destroys the adapter during Save. A local shared backend reference keeps the port alive for the callback; the saving flag is reset only if the adapter survives.
- The backend readiness probe also verifies that its adapter survived before starting a Save, preventing a teardown callback from leaving an invalid `this`.
- Expanded desktop Qt regression for successful Save, failed Save, and readiness checks that delete their host synchronously.
- **Verification:** previous document-history revision `4d4bd33194967aa20e2f71f06d74d178f9906c2c` passed exact-SHA local WSL Android compilation. New source/test changes await exact-SHA WSL and Qt test verification. No APK upload.


## November 14 — one version history for symlinked Save As parents

- Local saved-version identity now resolves the closest existing ancestor for a not-yet-created project destination. Save As to `real-folder/future.kra`, a symlinked folder alias, or its local `file://` URL therefore shares one version-history identity before the new file exists.
- Recovery-copy identification also resolves local file URLs, so a URL alias of a private restored KRA still requires Save As rather than risking an overwrite.
- Added Qt regressions for canonical/symlinked future Save As parents and for URL aliases of private recovery copies.
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
