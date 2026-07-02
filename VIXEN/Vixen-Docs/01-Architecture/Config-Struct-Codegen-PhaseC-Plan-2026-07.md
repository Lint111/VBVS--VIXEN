# Config-Struct Codegen — Phase C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Run via [[post-brainstorm-context-manager]]: both milestones on VIXEN `feat/config-codegen`, Sonnet implementer + Opus validator, controller-run gates (C2's live render gate is authoritative — run it on lavapipe).

**Goal:** retire the hand-written `Vixen::SVO::OctreeConfig` / `Vixen::SVO::ChannelDesc` and the shader's inline `OctreeConfig` so both consume the generated single-source headers from Phase B, with a live render no-regression gate.

**Architecture:** C1 aliases the generated `Vixen::Gpu::{ChannelDesc,OctreeConfig}` into `Vixen::SVO` (via `using`-declarations) and deletes the hand-written definitions — all 18 consumers keep their `Vixen::SVO::…` spellings unchanged. C2 replaces the shader's inline struct with an `#include` of the generated GLSL. No new codegen; no Yeroket changes.

**Tech Stack:** C++23/glm; GLSL (glslang `#include`); CMake + Ninja (`vixen-wsl` preset); lavapipe (offscreen render gate); gtest.

## Global Constraints

- **Scope is exactly one logical struct + its shader mirror.** Migrate ONLY `Vixen::SVO::OctreeConfig` (`libraries/SVO/include/ShellOctreeGpu.h`), `Vixen::SVO::ChannelDesc` (`libraries/SVO/include/VoxelChannelFormat.h`), and `shaders/BodyInstanceRayMarch.comp`. Do NOT touch the separate `CashSystem::OctreeConfig` (`VoxelSceneCacher.h:100`, 256 B std140) or `Vixen::RenderGraph::OctreeConfig` (`VoxelGridNode.cpp:35`) — they share only a name.
- **Proven-clean basis (Phase B).** Generated `Vixen::Gpu::OctreeConfig` is byte-identical to the hand-written `Vixen::SVO::OctreeConfig`; generated GLSL is std430-identical to the shader's inline struct. Do not re-derive — trust B2's parity + the drift-guard.
- Do NOT hand-edit committed `.g.*` artifacts. Do NOT push/merge to origin (controller finishes; user pushes). Work on VIXEN `feat/config-codegen`.
- Build: `cmake --preset vixen-wsl && cmake --build build-wsl --target <t>`; build dir is `/mnt/c/cpp/VBVS--VIXEN/build-wsl` (worktree-local `../build-wsl`). Run lavapipe tests with `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`.
- Clean-commit gate: relevant build/tests green before each commit. Commit trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`
- Live-run gate is authoritative for GPU work: C2 is not done until `test_body_instance_raymarch_render` passes on an actual lavapipe run.

## Milestone Map (post-brainstorm-context-manager)

- [ ] **C1 — C++ consumes the generated struct** · Task 1. Alias `ChannelDesc` (VoxelChannelFormat.h) + `OctreeConfig` (ShellOctreeGpu.h) to the generated types; delete the hand-written definitions + redundant static_asserts; retire the now-tautological B2 parity gtest. Gate: full WSL build + SVO/RenderGraph test suites + the SPIR-V drift-guard (`test_octree_config_sdi_parity`) all green.
- [ ] **C2 — shader consumes the generated struct + live gate** · Task 2. `#include "Generated/OctreeConfig.glsl"` in BodyInstanceRayMarch.comp; drop the inline struct. Gate: `test_body_instance_raymarch_render` live no-regression on lavapipe + `test_octree_config_sdi_parity` green.

