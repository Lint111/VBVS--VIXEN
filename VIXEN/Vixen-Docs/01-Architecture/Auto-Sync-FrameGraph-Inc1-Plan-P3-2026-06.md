# Auto-Sync FrameGraph — Implementation Plan (Phase P3: Tier-1 barrier replay)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the live voxel path *consume* the baked schedule — `ComputeDispatchNode` replays its `entryBarriers` (`vkCmdPipelineBarrier2`) instead of its hand-rolled `UNDEFINED→GENERAL→PRESENT` v1 transitions — driven by real per-slot `AccessKind` declarations, and verified under Vulkan synchronization validation.

**Architecture:** Three steps. (1) A new opt-in slot field `AccessKind` (mirrors the `mutability` plumbing) lets recording nodes declare per-slot usage; the scheduler then bakes *real* barriers (not the provisional fallback). (2) Turn on syncval behind the existing validation gate. (3) `ComputeDispatchNode` looks up its `SubmitGroup` (new `FindGroupForNode` helper), replays its baked `entryBarriers` against the runtime `VkImage` (node-local correlation via the `IRenderTarget` it already holds), and the hand-rolled transitions are deleted. The submit/semaphore chain is untouched (timeline/multi-submit is the later P5).

**Tech Stack:** C++23, Vulkan 1.3 (`vkCmdPipelineBarrier2`, `VkValidationFeaturesEXT`), GoogleTest, Ninja (`vixen-ninja`).

**Spec:** [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]. **Predecessors:** P1+P2 on `main` (merge `6258b93e`). **Branch:** `feat/auto-sync-framegraph-p3` (off `main`).

> Vault location per project convention. Builds run FOREGROUND with `timeout: 600000` (full build exceeds the 120s Bash default — see `~/.claude/friction.md`).

---

## Plan series position

**Plan 3 of 6.** P1 (foundation) + P2 (analysis engine) are merged to `main`. This plan is the **first execution-changing phase**: the schedule, baked since P2, is now replayed on the live voxel path.

### Resolved design decisions (2026-06-21)
1. **`AccessKind` declaration = opt-in macro variant** (user choice). New `SLOT_EXTENDED_FIELDS` field `AccessKind accessKind = AccessKind::None`, exposed via new `INPUT_SLOT_SYNC`/`OUTPUT_SLOT_SYNC` macros; base `INPUT_SLOT`/`OUTPUT_SLOT` and the 100+ existing configs are untouched (default `None` → the existing `ProvisionalKind` fallback). Flows ResourceSlot → SlotInfo → ResourceDescriptor → tracker → scheduler exactly like `mutability`.
2. **Swapchain→`VkImage` correlation = node-local.** `ComputeDispatchNode` matches a `GroupBarrier.resource` against its `SWAPCHAIN_INFO` input slot's resource and resolves the image via the `IRenderTarget::GetImage(imageIndex)` it already calls. No graph-wide swapchain `Resource*` identity; the adapter's `swapchainResource` stays `nullptr` until Tier-2 (P5).
3. **Submit/semaphore chain unchanged.** P3 is Tier-1 (barriers only). Acquire/present binary semaphores + the per-node submit stay exactly as today.
4. **syncval** is its own milestone (instance-creation `VkValidationFeaturesEXT`), gating M3's verification.

### Milestone Map (for the post-brainstorm-context-manager pipeline)
- [x] **Milestone 1 — Task 1:** `AccessKind` slot declaration + scheduler reads it + `FindGroupForNode` + drop dead `nodeToGroup`. CPU-unit-testable. Implementer: **Sonnet**. ✅
- [x] **Milestone 2 — Task 2:** enable synchronization validation behind `VIXEN_VULKAN_VALIDATION`. Implementer: **Sonnet**. ✅
- [ ] **Milestone 3 — Task 3:** `ComputeDispatchNode` replays baked `entryBarriers` (`barrier2`); delete hand-rolled transitions. **Execution-changing → syncval gate is HANDS-ON (user-driven), not pipeline-self-gated.** Implementer: **Sonnet**, then user verification.

