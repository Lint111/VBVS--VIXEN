# Auto-Sync FrameGraph — Implementation Plan (Phase P1: Declarative Foundation)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the declarative sync-semantics foundation — an `AccessKind` type that maps each
resource access to concrete Vulkan `{stage, access, layout}`, and complete the `ResourceAccessTracker`
so it records read/write *and* `AccessKind` per access — with zero behavior change to the running graph.

**Architecture:** Pure, CPU-testable additions. `AccessKind` + `ResolveAccess()` is the single source of
truth for sync semantics, consumed later by the `FrameSyncScheduler` (P2). `ResourceAccessTracker` gains
the data the scheduler needs (correct `ReadWrite` tracking via the long-standing `:92` TODO, plus an
`AccessKind` per access). No GPU, no submit changes — this is the substrate everything else builds on.

**Tech Stack:** C++23, Vulkan 1.3 (`synchronization2` / `VkPipelineStageFlags2`/`VkAccessFlags2`),
GoogleTest, CMake + Ninja preset (`vixen-ninja`).

**Spec:** [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]] (Tiers 1+2). **Branch:** `feat/auto-sync-framegraph`.

> Plan saved to the vault (`01-Architecture/`) per the project's obsidian-first convention rather than
> the writing-plans default `docs/superpowers/plans/` — matches the design spec's location and the
> `RenderTarget-Implementation-Plan-2026-06` precedent.

---

## Plan series (P1–P6)

This document is **Plan 1 of 6**. Each phase is an independently build-green, testable milestone; each
later plan is authored against the *landed* interfaces of the prior one (the GPU phases must be written
against P2's concrete schedule types and validated under syncval, so writing their code now would be
speculation).

| Plan | Phase | Scope | Gate |
|---|---|---|---|
| **1 (this doc)** | **P1** | `AccessKind` + mapping; complete `ResourceAccessTracker` (+`VirtualResourceAccessTracker`) | build green, tests pass, no behavior change |
| 2 | P2 | `FrameSyncScheduler` (pure) → structured `FrameSyncSchedule` (intra barriers + inter-group edges), fully unit-tested | scheduler tests pass |
| 3 | P3 | Tier-1 leaf replay: `ComputeDispatchNode` consumes baked barriers; drop hand-rolled transitions; standardize `barrier2` | live voxel, 0 syncval |
| 4 | P4 | Generalize `MultiDispatchNode` (trailing render pass; reusable pass-recording core) | demo, visual + 0 syncval |
| 5 | P5 | Tier-2 timeline: `FrameSyncNode` timeline + `frameBase`; inter-group edges; migrate composite off `leaveImageInGeneral` | live composite, 0 syncval |
| 6 | P6 | Fan-in multi-submit demo (2 producers → 1 consumer → present) | visual + 0 syncval |

---

## Milestone Map

Segmentation for the post-brainstorm-context-manager pipeline (confirmed 2026-06-21). A resume reuses
this grouping verbatim — do not re-segment.

- [x] **Milestone 1 — Task 1:** `AccessKind` + sync-semantics mapping + tests. Implementer: **Haiku**. ✅
- [ ] **Milestone 2 — Tasks 2–3:** Complete `ResourceAccessTracker` + `VirtualResourceAccessTracker`
  (ReadWrite + `AccessKind`). Implementer: **Sonnet**.

Opus validates each milestone; the controller runs the `vixen-ninja` build gate between milestones.

### Progress Log
- **Milestone 1 (Task 1): DONE** · `AccessKind` + `ResolveAccess` mapping + `test_barrier_types` (5/5 pass) · commit `5c243d82` (Opus validator **APPROVED** — all 12 stage/access/layout mappings verified correct, no `ResourceUsage` collision) + `87e53309` (untracked the generated `VoxelRayMarch_CompressedNames.h` SDI header the implementer over-committed) · `.gitignore` build-tree hygiene kept (`d4a6c3f4`) · full build green · 2026-06-21

---

## File structure (P1)

- **Create** `libraries/RenderGraph/include/Core/BarrierTypes.h` — `AccessKind`, `AccessInfo`,
  `ResolveAccess()`, `AccessReads()/AccessWrites()`. Pure header, depends only on `<vulkan/vulkan.h>`.
- **Create** `libraries/RenderGraph/tests/test_barrier_types.cpp` — mapping + classification tests.
- **Modify** `libraries/RenderGraph/include/Core/ResourceAccessTracker.h` — add `AccessKind kind` to
  `ResourceAccess`; the recording signature carries it.
