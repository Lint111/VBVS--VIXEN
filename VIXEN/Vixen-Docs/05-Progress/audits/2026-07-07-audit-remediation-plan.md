# VIXEN Audit Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **Pipeline note:** designed for the context-aware pipeline — milestones are chunk boundaries; suggested worker tier annotated per milestone. **Milestone V0 is a hard gate: no other milestone may start before it completes**, because `origin/main` is 203 commits ahead of the audited branch and has already fixed some findings.

**Goal:** Close the Immediate + Next findings from `Vixen-Docs/05-Progress/audits/audit-2026-07-06-23-45-code-quality.md` (the audit is the spec; V-* IDs refer to it), on top of the latest `origin/main`.

**Architecture:** Sync the working branch to main first, re-verify every finding at its new location (some are fixed on main), then fix in safety-first order: trust boundaries → error handling → thread safety → dead code/clarity. Tasks anchor to SYMBOLS, not the audit's line numbers — line numbers predate the merge.

**Tech Stack:** C++17/20, Vulkan, CMake presets, GoogleTest-style test targets. Two repos are touched: the engine (`vixen/engine`, this repo) and the **consumer host in the undertow repo** (`undertow/vixen/{render,app,abi}`) — tasks state which.

## Global Constraints

- Engine work happens on a branch off the SYNCED `claude/wsl-build-portability` (or directly off `origin/main` if the user prefers — ask ONCE at V0, then proceed). Never commit to main directly. Do not push without a fresh confirm (memory rule).
- Consumer-host tasks (V1.x) edit the **undertow repo** — use a worktree there, branch `audit-remediation-view`.
- Build/test on WSL is 10-25x slower than native Windows for GPU paths; all tasks in this plan are CPU-testable — use the WSL preset only for compile+unit tests, never judge perf here. Watch long builds with a polling loop, never a blind wait (memory rule).
- Build: `cmake --preset vixen-wsl && cmake --build build-wsl -j` (adjust to the presets found in `CMakePresets.json` after sync). Test: `ctest --test-dir build-wsl -j --output-on-failure`.
- Every task: build green + relevant test target green before commit. No `//TODO` left behind by this plan's own work.

---

## Milestone V0 — Sync + re-verify (tier: medium; GATE for everything below)

### Task V0.1: Merge origin/main into the working branch

**Files:** whole tree (merge).

**Steps:**
- [ ] `cd vixen/engine && git fetch && git merge origin/main` on `claude/wsl-build-portability`. Resolve conflicts preferring main's side for files this plan doesn't touch; if a conflict lands in a file named by a task below, resolve minimally and note it in the merge commit body.
- [ ] Build: full configure+build with the WSL preset. Expected: green (main's HEAD message says xcb headless build gap was fixed — if the build fails on missing X headers, read `origin/main`'s recent build commits before patching anything).
- [ ] Run the full ctest suite; record pass/fail counts in the plan doc under this checkbox. Pre-existing failures (if any) are the baseline — note them, don't fix them here.
- [ ] Commit is the merge commit itself. Do not push.

### Task V0.2: Re-verify every finding; mark FIXED-ON-MAIN

**Files:** Modify: this plan document (strike tasks), and `Vixen-Docs/05-Progress/audits/audit-2026-07-06-23-45-code-quality.md` (append a `## Post-merge status 2026-07-07` section).

**Steps:**
- [ ] Already spot-checked (2026-07-07): V-M22 (couts) **FIXED on main** — verify `grep -c "std::cout" libraries/RenderGraph/src/Nodes/DescriptorSetNode.cpp` is 0 and strike task V5.3. V-M15 (staging quota) **likely fixed** — read `StagingBufferPool::ReturnToBucket` and confirm `ReleaseStagingQuota` is called on the pool-return path; if so strike V3.4's quota half.
- [ ] For each remaining task below, confirm its anchor symbol still exists and the defect is still present (one grep + one read each). Strike any task fixed on main; append the finding→status table to the audit doc.
- [ ] Commit: `docs(audit): post-merge re-verification — findings fixed on main struck from remediation`

## Milestone V1 — The view-contract trust boundary (tier: high; **undertow repo**, consumer host)

### Task V1.1: Bounds-validate the UTVW wire reader (V-C1, V-C2)

