# fable-glyph1: glyph rendering optimisation arc (R892)

Lane 2026-11-16, ws. Kernel worktree `~/projects/Yeroket-Fantasy/.claude-worktrees/glyph1` (`lane-glyph1`),
VIXEN worktree `~/projects/VBVS--VIXEN/.claude-worktrees/glyph1` (`lane-glyph1-v`), native tree
`C:\vixen-native\glyph1`. Nothing pushed.

## STATUS

**CHECKPOINT. Step 1 (B1, exact skip of row-walking leaves) is implemented, tested and
byte-identical everywhere it was witnessed. Its acceptance timing in an exclusive window is still
queued (two refusals, see BENCH REQUEST). Steps 2 (B5), 3 (B4) and 4 (relaxed march, added by the
controller during the lane) are not started; section 5 hands them over.**

- B1 is a kernel generator change only. No shader file in VIXEN changed.
- All 9 glyph rows of the RTL pair are guarded (`rowWalkingLeaves=5 exactSkip=5`,
  `rowWalkingLeaves=4 exactSkip=4` in the registration log).
- Byte identity: RTL pair 1080p 12 frames equals the reference `8ef40389...436aa`; look-dev 16 of
  16 equal the references; HUD 3 of 3 equal the references; all 20 `GlyphSdf` captures of the suite
  equal the captures of the base binary.
- Timing so far is one shared run on a busy device (2 to 4.5 times slow by the load indicators):
  frame period 330 to 480 ms against 1,410 ms on a quiet device before. A quiet number is pending.

## 1. Bases (approved by the controller during the lane)

| Repo | Base | Why not the brief's base |
|---|---|---|
| Kernel | `8cfb34b1f` (the kernel pin of the VIXEN wave) | `origin/main` (`061443bb1`) is 44 commits behind that pin (unlanded d11 kernel batch: material carry rules, GPU service ABI v5). VIXEN cannot build against a lane cut from `origin/main`. lane-k12 and the bench fixes are not needed. **To do at landing: rebase B1 onto kernel main after d11 lands, rerun full CG.** |
| VIXEN | `lane-gpusubmit1` `57a94e7a5` (wave `7daa62d89` + 14 commits) | Every before number and reference hash in fable-rtlrt1 was taken with the gpusubmit1 binary (bounded submits, watchdog guard, glyph hull skip). Before-from-history is valid only on that base. |

Velocity tips: kernel `velocity-1` and VIXEN `velocity-2` are already ancestors of both bases.

## 2. Step 1: B1, exact skip of row-walking leaves

### SHAs

| Repo | Commit | Content |
|---|---|---|
| Kernel `lane-glyph1` | `aab87eb8b` | `RecipeGpuServiceEmitter.cs` (field walk + derived predicates), tests, two fixtures |
| VIXEN `lane-glyph1-v` | `8bd4bcb35` | kernel pin `8cfb34b1f` -> `aab87eb8b`; registration log line and located warning |

### What changed

The recipe GPU service emits one field function per recipe as straight-line statements. A program
with a row-walking leaf `L` (a value callable that takes a fixed-bucket instance row:
`GlyphOutline`, `PathDistance`) in the shape

    acc, L, C.., Intersect, U.., Subtract|Union        (or  acc, C, L, Intersect, U.., ...)

is now emitted as

    <C statements>
    precise float tL = tC;              // the leaf stands in as its Intersect partner
    <Intersect and U statements, unchanged>
    if (!((-(tChain)) < (tAcc))) {      // Union: if (!((tChain) > (tAcc)))
      tL = SdfCore_GlyphOutline...(...);
      <the same Intersect and U statements again, as assignments>
    }
    precise float tK = SdfCore_Subtract(tAcc, tChain);

Why it is exact: the merged value is `max(acc, -U(max(L, C)))` or `min(acc, U(max(L, C)))`, and
`U` is a chain of non-decreasing binary32 steps, so `U(max(L, C)) >= U(C)` whatever `L` is. Where
`U(C)` already loses against `acc` (strict compare, so no tie and no signed-zero case) the merge
returns `acc`. A NaN makes the compare false, which evaluates the leaf as before. The material
expression selects the same side for the same reason. It is exact at every point, including inside
the solid, where the rtlrt1 probe P2 (an early return on the box alone) was not.