- **Modify** `libraries/RenderGraph/src/Core/ResourceAccessTracker.cpp` — complete the `:92` TODO
  (read input-slot `SlotMutability` → `ReadWrite`), populate `AccessKind`.
- **Modify** `libraries/RenderGraph/src/Core/VirtualResourceAccessTracker.cpp` — same completion for the
  `:119` TODO (keep the two trackers consistent).
- **Modify** (only if needed — see Task 2 Step 1) `libraries/RenderGraph/include/Data/Core/CompileTimeResourceSystem.h`
  (`ResourceDescriptor`) + the config `GetInputVector()` population — to expose `mutability` at runtime.
- **Modify** `libraries/RenderGraph/tests/CMakeLists.txt` — register `test_barrier_types`.
- **Modify** `libraries/RenderGraph/tests/test_resource_access_tracker.cpp` — add `ReadWrite` + `AccessKind` cases.

**Build:** `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
**Run a test:** `cmd.exe /c "build-ninja\libraries\RenderGraph\tests\<exe>.exe --gtest_brief=1"` (from repo root).
**Gotcha (memory):** WSL bash does not pass env vars to Windows `.exe`; the build/test are Windows binaries — always go through `cmd.exe /c`.

---

## Task 1: `AccessKind` + sync-semantics mapping

**Files:**
- Create: `libraries/RenderGraph/include/Core/BarrierTypes.h`
- Test: `libraries/RenderGraph/tests/test_barrier_types.cpp`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt`

> **Naming note:** A `ResourceUsage` enum already exists (`Data/Core/ResourceTypes.h:24`) for
> *creation-time* usage flags. Do **not** reuse it. The new type is `AccessKind` (per-pass sync
> semantics) — a distinct concept.

- [ ] **Step 1: Write the failing test**

Create `libraries/RenderGraph/tests/test_barrier_types.cpp`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/BarrierTypes.h"

using namespace Vixen::RenderGraph;

TEST(BarrierTypes, ComputeStorageWriteMapsToGeneral) {
    const AccessInfo i = ResolveAccess(AccessKind::ComputeStorageWrite);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_GENERAL);
}

