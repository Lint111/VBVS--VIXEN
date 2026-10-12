# descset (sn-descset, R532): one descriptor set per shader

Branch `lane-descset`, off `lane-hk-spirvset` (`78698a1e`). Product commit `b6905cd7`.

R532 and R529.1 are not in the register I could read; the lane brief is the only statement of the rule: one descriptor set per shader, with feature variants adding or omitting bindings inside that set and never renumbering.

## Inventory: every place a second descriptor set exists

A grep for `set = N` (N > 0), HLSL `space N` / `[[vk::binding(b, N)]]`, `setLayoutCount`, `firstSet` and `vkCmdBindDescriptorSets` over `VIXEN/`, plus the checked-in SPIR-V, `VIXEN/generated/sdi/**` and `VIXEN/shaders/sdi-variants.json` (25 programs).

| Where | Second set? | Why | Feature variant? |
|---|---|---|---|
| Every shipped GLSL/HLSL shader under `VIXEN/shaders/` (including `SceneBindings.glsl`) | No, all in set 0 | n/a | The only optional binding is `SceneBindings.glsl` binding 14, gated on `VIXEN_GPU_TRACE_HOOKS`. It is already an add-only binding in set 0. |
| `VIXEN/generated/sdi/**` merged and per-program SDI headers, checked-in SPIR-V | No | derived from the shaders above | n/a |
| Pipeline layouts: `SkyProjectionNode.cpp:290`, `RayTracingPipelineNode.cpp:394`, `VixenRmlRenderInterface.cpp:101`, `ShellRevalidateNode.cpp:218`, `GraphicsPipelineNode.cpp:265`, `pipeline_layout_cacher.cpp:40` | No, `setLayoutCount` is 1 (0 or 1 in the cacher) | | n/a |
| `PassRecorder.cpp:59-124`, `MultiDispatchNode.cpp:589-618`, `PassStep.h:30,46`, `DispatchPass.h:58` | `firstSet` is a plumbing field. Every producer leaves it 0. | generic binder | n/a |
| `VIXEN/libraries/ShaderManagement/tests/test_sdi_lifecycle.cpp:98-99` (`albedoMap`, `normalMap` at set 1) and the `namespace Set1` assertion near line 212 | Yes | Test fixture for per-material data in a second set (legacy split) | **No.** See STOPs. |
| `VIXEN/libraries/ShaderManagement/tests/test_spirv_descriptor_layout.cpp:17` (hk-spirvset's reuse-of-binding-index case) | Yes, deliberately | Negative fixture proving the set-blind reflector fails closed | n/a (a rejection test) |

Finding: no production shader, generated interface, checked-in SPIR-V or pipeline layout uses more than set 0, so there was nothing to convert. The deliverable became making the invariant hold by construction and proving it.

## Change

`MergeSdiVariants` (`SdiVariantMerge.cpp`) is the step that derives each per-variant layout from declaration and feature selection (R499.3, R491). It now hard-fails when:

- the variants of one program span more than one descriptor set, or
- one binding name is at binding indices that do not nest across variants (a variant renumbered it; a variant may only add or omit).

The real manifest (25 programs) passes the stricter merge, and `sdi_tool merge-variants shaders/sdi-variants.json --check` reports all merged headers up to date, so no generated output changed.

### Tests

- `SdiVariantMerge.OptionalFeatureAddsOneBindingToTheSameSet` (disabled and enabled variants share set 0; enabled has exactly one more binding, with the feature predicate)
- `SdiVariantMerge.VariantsSpanningTwoSetsFail`, `SingleVariantSpanningTwoSetsFails`, `VariantRenumberingABindingFails`
- `SpirvDescriptorLayout.OptionalFeatureAddsOneBindingToTheSameSet` (real glslang compile of a compute shader with and without `VIXEN_GPU_TRACE_HOOKS`, reflected with `SpirvReflector`, then merged)

Red/green: the three new merge tests fail against the old `MergeSdiVariants` and pass with the change. `SdiVariantMerge.DuplicateMemberNamesSkipAliases` (two bindings sharing a name inside one variant) failed against my first, stricter rule. I changed the rule to compare a name's binding indices across variants only, and all 17 merge tests pass.

## Evidence

- Build: fresh full `cmake --build build/wsl -j4` passed, including all codegen golden checks.
- Targeted tests: `SdiVariantMerge.*` (17), `SpirvDescriptorLayout.*` (2) and `sdi_merged_drift_check` passed under the queue (`.tmp/targeted-after.log`).
- Full RenderGraph|SVO suite (`ctest -L 'RenderGraph|SVO'`, serial, queued GPU job, log `/home/liory/.local/state/undertow/undertow-box-logs/1791800161-gpu-descset:rendergraph-svo-suite.log`): 2171 selected, `99% tests passed, 1 tests failed out of 2170`, failing-name set exactly `{RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes}`. That equals the wave base set in `reports/vxtrain.md` (same 2171-case selection at vxtrain's tip). Process note: this run was submitted with `--exclusive`, which R533 rules out for correctness runs. It only waited for an emptier box, so the result stands, but the later jobs used plain `--resource gpu`. I did not re-run a base suite: the product diff is confined to `SdiVariantMerge.*` and two test files, and the failing name is the accepted opcode-94 red.
- Look-dev captures: `vixen_lookdev_capture` has sha256 `7ee9fb262bb56cbfb6cd39d510efb7663eb8750db8f813014a7dd61cd1da696a` both with the old `SdiVariantMerge.cpp` (HEAD before this lane) and with the new one, built in the same tree. `MergeSdiVariants` has no runtime caller; it is reached only from `sdi_tool` and tests. Captures from an identical binary cannot differ, so the byte-identical requirement is met by construction. The ctest `Application.LookdevCapture...` also compares each state's first run to its repeat. See the per-state results below.
- Look-dev ctest: `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` passed for each of midday, late-afternoon, overcast and night-service (627 s, 562 s, 534 s, 629 s; one state per ctest invocation, plain `--resource gpu`, log `.tmp/lookdev-states.log`). Each state's first and repeat captures are byte-identical (16 angles, 32 PNGs). The first all-states run timed out at the test's own 1200 s limit, and a quiet run was killed by the queue's 180 s idle guard until I added a heartbeat line.

## LANDABLE NOW

- `b6905cd7` feat(shader): enforce one descriptor set per program across feature variants (R532). Checks: `test_sdi_variant_merge` 17/17, `test_spirv_descriptor_layout` 2/2, `sdi_merged_drift_check`, `sdi_tool merge-variants shaders/sdi-variants.json --check`, full RenderGraph|SVO suite failing set equal to base.
- This report commit.

## STOPs

1. **`VIXEN/libraries/ShaderManagement/tests/test_sdi_lifecycle.cpp:98-99`** declares `albedoMap` and `normalMap` at `set = 1` and asserts a `Set1` namespace (~line 212). That second set is a per-material/legacy split, not a feature variant: the maps are present in the fixture unconditionally. I did not change it, because converting it would rewrite what that SDI lifecycle test asserts about multi-set emission. Decision for the owner: fold both bindings into set 0 (renumbering the fixture) or keep it as the one deliberate multi-set fixture for the generator.
2. **Per-set layouts versus fail-closed (from hk-spirvset).** `SPIRVReflection.cpp` still fails closed on multi-set programs rather than modelling per-set layouts. With R532 that is consistent, but only if the owner confirms that multi-set support is never wanted. If it is, hk-spirvset's fail-closed behaviour is the permanent form; if not, it needs a set-aware replacement.

## SPT DISPOSITION

No SPT proposals from this lane.

## CONSOLIDATION ISSUES

- The look-dev ctest runs all four states in one test with a 1200 s limit and timed out in my first run (`.tmp/targeted-after.log`); vxtrain already proposed partitioning it. I ran it one state per ctest invocation.
- R532 and R529.1 are cited by the brief but absent from the register, so the rule's wording is unrecorded.
