# Code Audit Report — VIXEN: Code Quality, Architecture, Safety & Consumer UX

**Date:** 2026-07-06 23:45
**Scope:** Full engine — `libraries/*` (RenderGraph, SVO, Profiler, ResourceManagement, ShaderManagement, CashSystem, VoxelData, …), `application/`, `codegen/`, `tools/`, `shaders/` — plus the **consumer integration seam** (the undertow-side host at `undertow/vixen/{abi,render,host,app}`, read as consumer-pain evidence; findings there are marked *[consumer host]*). Excluded: build dirs, `dependencies/`, `.vulkan-sdk/`, `archive/`, `generated/` (audited as artifacts, not style), tests (except as evidence).
**Mode:** Full (4 parallel specialists: architecture, clarity, maintainability/C++-safety, consumer-UX/DX)
**Files:** 1,158 C++ files, ~212k lines; ~15 hotspot files deep-read chunk-by-chunk; remainder swept by targeted grep with follow-up reads; the FR consumer-feedback log read in full.

## Summary

| Severity | Count |
|----------|-------|
| Critical | 3 |
| Major | 27 |
| Minor | 20 |
| Info | ~16 |

**Overall Assessment:** **NEEDS ATTENTION.** The architectural bones are healthy — acyclic, actively-managed library graph; real registration seams; a superbly documented algorithmic core — but there is genuine safety debt concentrated in three places: **(1)** unvalidated trust boundaries (the UTVW view-slice wire reader and the three disk-cache codecs), **(2)** thread-safety hazards that are latent today but armed (parallel executor + queue submits, async cache load/save, deferred destruction fed from atomic refcounts), and **(3)** half-finished codegen/validation seams whose remaining hand-maintained halves fail silently (SDF-op glue tables, string-typed node probing, the never-checked view `format_version`). The consumer FR log independently converges on the same theme: the recurring pain class is **silent cross-boundary mismatch**, point-fixed three times (FR-4, FR-11, FR-22) but never structurally closed.

### What's verified GOOD (preserve these)

