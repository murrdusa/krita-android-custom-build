# Krita Mobile Studio — current development state

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
