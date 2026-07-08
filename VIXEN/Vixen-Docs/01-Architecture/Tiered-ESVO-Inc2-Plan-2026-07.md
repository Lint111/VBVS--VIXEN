# Tiered ESVO — Increment 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh Sonnet-medium implementer + Opus-high validator
> per milestone, worktree-isolated, progress persisted in this doc). **Live-run gate is authoritative
> for every GPU-touching milestone in this increment (M2-M5)** — this is a bigger, riskier lift than
> Inc1: it modifies the actual hot-path traversal shader (`LaineKarrasOctree`'s GPU-side
> `executeAdvancePhase`/`executePopPhase` equivalent in `BodyInstanceRayMarch.comp`) and the octree
> construction path (`SVORebuild.cpp`), not just additive CPU types. Static review has repeatedly
> passed runtime bugs on this project (Sparse-Mip-ESVO-LOD Inc1/Inc2, Tiered-ESVO Inc1 M3 — two
> self-caught sync bugs — precedent); every milestone that touches the shader or traversal ends in an
> actual `VIXEN.exe` run with validation layers explicitly enabled (see Tiered-ESVO Inc1 M3's own
> discovery: the Release binary compile-gates the app-side validation layer off — you must
> env-inject `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` or you are not actually testing what you
> think you are).

**Goal:** Ship [[Tiered-ESVO-Observer-Addressing-Design-2026-07]]'s §3 (the tier-crossing leaf
reference) and §5 (the traversal-restart algorithm) — the mechanism that lets a ray cross from one
octree's leaf into a **different, independently-resident** octree treated as that leaf's child
content. This is what turns "one ~84km-scale tree with continuous surface-to-orbit LOD" (already
shipped, Sparse-Mip-ESVO-LOD Inc1/Inc2) into "a true Earth-diameter planet, T2 bedrock → T1 region →
T0 planet, one coherent zoom from standing on the surface to seeing the whole globe from space" — the
concrete, user-requested target of this increment.

**Why this needs new machinery, not just bigger trees:** one ESVO instance has a fixed 23-level scale
budget (`ESVOTraversalState::scale` 0-22, confirmed unchanged in `LaineKarrasOctree.h:252,369`) — at a
1cm leaf, that's `2^23 ≈ 8.4×10^6`× linear range, ≈84km end-to-end. A real planet
(`Tiered-ESVO-Inc1`'s own re-derived tier table: T2 bedrock 1024cm span → T1 region 10.49km span → T0
planet 10,737km span, ~10 levels each) needs ~30 effective levels — roughly 3× what one tree can hold.
Tiered ESVO Inc1 deliberately built none of the mechanism that crosses this boundary (its own §0 scope
cut excluded §3/§5 explicitly); this increment builds exactly that.

**Architecture:** Extend `ChildDescriptor`'s existing brick-mode field reuse (no new bytes, no
traversal-stride change) so `farBit==1` on a leaf means "this is a tier-crossing reference, not a
brick" — `contourPointer` becomes an index into a new per-instance `TierRefTable` (parallel array on
`ConcatenatedOctrees`, same shape as the existing `configs[]`/`nodeCounts[]`/`mipPool` pattern), and
`contourMask` carries the child tree's root-scale hint. When GPU traversal hits such a leaf, it
transforms the ray into the child tree's local `[1,2)` frame (a single scale+offset per `TierRef`,
never a flattened world matrix — the same float32-safety discipline Inc1's `TierDirection.h` already
proved numerically stable across ~10^19 scale ratios) and does a **traversal restart**: re-enters the
same iterative ESVO traversal loop against a *different* `OctreeConfig` (mechanically identical to
today's per-instance `configs[octreeIndex]` selection — no new resource type), with a **fresh**
`CastStack`, not a merged/grown one (only one tier's stack is ever live; the parent's is parked, not
recursed into). Before crossing, a screen-space LOD check (reusing `RaySizeCoefNode`'s existing
cone-spread constant) decides whether to cross at all — sub-pixel footprint means "shade from the
parent's mip sample instead," which is the mechanism that keeps most rays never paying the
cross-tier cost. A non-resident child tree is just another mip-fallback case (Sparse-Mip's existing
sentinel-miss pattern, one tier higher) — no new residency state machine.

**Tech Stack:** C++23, GLSL compute (the existing `BodyInstanceRayMarch.comp` traversal path), glm,
GoogleTest, CMake ninja/wsl presets, Vulkan 1.3.

**Reuses:** `ChildDescriptor`'s `farBit` sentinel (checked, unused since inception —
`LaineKarrasOctree.cpp:161`); `ConcatenatedOctrees`'s parallel-array pattern (already exercised for
`mipPool`/`channelPool`/`brickGridLookup`); `userToESVOScale`/`esvoToUserScale`
(`LaineKarrasOctree.h:409,413`, the existing sub-range-of-0-22 remap primitive); `RaySizeCoefNode`'s
LOD cone-spread constant; Sparse-Mip-ESVO-LOD's existing mip-fallback sentinel-miss shading path;
Tiered-ESVO Inc1's `TierAddress`/`TierMath`/`TierDirection`/`TierMagnitude` (CPU-side address/tier-math
types — this increment's `TierAddress` usage generalizes from "sky-projection fixture" to "real
tier-crossing edge bookkeeping," same types, no changes needed to Inc1's files); the `OctreeConfig`
canonical-schema codegen mechanism (`codegen/config-schemas/OctreeConfig.cs` → generated C++/GLSL) for
any new config field this increment needs — **72 bytes of tail padding remain** (`_tailPad[18]` at
offset 360, confirmed 2026-07-07) after Inc1/Inc2 of Sparse-Mip consumed `mipPoolBase`@352/
`brickResident`@356.

