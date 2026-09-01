---
title: Config-Struct Codegen — Phase C Design (consume the generated OctreeConfig)
status: COMPLETE — Phase C migrated C++ and GLSL to the generated config artifacts (`52d4294c`,
`1bdf8ac4`); the follow-on parallel-tool cleanup also landed (`1b075391`).
date: 2026-07-02
tags: [architecture, codegen, octreeconfig, migration, live-gate, D5]
related:
  - "[[Config-Struct-Codegen-PhaseB-Design-2026-07]]"
  - "[[Config-Struct-Codegen-Pivot-RealKernelCore-Design-2026-07]]"
---

# Config-Struct Codegen — Phase C Design

**Goal:** retire the hand-written `Vixen::SVO::OctreeConfig` / `Vixen::SVO::ChannelDesc` and the shader's inline `OctreeConfig` — both consume the **generated single-source** headers from Phase B — with a live render no-regression gate and the SPIR-V drift-guard proving the migrated shader's compiled layout still matches C++.

**Builds on Phase B (DONE):** the generated `OctreeConfig.g.h` (`Vixen::Gpu::OctreeConfig`, 432 B) is machine-proven byte-identical to the current `Vixen::SVO::OctreeConfig` (parity gtest, 3/3), and the generated `OctreeConfig.glsl` produces the **same std430 layout** as the shader's inline struct. Phase C flips the consumers from hand-written to generated; it adds no new codegen.

## Scope

**In scope — exactly one logical struct + its shader mirror:**
- `Vixen::SVO::OctreeConfig` (`libraries/SVO/include/ShellOctreeGpu.h`) — the 432 B std430 struct the ray-march shader's `binding=5 configs[]` consumes (`BodyOctreeSceneNode.cpp:446` uploads `sizeof(Vixen::SVO::OctreeConfig)`).
- `Vixen::SVO::ChannelDesc` (`libraries/SVO/include/VoxelChannelFormat.h`) — `OctreeConfig.channels[]`'s element, cross-copied between structs.
- `shaders/BodyInstanceRayMarch.comp` — its inline `struct OctreeConfig` (line 118).

**Explicitly OUT of scope (separate structs — untouched):**
- `CashSystem::OctreeConfig` (`VoxelSceneCacher.h:100`) — a **different** 256 B std140 struct (`_padding4[16]` + non-uploaded `worldGridSize`); the legacy VoxelGrid cacher.
- `Vixen::RenderGraph::OctreeConfig` (`VoxelGridNode.cpp:35`) — the legacy voxel-grid node's file-local struct.
- These share only a name; their `_padding2`/`_padding3` fields are their own. Dropping those field NAMES from the generated SVO struct is safe — no code references `Vixen::SVO::OctreeConfig._padding2`.
- **Cleanup** (delete the parallel `Vixen.Codegen` from VIXEN) followed Phase C and is complete in
  `1b075391`; it is no longer the next phase.

## Why both migrations are proven-clean drop-ins

**C++ (byte-identical, from B2):** generated `Vixen::Gpu::OctreeConfig` == `Vixen::SVO::OctreeConfig` field-for-field/offset-for-offset (the only textual diff is layout-neutral pad naming: `_pad0/_pad1` vs `_padding2/_padding3`). The B2 parity gtest is the standing proof.

**GLSL (std430-identical):** the shader's inline struct uses explicit `vec3 gridMin; float _padding2; …`; the generated uses `vec3 gridMin; vec3 gridMax;` — under std430 both place `gridMax@48`, `localToWorld@64`, `channels@224`, size 432 (glslang auto-pads the vec3→16 gaps the explicit floats filled). `_tailPad` differs (`uvec4[5]` vs `uint[20]`) but is 80 B of **unread trailing pad** either way. Every field the shader READS (`gridMin`, `channels[i].x…w`, the mat4s, all scalars) is present with identical name/type/offset → no access site changes. The SPIR-V drift-guard verifies this on the real compiled `.spv`.

## Architecture

### C1 — C++ migration (alias the generated types into `Vixen::SVO`)

Decision (user, 2026-07-02): **alias, no tool change.** The generated `OctreeConfig.g.h` is self-contained (`<cstdint>`/`<cstddef>`/`<glm/glm.hpp>` only), so the include order is acyclic: `OctreeConfig.g.h` ← `VoxelChannelFormat.h` ← `ShellOctreeGpu.h`.

