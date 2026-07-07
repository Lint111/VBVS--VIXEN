# Tiered ESVO — Increment 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh Sonnet-medium implementer + Opus-high validator
> per milestone, worktree-isolated, progress persisted in this doc). **Live-run gate is authoritative
> for the composite-draw milestone (M3)** — static review has repeatedly passed runtime bugs on this
> project (Sparse-Mip-ESVO-LOD Inc1/Inc2 precedent); M3 ends in an actual `VIXEN.exe` run, not just a
> clean compile. M1/M2 are pure-CPU, gtest-verified, no GPU dependency.

**Goal:** Ship the "nearest actionable slice" [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §9
names explicitly: **address-level sky projection for static content** — a `TierAddress` type (the
short hop-chain identifying any cell/object across tiers, §4 of that doc) plus a new sky-projection
node that consumes addresses and composites direction+magnitude points into the existing skybox
render target. This is the observation-post fleet-detection use case's **first step only** (§7 step 1:
"For every candidate object at or above the observer's tier... render as a point/disk in the skybox")
— evaluated at the address level, no ray-marching, no tier-crossing traversal.

**Explicitly NOT this increment** (per the design doc's own §9 sequencing and this plan's own scope
cut): the tier-crossing leaf reference (`farBit==1` payload, §3), the `TierRef`/`TierRefTable` GPU
data, the traversal-restart algorithm (§5.1), and the zoom-driven LOD handoff into real geometry (§7
steps 2-3). Those all require a resident child tree with actual octree content to cross into — the
design doc is explicit that step 1 alone needs `TierAddress` and a sky-projection node, "not §5's
tier-crossing traversal restart, since a point-of-light skybox entry doesn't require ray-marching into
the referenced tree." Building §5 before anything exercises §4's address type in a real render loop
would be exactly the kind of speculative build the Sparse-Mip-ESVO-LOD epic's own Inc2 M4 milestone
warned against (see that plan's "Concrete triggers" section) — prove the address type and the
composite-draw path first, on the cheapest possible consumer.

**Architecture:** A small, standalone `TierAddress` CPU type (hop-chain of tier indices, §4) plus pure
functions to derive sky-relative direction/magnitude from two addresses sharing a prefix (§4's
"shared-prefix = shared ancestor" rule) and the existing per-tier scale math already established in
[[Sparse-Mip-ESVO-LOD-Direction-2026-07]]'s tier derivation. A new `SkyProjectionNode`
(`RenderGraph`) uploads a small per-object SSBO (direction, magnitude, optional light-delay term) and
draws point/disk sprites into the existing skybox/background target via a `Load`-op render pass
stacked over it — structurally identical to `BuildRenderGraph.cpp`'s existing
`ui_composite_render_pass`/`UIRenderNode` composite pattern (a `RenderPassNode` set to `Load` instead
of `Clear`, feeding a small vertex+fragment shader pair, not a compute dispatch), not the
`BodyInstanceRayMarch.comp`-style full ray-march compute path Sparse-Mip-ESVO-LOD's own increments
used — this consumer has no octree traversal at all.

**Tech Stack:** C++23, GLSL vertex+fragment (point-sprite composite, std430 SSBO), glm, GoogleTest,
CMake ninja/wsl presets, Vulkan 1.3.

**Reuses:** `ChildDescriptor`'s existing 8-byte layout and `farBit` sentinel (present, unused — not
touched this increment, only confirmed still available for a later increment); `ConcatenatedOctrees`'
established "add a new parallel array" pattern (`ShellOctreeGpu.h`, already exercised twice by
Sparse-Mip-ESVO-LOD Inc1/Inc2 for `mipPool`/`channelPool`/`brickGridLookup` — not touched this
increment, since M1 scope has no GPU octree data at all); `RaySizeCoefNode`'s LOD cone-spread constant
(consumed conceptually for the eventual §5.2 zoom handoff, not this increment — no dependency here);
`UIRenderNode`'s composite-over-existing-target render-pass shape (`BuildRenderGraph.cpp`'s
`ui_composite_render_pass`/`ui_composite_framebuffer`/`ui_composite_render`, `PARAM_COLOR_LOAD_OP =
Load`) as the direct structural template for M3's new render pass.