Every fact the plan uses is derived, no new attribute (R665.3), no runtime switch (R857.8):
- leaf: callable with a `GlyphInstance` or `FixedBucketInstance` argument, pops 0, pushes 1;
- `Intersect`, `Subtract`, `Union`: the opcode's interval rule in the generated metadata table;
- non-decreasing steps: a unary callable whose body is its distance input plus or minus a term
  that does not read it (today: `Round`; the brief's "non-negative radius" is not needed, `d - r`
  is monotone in `d` for any `r`), a `RestorePos` whose saved distance scale is positive and
  finite, a `MaterialRef` + `Attach` pair. Anything else (`Onion`, smooth operators) ends the shape.

The walk records text and stack per instruction only for a program that has such a leaf; the
rewrite validates each plan against the recorded text and stack and drops a plan that does not
fit (the leaf is then emitted as before). Both dialects (GLSL, HLSL) share the walk.

A leaf outside the shape is emitted as before and listed in the module manifest
(`"rowWalkingLeaves": {"count", "guarded", "unguardedInstructions"}`; the key exists only for a
program with such leaves). VIXEN prints `rowWalkingLeaves=N exactSkip=M` at registration and a
warning with recipe id and instruction index for each unguarded leaf.

### Kernel tests

- `RecipeGpuServiceTests` 13/13 (`fable-glyph1:k-leafskip-b1b`). New tests:
  - `RowWalkingLeavesAreSkippedOnlyWhereTheMergeCannotChange`: the emitted GLSL field, run on the
    CPU through GLM, against the same field with every guard forced open, 60,000 points in and
    around the plaque, 8 fixtures (carved, embossed, partner below the leaf, unit scale, row
    material, plaque material, material embossed, a real `PathDistance` row): 0 distance-bit
    mismatches, 0 material mismatches. **Deliberate break**: the same field with the bound wrong by
    0.05 must differ, and does. The leaf must run at fewer samples than without the guard. Two
    unguardable shapes (no `Intersect`; an `Onion` in the chain) stay unguarded and are named.
  - `ARecipeWithoutARowWalkingLeafKeepsItsFieldText`: the field text of a leafless plaque equals
    `Fixtures/RecipeGpuLeaflessPlaqueField.glsl` byte for byte. That file is the text the **base**
    generator emitted (taken from a run with the base emitter restored,
    `k-leafskip-base2.log`), not text from the new one.
  - `TheGuardedFieldsCompileWithTheHostCompiler`: glslang compiles six guarded fields with the
    kernel's own glyph kernel bound.
- Full suites once (`k-suites.log`, paired undertow `aa6b95edf`): **SG 1505/1505. CG 3827 passed,
  8 failed, 2 skipped.** The 8 are the base reds the controller named (see REDS); nothing else failed.
- CPU/GPU parity: the kernel CPU recipe service refuses instance-buffer opcodes at lower time, so
  there is no kernel CPU face of a glyph or path leaf to compare with or to guard. The existing
  CPU/GPU parity tests (leafless programs) pass unchanged; their field text is unchanged.

Not captured: the per-fixture counts (leaf calls with and without the guard, broken-bound
mismatch counts). The test asserts them but a passing run does not print them; the test now
writes them to its output (`TestContext.Out`), visible with a detailed logger.

### Native witness (binary: `C:\vixen-native\glyph1`, full `vixen-ninja-release`, `native-build-1.log`)

Configure line: `[codegen] using tracked Yeroket kernel pin: aab87eb8b49ff17abd64392eb21ca47e0d61828b`.
The build ran every `*_check` target (golden checks matched).