### Progress Log
- **Milestone 1 (Task 1): DONE** · per-slot `AccessKind` (`SLOT_EXTENDED_FIELDS` + opt-in `INPUT_SLOT_SYNC`/`OUTPUT_SLOT_SYNC`; only `ComputeDispatchNodeConfig` changed) flowing slot→descriptor→tracker→scheduler; `FindGroupForNode` added; dead `nodeToGroup` removed; `SWAPCHAIN_INFO` declared `ReadWrite`+`ComputeStorageWrite` · commit `1752b1fd` · Opus validator **APPROVED** (plumbing mirrors `mutability`, macros non-breaking, wave conflicts benign) · frame_sync 10/10, tracker 23/23, wave 15/15, node-reg 2/2 · full build green · 2026-06-21 · *nit→M3:* declared-kind-via-`Build` only transitively tested (closed by M3's live path)
- **Milestone 2 (Task 2): DONE** · synchronization validation chained at the real instance site (`VulkanInstance::CreateInstance`) behind `VIXEN_VULKAN_VALIDATION` (`VK_EXT_validation_features` added post-filter; `pNext` preserved; static lifetime) · commit `a500f297` · Opus validator **APPROVED** (airtight gate, default build byte-unchanged, no second instance site) · build 84 targets green · 2026-06-21

---

## File structure (P3)

- **Modify** `include/Data/Core/SlotFields.h` — add `AccessKind accessKind` to `SLOT_EXTENDED_FIELDS`.
- **Modify** `include/Data/Core/ResourceConfig.h` — `ResourceSlot` template gains an `AccessKind` param + constexpr member; new `INPUT_SLOT_SYNC`/`OUTPUT_SLOT_SYNC` macros; `MakeDescriptor` copies `SlotType::accessKind`.
- **Modify** `include/Data/Core/CompileTimeResourceSystem.h` — `ResourceDescriptor` gains `AccessKind accessKind = AccessKind::None` (mirrors `mutability`, added P1).
- **Modify** `src/Core/ResourceAccessTracker.cpp` (+ virtual) — when recording an access, read `GetInputDescriptor(i)->accessKind` / `GetOutputDescriptor(i)->accessKind` into `ResourceAccess.kind` (replacing/ahead of the provisional fallback when declared).
- **Modify** `include/Core/FrameSyncSchedule.h` — add `const SubmitGroup* FindGroupForNode(const NodeInstance*) const` helper.
- **Modify** `src/Core/FrameSyncScheduler.cpp` — keep `ProvisionalKind` only for `None`; remove the dead `nodeToGroup` map (P2 carry-forward nit).
- **Modify** `include/Data/Nodes/ComputeDispatchNodeConfig.h` — declare `SWAPCHAIN_INFO` with `INPUT_SLOT_SYNC(..., AccessKind::ComputeStorageWrite)` + `SlotMutability::ReadWrite`.
- **Modify** `application/main/source/main.cpp` (+ `VulkanGraphApplication` if it owns instance creation) — chain `VkValidationFeaturesEXT` (syncval) + add `VK_EXT_validation_features`.
- **Modify** `src/Nodes/ComputeDispatchNode.cpp` — replay `entryBarriers`; delete `TransitionImageToGeneral`/`TransitionImageToPresent`.
- **Tests:** extend `tests/test_frame_sync_scheduler.cpp` (declared-AccessKind path, `FindGroupForNode`); `tests/test_resource_access_tracker.cpp` (accessKind read).

**Build:** `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat` (FOREGROUND, `timeout: 600000`).
**Verify (M3):** run the app with validation+syncval (see Task 2) and confirm `[Validation] 0 errors` on both voxel and composite paths.

---

## Task 1: `AccessKind` slot declaration + scheduler consumption

**Files:** `SlotFields.h`, `ResourceConfig.h`, `CompileTimeResourceSystem.h`, `ResourceAccessTracker.cpp` (+ `VirtualResourceAccessTracker.cpp`), `FrameSyncSchedule.h`, `FrameSyncScheduler.cpp`, `ComputeDispatchNodeConfig.h`; tests `test_frame_sync_scheduler.cpp`, `test_resource_access_tracker.cpp`.

> **Pattern to mirror:** P1 added `SlotMutability mutability` to the slot system end-to-end. Read how `mutability` flows — `SlotFields.h` (`SLOT_CORE_FIELDS`), `ResourceConfig.h` `ResourceSlot` template member + `MakeDescriptor` copy (~`:573`), `CompileTimeResourceSystem.h` `ResourceDescriptor.mutability`, and the tracker reading `GetInputDescriptor(i)->mutability` — and replicate it for `AccessKind accessKind`. The ONLY differences: it goes in `SLOT_EXTENDED_FIELDS` (not core), and it's surfaced through NEW macro variants so existing configs are untouched.

- [ ] **Step 1: Failing test — declared AccessKind reaches the scheduler.** Append to `tests/test_frame_sync_scheduler.cpp` an adapter test using a node whose input slot is declared (via the new `INPUT_SLOT_SYNC`) with `AccessKind::ComputeStorageWrite` + `SlotMutability::ReadWrite`, connected to a resource also read by a second node; assert the produced `GroupBarrier.src` resolves to `VK_IMAGE_LAYOUT_GENERAL` (i.e. the real declared kind was used, not the provisional `ComputeStorageRead`). (Reuse the `MockNodeType2`/`AddOutput2`/`AddInput2` harness already in the file; you will need a mock node type whose config declares a synced slot — define a tiny config inline or extend the mock to carry a descriptor with `accessKind` set, whichever is simpler given the harness.)

- [ ] **Step 2: Build FOREGROUND (`timeout:600000`) — verify FAIL** (`INPUT_SLOT_SYNC` undefined / accessKind absent).

- [ ] **Step 3: Add the field.** In `include/Data/Core/SlotFields.h`, extend `SLOT_EXTENDED_FIELDS`:
```cpp
#define SLOT_EXTENDED_FIELDS(X) \
    X(SlotFlags,  flags,      SlotFlags::None) \
    X(AccessKind, accessKind, AccessKind::None)
```
Add `#include "Core/BarrierTypes.h"` to `SlotFields.h` (for `AccessKind`). Confirm the X-macro expansions (`SlotInfo`, `ResourceSlot`, `FromSlot` copy) pick it up automatically — that is the point of the X-macro SSOT.

- [ ] **Step 4: Thread it through `ResourceSlot` + macros + descriptor.** In `include/Data/Core/ResourceConfig.h`: add an `AccessKind Kind = AccessKind::None` trailing template parameter to `ResourceSlot` and a `static constexpr AccessKind accessKind = Kind;` member (mirror the `mutability` member). Add NEW macros next to the existing `INPUT_SLOT`/`OUTPUT_SLOT` (keep those intact):
```cpp
// Opt-in sync-declaring slot variants (auto-sync P3). Same as INPUT_SLOT/OUTPUT_SLOT
// plus a trailing AccessKind; existing INPUT_SLOT/OUTPUT_SLOT remain (accessKind = None).
#define INPUT_SLOT_SYNC(NAME, TYPE, IDX, NULLABILITY, ROLE, MUTABILITY, SCOPE, ACCESSKIND) \
    /* identical expansion to INPUT_SLOT, passing ACCESSKIND as the ResourceSlot Kind param */
#define OUTPUT_SLOT_SYNC(NAME, TYPE, IDX, NULLABILITY, MUTABILITY, ACCESSKIND) \
    /* identical expansion to OUTPUT_SLOT, passing ACCESSKIND */
```
Implement each by copying the existing `INPUT_SLOT`/`OUTPUT_SLOT` macro bodies verbatim and adding the `ACCESSKIND` argument in the `ResourceSlot<...>` instantiation's new trailing position. In `MakeDescriptor<SlotType>()` (~`:573`), add `descriptor.accessKind = SlotType::accessKind;` beside the `mutability` copy. In `include/Data/Core/CompileTimeResourceSystem.h`, add `AccessKind accessKind = AccessKind::None;` to `ResourceDescriptor` beside `mutability` (and ensure `BarrierTypes.h` is included).

- [ ] **Step 5: Tracker reads the declared kind.** In `src/Core/ResourceAccessTracker.cpp` `AddNode`, when recording each input/output access, set the access's `kind` from the descriptor: `desc->accessKind` (via `GetInputDescriptor(i)`/`GetOutputDescriptor(i)`). Mirror in `VirtualResourceAccessTracker.cpp`. (Leave the `ResourceAccess.kind` field plumbing from P1 as-is; you're now populating it for real.)