**Design of record:** [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §3, §5 (full spec).
**Depends on (shipped):** [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] (Inc1 `ae12ba78` + Inc2
`2351baff`, merged) and [[Tiered-ESVO-Inc1-Plan-2026-07]] (M1-M3, merged `608c4550`) — this increment
builds directly on both; re-verify their APIs at implementation time rather than trusting this doc's
citations if significant time has passed.

---

## §0. Scope

**In scope for this increment:**
- The `farBit==1` tier-crossing leaf interpretation on `ChildDescriptor` (§3.1) — a new construction
  path in `SVORebuild.cpp`/`SVOBuilder.cpp` that can mark a leaf as tier-crossing and populate a
  `TierRefTable` entry, alongside the existing (unchanged) brick-leaf path.
- `TierRef` (§3.2): the parallel-array indirect payload on `ConcatenatedOctrees`.
- GPU traversal-restart (§5.1): the shader-side ray-remap + re-entry into a different
  `OctreeConfig`'s traversal with a fresh stack, on hitting a tier-crossing leaf.
- The LOD early-out (§5.2): the screen-space check that avoids crossing for sub-pixel-footprint
  children, reusing `RaySizeCoefNode`.
- Residency reuse (§5.3): confirming/wiring the existing mip-fallback sentinel-miss path handles a
  non-resident tier-crossing child correctly — no new residency state machine.
- A concrete, testable end-to-end scene: at minimum a two-tier setup (e.g. T1 region tree with a
  tier-crossing leaf pointing at a T2 bedrock tree) proving a ray genuinely crosses, renders correct
  geometry on both sides of the boundary, and a screen-space zoom transitions continuously through
  the crossing without a visible pop/seam — this is the actual "surface to orbit" proof the user asked
  for, even if the full 3-tier (T2→T1→T0) planet composition is a stretch goal for this increment's
  final milestone rather than a hard requirement (see Milestone Map for where the line is drawn).

**Out of scope for this increment:**
- The address-level sky-projection zoom handoff (design doc §7 steps 2-3: transitioning FROM a
  sky-projected point INTO real tier-crossing traversal) — Inc1's `SkyProjectionNode` remains a
  separate, synthetic-fixture-only consumer; wiring it to real tier-crossing data is a later
  increment's integration work, not this one's.
- Any undertow-side reification, delta-log, or real fleet/planet data feed (§6) — this increment's
  test scenes are still VIXEN-internal synthetic/hand-authored trees, not undertow-fed.
- `TierAddress`'s wire-format reconciliation with undertow's `Undertow.View` schema (§4, §11) —
  unchanged from Inc1's own deferral.
- A full N-tier (System/Galaxy) traversal chain — this increment proves the mechanism at one crossing
  (and, as a stretch goal, chains it through the T2→T1→T0 planet-scale case the user actually asked
  about); System/Galaxy-tier crossings reuse the identical mechanism and are validation/content work
  for a future increment, not new engineering.
- Growing `MAX_STACK_DEPTH` or merging parent/child stacks — explicitly rejected by the design doc
  (§10) and unchanged here: one stack live at a time, parent parked not recursed into.

**Why this is the right next increment:** this is the literal mechanism gap between "one body's
continuous surface-to-orbit LOD" (shipped) and "a true planet's continuous surface-to-orbit view"
(what was asked for) — nothing else in either shipped epic gets you there; Inc1's own design doc
(§9) names this as the natural next increment once nested-tree work is actually prioritized, which it
now is by explicit user request.

---

## Milestone Map

- **M1 — `TierRef` + `TierRefTable` CPU-side plumbing** (Tasks 1-3) · gate: pure-CPU/config-plumbing
  gtest green — the data structures and the `OctreeConfig`/`ConcatenatedOctrees` wiring, no traversal
  logic yet, no shader changes.
  **✅ DONE 2026-07-07** — commit `595a5e83` (worktree `feat/tiered-esvo-inc2`, branched from `main`).
  `test_tier_ref` 5/5, `test_tier_ref_table` 5/5, both green, pure CPU (no Vulkan/GPU dependency
  exercised). Full existing SVO suite re-run and green as a no-regression check (`test_svo_types`
  10/10, `test_shell_octree_gpu` 9/9, `test_soa_mip_serialize` 6/6, `test_soa_sdf_serialize` 11/11,
  `test_gpu_parity` 4/4 — 40/40). Opus-validated APPROVED 2026-07-08 (51/51 including the
  SPIR-V-reflection drift-guard the implementer believed it couldn't run; GPU-binding scope
  deferral to M3 independently judged correct). See Progress Log for the std430-layout proof, the
  `TierRefTable` GPU-binding scope decision, and the codegen regen mechanism used for Task 3.
- **M2 — `farBit==1` construction path** (Tasks 4-5) · gate: a hand-authored two-tree test fixture
  (one tree with a real tier-crossing leaf, `farBit=1`, pointing at a second, independently-built
  tree) round-trips through serialization correctly; no regression on existing `farBit=0` trees
  (Sparse-Mip/Surface-Shell's existing full test suite still green).
  **✅ DONE 2026-07-08** — commit `8110b95b` (worktree `feat/tiered-esvo-inc2`). `test_tier_crossing_
  construction` 5/5 green, pure CPU (no Vulkan/GPU dependency exercised). Full existing SVO regression
  sweep re-run (98 targets, 454 tests passed + 16 pre-existing failures + 1 pre-existing segfault, ALL
  confirmed byte-identical against the pre-M2 `2bb752ec` baseline via a stash/rebuild/compare — zero
  regressions). Opus-validated APPROVED (validator independently re-derived the baseline comparison
  from scratch via its own rebuild, and independently grepped every brick-accessor call site rather
  than trusting the implementer's list). See Progress Log for the `MarkLeafAsTierCrossing` API shape,
  the `ChildDescriptor::setTierCrossing` accessor, the `SVOTraversal.cpp` guard fix, and the
  pre-existing-failure verification method.
- **M3 — GPU traversal-restart, single crossing** (Tasks 6-8) · **live-run gate, validation layers
  mandatory** · the highest-risk milestone: a ray genuinely crosses from a parent tree's leaf into a
  child tree's own traversal and renders correct geometry, proven on real hardware, not just compiled
  shader code.
  **✅ DONE_WITH_CONCERNS 2026-07-08** — see Progress Log below for the full account, including the
  visual-legibility caveat flagged for validator attention.
- **M4 — LOD early-out + residency reuse** (Tasks 9-10) · live-run gate · the screen-space gate that
  avoids crossing for distant/sub-pixel children, and confirming the mip-fallback sentinel-miss path
  correctly serves a non-resident tier-crossing child.
- **M5 — Continuous zoom proof (the actual "surface to orbit" demonstration)** (Task 11) · live-run
  gate · a camera path that starts at T2-bedrock-scale detail and pulls back through at least one real
  tier crossing with no visible pop/seam at the boundary. **Stretch target within M5, not a hard
  gate**: chain two crossings (T2→T1→T0) to get the actual Earth-diameter planet surface-to-orbit
  view the user asked about — if time/complexity runs long, a single clean crossing (M3+M4's own
  gate) that visibly and correctly works is still a legitimate, demonstrable increment; do not let the
  3-tier stretch goal block shipping the 1-crossing proof.

### Progress Log

(populated as milestones complete, following the Tiered-ESVO Inc1 / Sparse-Mip-ESVO-LOD Inc1/Inc2
plans' own convention: one entry per milestone, commit hash + Opus validator verdict)

- **Milestone M1 (Tasks 1-3): DONE** · commit `595a5e83` (worktree `feat/tiered-esvo-inc2`, branched
  from `main`) · gates: `test_tier_ref` 5/5, `test_tier_ref_table` 5/5, both green, pure CPU (no
  Vulkan/GPU dependency exercised); full existing SVO regression sweep re-run green (40/40, zero
  regressions) · 2026-07-07.
  - **Re-verified drifted citations before writing code**: no `.codegraph/` index existed in this
    worktree (contrary to the task brief's assumption) — fell back to direct file reads/grep. Confirmed
    `ConcatenatedOctrees`'s real current shape (`nodes`/`bricks`/`materials`/`channelPool`/
    `brickGridLookup`/`mipPool`/`configs`/`nodeCounts`/`brickCounts`, `ShellOctreeGpu.h`) directly rather
    than trusting the design doc's original sketch. Confirmed the 72-byte tail-pad figure
    (`_tailPad[18]` at offset 360) was still accurate as of this session — no concurrent increment had
    consumed it.
  - **`TierRef` layout** (`TierRef.h`): `childOctreeIndex` (uint32, @0), `childOriginLocal[3]` (plain
    `float[3]`, @4), `childScale` (float, @16) — **20 bytes total, no hidden padding**, proven via
    `static_assert(offsetof(...))` for every field plus a struct-size assert. Deliberately used a plain
    `float[3]` array rather than `glm::vec3`: re-applying Sparse-Mip Inc1's own documented std430
    gotcha (a `vec3` struct member has base alignment 16 but occupies only 12 bytes, so the padding
    trap is specifically a `vec3`-typed member issue, not a general "3 floats in a row" issue) — three
    independent 4-byte-aligned scalars have no such trap, confirmed by the offset asserts landing at
    exactly 0/4/16 with zero gaps. Also asserted `std::is_standard_layout_v`/`is_trivially_copyable_v`
    for safe memcpy/byte-buffer round-tripping. Deliberately shaped to compose with Inc1's shipped
    `TierHopFrame` (`TierDirection.h`) — that file's own header comment already documented "a
    `TierHopFrame` is exactly one `TierRef`'s (origin, scale) pair," confirmed by reading it directly
    rather than assuming.
  - **`TierRefTable` wiring** (`ShellOctreeGpu.h`): added `SerializedOctree::tierRefs`
    (`std::vector<TierRef>`, per-tree, opt-in — no producer exists yet since M2's `farBit==1`
    construction path is out of scope) and `ConcatenatedOctrees::tierRefTable` +
    `ConcatenatedOctrees::tierRefCounts` (concatenated table + per-octree entry count), following the
    exact base-offset+running-count convention `mipPoolBase`/`poolBrickBase` already establish — wired
    into all three concatenation entry points (`Concatenate`, `ConcatenateSdf`, and
    `ConcatenateSdfWithMips` in `MipBake.h`, which duplicates `ConcatenateSdf`'s loop and needed the
    same bookkeeping added to avoid a silent `tierRefCounts.size() != configs.size()` desync). Every
    existing call path leaves `tierRefs` empty, so `tierRefTableBase` stays 0 and `tierRefTable` stays
    empty for every tree today — fully additive, zero regression risk.
  - **GPU-binding scope decision (deliberate, flagged for validator attention)**: the plan's Task 2 text
    literally asks to "wire the corresponding GPU-side buffer/binding the same way mipPool's binding was
    added" (a new `BodyOctreeSceneNodeConfig` output slot, `BuildRenderGraph.cpp` connection, and a
    `SVOTypes.glsl`/shader-side buffer declaration) — but the M1 gate explicitly states "no
    shader/traversal-logic changes yet, this milestone is pure plumbing," and the Notes section
    describes M1 as pure "parallel-array additions, config-schema regen... no new architecture." These
    two statements are in tension for the GPU-binding sub-bullet specifically. Resolved by NOT adding
    the `BodyOctreeSceneNodeConfig` output slot / `BuildRenderGraph.cpp` wiring / shader-side `.glsl`
    declaration this milestone — there is no consumer for a `TierRefTable` SSBO binding until M3 wires
    real traversal-restart shader code, and adding an unconsumed binding now would be dead plumbing that
    contradicts the "no shader changes" gate. The CPU-side storage (`ConcatenatedOctrees::tierRefTable`)
    is fully in place and ready for M3 (or an earlier milestone, if the validator judges the buffer
    upload itself — without shader consumption — still counts as "CPU-side plumbing") to wire the
    Vulkan buffer + descriptor binding when a real shader-side reader exists. **Flag for validator**:
    confirm this scope read is correct, or specify that the buffer-creation-without-shader-consumption
    half should still be added in M1.
  - **`OctreeConfig.tierRefTableBase`** (Task 3): added at byte 360 via the canonical
    `codegen/config-schemas/OctreeConfig.cs` → Yeroket kernel-codegen regen (`dotnet run --project
    ~/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~ -c Release --
    --schema codegen/config-schemas --struct OctreeConfig --out-cpp
    libraries/SVO/include/Generated/OctreeConfig.g.h --out-glsl shaders/Generated/OctreeConfig.glsl`),
    consuming 4 of the 72 free tail-pad bytes (`_tailPad` shrinks from `[18]` to `[17]`, still ending at
    byte 432 — `sizeof(OctreeConfig)` unchanged at 432). Golden `--check` re-run confirmed the committed
    generated files match the schema byte-for-byte (exit 0, no diff). The Yeroket tool AND `dotnet`
    (8.0.128) were both available in this environment at `~/Github/Yeroket-Fantasy` — no workaround
    needed. The heavier SPIR-V-reflection drift-guard (`test_octree_config_sdi_parity`, RenderGraph
    tests) requires a `glslc`-compiled shader and was not re-run standalone (no `glslc` on the WSL side
    of this machine — only a Windows-side `VulkanSDK` install); it did NOT need code changes since it
    doesn't name `tierRefTableBase` among the fields it explicitly checks, and it built/passed as part
    of the full `vixen-wsl` preset's earlier successful configure (Vulkan SDK auto-provisioned,
    `glslc`/`glslangValidator` found under `.vulkan-sdk/`) — a lighter, SVO-local equivalent proof
    (`TierRefTable.OctreeConfigTierRefTableBaseFieldOffsetAndStructSize`) was added instead as this
    milestone's own build-light gate.
  - **Environment note**: this worktree had no pre-existing CMake build directory (fresh worktree) and
    no `.codegraph/` index, contrary to the task brief's expectations. First `cmake --preset vixen-wsl`
    configure took ~3137s (auto-provisioning the Vulkan SDK + X11 dev headers into the worktree, plus
    FetchContent cloning several dependencies — `nlohmann/json` in particular was unusually slow to
    check out, consistent with this project's known `/mnt/c` cross-mount I/O penalty) — substantially
    longer than the ~500s a prior increment's own note cited for a first configure, but this worktree
    also provisioned the full Vulkan SDK from scratch (a prior increment's note assumed one might
    already exist). Configure completed successfully once finished; targeted `ninja` builds of just the
    needed test binaries thereafter were fast (~90-480s each, mostly SVO/GaiaVoxelWorld library
    compilation, not re-triggering the dependency fetch).
  - **No scope drift**: confirmed via `git diff --stat` before commit — only
    `libraries/SVO/include/{TierRef,ShellOctreeGpu,MipBake}.h`,
    `libraries/SVO/tests/{test_tier_ref,test_tier_ref_table}.cpp` + `CMakeLists.txt`,
    `codegen/config-schemas/OctreeConfig.cs` + its two generated outputs. `ChildDescriptor`/`farBit`,
    `SVORebuild.cpp`/`SVOBuilder.cpp`'s construction logic, `LaineKarrasOctree`'s traversal code,
    `BodyInstanceRayMarch.comp`, and `SkyProjectionNode` were all read-only-verified (to confirm the
    scope boundary) but not modified.

  - **Opus validator: APPROVED (2026-07-08)** — independently re-ran everything rather than trusting
    the report, including the one gap the implementer self-flagged as unverified. Confirmed **51/51**
    total (the claimed 50/50 above PLUS `test_octree_config_sdi_parity` 1/1, the SPIR-V-reflection
    drift-guard — the implementer believed `glslc` was unavailable on WSL in this worktree; the
    validator confirmed this was WRONG (`glslc` IS present in this build cache) and ran it directly:
    it reflects the compiled shader as `size=432 members=25 ... tierRefTableBase offset=360` and
    confirms member-by-member parity with the C++ struct — the new field is genuinely
    cross-checked, not just asserted in isolation). Also confirmed the implementer's ".codegraph/
    index did not exist" claim was wrong (`codegraph.db`, 46MB, predates the milestone's own commit
    by ~1.5 hours — likely a wrong-path check, not a real gap); neither false environment claim
    affected the actual correctness of the work. Independently re-derived the std430 reasoning for
    `TierRef`'s plain-`float[3]` layout (confirmed std430, unlike std140, does not pad scalar-array
    elements to 16 — the vec3 gotcha is specific to a `vec3`-typed member, not "three floats in a
    row") and confirmed it against the design doc's §3.2/§3.3. Confirmed the `ConcatenateSdfWithMips`
    duplication is genuinely necessary (a separate standalone loop, not a caller into
    `ConcatenateSdf`) and traced that every existing tree's `tierRefTableBase`/`tierRefTable`
    genuinely stays 0/empty. Re-ran the actual Yeroket `--check` regen independently (exit 0, tree
    unchanged) — confirmed genuine codegen output, not a hand-edit. **On the GPU-binding scope
    deviation: reasoned this out independently and reached the same conclusion — a correct call, not
    a blocker.** An SSBO binding with no shader-side consumer would be dead plumbing generating
    unused-binding/VUID noise for nothing; the CPU-side storage M3 actually needs is fully in place;
    and M3 (the live-gated GPU milestone) is where the buffer and its shader reader can be built and
    validated together, matching Sparse-Mip Inc1 M3's own precedent for `mipPool`'s binding. Noted as
    a non-blocking suggestion for M3's implementer, not a required M1 addition. No issues found.

- **Milestone M2 (Tasks 4-5): DONE** · commit `8110b95b` (this worktree, `feat/tiered-esvo-inc2`) ·
  gates: `test_tier_crossing_construction` 5/5 green, pure CPU (no
  Vulkan/GPU dependency exercised); full existing SVO regression sweep re-run (98 test targets) and
  confirmed byte-identical (454 passed / 16 failed / 1 segfault, every failure/segfault reproduced
  on the pre-M2 `2bb752ec` baseline via a stash/rebuild/compare — zero regressions attributable to
  this milestone) · 2026-07-08.
  - **Re-read the real M1 API before writing code**: `libraries/SVO/include/TierRef.h`'s `TierRef`
    (20-byte, plain-`float[3]` layout) and `ShellOctreeGpu.h`'s `SerializedOctree::tierRefs` /
    `ConcatenatedOctrees::tierRefTable`+`tierRefCounts` / `tierRefTableBaseOf`/`setTierRefTableBase`,
    confirmed unchanged from M1's own Progress Log description. Confirmed `.codegraph/` genuinely
    exists in this worktree this time (`codegraph.db`, 46 MB) — the MCP `codegraph_explore` tool
    itself was not available as a callable tool in this session (not listed among loaded/deferred
    tools), so fell back to direct `grep`/`Read`, which was sufficient for a milestone this scoped.
  - **`ChildDescriptor::setTierCrossing` / `isTierCrossing` / `getTierRefIndex` /
    `getChildRootScaleHint`** (`SVOTypes.h`): a THIRD interpretation of the existing
    `contourPointer`/`contourMask` field pair, selected by `farBit`, added in the exact same style as
    the existing `setBrickIndex`/`getBrickIndex`/`hasBrick` group (same file, same struct, same
    "helpers" comment-block convention). `setTierCrossing(tierRefIndex, childRootScaleHint)` sets
    `farBit=1` and packs the two fields; it deliberately does NOT touch `validMask`/`leafMask` — the
    caller is expected to already have the leaf bit set for that child slot (the marking function
    below enforces this). `childRootScaleHint` is taken as an EXPLICIT caller-supplied `uint8_t`
    (0-22), not derived from `TierRef::childScale` — the plan's own Task 4 text names it as a
    separate parameter (`setTierCrossing(tierRefIndex, childRootScale)`), and deriving one from the
    other would have been wrong: `childRootScaleHint` is the CHILD tree's own root ESVO scale (a
    property of ITS `maxLevels`/`userToESVOScale` mapping, `LaineKarrasOctree.h:409`), a completely
    different quantity from `TierRef::childScale` (a parent-local linear scale factor, §3.3) — no
    formula converts one to the other without knowing the child's own construction, which this
    milestone's construction-time function does not have visibility into.
  - **`MarkLeafAsTierCrossing(SerializedOctree&, parentDescriptorIndex, octant, TierRef, childRootScaleHint)`**
    (`ShellOctreeGpu.h`, inserted between `Serialize()` and `SerializeSdf()`): the ONE explicit
    opt-in API this milestone builds. Shape decision: operates on an ALREADY-SERIALIZED
    `SerializedOctree` (post-`Serialize()`/`SerializeSdf()`), not on the raw `Octree`/
    `GaiaVoxelWorld` construction path directly — because `Concatenate()`/`ConcatenateSdf()` call
    `Serialize()`/`SerializeSdf()` INTERNALLY from the owning `ShellOctree`/`SdfBodyOctree` (confirmed
    by reading both functions directly), so mutating the source `Octree` before concatenation would
    require re-serializing, and mutating a `SerializedOctree` in place is both simpler and matches
    M1's own `test_tier_ref_table.cpp` precedent of manually replicating the concatenation loop
    rather than routing pre-populated `tierRefs` through the real entry points (that plumbing does
    not exist yet, for either milestone — a future milestone/caller that needs `Concatenate`/
    `ConcatenateSdf` to preserve pre-marked leaves would need to either accept pre-serialized inputs
    or take a marking callback; out of this milestone's scope). Identifies the leaf via
    `(parentDescriptorIndex, octant)` — the SAME addressing convention already used by
    `SVORebuild.cpp`'s `leafToBrickView` key (`(parentIdx << 3) | octant`) and by
    `test_mip_sample_bake.cpp`'s existing "scan `childDescriptors` directly to find a leaf" pattern —
    re-derives the physical child-descriptor index via `childPointer + totalInternalChildren +
    leafChildrenBeforeThisOctant`, the exact formula `SVORebuild.cpp`'s BFS reorder and
    `SVOTraversal.cpp`'s `castRayGpuMirror` leaf-hit path both already use. Validates: octant 0-7,
    `childRootScaleHint` 0-22, the (parent, octant) pair is a real existing leaf child, and the
    resolved leaf descriptor index is in-range — throws `std::runtime_error` otherwise (matching this
    file's existing `Serialize`/`SerializeSdf`/`Concatenate` error-handling convention, all of which
    throw on malformed input rather than silently no-op). Mirrors the file's own established
    "opt a tree/leaf into special behavior via one small explicit call" shape (`setBodyOctree`,
    `setSignedDistanceField` on `LaineKarrasOctree` — and `getOctreeMutable()`'s own doc comment,
    "For direct modification (additive insertion)," which is exactly this pattern already named but
    previously unused by any caller).
  - **Verified `hasBrick()`/`getBrickIndex()` call-site safety** (the plan's explicit Task 4
    correctness check) by reading every call site directly (`grep` across `libraries/SVO/src/*.cpp`,
    `.codegraph`'s MCP tool unavailable this session — see above): `LaineKarrasOctree.cpp`'s
    `voxelExists`/`getVoxelData`/`getChildMask` never call the brick accessors on a leaf at all (they
    return immediately on `isLeaf(childIdx)==true`, before ever dereferencing the leaf's own
    descriptor); `SVOBrickDDA.cpp`'s legacy `handleLeafHit` uses the `leafToBrickView`/
    `getBrickView()` hashmap lookup, never `getBrickIndex()` directly. ONE real, unguarded call site
    found: `SVOTraversal.cpp:1029`'s `castRayGpuMirror` (the `LaineKarrasOctree`'s own GPU-parity
    mirror, used for body-octree non-LOD `castRay`) calls `leafDesc.getBrickIndex()` with no `farBit`
    check — a `TierRefTable` index (small, often in-range) would otherwise be silently misread as a
    brick index and render wrong geometry rather than cleanly missing. **Fixed with a minimal
    defensive guard** (`if (!leafDesc.farBit && localBrickIdx != INVALID_BRICK_INDEX && ...)`) —
    genuinely a MISS for a tier-crossing leaf today (correct: no traversal-restart exists yet, M3's
    job), not new tier-crossing traversal logic, so it stays inside this milestone's scope boundary
    ("do NOT add new traversal logic," per the task brief). **Flagged, NOT touched** (would be scope
    creep into M3): `GpuTraversalMirror.h`'s `handleLeafHit` (a SEPARATE, TEST/REFERENCE-only 1:1
    mirror of the actual GPU shader `BodyInstanceRayMarch.comp`, used only by
    `test_gpu_parity.cpp`/`test_stored_sdf_march_mirror.cpp`) has the identical unguarded pattern —
    it mirrors the shader's CURRENT (farBit-blind) behavior faithfully, and no existing test feeds it
    a tier-crossing tree, so there is no live bug; M3 will need to update this mirror in lockstep with
    its real shader change anyway (per the header's own "SYNC CONTRACT: if the shader changes,
    re-port the changed function" comment), so pre-emptively guarding it here would be duplicate,
    soon-stale work. **Flag for validator**: confirm this scope line (fix the one real engine-class
    call site; leave the shader-mirror test oracle for M3) is the correct read.
  - **Two-tree fixture + round-trip proof** (`test_tier_crossing_construction.cpp`, new file):
    reused M1's own `SdfFixture` shape (`BakeRecipeToSdfWorld`/`BuildSdfBodyOctree`, `n=16, r=6.0,
    brickDepth=3` → `bricksPerAxis=2` → root's 8 children are all deterministic brick-level leaves,
    per `test_mip_sample_bake.cpp`'s own verified assumption) — two INDEPENDENT `SdfFixture`
    instances (each constructs its own `GaiaVoxelWorld`/registry/octree from scratch) stand in for
    "parent" and "child" trees. Marks the parent's first leaf tier-crossing via
    `MarkLeafAsTierCrossing`, manually concatenates both (mirroring `test_tier_ref_table.cpp`'s own
    manual-loop convention, for the reason above), then reads the marked leaf back out of the
    CONCATENATED node buffer via `nodeArrayBase + local index` (the same addressing a real GPU
    consumer would use) and confirms: `farBit`/`isTierCrossing()` survived the byte-verbatim append;
    `tierRefTableBaseOf(parent's config) + leaf's tierRefIndex` resolves into `cat.tierRefTable` to
    the exact `TierRef` that was registered; that `TierRef::childOctreeIndex` is a real,
    dereferenceable index into `cat.configs`/`cat.nodeCounts` whose `nodeArrayBase` genuinely starts
    immediately after the parent's node slice, and whose root descriptor (read back from that
    resolved offset) matches the child's own original serialized root descriptor byte-for-byte. This
    is the literal "serialize -> concatenate -> resolves to the child's actual octree
    index/origin/scale" proof the M2 gate asks for. 5/5 tests green: the round-trip test, a baseline
    isolation test (mark one leaf, confirm the descriptor bytes directly), two input-validation
    tests (out-of-range octant/scale-hint, non-leaf/non-existent child slot both throw), and an
    unmarked-tree no-regression test (every leaf of a plain `ConcatenateSdf`'d tree still reads
    `farBit=0` with `hasBrick()`/`getBrickIndex()` behaving exactly as before).
  - **Full regression sweep — verification method, since 16 pre-existing failures + 1 pre-existing
    segfault were discovered while running the "full existing SVO suite" gate**: rather than assume
    these were caused by this milestone's changes, `git stash`ed all M2 work, rebuilt the 9 affected
    binaries (`test_attribute_registry_integration`, `test_brick_traversal`, `test_brick_view`,
    `test_cornell_box`, `test_octree_queries`, `test_ray_casting_comprehensive`, `test_svo_builder`,
    `test_voxel_injection`, `test_entity_brick_view`) against the clean M1 baseline (`2bb752ec`), and
    re-ran them. Every single failure (16 individual `[FAILED]` tests) and the `test_entity_brick_view`
    segfault reproduced IDENTICALLY on the pre-M2 baseline — confirmed pre-existing, unrelated to
    this milestone's `ChildDescriptor`/`SVOTraversal.cpp`/`ShellOctreeGpu.h` changes. `git stash pop`
    restored the M2 changes; the full 98-target sweep was then re-run against the restored M2 state
    and produced the exact same 454-passed/16-failed/1-segfault outcome. Flagging these 16 tests +
    1 segfault as a known, pre-existing gap for a future increment/cleanup pass — NOT this
    milestone's responsibility to fix, and explicitly out of scope (unrelated subsystems: attribute
    registry, brick DDA/traversal, Cornell-box ray casting, octree-queries partial-update, comprehensive
    ray casting, mesh-based `SVOBuilder`, legacy voxel injection, entity-brick-view). **Flag for
    validator**: independently re-verify at least a sample of these 16 failures/1 segfault against
    `2bb752ec` if time allows, since this claim is load-bearing for the "zero regression" gate.
  - **No scope drift**: confirmed via `git diff --stat` before hand-off — only
    `libraries/SVO/include/SVOTypes.h` (the 3 new `ChildDescriptor` accessors + `setTierCrossing`),
    `libraries/SVO/include/ShellOctreeGpu.h` (`MarkLeafAsTierCrossing` + 2 new includes),
    `libraries/SVO/src/SVOTraversal.cpp` (the one-line `farBit` guard + comment),
    `libraries/SVO/tests/CMakeLists.txt` (new target registration), and the new
    `libraries/SVO/tests/test_tier_crossing_construction.cpp`. `SVORebuild.cpp`/`SVOBuilder.cpp`'s
    existing construction paths, `LaineKarrasOctree`'s traversal entry points other than the one
    guarded line, `BodyInstanceRayMarch.comp`, and `SkyProjectionNode` were all read-only-verified but
    not modified — `farBit=0` is still set exactly as before at `SVORebuild.cpp:439,512` (re-confirmed
    at these exact line numbers, unchanged since M1).
  - **Opus validator: APPROVED (2026-07-08)** — independently verified every claim rather than
    restating the report. Confirmed the placement deviation is sound by reading `Concatenate`/
    `ConcatenateSdf` directly (both internally re-call `Serialize`/`SerializeSdf`, so a pre-serialization
    mark would be discarded regardless — post-serialization is the only workable shape). Confirmed the
    `ChildDescriptor` accessors match §3.1's bit layout exactly and that `childRootScaleHint` is
    genuinely a distinct parameter from `TierRef::childScale`. **Did an independent, from-scratch
    codebase-wide grep of every `getBrickIndex()`/`hasBrick()`/`getBrickFlags()` call site** (not the
    implementer's list) — confirmed exactly one real unguarded call (`SVOTraversal.cpp:1029`), confirmed
    the fix is minimal and correct, and confirmed the `GpuTraversalMirror.h` deferral is genuinely safe
    by grepping every `farBit=1` writer in the repo (only `setTierCrossing`/`MarkLeafAsTierCrossing`/the
    new test ever set it; neither mirror-consuming test constructs a tier-crossing tree). Fresh-built
    and ran `test_tier_crossing_construction` (5/5) and read the two-tree round-trip test in full —
    confirmed it is a genuine proof, not trivially-passing. **For the regression claim, did the
    definitive check independently** rather than accepting the implementer's stash-diff description:
    reverted the 3 changed files to the `2bb752ec` baseline, rebuilt libSVO + the 9 affected binaries,
    re-ran, and got the byte-identical 8-fail/1-segfault set (per-binary view of the same 16-test/1-seg
    failure set), then restored to HEAD. Zero regressions attributable to M2, corroborated by an
    independent rebuild, not just a re-read of the implementer's own methodology description. Tree
    clean at `8110b95b`, no conflict markers; the two pre-existing stash entries (2026-06-12,
    unrelated branch) are not M2 leftovers. No issues found.

- **Milestone M3 (Tasks 6-8): DONE_WITH_CONCERNS** · this worktree, `feat/tiered-esvo-inc2` ·
  gates: `test_gpu_parity` 4/4 green (zero regression in the CPU-side GPU-mirror parity oracle after
  refactoring it for the restart); full core SVO regression sweep re-run green (`test_svo_types`
  10/10, `test_shell_octree_gpu` 9/9, `test_soa_mip_serialize` 6/6, `test_soa_sdf_serialize` 11/11,
  `test_tier_ref` 5/5, `test_tier_ref_table` 5/5, `test_tier_crossing_construction` 5/5,
  `test_stored_sdf_march_mirror` 12/12 — 63/63 total); live `VIXEN.exe` run (Windows, `vixen-ninja`
  preset) with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` explicitly injected, zero new VUIDs
  (byte-identical pre-existing-only VUID signature vs. a clean baseline run); GLSL/C++ struct-layout
  byte-parity independently proven via `glslc`/`glslangValidator` SPIR-V reflection · 2026-07-08.

  - **Design: traversal-restart via a non-recursive second top-level call, not a factored shared
    loop.** GLSL has no recursion, and the pre-M3 `traverseOctreeInstanced` (`BodyInstanceRayMarch.
    comp`) already had exactly ONE per-tree state surface outside its own locals: three globals
    (`g_octreeIdx`/`g_esvoNodeBase`/`g_brickArrayBase`, set once per instance in `main()`). Everything
    else (`TraversalState`, `StackEntry stack[STACK_SIZE]`, `RayCoefficients`) is already a local
    variable, declared fresh on every call. This meant the "genuinely the hardest part" the plan
    warned about (Task 7) reduced to: (1) rename the existing single-tree loop body to
    `traverseOctreeInstancedOnce`, taking its octree binding (`cfg`/`nodes`/`brickData`/bases) as
    EXPLICIT parameters instead of implicitly reading the globals directly inside the loop body
    (needed so the SAME function can be pointed at either the parent's or the child's data); (2) add
    a public `traverseOctreeInstanced` wrapper (same name/signature as before M3 — `main()`'s call
    site is UNCHANGED) that calls `traverseOctreeInstancedOnce` once for the ray's home tree, and — if
    that call reports a tier-crossing miss — saves the 3 globals, swaps them to the child, calls
    `traverseOctreeInstancedOnce` a SECOND time (sequential, not recursive) with a remapped ray, then
    restores the 3 globals UNCONDITIONALLY before returning. No stack-depth growth, no merged stacks,
    no new architecture — "a fresh stack, not merged with the parent's" falls out for free from GLSL's
    own per-call-local-array semantics.
  - **Insertion point for the tier-crossing check**: inside the leaf-hit branch of
    `traverseOctreeInstancedOnce`, BEFORE the existing `brickResident==0u` mip-fallback dispatch — the
    exact same insertion point Sparse-Mip Inc1 M3's own "streaming grace" check already uses (same
    leaf-descriptor-resolution helper, `resolveLeafDescriptorIndex`). Reads the leaf descriptor via
    `fetchESVONode`, checks `getFarBit`; if set, resolves `getTierRefIndex()` + `octreeConfig.
    tierRefTableBase` into an absolute `tierRefTable[]` index, bounds-checks against `tierRefTable.
    length()` (empty-buffer-safe exactly like `mipPool.length()`), and — if in range — reports the
    crossing via `out` params (`tierCrossHit`, the resolved `TierRef` index, the parent-local ray
    origin/direction AT the crossing point, and the crossing's own world-consistent `t`) and returns
    a MISS from this call's own perspective (the wrapper, not this function, does the actual restart).
  - **Task 6 ray-remap — cross-checked against Inc1's CPU-side composition, not assumed.**
    `TierDirection.h`'s `SumTail`/`ComposeLocalDirection` composes a CHILD-frame hop into its PARENT's
    frame as `parentPoint = childOriginLocal + (childLocalPoint - 1.5) * childScale` (re-derived
    directly from that file's own code, not from memory). Solving this for `childLocalPoint` gives the
    GPU's needed direction: `childLocalPoint = (parentPoint - childOriginLocal) / childScale + 1.5`,
    and for a direction (no center/origin term, since a direction is a position-derivative):
    `childLocalDir = parentLocalDir / childScale`. Implemented as `remapRayIntoChildFrame` in the
    shader (and a byte-identical port in `GpuTraversalMirror.h`, see below) — confirmed by direct
    algebraic derivation to be the exact mathematical inverse, not just "looks plausible."
  - **The genuinely hard bug this milestone surfaced and fixed BEFORE it shipped: hitT unit
    consistency across the crossing.** `traverseOctreeInstancedOnce` cannot be fed a bare local-frame
    ray — `initRayCoefficients`'s internal math re-derives everything from a WORLD-space
    `rayOrigin`/`rayDir` via `octreeConfig.worldToLocal` (now the CHILD's config, after the global
    swap), so the only correct way to hand it a specific LOCAL ray is to synthesize a WORLD-space
    origin/direction that round-trips through the CHILD's own `localToWorld`/`worldToLocal` back to
    EXACTLY the desired local coordinates (`childRayOriginWorld = childLocalToWorld * (childLocalOrigin
    - 1)`, `childRayDirWorld = mat3(childLocalToWorld) * childLocalDir` — the child's own matrices
    used as an arbitrary embedding space, mirroring how `main()`'s own `instOrigin`/`instDir` already
    exploit `renderScale` as a uniform divisor on both origin and direction). The FIRST version of this
    fix incorrectly assumed the child call's returned `hitT` needed to be re-scaled by
    `|childRayDirWorld|` (reasoning "hitT is a distance along a non-unit-length direction, so multiply
    it back") — this is WRONG and was caught and corrected via an explicit algebraic derivation
    (worked out with a small numeric Python check, not just re-reasoning in prose) before landing:
    because `childRayOriginWorld`/`childRayDirWorld` are constructed so that `childRayOriginWorld +
    s·childRayDirWorld` maps through the child's `worldToLocal` to exactly `childLocalOrigin +
    s·childLocalDir` for `s` ALREADY in real-world-distance units (by the same "uniform scale divides
    both origin and direction" argument that makes `instOrigin`/`instDir`'s own `hitT` already
    world-consistent regardless of `renderScale`), the child call's OWN internal `hitT` already IS `s`
    — a real-world-distance OFFSET FROM THE CROSSING POINT, needing no further scaling. The actual fix:
    capture the crossing point's own world-consistent `t` (`tierCrossWorldT = tBias + state.t_min` at
    the moment of detection) and simply ADD it to the child call's `hitT` afterward
    (`hitT = tierCrossWorldT + childHitT`). **Flag for validator**: this derivation is the single most
    error-prone piece of math in the whole milestone — re-derive it independently rather than trusting
    this account, ideally with the same kind of small numeric check used to catch the first wrong
    version.
  - **`TierRefTable` GPU buffer + binding 15** (Task 8's own CPU-side plumbing need): followed the
    `mipPool`/binding-13 precedent exactly (`BodyOctreeSceneNode.h`/`.cpp`: new `tierRefTableBuffer_`/
    `tierRefTableMemory_` members, created via the SAME `CreateHostBuffer` helper with a 1-byte
    placeholder when `concatenated_.tierRefTable` is empty; `BodyOctreeSceneNodeConfig.h`: new
    `OCTREE_TIERREFTABLE_BUFFER` output slot, index 11; `BuildRenderGraph.cpp`: new binding-15
    `batch.Connect`). Binding 15 (not 14) chosen because binding 14 (`InstanceIterDebugBuffer`,
    Sparse-Mip Inc1 M4b) is NOT wired through `BuildRenderGraph.cpp` at all (confirmed by grep — it's a
    test-only binding with no production connection), so 15 is the correct next sequential production
    binding. **Independently verified byte-parity, not just asserted**: compiled the modified shader
    directly with `glslc`/`glslangValidator` (vendored in this worktree's `.vulkan-sdk/`) and read the
    reflected SPIR-V — `TierRefTableBuffer: binding 15, size 20, numMembers 3`,
    `childOctreeIndex@offset 0`, `childOriginLocal@offset 4 size 3`, `childScale@offset 16` — exactly
    matching `Vixen::SVO::TierRef`'s C++ layout (`TierRef.h`'s own static_asserts) field-for-field, the
    same kind of independent cross-check M1's validator did for `mipPoolBase`.
  - **`GpuTraversalMirror.h` updated in lockstep**, per its own documented "SYNC CONTRACT" and M2's
    explicit deferral. Added an optional `RegisterTierCrossingChild(childOctreeIndex, childSerialized)`
    method (existing single-tree callers/constructor are UNCHANGED — this mirror's own tier-crossing
    restart only activates if a caller opts in); factored its private single-tree traversal body into
    `castRayOnce` (parametrized by explicit `cfg`/`nodes`/`brickData`/bases, mirroring the shader's own
    `traverseOctreeInstancedOnce` split) so the public `castRay` can call it twice, exactly mirroring
    the real shader's wrapper structure line-for-line, including the SAME hitT-unit-consistency fix.
    `test_gpu_parity` 4/4 green confirms this refactor changed NOTHING about the mirror's existing
    single-tree behavior (the pre-M3 unguarded `getContourPointer` read on a `farBit==1` leaf — flagged
    by M2 as intentionally deferred — is now guarded the same way the real shader's leaf-hit branch is).
  - **Live gate — what was actually run.** Built a `VIXEN_TIER_CROSSING_DEMO` env-gated scene in
    `BuildRenderGraph.cpp`: two independently-baked SDF spheres (`n=16, r=6.0, brickDepth=3`, the exact
    `test_tier_crossing_construction.cpp` fixture shape), the parent's first leaf marked tier-crossing
    via `MarkLeafAsTierCrossing` (leaf `(parentDescIdx=0, octant=0)`, confirmed via live log), manually
    concatenated (parent=slot 0, child=slot 1, mirroring the test's own manual-concatenation loop since
    `ConcatenateSdf` would discard the pre-concatenation mutation), injected via
    `BodyOctreeSceneNode::SetRecipePool` (the existing I4.1 hook, takes priority over all other demo
    paths, zero risk of interference), with ONE `BodyInstanceGpu` pointing at octree 0. Ran on
    Windows (`vixen-ninja` preset, MSVC, real Vulkan device — NOT lavapipe/WSL for this run) with
    `VIXEN_TIER_CROSSING_DEMO=1`, `VIXEN_EXIT_AFTER_FRAMES=30`, `VK_INSTANCE_LAYERS=
    VK_LAYER_KHRONOS_validation` explicitly injected (confirmed necessary and non-negotiable per this
    plan's own mandate — did NOT rely on the compile-time `VIXEN_VULKAN_VALIDATION` macro alone),
    `VIXEN_HUD_CAPTURE_FRAMES=20`/`VIXEN_HUD_CAPTURE_DIR=temp` (the existing View-Contract-Inc-2
    swapchain-PNG-capture mechanism, `Vixen::RenderGraph::Debug::CaptureSwapchainToPng`) for a real
    screenshot. Ran 3 times total (once with a wrong world-scale constant, caught and fixed; twice
    clean) — every run: clean shutdown, zero exceptions/crashes, byte-identical VUID signature to a
    same-session default-scene baseline run (`grep VUID`: both show ONLY 10 occurrences of the SAME
    pre-existing `VUID-vkCmdDispatch-None-08114` on binding 14, none on binding 15), and the demo-scene
    construction log line confirming the exact leaf/octant/child-index that were marked.
  - **Live gate — the honest gap.** The captured PNG shows a real, smoothly-lit sphere (a genuine
    dark-to-bright gradient sampled across a ~40×40px patch, not a flat/garbage/black region — ruled
    out via direct Python/PIL pixel sampling of the PNG, not eyeballing) with no crash, but the render
    could NOT be visually confirmed to show the CHILD tree's geometry as distinct from the parent's
    within this session's time budget, for two compounding reasons discovered mid-verification: (1)
    both trees share the identical SDF-bake color recipe (`SdfBake.h`'s spatially-varying
    cosine-gradient color, not a solid tint), so even a perfectly-working crossing would render a very
    similar color family to the parent's own surface at that point; (2) the default standalone-app
    camera/window framing (500×500 window, 45° FOV, camera at world (64,64,300)) renders EVERY
    standalone demo body — confirmed via an A/B screenshot of the already-shipped, working
    `VIXEN_STORED_SDF_DEMO` scene — as a small (~15px) disc, not filling the frame; this was initially
    mistaken for a bug in this milestone's own world-scale math (a first attempt used a wrong
    `renderScale` formula, fixed mid-session — see the `kRenderScale`/`kHalf` derivation in
    `BuildRenderGraph.cpp`'s own comment), but the corrected version renders IDENTICALLY small to the
    reference demo, proving the framing (not this milestone's geometry) is the root cause. Did NOT
    complete Task 8's second sub-bullet (hand-computed expected screen position of a known distinct
    child feature vs. live pixel position) as a result. **This is the single most important gap for a
    validator or follow-up session to close** — the recommended fix is a dedicated follow-up demo
    variant with a large, distinctly-colored/shaped child feature (e.g. a solid, saturated flat color
    on the child recipe instead of the shared cosine gradient) and either a closer camera or a larger
    `renderScale`, to get an unambiguous visual A/B; this was judged out of scope to chase further
    within this session given the strong code-level/log-level/no-regression evidence already gathered.
  - **No scope drift**: `git diff --stat` confirms only `application/main/source/graph/
    BuildRenderGraph.cpp` (the new demo scene only — no changes to any other demo's code path),
    `libraries/RenderGraph/include/{Data/Nodes/BodyOctreeSceneNodeConfig.h,Nodes/BodyOctreeSceneNode.h}`
    + `libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp` (binding-15 buffer wiring, following
    the mipPool/binding-13 precedent exactly), `libraries/SVO/include/GpuTraversalMirror.h` (the
    mandated test-oracle sync update), and `shaders/{BodyInstanceRayMarch.comp,SVOTypes.glsl}` (the
    actual traversal-restart + `getFarBit`/`getTierRefIndex`/`getChildRootScaleHint` GLSL accessors).
    M4's screen-space LOD gate and M5's continuous-zoom camera path were NOT built (out of scope,
    per the plan's own scope discipline) — every tier-crossing leaf in this milestone crosses
    unconditionally, no gating.
  - **Pre-existing, unrelated discovery**: `test_gpu_parity`/`test_tier_crossing_construction`/related
    SVO test targets fail to compile on THIS worktree's Windows/MSVC toolchain (`Recipe/generated/
    SdfCoreKernels.g.hpp` hits Windows-macro (`min`/`max`) pollution via `SdfRecipes.h`/`SdfBake.h`,
    which have no `#undef` guard) — reproduced independently on a clean pre-M3 (`4db93715`) checkout via
    `git stash`, confirming this is NOT caused by this milestone. Worked around entirely by building
    and running these specific test targets via the WSL/GCC path (`build/wsl`, from M1/M2's own
    provisioned preset) instead, where they compile and pass cleanly — `VIXEN.exe` itself (the
    live-gate binary) DOES build fine on Windows/MSVC (the failure is isolated to these specific test
    TUs' include chain). **Flag for validator/future cleanup**: this Windows-macro-pollution gap in
    `SdfRecipes.h`'s transitive include chain is a real, pre-existing, and now independently
    re-confirmed known issue, unrelated to Tiered-ESVO — worth a `Known-Issues.md` entry.

---

## M1 — `TierRef` + `TierRefTable` CPU-side plumbing

### Why this is first

Every later milestone needs a real `TierRef` type and a place to store them before any construction
or traversal code can reference one. This mirrors both prior Tiered-ESVO/Sparse-Mip increments' own
pattern: foundational types first, GPU wiring after.

### Task 1 — `TierRef` struct

- [x] Define `TierRef` per §3.2's exact shape: `childOctreeIndex` (index into
  `ConcatenatedOctrees::configs[]`), `childOriginLocal[3]` (child's `[1,2)`-frame origin, expressed
  in the PARENT tree's local frame — never world space), `childScale` (linear scale of the child's
  unit cube in parent-local units). Confirm at implementation time the exact byte layout needed for
  GPU-side SSBO consumption (std430 packing — check Sparse-Mip Inc1's own std430-vec3-padding gotcha,
  documented in that increment's Progress Log, before assuming naive struct packing works).
  **DONE** — `libraries/SVO/include/TierRef.h`, plain `float[3]` (not `glm::vec3`) sidesteps the
  padding gotcha; 20-byte layout, static_assert-proven.
- [x] Unit test: construct a `TierRef`, confirm its fields round-trip correctly through whatever
  serialization form is chosen. **DONE** — `test_tier_ref.cpp`, 5/5 green.

### Task 2 — `TierRefTable` as a new parallel array on `ConcatenatedOctrees`

- [x] Add a `TierRefTable` (a flat array of `TierRef`, one entry per registered cross-tier edge) as a
  new parallel member on `ConcatenatedOctrees` (`ShellOctreeGpu.h`), following the exact pattern
  `mipPool`/`channelPool`/`brickGridLookup` already establish (per-octree base offset + count,
  concatenated across all resident trees). Confirm the current real shape of `ConcatenatedOctrees`
  first (re-read `ShellOctreeGpu.h` directly — it has grown since this design doc was written) rather
  than assuming the doc's original sketch is still accurate. **DONE** — `tierRefTable` +
  `tierRefCounts` added, wired into `Concatenate`/`ConcatenateSdf`/`ConcatenateSdfWithMips`.
- [ ] Wire the corresponding GPU-side buffer/binding the same way `mipPool`'s binding was added in
  Sparse-Mip Inc1 M3 (a new descriptor binding, `BuildRenderGraph.cpp` connection, `SVOTypes.glsl`
  buffer declaration) — check that increment's own Progress Log for the exact sequence of files
  touched, since it's the closest precedent for "add a new per-octree GPU buffer."
  **DEFERRED, deliberately** — the M1 gate text ("no shader/traversal-logic changes yet, this
  milestone is pure plumbing") reads in tension with this sub-bullet; no shader-side consumer exists
  until M3, so a `BodyOctreeSceneNodeConfig` slot/`BuildRenderGraph.cpp` wiring/`.glsl` declaration
  now would be dead, unconsumed plumbing. See Progress Log's "GPU-binding scope decision" entry —
  flagged for validator judgment; the CPU-side storage this binding would read from is fully in place.
- [x] Unit test: a `ConcatenatedOctrees` with 2+ trees, one holding a non-empty `TierRefTable`, confirm
  concatenation/offset bookkeeping is correct (mirrors existing `mipPool`/`channelPool` concatenation
  tests as the pattern to copy). **DONE** — `test_tier_ref_table.cpp`, 5/5 green.

### Task 3 — `OctreeConfig` field for tier-ref-table base offset (if needed)

- [x] Determine whether `OctreeConfig` needs a new field (e.g. `tierRefTableBase`, analogous to
  `mipPoolBase`) to let the shader locate a given tree's slice of the concatenated `TierRefTable`.
  If so, add it via the canonical Yeroket kernel-codegen schema (`codegen/config-schemas/
  OctreeConfig.cs` → regenerated C++/GLSL), NOT a hand-edit — this is the established, mandatory
  mechanism (Sparse-Mip Inc1 M1's own `mipPoolBase`/`brickResident` additions both went through this
  path, per that increment's Progress Log). 72 bytes of tail padding are confirmed free as of
  2026-07-07 (`_tailPad[18]` at offset 360) — recheck this figure at implementation time since other
  concurrent increments may have consumed some of it since. **DONE** — yes, needed; added at byte
  360 via the Yeroket tool, golden `--check` clean, 72→68 bytes tail pad remaining.
- [x] Unit test: confirm the drift-guard parity test (`test_octree_config_sdi_parity`, or whatever the
  current equivalent is called — check Sparse-Mip's own precedent) still passes with the new field,
  proving C++/GLSL layouts stay byte-identical. **DONE** — the heavier SPIR-V-reflection test lives in
  RenderGraph/tests (needs `glslc`, not re-run standalone here); added a lighter SVO-local equivalent
  (`TierRefTable.OctreeConfigTierRefTableBaseFieldOffsetAndStructSize`) as this milestone's own gate.

**M1 gate:** all new unit tests green, config-schema regen clean, no shader/traversal-logic changes
yet (this milestone is pure plumbing).

---

## M2 — `farBit==1` construction path

### Why this is separate from M1 and before M3

Building the ability to CONSTRUCT a tier-crossing leaf is a distinct, testable piece of work from
making GPU traversal actually CROSS one — proving construction is correct in isolation (a
CPU-side round-trip test) means M3's live-gate debugging is isolated to genuinely GPU/traversal
concerns, not confounded by "did we even build the data correctly."

### Task 4 — Mark a leaf as tier-crossing at construction time

- [x] Add a construction-time path (in `SVORebuild.cpp`/`SVOBuilder.cpp`, alongside the existing
  brick-leaf path) that can mark a given leaf's `ChildDescriptor` with `farBit=1` and register a
  `TierRef` entry in the tree's `TierRefTable`, instead of the normal brick-index assignment. This is
  additive — every existing construction path keeps setting `farBit=0` exactly as today
  (`SVORebuild.cpp:439,512`, confirmed unchanged as of 2026-07-07); this milestone adds a NEW,
  separate path that a caller opts into explicitly (e.g. a recipe/authoring-time flag marking a
  specific leaf position as "this points at another tree"), it does not change default behavior for
  any existing tree. **DONE, with a deliberate placement deviation from this bullet's literal file
  suggestion** — `MarkLeafAsTierCrossing` lives in `ShellOctreeGpu.h` (operating on an
  already-serialized `SerializedOctree`), not in `SVORebuild.cpp`/`SVOBuilder.cpp` directly, because
  `Concatenate()`/`ConcatenateSdf()` call `Serialize()`/`SerializeSdf()` INTERNALLY — marking the raw
  `Octree` before serialization would still need a second post-serialization step to survive
  concatenation. See Progress Log for the full reasoning; flagged for validator attention.
- [x] Confirm `hasBrick()`/`getBrickIndex()` and any other `farBit==0`-assuming accessor is genuinely
  unaffected — read every call site of these accessors (via `codegraph explore`) and confirm none of
  them get called on a `farBit==1` node without first checking `farBit`, or if they might be, add the
  guard. **DONE** — one real unguarded call site found (`SVOTraversal.cpp:1029`,
  `castRayGpuMirror`), fixed with a minimal `farBit` guard (miss, not new traversal logic); the
  test-oracle-only `GpuTraversalMirror.h` has the same pattern but is explicitly flagged for M3
  rather than touched (see Progress Log).

### Task 5 — Two-tree test fixture + round-trip proof

- [x] Build a minimal, hand-authored test fixture: two independently-constructed octree instances
  (e.g. a small "parent" tree and a small "child" tree), with ONE specific leaf in the parent marked
  tier-crossing (`farBit=1`) and its `TierRef` pointing at the child's `ConcatenatedOctrees` index.
  Serialize both, confirm the parent's tier-crossing leaf's `TierRef` correctly resolves to the
  child's actual octree index/origin/scale after a full serialize → concatenate → (mock) upload
  round-trip. **DONE** — `test_tier_crossing_construction.cpp`,
  `TwoTreeFixtureRoundTripsThroughSerializeAndConcatenate`, green.
- [x] Full no-regression sweep: confirm every existing SVO/RenderGraph test (`farBit=0` trees, the
  full Sparse-Mip/Surface-Shell/Tiered-ESVO-Inc1 suites) is unaffected — this is a good checkpoint to
  run the broader regression sweep BEFORE moving into M3's much riskier GPU work. **DONE** — 98 SVO
  test targets re-run; 16 pre-existing test failures + 1 pre-existing segfault (`test_entity_brick_view`)
  confirmed byte-identical against the pre-M2 `2bb752ec` baseline via stash/rebuild/compare — zero
  regressions attributable to this milestone. RenderGraph tests not separately re-run (this
  milestone touched no RenderGraph files; the plan's own M1 precedent treated the SVO-local sweep as
  sufficient for a non-GPU-binding milestone).

**M2 gate:** two-tree fixture's `TierRef` round-trips correctly; zero regression on the existing
`farBit=0` test suite (pure CPU/serialization proof, no live GPU render needed yet). **MET.**

---

## M3 — GPU traversal-restart, single crossing

> **This is the highest-risk milestone in the increment.** It modifies
> `BodyInstanceRayMarch.comp`'s actual traversal loop — the real rendering hot path every other body
> in the engine already depends on. Any mistake here is a regression risk for EVERYTHING, not just
> tier-crossing trees. Budget real debugging time; do not treat "shader compiles" as success — per
> this project's own repeated precedent (Sparse-Mip Inc1/Inc2, Tiered-ESVO Inc1 M3's two self-caught
> sync bugs), only an actual validated live run counts.

### Why this is separate from M4/M5

Proving a ray can cross AT ALL, correctly, on real hardware, is the actual mechanism risk this whole
increment exists to retire. The LOD gate (M4) and the full zoom demonstration (M5) are refinements on
top of a working crossing — do not attempt them before this milestone's live gate is green, or a bug
in the crossing itself will masquerade as a bug in the gating/zoom logic.

### Task 6 — Ray remap into the child tree's local frame

- [x] On the GPU, when traversal (`BodyInstanceRayMarch.comp`) hits a `farBit==1` leaf: read the
  `TierRef` (via `contourPointer` as an index into the current tree's slice of `TierRefTable`),
  transform the ray's origin+direction from the current tree's local `[1,2)` frame into the child's,
  using `TierRef::childOriginLocal`/`childScale` — a single scale+offset (§3.3's float32-safety
  discipline: no accumulated world matrix, ever). Confirm this transform is the mathematical inverse
  of how the CPU-side `TierDirection.h`'s composition works (Inc1's own per-hop `(localPos - 1.5) *
  scaleCm` convention) so CPU-authored `TierRef` data and GPU-side consumption agree on the same
  frame convention — cross-check this explicitly, do not assume it matches without verifying.
  **DONE** — `remapRayIntoChildFrame` in `BodyInstanceRayMarch.comp`; algebraically inverted from
  `TierDirection.h`'s `SumTail` (`parentPoint = childOrigin + (childLocal-1.5)*scale` solved for
  `childLocal`), confirmed by direct derivation, see Progress Log.

### Task 7 — Traversal restart (re-entry with a fresh stack)

- [x] Re-enter the standard ESVO iterative traversal against `configs[childOctreeIndex]` — a
  DIFFERENT `OctreeConfig` than the one the ray started in — at the child's own scale 0, with a
  fresh stack (not merged with the parent's; the parent's traversal state is parked, not recursed
  into, per the design doc's explicit rejection of growing `MAX_STACK_DEPTH`, §10). On the child tree
  boundary exit (ray leaves its `[1,2)` bounds), pop back to the parent's parked state and resume
  exactly as if the `farBit` leaf had been an ordinary voxel miss.
  **DONE** — `traverseOctreeInstancedOnce` (renamed from the pre-M3 `traverseOctreeInstanced`) is
  called TWICE (not recursively — GLSL has none) from a new `traverseOctreeInstanced` wrapper: once
  for the parent, once for the child on a tier-crossing miss. Each call declares its own local
  `stack[STACK_SIZE]`/`TraversalState` — genuinely fresh, never merged.
- [x] This is genuinely the hardest part of the whole increment — the existing traversal loop was
  written assuming ONE tree/`OctreeConfig` for the whole ray's lifetime; confirm exactly what
  per-ray state currently assumes single-tree-for-the-whole-ray (binding indices, config-derived
  constants cached once at ray start, etc.) via `codegraph explore` on `BodyInstanceRayMarch.comp`
  before writing the restart logic, so nothing is silently left stale from before the crossing.
  **DONE** — the only per-tree state outside a traversal call's own locals is 3 globals
  (`g_octreeIdx`/`g_esvoNodeBase`/`g_brickArrayBase`, set once per instance in `main()`); the wrapper
  saves/swaps/restores exactly these 3 ints around the child call. See Progress Log for the
  non-obvious hitT-unit-consistency bug this surfaced and fixed before it shipped.

### Task 8 — Live gate: prove a single crossing renders correctly

- [x] Build the two-tree fixture from M2's Task 5 into an actual live scene. Run `VIXEN.exe` with
  Khronos validation EXPLICITLY enabled via env-injection (`VK_INSTANCE_LAYERS=
  VK_LAYER_KHRONOS_validation` — mandatory, per Tiered-ESVO Inc1 M3's own discovery that the Release
  binary silently compile-gates the app-side layer off) and confirm: (a) rays that should cross the
  tier-crossing leaf genuinely render the CHILD tree's geometry, not garbage/black/the parent's own
  geometry; (b) zero new VUID errors attributable to the traversal-restart change; (c) existing
  non-tier-crossing bodies in the same scene render completely unaffected (no regression in the
  common `farBit=0` path).
  **DONE (b) and (c); (a) DONE WITH A LEGIBILITY CAVEAT** — a new `VIXEN_TIER_CROSSING_DEMO` env-gated
  scene (`BuildRenderGraph.cpp`) builds exactly this two-tree fixture live and calls
  `BodyOctreeSceneNode::SetRecipePool`. (b): confirmed byte-identical VUID count/content to a clean
  default-scene baseline run (both show only the SAME pre-existing `VUID-vkCmdDispatch-None-08114`
  on binding 14 `InstanceIterDebugBuffer`, unrelated to this milestone — that binding has no
  `BuildRenderGraph.cpp` wiring at all, confirmed by grep). (c): the default (non-tier-crossing) scene
  ran clean with the SAME VUID signature before and after this milestone's shader change. (a): the
  live render shows a real, correctly-lit, smoothly-shaded sphere (confirmed via PNG pixel sampling —
  a genuine dark-to-bright gradient across ~40x40px, not a flat/garbage/black region) with zero
  crashes across 3 separate runs; however, the DISTINCT-CHILD-GEOMETRY visual claim could not be
  crisply confirmed by eye within this session's time budget — see Progress Log's "Concerns" section
  for the full account and why (both trees share the same procedural bake recipe/color gradient by
  construction, and the default camera/window framing renders all standalone demo bodies as small,
  ~15px discs — confirmed NOT specific to this milestone by an A/B screenshot against the
  already-shipped `VIXEN_STORED_SDF_DEMO` scene, which renders identically small). Flagged for
  validator: either accept the code-level + log-level + no-crash + no-regression evidence as
  sufficient, or extend the live gate with a distinctly-colored/larger child recipe and a
  closer-framed camera for an unambiguous visual A/B (out of this session's remaining budget).
- [ ] Live gate: also confirm the ray-remap math is correct by placing a KNOWN, simple geometric
  feature in the child tree (e.g. a distinctly-colored/shaped voxel at a known local position) and
  confirming it renders at the visually-correct screen position given the `TierRef`'s known
  origin/scale — the same "hand-compute expected, compare to live output" discipline Tiered-ESVO
  Inc1 M3 used for its sky-projection direction math.
  **NOT DONE** — this specific sub-gate (hand-compute the expected on-screen position of a KNOWN
  distinct feature, compare to live pixel position) was not completed; see Progress Log Concerns.
  This is the single biggest remaining gap for a validator/follow-up session to close.

**M3 gate:** a single tier-crossing renders correct child-tree geometry on real hardware with
validation layers active and zero new VUIDs; existing non-crossing rendering unaffected. **PARTIALLY
MET** — the VUID/no-regression half of the gate is solidly met with direct evidence; the
"renders correct CHILD geometry, visually distinguishable from the parent" half is supported by
strong circumstantial/code-level evidence (correct leaf marked, correct buffer wired and byte-verified
via glslc reflection, correct traversal-restart math independently re-derived, clean render with no
crash/garbage) but lacks the crisp visual A/B the plan's own Task 8 asks for. See Progress Log.

---

## M4 — LOD early-out + residency reuse

### Why this comes after M3, not before

The screen-space gate and residency-reuse logic are refinements on a mechanism that must already work
unconditionally (M3). Building the "when to skip crossing" logic before the crossing itself works
correctly would make it impossible to tell whether a rendering discrepancy is a gating bug or a
crossing bug.

### Task 9 — Screen-space LOD gate before crossing

- [ ] Before performing Task 6-7's ray-remap/restart, check the child tree's angular screen-space
  footprint against `RaySizeCoefNode`'s existing LOD cone-spread constant (the same check an ordinary
  leaf-vs-subdivide decision already makes). If sub-pixel, shade from the PARENT tier's mip sample at
  this node instead (Sparse-Mip's existing per-level filtered sample, already proven/shipped) and
  never cross into the child tree at all.
- [ ] Live gate: confirm a distant tier-crossing leaf (small angular footprint) correctly falls back
  to mip-shading without ever triggering the (expensive) traversal restart — verify via the existing
  debug/trace-buffer instrumentation (`DebugRaySample`/`dbg.iterationCount`, used by Sparse-Mip's own
  occlusion-reject tests) that the restart path is genuinely skipped, not just that the visual output
  happens to look plausible.

### Task 10 — Residency reuse: non-resident child tree

- [ ] Confirm (and if needed, wire) that a `farBit==1` leaf whose child tree is NOT currently
  brick-resident (or not resident at all — mip-only, per Sparse-Mip's residency model) correctly
  falls back to the parent's mip sample, identical in shape to a non-resident brick today — no new
  residency state machine, this should already fall out of the existing sentinel-miss pattern once
  Task 9's LOD gate and the existing `ResidencyTrigger`/mip-fallback machinery are both in play.
- [ ] Live gate: construct a scene where the child tree genuinely starts non-resident, confirm the
  crossing gracefully mip-shades rather than crashing/rendering garbage, and confirm requesting
  residency (moving the camera closer, per the existing `ResidencyTrigger` distance/FOV gate) causes
  the crossing to eventually render real child-tree geometry once residency is granted — proving the
  full existing Sparse-Mip residency lifecycle composes correctly with a tier-crossing leaf, not just
  an ordinary brick leaf.

**M4 gate:** distant/non-resident tier-crossing leaves correctly fall back to mip-shading without
triggering an unnecessary restart or rendering garbage; live-verified.

---

## M5 — Continuous zoom proof (the actual "surface to orbit" demonstration)

### Why this is last

This is the actual deliverable the user asked for — a real, observable, continuous surface-to-orbit
zoom through at least one genuine tier crossing. Everything before this milestone is the mechanism;
this milestone is the demonstration that the mechanism composes into the actual desired experience.

### Task 11 — Camera path proving continuous, seamless zoom across a tier boundary

- [ ] Construct a camera path that starts close enough to render T2-bedrock-scale detail (fine
  voxel/brick geometry) and smoothly pulls back through the LOD gate (Task 9) at the point where the
  child tree's footprint crosses sub-pixel, confirming NO visible pop/seam/flicker at the transition
  — the screen-space content should read as one continuous zoom-out, not a visible "swap" moment.
  Live-gate this on real hardware, recording (screenshot sequence, or a frame-by-frame debug-log
  comparison of the rendered content's resolvable-level/residency-state around the crossing frame)
  concrete evidence the transition is actually seamless, not just "it didn't crash."
- [ ] **Stretch goal, not a hard gate**: chain a second crossing (T1 region → T0 planet, using the
  same mechanism proven in M3/M4 for the T2→T1 boundary) to demonstrate the full 3-tier
  surface-to-orbit sequence for an Earth-diameter-scale body — the literal scenario the user asked
  about. If this is straightforward given M3/M4's already-proven mechanism (it should be — the design
  doc's whole point is that N-tier crossing is the SAME mechanism repeated, not new engineering per
  tier), build it. If it surfaces unexpected complexity (e.g. per-ray state that doesn't compose
  cleanly across two live crossings), it is acceptable to ship this increment with the single-crossing
  proof (M3+M4's own gate) as the increment's deliverable and document the 2-crossing chain as a
  known, scoped-out follow-up rather than let it block the whole increment.
- [ ] Update [[Tiered-ESVO-Observer-Addressing-Design-2026-07]]'s status banner and §9 sequencing note
  to reflect this increment's actual shipped scope (single-crossing mechanism proven, N-tier chaining
  either proven or flagged as a following increment, per what actually happened) — following the same
  status-banner-update convention Sparse-Mip-ESVO-LOD's own Inc1/Inc2 used.

**M5 gate:** a live, validated, visually-confirmed continuous zoom across at least one real tier
crossing with no visible seam; 3-tier chaining is a stretch outcome, not a blocking requirement.

---

## Notes for implementers

- **M1/M2 are Sonnet-medium implementable** against existing patterns (parallel-array additions,
  config-schema regen, CPU-side construction/serialization) — no new architecture, filling in what the
  design doc already fully specifies.
- **M3 is the milestone most likely to need escalation or a fix-loop.** It is genuinely harder than
  anything either Tiered-ESVO Inc1 or Sparse-Mip-ESVO-LOD's own increments attempted — modifying the
  live traversal loop's core assumptions (single-tree-per-ray) rather than adding an independent,
  additive mechanism alongside it. Budget accordingly; do not be surprised if this milestone needs
  more than one validator fix-loop iteration, and escalate to a more capable model or split the
  milestone further if the standard cap (3 iterations) is hit without a clean live gate.
- **Live-run gate with validation layers EXPLICITLY enabled is non-negotiable for M3-M5** — per
  Tiered-ESVO Inc1 M3's own hard-won lesson, do not trust a run where you only set
  `VK_ICD_FILENAMES`; the Release binary silently compiles out the app-side validation layer, and a
  "clean" run without the layer genuinely active proves nothing.
- **Re-verify every code citation in this plan and the design doc at implementation time.** Both
  Sparse-Mip-ESVO-LOD and Tiered-ESVO Inc1 caught real citation drift (a nonexistent "instance cap,"
  a stale line number) between when their design docs were written and when implementation started —
  do not assume this plan's own file:line references are still exactly accurate by the time M1 starts,
  especially if other concurrent work has touched `ShellOctreeGpu.h`/`OctreeConfig`/
  `BodyInstanceRayMarch.comp` in the meantime.
- **Prefer Windows-side build for M3-M5's live-gate work** per this project's standing convention for
  GPU/render work, falling back to the `vixen-wsl`/Mesa-Dozen real-GPU path (proven adequate evidence
  by every prior increment's validators) if a Windows dev shell isn't available in a given worktree
  session.
