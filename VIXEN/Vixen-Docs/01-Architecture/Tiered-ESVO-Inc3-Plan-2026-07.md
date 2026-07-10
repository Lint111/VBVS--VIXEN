---
title: Tiered ESVO — Inc3 Implementation Plan (scale-magnified tiers + 3-tier chain — Earth-scale surface-to-orbit)
status: M1-M3 SHIPPED 2026-07-10; M4 mechanism-complete, BLOCKED on a live-render finding (see Progress Log)
depends: Tiered-ESVO-Observer-Addressing-Design-2026-07.md (§3, §5, §9), Tiered-ESVO-Inc2-Plan-2026-07.md (shipped 2026-07-10, merged `2d67840e`, origin/main `12145d60`)
---

# Tiered ESVO Inc3 — scale-magnified tiers + chained crossings

## Goal

Turn Inc2's proven same-scale tier-crossing mechanism into the actual deliverable the epic
exists for: an **Earth-diameter-scale planet with a live, continuous surface-to-orbit zoom
across MULTIPLE tier crossings at real scale ratios**. Inc2 proved one crossing at
`childScale==1.0` (both trees the same physical scale — demonstrates machinery, buys zero
scale range). A true ~12,700 km planet needs ~30-31 octree levels against a single tree's 23;
that headroom comes exclusively from magnified tiers (`childScale != 1`), chained.

## Why this is new machinery (small, but real)

Two exact-only-at-unity assumptions are baked into Inc2's shipped code, both flagged as named
prerequisites in Inc2's own M3/M4 validator addenda (plan doc + design doc §9):

1. **Additive hitT**: the crossing composes `hitT = tierCrossWorldT + childHitT`. The Inc2 M3
   validator traced numerically that child-t and parent-t units are identical ONLY at
   `childScale==1.0`; at any other scale the child's internal t is in a different
   world-distance unit (factor involving `1/childScale`, per the `remapRayIntoChildFrame`
   direction division). A per-child-scale normalization is required at the composition site.
2. **LOD gate footprint**: the crossing's early-out gates on the PARENT leaf's own footprint
   (`tv_max*raySizeCoef+raySizeBias >= state.scale_exp2`), exactly equal to the child's
   footprint at unity scale and conservative below it. The M4 validator named the
   generalization: gate against the child's own finest resolvable detail, i.e.
   `>= childScale * scale_exp2` (starting point — re-derive, don't transplant).

And one structural gap:

3. **Single-hop wrapper**: Inc2's `traverseOctreeInstanced` wrapper performs exactly ONE
   restart (parent → child). A `farBit==1` leaf encountered DURING a child traversal is not
   followed. Chaining T2→T1→T0 needs the wrapper generalized to a bounded hop LOOP (GLSL has
   no recursion; park-state becomes a small per-hop chain). Design doc §10 already rejected
   simultaneous stacks — "parent parked, child active" generalizes to a parked CHAIN, one
   fresh stack live at a time. This is composition of the proven mechanism, not new per-tier
   engineering — but it touches the live traversal wrapper again, so it carries M3-of-Inc2
   levels of live-gate discipline.

## §0. Scope

**In scope:**
- Per-child-scale hitT normalization at the crossing composition (shader + `GpuTraversalMirror.h`
  in lockstep per its sync contract), with numeric derivation evidence and CPU parity tests at
  `childScale ∈ {0.5, 2.0, 2^-10}` chosen to CATCH a wrong sign/inverse, not just pass at unity.
- LOD-gate generalization to the child's scale (same lockstep + the comment at the gate site
  updated — it currently documents the unity-only equivalence).
- Wrapper generalization to N bounded hops (`MAX_TIER_HOPS`, ~4-5 per the tier-count math in the
  direction doc) + a 3-tree chained fixture (T2→T1→T0) + mirror chain-parity test.