- [ ] **Step 6: Scheduler prefers declared kind.** In `src/Core/FrameSyncScheduler.cpp::Build`, the existing line already uses `a.kind` when `!= None` and falls back to `ProvisionalKind` otherwise — verify it does, and **remove the dead `nodeToGroup` map** (built, never read — P2 validator nit). Add the helper to `include/Core/FrameSyncSchedule.h`:
```cpp
[[nodiscard]] inline const SubmitGroup* FindGroupForNode(
    const FrameSyncSchedule& s, const NodeInstance* node) {
    for (const SubmitGroup& g : s.groups) if (g.node == node) return &g;
    return nullptr;
}
```

- [ ] **Step 7: Declare the live slot.** In `include/Data/Nodes/ComputeDispatchNodeConfig.h`, change the `SWAPCHAIN_INFO` input slot from `INPUT_SLOT(...)` to `INPUT_SLOT_SYNC(..., SlotMutability::ReadWrite, <scope>, AccessKind::ComputeStorageWrite)` (the compute shader storage-writes the swapchain image). Keep all other slots unchanged.

- [ ] **Step 8: Build FOREGROUND — run, verify PASS** (`test_frame_sync_scheduler` incl. the new declared-kind test; `test_resource_access_tracker`; full build green). Quote summaries.