**Design of record:** [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] (full spec, §1-§11).
**Parent epic:** [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] (base mip-sampling/bandwidth mechanism this
doc generalizes upward — merged to `main`, Inc1 `ae12ba78` + Inc2 `2351baff`, both COMPLETE).

---

## §0. Scope

**In scope for this increment:**
- `TierAddress` type (§4): a hop-chain identity, plus shared-prefix comparison and per-hop local-frame
  composition (direction + distance between two addresses, composed only through the shared-prefix
  ancestor — never a flattened world coordinate, per §4/§3.3's precision discipline).
- The concrete tier math this increment's `TierAddress` instances actually use: reuse
  [[Sparse-Mip-ESVO-LOD-Direction-2026-07]]'s T0/T1/T2 tier sizing, generalized upward per this
  design doc's §1 (system, galaxy tiers) — CPU-side only, no new GPU tier data.
- A new `SkyProjectionNode`: CPU-side per-object direction+magnitude computation from
  `TierAddress` pairs, small SSBO upload, point/disk-sprite vertex+fragment shader, composited into
  the existing skybox/background render target via a `Load`-op pass.
- A synthetic/test-harness way to exercise this end-to-end (a handful of hand-placed `TierAddress`
  "fleet" objects at varying tier depths and angular positions) — **not** a real undertow-fed data
  source; that integration is explicitly deferred (§ "Out of scope" below).

**Out of scope for this increment** (per [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §9 and
§0's own cut above):
- The tier-crossing leaf reference, `TierRef`/`TierRefTable`, and any `ChildDescriptor` bit-layout
  change (§3). `farBit` stays `0` everywhere, unchanged.
- The traversal-restart algorithm (§5.1) and the LOD-driven zoom handoff into real ray-marched
  geometry (§5.2, §7 steps 2-3). This increment renders points, never crosses into a child tree.
- Any undertow-side reification, delta-log, or `Undertow.View` schema work (§6, §11's "open decision"
  on wire format) — this increment's `TierAddress` is VIXEN-internal and synthetic; reconciling it
  with undertow's eventual schema is future work, tracked in [[undertow-vixen-integration-map]].
- `TierRefTable` upload shape / `OctreeConfig` tail-byte allocation (§11) — moot until a later
  increment actually builds §3's tier-crossing leaf reference.

**Why this is a coherent, shippable increment on its own:** per the design doc's own words, step 1 of
the driving use case (§7) "needs no tier-crossing traversal at all — it's evaluated at the address
level, not ray-marched." A working sky-projection consumer end to end (address → direction/magnitude
→ composited skybox point) is a complete, demonstrable, gate-able piece of the eventual fleet-detection
feature, and it is the cheapest possible way to prove out `TierAddress` (§4) in a real render loop
before any GPU-side tier-crossing machinery (§3/§5) is built against it.

---

## Milestone Map

- **M1 — `TierAddress` type + tier math** (Tasks 1-2) · gate: pure-CPU gtest green, no GPU dependency
  (pattern per `tests/Data/` + a plain `.cmake` registration, not `test_critical_nodes.cmake` — see
  Notes for implementers). ·
  **✅ DONE 2026-07-07** — commit `709bb639` (worktree `feat/tiered-esvo-inc1`, branched from `main`).
  `test_tier_address` 16/16, `test_tier_math` 9/9, both green. Opus-validated APPROVED (independent
  from-scratch math re-derivation confirmed). See Progress Log for representation choice + the
  tier-math re-derivation.
- **M2 — Direction/magnitude derivation** (Tasks 3-4) · gate: pure-CPU gtest green — shared-prefix
  composition, apparent-magnitude falloff, optional light-delay staleness term.
- **M3 — `SkyProjectionNode` + live composite gate** (Tasks 5-7) · gate: shader compiles + a live
  `VIXEN.exe` run showing synthetic fleet points correctly composited over the existing skybox/voxel
  render, at the correct screen-space direction for a known observer/object address pair.

### Progress Log

- **Milestone M1 (Tasks 1-2): DONE** · commit `709bb639` (worktree `feat/tiered-esvo-inc1`, branched
  from `main`) · gates: `test_tier_address` 16/16, `test_tier_math` 9/9, both green, pure CPU (no
  Vulkan/GPU dependency exercised by either binary) · 2026-07-07.
  - **`TierAddress` representation**: fixed-capacity inline array (`std::array<uint32_t,
    kMaxTierAddressDepth=8>` + a running `depth_`), not `std::vector<uint32_t>` as the design doc's §4
    sketch literally wrote. Chosen because the type is compared (shared-prefix) and copied (per-object,
    per-frame, potentially many fleet objects) frequently, and the concrete depth this increment's own
    tier math produces is bounded and small (5 tiers today — see below), so `std::vector`'s heap
    indirection buys nothing; `kMaxTierAddressDepth=8` leaves headroom for a couple of future tiers
    without forcing another representation change. `PushHop` past capacity clamps rather than UB's or
    throws (documented as a defensive clamp, not a silent-failure API surface).
  - **Shared-prefix helper**: `TierAddress::SharedPrefixLength(a, b)` — walks both hop arrays up to
    `min(depth)`, returns the count of leading equal hops. Verified: self-comparison and identical
    addresses both return full depth; fully-divergent-at-root pairs return 0; partial-overlap pairs
    return exactly the common-ancestor depth; sibling addresses (same prefix, differ only at the final
    hop) return `depth - 1`; one-address-is-a-prefix-of-the-other is symmetric. 8 dedicated test cases.
  - **Serialization form**: a simple depth-prefixed dot-separated string (`ToString()`, e.g. `"4:7.2.5.0"`)
    — explicitly documented in the header as NOT the eventual undertow wire format (§4/§11 defer that),
    just a deterministic, easy-to-replace stand-in for this increment's own tests/logging.
  - **Tier-math re-derivation** (`TierMath.h`, `BuildTierScaleTable()`): rebuilt the 5-tier table
    bottom-up from the Sparse-Mip-ESVO-LOD-Direction doc's cited 1cm bedrock voxel, rather than copying
    the doc's own (slightly rounded) span figures — T2 bedrock (10 levels) → T1 region (10 levels,
    leaf = T2's span) → T0 planet (10 levels, leaf = T1's span) matches the source doc's "~10 effective
    levels each" table within the same order of magnitude (T0 span comes out ~10,737 km vs. the doc's
    cited "12,700 km" Earth-diameter figure — the doc's own number is itself an approximation of Earth's
    actual diameter using clean powers of two, not a bug in this derivation). Generalized upward per the
    Tiered-ESVO design doc's §1: the remaining level budget up to the cited 9.46×10²² cm galaxy diameter
    (`log2(galaxy_cm / T0_span_cm) ≈ 46.08` levels) is split evenly across exactly 2 tiers (System,
    Galaxy) — landing at ~23.04 levels/tier, which reconciles the design doc's own two framings
    ("~10 effective levels/tier" for T0-T2, vs. "23 levels per ESVO instance" cited for the ~4-5-tier
    total): the lower 3 tiers keep conservative headroom in the 23-level ESVO stack (for brick-local
    subdivision), while System/Galaxy — pure scale/index hops, no brick subdivision — use close to the
    full per-instance budget. Total re-derived level count across all 5 tiers: **76.324** (exactly
    `log2(9.46e22/1)` to machine precision — confirmed by the Opus validator's own from-scratch
    re-derivation; the code's own arithmetic hits this exactly, correcting an earlier hand-rounded
    "76.08" figure that appeared in this section before validation). Bottom (T2) leaf and top (Galaxy)
    span both bracket the design doc's cited figures exactly (galaxy span is reconstructed to match
    9.46×10²² cm by construction of the derivation, not an independent coincidence — see `TierMath.h`'s
    header comment for the full worked derivation).
  - **No scope drift**: confirmed via `git diff --stat` before commit — only
    `libraries/SVO/include/{TierAddress,TierMath}.h`, `libraries/SVO/tests/test_tier_{address,math}.cpp`,
    and the `CMakeLists.txt` registration touched. No `ChildDescriptor`/`farBit`/`SVORebuild.cpp`/
    `LaineKarrasOctree`/`ConcatenatedOctrees` file touched, per §0.
  - **Opus validator: APPROVED (2026-07-07)** — independently re-derived the tier math from scratch
    (Python, starting from the 1cm leaf): confirmed T0 span lands at exactly 2³⁰ cm = 10,737 km, galaxy
    span hits 9.46×10²² cm to machine precision, and the total is 76.324 = `log2(9.46e22/1)` exactly.
    Judged both self-flagged concerns reasonable and internally consistent: (1) the T0/T1/T2 "12,700 km"
    vs. re-derived "10,737 km" discrepancy is genuinely because the source doc cites Earth's REAL
    diameter (which itself rounds `log2(12700km/1cm)≈30.24` to "~30-31 levels" in its own text), not an
    error in the re-derivation; (2) the 5-tier 10/10/10/~23/~23-level split is "a reasonable,
    internally-consistent reading" of the design doc's genuinely two-framed prose — there is no single
    unambiguous reading, and this one makes both framings cohere without straining. Rebuilt and re-ran
    both binaries fresh (forced recompile of all 4 sources): `test_tier_address` 16/16, `test_tier_math`
    9/9, pure CPU — confirmed the test bodies are genuine proofs (divergent-at-root, sibling,
    partial-overlap, prefix-symmetric, capacity-clamp cases), not trivial assertions. Confirmed scope
    discipline directly (6 files total, out-of-scope symbols appear only in comments). One non-blocking
    nit found and fixed above (the stale hand-rounded "76.08" total, corrected to the code's actual
    76.324).
    validation pass per the pipeline).