| Witness | Result | Evidence |
|---|---|---|
| RTL pair 1080p, 12 frames | `8ef40389543c7b0ace7552928cf4c9c314a2533160eea11838c98a33073436aa`, **identical to the reference** | `b1-rtl-1080-shared.log` |
| RTL pair 4K, 12 frames | **not run yet** (exclusive job queued) | reference `c5a9d48f...037fe` |
| Look-dev 16 | **16 of 16 identical** to the references in fable-gpusubmit1.md | `b1-lookdev16.sha256` |
| HUD 3 | **identical**: `754bcd8e...a157`, `7c4d3688...3801`, `cdb9b393...52a3` | `b1-hud-capture.log` |
| All 20 `GlyphSdf` captures of the suite (latin and qps-rtl, carved and embossed, RTL pair and submit guard, analytic-vs-voxel, isolation set) | **identical** to the captures in the base binary's tree (`C:\vixen-native\gpusubmit1\...\runtime-captures\glyphsdf`, read only) | `b1-glyphsdf.sha256`, `base-gpusubmit1-glyphsdf.sha256` |
| Native GPU CTest, affected set (label gpu, `LookdevCapture|Recipe|UberShader|Glyph|SwitchCost|DeclaredPosition|NestedInvocation`, same exclusions as fable-gpusubmit1) | **106/106 passed**, 0 failed, 3 not run (declared Disabled/Skipped), no device loss, 1805 s | `b1-gpu-ctest-affected.log`, `C:\vixen-native\glyph1-scratch\b1-suite\ctest.log` |
| Registration log | `recipeId=1197034576 rowWalkingLeaves=5 exactSkip=5`, `recipeId=1197034577 rowWalkingLeaves=4 exactSkip=4`, no warning | `b1-rtl-1080-shared\run.log:45,48` |

### Timing

Before is from history (fable-gpusubmit1 `final-rtl-1080`, `final-rtl-4k`, quiet device, shipping
rule). After, so far, is **one shared run on a busy device**; the fixed small passes show the load.

| RTL 1080p, steady frames | Before (quiet) | After (shared, busy) |
|---|--:|--:|
| `test_dispatch` (march) | 1,150 ms in 180 submissions | 128 to 153 ms in 1 submission |
| `shadow_visibility_wave` | 228 ms in 17 submissions | 112 to 152 ms in 1 (once 14) |
| frame period | 1,408 to 1,420 ms | 330 to 480 ms |
| load indicators: Hi-Z / exposure meter / shading | 0.23 / 0.088 / 1.36 ms | 1.03 to 1.38 / 0.18 to 0.36 / 2.0 to 2.3 ms |
| worst frame of the run | first frames | 1,364 ms once (frame 9 of 12; the wave refined to 14 commands in that frame; needs the quiet run to read) |

The device was 1.5 to 4.5 times slow during that run, so the after column is an upper bound, not
the acceptance number. **Steady and worst frame at 1080p and 4K on a quiet device are not
measured yet.** The probe expectation was march 36.6 ms, wave 32.3 ms, period 75 to 81 ms.

One thing to read in the quiet run: `RtlProof.Pair` took 227 s inside the suite (first run in a
fresh tree, cold pipeline cache, other GPU tests in parallel); `RtlProof.SubmitGuard` right after
took 8.7 s (13.5 to 23.6 s on the base). If the cold pipeline compile of the guarded field is
slower than before, that is a start-up finding for cx-startup1; I did not separate it.

### GPU runs used: 2 of 14 (0 of 6 exclusive)

| # | Run | Mode | Purpose |
|--:|---|---|---|
| 1 | `b1-gpu-ctest-affected` | shared | affected native GPU CTest, includes look-dev 16 and every glyph capture |
| 2 | `b1-rtl-1080-shared` | shared | RTL 1080p hash + a first timing |
| light | `b1-hud-capture` | shared | HUD 3 |

## BENCH REQUEST

Two exclusive jobs are queued again (`fable-glyph1:b1-rtl-1080`, `fable-glyph1:b1-rtl-4k`). The first
submissions waited 40 to 55 minutes and exited 79 after `bench-not-taken-open-timeout` /
`remaining-estimate-...-exceeds-...` refusals (`b1-rtl-1080.refused-1.log`, `b1-rtl-4k.refused-1.log`).
When this report was written both were still waiting behind three other benchmarks (five
queued, node busy). They are held by this agent's session, so they end with it: treat them as not
run. Run in a planned quiet window:

    bash ~/codeman-cases/undertow/reports/fable-glyph1-evidence/cap.sh b1-rtl-1080 rtl 1920 1080 12 "" bench
    bash ~/codeman-cases/undertow/reports/fable-glyph1-evidence/cap.sh b1-rtl-4k   rtl 3840 2160 12 "" bench
    bash ~/codeman-cases/undertow/reports/fable-glyph1-evidence/summ.sh b1-rtl-1080   # per-pass table + frame periods