- **The algorithmic core is exemplary.** `SVOTraversal.cpp` is 1,205 cohesive lines split into ~25 named phase functions with paper citations; dense bit-math carries why-comments throughout. Do not split it for size's sake.
- **The compile-time resource registry is the OCP model.** `REGISTER_COMPILE_TIME_TYPE` (CompileTimeResourceSystem.h:76-90): a new resource type is one macro line with a `static_assert` gate — this is the pattern the SDF-op tables (V-M1) should converge to.
- **Layering holds.** No library→application includes; the cross-library graph is acyclic with documented deliberate relocations to break cycles (e.g. CashSystem CMakeLists:37).
- **`RenderFrame` error strategy is coherent.** Both sequential and parallel paths catch per-node exceptions, convert to VkResult (nothing throws across the C# host boundary), and the device-lost latch outranks generic failures.
- **The generated VRC1 recipe reader validates correctly** — null/len/magic/version/exact-size checks with `size_t` widening (`RecipeContainer.g.h:23-33`). It is the in-repo model the hand-written UTVW reader (V-C1/2) should copy.
- **The stale-DLL trap now has layered guardrails** *[consumer host]* — Release-preferred managed-dir resolution, a 48h mtime warning with the exact rebuild command, and an all-zero-columns smell test (`main.cpp:204-227,281-289`). Strongest DX response in the integration; generalize it.
- **TODO hygiene is reasonable:** 102 TODOs, 0 FIXME/HACK/XXX in code; used as honest forward-markers, concentrated in SVO-builder inputs and resource-ownership seams.

---

## Findings

### Critical

#### V-C1. [Trust boundary] The UTVW view-slice wire reader does ZERO bounds validation on any offset it reads *[consumer host]*
- **File:** `undertow/vixen/render/view_wire.h:16-30` + every accessor in `view_contract.h`
- **Why:** `WireReader::valid()` checks only `len >= 12` + the magic. After that, section bases, column counts, column offsets, var-column offset arrays, and string-blob offsets are `rd_u32`'d from the buffer and used to index **without ever comparing against `buf_end`** (which is threaded into the section classes but never used to bound anything). Every `std::string_view` ctor builds pointer+length from untrusted offsets. A truncated or malformed frame ⇒ out-of-bounds reads (crash / info leak).
- **Fix:** Validating pass or per-access guards: every section base, column `off`/`len`, and var-offsets array must lie in `[0, len]` before any accessor dereferences. Round-trip tests exist but no truncation/corrupt-offset test — add one; it drives the fix.

#### V-C2. [Trust boundary] `column()` computes `end - off` unsigned with no ordering guarantee — corrupt TOC ⇒ ~4GB span
- **File:** `undertow/vixen/render/view_wire.h:24-25`
- **Snippet:** `out_len = end - off;` — both untrusted; `end < off` wraps to a near-4GB "valid" column length.
- **Fix:** Reject the column (`nullptr`, `out_len=0`) when `end < off || off > len || end > len`.

#### V-C3. [Trust boundary / UX] The view `format_version` is produced, documented, and then discarded by every consumer read site *[consumer host]*
- **File:** `undertow/vixen/app/src/main.cpp:129-134, 631-634`; contract at `undertow/vixen/abi/undertow_abi.h:32-33`
- **Observed:** `ut_view(sim, &ptr, &len, &ver);` — then `ver` is never compared to anything; there is no `kViewFormatVersion` constant on the C++ side at all. The ABI *function* version is gated (abort at `main.cpp:258`); the view *schema* version — the one that shifts on every render-field add — is not. Because the columnar reader tolerates missing columns by returning defaults (`kind_ ? kind_[i] : 0`), schema drift renders silently-wrong output. This exact class already cost days (2026-07-03 all-zero recipe columns), and the engine's own cache codecs all version-gate loudly — this is the one boundary that doesn't.
- **Fix:** Emit `kViewFormatVersion` into the generated `view_contract.h`; host refuses to decode (or `LOG_ERROR`s once) on mismatch, mirroring the ABI-version abort.

### Major — Architecture (SRP / OCP / god classes)

#### V-M1. [OCP] SDF op set: kernel layer is generated from Yeroket, but four per-op glue tables remain hand-mirrors
- **File:** `libraries/SVO/include/Recipe/SdfRecipeEval.h` (VM dispatch), `SdfRecipeCodegen.h:94` (HLSL emit), `RecipeStack.h:20` (arity), `RecipeRegistry.h:13` (validity)
- **Why:** Verified split: the enum (`SdfOpCodes.g.h`), the C++ kernel bodies (`SdfCoreKernels.g.hpp`), the HLSL kernels, and `RecipeContainer.g.h` ARE generated by the Yeroket kernel-framework emitter — per-op *math* cannot drift. But no generator emits the four glue/metadata tables (confirmed: zero hits in `SourceGenerator~`, `CodegenTool~`, and VIXEN `codegen/`; no provenance banners; two carry `ponytail:` hand-maintenance markers). Adding op #88 in Yeroket regenerates enum+kernels free, then requires 4 synchronized hand edits — and the emit switch's `default: break;` (SdfRecipeCodegen.h:899) silently drops an unhandled opcode rather than failing to compile.
- **Fix:** Extend the Yeroket emitter (it already has the kernel signatures, which give arity) to also generate the dispatch/arity/validity tables — or at minimum emit a completeness `static_assert` so a new enumerator without an emit case breaks the build. Target pattern: `REGISTER_COMPILE_TIME_TYPE`.

#### V-M2. [God class] `VoxelSceneCacher` fuses a 6-stage asset-build pipeline into a "cache"
- **File:** `libraries/CashSystem/src/VoxelSceneCacher.cpp:108-904`
- **Why:** One class owns cache keying, scene generation, octree construction, compression, brick-grid lookup, GPU buffer creation+upload (~175 lines), material-palette authoring, AND file serialization — independent reasons-to-change sharing private state. The "cacher" name hides that it's the whole voxel-scene build pipeline.
- **Fix:** Keep the cache/key/serialize role; extract Generate→BuildOctree→Compress→BrickGrid→Upload into a `VoxelScenePipeline` (or the existing `SceneGeneratorFactory` seam) returning a built `VoxelSceneData`.

#### V-M3. [God class] `BenchmarkGraphFactory` holds three orthogonal render-path pipelines (2,399 lines)
- **File:** `libraries/Profiler/src/BenchmarkGraphFactory.cpp:104-2399`
- **Why:** Compute, fragment, and hardware-RT graph-construction pipelines in one class, each with its own Build/Configure/Connect/Wire/Register set (`ConnectHardwareRT` alone ~248 lines); the include block makes the profiler transitively know the entire node catalog.
- **Fix:** One factory per render path behind a small `IBenchmarkGraphFactory`, each owning its own node includes.

#### V-M4. [Consistency/OCP] RenderGraph core special-cases concrete nodes by magic type-name strings — against its own documented layering
- **File:** `libraries/RenderGraph/src/Core/RenderGraph.cpp:547, 637, 1018, 1071, 1370`
- **Why:** `if (nodeType->GetTypeName() == "SwapChain") …` / `"Device"` / `"GeometryRender"` in Compile/RecoverFromDeviceLoss/Validate — while `RenderFrame`'s own comment (782-784) celebrates that the core "no longer special-cases any concrete node type." Rename a node ⇒ silent behavior loss.
- **Fix:** Capability interfaces (`IDeviceProvider`, `ISwapchainResourceProvider`) discovered via `dynamic_cast`, the pattern the core already uses (`IGraphCompilable`).

### Major — Trust boundaries (disk caches)

#### V-M5. `ReadVector` trusts a 64-bit length from disk and `resize()`s before any check — `VoxelSceneCacher.cpp:202`. Unwrapped in try/catch AND crosses the `std::async`/`future.get()` boundary in `LoadAll`, so a corrupt size crashes the load of *all* caches. Cap vs remaining file length + overflow-check `size*sizeof(T)`.
#### V-M6. Shader-cache `spirvSize`/string lengths drive unbounded `resize()` — `shader_module_cacher.cpp:428-468`. Same class, wrapped (better) but still a load-time DoS.
#### V-M7. SPIR-V loader heap overflow: `memcpy`s `fileSize` bytes into a `(fileSize/4)*4` buffer — `shader_module_cacher.cpp:278`. Non-multiple-of-4 file overflows by up to 3 bytes; `file.read` result unchecked; no SPIR-V magic check. Reject `size%4!=0`, copy `spirvCode.size()*4`.
#### V-M8. Pipeline-cache blob fed to `vkCreatePipelineCache` with no header/UUID validation — `pipeline_cacher.cpp:473-481`. The Vulkan spec requires validating `VkPipelineCacheHeaderVersionOne` + vendor/device/UUID before trusting a blob; a foreign-GPU cache goes straight to the driver.

*(Root cause for V-M5..M8: three cachers re-implement magic/version/length serialization divergently. One shared bounds-checked `CacheReader`/`CacheWriter` fixes all four in one place — the single highest-value refactor in this report. The base `TypedCacher` locking discipline is correct; it's the hand-written overrides that bypass it.)*

### Major — Thread safety

#### V-M9. `PipelineCacher` mutates `m_entries`/`m_globalCache` unlocked while `DeviceRegistry` fires cachers concurrently via `std::async` — `pipeline_cacher.cpp:335,366,494` (driven from `device_identifier.cpp:113,184`). Only the fast-path `find` is guarded.
#### V-M10. `DeferredDestructionQueue` is unsynchronized but fed from atomic refcounts — `Lifetime/DeferredDestruction.h:338`. Last `Release()` can run on any worker thread → unguarded ring-buffer mutation racing the frame loop's `ProcessFrame` → double-run or dropped destructor = double-free or leaked Vulkan handle.
#### V-M11. Concurrent `vkQueueSubmit` on one `VkQueue` is reachable under the TBB parallel executor — `ComputeDispatchNode.cpp:310`, `TraceRaysNode.cpp:356`. The access tracker models only data-flow resources; the queue isn't one, so two conflict-free submit nodes can share a level. Vulkan requires external queue synchronization — UB. Latent today (swapchain semaphores serialize), armed the moment two independent submit chains coexist. Per-queue mutex or model the queue as a shared-exclusive resource.
#### V-M12. Non-atomic `++stats_.failedTasks` inside the parallel task lambda — `TBBVirtualTaskExecutor.cpp:246,251`; the neighboring `RecordError` IS mutex-guarded, so this is an inconsistency, not a choice.
#### V-M13. `DirectAllocator::CreateAliased*` dereference a map-record pointer after releasing the lock — `DirectAllocator.cpp:515-607`. Concurrent `FreeBuffer` can erase the entry → UAF, can bind freed memory. VMAAllocator does it right (copies fields under the lock).

### Major — Leaks & error handling

#### V-M14. A throw during `UploadToGPU` leaks up to 7 `VkBuffer`s + device memory — `VoxelSceneCacher.cpp:33, 835-893`; `VoxelSceneData` has no RAII destructor and cleanup only runs for entries that reached `m_entries`. Scope-guard the creation.
#### V-M15. `StagingBufferPool` reserves staging quota on every acquire but releases only on destroy, not pool-return — `StagingBufferPool.cpp:70,128-131,378-446`. A buffer reused N times leaks `(N-1)×bucketSize` of quota → reserves start failing → uploads deadlock via backpressure.
#### V-M16. `BatchedUploader::SubmitBatch` ignores `vkCreateFence`/`vkQueueSubmit` failures — `BatchedUploader.cpp:466-473`. Failed submit ⇒ a fence that never signals ⇒ FIFO `ProcessCompletions` blocks forever; null fence ⇒ staging released while the GPU may still read it.
#### V-M17. Pool exhaustion leaves `cmdBuffer` null guarded only by an `assert` — `BatchedUploader.cpp:421`. Under NDEBUG, `vkBeginCommandBuffer(VK_NULL_HANDLE,…)` is UB. Replace with a real branch (mark Failed, release staging, return).

### Major — Dead code / DRY

#### V-M18. `RenderGraph::Execute(VkCommandBuffer)` is an unused stale duplicate of the render loop — `RenderGraph.cpp:570`. Lacks device-loss handling, the parallel path, and per-node catching; independently increments `globalFrameIndex`. Delete it (+ two stale `LoopManager.h` doc references).
#### V-M19. Byte-identical swapchain barrier helpers copied across compute nodes — `ComputeDispatchNode.cpp:473,569` == `ComputeStageNode.cpp:330,349`. Any VUID/stage-mask fix must be made in every copy. Extract `Barriers::ImageToGeneral/ImageToPresent`.

### Major — Clarity

#### V-M20. [Naming] `CashSystem` is a confirmed misspelling of "Cache", not a pun — whole library (`namespace CashSystem`, ~90 `*Cacher` identifiers) while `libraries/README.md:12` describes it as a "persistent resource **cache**". Every grep for `Cache` in the cache system finds nothing. Rename `CashSystem`→`CacheSystem`, `*Cacher`→`*Cache` as one dedicated commit; if out of scope, add a legacy-note at the library root.
#### V-M21. [Stale comments] "MVP STUB" banners lie about fully-implemented code — `DescriptorSetNode.cpp:38,46` (a complete descriptor-set implementation labeled stub, while three genuinely-unimplemented methods at :897-923 also say stub — the reader can't tell which is real). Pattern recurs in BuildRenderGraph.cpp, ShaderLibraryNode.cpp, SPIRVReflection.h, VulkanGraphApplication.cpp.
#### V-M22. [Debug residue] Six unconditional `std::cout` writes in the hot descriptor-write path — `DescriptorSetNode.cpp:348-357,714,787-788`, printing raw pointers on first-frame binds, unlike the neighboring guarded logs. The bufferInfos-reallocation "WARNING" at :714 invalidates `pBufferInfo` and deserves `NODE_LOG_ERROR`/assert, not cout.
#### V-M23. [Commented-out code] ~330 lines of dead graphics-pipeline blocks in `BuildRenderGraph.cpp` (143-367, 752-864, camera presets 501-526) — including a hardcoded personal path `"C:\\Users\\liory\\Downloads\\earthmap.jpg"` (:276). Roughly a quarter of the file is dead commented code. Delete; git preserves history.
#### V-M24. [Magic number] `esvoMaxScale = 22` duplicated as a bare literal — named `ESVO_MAX_SCALE` in three headers but re-hardcoded at `VoxelSceneCacher.cpp:605`; a change silently desyncs the cached config from traversal. Use the constant; consolidate the three header definitions to one.

### Major — Consumer UX *[consumer host + engine]*

#### V-M25. Recipe id/provider contract is bare `uint32_t`s + hand-kept comments duplicated in ≥3 files — `main.cpp:115-116`, `scene_instances.h:96-97`, `star_scene.h:30-31` (`// 0 = sphere, 1 = displaced_sphere`). The authoritative enum lives in C#; no shared header, no drift test. Generate `recipe_ids.h` from the same schema source, like `view_contract.h` already is.
#### V-M26. Engine loggers are silent by default — `logger/Logger.h:23,68-69`: `enabled=false` + `terminalOutput=false` per instance; an internal `LOG_ERROR` reaches the terminal only if that specific logger had both flags set. 71 LOG_ERROR/WARN sites can vanish → engine-detected misconfigurations present as a black screen. Default terminal output ON for ERROR/CRITICAL, or add `SetGlobalTerminalOutput`.
#### V-M27. The recurring FR class — silent cross-boundary mismatch — is point-fixed but structurally open. FR-4 (typed connection accepts implicit conversion), FR-11 (`__COUNTER__` message ids mis-route cross-TU), FR-22 (unfiltered instance extensions) are the same root class; each instance was fixed, but the requested FR-4/FR-9 graph-validation pass (reject slot type mismatch, flag render node with no framebuffer, present node with no semaphore) never landed — and V-C3/V-M25 are new instances. One structural validation pass retires the class.

### Minor

- **V-N1** RenderGraph owns session-lock/crash-recovery filesystem lifecycle (`RenderGraph.cpp:308-343,1221-1267`) — move to MainCacher/`CacheSessionGuard`.
- **V-N2** Stringly-typed "GeometryRender" validation block is an empty placeholder (`RenderGraph.cpp:1014-1039`) — delete until real validation exists.
- **V-N3** `NodeInstance` base accretes task-profiling + virtual-task APIs onto every node (`NodeInstance.h:109-537`) — split behind opt-in `IProfilable`/`IParallelizable`.
- **V-N4** `*_OLD.h` headers ×3 + `GaiaVoxelWorld_ITERATOR_EXAMPLE.cpp` compiled into production libs — delete/move; version-in-filename where git already tracks history.
- **V-N5** GPU-mirror boundary epsilon `be = 1e-4f` vs CPU's named `boundary_epsilon = 0.01f` (`SVOTraversal.cpp:975-976` vs 746-747) — same logic, different magnitudes, no comment on whether divergence is intentional. **Given the file's bit-for-bit parity goal, verify this isn't a live parity bug.**
- **V-N6** Deserialized `OctreeConfig`/vector sizes used without semantic cross-checks (`VoxelSceneCacher.cpp:343`).
- **V-N7** VoxelScene header/`entryCount` read before EOF check (`VoxelSceneCacher.cpp:294`).
- **V-N8** `TraceRaysNode` swallows begin/end/reset/submit failures where sibling `ComputeDispatchNode` throws (`TraceRaysNode.cpp:183-356`) — device-loss on the RTX path never reaches recovery.
- **V-N9** `vkResetFences` result unchecked (`ComputeDispatchNode.cpp:183`).
- **V-N10** `RegisterExternalCleanup` unguarded function-local `static` counter (`RenderGraph.cpp:447`).
- **V-N11** `RemoveNode` shifts indices but leaves cleanupStack/dirtyNodes/caller handles stale (`RenderGraph.cpp:297-303`).
- **V-N12** Budget TOCTOU: check-then-record lets two threads exceed `maxBytes` (`ResourceBudgetManager.cpp:264`).
- **V-N13** `HostBudgetManager::ResizeFrameStack` can realloc the arena under the lock-free bump allocator (`HostBudgetManager.cpp:201`).
- **V-N14** `DirectAllocator::Free*` deletes/frees untracked handles and skips budget decrement (`DirectAllocator.cpp:170-336`).
- **V-N15** `BatchedUploader::uploadStatus_` map never pruned — steady growth (`BatchedUploader.cpp:511`).
- **V-N16** Inverted `Upload` non-mapped fallback queues an unwritten staging buffer — garbage transferred (`BatchedUploader.cpp:87`).
- **V-N17** WSL software-Vulkan (10-25x slower) not surfaced in README for newcomers — buried in a preset description + FR-20.
- **V-N18** `shader_tool` header comment vs `sdi_tool` in PrintUsage/--version — two names, one binary (`shader_tool.cpp:8-12` vs 199-321).
- **V-N19** README cites Vulkan SDK `1.4.321.1`; the provisioned tree is `1.4.350.1`.
- **V-N20** The advertised `debugging-known-issues` skill doesn't exist on disk — restore or de-list.

### Info (selected)

- **`assert`-guarded pointers dereferenced unconditionally in release** (`StagingBufferPool.cpp:24`, `BatchedUploader.cpp:24-26`) — take references or real checks.
- **`BudgetBridge::onUploadComplete_`** read under lock, written without (`BudgetBridge.cpp:178`); "drop oldest (assume completed)" reclaims staging the GPU may still read (77-81).
- **`DynamicVoxelStruct`** uses try/`any_cast`/catch(...) as type dispatch on a per-voxel path, 10+ near-identical blocks (`DynamicVoxelStruct.cpp:91-181`) — dispatch on `arrayAny.type()` instead.
- **Vestigial CashSystem code:** unused `CreateBuffer` (VoxelSceneCacher.cpp:904); `main_cacher.cpp:19-27` sentinel-address "static-deinit" guards that mask shutdown UAF; documented no-op `SetBudgetManager`.
- **Illustrative "emits:" gap:** stale `#include` provenance comments in two headers point at the wrong home library (VertexBufferNode.h:5).
- **Positives verified:** VMAAllocator error/free/aliasing paths clean; `SharedResourcePtr` single-destroy discipline correct; all CashSystem file I/O is RAII; cache codecs version-gate loudly (the view contract is the outlier); FR ledger ~16 FIXED / ~7 open, with the open ones clustered on ergonomics/validation, not correctness.

---

## Action Items (Prioritized)

### Immediate — safety
| # | Issue | Effort |
|---|-------|--------|
| V-C1/C2 | Bounds-validate the UTVW wire reader + add a truncation/corrupt-offset test (copy the VRC1 reader's discipline) | Small |
| V-C3 | Emit `kViewFormatVersion` into `view_contract.h`; gate decoding on it | Small |
| V-M17/M16 | Real branch on null cmdBuffer; check fence/submit results in BatchedUploader | Small |
| V-M15 | Fix the staging-quota reserve/release pairing | Small |
| V-M22 | Route the six `std::cout`s through NODE_LOG; promote the reallocation warning | Trivial |

### Next — structural
| # | Issue | Effort |
|---|-------|--------|
| V-M5..M8 | One shared bounds-checked `CacheReader`/`CacheWriter`; standardize the three cachers' failure strategy (try/catch→false→regenerate) | Medium |
| V-M9..M13 | Thread-safety pass: lock the cacher bodies, atomic stats counter, per-queue submit mutex, copy-under-lock in DirectAllocator, guard the deferred-destruction queue | Medium |
| V-M1 | Generate (or static_assert-gate) the four SDF-op glue tables from the Yeroket emitter | Medium |
| V-M27 | The FR-9 graph-validation pass — retires the recurring silent-mismatch class | Medium |
| V-M18/M23/N4 | Delete dead `Execute()`, the commented-out pipeline blocks, `_OLD` headers | Trivial |
| V-M20 | `CashSystem`→`CacheSystem` rename (one dedicated commit) | Small, wide |
| V-N5 | Verify the CPU/GPU boundary-epsilon divergence isn't a parity bug | Trivial to check |

### Ongoing
| # | Issue | Direction |
|---|-------|-----------|
| V-M2/M3 | Pipeline-fused god classes | Split behind the interfaces the codebase already uses; ratchet, don't rewrite. |
| V-M4 | String-typed node probing in the core | Convert to capability interfaces as each site is next touched. |
| V-M26 | Logger defaults | Error-level output should be on by default before the next consumer integration round. |

## Metrics
- Lines in scope: ~212,000 across 1,158 C++ files (incl. tests; tests excluded from findings)
- Deep-read chunk-by-chunk: ~15 hotspot files (RenderGraph core, SVO traversal + recipe chain, the three cachers, ResourceManagement memory subsystem, BatchedUploader, view-slice readers, BuildRenderGraph, BenchmarkGraphFactory)
- Consumer evidence: `consumer-feedback-undertow.md` read in full (23 FRs: ~16 fixed, ~7 open); undertow-side host read as the integration seam
- Mode: Full — 4 specialists (architecture, clarity, maintainability/C++-safety, consumer-UX) with non-overlapping rules; the one overlapping finding (view version gate) merged

---

## Post-merge status 2026-07-07

Re-verified against `claude/wsl-build-portability` synced to `origin/main` (fast-forward — the
branch had no unique commits since its last sync, so `git merge origin/main` advanced HEAD
directly to origin/main's tip `4d57b60f`, 218 commits ahead of the pre-sync HEAD; no merge commit
was created). Each finding re-anchored by symbol (not the audit's line numbers, which predate the
sync) with one grep + one targeted read. V-C1/C2/C3 are `undertow`-repo findings — unaffected by
this engine-repo sync, skipped here.

| ID | Status | Note |
|----|--------|------|
| V-C1/C2/C3 | SKIPPED | undertow-side (`vixen/render`, `vixen/app`), not touched by this merge |
| V-M1 | LIVE (not re-confirmed deeply) | Time-boxed; glue-table `default: break;` grep inconclusive, no closer read done |
| V-M2 | LIVE | `VoxelSceneCacher.cpp` still 922 lines, still fuses build+cache+upload |
| V-M3 | LIVE | `BenchmarkGraphFactory.cpp` now 2,405 lines (grew slightly) |
| V-M4 | LIVE | `RenderGraph.cpp` still string-compares `GetTypeName() == "SwapChain"/"Device"/"GeometryRender"` (5 sites) |
| V-M5 | LIVE | `ReadVector` (moved to `CashSystem/src/VoxelSceneCacher.cpp:202`) still `resize(size)`s an unchecked disk `uint64_t` |
| V-M6/M7 | LIVE (not re-confirmed) | `shader_module_cacher.cpp` moved `ShaderManagement/`→`CashSystem/src/`; not re-read line-by-line this pass |
| V-M8 | LIVE | `pipeline_cacher.cpp` still builds `cacheInfo.pInitialData` from a raw disk blob with zero header/UUID validation before `vkCreatePipelineCache` |
| V-M9 | LIVE | `pipeline_cacher.cpp` has exactly one `shared_lock` (fast-path find); `SerializeToFile`/`DeserializeFromFile` (driven from `device_identifier.cpp` `std::async`) mutate `m_globalCache`/`m_entries` unlocked |
| V-M10 | LIVE | `DeferredDestruction.h`'s `DeferredDestructionQueue` class body has zero `mutex`/`lock` hits |
| V-M11 | LIVE | `ComputeDispatchNode.cpp`/`TraceRaysNode.cpp` `vkQueueSubmit`(2) call sites have zero `mutex`/`lock_guard` hits |
| V-M12 | LIVE | `TBBVirtualTaskExecutor.h:66` `failedTasks` still plain `size_t`, not atomic |
| V-M13 | LIVE | `DirectAllocator.cpp::CreateAliasedBuffer` still captures `sourceRecord` under `lock_guard` (514-524) then dereferences it at line 540, after the lock scope closes |
| V-M14 | LIVE (pattern-confirmed) | `UploadToGPU` still has multiple `throw` sites (835-893) after buffer creation begins; no scope guard visible |
| V-M15 | **FIXED-ON-MAIN** | Confirmed: `ReturnToBucket` pools the buffer (no quota release needed — still reserved); `DestroyBuffer` (the real teardown path, called on bucket-eviction and explicit destroy) correctly calls `ReleaseStagingQuota` |
| V-M16/M17 | LIVE | `BatchedUploader.cpp`: `vkCreateFence`/`vkQueueSubmit` return values discarded (469, 474, 476); `assert(cmdBuffer != VK_NULL_HANDLE)` (424) is still assert-only, no real branch |
| V-M18 | LIVE | `RenderGraph::Execute(VkCommandBuffer)` still defined at `RenderGraph.cpp:575`, alongside the real render loop |
| V-M19 | LIVE | `TransitionImageToGeneralBarrier2`/`ToPresentBarrier2` still separately defined in both `ComputeStageNode.cpp` and `ComputeDispatchNode.cpp` |
| V-M20 | LIVE | 50 files still `namespace CashSystem` / `*Cacher` |
| V-M21 | LIVE | `DescriptorSetNode.cpp` still has "MVP STUB" banner at 33/41 over what the audit found to be complete code, alongside genuinely-stub methods at 1003-1027 — same ambiguity |
| V-M22 | **FIXED-ON-MAIN** | Confirmed: `grep -c std::cout DescriptorSetNode.cpp` = 0 |
| V-M23 | LIVE | `BuildRenderGraph.cpp` still has `DISABLED FOR COMPUTE TEST` blocks (148, 307, 813) and the hardcoded `C:\Users\liory\Downloads\earthmap.jpg` path (318) |
| V-M24 | LIVE | `VoxelSceneCacher.cpp:605` still `esvoMaxScale = 22` as a bare literal |
| V-M25 | LIVE (not re-confirmed) | Not re-read this pass; no reason to expect it moved |
| V-M26 | LIVE | `Logger.h`: `enabled=false` (23), `terminalOutput=false` (69) still both default off |
| V-M27 | LIVE (not re-confirmed) | Structural/cross-cutting; not re-read this pass |
| V-N1..N3 | LIVE (not re-confirmed) / V-N2 pattern seen | `RenderGraph.cpp:1053` still string-checks `"GeometryRender"`, consistent with V-N2 still applying |
| V-N4 | LIVE | All 3 `_OLD.h` headers + `GaiaVoxelWorld_ITERATOR_EXAMPLE.cpp` still present |
| V-N5 | LIVE | `SVOTraversal.cpp`: GPU-mirror `be = 1e-4f` (975) vs CPU `boundary_epsilon = 0.01f` (746) still diverge, unchanged |
| V-N6..N13 | SKIPPED (not re-confirmed) | Time-boxed; not colocated with files opened this pass |
| V-N14 | LIVE (not re-confirmed) | Not re-read this pass; no reason to expect it moved |
| V-N15 | LIVE | `BatchedUploader.cpp`'s `uploadStatus_` map: only `.find`/insert sites found, no prune/erase call |
| V-N16 | LIVE (not re-confirmed) | Not re-read this pass |
| V-N17 | LIVE (partial) | README does document the WSL/Linux Vulkan path but the specific "10-25x slower" performance warning wasn't confirmed present in this pass |
| V-N18 | LIVE | `shader_tool.cpp` header/usage comments still say `shader_tool` throughout except one `sdi_tool` reference in a piping example (line 71) — naming still split |
| V-N19 | LIVE | `README.md:22` still cites `C:/VulkanSDK/1.4.321.1`; provisioned tree is `1.4.350.1` |
| V-N20 | LIVE | No `debugging-known-issues` directory under this repo's `.claude/skills/` |

**Build-portability bugs found and fixed during this gate (not audit findings, pre-existing, not caused by the merge):**
1. `libraries/RenderGraph/tests/test_type_system.cmake` hardcoded `${VULKAN_PATH}/Include` (Windows-SDK casing) in 3 places instead of `${Vulkan_INCLUDE_DIRS}` — 404s on the case-sensitive Linux SDK layout (`x86_64/include`, lowercase). Fixed: swapped to `${Vulkan_INCLUDE_DIRS}`, matching the sibling non-trimmed test targets in the same file.
2. Two test files (`test_push_constant_gatherer_node.cpp`, `test_descriptor_resource_gatherer_node.cpp`) `#include "Libraries/RenderGraph/tests/TestMocks.h"` (capital `Libraries`) against the real lowercase `libraries/` — same class of bug. Fixed: lowercased both includes.
3. Root `CMakeLists.txt` called `enable_testing()` at line ~445, *after* `add_subdirectory(libraries)` (line 393) and `add_subdirectory(application)` (line 435) — CMake silently drops `add_test()` calls registered before `enable_testing()` runs. Result: `ctest -N` reported **0 tests** despite `BUILD_TESTS=ON`. Fixed: moved `enable_testing()` to immediately after `add_subdirectory(dependencies)`, before `libraries`/`application`. This is pre-existing (same line ordering confirmed on pre-merge HEAD `f53be93f`), not a merge regression — it just made the V0 gate's own ctest step non-functional until fixed.

**Build:** green, 303/303 targets (WSL/GCC preset `vixen-wsl`), after the 3 fixes above.
**Tests:** 2042 registered (0 before the `enable_testing()` fix) — 2041 ran (97% passed), **57 failed**, baseline (not investigated further, none in files this gate touched):
- `ShaderCacheManagerTest` ×7 (StoreAndRetrieveShader, OverwriteExistingCache, DifferentKeysStoreSeparately, InvalidateRemovesCache, ClearRemovesAllCaches, StoreMultipleShadersQuickly, CacheFilesCreatedOnDisk)
- `SwapChainNodeTest.ConfigHasSevenInputs`
- `EditorToggleUndoCapture.ToggleUndoRedoRoundTripThroughWindowedRun`
- `HudRenderCapture` ×3 (BaselineIsNonEmpty, PayloadSwapProducesRealPixelDifference, SameFrameDeterministic)
- `CalibrationStoreTest.DeleteFile`
- `SVOBuilderTest.GeometricError`
- `VoxelInjectionNewAPITest` ×2 (SparseVoxels, MultipleVoxelsSpread)
- `PartialBlockUpdateTest.AddNewBrick`
- `BrickViewTest` ×2 (Index3DOutOfBounds, ThreeDCoordinateAPI)
- `EntityBrickViewTest` ×~22, most **SEGFAULT** (GetSetEntity_LinearIndex, GetSetEntity_AllVoxels, ClearEntity_LinearIndex, GetSetEntity_3DCoords, GetSetEntity_AllCubicPositions, ClearEntity_3DCoords, GetDensity_LinearIndex, GetDensity_3DCoords, GetColor_LinearIndex, GetColor_3DCoords, GetNormal_LinearIndex, GetNormal_3DCoords, GetMaterialID, GetEntitiesSpan[+Const], SpanIterateAllEntities, CountSolidVoxels_PartiallyFilled/FullBrick, IsEmpty_False, IsFull_True/False, ZeroCopySpanAccess, SetEntity_BoundaryVoxels, ClearEntireBrick, SparseOccupancy, ModifyEntityAttributes_ThroughBrickView, DestroyEntity_BrickViewHandlesGracefully)
- `ComprehensiveRayCastingTest` ×2 (RaysFromInsideGrid, MultipleVoxelTraversal)
- `AttributeRegistryIntegrationTest` ×2 (BrickViewPointerAccess, BackwardCompatibility_StringLookup)
- `BrickTraversalTest` ×4 (BrickHitToLeafTransition, RayThroughMultipleBricks, BrickDDAStepConsistency, BrickToBrickTransition)
- `CornellBoxTest` ×2 (FloorHit_FromAbove, LeftWallHit_Red)
- `LODRayCastingTest.NoPerformanceRegressionWithoutLOD`

Since `ctest` registered 0 tests before this gate's fix, there is no prior pass/fail baseline to diff
against — this 97%/57-failed snapshot **is** the new baseline. None of the 57 failures are in files
touched by the 3 build-portability fixes above.