- [ ] **Step 9: Commit** (stage the touched headers/sources/tests explicitly; no `-A`):
```bash
git commit -m "feat(rendergraph): per-slot AccessKind declaration + scheduler consumption (auto-sync P3)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Enable Vulkan synchronization validation

**Files:** `application/main/source/main.cpp` (and `VulkanGraphApplication` / wherever `vkCreateInstance` is called with the validation layer).

- [ ] **Step 1: Locate instance creation.** Find where `VK_LAYER_KHRONOS_validation` is added under `VIXEN_VULKAN_VALIDATION` (grounding: `main.cpp:94–103`) and the `VkInstanceCreateInfo`.

- [ ] **Step 2: Chain syncval.** Behind the same `VIXEN_VULKAN_VALIDATION` gate, add `VK_EXT_validation_features` to the instance extension list and chain:
```cpp
#if VIXEN_VULKAN_VALIDATION
static const VkValidationFeatureEnableEXT kSyncvalEnables[] = {
    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
};
VkValidationFeaturesEXT validationFeatures{};
validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
validationFeatures.enabledValidationFeatureCount = 1;
validationFeatures.pEnabledValidationFeatures = kSyncvalEnables;
validationFeatures.pNext = instanceCreateInfo.pNext; // preserve any existing chain
instanceCreateInfo.pNext = &validationFeatures;
#endif
```
Ensure `VK_EXT_validation_features` is requested only when the gate is on, and that lifetime of `validationFeatures`/`kSyncvalEnables` spans the `vkCreateInstance` call.

- [ ] **Step 3: Build FOREGROUND — verify green.** This is a no-op when `VIXEN_VULKAN_VALIDATION` is off (default), so the normal build/app are unaffected. Confirm the build links.

- [ ] **Step 4: Commit:**
```bash
git commit -m "feat(app): enable Vulkan synchronization validation behind VIXEN_VULKAN_VALIDATION (auto-sync P3)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: `ComputeDispatchNode` replays baked barriers

