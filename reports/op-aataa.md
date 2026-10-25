# op-aataa: anti-aliasing S3 (module contract, shared selection, `none`) and S4 part 1 (TAA kernel system)

Rulings: R676, R677, R674, R675 (R680 is not in the undertow register at `origin/main-wave` d65d7097d; the
render-bridge runner it stages came in through lane-rbridge). Design: `reports/aadesign/design.md` S3-S4.
Evidence: `/home/liory/codeman-cases/undertow/reports/aataa-evidence/`. Dozen only: correctness, no timing.

Tips: VIXEN `lane-aataa` (this worktree), kernel `lane-aataa` (`Yeroket-Fantasy/.claude-worktrees/aataa`).
Both branches merge lane-rbridge (VIXEN afcc4c32, kernel 503baffd8/fc9bd33a1): the TAA has to be a
generated kernel system run by the generic runner (R677), and the runner exists only there.

## What changed

### S3 (VIXEN 2edeccbf): AA contract, one selection predicate, `none`
- `graph/RenderModuleSelection.h`: the upscaler's ordered eligibility gates, failure reasons, candidate
  walk, init-failure bookkeeping and the frame-input / format / history-reset vocabulary, moved unchanged
  under module-neutral names (R676.5: one predicate). Every upscaler call site uses it; no copy remains.
  `UpscalerContract.h` keeps only what is upscaler-specific (extents, depth conventions, external resources).
- `graph/AntiAliasingModule.h`: the AA host in the upscaler's shape. Backend table with the R676.5 ids
  (`taa`, then `none` as the terminal pass-through; the post providers and `supersample` join with their
  slices), a provider contract (linear-HDR vs display-referred colour, required frame inputs, history),
  the pass gate (canonical sampling stays one raw sample, R676.2 / design M1; a history provider steps aside
  for a temporal upscaler, M3) and `BuildAntiAliasingCandidates` over the shared predicate.
- `test_aa_selection` (`Application.AntiAliasingSelection`, labels `Application;Unit`).