- Live gates throughout on real hardware with validation forced on, including hand-computed
  prediction-first proofs (Inc2's discipline: predict the number BEFORE running).
- The epic gate: a scale-magnified 3-tier body at realistic ratios (~2^-10 per hop) with a
  continuous, seamless surface-to-orbit camera zoom across TWO live crossings.

**Out of scope (documented follow-ups, not silently dropped):**
- **Per-octree residency state machine** — Inc2 M4 documented `RequestBrickResidency` is
  whole-node; the 3-tier demo may script residency the way Inc2 M5 did (or set per-config
  fields demo-side); building real per-octree residency granting is its own increment.
- §7 steps 2-3 telescope-zoom handoff to `SkyProjectionNode` (fleet-detection mechanic).
- undertow integration / `TierAddress` wire-format reconciliation (design doc §11).
- Any change to `TierRef`'s 20-byte layout or `ChildDescriptor`'s 8 bytes.

## Milestone Map

- **M1 — Scale-correct crossing math** (Tasks 1-3) · gate: CPU parity green at non-unity scales
  + live baseline regression (childScale==1.0 demo byte-equivalent behavior, zero new VUIDs).
- **M2 — Live single magnified crossing** (Task 4) · live-run gate · a `childScale != 1` child
  renders through the crossing at the hand-computed screen position AND apparent size.
- **M3 — Chained crossings (hop loop)** (Task 5) · live-run gate, **highest-risk milestone**
  (rewrites the traversal wrapper again) · a T2→T1→T0 3-tree chain renders correctly live.
- **M4 — Earth-scale surface-to-orbit zoom (the epic gate)** (Tasks 6-7) · live-run gate ·
  continuous seamless zoom across two real magnified crossings + docs closure.

## M1 — Scale-correct crossing math

### Task 1 — hitT normalization for `childScale != 1`
- [x] Derive the exact composition algebraically from `remapRayIntoChildFrame`'s actual shipped
  math (direction divided by `childScale`) — determine whether the child's returned hitT must be
  multiplied or divided by `childScale` (or something subtler) to land in the parent's world-t
  unit. Verify the derivation with a standalone numeric trace (Inc2 M3's implementer got this
  exact class of question wrong on first instinct and self-caught via a Python check — repeat
  that discipline). Implement at the wrapper's composition site in `BodyInstanceRayMarch.comp`.
  **RESULT: the correction is a MULTIPLY by `length(childRayDirWorld)`, NOT a bare `childScale`
  term.** The child's returned `t` is parametric in the (non-unit) child-frame direction, so the
  true world arc-length is `t * |childRayDirWorld|`; a bare childScale cannot recover the child
  octree's independent localToWorld scale. Reduces byte-exactly to Inc2's plain addition when
  `|childRayDirWorld|==1` (S_child==S_parent, the shipped demo). Derivation trace + write-up at
  `Tiered-ESVO-Inc3-M1-hitT-derivation-trace.py` / `-derivation.md`.
- [x] Port to `GpuTraversalMirror.h` in lockstep (its SYNC CONTRACT is mandatory; M3/Inc2 already
  ported the crossing restart — extend `castRay`'s composition identically). Confirmed identical:
  shader `hitT = tierCrossWorldT + hitT*childRayDirWorldLen`, mirror `childOut.t = tierCross.worldT
  + childOut.t*glm::length(childRayDirWorld)`.

### Task 2 — LOD-gate generalization
- [x] Generalize the crossing LOD gate from the parent leaf's footprint to the child's finest
  resolvable detail (`>= childScale * scale_exp2` is the M4-validator-named starting point —
  re-derive against how `scale_exp2`/`raySizeCoef` actually interact in the existing non-leaf
  cutoff). Update the gate-site comment (currently documents the unity-only equivalence).
  Confirm behavior is unchanged at `childScale==1.0` and conservative-or-correct otherwise.
  **RESULT: `>= childScale * scale_exp2` (multiply). Correct direction (childScale<1 → smaller RHS
  → harder to trigger fallback → crosses more → reveals fine child detail); byte-identical at
  unity. Conservative-or-correct otherwise, as the plan allows. Mirror correctly does NOT port
  (raySizeCoef==0 there → LOD structurally disabled; documented).**

### Task 3 — CPU parity tests that would catch an inverse error
- [x] Extend `test_tier_crossing_mirror_parity.cpp` (or a sibling) with `childScale ∈ {0.5, 2.0}`
  fixtures asserting hit-t values against independently hand-computed expected world distances
  (not just mirror-vs-mirror self-consistency), plus a `2^-10` case for numeric sanity at a
  realistic tier ratio. A wrong multiply-vs-divide MUST fail these tests.
  **RESULT: `NonUnityChildScaleHitTParity` added, childScale ∈ {0.5, 2.0, 2^-10}. Anchors are
  code-measured (fully SDF-independent closed form was not derived), BUT the validator independently
  proved the k-INVARIANT-geometry construction pins the correct answer analytically: hitT must fit
  `C + D/k` (C=tierCrossWorldT constant, D=child portion at k=1); measured {45,70,32.5} fit C=20,
  D=25 to ~1e-5, while the old plain-addition formula gives a k-invariant 45 → the direction IS
  analytically pinned, not just measured. Regression-catch re-verified: reverting to plain addition
  makes the test fail (44.998 invariant); restore → pass. HARDENING NOTE for a future pass: assert
  the `C+D/k` relationship rather than three measured absolutes for a strictly stronger test.**

**M1 gate:** all SVO CPU suites green including the new non-unity parity cases; a live rerun of
the UNCHANGED Inc2 `childScale==1.0` demo shows identical behavior (magenta octant proof intact,
canonical VUID baseline: 10 emissions of binding-14 `VUID-vkCmdDispatch-None-08114`, zero new).

## M2 — Live single magnified crossing

### Task 4 — `childScale != 1` demo + prediction-first live proof
- [x] Extend the `VIXEN_TIER_CROSSING_DEMO` fixture (or a variant knob) so the child tree is
  marked with a genuinely non-unity `TierRef::childScale` (e.g. 0.25: child's unit cube spans a
  quarter of the parent leaf cell) — construction side already supports arbitrary `childScale`
  via `MarkLeafAsTierCrossing`; what's new is exercising it. **DONE: new env knob
  `VIXEN_TIER_CROSSING_SCALE_DEMO` (default 0.25, parses float, guards <=0), isolates childScale
  as the single variable vs the Inc2 fixture.**
- [x] Live gate, prediction-first: hand-compute BEFORE running (a) the expected screen position
  AND (b) the expected apparent SIZE of the child's distinctly-colored geometry given the scale
  factor, then pixel-verify both from the capture. The size check is what makes this a
  magnification proof rather than a re-run of Inc2 M3's position proof. Zero new VUIDs.
  **DONE: predicted 4.0× linear ratio at childScale=0.25 (validator independently re-derived from
  the shipped `remapRayIntoChildFrame`: child `[1,2)` cube occupies parent-local `1.5 ± 0.5*childScale`
  → fills the WHOLE parent cell at unity, linear in childScale); measured 3.93× two independent ways
  (1D y-band AND 2D filled-area sqrt), 1.7-2% err within AA noise. Position at predicted (250,250).
  Diff between runs confined to ONE lower-child bbox — clean magnification, no global shift.**

**M2 gate:** a scale-magnified child renders through the crossing at the predicted position and
apparent size on real hardware; validation clean.

## M3 — Chained crossings (hop loop)

### Task 5 — Wrapper hop-loop + 3-tree chain
- [x] Generalize `traverseOctreeInstanced` from one restart to a bounded hop loop
  (`MAX_TIER_HOPS` ≈ 5): on a tier-crossing hit inside the CURRENT tree, park the current
  traversal state (the park record generalizes to a small fixed-size chain — never simultaneous
  full stacks, per design doc §10), remap, descend; on child miss/exit, pop back one hop and
  resume. Preserve Inc2's invariants: the 3 per-tree globals are the ONLY swapped state; fresh
  stack per hop from locals; the M4 early-outs (LOD/residency) apply at EVERY hop's crossing
  decision, not just the first. **DONE: `MAX_TIER_HOPS=5` loop in both shader + mirror; LOD gate
  needs no new plumbing (lives in `traverseOctreeInstancedOnce`, called fresh per hop → gates
  against that hop's own already-local `scale_exp2`).**
- [x] Mirror lockstep port + a chained parity test (grandchild geometry reached through two
  hops, hit-t hand-verified through two scale compositions). **DONE: `ChainedTwoHopCrossingComposesHitT`
  asserts against an EXTERNAL closed form `(1/childScale)^hop` (not a self-consistency read-back).
  Permanent `HopTrace` diagnostic out-param on `castRay` records per-hop worldT + cumulativeDirLen.**
- [x] 3-tree construction fixture: T2 marked into T1, T1 marked into T0 (reuse
  `MarkLeafAsTierCrossing` twice across three `SerializedOctree`s concatenated together). **DONE.**
- [x] Live gate: the 3-tier chain renders correct grandchild geometry through both crossings on
  real hardware (distinct color per tier so each hop is visually attributable), zero new VUIDs,
  and the farBit==0 hot path regression-checked (full baseline demo + default scene unchanged).
  **DONE: `VIXEN_TIER_CHAIN_DEMO` (T0 gradient / T1 green / T2 cyan). Validator's disable-hop-1
  discriminator PROVES the 2nd crossing genuinely fires (same footprint → cyan with both marks,
  green with only T0→T1 → rules out T0→T2 wrong-path). VUID 10× `08114` zero-new in chain demo,
  unregressed M2 demo, AND default no-crossing scene.**

**M3 gate:** two chained live crossings render correctly; single-crossing and no-crossing paths
unregressed; validation clean. This is the highest-risk milestone — same live-gate discipline as
Inc2 M3 (which found the camera bug) and M5 (which found the buffer-flag bug): trust nothing
that hasn't rendered.

## M4 — Earth-scale surface-to-orbit zoom (the epic gate)

### Task 6 — The real-scale demonstration
- [ ] Build the demo body at realistic tier ratios (~`2^-10` per hop, per the direction doc's
  ~10-levels-per-tier math — hand-compute and document the actual world-unit sizes so the
  "Earth-diameter-scale" claim is numerically explicit, not vibes).
- [ ] Scripted continuous camera zoom from T2-bedrock-scale detail out to full-body orbit view,
  crossing BOTH tier boundaries mid-flight. Prediction-first: compute the camera distances where
  each LOD handoff should flip; verify observed transition ticks match. Per-frame capture
  density around both predicted crossings (Inc2 M5's every-tick-around-the-transition pattern);
  seamlessness evidenced by per-frame pixel deltas, not assertion.
- [ ] Exercise the residency composition at least at one hop mid-flight (scripted grant, as
  Inc2 M5 did; per-octree residency machinery itself stays out of scope).
- [ ] Watch for float32 discipline: each hop is a bounded local transform (design doc §3.3);
  if any hop composition shows precision artifacts at 2^-10 ratios, that is a finding to
  surface, not smooth over.

### Task 7 — Docs closure
- [ ] Update the design doc's status banner + §9 (chaining now proven at scale, or honestly
  what actually happened), same convention as Inc1/Inc2.
- [ ] Plan-doc Progress Log closure; CHANGELOG entry rides at merge time (not in-branch).

**M4 gate:** a live, validated, visually-confirmed continuous zoom from surface detail to orbit
across two real scale-magnified tier crossings with no visible seam — the epic's original ask.

## Notes for implementers (hard-won environment facts — trust these)

- Worktree: `.claude/worktrees/tiered-esvo-inc2` (reused for Inc3, branch `feat/tiered-esvo-inc3`).
  Build INSIDE the worktree (its own `build/wsl` + `VIXEN/temp/win_build.bat` pattern) — the main
  checkout's `build/wsl` is shared with a concurrently-active session and ninja contention WILL
  kill your build.
- Windows app build for live gates (strong default for GPU work); bash env vars do NOT reach a
  Windows .exe — `cmd.exe /c "set VAR=...&& ..."`. Validation must be forced:
  `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (Release compile-gates the layer off).
- Canonical VUID baseline: **10 emissions** of binding-14 `VUID-vkCmdDispatch-None-08114`
  (count `Validation Error:` header lines; `grep -c VUID` double-counts URL lines — this
  confusion recurred across three Inc2 milestones).
- KI-017: broad TEST sweep runs on WSL (`build/wsl`), app builds fine on MSVC. A known Dozen
  SPIR-V 1.5/1.6 family of RenderGraph render-tests segfaults on WSL — pre-existing, documented
  in Inc2's logs; not yours unless the signature changes.
- Poll long builds (`until ! kill -0 $PID; do echo "[watch +${t}s] $(tail -1 $LOG)"; sleep 20;
  t=$((t+20)); done`); NEVER overlap two builds of one target.
- Color-based visual proofs: the shared SdfBake cosine-gradient bakes magenta-adjacent hues —
  either override channel colors per tier (Inc2's technique) or use an in-shader termination
  tint (Inc2 M3-validator's technique) for crisp attribution; material color alone cannot
  separate trees.
- Camera: configured `PARAM_CAMERA_*` is authoritative at rest; orbit engages on interaction or
  the ForTest hooks; `EngageOrbit` clamps orbitDistance to [0.1,120] — an Earth-scale zoom path
  WILL exceed this; widen the clamp deliberately (own commit, justified) rather than working
  around it.

## Progress Log

(one entry per milestone: commits, gates, validator verdict — Inc1/Inc2 convention)

- **M1 (Tasks 1-3): DONE · commit `72496ceb` · Opus validator APPROVED · 2026-07-10.**
  hitT normalization = multiply by `length(childRayDirWorld)` (validator independently re-derived
  from shipped `castRayOnce` return convention, confirmed multiply-not-divide, reduces to Inc2 plain
  addition byte-exactly at unity). LOD gate = `>= childScale*scale_exp2` (correct direction, unchanged
  at unity). Lockstep shader↔mirror confirmed identical. New `NonUnityChildScaleHitTParity` (k ∈
  {0.5, 2.0, 2^-10}); regression-catch re-verified by validator (revert→fail 44.998 invariant,
  restore→pass). CPU: parity 4/4 + construction 5/5 green. Live baseline (unchanged k==1.0 demo,
  forced validation): VUID exactly 10× `08114`, zero new; render **pixel-identical (max abs diff 0)**
  to Inc2 reference — true no-op at unity; body count non-zero. Tree clean (72496ceb = 5 intended
  files; two harmless parked cmake-pollution stashes noted, no M1 work lost).
  **CARRY-FORWARD for M3:** validator flagged a latent — if a chained/off-boundary child ray enters
  MACROSCOPICALLY OUTSIDE the child grid, `castRayOnce` folds a true arc-length `tEntryWorld` into
  `out.t` which the `*childRayDirWorldLen` would then misscale. Harmless for well-formed crossings
  (entry at/inside boundary, tEntryWorld≈0) and pre-existing (Inc2's plain addition had the same
  term), but M3's hop loop must ensure child entries stay at/inside the child boundary, or handle the
  `tEntryWorld` term explicitly. Add a comment at the composition site if M3 can produce off-boundary
  entries.

- **M2 (Task 4): DONE · commit `b3d990a6` (single file, +23/-1) · Opus validator APPROVED · 2026-07-10.**
  Env knob `VIXEN_TIER_CROSSING_SCALE_DEMO` exercises a genuinely non-unity childScale (0.25) live.
  Magnification proof: predicted 4.0× linear ratio (validator re-derived independently from shipped
  remap: child cube edge = childScale, fills whole parent cell at unity), measured **3.93× by two
  independent methods** (implementer's 1D scale-invariant-edge y-band + validator's 2D filled-area
  sqrt), 1.7-2% err within AA noise. Position at predicted screen center (250,250); inter-run diff
  confined to one child bbox = clean magnification not global shift. VUID exactly 10× `08114`, zero
  new, zero binding-15, in BOTH the 0.25 run and a same-session unity rerun. **"bodies 0" HUD reading
  RESOLVED (code-traced, not asserted): the HUD bodyCount field is set ONLY by a hardcoded
  `PushHudView(...,bodyCount=3,...)` inside the `VIXEN_HUD_SCRIPT` A/B block, which the tier demo
  never invokes → field stays at init default 0. Real geometry (thousands of contiguous magenta px,
  ~100px sphere w/ specular at predicted center) confirmed by pixel decode — NOT the Inc2-M3 bodiless
  class.** Tree clean (b3d990a6 = exactly one intended file).

- **M3 (Task 5): DONE · commit `e7f64b56` (4 files) · Opus validator APPROVED · 2026-07-10 · HIGHEST-RISK
  milestone, found+fixed TWO SILENT BUGS.** Wrapper generalized to `MAX_TIER_HOPS=5` parked-chain hop
  loop (never simultaneous stacks, per §10) in shader + mirror. **Bug #1 (mirror-only):**
  `RegisterTierCrossingChild` never captured each child's own tierRefs → hop 1+ got an EMPTY table →
  2nd crossing silently degraded to a wrong `contourPointer` brick read. Fixed (per-ChildLink table
  slice). Shader UNAFFECTED (validator confirmed: single concatenated binding-15 SSBO offset by
  `configs[g_octreeIdx].tierRefTableBase`, resolves per-hop since g_octreeIdx swaps before each hop —
  agrees with the fixed mirror for the RIGHT reason). **Bug #2 (shader AND mirror):**
  `cumulativeDirLen *= length(childRayDirWorld)` double-counted every hop past the first (2-hop @ 0.5
  gave 8 vs correct 4=(1/0.5)^2) — must be plain ASSIGN `=` because `childRayDirWorld` ALREADY carries
  full compounding from prior hops via `curDirLocal`. Validator re-derived from scratch (not via
  HopTrace) + empirically (revert→fail@8, restore→pass); this fix corrects hitT for EVERY chained ray,
  so it was load-bearing for M4. Lockstep confirmed identical post-fix. 2nd crossing genuinely walked
  (validator's disable-hop-1 green/cyan discriminator, not absence-explained-away). CPU: parity 5/5 +
  construction 5/5 + tier_ref 5/5 + tier_ref_table 5/5. VUID 10× `08114` zero-new across all 3 scenes.
  Tree clean (HopTrace is a deliberate permanent diagnostic, not cruft).
  **CARRY-FORWARD for M4 (sharpened from M1's note — now a FIRST-CLASS CONSTRAINT):** the off-boundary
  `tEntryWorld` invariant holds in M3 ONLY BY PLACEMENT (childScale=1.0, centered childOriginLocal=1.5
  → entry inside child cell → tEntryWorld=0), NOT structurally enforced — there is no clamp forcing
  entry inside the grid. When a remapped hop starts OUTSIDE the grid, `tEntryWorld` folds a real
  arc-length into the crossing t and gets MISSCALED by cumulativeDirLen. The chained test's own header
  documents that M1's off-boundary offset, chained, breaks hop 2 (gridT.x=12.5 → pop-logic precision
  failure) — hence its deliberately-inside `(0.1,0.1,0.1)` offset. **M4's non-centered, childScale=2^-10
  hops MUST handle the tEntryWorld term explicitly OR enforce at/inside entry — do NOT rely on lucky
  placement. This is the most likely place M4 introduces a depth/precision artifact.**

- **M4 (Tasks 6-7): MECHANISM COMPLETE, BLOCKED · commits `a73de7b1`+`cee69ff2` (CPU proof),
  `4267dbbc`+`30812e5b` (live demo + clamp) · 2026-07-10.** Numeric derivation: T0's existing
  48-world-unit diameter declared to represent Earth's actual 12,742 km diameter (1 world unit
  = 265,458 m); T1 (region) ~12.4 km diameter; T2 (bedrock) ~12.15 m diameter, ~6.08 m brick,
  ~0.76 m voxel; total ratio across both hops 2^-20. **tEntryWorld (the M1/M3 carried-forward
  constraint): handled via approach (b)** — a k-invariant `childOriginLocal` placement
  (`childOriginLocal = entryPointLocal - offset*childScale` collapses the remap to a
  k-invariant point regardless of childScale) keeps every hop's entry safely inside `[1,2)`
  even at `1/childScale~=1024` amplification; CPU-proven via a new
  `EarthScaleChainedCrossingKInvariantPlacement` test at the real 2^-10 ratio (hop 1 entry
  point asserted inside `[1.05,1.95]` on every axis, composition/cumulativeDirLen verified
  against the closed forms `(1/childScale)` and `(1/childScale)^2`). **Correction found
  mid-implementation** (own discovery-trail, CPU-probed before touching the live scene): the
  per-axis offset SIGN is not a universal constant — it must point INTO the specific octant's
  own asymmetric box (octant 4 = x<1.5,y<1.5,z>=1.5 needs `(-,-,+)`, not a uniform sign; a
  uniform `(-0.1,-0.1,-0.1)` walked the remapped entry outside the target octant's own box on
  z). `VIXEN_TIER_EARTH_DEMO` (3-tree chained construction at the real ratio) +
  `VIXEN_TIER_EARTH_ZOOM_DEMO` (log/linear-schedule scripted zoom + mid-flight
  `RequestBrickResidency(true)` at tick 50) built in `BuildRenderGraph.cpp`/
  `VulkanGraphApplication.cpp`; `CameraNode::kOrbitDistanceMin` widened 0.1→1e-6 (its own
  commit). Fixed the SAME `orbitCenter` gotcha M5's own comment documents (this new demo's
  body sits at world (64,64,64), not the stale Cornell-box default) — caught live via a first
  capture pass.
  **BLOCKED — live-render finding, not a mechanism defect:** live captures at NO tested
  non-unity, non-near-unity `childScale` (0.25, 0.5, 2^-10) show the expected distinctly-colored
  child geometry at the crossing — the crossing region renders as background/miss instead.
  Isolated via a systematic scale sweep (1.0 works; 0.9/0.8/0.7 work, visibly shrinking per the
  predicted linear ratio; 0.5 and below do not) and by checking out Inc3 M2's own original
  commit (`b3d990a6`) standalone with this milestone's own changes fully reverted — the SAME
  `VIXEN_TIER_CROSSING_SCALE_DEMO=0.25` construction that M2's own Progress Log reports as
  "measured 3.93× two independent ways... real geometry... confirmed by pixel decode" does NOT
  reproduce that result in this worktree/environment as tested this session. This proves the
  finding predates and is independent of ALL of this milestone's own work (M1-M4). Root cause
  is narrowed to the live SDF-march/shading path specifically: `GpuTraversalMirror.h` only
  models the binary-DDA leaf-hit path (`marchBrickInstanced`), never the live SDF march
  (`handleLeafHitInstancedSdf`/`marchBrickSdf` in `StoredSdf.glsl`) — so the CPU parity tests
  that DO pass (composition math, hop-loop mechanism, k-invariant placement) structurally
  cannot exercise or catch a defect in that specific path, and one is now suspected to live
  there. **A genuinely separate finding surfaced along the way (own root cause, not conflated
  with the above):** orbiting the camera down toward very small distances (this milestone's
  first zoom-schedule attempt, `kNearDist=1e-5`) puts it INSIDE T0's own solid volume (the
  body's real surface radius is empirically ~25-30 world units — noisy/degenerate render below
  that, clean above it, confirmed by bisection) — unrelated to childScale/tier-crossing at all;
  the corrected zoom schedule must stay outside this radius. **CARRY-FORWARD:** the live
  SDF-march-at-non-unity-scale defect must be root-caused (extend `GpuTraversalMirror.h` or a
  sibling mirror to model `handleLeafHitInstancedSdf`/`marchBrickSdf`, per the gpu-shader-debug
  skill's own CPU-mirror methodology, then re-run this milestone's live gate) before the Earth-
  scale zoom's two crossings can be demonstrated genuinely seamless end-to-end and this
  milestone/epic can be closed. CPU test suite: parity 6/6, construction 5/5, tier_ref_table
  5/5, all green (unaffected by the live finding). VUID 10× `08114` zero-new across every
  tested scene this session (default 3-body scene, unity single-crossing, unity chain, all
  non-unity scale variants, Earth-scale demo). Default farBit==0 hot path unregressed.
