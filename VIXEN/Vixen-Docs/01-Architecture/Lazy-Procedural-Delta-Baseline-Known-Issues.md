# Lazy-Procedural + Delta Baseline — Known Issues

Follow-ups surfaced during Inc0+Inc1 ([[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]]).
None blocked Inc1 landing; each is scoped for a later increment or a focused fix.

## KI-LPD-001 — Domain-modifier recipes render `virtualHits=0` GPU-direct (8× step inflation)

**Status:** ✅ RESOLVED 2026-07-11, commit `9f6d82df`, Opus-validated. Domain-modifier recipes now
render GPU-direct (twist_sphere `virtualHits` 0 → 9183, ≈98% of the baked path's 9351 hits).

**Symptom (was):** a virtual (GPU-direct, zero-bake) body whose recipe contained a **domain-modifier
/ position-stack opcode** (Twist, Bend, Mirror*, Repeat*, Elongate) rendered **nothing**
(`virtualHits=0`), while the baked→octree→ESVO path rendered it fine. Leaf/CSG/Round/Onion recipes
rendered correctly GPU-direct throughout.

**Real root cause (Opus-validated — NOT the originally-hypothesized relaxation overshoot):** an
**8×-inflated march step for any recipe with no occupancy grid.** `traceUberRecipeBody`'s step was
`step = max(d·relaxation, min(gridBound, d·relaxation·8.0))`. For an ungridded recipe (`gridDim==0u`
— exactly the domain-warp/whitelist-bail class, since `DeriveOccupancyGrid` declines the same
opcodes `DeriveConservativeBounds` does), `sampleRecipeOccupancy` returns `kNoGridSentinel = 1e30`,
so `min(1e30, d·relaxation·8.0)` **always** picks the 8× term → every march step silently 8×
regardless of relaxation → overshoots the thin distorted shell for all 128 steps. **This is why the
first attempted fix (reducing `stepRelaxation`) did nothing** — relaxation scales both sides of the
`max` equally, so the `·8.0` multiplier survives any relaxation value; the `·8.0` is downstream and
dominates. The original "step-relaxation overshoot" hypothesis had the right symptom (overshoot) and
the **wrong mechanism.**

**Fix:** gate the occupancy-skip boost on grid presence —
`step = (gridDim != 0u) ? max(d·relaxation, min(gridBound, d·relaxation·8.0)) : d·relaxation;`
(`shaders/SdfRecipes.glsl`, one site). The `gridDim!=0u` branch is byte-identical to before (no
regression to the occupancy-skip proof for gridded recipes); the `gridDim==0u` branch takes the
plain conservative relaxed step (no unbacked 8×). Validator confirmed: only that shader file
changed, sphere/CSG unregressed (IoU 0.84/0.87), no other `min(gridBound,…·8.0)` sentinel-bug site,
0 VUIDs. **Ruled out and confirmed correct: bounds, occupancy, GLSL emission — the marcher is now
correct** (verified via an independent Python march reference + geometric silhouette model).

## KI-LPD-003 — Parity-corpus twist-frame mismatch (`twist_sphere` IoU 0.585 < 0.75 gate)

**Status:** ✅ RESOLVED 2026-07-11, commit `da2ba9c5`, Opus-validated. Corpus-only fix (floor
un-weakened): twist_sphere IoU **0.585 → 0.9023** PASS; sphere 0.8386 / csg 0.8662 unregressed; 0
VUIDs. The two programs now present the SAME absolute `p.y` to `SdfCore_Twist` — the world-space
program's Twist is wrapped in a `Transform(worldTarget)` (pure `pos−worldTarget` translate) with the
sphere re-authored at local origin, plus a second `RestorePos` so the position stack is BALANCED
(Transform push + Twist push = 2 pops, verified on both the CPU VM and the GLSL codegen path).
`kIoUFloor` stayed 0.75; no assertion weakened/skipped; no shader or production code touched. The
branch parity gate is now fully green. Historical diagnosis retained below.

**Root cause (Opus-validated):** `SdfCore_Twist` rotates (x,z) by `k·p.y` using the **absolute**
sample `p.y`. The corpus's two programs for this case place the sphere at **different absolute Y**:
`worldSpaceProgram` (virtual path) at `worldTarget=(5,5,5)`, `localSpaceProgram` (baked path) at
local origin `(0,0,0)`. Same `k=0.05`, so the virtual body twists by ≈`0.05·5 = 0.25` rad near its
shell while the baked body twists by ≈0 — the two paths render **genuinely different geometry**, so
IoU cannot reach 0.75. Decisive evidence: virtual=9183 ≈ 98% of baked=9351 hit COUNT (marcher finds
essentially the full shell — a marcher *error* would drop the count, as the old 0-hit failure did),
and an independent geometric model of "twisted off-axis sphere vs plain sphere" reproduces both the
0.585 IoU and the ~equal footprint area from the frame offset alone.

**Fix direction:** align the two corpus programs' absolute-Y twist frame — e.g. place the baked
sphere at the same absolute Y as the virtual one, or wrap both in a consistent translate-to-origin
`Transform` so `SdfCore_Twist` sees the same `p.y` on both paths. A test-corpus edit only; the shader
is correct. Do NOT weaken the 0.75 floor. Repro: `test_baked_vs_virtual_parity` `twist_sphere`
(real GPU).

## KI-LPD-002 — `esvo_traverse_shade_ms` fix + switch-scaling re-capture (perf attribution)

**Status:** RESOLVED (timer) / OPEN (re-capture). The GPU-pass timestamp is now wired (commit
`c0045dcd` — fixed a shared `GPUQueryManager` dead-slot bug that blocked pool-wide readback). The
N=3/10/100/500 switch-scaling table in [[Perf-Ledger]] was captured BEFORE that fix (GPU column
read 0), so its per-frame-GPU attribution is from FPS only. **Optional follow-up:** re-capture
N=3/100/(retry 500) with the working timestamp to attribute the N=100 knee to GPU-vs-CPU precisely.
The scaling CONCLUSION (switch unusable at high N → [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]])
already holds from FPS.

## Environmental (operational, not this program's code)

- **Shared C: drive disk exhaustion.** Windows-side builds share one physical drive across ~15
  sibling worktrees; each `build/ninja` can be tens of GB (this worktree's ~56 GB). It hit 0 free
  mid-M6-sweep, corrupting 21 test `.exe` to 0-byte and blocking fresh full builds. Recovery: free
  space, rebuild the affected targets. Not fixable from within a single worktree.
- **KI-017 (windows.h `min`/`max` macro poisoning).** Pre-existing, in `Vixen-Docs/04-Development/Known-Issues.md`.
  Blocks ~20 SVO test targets on MSVC. The real fix is a global `NOMINMAX`; M6 applied only the
  doc's suggested `glm::max` partial (insufficient alone). Out of Inc1 scope.