### Progress Log
_(controller appends one line per milestone: `- Milestone Cx: DONE · commits <short>..<short> · Opus validator OK · <date>`)_

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `libraries/SVO/include/VoxelChannelFormat.h` | modify | `#include` the generated header; replace hand-written `struct ChannelDesc` with `using Vixen::Gpu::ChannelDesc;` (keep enums/constants) |
| `libraries/SVO/include/ShellOctreeGpu.h` | modify | replace hand-written `struct OctreeConfig {…}` + its static_assert battery (lines 136–205) with `using Vixen::Gpu::OctreeConfig;` |
| `libraries/SVO/tests/test_octreeconfig_codegen_parity.cpp` | delete | tautological after aliasing (SVO≡Gpu); ongoing guards are the generated `.g.h` static_asserts + the SPIR-V drift-guard |
| `libraries/SVO/tests/CMakeLists.txt` | modify | remove the parity-test registration added in B2 |
| `shaders/BodyInstanceRayMarch.comp` | modify | replace inline `struct OctreeConfig {…}` (lines 118–143) with `#include "Generated/OctreeConfig.glsl"` |

---

## Task 1: C++ consumes the generated OctreeConfig/ChannelDesc (C1)

**Files:**
- Modify: `libraries/SVO/include/VoxelChannelFormat.h`, `libraries/SVO/include/ShellOctreeGpu.h`, `libraries/SVO/tests/CMakeLists.txt`
- Delete: `libraries/SVO/tests/test_octreeconfig_codegen_parity.cpp`

**Interfaces:**
- Consumes: generated `Vixen::Gpu::OctreeConfig` + `Vixen::Gpu::ChannelDesc` (`libraries/SVO/include/Generated/OctreeConfig.g.h`, committed in B2).
- Produces: `Vixen::SVO::OctreeConfig` and `Vixen::SVO::ChannelDesc` as aliases of the generated types (all existing consumers keep compiling against these names).

- [ ] **Step 1: Alias `ChannelDesc` in `VoxelChannelFormat.h`.** Add the generated include just after `#include <cstdint>` (line 2), OUTSIDE the namespace:

```cpp
#pragma once
#include <cstdint>
#include "Generated/OctreeConfig.g.h"   // Vixen::Gpu::{ChannelDesc,OctreeConfig} — generated single-source (Phase C)
namespace Vixen::SVO {
```

Then replace the hand-written struct (line 18) `struct ChannelDesc { uint32_t semanticId, elemCount, channelBaseFloats, fieldKind; };  // = 1 uvec4` with the alias (still inside `namespace Vixen::SVO`):

```cpp
using Vixen::Gpu::ChannelDesc;   // was a hand-written struct; now the generated single-source type (Phase C)
```

Keep the `SemanticId`/`FieldKind` enums, `SemanticElemCount()`, `kVoxelsPerBrick`, `kMaxChannels`.

- [ ] **Step 2: Verify the ChannelDesc alias compiles.** The generated header is self-contained (`<cstdint>`/`<cstddef>`/`<glm/glm.hpp>`), so no include cycle. Quick check:

Run: `cmake --build /mnt/c/cpp/VBVS--VIXEN/build-wsl --target Vixen_SVO 2>&1 | tail -20` (or the SVO object target).
Expected: SVO library compiles. If `<glm/glm.hpp>` is not found from this header, confirm the SVO target's include dirs cover glm (they already do — `ShellOctreeGpu.h` uses `glm::mat4`).

- [ ] **Step 3: Alias `OctreeConfig` in `ShellOctreeGpu.h`.** `ShellOctreeGpu.h:76` already `#include "VoxelChannelFormat.h"` (which now pulls the generated header). Replace the hand-written struct + its static_assert battery — the block from line 136 `struct OctreeConfig {` through line 205 (the last `static_assert(offsetof(OctreeConfig, channels) == 224, …)`), inclusive, which also contains the redundant `static_assert(sizeof(ChannelDesc) == 16, …)` — with:

```cpp
// OctreeConfig is generated from the canonical [GpuStruct] (Phase C). Its 432 B
// std430 layout + full sizeof/offsetof static_assert battery live in the generated
// header (included transitively via VoxelChannelFormat.h). ChannelDesc is aliased
// there too. All consumers keep the Vixen::SVO::OctreeConfig spelling.
using Vixen::Gpu::OctreeConfig;
```

Leave everything from line 206 onward (the `FORMAT_BINARY`/`STORED_SDF` constants, the descriptor-helper shims, and the ~700 lines of serialization logic) UNCHANGED — they reference `OctreeConfig`/`ChannelDesc` by the (now-aliased) names and set fields by name, which still resolve. (The block boundaries are content markers: start at `struct OctreeConfig {`, end at the final consecutive `static_assert(...OctreeConfig...)` before the blank line + the `Inc2 Stored-SDF descriptor helpers` comment banner.)