**Files:**
- Modify: `undertow/vixen/render/view_wire.h` (`WireReader`, `column()`)
- Modify: `undertow/vixen/render/view_contract.h` (accessors — only where they trust var-offsets)
- Test: `undertow/vixen/render/test_view_contract.cpp` (extend the existing round-trip fixture)

**Interfaces:**
- Produces: `WireReader::column(...)` returns `nullptr` with `out_len = 0` for any column whose `[off, end)` is not fully inside `[0, len)` or where `end < off`. Section lookup returns false for a section whose base/count/TOC would read past `len`. Accessors keep their existing "missing column → default value" behavior — that contract is deliberate; this task only closes OUT-OF-RANGE, not MISSING.

**Steps:**
- [ ] Write the failing tests first (names/style per the existing file):

```cpp
TEST(ViewWire, TruncatedBufferYieldsNoSections) {
    auto full = BuildValidFrame();                    // existing fixture helper
    for (size_t cut = 0; cut < full.size(); cut += 7) {
        undertow::view::WireReader r(full.data(), cut);
        // must not crash / ASAN-trip; either invalid or zero rows
        if (r.valid()) { BodiesSection s; if (r.section(kSectionBodies, s)) (void)s.row_count(); }
    }
}
TEST(ViewWire, CorruptTocOffsetsRejected) {
    auto buf = BuildValidFrame();
    // stomp a column offset in the TOC to point past the end
    OverwriteFirstColumnOffset(buf, 0x7fffffff);      // small helper added in this task
    undertow::view::WireReader r(buf.data(), buf.size());
    BodiesSection s;
    ASSERT_TRUE(r.section(kSectionBodies, s) == false || s.row_count() == 0);
}
TEST(ViewWire, ReversedColumnRangeYieldsZeroLen) {
    // end < off must produce out_len == 0, never an underflowed span
}
```