### S4 part 1 (kernel 7f03502b3, a9894e1c4; VIXEN 9afd08d1): the TAA as a generated kernel system
- `codegen/gpu-systems/TemporalAntiAliasingSystem.cs` (exemplar: `ExposureTonemapSystem.cs:18-31`, same query
  and texture-view forms). It resolves in linear HDR before the exposure meter (R676.1): the jittered current
  sample (the engine's Halton(2,3) cycle), a 3x3 neighbourhood colour box read through the ruled
  `Sample(uv, lod)` view (R493 texture surface: `Current[...]` stays own-id only), history reprojected through
  the engine motion vectors (camera and instance transforms, `ReconstructionMotion.glsl`) with a bilinear
  `Previous[]` fetch, a strict clamp to the box (R676.5: a stochastic night sample from an earlier frame is
  not averaged in), the 1/(1+luma) weighting (R676.1), and a 1/N ramp to the history-weight ceiling counted
  in the resolve's alpha (a still camera converges like the 1/N accumulation the design measured).
  The kernel generates its GLSL (`shaders/Generated/TemporalAntiAliasingSystem.comp`), manifest entry and
  schedule. History stays GPU-side (R674).
- `FrameTargets` gains `MotionVectors` (`Rg32f`) and `ResolvedRadiance`; the tone map reads `ResolvedRadiance`.
- `PrevCameraConfig` carries the input extent and the history-weight ceiling beside the jitter it already
  carried. No transfer of their own (R674.2): 80 -> 96 bytes on an existing per-frame upload.
- Runner: `GeneratedSystemStages::Instantiate` takes an `enabled` filter. The AA backend table names each
  provider's generated system and the host leaves out the systems of providers it did not select. AA
  selection runs before instantiation (the M3 gate reads the requested upscaler).
- With `none`, the host registers `ResolvedRadiance` as the scene radiance image itself.
- Kernel: `Rg32f` joins the closed texture-format vocabulary (7f03502b3). A scheduler fix (a9894e1c4): a
  named kind's whole-row access (`in LightingConfig[]`, field `*`) set aside EVERY conflicting pair as
  wildcard-tainted, so two GPU systems meeting on one texture could never be ordered (SCHED004/SCHED001 on
  `ResolvedRadiance`, MEASURED). It now taints only conflicts on its own kind; a global residual still
  taints everything. Two new tests pin both sides.
- Tracked kernel pin -> a9894e1c4. `Program.Legacy.cs`: 0 lines changed.

## Evidence

| Check | Before | After | Label |
|---|---|---|---|
| midday 500^2 6 frames, sha256 | base b9b47023 `769bfa2e…` | S3 `769bfa2e…`, S4a `769bfa2e…` | MEASURED |
| night-service 500^2 6 frames, sha256 | base `87e304dc…` | S3 `87e304dc…`, S4a `87e304dc…` | MEASURED |
| `Application.AntiAliasingSelection` | n/a | 8/8 | MEASURED |
| `Application.UpscalerSelection` | 7/7 | 7/7 (renamed types) | MEASURED |
| kernel CodegenTool suite | floor 2618/17/0 (rbridge) | 2621 passed / 18 skipped / 0 failed | MEASURED |
| kernel SourceGenerator suite | 1208/29/0 | 1208/29/0 | MEASURED |
| `gpu_systems_check`, `sdi_merged_drift_gate` | — | regenerated, build green | MEASURED |
| `Application.ExposureTonemapParity.*` (raw parity) | — | 32/32 | MEASURED |
| RenderGraph label (`-E RendersGlyphSdfLocalizedSign`) | base: the same 15 names fail | lane 1430/1445; the 15 failures are exactly base's | MEASURED |

RenderGraph failing names, identical on base b9b47023 and lane 9afd08d1 (base rerun of these 15 names:
`rendergraph-base-failnames.log`): `EditorDocumentRenderTest.{GoldenDocumentAllLayersRendersVisibleBody,
DisablingCutLayerChangesTopFaceSilhouette, LayeredSlabSubtractSphereGuardsAreExactAndCaptureHasHole}`,
`AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`, `ProceduralRecipeRenderTest.{RenderProceduralRecipe,
RenderMirrorCsgRecipe, RenderSubtractBoxSphere, RenderTorus, RenderCone, RenderPyramid, RenderRevolution,
RenderTwist, RenderDisplacement, RenderComposition}` (an HLSL compile error in the test's recipe shader:
"shader(6): error at column 57"), `vixen_text_shader_capture_producer` (timeout, the aahue lane's known
harness-budget case). Pre-existing; not this lane's.

The captured log shows `Anti-aliasing provider 'taa' rejected: not compiled` and no
`TemporalAntiAliasingSystem` stage instantiated with `none`.

## Not done (S4 part 2) and why

`taa` stays uncompiled in the backend table. The kernel half is complete; the host half is missing generic
runner support:
1. **Previous-slot binding:** the registry keys `{kind, member}` only and ignores the manifest's `slot`, so
   `ResolvedRadiance` current and previous cannot bind different images.
2. **Sampled view + sampler:** the runner does not bind the manifest's `sampled` form and `samplerBinding`;
   no generic sampler node exists, and `DescriptorSetNode::HandleSampledImage` hard-codes
   `SHADER_READ_ONLY_OPTIMAL` while these images stay `GENERAL` (the manifest says `GENERAL`).
3. **Derived allocation:** the runner allocates nothing from the manifest's `allocations`; the TAA history
   pair (slots 2) needs it (or an `AccumulationHistoryNode` pair with `SAMPLED` usage for the scene radiance).
4. **Host enable:** with `taa` selected, turn on jitter and the motion module, provide `MotionVectors`, and
   point the hand exposure meter at `ResolvedRadiance` (or convert the meter, CONSOLIDATION).
5. **Witness:** hard-step share and edge error vs the 16-spp reference after 8 frames at 500 and 1000, the
   pan ghosting check, the night guard, parity 32/32 raw, and the canopy-overlap zoom crops for
   `reports/aataa/`. None of these ran: there is no `taa` path to capture yet.

## SPT DISPOSITION
No SPT task id was named in the brief. Proposals filed: `.spt-proposals/op-aataa.jsonl` (6 entries; the
6th corrects the 5th's body, which lost two words to shell quoting).

## STOPs
1. **R675 settings.** The AA provider request and the TAA history weight live in
   `VulkanGraphApplication::RenderPathConfig` beside the upscaler request. R675's per-module settings record
   (`<Name>Settings.cs` + canonical per-variant JSON read by generated code) does not exist on the wave, and
   declaring a settings kind nothing reads would be a second declaration. Recommendation: move both to
   `AntiAliasingSettings` when op-modoptions lands R675 S1.
2. **Kernel scheduler change.** a9894e1c4 changes when a whole-row access counts as a wildcard (core schedule
   model). It is a narrow bug fix with tests and a clean full suite. It is not a new form, but a controller
   should confirm it because it touches the schedule derivation.

## CONSOLIDATION ISSUES
- Runner gaps 1-3 above (filed, tier:luna).
- `HandleSampledImage` layout constant (filed, tier:haiku, red-state).
- `PrevCameraConfig` is now the reconstruction record; its `reconstructionSampling.z` is dead (filed, tier:haiku).
- The exposure meter is a hand pass bound by provider name; it must read `ResolvedRadiance` once `taa` runs
  (filed, tier:luna; overlaps op-rbridge consolidation 3).
- The syntax-only `math` intrinsic class lives in `ExposureTonemapSystem.cs` and the GLSL map has no
  `floor` (the TAA uses `x - frac(x)`); a shared intrinsics file would serve every GPU system (not filed: tiny).
- Canonical-shape checklist on added lines, both repos: no hits (no EntityId, `KindRef<All>`, `Catalog<`,
  ordering hints, minted attributes, `Id` members).