**Files:** `src/Nodes/ComputeDispatchNode.cpp` (+ its header for the helper signatures).

- [ ] **Step 1: Add a replay helper.** In `ComputeDispatchNode.cpp`, add a method that, given the command buffer + this node's `SubmitGroup` + the runtime `imageIndex` + the `IRenderTarget* swapchainInfo`, records each `GroupBarrier` as a `vkCmdPipelineBarrier2`:
```cpp
void ComputeDispatchNode::ReplayEntryBarriers(
    VkCommandBuffer cmd, const SubmitGroup& group,
    uint32_t imageIndex, Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo) {
    if (group.entryBarriers.empty()) return;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkMemoryBarrier2> memBarriers;
    for (const GroupBarrier& b : group.entryBarriers) {
        if (b.isImage) {
            VkImageMemoryBarrier2 ib{};
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            ib.srcStageMask  = b.src.stage; ib.srcAccessMask = b.src.access; ib.oldLayout = b.src.layout;
            ib.dstStageMask  = b.dst.stage; ib.dstAccessMask = b.dst.access; ib.newLayout = b.dst.layout;
            ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.image = swapchainInfo->GetImage(imageIndex); // node-local correlation (swapchain image)
            ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            imageBarriers.push_back(ib);
        } else {
            VkMemoryBarrier2 mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mb.srcStageMask = b.src.stage; mb.srcAccessMask = b.src.access;
            mb.dstStageMask = b.dst.stage; mb.dstAccessMask = b.dst.access;
            memBarriers.push_back(mb);
        }
    }
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = (uint32_t)imageBarriers.size();
    dep.pImageMemoryBarriers = imageBarriers.data();
    dep.memoryBarrierCount = (uint32_t)memBarriers.size();
    dep.pMemoryBarriers = memBarriers.data();
    vkCmdPipelineBarrier2(cmd, &dep);
}
```
> NOTE: the baked schedule produces a single transition into the consume layout (e.g. UNDEFINED→GENERAL for the dispatch). The frame-end transition to `PRESENT_SRC_KHR` for the voxel-only path is a *release* the current `TransitionImageToPresent` does; until the scheduler also bakes a trailing present barrier (the `PresentSrc` access on the present-side group — a P5 concern with real swapchain identity), **retain a single explicit GENERAL→PRESENT_SRC `barrier2` at end of `RecordComputeCommands` for the `!leaveImageInGeneral` path** (converted to `barrier2`, replacing the v1 `TransitionImageToPresent`). This keeps the voxel-only path correct in P3 while the entry transition comes from the schedule.

- [ ] **Step 2: Wire replay into `RecordComputeCommands`.** Replace the `TransitionImageToGeneral(cmdBuffer, swapchainImage)` call (line ~334) with:
```cpp
const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
if (const SubmitGroup* myGroup = FindGroupForNode(sched, this)) {
    ReplayEntryBarriers(cmdBuffer, *myGroup, imageIndex, swapchainInfo);
} else {
    // Fallback: no schedule entry (e.g. schedule disabled) — keep correctness.
    TransitionImageToGeneralBarrier2(cmdBuffer, swapchainImage); // barrier2 version
}
```
Convert the end-of-frame present transition (`!leaveImageInGeneral` branch, line ~353) to a `barrier2` (`TransitionImageToPresentBarrier2`).