- [ ] Run under ASAN if the preset supports it (`-DCMAKE_CXX_FLAGS=-fsanitize=address` on the test target, or the repo's sanitizer preset if one exists) — expected: the truncation test CRASHES or ASAN-reports on the current code. That is the red state.
- [ ] Implement: thread `len` through `WireReader` (it already stores `buf_`+len); in `column()`:

```cpp
uint32_t off = rd_u32(toc + 4);
uint32_t end = (i + 1 < count) ? rd_u32(toc + 12) : uint32_t(len_ - base_);
if (end < off || base_ + end > len_ || base_ + off > len_) { out_len = 0; return nullptr; }
out_len = end - off;
```

  and in `section(...)`: validate `base_ + 8 + count*8 <= len_` before walking the TOC, and the section base itself `<= len_`. In `view_contract.h`, any accessor that reads a var-offsets array must bound each entry the same way (model: the generated VRC1 reader `RecipeContainer.g.h:23-33` — copy its discipline, including `size_t` widening before adds).
- [ ] Run the three tests + the existing round-trip tests — expected: all green, ASAN clean.
- [ ] Commit (undertow repo): `fix(view): UTVW wire reader bounds-validates every section/column offset (audit V-C1/C2)`

### Task V1.2: Gate decoding on the view format version (V-C3)

**Files:**
- Modify: the C# schema emitter that generates `view_contract.h` (find it: `grep -rl "view_contract" core/src/Undertow.View core/src/Undertow.Authoring.Codegen`) — emit `constexpr uint32_t kViewFormatVersion = <ViewSchema.FormatVersion>;`
- Modify: `undertow/vixen/app/src/main.cpp` — both `ut_view` call sites
- Test: `undertow/vixen/render/test_view_contract.cpp` + the C# `ViewContractHeaderTests`

**Steps:**
- [ ] In the generator, emit the constant into `view_contract.h` from the same `ViewSchema.FormatVersion` the writer stamps. Regenerate the header via the documented flow (the C# test `ViewContractHeaderTests` pins the committed header — run it; it will tell you the regen env var if the header drifts).
- [ ] In `main.cpp`, after each `ut_view(sim, &ptr, &len, &ver)`:

```cpp
static bool versionWarned = false;
if (ver != undertow::view::kViewFormatVersion) {
    if (!versionWarned) { hostLog.Error("view schema mismatch: wire v%u, host built for v%u — refusing to decode; rebuild the host (see managed_dir.h stale-sim guard)", ver, undertow::view::kViewFormatVersion); versionWarned = true; }
    return /*skip frame decode; render last-good scene*/;
}
```

  (match the existing stale-DLL guard's tone at `main.cpp:204-227` — this is its natural next layer; wire the same remediation text style).
- [ ] Test: C# side — extend `ViewContractHeaderTests` to assert the emitted header contains `kViewFormatVersion`. C++ side — a unit test that a `WireReader` frame stamped with `FormatVersion+1` is refused by the host decode helper (extract the check into a small `bool AcceptFrame(uint32_t ver)` if `main.cpp` isn't unit-testable — it isn't; the helper lives in `undertow/vixen/render/view_wire.h`).
- [ ] Run: full undertow suite (`dotnet test core/Undertow.sln`) + the vixen render tests.
- [ ] Commit (undertow repo): `fix(view): host refuses to decode a mismatched view schema version (audit V-C3)`

## Milestone V2 — Disk-cache trust boundary (tier: high; engine repo)

### Task V2.1: One shared bounds-checked cache codec (V-M5..M8 root cause)

**Files:**
- Create: `libraries/CashSystem/include/CacheCodec.h` (header-only `CacheReader`/`CacheWriter`)
- Modify: `libraries/CashSystem/src/VoxelSceneCacher.cpp`, `shader_module_cacher.cpp`, `pipeline_cacher.cpp` (their Serialize/Deserialize bodies)
- Test: `libraries/CashSystem/tests/` (new `test_cache_codec.cpp` beside the existing test target — read `libraries/ResourceManagement/tests/` for the registration pattern)

**Interfaces:**
- Produces: `class CacheReader { bool ReadPod(T&); bool ReadString(std::string&, size_t maxLen); template<class T> bool ReadVector(std::vector<T>&, size_t maxElems); bool Ok() const; }` — every Read validates the length against BOTH a caller-supplied cap and the remaining file size, checks `size*sizeof(T)` overflow via `size_t` widening, and never throws: corrupt input ⇒ `false`, caller regenerates. `CacheWriter` mirrors it.

**Steps:**
- [ ] Write failing tests: `ReadVector` with a length word of `0xFFFFFFFFFFFFFFFF` returns false (no allocation); truncated stream returns false at the right read; a round-trip through `CacheWriter`→`CacheReader` reproduces the payload.
- [ ] Implement the codec (~120 lines, header-only, no deps beyond `<fstream>`).
- [ ] Migrate `VoxelSceneCacher::DeserializeFromFile` first (it is the unprotected one): every raw `in.read` → codec calls; delete the local `ReadVector` at the audited site; wrap the whole body `try/catch(const std::exception&) { return false; }` so nothing escapes across the `std::async` boundary in `LoadAll`. Header/magic/`entryCount` checked via `Ok()` immediately after reading (V-N7 folds in here).
- [ ] Migrate `shader_module_cacher` (V-M6) and `pipeline_cacher` (V-M8 length half) the same way. Uniform failure strategy: `false` = ignore + regenerate (V audit Info "three strategies" resolves to this one).
- [ ] Run all CashSystem tests + a full build. Expected: green; caches regenerate on first run after format-irrelevant changes (the codec preserves the on-disk format byte-for-byte — verify by diffing a cache file written before/after on the same scene).
- [ ] Commit: `fix(cash): shared bounds-checked CacheReader/Writer closes the disk trust boundary (audit V-M5..M8)`

### Task V2.2: SPIR-V loader hardening (V-M7)

**Files:** Modify: `libraries/CashSystem/src/shader_module_cacher.cpp` (the `.spv` file loader, audited at `:278`).

**Steps:**
- [ ] Failing test: a 7-byte file and a file not starting with SPIR-V magic both load as failure, not a shader module.
- [ ] Implement: reject `fileSize == 0 || fileSize % 4 != 0`; check `file.read` succeeded; `memcpy` exactly `spirvCode.size() * sizeof(uint32_t)` bytes; verify `spirvCode[0] == 0x07230203u` before handing to `vkCreateShaderModule`.
- [ ] Tests green; commit: `fix(cash): SPIR-V loader validates size/magic; no 3-byte heap overflow (audit V-M7)`

### Task V2.3: Pipeline-cache blob header validation (V-M8 UUID half)

**Files:** Modify: `libraries/CashSystem/src/pipeline_cacher.cpp` (before `vkCreatePipelineCache` with `pInitialData`).

**Steps:**
- [ ] Implement a `static bool PipelineCacheBlobMatchesDevice(const std::vector<uint8_t>&, const VkPhysicalDeviceProperties&)`: parse `VkPipelineCacheHeaderVersionOne` (size ≥ 32, `headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE`, `vendorID`/`deviceID` match, `pipelineCacheUUID` memcmp). On mismatch: log at INFO ("pipeline cache is for another device — regenerating") and create an empty cache.
- [ ] Unit test with a hand-built 32-byte header (right and wrong UUID).
- [ ] Commit: `fix(cash): validate pipeline-cache blob header/UUID before trusting it (audit V-M8)`

## Milestone V3 — Thread safety (tier: high; engine repo)

### Task V3.1: Lock the cacher bodies fired via std::async (V-M9)
- [ ] In `pipeline_cacher.cpp`: take the existing member lock (`m_lock` — confirm its type; if `std::shared_mutex`, unique-lock) for the FULL bodies of `SerializeToFile`, `DeserializeFromFile`, `Cleanup`; make `m_globalCache` access consistently guarded. The base `TypedCacher` discipline is the model — the overrides bypass it today.
- [ ] Grep the other cachers for the same bypass pattern; fix identically.
- [ ] Build + tests; commit: `fix(cash): cacher serialize/deserialize/cleanup bodies hold the lock (audit V-M9)`

### Task V3.2: Guard the deferred-destruction queue (V-M10)
- [ ] `libraries/ResourceManagement/include/Lifetime/DeferredDestruction.h`: add a `std::mutex` to the queue; lock in `AddGeneric`/`PushInternal` AND in `ProcessFrame`/`Flush` (short critical sections — pop into a local batch under the lock, run destructors outside it so a destructor can't deadlock re-entering the queue).
- [ ] Failing test if the RM test target supports threads: N producer threads Release-ing SharedResourcePtrs while a consumer ProcessFrames — run under TSAN if available, else assert count-in == count-destroyed.
- [ ] Commit: `fix(rm): deferred-destruction queue is mutex-guarded; refcounts may hit zero on any thread (audit V-M10)`

### Task V3.3: Serialize queue submits under the parallel executor (V-M11)
- [ ] Add to the device/queue wrapper (find where `VkQueue` handles are owned — likely `VulkanDevice`): `std::mutex& SubmitMutex(VkQueue)` (one mutex per queue, created with the queue).
- [ ] In `ComputeDispatchNode.cpp` and `TraceRaysNode.cpp`, wrap each `vkQueueSubmit*` in `std::scoped_lock lk(device->SubmitMutex(queue));`. Grep for ALL `vkQueueSubmit` call sites (`grep -rn "vkQueueSubmit" libraries application`) and wrap every one — a half-guarded queue is unguarded.
- [ ] Commit: `fix(rg): per-queue submit mutex — vkQueueSubmit is externally synchronized (audit V-M11)`

### Task V3.4: Small races (V-M12, V-M13, V-N10; quota V-M15 only if V0.2 found it still live)
- [ ] `TBBVirtualTaskExecutor.cpp`: `stats_.failedTasks` (and any sibling counters mutated in the task lambda) → `std::atomic<size_t>` with `fetch_add`.
- [ ] `DirectAllocator.cpp` `CreateAliased*`: copy `memory`/`size` into locals INSIDE the locked region (mirror VMAAllocator's pattern); also make `Free*` treat handle-not-in-map as invalid-free early-return (V-N14).
- [ ] `RenderGraph.cpp` `RegisterExternalCleanup`: function-local static counter → `std::atomic<uint32_t>` member.
- [ ] Build + full ctest; commit: `fix(rm/rg): atomic stats counter, copy-under-lock aliasing, atomic cleanup ids (audit V-M12/M13/N10/N14)`

## Milestone V4 — Uploader error handling (tier: medium; engine repo)

### Task V4.1: BatchedUploader checks its sync primitives (V-M16, V-M17, V-N16)
- [ ] Failing tests (the RM test target already covers BatchedUploader — extend it): a submit forced to fail (null queue seam or mock — read how the tests fake devices; if they can't, test the pure logic by extracting the decision) must mark the batch's uploads Failed and release their staging; a batch must never be enqueued with `fence == VK_NULL_HANDLE`.
- [ ] Implement in `SubmitBatch`: capture `vkCreateFence`/`vkQueueSubmit` results; on failure → mark Failed, release staging immediately, do not enqueue. Replace the `assert(cmdBuffer != VK_NULL_HANDLE)` with a real branch doing the same. In `Upload`'s non-mapped fallback: map+memcpy unconditionally; if mapping is impossible, release and return Invalid.
- [ ] Prune `uploadStatus_` on observation of terminal states (V-N15): erase Completed/Failed entries in the status query, or cap with a small ring.
- [ ] Commit: `fix(rm): BatchedUploader fails loud — no unsignalable fences, no null cmd buffers, no unwritten staging (audit V-M16/M17/N15/N16)`

### Task V4.2: TraceRaysNode matches ComputeDispatchNode's failure strategy (V-N8, V-N9)
- [ ] In `TraceRaysNode.cpp`: throw on begin/end/submit failure exactly as `ComputeDispatchNode` does (same exception type/message shape); route `VK_ERROR_DEVICE_LOST` to `NotifyDeviceLost`. Check `vkResetFences` result in BOTH nodes.
- [ ] Commit: `fix(rg): RTX path reports device loss like the compute path (audit V-N8/N9)`

## Milestone V5 — Dead code & clarity (tier: low; engine repo; all subject to V0.2 re-verify)

### Task V5.1: Deletions (V-M18, V-M23, V-N4, V-N2)
- [ ] Delete `RenderGraph::Execute(VkCommandBuffer)` + its declaration; update the two stale `LoopManager.h` doc-comments to reference `RenderFrame`.
- [ ] `BuildRenderGraph.cpp`: delete the two `/* DISABLED FOR COMPUTE TEST */` blocks, the 5 commented camera presets, the dead loop-propagation stub, and with them the `earthmap.jpg` personal path. (Verify none is referenced by an env-var demo branch first: `grep -n "getenv" application/main/source/graph/BuildRenderGraph.cpp`.)
- [ ] Delete `DescriptorSetNodeConfig_OLD.h` (both copies), `VoxelComponents_OLD.h`, and move `GaiaVoxelWorld_ITERATOR_EXAMPLE.cpp` out of the library (delete; the iterator usage lives in the header docs). Grep each name first to prove nothing includes them.
- [ ] Delete the empty `GeometryRender` placeholder validation block in `RenderGraph.cpp` Validate().
- [ ] Full build + ctest; commit: `chore(rg/app): delete dead render loop, commented-out pipeline blocks, _OLD headers, placeholder validation (audit V-M18/M23/N2/N4)`

### Task V5.2: Truthful stubs + shared barrier helper (V-M21, V-M19)
- [ ] `DescriptorSetNode.cpp`: delete the `(MVP STUB)` banner + the ctor's stub comment; keep "not implemented" only on the three genuinely-unimplemented `UpdateBinding`/`UpdateDescriptorSet` methods (or delete those methods if `grep -rn` proves no caller — prefer deletion). Sweep the other `MVP STUB` sites found by `grep -rn "MVP STUB\|MVP:" libraries application` and fix each the same way: the comment matches reality or it goes.
- [ ] Extract `Barriers::ImageToGeneral(...)`/`Barriers::ImageToPresent(...)` into a small shared header under `libraries/RenderGraph/include/Nodes/Common/`; replace the four duplicated helpers in `ComputeDispatchNode.cpp`/`ComputeStageNode.cpp`.
- [ ] Commit: `refactor(rg): stub comments tell the truth; one shared swapchain barrier helper (audit V-M21/M19)`

### Task V5.3: Constants + epsilon parity check (V-M24, V-N5) — SKIP couts if V0.2 confirmed fixed
- [ ] Replace the bare `22` in `VoxelSceneCacher.cpp` (`esvoMaxScale` assignment) with the named `ESVO_MAX_SCALE` constant; consolidate the three header definitions to one canonical `constexpr` in `LaineKarrasOctree.h` (the one with the explanatory comment) and reference it from the other two.
- [ ] `SVOTraversal.cpp`: name the GPU-mirror epsilon `boundary_epsilon` to match the CPU path, then INVESTIGATE the value divergence (1e-4 vs 1e-2): run the existing CPU/GPU parity test target (`test_stored_sdf_march_mirror`, `test_recipe_eval_parity`) with both values; if 0.01 passes for both paths, unify; if not, keep both values and write the one-line comment explaining why they differ. Do not guess — the parity tests decide.
- [ ] Commit: `fix(svo): single ESVO_MAX_SCALE source; boundary epsilons named and parity-tested (audit V-M24/N5)`

### Task V5.4: shader_tool name + README fixes (V-N18, V-N19, V-N17)
- [ ] Pick `sdi_tool` (what `--help` already says) and align the file-header comment examples in `shader_tool.cpp`; or if CMake names the binary `shader_tool`, align PrintUsage instead — the CMake target name wins. Check: `grep -rn "shader_tool\|sdi_tool" libraries/ShaderManagement/CMakeLists.txt`.
- [ ] README: drop the hardcoded `1.4.321.1` SDK version (reference the provisioned dir); add the one-line WSL software-Vulkan warning pointing at `ProvisionWslVulkan.cmake`/FR-20 and recommending native Windows for perf work.
- [ ] Commit: `docs(tooling): one name for the shader tool; README warns about WSL software Vulkan (audit V-N17/N18/N19)`

## Milestone V6 — Logger error visibility (tier: medium; engine repo)

### Task V6.1: Errors reach the terminal by default (V-M26)
- [ ] Failing test in the logger test target: a fresh `Logger("x")` with defaults, `LOG_ERROR(...)` → the message reaches the terminal sink (capture stdout/stderr in the test).
- [ ] Implement the minimal rule in `Logger::Log`: if `level >= Error`, bypass the per-instance `enabled`/`terminalOutput` gates (still honor `SetGlobalMinLevel`). Add `Logger::SetGlobalTerminalOutput(bool)` for consumers that want everything.
- [ ] Grep for tests/tools that assert on clean stdout and now see error lines — fix expectations, don't weaken the rule.
- [ ] Commit: `fix(logger): LOG_ERROR/CRITICAL are terminal-visible by default (audit V-M26)`

---

## Deferred (tracked in the audit, NOT this plan)
- V-M1 SDF glue-table generation (cross-repo Yeroket emitter work — its own epic; pairs with the Yeroket plan's follow-ons) · V-M25 generated `recipe_ids.h` (same seam) · V-M27/FR-9 graph-validation pass (design work — retires the recurring silent-mismatch class; schedule as the next VIXEN workstream) · V-M20 `CashSystem`→`CacheSystem` rename (mechanical but ~20-file blast radius; do as a dedicated standalone commit AFTER this plan merges, so it doesn't pollute these diffs) · V-M2/M3 god-class splits (ratchet epics) · V-M4 capability interfaces (convert per-site as touched) · V-N1/N3/N11/N12/N13 + Info items (opportunistic).

## Self-review notes
- Immediate table → V1.1/V1.2 (C1/C2/C3), V4.1 (M16/M17), V3.4-conditional (M15), V0.2-strike (M22). Next table → V2.1-2.3 (M5-M8), V3.1-3.4 (M9-M13), deferred-with-reason (M1, M27), V5.1 (M18/M23/N4), V5.3 (N5, M24), V5.2 (M21/M19), V5.4 (N17-N19), V6.1 (M26). V-N20 (missing skill file) is a local-machine config issue, not repo work — flag to the user, no task. Gaps: none unexplained.
- Order dependency: V0 gates all. V1 is in the undertow repo and independent of V0 (the consumer host didn't move) — V1 may run in parallel with V0.

## Milestone Map (confirmed 2026-07-07)
- Milestone V0 = V0.1-V0.2 (GATE) · V1 = V1.1-V1.2 (undertow repo) · V2 = V2.1-V2.3 · V3 = V3.1-V3.4 · V4 = V4.1-V4.2 · V5 = V5.1-V5.4 · V6 = V6.1
- Runs THIRD. User decision recorded: V0.1 merges origin/main INTO claude/wsl-build-portability (keep WSL portability work).
- WSL note (memory 2026-07-07): Vulkan VISUAL render is Windows-only from this box; compile + unit tests (ctest) are WSL-fine — all tasks in this plan are CPU-testable.

## Progress Log