- [ ] **Step 4: Retire the tautological parity gtest.** `git rm libraries/SVO/tests/test_octreeconfig_codegen_parity.cpp`. In `libraries/SVO/tests/CMakeLists.txt`, remove the registration block added in B2 (the `add_executable`/`gtest_discover_tests`/`target_link_libraries` lines naming `test_octreeconfig_codegen_parity`). Rationale: after Step 3, `Vixen::SVO::OctreeConfig` IS `Vixen::Gpu::OctreeConfig`, so the test compares a type to itself. The ongoing guards are (a) the generated `.g.h`'s own `static_assert` battery (fires wherever it's compiled) and (b) `test_octree_config_sdi_parity` (the SPIR-V drift-guard, which checks `sizeof/offsetof(Vixen::SVO::OctreeConfig)` — now the alias — against the reflected shader).

- [ ] **Step 5: Full build — verify all 18 consumers compile against the aliases.**

Run: `cmake --preset vixen-wsl && cmake --build /mnt/c/cpp/VBVS--VIXEN/build-wsl 2>&1 | tail -30`
Expected: full build green (no errors in the 15 `ShellOctreeGpu.h` + 3 `VoxelChannelFormat.h` includers — VoxelSceneCacher, BodyOctreeSceneNode, BuildRenderGraph, the SVO/RenderGraph tests, etc.). If a forward-declaration of `OctreeConfig`/`ChannelDesc` surfaces (none found in survey, but re-check on failure), convert it to include the header.

- [ ] **Step 6: Run the C++ test suites + the drift-guard.**

Run:
```
cmake --build /mnt/c/cpp/VBVS--VIXEN/build-wsl --target test_shell_octree_gpu test_channel_format test_gpu_parity test_stored_sdf_march_mirror test_octree_pool test_soa_sdf_serialize test_octree_config_sdi_parity
for t in test_shell_octree_gpu test_channel_format test_gpu_parity test_stored_sdf_march_mirror test_octree_pool test_soa_sdf_serialize; do /mnt/c/cpp/VBVS--VIXEN/build-wsl/libraries/SVO/tests/$t --gtest_brief=1; done
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json /mnt/c/cpp/VBVS--VIXEN/build-wsl/libraries/RenderGraph/tests/Nodes/test_octree_config_sdi_parity --gtest_brief=1
```
Expected: all PASS. The drift-guard proves `sizeof(Vixen::SVO::OctreeConfig)==432` + offsets still hold with the aliased type. (Adjust exe paths if the SVO test binaries live elsewhere — confirm with `find /mnt/c/cpp/VBVS--VIXEN/build-wsl -name 'test_shell_octree_gpu'`.)

- [ ] **Step 7: Commit.**

```bash
cd /mnt/c/cpp/VBVS--VIXEN/VIXEN
git add libraries/SVO/include/VoxelChannelFormat.h libraries/SVO/include/ShellOctreeGpu.h libraries/SVO/tests/CMakeLists.txt
git rm libraries/SVO/tests/test_octreeconfig_codegen_parity.cpp
git commit --no-verify -m "refactor(codegen): C++ consumes generated OctreeConfig/ChannelDesc (Phase C C1)

<trailers>"
```

---

## Task 2: Shader consumes the generated OctreeConfig + live gate (C2)

**Files:**
- Modify: `shaders/BodyInstanceRayMarch.comp`

**Interfaces:**
- Consumes: generated `shaders/Generated/OctreeConfig.glsl` (committed in B2; defines `struct OctreeConfig { … vec3 gridMin; … mat4 localToWorld; … uvec4 channels[8]; … }`).

- [ ] **Step 1: Baseline the live render test (pre-migration).** Confirm it passes BEFORE the change, so a post-change failure is unambiguous.

Run:
```
cmake --build /mnt/c/cpp/VBVS--VIXEN/build-wsl --target test_body_instance_raymarch_render
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json /mnt/c/cpp/VBVS--VIXEN/build-wsl/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render --gtest_brief=1
```
Expected: PASS (baseline).