- [ ] **Step 3: Delete the v1 transitions.** Remove `TransitionImageToGeneral` and `TransitionImageToPresent` (the v1 `vkCmdPipelineBarrier` versions) and their declarations once the `barrier2` replay + present-release replace them. Standardize all of this node's barriers on `vkCmdPipelineBarrier2`.

- [ ] **Step 4: Build FOREGROUND — verify green** + unit suites still pass (`test_frame_sync_scheduler`, `test_resource_access_tracker`, `test_node_self_registration`).

- [ ] **Step 5: HANDS-ON syncval verification (USER-DRIVEN — do not self-certify).** Build with validation on, then run both paths and read the validation summary:
  - Build: configure with `VIXEN_VULKAN_VALIDATION=1` (CMake cache var / preset), full build.
  - Voxel path: `cd VIXEN/binaries && cmd.exe /c "set VIXEN_VULKAN_VALIDATION=1&& VIXEN.exe"` — confirm it renders the Cornell box and the log shows **0 validation/syncval errors**; `taskkill` to reap.
  - Composite path (compute→UI): run the composite config likewise; confirm **0 errors**.
  - This is the real Tier-1 gate. If syncval reports a hazard, the baked barrier is wrong — fix the declaration/schedule, do not silence the layer.

- [ ] **Step 6: Commit** (only after syncval is clean):
```bash
git commit -m "feat(rendergraph): ComputeDispatchNode replays baked barrier2 schedule (auto-sync P3 Tier-1)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase P3 exit gate

- [ ] Full `vixen-ninja` build green (FOREGROUND).
- [ ] Unit suites green (`test_frame_sync_scheduler` incl. declared-kind + `FindGroupForNode`; `test_resource_access_tracker`; `test_node_self_registration`).
- [ ] **syncval clean (HANDS-ON):** voxel + composite paths render with **0 synchronization-validation errors** with `VIXEN_VULKAN_VALIDATION=1`. (This is the gate the CPU pipeline cannot self-certify.)
- [ ] Dead `nodeToGroup` map removed; v1 `TransitionImageTo*` deleted; node on `barrier2`.
- [ ] Commits for Tasks 1–3 on `feat/auto-sync-framegraph-p3`.

**Next (P4/P5):** generalize `MultiDispatchNode` for a compute→compute→render chain (P4); Tier-2 timeline semaphores + real swapchain `Resource*` identity + multi-submit composition + migrate the composite path off `leaveImageInGeneral` (P5).

---

## Self-review (P3 plan vs spec)

- **Spec coverage:** implements the spec's P3 "Tier-1 leaf replay" (component #5 `ComputeDispatchNode` consumes baked barriers; standardize `barrier2`) + the declarative `AccessKind` the design names as the node-layer contribution. Tier-2 (timeline/multi-submit, swapchain identity, composite migration) is explicitly deferred to P5, matching the design phasing.
- **Placeholders:** the macro-variant bodies in Task 1 Step 4 are specified as "copy the existing INPUT_SLOT/OUTPUT_SLOT body + add the trailing ACCESSKIND arg" against a proven in-repo pattern (the `mutability` field) with exact anchors — an instruction to replicate concrete existing code, not a vague TODO. All other steps carry complete code.
- **Type consistency:** `AccessKind`/`AccessInfo`/`GroupBarrier`/`SubmitGroup`/`FrameSyncSchedule`/`FindGroupForNode` match the P1/P2 definitions; `ReplayEntryBarriers` consumes `GroupBarrier.{src,dst,isImage,resource}` and `SubmitGroup.entryBarriers` as defined in P2's `FrameSyncSchedule.h`.

---

*Created 2026-06-21 by Claude Code (writing-plans). Plan 3 of 6. Spec: [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]. Predecessors: [[Auto-Sync-FrameGraph-Inc1-Plan-2026-06]] (P1), [[Auto-Sync-FrameGraph-Inc1-Plan-P2-2026-06]] (P2).*