(remove an existing `C:\vixen-native\glyph1-scratch\<name>` directory and `<name>.log` first; the
runner refuses to overwrite). Accept: 1080p hash `8ef40389...436aa`, 4K hash `c5a9d48f...037fe`;
report march, wave, steady period and worst steady frame with the three load indicators.

## LANDABLE NOW

Not yet: B1 waits for (1) the quiet acceptance timing and the 4K hash, (2) the rebase onto kernel
main after d11 lands with a full CG rerun (controller instruction). The commits are self-contained
and green on their bases: kernel `aab87eb8b`, VIXEN `8bd4bcb35`.

## 5. Hand-over for the steps not started

### Step 2 (B5): what the analysis service needs, found while reading it

`RecipeAnalysisServiceText.cs`:
- `DeriveBound` uses the generated `TryRecipeExtentTransfer`; `GlyphOutline` has no extent rule, so
  the program is refused (`NotCertified`). A leaf has no extent of its own; the sound rule is
  "unbounded, and `Intersect` takes the bounded operand's extent", which gives the row's box.
- `DeriveOccupancy` evaluates the field with `KernelScalarOracle::Evaluate`, whose glyph and path
  spans are empty by construction (`RecipeServiceLowering.cs`, "Programs reach it ... without
  instance-buffer opcodes"). Two ways to cover a glyph program:
  1. pass the rows through the analysis ABI (new request members, VIXEN supplies the registry's
     glyph blob) and let the oracle walk them. Exact, but an ABI and a CPU glyph face;
  2. **recommended**: bound the field without the leaf, using the same fact B1 uses. With `L`
     replaced by its partner (`Intersect -> C`) the program is leafless and gives one bound of the
     field, and with the row removed (`acc`) it gives the other:
     carved `acc <= field <= max(acc, -U(C))`, embossed `min(acc, U(C)) <= field <= acc`. The
     occupancy value of a cell is the smaller distance to zero of the two bounding fields (0 where
     they straddle zero), minus the usual margin. Both programs are ordinary certified programs,
     so the existing coverage table, Lipschitz certificate and grid code apply unchanged, and no
     glyph row reaches the analysis.
  The plan B1 emits (`PlanLeafSkips`) is exactly the rewrite rule for (2); it should move into the
  shared lowering so the analysis and the GPU walk read one plan.
- Derived bounds default: `VulkanGraphApplicationRecipes.cpp:734` (`DerivedBoundsEnabled`,
  env `VIXEN_RECIPE_DERIVED_BOUNDS`) and `:747`. Removing the env switch changes all 13 look-dev
  recipes: that is the re-golden (owner reviews new captures; references stay until then).
- Unexplained from rtlrt1, still open: the RTL recipes are authored with radius 18 and register
  with `boundRadius=24 (engine-default)`.

### Step 3 (B4): segment grid

The walk is hand-written (`VIXEN/shaders/RecipeGlyphBindings.glsl`, `SdfCore_GlyphOutlineBounded`),
while the kernel has its own generated `SdfCore_GlyphOutline`. The grid is kind-shaped data, so the
R879 question in the B4 brief is real: the grid belongs in the kernel's glyph row declaration and
both walks should be the generated one. Decide after the quiet B1 number shows what the walk still
costs at 1080p.

### Step 4 (controller, 11-16): fewer steps for grazing rays

Not started. Recorded as given: probe first (steps-per-ray histogram, RTL and look-dev, grazing
split, instrumentation compile-gated per R889; emulated relaxed / segment-traced march giving step
reduction and hit deviation), use the R602 generated gradient faces for the exact directional
slope `dot(grad f, dir)`, check whether second derivatives compose, measure the gradient's cost,
message the controller with numbers before implementing; the implementation goes through owner
capture review. One interaction with B1 to keep in mind: the guard makes a field sample cheap
exactly where the row cannot matter, so the remaining cost is concentrated in steps near the text
surface, which is where step reduction pays most.

### Follow-up item (controller: note, not in scope)

VIXEN's own recipe VM (`libraries/SVO/include/Recipe/SdfRecipeEval.h`, generated dispatch)
evaluates glyph leaves unguarded on the CPU. Same result, no CPU bake gain yet. The same plan
would apply there.

## REDS

Base reds, not from this lane (controller reference: retired `--parallel` in native-fixture
payloads refused by the live queue; fixed by kernel `78551d837` inside d11 kernel `eafa75d07`;
cx-harden1 hit the same 8 on kernel `origin/main`). Log:
`~/projects/Yeroket-Fantasy/.claude-worktrees/glyph1/.tmp/suites/kernel-test-suite/cg.log`.

1. `AFailingAssertionFailsTheWitnessUnderTheReleaseDispatcher`
2. `CompositionEntersTheGeneratedPlanAndItsOwnerDrainsAfterTheSeal(1,False)`
3. `CompositionEntersTheGeneratedPlanAndItsOwnerDrainsAfterTheSeal(4,False)`
4. `CompositionEntersTheGeneratedPlanAndItsOwnerDrainsAfterTheSeal(4,True)`
5. `GaiaWorldRoundTripsAfterSparseChunkRemovals`
6. `GeneratedTickFinalizesDeletionsBeforeSaveAndReleasesChunks`
7. `RealHaulPublicationAndScheduledOwnersPreserveConsumptionFuelCargoAndTransitRules`
8. `ScheduledSetOwnerRoutesParticipantsAndCommitsTheFetchedRows`

I did not rerun them on the base myself; the attribution is the controller's.

Cold build (lane rule: a cold build is a red): `C:\vixen-native\glyph1` is a new native tree,
warned and approved; 1396 steps, about 40 minutes with ccache.

## CANONICAL-SHAPE CHECKLIST

Kernel and VIXEN added lines: no marker class, no `KindRef`, no `EntityId`, no attribute, no
`Catalog<T>`, no `[GpuStruct]`, no Python, no env switch, no runtime throw. One new C++ struct in
VIXEN (`RecipeRowWalkingLeaves`, a parsed report of the service manifest, not kind-shaped data).
`Program.Legacy.cs` line delta: 0.

## CONSOLIDATION ISSUES

- **An exclusive GPU job can wait 40 to 55 minutes and then exit 79.** Both first submissions
  ended with a refusal line and a `retry-command`, no run. A lane has to notice the exit code and
  resubmit. The refusal should either keep the job queued or say plainly that it gave up.
- **A passing kernel test prints nothing**, so measured counts asserted by a test (here: leaf
  evaluations with and without the guard) are not in the queue log.
- **The existing shader-compile test enumerates every `*.comp` in the shared provider directory**
  (`TheEmittedShadersCompileWithTheHostCompilers`), so a sibling test that writes a `.comp` there
  breaks it. I named my files `.shader`.
- **`RecipeServiceEmission.Directory` is deleted at process exit**, so generated text of a test run
  cannot be inspected afterwards; I printed the fields I needed.
- **Kernel `origin/main` cannot build the VIXEN wave** (the wave pins an unlanded kernel commit);
  a brief that says "kernel lane from origin/main, regenerate into VIXEN" cannot be followed
  literally until d11 lands.
- Step under-use: the native build sat about 6 minutes at `[1370/1396]` on golden-check steps with
  the queue grant mostly idle (`native-build-1.log`).

## SPT DISPOSITION

No SPT task id was given. No proposal filed from this lane (the items above are for the
controller to file or fold into existing ones).

## Evidence

`~/codeman-cases/undertow/reports/fable-glyph1-evidence/` on ws: queue logs (`k-*.log`,
`native-*.log`, `b1-*.log`), runners (`cap.sh`, `summ.sh`), hash lists, the base generator's field
texts inside `k-leafskip-base2.log`. Native: `C:\vixen-native\glyph1-scratch\` (`capture.ps1`,
`suite-affected.ps1`, per-run `run.log`).