- [ ] **Step 2: Swap the inline struct for the generated include.** In `shaders/BodyInstanceRayMarch.comp`, replace the inline definition — lines 118–143, `struct OctreeConfig {` through its closing `};` (the block just above `layout(std430, binding = 5) readonly buffer OctreeConfigsSSBO {`) — with:

```glsl
#include "Generated/OctreeConfig.glsl"   // generated single-source struct (Phase C); was an inline copy
```

Leave the `OctreeConfigsSSBO { OctreeConfig configs[]; }` block and every `configs[i].…` access unchanged. (Optionally trim the now-stale byte-layout comment on lines 116–117.)

- [ ] **Step 3: Rebuild + run the live render test (post-migration) — the authoritative gate.**

Run:
```
cmake --build /mnt/c/cpp/VBVS--VIXEN/build-wsl --target test_body_instance_raymarch_render
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json /mnt/c/cpp/VBVS--VIXEN/build-wsl/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render --gtest_brief=1
```
Expected: PASS, identical to the Step-1 baseline (the generated GLSL is std430-identical → no pixel change). If the shader fails to compile with "Generated/OctreeConfig.glsl not found", confirm the shader `#include` resolver's search path includes `shaders/` (it already resolves the 8 sibling `.glsl` includes; the generated file is at `shaders/Generated/OctreeConfig.glsl`, so the relative path `Generated/OctreeConfig.glsl` is correct).

- [ ] **Step 4: Run the SPIR-V drift-guard on the migrated shader.**

Run: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json /mnt/c/cpp/VBVS--VIXEN/build-wsl/libraries/RenderGraph/tests/Nodes/test_octree_config_sdi_parity --gtest_brief=1`
Expected: PASS — now proves the *generated* GLSL compiles to the layout the (aliased-to-generated) C++ struct declares. This is the end-to-end proof: one canonical `[GpuStruct]` → C++ + GLSL that agree at the SPIR-V level.

- [ ] **Step 5: Commit.**

```bash
git add shaders/BodyInstanceRayMarch.comp
git commit --no-verify -m "refactor(codegen): shader consumes generated OctreeConfig.glsl (Phase C C2)

<trailers>"
```

**C2 gate (controller):** `test_body_instance_raymarch_render` PASS on lavapipe (no regression vs baseline) + `test_octree_config_sdi_parity` PASS.

---

## Self-Review

**Spec coverage:** C1 = alias ChannelDesc (Task 1 Step 1) + alias OctreeConfig + delete hand-written (Steps 3) ✓; retire tautological parity gtest (Step 4) ✓; C2 = shader #include-swap (Task 2 Step 2) + live gate (Step 3) + drift-guard (Step 4) ✓. Scope guard (only the SVO struct + shader; legacy structs untouched) enforced by touching only the 5 listed files ✓.

**Placeholder scan:** all edits are concrete old→new with exact line anchors + content markers; test/build commands are exact with expected results. `<trailers>` refers to the Global Constraints trailer block (spelled out verbatim there). No TBD/TODO/"handle edge cases".

**Type consistency:** `Vixen::SVO::OctreeConfig` / `Vixen::SVO::ChannelDesc` remain the consumer-facing names (now `using` aliases of `Vixen::Gpu::…`). The drift-guard (`test_octree_config_sdi_parity`) references `Vixen::SVO::OctreeConfig` — valid post-alias. The retired test (`test_octreeconfig_codegen_parity`) is the only one removed; no task references it afterward. Shader field accesses (`configs[i].gridMin/.channels/.localToWorld/…`) all exist in the generated GLSL.

## Execution Handoff

Run via [[post-brainstorm-context-manager]] on VIXEN `feat/config-codegen`: **C1** (Task 1) Sonnet + Opus validator, controller-run full-build + test-suite + drift-guard gate; **C2** (Task 2) Sonnet + Opus validator, controller-run **live lavapipe render gate** (authoritative) + drift-guard. On completion, Phase C is done and the hand-written OctreeConfig is fully retired — leaving the Cleanup phase (delete the parallel `Vixen.Codegen`) as the last epic step.
