# RVA1 Run 2 — tile-local SDF recipe interval pruning

## LANDABLE NOW

- Kernel: `f1ac3fb0f8aa71f98075fb33e35d035999c4f334`, based on merged `origin/main` tip `bd05ce51`.
- VIXEN feature: `a53de758752ff06f1cf535064481d3d13c0f2dd6`, based on merge `8ee92e5c848b4907ac93984e4a2bd99b830f6138`, which contains wave commit `846ab1a999a542f663ee08764fe69b3de2beade3`.
- The recipe-only specialization produces a disposable per-tile postfix tape. Source recipe bytes and edit history remain unchanged.

## Changes

- Kernel declarations: `Packages/com.yeroket.utility.kernel-framework/Runtime/KernelCallableAttribute.cs:131` adds interval-rule metadata; `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs:13` declares Sphere, Box, hard Union, Subtract, and Intersect rules.
- Kernel lowering: `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RecipeLoweringModel.cs:377` reads the declarations, validates opcode/stack shape and data-slot bounds, and carries operand offsets into generated metadata. `RecipeDispatchEmitter.cs:53` emits the bounded postfix GLSL evaluator. `CodegenTool~/Program.Legacy.cs:158` writes and drift-checks the generated tape GLSL.
- VIXEN specialization: `VIXEN/libraries/SVO/include/Recipe/RecipeTileSpecialization.h:166` evaluates conservative Sphere/Box intervals over object-local AABBs and prunes only hard CSG branches proven irrelevant by strict interval separation. Unsupported opcodes, invalid domains, and invalid operands return the original tape.
- VIXEN wiring and tests: `VIXEN/codegen/CMakeLists.txt:238` adds the generated tape evaluator to the recipe codegen checks. `VIXEN/libraries/SVO/tests/test_recipe_interval_pruning.cpp:52` covers CPU interval proofs and fallback. `test_recipe_declared_position_render.cpp:859` exercises full, pruned, and unrolled tapes on the GPU.
- The library API is exercised by the focused GPU fixture. Runtime scene-host integration remains a follow-on.

## Witness numbers

The fixed edit-heavy recipe has 325 instructions and uses 64 object-local tiles. Each tile’s disposable tape retains one Sphere instruction. The GPU device was an NVIDIA GeForce RTX 3060 Laptop GPU through the WSL DZN driver.

| Measure | Full tape | Pruned tape | Result |
|---|---:|---:|---:|
| Output bytes | — | — | 0 differing bytes versus both pruned and unrolled paths; all depth/material/channel values match |
| Hit pixels | 1,804 / 4,096 | 1,804 / 4,096 | Identical, nonempty image |
| Executed clauses per image pixel | 26,499.9 | 81.5381 | 99.6923% reduction |
| GPU median time | 14.9012 ms | 0.108544 ms | Unrolled path: 0.340992 ms |
| Tape upload | 43,412 B | 8,960 B | Includes tile ranges |
| CPU tile specialization | — | 0.749964 ms | 20,800 interval evaluations; 20,736 source instructions pruned |
| Shader compile time | 13.2093 ms unrolled | 151.813 ms tape | One-time compile measurements |
| Shader source size | 39,275 B unrolled | 41,327 B tape | — |

The GPU test checks exact RGBA32F bytes for full/pruned/unrolled output, exact material/channel constants, hit count, and clause reduction. The 0.858508 ms serial figure is CPU specialization plus pruned GPU execution; shader compilation is reported separately.

## Build and test record

- Kernel build after merge: both explicit test projects built with 0 warnings and 0 errors.
- Kernel SourceGenerator suite: 973 passed.
- Kernel CodegenTool suite: 2,081 passed, 2 skipped.
- CodegenTool generated all recipe artifacts; the follow-up `--check` run passed.
- VIXEN configure passed with `YEROKET_ROOT=/home/liory/projects/Yeroket-Fantasy` and kernel pin `f1ac3fb0f8aa71f98075fb33e35d035999c4f334`.
- Full queued VIXEN build passed. Its 22 generated-code drift checks, including the recipe SIMD/CPU/GLSL/tape evaluator check, passed. An explicit second target invocation returned `ninja: no work to do`.
- Focused SVO CPU/GPU tests: 5 passed, 0 failed. The GPU witness passed in 4.00 s.
- Baseline and post-change CelShading capture tests: 4 passed each.
- Captures: 11/11 byte-identical and pixel-identical (7 HUD/editor and 4 CelShading).
- No-op VIXEN rebuild: `ninja: no work to do`.

## Recovery record

- `dotnet build Yeroket-Fantasy.sln -c Release -nodeReuse:false` returned exit 0 with `Unable to find a project to restore!` and compiled nothing. Recovery: build and test `SourceGenerator~/Tests/SDFNodeGenerator.Tests.csproj` and `CodegenTool~/Tests/CodegenTool.Tests.csproj` directly through the queue. Proposal recorded below.
- The first baseline capture run passed a relative output directory. Both apps rendered, then PNG writes failed after the capture script changed working directory. Recovery: rerun with an absolute output directory. Proposal recorded below.
- The first CodegenTool generation command used `.../.claude-worktrees/rva1/codegen/RecipeOpDeclarations.cs`; that file is at `.../VIXEN/codegen/RecipeOpDeclarations.cs`. The corrected queued `dotnet run` and `--check` passed.
- The first VIXEN configure was launched from the checkout root, where `CMakePresets.json` is absent. Recovery: run `cmake --preset vixen-wsl` from `VIXEN/` with the same FetchContent cache and kernel SHA override.
- The first VIXEN build found a float/double type mismatch in an interval error-scale expression. The domain values were explicitly promoted to double; the recovered full build and focused tests passed.

## CodeGraph and integration scope

VIXEN, the kernel worktree, and KernelFederationRenderer have no `.codegraph/` index; no index was created. VIXEN navigation used `rg`. KFR’s `app/src/session_renderer.cpp` contains no SDF recipe consumer. `SessionRenderer::BuildRenderGraph()` currently builds the UI graph; this is the closest KFR host integration point for a future scene node.

The mixed voxel/virtual delta stack is outside this recipe-only proof. The specializer has no ordered materialized-brick references, per-brick occupancy or min/max distance bounds, or revision linkage from that stack, so it cannot prove that a brick override or virtual clear leaves a recipe branch irrelevant. The recipe-only interval proof is implemented; rv-a2 owns the mixed-stack gap.

## Shared files touched

No cross-lane shared source files were changed. Changes are limited to the authorized kernel worktree and VIXEN SVO/codegen/test files.

## STOPs

At the 08:00 UTC three-hour checkpoint, the full RenderGraph CTest group, full SVO CTest group, and full VIXEN CTest suite were not run. The lane stops with the full build and feature-specific witness green. Current full-suite status on merged wave commit `8ee92e5c` is unclassified. The earlier run’s `T-1449` opcode-94 result was recorded on a different base and was not reverified here.

## SPT DISPOSITION

- `T-1449`: prior report records the opcode-94 recipe parity failure; this run did not rerun the full suite or establish its status on the merged wave.
- `T-1450`: keep the R328 interval-arithmetic follow-on open. This lane did not implement or close it.
- No existing SPT task was closed.

## CONSOLIDATION ISSUES

- proposed: CodegenTool tests should discover the Undertow root
- proposed: Standard VIXEN build should regenerate merged SDI before drift checks
- proposed: Kernel solution build exits successfully without discovering projects
- proposed: Capture script must resolve output directory before changing working directory