TEST(BarrierTypes, FragmentSampledReadMapsToReadOnlyOptimal) {
    const AccessInfo i = ResolveAccess(AccessKind::FragmentSampledRead);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST(BarrierTypes, ColorAttachmentWriteMapsToColorOptimal) {
    const AccessInfo i = ResolveAccess(AccessKind::ColorAttachmentWrite);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(BarrierTypes, PresentSrcHasNoAccessAndPresentLayout) {
    const AccessInfo i = ResolveAccess(AccessKind::PresentSrc);
    EXPECT_EQ(i.access, VK_ACCESS_2_NONE);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

TEST(BarrierTypes, ReadWriteClassification) {
    EXPECT_TRUE (AccessWrites(AccessKind::ComputeStorageWrite));
    EXPECT_FALSE(AccessWrites(AccessKind::FragmentSampledRead));
    EXPECT_TRUE (AccessReads (AccessKind::ComputeStorageReadWrite));
    EXPECT_TRUE (AccessWrites(AccessKind::ComputeStorageReadWrite));
    EXPECT_FALSE(AccessReads (AccessKind::ColorAttachmentWrite)); // write-only attachment
}
```

- [ ] **Step 2: Register the test target** (mirror the `test_node_self_registration` block at `tests/CMakeLists.txt:612-618`)

Add to `libraries/RenderGraph/tests/CMakeLists.txt`:

```cmake
add_executable(test_barrier_types test_barrier_types.cpp)
target_link_libraries(test_barrier_types PRIVATE GTest::gtest_main RenderGraph)
set_target_properties(test_barrier_types PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_barrier_types)
message(STATUS "✓ test_barrier_types configured (auto-sync P1)")
```

- [ ] **Step 3: Build and verify it FAILS to compile** (header doesn't exist yet)

Run: `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
Expected: FAIL — `Core/BarrierTypes.h: No such file or directory`.

- [ ] **Step 4: Write the implementation**

Create `libraries/RenderGraph/include/Core/BarrierTypes.h`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Vixen::RenderGraph {

/// Concrete Vulkan sync semantics of one resource access. `layout` is ignored for buffers.
struct AccessInfo {
    VkPipelineStageFlags2 stage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        access = VK_ACCESS_2_NONE;
    VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// Declarative per-pass access kind. Distinct from creation-time ResourceUsage flags
/// (ResourceTypes.h): this describes how a *pass* touches a resource, for sync.
enum class AccessKind : uint8_t {
    None = 0,
    ComputeStorageRead,
    ComputeStorageWrite,
    ComputeStorageReadWrite,
    ComputeSampledRead,
    FragmentSampledRead,
    VertexStorageRead,
    ColorAttachmentWrite,
    DepthAttachmentReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite,
    PresentSrc,
};

/// Single source of truth: AccessKind -> {stage, access, layout}.
[[nodiscard]] constexpr AccessInfo ResolveAccess(AccessKind kind) {
    switch (kind) {
    case AccessKind::ComputeStorageRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeStorageWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeStorageReadWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case AccessKind::ComputeSampledRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case AccessKind::FragmentSampledRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case AccessKind::VertexStorageRead:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case AccessKind::ColorAttachmentWrite:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case AccessKind::DepthAttachmentReadWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case AccessKind::IndirectRead:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case AccessKind::TransferRead:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case AccessKind::TransferWrite:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case AccessKind::PresentSrc:
        return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    case AccessKind::None:
    default:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
}

[[nodiscard]] constexpr bool AccessWrites(AccessKind k) {
    switch (k) {
    case AccessKind::ComputeStorageWrite:
    case AccessKind::ComputeStorageReadWrite:
    case AccessKind::ColorAttachmentWrite:
    case AccessKind::DepthAttachmentReadWrite:
    case AccessKind::TransferWrite:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool AccessReads(AccessKind k) {
    switch (k) {
    case AccessKind::ComputeStorageRead:
    case AccessKind::ComputeStorageReadWrite:
    case AccessKind::ComputeSampledRead:
    case AccessKind::FragmentSampledRead:
    case AccessKind::VertexStorageRead:
    case AccessKind::DepthAttachmentReadWrite:
    case AccessKind::IndirectRead:
    case AccessKind::TransferRead:
        return true;
    default:
        return false;
    }
}

} // namespace Vixen::RenderGraph
```

> Note: `VK_PIPELINE_STAGE_2_*` / `VK_ACCESS_2_*` are core in Vulkan 1.3 (the repo targets 1.3 and
> already uses `vkCmdPipelineBarrier2` in `MultiDispatchNode`), so no extension guard is needed.

- [ ] **Step 5: Build and run the test — verify PASS**

Run: `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
Then: `cmd.exe /c "build-ninja\libraries\RenderGraph\tests\test_barrier_types.exe --gtest_brief=1"`
Expected: PASS, 5 tests.

- [ ] **Step 6: Commit**

```bash
git add libraries/RenderGraph/include/Core/BarrierTypes.h \
        libraries/RenderGraph/tests/test_barrier_types.cpp \
        libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "feat(rendergraph): AccessKind sync-semantics mapping (auto-sync P1)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Complete `ResourceAccessTracker` (ReadWrite + AccessKind)

**Files:**
- Modify: `libraries/RenderGraph/include/Core/ResourceAccessTracker.h` (`ResourceAccess` struct, `RecordAccess` signature)
- Modify: `libraries/RenderGraph/src/Core/ResourceAccessTracker.cpp` (`AddNode` — the `:92` TODO)
- Test: `libraries/RenderGraph/tests/test_resource_access_tracker.cpp` (existing target, `CMakeLists.txt:406`)
- Possibly modify: `libraries/RenderGraph/include/Data/Core/CompileTimeResourceSystem.h` (`ResourceDescriptor`) — see Step 1.

- [ ] **Step 1: Confirm the runtime mutability source (verify-then-implement)**

The tracker's `AddNode` has `node` and slot indices. The runtime slot descriptor is
`node->GetNodeType()->GetInputDescriptor(slotIndex)` (returns `const ResourceDescriptor*`;
`Schema = std::vector<ResourceDescriptor>`, `NodeType.h:18,61`).

Open `include/Data/Core/CompileTimeResourceSystem.h` (`struct ResourceDescriptor`, ~line 1295) and check
for a `SlotMutability mutability` field.
- **If present:** use `desc->mutability` directly in Step 3 — no descriptor change needed.
- **If absent:** add `SlotMutability mutability = SlotMutability::ReadOnly;` to `ResourceDescriptor`, and
  populate it where the config builds the schema (`GetInputVector()`/`GetOutputVector()`, see
  `TypedNodeInstance.h:955-956` and `ResourceConfig.h` slot factories — copy `SlotType::mutability`,
  exactly as `SlotInfo::FromSlot` does at `SlotInfo.h:159`). Acceptance: a node declared with a
  `SlotMutability::ReadWrite` input exposes `GetInputDescriptor(i)->mutability == ReadWrite`.

Add `#include "ResourceConfig.h"` for `SlotMutability` if not already transitively included.

- [ ] **Step 2: Write the failing test**

Add to `libraries/RenderGraph/tests/test_resource_access_tracker.cpp` (uses the existing harness in that
file — reuse its node/topology builders; the snippet below names the helper `MakeNodeWithInput` as a
placeholder for whatever builder the file already defines — match the existing fixture):

```cpp
// A ReadWrite input must be tracked as a writer (not just a reader), so WAR/WAW
// hazards are detectable. Before the :92 fix it was recorded Read-only.
TEST(ResourceAccessTrackerTest, ReadWriteInputCountsAsWriter) {
    // Build a node whose input slot is declared SlotMutability::ReadWrite and is
    // connected to `resource`. (Use the fixture's existing topology builder.)
    auto [topology, node, resource] = BuildReadWriteInputTopology(); // existing-style helper

    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);

    const ResourceAccessInfo* info = tracker.GetAccessInfo(resource);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(tracker.IsWriter(node));                  // was false pre-fix
    EXPECT_EQ(info->accesses.size(), 1u);
    EXPECT_EQ(info->accesses[0].accessType, ResourceAccessType::ReadWrite);
}
```

If the fixture has no ReadWrite-input builder, add one mirroring the file's existing builders; if that is
non-trivial, assert at the unit level instead by calling the (to-be-added) helper from Step 3 directly.

- [ ] **Step 3: Build, run — verify the new test FAILS**

Run: `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
Then: `cmd.exe /c "build-ninja\libraries\RenderGraph\tests\test_resource_access_tracker.exe --gtest_filter=*ReadWriteInputCountsAsWriter* --gtest_brief=1"`
Expected: FAIL — `accessType` is `Read`, `IsWriter` is false (inputs are recorded Read-only today).

- [ ] **Step 4: Add `AccessKind` to the access record**

In `include/Core/ResourceAccessTracker.h`, add `#include "Core/BarrierTypes.h"` and extend the struct:

```cpp
struct ResourceAccess {
    NodeInstance* node = nullptr;
    ResourceAccessType accessType = ResourceAccessType::Read;
    uint32_t slotIndex = 0;
    bool isOutput = false;
    AccessKind kind = AccessKind::None;   // NEW: declarative sync semantics (P1)
};
```

Add `AccessKind kind` as a trailing parameter (defaulted) to the private `RecordAccess` declaration:

```cpp
void RecordAccess(Resource* resource, NodeInstance* node,
                  ResourceAccessType accessType, uint32_t slotIndex, bool isOutput,
                  AccessKind kind = AccessKind::None);
```

- [ ] **Step 5: Complete the `:92` TODO in `AddNode`**

In `src/Core/ResourceAccessTracker.cpp`, replace the input loop (currently records every input as
`Read`) so it consults the slot's mutability via the descriptor confirmed in Step 1:

```cpp
// Inputs: Read by default; ReadWrite if the slot declares it (completes the :92 TODO).
NodeType* type = node->GetType();
for (size_t slotIndex = 0; slotIndex < bundle.inputs.size(); ++slotIndex) {
    Resource* resource = bundle.inputs[slotIndex];
    if (!resource) continue;

    ResourceAccessType access = ResourceAccessType::Read;
    if (type) {
        const ResourceDescriptor* desc = type->GetInputDescriptor(static_cast<uint32_t>(slotIndex));
        if (desc && desc->mutability == SlotMutability::ReadWrite) {
            access = ResourceAccessType::ReadWrite;
        }
    }
    RecordAccess(resource, node, access, static_cast<uint32_t>(slotIndex), /*isOutput=*/false);
}
```

Add includes as needed: `"Core/NodeType.h"`, `"Data/Core/ResourceConfig.h"` (for `SlotMutability`),
and the `ResourceDescriptor` definition header. Update `RecordAccess`'s definition signature to match
Step 4 (it already stores into `ResourceAccess`; set `.kind = kind`).

- [ ] **Step 6: Build, run — verify PASS** (and the existing tracker suite still passes)

Run: `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
Then: `cmd.exe /c "build-ninja\libraries\RenderGraph\tests\test_resource_access_tracker.exe --gtest_brief=1"`
Expected: PASS — the new test plus all pre-existing tracker tests.

- [ ] **Step 7: Commit**

```bash
git add libraries/RenderGraph/include/Core/ResourceAccessTracker.h \
        libraries/RenderGraph/src/Core/ResourceAccessTracker.cpp \
        libraries/RenderGraph/tests/test_resource_access_tracker.cpp
# include CompileTimeResourceSystem.h / config files too if Step 1 modified them
git commit -m "feat(rendergraph): track ReadWrite inputs + AccessKind in ResourceAccessTracker (auto-sync P1)

Completes the long-standing SlotMutability TODO so WAR/WAW hazards are detectable.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Mirror the completion in `VirtualResourceAccessTracker`

The virtual tracker carries the identical `:119` TODO and must stay consistent (it tracks the *virtual*
resource graph used for transient aliasing / pre-execute analysis).

**Files:**
- Modify: `libraries/RenderGraph/src/Core/VirtualResourceAccessTracker.cpp` (the `:119` TODO)
- Modify (if it has its own access struct): the corresponding header
- Test: extend its existing test if one exists; otherwise assert via the shared tracker test fixture

- [ ] **Step 1: Inspect** `VirtualResourceAccessTracker.cpp` around `:119` and its header. Confirm whether
  it reuses `ResourceAccess`/`ResourceAccessType` from `ResourceAccessTracker.h` (likely) or defines its
  own. This determines whether the `AccessKind`/`ReadWrite` change from Task 2 already applies.

- [ ] **Step 2: Apply the same input-mutability logic** as Task 2 Step 5 at the `:119` site (same code
  shape, reading `GetInputDescriptor(i)->mutability`).

- [ ] **Step 3: Build + run** the virtual-tracker test (find it via
  `grep -n virtual tests/CMakeLists.txt`); if none exists, add a `ReadWrite` case mirroring Task 2 Step 2.

Run: `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat`
Expected: build green; tracker tests pass.

- [ ] **Step 4: Commit**

```bash
git add libraries/RenderGraph/src/Core/VirtualResourceAccessTracker.cpp # + header/test if changed
git commit -m "feat(rendergraph): complete SlotMutability TODO in VirtualResourceAccessTracker (auto-sync P1)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase P1 exit gate

- [ ] Full build green via the `vixen-ninja` preset (whole solution, not just the test runner).
- [ ] `test_barrier_types` passes; `test_resource_access_tracker` passes (new + pre-existing).
- [ ] **No behavior change**: run the app once and confirm it still renders with 0 validation errors —
      `cd VIXEN/binaries && cmd.exe /c VIXEN.exe` (Cornell box; this phase changes no execution path).
- [ ] All three commits on `feat/auto-sync-framegraph`.

When green, P2 (`FrameSyncScheduler`) gets its own plan, authored against the `AccessKind` +
`ResourceAccess.kind` interfaces this phase landed.

---

## Self-review (P1 plan vs. spec)

- **Spec coverage:** P1 implements the spec's components #1 (`ResourceUsage`→here `AccessKind` + mapping)
  and #2 (`ResourceAccessTracker` completion: SlotMutability TODO + carry usage). Components #3–#7
  (scheduler, FrameSyncNode timeline, node consumers, composite migration) are P2–P5 — explicitly scoped
  out, tracked in the series table. No P1-scoped spec requirement is unaddressed.
- **Placeholders:** Task 1 is fully concrete. The two soft spots are honest, bounded decisions, not
  hand-waves: (a) Task 2 Step 1's verify-then-implement for `ResourceDescriptor.mutability` (both
  branches specified with acceptance criteria); (b) the test-fixture helper name in Task 2 Step 2, which
  must match the existing `test_resource_access_tracker.cpp` builders (instructed to mirror siblings).
- **Type consistency:** `AccessKind`, `AccessInfo`, `ResolveAccess`, `AccessReads/Writes` are used
  identically across Task 1 and Task 2; `ResourceAccess.kind` matches the `RecordAccess` parameter.

---

*Created 2026-06-21 by Claude Code (writing-plans). Plan 1 of 6. Spec:
[[Auto-Sync-FrameGraph-Inc1-Design-2026-06]].*