- **`VoxelChannelFormat.h`:** remove the hand-written `struct ChannelDesc { … };`; add `#include "Generated/OctreeConfig.g.h"` and, inside `namespace Vixen::SVO`, `using Vixen::Gpu::ChannelDesc;`. Keep the enums (`SemanticId`, `FieldKind`), constants (`kMaxChannels`, `kVoxelsPerBrick`), and `SemanticElemCount()`. This makes `Vixen::SVO::ChannelDesc` an alias of the single generated type; its 3 direct includers + everyone transitively now share it.
- **`ShellOctreeGpu.h`:** remove the hand-written `struct OctreeConfig { … };` (and its now-redundant `static_assert`s — the generated header carries the full battery); add, inside `namespace Vixen::SVO`, `using Vixen::Gpu::OctreeConfig;`. It already `#include`s `VoxelChannelFormat.h`, so the generated header + `ChannelDesc` alias are in scope. The ~700 lines of serialization logic (BuildShellOctreeGpu etc.) are unchanged — they set fields by name, which still resolve.
- **Consumers:** all 15 `ShellOctreeGpu.h` + 3 `VoxelChannelFormat.h` includers keep using `Vixen::SVO::OctreeConfig` / `Vixen::SVO::ChannelDesc` verbatim (now aliases). Cross-struct copies (`c.channels[ci] = out.channels[ci]`, `m_channels[i] = s.channels[i]`) compile — both sides are the one `Vixen::Gpu::ChannelDesc`. No forward-declarations of these types exist to clash with the `using` aliases (verified).

### C2 — shader migration (`#include` the generated GLSL)

- **`shaders/BodyInstanceRayMarch.comp`:** replace the inline `struct OctreeConfig { … };` (line 118) with `#include "Generated/OctreeConfig.glsl"`. The `binding=5 OctreeConfigsSSBO { OctreeConfig configs[]; }` block and all field accesses stay. The shader already `#include`s 8 sibling `.glsl` files, so the includer resolves `Generated/OctreeConfig.glsl` (relative to `shaders/`).
- **Drift-guard:** `test_octree_config_sdi_parity` already reflects the compiled `BodyInstanceRayMarch.spv` `configs[]` element vs the C++ struct. Post-migration it validates the **generated** GLSL's actual glslang layout == the C++ struct — the binding proof that the `#include`-swap is layout-correct. No change to the test unless a field name it asserts moved (it won't).

## Gates & testing

- **C1 gate (controller):** full WSL build green (`cmake --preset vixen-wsl && cmake --build build-wsl`) — proves all 18 consumers compile against the aliased types; SVO + RenderGraph test suites green (esp. `test_shell_octree_gpu`, `test_channel_format`, `test_gpu_parity`, `test_stored_sdf_march_mirror`); the B2 parity gtest still green.
- **C2 gate (controller, live — authoritative for GPU work [[live-verification-authoritative-for-gpu-work]]):** the shader compiles (glslc/glslang); `test_body_instance_raymarch_render` renders with **no regression** vs pre-migration (it is an offscreen lavapipe render test with pixel assertions, 777 lines); `test_octree_config_sdi_parity` green (drift-guard on the migrated shader). Run on lavapipe (`VK_ICD_FILENAMES=…/lvp_icd.json`).
- Live-run gate is authoritative: C2 is not "done" until the render test passes on an actual run.

## Milestones

- **C1 — C++ consumes the generated struct** (VIXEN `feat/config-codegen`). Alias `ChannelDesc` (VoxelChannelFormat.h) + `OctreeConfig` (ShellOctreeGpu.h) to the generated types; delete the hand-written definitions. Gate: full build + SVO/RenderGraph tests + parity gtest green.
- **C2 — shader consumes the generated struct + live gate** (VIXEN `feat/config-codegen`). `#include` the generated GLSL in BodyInstanceRayMarch.comp; drop the inline struct. Gate: live render no-regression + SPIR-V drift-guard green.

## Out of scope / deferred

- **Cleanup phase (complete):** the superseded `VIXEN/codegen/Vixen.Codegen*` parallel tool was
  removed in `1b075391`; `codegen/config-schemas/` and the Yeroket-driven `octreeconfig_check`
  remain.
- The legacy `CashSystem::OctreeConfig` (256 B std140) + `VoxelGridNode` struct — a future canonicalization if desired, not now.
- Full GLSL name↔offset drift map for `gridMin`/`gridMax` (still bracketed by matched neighbors + the size assert + the SPIR-V drift-guard).

## Global constraints

- Build: `cmake --preset vixen-wsl && cmake --build build-wsl --target <t>`; lavapipe for the render gate. `~/.dotnet/dotnet` only if regenerating (not needed here — artifacts committed in B2).
- Do NOT hand-edit committed `.g.*`. Do NOT push/merge to origin (controller finishes; user pushes). Work on VIXEN `feat/config-codegen`.
- Clean-commit gate: relevant build/tests green before each commit. Commit trailers:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` / `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.

## Open questions

None — scope bounded to the SVO struct + shader; ChannelDesc reconciliation resolved (alias, no tool change); both migrations proven-clean by B2; the live render test + drift-guard are the existing gates.
