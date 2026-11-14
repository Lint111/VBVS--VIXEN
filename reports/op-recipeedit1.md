# op-recipeedit1: runtime recipe editing as fast as possible: measure and design (R864, R841.4)

## STATUS

**DONE (measure and design).** One quiet-window run (`FULL-MACHINE-BENCH`, 14 recipes) and five non-quiet runs (13 to 163 recipes, 13 or 2,197 instances).

- **One cost dominates everything.** Any edit that changes the program, whether a literal tweak, a structural change or a new recipe, makes the Vulkan driver compile four large uber pipelines from scratch (BodyInstanceRayMarch, ProbeGather, ShadowVisibilityWave, ShadowRayTrace), one after another, **on the frame thread**. The frame thread is frozen for the whole compile. Edit-to-visible is **17.1–19.6 s in a quiet window with 14 recipes** (24–38 s contended), **82–89 s with 63 recipes and 115–140 s with 163** (non-quiet). GLSL to SPIR-V is only 0.19–0.53 s of that, source emission 7–28 ms, CPU registry work 6–12 ms, the device drain ~0 ms. The driver's ISA compile of the spliced recipe switch is **about 99%**.
- **The same edit is 0.16 s (quiet) to 0.86 s (contended, 2,197 instances) when the machine has compiled that exact source before.** VIXEN keeps a persistent shared VkPipelineCache, and the swap then takes 3–8 ms. That is the number earlier "warm" measurements saw (sn-gpuquery1's 0.4–0.7 s): their edit values repeated across runs. A real author's tweak is a value nobody has compiled before, so in practice it pays the cold cost every time.
- **Data edits are already fast.** Parameter, transform and material edits go through `SetBodyInstances`. They are visible in the frame they are called in (0 extra frames: 34 ms edit-to-submit against a 31 ms quiet frame) and cost 0.1–0.2 ms of CPU at 2,197 instances.
- **Today, every numeric shape value is a literal compiled into the shader.** Extents, radii, blend widths, bound spheres and occupancy-grid offsets are baked into one source per shader family, and that source holds all registered recipes. A one-float tweak to one recipe therefore recompiles every recipe in seven shader families, and the cost grows with the size of the registry, not the size of the edit. The kernel GPU service already has the fix for literals: `YK_RECIPE_GPU_FIELD_TAPE_OPERANDS` reads operands from a buffer, so the source contains no literal words. VIXEN does not use it, and neither does the recipevix3 port.
- **Design: no edit class compiles before it is visible.** Literal and bound edits become operand-buffer data, visible in the next frame. Structural and new-recipe edits render the next frame through the kernel's tape evaluator, compiled once and fed from a buffer. The specialised pipeline then compiles in the background, per recipe instead of per registry, and swaps in without stalling the frame thread. A lowering change reloads the service module in-process (R841.4).
- Lane plan: 8 lanes. The top three: an operand-buffered splice with bound/occupancy moved into an SSBO (kernel + VIXEN, M), a tape route for new or dirty recipes (VIXEN, M–L), and a swap off the frame thread. The swap item is sn-gpuquery1b's precreate, and it must use this bench as its gate.

Branch `lane-recipeedit1` (VIXEN, never pushed), base `wave/authoring-convergence` **4d202765** + `lane-gpuquery1` 7a42263f (fast-forward; it carries framebase1, loadscene1 and the async compile; the brief allows building on it). The wave tip was still 4d202765 when I captured (R858.1 checked: `git fetch` before the build, no newer tip).

## WHAT TODAY'S CODE DOES PER EDIT CLASS (read from code, then measured)

| class | seam today | what happens | compile? |
|---|---|---|---|
| parameter value (`ReadParam*` slots) | `SetBodyInstances` → `BodyInstanceMaterialGpu.recipeParams[6]` | the whole instance list is restaged (`RefreshInstanceStreams`, O(n) + memcmp); every ring slot is marked material-dirty; `rtQueryInstanceEpoch_` is bumped, so the RT-query TLAS is rebuilt; the slot is uploaded in the next `Execute` (`BodyOctreeSceneNode.cpp:261-312, 928-940`) | no |
| transform / placement | same | the same path; the transform stream is copied every frame anyway | no (unless the ring grows: `MarkNeedsRecompile`) |
| material / colour | same | the same path; the TLAS epoch is bumped even though only the colour changed | no |
| literal operand (any authored number in the program) | `ReplaceProceduralRecipe` + `RecompileProceduralShader` | on the frame thread: `ApplyRecipeBoundsDefaults` + `DeriveOccupancyGrid` (CPU scalar sampling) + registry + content cacher (`VulkanGraphApplicationRecipes.cpp:833-894`). Then 7 shader-library builders re-splice **every** recipe into their source (`UberShaderSplice.h`); 7 glslang builds run on workers; the next Update after all are ready runs `vkDeviceWaitIdle`, marks the 7 libraries dirty, and `RecompileDirtyNodes` rebuilds a **48-node** wave on the frame thread (shader modules, 4 large pipelines, descriptor gatherers, dispatch nodes) | yes, all 7 families |
| bound sphere / occupancy grid | none separately; changed by any Replace | baked as GLSL literals in `getRecipeBoundSphere`/`getRecipeOccupancyGrid` switches (`UberShaderSplice.h:143-186`) | yes |
| structural (opcode tape, tree shape) | as literal | identical to the literal path | yes |
| new recipe | `RegisterProceduralRecipe` + recompile | identical, plus a new `case` in every switch. An instance moved onto the id before the swap hits `default:` (zero field) | yes |
| kernel lowering change | build time | the generated recipe headers change; **23 VIXEN objects** recompile (measured from the native build's ninja deps: 15 in `application/main`, 8 in `libraries`; 396 s of compile at -j1, contended), VixenApp relinks (2.4 s lib + 6.9 s exe), the app restarts, and every pipeline is new (cold) | yes, plus a rebuild and restart |

## MEASUREMENTS

Harness (new, `vixen_recipe_edit_bench`, commit `08807602`): the look-dev scene at 1920×1080, Release with `UNDERSET_NATIVE_BENCHMARK=ON`, native Windows Vulkan (Radeon 8060S), offscreen, INFO log, each run its own queue job. Edits fire one at a time from a fixed cycle: param, transform, material, literal, revert, structural, revert, newrecipe, literal, revert. Each edit waits until it is visible, then the run waits 12 frames before the next. Every literal, structural and new-recipe edit uses a value never used before in the run. A revert puts back bytes the run has already compiled.

**Edit-to-visible** = from the edit call to the end of the `Render()` whose submit first carries the edit (`t_submit`). GPU completion of that frame adds one GPU frame (46–50 ms at 1080p, 13 instances). Work time (update + render on the frame thread) is used, not the paced period (R853).

**Quality:** `Q` = admitted as a quiet window (`FULL-MACHINE-BENCH`, window `20261112T044828-3993479`, concurrent 0, load 3.5). `L30`/`L18` = contended (host load 30/18, 1–4 concurrent jobs). `L3.6` = near-quiet (load 3.6, 0–1 concurrent), still `INVALID-FOR-COMPARISON`. Compile time is driver CPU time, so contention inflates it about 1.8x (Q 17.6 s vs A 31.8 s for the same 13–14-recipe registry). Use Q for absolute numbers and A/C/D for the scaling shape.

### Edit-to-visible per class (median, max in brackets; ms)

| class | **Q: 14 recipes, 13 inst, quiet (n)** | A: 13 recipes, 13 inst, L30 (n) | C: 63 recipes, L18 (n) | D: 163 recipes, L3.6 (n) | E: 13 recipes, 2,197 inst, L3.6 (n) |
|---|--:|--:|--:|--:|--:|
| param (data) | **34 (44) (3)** | 55 (70) (5) | 63 (74) (3) | 54 (61) (3) | 203 (218) (3) |
| transform (data) | **34 (39) (3)** | 50 (61) (5) | 69 (71) (3) | 57 (59) (3) | 207 (216) (3) |
| material (data) | **36 (43) (3)** | 65 (79) (5) | 60 (66) (3) | 54 (56) (3) | 208 (218) (3) |
| literal, never-seen source | **17,857 (19,583) (6)** | **32,190 (38,268)** (8) | **82,397 (89,887)** (6) | **114,766 (118,136)** (6) | (not measured: all hits) |
| structural, never-seen | **17,149 (17,916) (3)** | **32,854 (36,446)** (4) | **85,126 (89,346)** (3) | **117,471 (126,009)** (3) | |
| new recipe, never-seen | **17,684 (18,591) (3)** | **35,785 (37,908)** (4) | **85,106 (88,742)** (3) | **115,923 (125,447)** (3) | |
| revert to the boot state (first revert of a run) | 17,237 (1) | (hit from the smoke run) | 83,409 (1) | 139,695 (1) | |
| any compile class, source seen before (pipeline-cache hit) | **157 (173) (8)** | 292 (median; 169–497) (19) | 425 (522) (8) | 274 (276) (8) | 497–675 (24) |

Data edits are visible in the frame of the call: the "frames" column is 0 in every run, and the totals above are one frame of work time. `SetBodyInstances` itself costs < 0.05 ms at 13 instances and **0.1–0.2 ms at 2,197**. E's compile classes are all hits because A had already compiled the same registry states; the miss cost is the driver compile, which does not depend on the instance count.

### Where a never-seen compile edit spends its time (medians over the misses; Q quiet, A/C/D for scaling)

| phase | thread | **Q (14, quiet)** | A (13, L30) | C (63, L18) | D (163, L3.6) |
|---|---|--:|--:|--:|--:|
| C++ host / C# host | | none: the edit is C++ to C++ today (no C# host on this path) | | | |
| registry: bounds + occupancy grid + content cacher (`mutate`) | frame | **6.5–7.6 ms** | 8.6 ms | 8.5–12.0 ms | 6.2–8.2 ms |
| emission: re-splice of 7 sources (`emit`; 8.8 ms is `compute_shader_lib`, 0.3–0.6 ms each for the others) | frame | **7–10 ms** | 11 ms | 16–28 ms | 15–17 ms |
| GLSL → SPIR-V, 7 libraries in parallel: wall (CPU sum) | workers | **186–199 ms** | 267 ms (1.2 s) | 463–532 ms (2.3–2.6 s) | 399–416 ms (1.7 s) |
| ready → swap (next Update polls) | | **(1 frame)** | 22 ms | 17 ms | 34 ms |
| frame-thread work time while the compile runs (p50 vs steady) | frame | **33.4 vs 31.4 ms** | 61 vs 48 ms | 55 vs 48 ms | 55 vs 51 ms |
| **swap: Update containing drain + 48-node recompile** | **frame** | **16,865–17,555 ms** | **31,774 ms** | **81,748 ms** | **114,185 ms** |
| of which `vkDeviceWaitIdle` + `WaitForGraphDevicesIdle` | frame | 0.0 + 0.0–0.1 ms | 0.0 + 0.1 ms | 0.0 + 0.1 | 0.0 + 0.1 |
| of which shader modules / descriptor gatherers / other nodes | frame | < 1 ms each | 0.7 / 1.0 / 1.0 ms | similar | similar |
| **of which pipeline creation** | **frame** | **≈ swap** | **31,771 ms** | **81,745 ms** | **114,180 ms** |
| — BodyInstanceRayMarch (`test_compute_pipeline`) | | **7,022** | 12,917 | 28,161 | 38,692 |
| — ProbeGather | | **4,772** | 8,750 | 22,024 | 31,858 |
| — ShadowVisibilityWave | | **4,293** | 8,206 | 21,552 | 35,308 |
| — ShadowRayTrace | | **1,252** | 2,430 | 7,553 | 13,006 |
| — SpatialReuseShade | | < 0.1 (cache) | ~508 (smoke) | | |
| first submit after the swap (first use of new pipelines/descriptors) | frame | **34–38 ms (≈1 frame)** | 112 ms | 180 ms | 129 ms |
| **total edit → first submit** | | **17.1–17.9 s** | **32.2 s** | **82.4 s** | **114.8 s** |

The per-pipeline numbers are from the `[PHASE] stage=vkCreateComputePipelines` lines (run logs). The four pipelines are created **serially**. Small pipelines that splice no recipe (ProbeApply, InstanceOcclusionCull) take 52–54 ms, so the cost is the spliced recipe switch, not pipeline creation as such. A cache hit takes the same path with a 4–8 ms swap, and the rest of its 0.27–0.5 s is SPIR-V (the content-keyed disk cache still preprocesses and hashes: 92–290 ms), the frame-boundary poll and the first submit.

### Other measured facts

- **Going back to the boot registry state misses every cache.** The first revert of each run (smoke2, Q, C, D) rebuilt all four pipelines cold even though the registry held the boot bytes again. The boot source hashes differ from the post-Replace hashes for identical contents (smoke2: `BodyInstanceRayMarch:c1455fa0…` at boot vs `6706df31…` after the revert; later reverts in the same run hit). The source is therefore not a pure function of the recipe contents. Cause not isolated; candidate: the order in which `RecipeContentCacher`/the registry assign families after a Replace. This matters for prewarm keyed by content.
- Registry-size scaling of one never-seen edit: 13 → 63 → 163 recipes gives 32 → 82 → 115 s (A and C contended, D near-quiet); quiet at 14 recipes: 17.6 s. The switch-scaling knee near N≈100 recorded in `Vixen-Docs/01-Architecture/Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07.md` (frame cost) shows up here as edit latency too.
- Kernel lowering change today: 23 dependent objects out of the 266 in the `vixen_recipe_edit_bench` graph (`ninja -t deps`). Their compile times from `.ninja_log`: 396 s summed, at -j1 on a contended host (`buildrendergraph.cpp` 54 s, `vulkangraphapplicationrecipes.cpp` 44 s, …). The restart and cold pipelines follow.

Evidence: `reports/op-recipeedit1-evidence/` (`runs/<run>/` edits.csv, frames.csv, summary.md, pipeline-creates.txt, compressed frame-trace/perf/run.log; `analyze.py`, `run-one.sh`, `chain1.sh`, `collect.sh`, `ninja-deps.txt.xz`, `ninja_log.txt`, queue logs in `logs/`). Native outputs: `C:\vixen-native\recipeedit1-evidence\<run>\`.

## WHAT IN THE SPECIALIZATION KEY FORCES NEEDLESS RECOMPILES TODAY

The effective key of every recipe-evaluating pipeline is the whole spliced source text: all N registered recipes, all their literal words, plus per-recipe metadata. R841.1 allows content family, contract, device caps, channels, declared-position mode, domain/proof, resource ABI and hotness tier. These parts of today's key go beyond that:

1. **Literal operands.** Every `SdfInstruction.data[]` word is an exact binary32 literal in the source (`uintBitsToFloat(Nu)`), so any numeric tweak is a new key. The kernel service's `TAPE_OPERANDS` flag removes them from the source (`RecipeGpuServiceEmitter.cs:451-458`: operands become `recipeTape[recipeOperandBase + i].data[k]`). It is unused.
2. **Bound sphere and step relaxation** as switch literals (`UberShaderSplice.h:143-156`).
3. **Occupancy-grid offset, dim, AABB and cell size** as switch literals, with a *running offset*: changing one recipe's grid shifts the offsets of every later recipe (`UberShaderSplice.h:165-186`).
4. **All recipes in one source per family.** A one-recipe edit invalidates all N field functions in all 7 families (4 of them heavy), and the cost scales with N.
5. **`recipeId` case labels.** A new recipe is a new key everywhere. Ids are an alias, not content (R841.1).
6. **Boot vs Replace source instability** (above): the same contents produce two keys.
7. **The swap's recompile closure:** 48 nodes rebuild for a shader-module change whose descriptor layout and pipeline layout did not change.

## DESIGN: the fastest path per edit class

Shape per class: inputs → logic → outputs. Shared pieces first.

### Shared pieces

- **Recipe operand table (data).** For each registered recipe: its closed instruction records (the 132-byte container records) in the existing `recipeTape` SSBO, at `recipeOperandBase[recipeId]`. It is written through the frame packet's on-change tier (R851.4/.10: delta, two slots, R851.13). Bound sphere, relaxation and occupancy offset/dim/AABB/cell go into a per-recipe metadata row in the same packet block instead of switch literals; the occupancy grid values stay in their blob.
- **Two GPU evaluation routes behind the one `evalRecipeField(recipeId, p, params)` entry:**
  - *Tier S (specialised):* a direct field function lowered by the kernel GPU service with `TAPE_OPERANDS`. Its source depends only on the opcode sequence, so the key is the **shape**: the content digest with literal words masked. The kernel manifest's `sourceDigest` is already that, because a `TAPE_OPERANDS` source carries no literals.
  - *Tier 0 (generic):* the kernel tape evaluator (`GLSL_TAPE` backend / `RecipeTapeEval.g.glsl`), compiled **once** at the maximum storage counts of the stack contract (64/64) and fed tapes from the buffer. A per-recipe **route word** in the metadata row (`0 = tier S case, 1 = tape at offset`) selects the route. The uber switch's `default:` calls the tape, so an id with no compiled case is still drawn.
- **Caches (R841.1):** L1 lowered module in-process, keyed by `(shapeDigest, backend, flags, kernel contract digest)`; L2 SPIR-V on disk, keyed by the source SHA (exists); L3 VkPipelineCache plus the `ComputePipelineCacher`, keyed by the SPIR-V digest and layout (exists, persistent). Shape keys make literal edits hit all three.
- **Swap without stall:** pipelines are created on workers (sn-gpuquery1b's precreate), and the new handle is swapped into the dispatch node at a frame boundary. The old one is retired on its frame fence. No `vkDeviceWaitIdle` and no node-wave recompile when the layout is unchanged.

### Per class

| class | inputs | logic | outputs | compiles before visible |
|---|---|---|---|---|
| parameter / transform / material | per-instance field delta (row id, field, value) | write the changed rows into the frame packet slot (R851.10 delta: changed 64-B chunks only); bump the TLAS epoch **only** for transform changes | the GPU reads the row in the next frame | none, visible in frame N+1 at worst (today: frame N) |
| literal operand | recipe id, instruction index, data word, value | update the operand table row (packet on-change tier); recompute the bound/occupancy row **off the frame thread** through the kernel analysis service and publish it when ready (the old bound stays conservative or the instance stays ungapped meanwhile, as `RecipeOccupancy` already allows) | the new operand is read next frame by tier S or tier 0 | none (shape unchanged → same pipeline) |
| bound / occupancy only | recipe id, new metadata | metadata row update | next frame | none |
| structural | recipe id, new closed program | (1) validate and lower to a tape with the in-process kernel service (R841.4, C++ to C++); write the tape and set the route word to tape: **visible next frame**. (2) In the background: lower the tier S field for the new shape (L1 hit if the shape was seen), build SPIR-V and create the pipeline on workers, keyed by shape; swap the handle; route word back to tier S | next frame on tier 0, specialised later | none before visible; one background pipeline (per recipe once the uber is split, lane RE4) |
| new recipe | new id + program | as structural: tape route from the next frame; tier S joins on the next background rebuild. Instances may move onto the id at once (the route word makes it drawable) | next frame | none before visible |
| kernel lowering change | regenerated service module (warm C# host, R841.4) | rebuild the one provider TU; reload the service module in-process in dev builds; the contract digest changes, which invalidates L1/L2/L3 keys for every recipe. Recompile the tape evaluator **once** (the tier 0 source changed), route every recipe to tape, then re-specialise in the background, hot recipes first | everything visible on tier 0 after one tape-evaluator compile | one (the tape evaluator) |

**Prewarm.** Only two kinds of prewarm pay off. (a) At boot or session start, compile the tape evaluator and the tier S shapes of the registered recipes (this is what loading does today). (b) When an author selects a recipe in an editor, warm its shape plus the shape after each one-op structural change the editor offers, if that list is small (owner question). Literal edits need no prewarm under shape keys. Prewarm must key on the stable shape digest, and the boot/Replace instability has to be fixed first (RE7).

**What this removes.** Today's 32–140 s (13–163 recipes) becomes next-frame visibility for every class except a lowering change, with no frame-thread stall. Two background costs remain for structural and new-recipe edits: one specialised pipeline compile, which is per recipe after RE4 and per registry before it, and the operand-read cost of tier S (to be measured, owner question 1).

## TARGETS (and the gate)

Gate: `vixen_recipe_edit_bench` (this lane) at 1080p in a quiet `--resource bench` window, three repeats, at 13 / 63 / 163 registered recipes and 13 / 2,197 instances. All values are work time (R853).

| class | today (this report) | target | gate metric |
|---|---|---|---|
| param / transform / material | same frame; 0.1–0.2 ms CPU at 2,197 | same frame; **≤ 0.1 ms CPU per edited row** at 2,197; TLAS rebuild only on transform | `frames`=0; `mutate` ms; TLAS rebuild count |
| literal | 17.9 s quiet (14 recipes); 32 / 82 / 115 s at 13 / 63 / 163 non-quiet; the frame thread frozen for all of it | **next frame (≤ 1 frame), ≤ 2 ms frame-thread work**, flat in registry size | `frames` ≤ 1; total ≤ 1 frame + 2 ms; the same at 13/63/163 |
| bound / occupancy | (inside literal) | next frame; grid derivation off-thread | as literal |
| structural | 17.1 s quiet; 33 / 85 / 117 s non-quiet | **visible ≤ 2 frames** (tape); specialised swap **≤ 5 s** after the edit at 13 recipes, independent of registry size; swap-frame work ≤ steady p95 + 2 ms | `frames` ≤ 2; specialised swap time; max work time of the swap frame |
| new recipe | 17.7 s quiet; 36 / 85 / 116 s non-quiet | as structural | as structural |
| compile edit, source seen before | 0.16 s quiet; 0.27–0.68 s non-quiet | ≤ 2 frames | as above |
| kernel lowering change | 23 objects + relink + restart + all pipelines cold | **≤ 10 s to everything visible on tier 0** (R772: seconds), specialised re-promotion in the background | wall from service regeneration to first submit |
| any edit | frame-thread stall 17 s quiet (14 recipes), up to 140 s non-quiet (163) | **no frame over 2× the steady p50** during any edit | frames.csv max work time while an edit is in flight |

## LANE PLAN (ranked)

| # | lane | repo | size | depends on | does | done when |
|--:|---|---|---|---|---|---|
| RE1 | swap off the frame thread | VIXEN | (in flight) | — | **sn-gpuquery1b** precreates pipelines via `ComputePipelineCacher`; add: pipeline creates in parallel on workers, handle swap at the frame boundary, retire by fence, no `vkDeviceWaitIdle`, no 48-node wave when layouts are unchanged | this bench: swap-frame work ≤ steady p95 + 2 ms; edit-to-visible unchanged (it is still ~max(pipeline)) |
| RE2 | operand-buffered splice + metadata rows | kernel (if a flag is missing) + VIXEN | M | recipevix3 (splice through the service), R851 packet lane 4 (or the existing recipeTape SSBO in the meantime) | splice with `TAPE_OPERANDS`; operand table in `recipeTape`; bound/occupancy rows in an SSBO instead of switch literals; literal edits write data only | literal and bound edits: `frames` ≤ 1, no pipeline create; GPU cost of operand reads measured on `esvo_traverse_shade` (owner question 1) |
| RE3 | tape route for new and dirty recipes | VIXEN | M–L | RE2 | tape evaluator compiled once at the max storage counts; route word per recipe; `default:` → tape; structural and new-recipe edits route to tape at once, then re-specialise in the background | structural/new `frames` ≤ 2 at 13/63/163 recipes; parity of the tape vs direct on the corpus |
| RE4 | per-recipe specialised pipelines (break the uber) | VIXEN (+ kernel variant request) | L | RE3, RE1 | tier S per recipe (or per small group, R554 per bucket) instead of one switch over all recipes; pipeline cost scales with the edited recipe | specialised swap ≤ 5 s, flat in registry size |
| RE5 | data-edit deltas | VIXEN | S | R851 packet lane 4 (framepipe-packet) | row-range instance updates instead of whole-list `SetBodyInstances`; TLAS epoch on transform only | ≤ 0.1 ms per edited row at 2,197; no TLAS rebuild on colour/param edits |
| RE6 | in-process lowering cache + hot-reloadable service | kernel + VIXEN | M | recipevix3 | L1 module cache keyed by (shape digest, flags, contract); dev builds load the service as a reloadable module; contract-digest change → tape fallback + background re-specialise (R841.4) | lowering change ≤ 10 s to visible; zero VIXEN objects rebuilt |
| RE7 | stable source identity + prewarm | VIXEN | S | RE2 | find why the boot source ≠ the Replace source for equal contents; prewarm on shape digest | first revert to the boot state is a cache hit |
| RE8 | registry derivations off the frame thread | VIXEN | S | recipevix3 (analysis service) | bounds/occupancy through the analysis service on a worker; publish when ready | `mutate` ≤ 0.5 ms on the frame thread |

Order: RE1 (in flight) and RE8/RE5 can run now. RE2 → RE3 → RE4 after recipevix3 lands. RE6 after recipevix3; it is independent of RE2–RE4 but uses RE3's tape fallback. RE7 with RE2.

## OWNER QUESTIONS

1. **Operand-buffered fields: always, or only while editing?** (a) Always: one pipeline per shape, every literal edit is data, and each operand costs a buffer read on the GPU. (b) Edit mode only: buffered while a recipe is being edited, literal-baked again after it settles, so two variants per recipe and a background swap. (c) Specialisation constants for operands: the literal stays at the driver level, but every value change is still a pipeline create (the cost measured here), so it does not solve the problem. **Recommendation: (a) if RE2 measures ≤ 3% on `esvo_traverse_shade` at 1080p, else (b).**
2. **Is the tape evaluator the generic tier 0?** The old JIT direction treated the uber switch as "universal", but it is not: it needs a compile for every new recipe. (a) The tape is tier 0, compiled once, with a route word per recipe. (b) Keep the uber as tier 0 and only shorten its compile (RE4 alone). **Recommendation: (a).** It is the only option that makes structural and new-recipe edits visible without a compile.
3. **May an edit ever block the frame thread?** **Recommendation: never.** The old pipeline renders until the new one is ready, and a failed compile keeps the old one (R772 swap first, verify after).
4. **Hot reload of the lowering service module (R841.4) in which builds?** (a) dev + mod tooling builds load it as a reloadable module, and shipping links it statically; (b) always a reloadable module; (c) static only, with a lowering change meaning a restart. **Recommendation: (a).**
5. **Accept the targets above** (next frame for every class except a lowering change; ≤ 5 s specialised swap; ≤ 10 s for a lowering change; no frame over 2× the steady p50 during an edit)?

## STOPs

- **S1. Quiet-window coverage: 1 of 3 registry sizes.** Q (14 recipes) ran in a `FULL-MACHINE-BENCH` window. The 63 and 163-recipe scaling (C, D) and the 2,197-instance run (E) are contended or near-quiet. Their shape (cost grows with registry size; 99% pipeline creation) matches Q, but their absolute seconds are not quiet numbers (BENCH REQUEST).
- **S2. Design only, no product change.** RE2–RE4 change the uber splice, which sn-recipevix3 is porting now. Per the brief I did not touch the port.
- No STOP on the base or the build.

## LANDABLE NOW

VIXEN `lane-recipeedit1` (on `lane-gpuquery1` 7a42263f, which lands first or with it):

- `08807602` `feat(bench): recipe edit-to-visible latency bench per edit class` (`test_recipe_edit_bench.cpp/.cmake`, a `CMakeLists.txt` include; a benchmark-only FrameTrace span in `ShaderLibraryNode` (emit, SPIR-V), `RenderGraph::RecompileDirtyNodes` (per node, device idle) and `MarkProceduralShaderLibsDirty`; `IsProceduralShaderSwapPending()`; `run-load-scenario.bat` takes `SCENARIO_EXE`)

Checks: native Release benchmark build of `vixen_recipe_edit_bench` through the queue, no compiler warnings (`logs/build1.log`); 7 complete bench runs (smoke2, Q, A, C, D, E), all `edits=N ok`. The spans compile to nothing outside `UNDERSET_NATIVE_BENCHMARK`, so non-benchmark builds are unchanged. It is not a CTest case (benchmark executable, like `vixen_load_scenario`), so no RenderGraph suite was run; the only code change outside the bench is trace macros plus one const accessor. No schema, kernel, env switch or attribute; the bench reads `RECIPE_EDIT_*` harness variables only for its own options (the .bat cannot pass spaced arguments).

## BENCH REQUEST

Quiet window, one job per line (each 5–25 min). Do not reuse the values: run on a host whose shared pipeline cache has not seen these sources (or bump `--extra-recipes` by one, which makes every source new):

```
E=/home/liory/codeman-cases/undertow/reports/op-recipeedit1-evidence
# q-t0-x1-1080 (14 recipes) is done; the remaining quiet cells:
bash $E/run-one.sh q-t0-x51-1080  bench 0 51  3 1920 1080
bash $E/run-one.sh q-t0-x151-1080 bench 0 151 2 1920 1080
bash $E/collect.sh <name>   # then runs/<name>/summary.md
```

## FINDINGS (shared-finding shape; for the controller to file)

1. **A recipe edit freezes the frame thread for 17 s (quiet, 14 recipes) to 140 s (163 recipes, non-quiet)** (VIXEN, P1). Repro: `vixen_recipe_edit_bench`, any literal/structural/new-recipe edit with a never-seen value. Expected: the old pipeline renders until the new one is ready. Actual: four `vkCreateComputePipelines` calls of 2.4–39 s run serially inside `RecompileDirtyNodes` on the frame thread. Owner lane: sn-gpuquery1b (RE1). Size M.
2. **The edit cost scales with the registry, not the edit**: 13 → 63 → 163 recipes gives 32 → 82 → 115 s (VIXEN, P1). Cause: all recipes are spliced into one source per family, and literals, bounds and occupancy are compiled into it. RE2–RE4. Size L.
3. **The kernel `TAPE_OPERANDS` flag is unused**, so literal tweaks recompile (VIXEN consumer, P1). RE2. Size M.
4. **The boot source ≠ the post-Replace source for identical registry contents** (VIXEN, P2): the first revert misses every cache. RE7. Size S.
5. **`SetBodyInstances` bumps the RT-query TLAS epoch on colour/param-only changes** (VIXEN, P3; read from code, cost not measured). RE5. Size S.

## CONSOLIDATION ISSUES

- try → fail → remedy: my first smoke job ran under `timeout 3000` and was killed while still queued (bench windows of another lane held the queue for 50+ min). Remedy: detached chains. Gate: none needed. The queue's wait was correct, and the `timeout` was my mistake.
- The native build ran at **-j1** (`build jobs: 1 (source=queue cores=32 windows_available_mb=7914 other_uncommitted_mb=47488)`): vixen-native subtracts Linux queue reservations from Windows memory. This is the known defect sn-vxjobs1 is fixing. Wall 2,794 s for 681 steps. Not re-filed.
- `benchmark-configure.bat` is still untracked (copied from the gpuquery1 checkout), the same issue sn-loadscene1 and sn-gpuquery1 filed. Not re-filed.
- `ninja -t deps` needed a hand-written .bat in the native checkout (`opre-ninja-deps.bat`, untracked, outside the repo). A `vixen-native.sh ninja <args>` verb would make build-graph queries routine.

## SPT DISPOSITION

No SPT id was given for this lane (R864 brief). The OWNER QUESTIONS, FINDINGS and LANE PLAN are in the shared-finding shape for the controller to file.