---

## M1 — `TierAddress` type + tier math

### Why this is first

Every other milestone consumes `TierAddress` instances; nothing about direction/magnitude math or the
render node can be tested meaningfully without a real address type to construct test fixtures from.
This mirrors Sparse-Mip-ESVO-LOD Inc1's own M1 (a foundational CPU-only type before any GPU wiring).

### Task 1 — `TierAddress` type + shared-prefix comparison

- [x] New header, e.g. `libraries/SVO/include/TierAddress.h` (pure CPU, no GPU/node dependency,
  matching the design doc §4's own framing: "Not GPU-resident — a small CPU-side identity, cheap to
  store/compare/serialize"). Fields: `std::vector<uint32_t> hops` per §4's sketch — confirm at
  implementation time whether a small fixed-capacity array (4-6 entries, per the design doc's own
  "4-5 entries typical" estimate) is a better fit than a `std::vector` for a type that gets compared
  and copied frequently; either is acceptable, document the choice.
  → Built as a fixed-capacity `std::array<uint32_t, 8>` + depth counter — see Progress Log for the
  reasoning.
- [x] Shared-prefix helper: given two `TierAddress` values, return the length of their common prefix
  (0 if they diverge at the root) — this is the "shared-prefix = shared ancestor" primitive §4
  describes, and every direction/distance computation in M2 depends on it.
  → `TierAddress::SharedPrefixLength(a, b)`.
- [x] Comparison/equality and a stable serialization form (even if only used by tests this increment —
  §4 notes this is "the one shared contract with undertow," so keep the type easy to re-derive a wire
  format from later, but do not attempt to design that wire format now — §0 explicitly defers it).
  → `operator==`/`operator!=` + `ToString()` (depth-prefixed dot-separated string, explicitly NOT the
  final wire format).
- [x] Unit tests: construct addresses at varying depths, confirm shared-prefix length is correct for
  identical/partially-overlapping/fully-divergent pairs, confirm a self-comparison returns full depth.
  → `test_tier_address.cpp`, 16/16 green.

### Task 2 — Tier math generalized upward (system, galaxy)

- [x] Re-derive (do not re-invent) the tier count/sizing math already established in
  [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] (T0 planet / T1 region / T2 bedrock, ~10 effective levels
  each) and this design doc's §1 restatement (~76 levels voxel-cm to galaxy-diameter, 23 levels per
  ESVO instance → ~4-5 tiers). Produce a small, testable CPU function/table mapping tier index → linear
  scale range (or the per-tier scale factor needed to convert a hop count into a real-world distance
  estimate for M2's magnitude falloff) — this is bookkeeping/derivation, not new design; verify the
  numbers against the source doc's own math rather than re-deriving from scratch.
  → `libraries/SVO/include/TierMath.h`, `BuildTierScaleTable()` — see Progress Log for the full
  re-derivation and how the "~10 levels/tier" vs "23 levels/instance" framings reconcile.
- [x] Unit test: confirm the tier-to-scale mapping produces sane, monotonically-increasing ranges
  across all tiers, and that the top tier (galaxy) and bottom tier (T2 bedrock) bracket the design
  doc's own cited figures (~9.46×10²² cm galaxy diameter, ~1cm voxel).
  → `test_tier_math.cpp`, 9/9 green.

**M1 gate:** all new unit tests green, pure CPU, no GPU/render dependency.

---

## M2 — Direction/magnitude derivation

### Why this is separate from M1

`TierAddress` and tier math are structural types; deriving a screen-relative direction and an apparent
magnitude from a pair of addresses is a distinct piece of domain logic with its own edge cases
(same-tier vs. cross-tier pairs, the optional light-delay term) — keeping it a separate milestone
means M3's render-node work can be validated against an already-proven math layer, not a mixed
concern.

### Task 3 — Direction from shared-prefix composition

- [ ] Given an observer `TierAddress` and a candidate object's `TierAddress`, compute a normalized
  direction vector composed ONLY through their shared-prefix ancestor (§4, §7 step 1: "compute
  direction `normalize(objectLocal − observerLocal)` composed only through the shared-prefix ancestor
  — never a flattened world coordinate"). Reuse M1's shared-prefix helper; the composition itself is
  per-hop local-frame math (§3.3's discipline — each hop is a bounded [1,2)-frame scale+offset, so
  composing K divergent hops is K well-conditioned steps, not an accumulated world transform).
- [ ] Unit test: two addresses sharing a full prefix except the last hop (siblings) should compose in
  exactly one hop each; addresses diverging near the root should compose through more hops but remain
  numerically well-behaved (no precision blowup) — construct a test with genuinely large tier-scale
  differences (e.g. a galaxy-tier divergence) to prove the "no accumulated world-space error" claim,
  not just a same-tier sanity check.

### Task 4 — Apparent magnitude + optional light-delay staleness

- [ ] Brightness/magnitude falloff as a function of the composed distance from Task 3 (a standard
  inverse-square-style falloff is sufficient for v1 — the design doc explicitly does not mandate
  precise astrophysical magnitude units, just "direction + apparent magnitude"). Document the exact
  formula chosen and why (simplicity/plausibility, not real-world radiometric accuracy, is the bar).
- [ ] Optional light-delay/staleness term (§7 step 1: "a physically-motivated detection floor, not
  required for v1") — implement as a genuinely optional parameter (e.g. defaults to off/zero delay);
  do not let this become a blocker if the underlying light-speed/time-model plumbing doesn't exist yet
  in VIXEN — confirm at implementation time whether such a time model exists at all, and if not,
  stub this term as a documented no-op rather than inventing a new time-simulation system this
  increment.
- [ ] Unit tests: magnitude falls off monotonically with composed distance; confirm the light-delay
  term (if built) is inert when disabled and produces a sane output when a nonzero delay is supplied.

**M2 gate:** all new unit tests green, pure CPU, no GPU/render dependency.

---

## M3 — `SkyProjectionNode` + live composite gate

### Why this is last

This is the only milestone touching the render graph/GPU at all — it consumes M1/M2's already-proven
CPU math and is purely a "how do these numbers reach a pixel" concern. Isolating it last means a
render-graph mistake can't be confused with a math mistake from M1/M2 during validation.

### Task 5 — `SkyProjectionNode` config + CPU-side data prep

- [ ] New `RenderGraph` node type (config struct via the project's standard
  `CONSTEXPR_NODE_CONFIG(...)`/`INPUT_SLOT`/`OUTPUT_SLOT` macro pattern — see
  `RaySizeCoefNodeConfig.h`/`BodyOctreeSceneNodeConfig.h` for the two existing shapes to follow).
  Takes a small CPU-side list of (direction, magnitude, optional-staleness) tuples — produced by M2's
  functions from a set of `TierAddress` pairs — and uploads them into a small SSBO, following the
  same "small CPU-side per-instance dataset → SSBO" idiom `InstanceBufferNode`/
  `BodyOctreeSceneNode`'s `INSTANCE_BUFFER` output slot already establish.
- [ ] `VIXEN_REGISTER_NODE(...)` self-registration, matching every other node type in this codebase
  (see `RaySizeCoefNode.cpp`'s trailing registration line for the minimal example).
- [ ] For this increment, the "candidate objects" feeding the node are a small synthetic/hardcoded
  test fixture (a handful of `TierAddress` pairs placed at plausible tier depths/angular offsets) —
  **not** a real undertow-fed data source (§0, out of scope). Document this explicitly in the node's
  own header comment so a future increment wiring real data doesn't mistake the synthetic fixture for
  the intended production data path.

### Task 6 — Point/disk-sprite composite shader

- [ ] New vertex+fragment shader pair (GLSL, matching `libraries/RenderGraph/src/Ui/shaders/ui.vert`/
  `ui.frag`'s convention — a plain vertex+fragment pair, NOT a `.comp` compute dispatch like
  `BodyInstanceRayMarch.comp`; this consumer has no ray-march/traversal at all). Reads the SSBO from
  Task 5, draws a small point or disk sprite per object at its screen-projected direction, with
  brightness/size driven by the magnitude value.
- [ ] Confirm at implementation time how "direction → screen position" should actually work for a
  skybox-style far-object projection (e.g. treat the direction as a point on the observer's view
  sphere, project through the existing camera/view matrices the same way the voxel render's own
  camera does) — this is a real design decision the design doc leaves to implementation ("a small,
  bolt-on node consuming `TierAddress` data... not a new traversal mode"), not something to guess
  blindly; check how the existing skybox/background rendering (if any) currently projects its content,
  and match that convention rather than inventing a new one.

### Task 7 — Composite wiring + live gate

- [ ] Wire `SkyProjectionNode` into `BuildRenderGraph.cpp` following the exact
  `ui_composite_render_pass`/`ui_composite_framebuffer`/`ui_composite_render` structural pattern (a
  new `RenderPassNode` with `PARAM_COLOR_LOAD_OP = AttachmentLoadOp::Load` — NOT `Clear` — stacked
  over the existing voxel-render output, feeding a `FramebufferNode` + this new node instead of
  `UIRenderNode`). Confirm ordering relative to the existing UI composite pass (does the sky-projection
  layer belong before or after HUD compositing? — almost certainly before, since HUD should draw over
  everything including sky points, but confirm by checking what's actually visually sensible rather
  than assuming).
- [ ] Live gate (Windows-side build + `VIXEN.exe` run, per this project's standing live-run-gate
  convention): place a known observer address and a small number of synthetic candidate addresses at
  known tier/angular offsets, run the app, and confirm the sky points render at the visually-correct
  screen positions/brightness relative to the observer — a static screenshot comparison or an
  in-engine debug-overlay readout of computed direction vs. expected direction is sufficient; a full
  automated pixel-diff test is not required for this increment (document what manual/semi-manual
  verification was actually performed).
- [ ] No-regression check: confirm the existing voxel render + UI/HUD composite are visually unaffected
  by the new Load-op pass being stacked in (no accidental double-clear, no z-fighting/ordering issue
  with the HUD layer).

**M3 gate:** shader compiles; live `VIXEN.exe` run shows synthetic sky points at correct positions;
existing voxel/UI render unaffected.

---

## Notes for implementers

- M1/M2 are Sonnet-medium implementable as pure CPU/math work — no GPU, no render graph, no live
  gate needed. Register their tests the way `tests/Data/test_scene_generators.cpp` does (a plain
  `add_executable` + `gtest_discover_tests` in a small dedicated `.cmake`, included from
  `libraries/RenderGraph/tests/CMakeLists.txt` or `libraries/SVO/tests/CMakeLists.txt` as appropriate
  for where `TierAddress.h` ends up) — **not** `test_critical_nodes.cmake`, which is reserved for
  live-GPU/Vulkan-infrastructure node tests (DeviceNode, WindowNode, SwapChainNode, FrameSyncNode,
  and this epic's own eventual M3-and-later GPU work).
- M3 is the one milestone needing the live-run gate and Windows-side build, per this project's
  standing convention (WSL/`/mnt/c` cross-mount is slow; GPU/render work should build+test
  Windows-side per `VIXEN/.claude/skills/project-rules/rules/commands.md`).
- Do not let M3's implementer drift into building §3/§5 (tier-crossing leaf reference, traversal
  restart) "since it's related" — that is explicitly out of scope for this increment (§0) and is a
  substantially larger, separate piece of work with its own open design decisions (§11 of the design
  doc) not yet resolved.
- `TierAddress`'s eventual wire-format reconciliation with undertow's `Undertow.View` schema (§4, §11)
  is NOT this increment's concern — keep the type's internal representation simple and CPU-only;
  don't try to anticipate a cross-repo schema now.
