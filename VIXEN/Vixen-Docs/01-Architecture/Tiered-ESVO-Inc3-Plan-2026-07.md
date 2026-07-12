---
title: Tiered ESVO — Inc3 Implementation Plan (scale-magnified tiers + 3-tier chain — Earth-scale surface-to-orbit)
status: Plan (2026-07-10) — NOT started
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
- [ ] Derive the exact composition algebraically from `remapRayIntoChildFrame`'s actual shipped
  math (direction divided by `childScale`) — determine whether the child's returned hitT must be
  multiplied or divided by `childScale` (or something subtler) to land in the parent's world-t
  unit. Verify the derivation with a standalone numeric trace (Inc2 M3's implementer got this
  exact class of question wrong on first instinct and self-caught via a Python check — repeat
  that discipline). Implement at the wrapper's composition site in `BodyInstanceRayMarch.comp`.
- [ ] Port to `GpuTraversalMirror.h` in lockstep (its SYNC CONTRACT is mandatory; M3/Inc2 already
  ported the crossing restart — extend `castRay`'s composition identically).

### Task 2 — LOD-gate generalization
- [ ] Generalize the crossing LOD gate from the parent leaf's footprint to the child's finest
  resolvable detail (`>= childScale * scale_exp2` is the M4-validator-named starting point —
  re-derive against how `scale_exp2`/`raySizeCoef` actually interact in the existing non-leaf
  cutoff). Update the gate-site comment (currently documents the unity-only equivalence).
  Confirm behavior is unchanged at `childScale==1.0` and conservative-or-correct otherwise.

### Task 3 — CPU parity tests that would catch an inverse error
- [ ] Extend `test_tier_crossing_mirror_parity.cpp` (or a sibling) with `childScale ∈ {0.5, 2.0}`
  fixtures asserting hit-t values against independently hand-computed expected world distances
  (not just mirror-vs-mirror self-consistency), plus a `2^-10` case for numeric sanity at a
  realistic tier ratio. A wrong multiply-vs-divide MUST fail these tests.

**M1 gate:** all SVO CPU suites green including the new non-unity parity cases; a live rerun of
the UNCHANGED Inc2 `childScale==1.0` demo shows identical behavior (magenta octant proof intact,
canonical VUID baseline: 10 emissions of binding-14 `VUID-vkCmdDispatch-None-08114`, zero new).

## M2 — Live single magnified crossing

### Task 4 — `childScale != 1` demo + prediction-first live proof
- [ ] Extend the `VIXEN_TIER_CROSSING_DEMO` fixture (or a variant knob) so the child tree is
  marked with a genuinely non-unity `TierRef::childScale` (e.g. 0.25: child's unit cube spans a
  quarter of the parent leaf cell) — construction side already supports arbitrary `childScale`
  via `MarkLeafAsTierCrossing`; what's new is exercising it.
- [ ] Live gate, prediction-first: hand-compute BEFORE running (a) the expected screen position
  AND (b) the expected apparent SIZE of the child's distinctly-colored geometry given the scale
  factor, then pixel-verify both from the capture. The size check is what makes this a
  magnification proof rather than a re-run of Inc2 M3's position proof. Zero new VUIDs.

**M2 gate:** a scale-magnified child renders through the crossing at the predicted position and
apparent size on real hardware; validation clean.

## M3 — Chained crossings (hop loop)

### Task 5 — Wrapper hop-loop + 3-tree chain
- [ ] Generalize `traverseOctreeInstanced` from one restart to a bounded hop loop
  (`MAX_TIER_HOPS` ≈ 5): on a tier-crossing hit inside the CURRENT tree, park the current
  traversal state (the park record generalizes to a small fixed-size chain — never simultaneous
  full stacks, per design doc §10), remap, descend; on child miss/exit, pop back one hop and
  resume. Preserve Inc2's invariants: the 3 per-tree globals are the ONLY swapped state; fresh
  stack per hop from locals; the M4 early-outs (LOD/residency) apply at EVERY hop's crossing
  decision, not just the first.
- [ ] Mirror lockstep port + a chained parity test (grandchild geometry reached through two
  hops, hit-t hand-verified through two scale compositions).
- [ ] 3-tree construction fixture: T2 marked into T1, T1 marked into T0 (reuse
  `MarkLeafAsTierCrossing` twice across three `SerializedOctree`s concatenated together).
- [ ] Live gate: the 3-tier chain renders correct grandchild geometry through both crossings on
  real hardware (distinct color per tier so each hop is visually attributable), zero new VUIDs,
  and the farBit==0 hot path regression-checked (full baseline demo + default scene unchanged).

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
