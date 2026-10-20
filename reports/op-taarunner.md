# op-taarunner: the generated-system runner gets history slots, sampled views + samplers and manifest allocation; `taa` is selectable

Rulings: R676 (AA module; TAA in linear HDR before exposure; canonical stays raw), R677, R680, R674.
Base: VIXEN `lane-aataa` ea3edf08 (code = 9afd08d1), kernel `lane-aataa` a9894e1c4 (unchanged by this lane).
Evidence: `/home/liory/codeman-cases/undertow/reports/taarunner-evidence/`. Witness images:
`/home/liory/codeman-cases/undertow/reports/taarunner/`. All GPU runs are Dozen: correctness only, timings
non-representative.

## LANDABLE NOW
- VIXEN `lane-taarunner`: a3133354 (runner), 2222701c (`taa` selectable), 276eace7 (capture harness), then this
  report commit. Kernel: no change (`lane-taarunner` = a9894e1c4). Undertow: no change.
- Checks that passed on the final tree: canonical midday and night 500x500 sha256 byte-identical to base;
  `Application.ExposureTonemapParity` 32/32; RenderGraph label: the same 15 failing names as base; runner, AA
  and upscaler unit tests; kernel SG and CG (stored attestations, kernel unchanged).

## What changed

### 1. Runner (a3133354): `VIXEN/libraries/RenderGraph/include/Nodes/GeneratedSystemStage.h`
- **History binding.** The manifest's `slot` is parsed into `FrameSlot` (`:58`). The registry keys
  `{identity, slot}`. `ApplyDescriptor(..., FrameSlot)` (`:324`) names the identity and "previous-frame" when
  nothing is registered. A previous-frame image is read-only: `ProvideImage(..., Previous)` adds no write
  hazard. The producer ordering skips previous-slot inputs.
- **Generation-safe ping-pong.** A two-slot allocation is an `AccumulationHistoryNode` pair. Current goes to
  CURRENT_HISTORY_*, previous to HISTORY_*, and the roles flip once per graph execution. The node now:
  - clears both images to zero whenever it creates them (first use, resize, format or usage change;
    `AccumulationHistoryNode.cpp:83-90, :275`). The TAA's valid-history count is then 0 and history is
    rejected. It never reads another generation's memory.
  - takes its format from `PARAM_FORMAT`.
  - derives its usage from its consumers.
- **Sampled image + sampler.** A `sampled` argument binds the view through `GeneratedSystemBinding` (`:62`), so
  the descriptor type is known when the connection is made. The producer then derives SAMPLED usage before the
  gatherer compiles. A runner-owned `SamplerNode` binds at `samplerBinding` (`:634`; new
  `Nodes/SamplerNode.h` and `src/Nodes/SamplerNode.cpp`: nearest, clamp-to-edge, lod 0). A texture view and a
  sampled view of one image count as one read hazard (`GpuSystemImageReads`, `:398`).
- **Manifest allocation.** `allocations` are parsed with formats from the kernel's closed vocabulary (`:93`,
  `:101`). `GpuSystemAllocationsToMake` (`:410`) picks the identities an instantiated system binds that no hand
  pass registers; hand registrations win (R680.4). `Allocate` (`:491`) makes:
  - two slots: the history pair, sized from the extent provider;
  - one slot: a `RenderTargetNode` following the extent target.
  
  `Wire` calls `Allocate` if the host did not. `Allocation(identity)` exposes the node.
- **Ordering.** `ProvideProducer(identity, stage, ordersAfterGeneratedWriters)` (`:298`) and
  `GpuSystemOrderingSource` (`:430`). A hand chain that already waits on a generated writer orders that
  writer's generated readers. Otherwise the rule is unchanged. See CONSOLIDATION.
- `ProvideSubGraph` (`:274`) registers a sub-graph's view and target (the motion module).
- **`HandleSampledImage` fix.** It binds GENERAL (`DescriptorSetNode.cpp:636`); it hard-coded
  SHADER_READ_ONLY_OPTIMAL. No other shader binds a separate sampled image (rg over the shaders).
- **Linking.** `application/main/CMakeLists.txt:99` adds the runner header to the node-manifest scan.
  Without it, the app does not link `SamplerNode` ("Node type not registered", MEASURED).

### 2. `taa` selectable (2222701c)
- `AntiAliasingModule.h:70`: `taa` compiled.
- `BuildRenderGraph.cpp`:
  - AA selection now runs before the jitter decision (`:576-614`). A selected `taa` turns on jitter and the
    reconstruction motion module.
  - Temporal inputs are offered only where the temporal path exists: HDR, no B1/B2 proxy traversal, no trace
    capture, not the hand tone-map reference.
  - The frame registry is built before the exposure meter (`:9638`). With `taa`, the runner allocates
    ResolvedRadiance (two slots from the manifest; `:9673`). MotionVectors comes from the motion module and
    PrevCameraConfig from its node.
  - The exposure meter reads the resolve (`:9675`; R676.1). The exposure chain waits on the TAA stage
    (`:9719`), and the tone map waits on the chain.
  - With `none` the bindings are those of the base. MEASURED: the canonical captures are byte-identical.
- `VulkanGraphApplication.cpp:187`: `VIXEN_ANTI_ALIASING=none|taa` (same seam as `VIXEN_UPSCALER`).

### 3. Capture harness (276eace7, `test_lookdev_capture.cpp`)
- The capture app kept dropping the AA request (`:1049`).
- New `VIXEN_LOOKDEV_CAPTURE_PAN_STEP_DEGREES` (`:1299`): a steady per-frame pan for the ghosting check.

### Not done: `PrevCameraConfig` rename and its dead `.z`
Not started. It touches about 200 sites: the [GpuStruct] and its generated `.g.h` and `.glsl`, three merged SDI
headers, four shaders, the node and its config, tests, and the manifest kind the runner keys
(`{"PrevCameraConfig","*"}`). It needs a codegen regeneration and a GPU re-witness. At the 3-hour checkpoint
it stays op-aataa's proposal ("PrevCameraConfig is the reconstruction record ...", tier:haiku). Next step:
rename the [GpuStruct] to the reconstruction record, regenerate, and update the runner registration
`BuildRenderGraph.cpp` `{"PrevCameraConfig","*"}`.

## Witness

| Check | Before (base ea3edf08 = 9afd08d1) | After (lane) | Label |
|---|---|---|---|
| canonical midday 500^2 6f sha256 | `769bfa2e…` (op-aataa s4a) | `769bfa2e…` (final tree) | MEASURED |
| canonical night-service 500^2 6f sha256 | `87e304dc…` | `87e304dc…` (final tree) | MEASURED |
| canonical AA provider | `taa` rejected: not compiled | `taa` rejected: not permitted by pass (canonical sampling stays one raw sample) | MEASURED |
| `Application.ExposureTonemapParity` | 32/32 | 32/32 | MEASURED |
| RenderGraph label (`-E RendersGlyphSdfLocalizedSign`) | 1430/1445, 15 failing | 1438/1453, the same 15 names (diff empty) | MEASURED |
| `GeneratedSystemStage.*` | 3/3 | 11/11 (8 new) | MEASURED |
| `Application.AntiAliasingSelection` | 8/8 | 9/9 (1 new: compiled `taa` selected on presentation, `none` on canonical) | MEASURED |
| `Application.UpscalerSelection` | 7/7 | 7/7 | MEASURED |
| final `ctest -R` AA, upscaler, runner, RenderTargetUsage, AccumulationHistory | — | 19/19 | MEASURED |
| kernel SourceGenerator | 1208/29/0 | 1208 passed / 29 skipped / 0 failed (REUSED attestation `00cf721e…`) | MEASURED |
| kernel CodegenTool | 2621/18/0 | 2621 passed / 18 skipped / 0 failed (REUSED attestation `5d911491…`) | MEASURED |
| wizard verdicts, EntityId ratchet, `Program.Legacy.cs` | — | not touched (no kernel or undertow change); `Program.Legacy.cs` +0 | MEASURED |

RenderGraph failing names (both trees):
- `EditorDocumentRenderTest.{GoldenDocumentAllLayersRendersVisibleBody, DisablingCutLayerChangesTopFaceSilhouette, LayeredSlabSubtractSphereGuardsAreExactAndCaptureHasHole}`
- `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`
- ten `ProceduralRecipeRenderTest.*`
- `vixen_text_shader_capture_producer` (timeout)

Logs: `rendergraph-lane.log`; the base list is op-aataa's `rendergraph-lane.log`.

### Edge metric (still camera, midday angle 0, 16 frames)
The reference is a 2x box-downsampled canonical capture at twice the resolution (4 samples per pixel). Edge
pixels are those whose reference 3x3 luma range exceeds 24. A pixel is "hard-step" when its luma lies within
15% of either end of that range. `.tmp/edge.py`; output in `taarunner-evidence`. MEASURED:

| res | provider | edge MAE | edge RMS | hard-step share | whole-frame MAE |
|---|---|---|---|---|---|
| 500 | off (canonical) | 9.46 | 17.41 | 74.7% | 2.67 |
| 500 | taa | **6.41** | **10.28** | **57.3%** | 2.48 |
| 1000 | off (canonical) | 8.07 | 15.92 | 76.0% | 2.40 |
| 1000 | taa | **6.24** | **10.79** | **60.2%** | 2.33 |

The reference itself has 64.3% (500) and 67.0% (1000) hard-step share. The TAA lands just below the 4-sample
reference: it is slightly softer than a 4-sample box.

### Zooms (owner's regions; 4x nearest, off | taa), `reports/taarunner/`
- `overlap-500.png`, `overlap-1000.png`: the canopy-overlap edge. The box is located on the aacov 1000 frame by
  template match (MAE ~12 against `aacov/overlap-1000.png`) and scaled by resolution. The pixel staircase along
  the light/dark canopy overlap is smoothed.
- `outer-500.png`, `outer-1000.png`: the outer silhouette against the sky. This box matched `aacov/outer-1000.png`
  less exactly (MAE ~10) and sits slightly above it; it is the same canopy-against-sky edge.
- `still-500.png`, `still-1000.png`: full still frames, off | taa. Same exposure: mean 93.94 vs 93.96.
- **2000: not captured with `taa`.** On Dozen the jittered sampling path loses the device or segfaults at
  frame 0 at 2000x2000, with `taa` and with `none`. The base tree, where `taa` is not compiled, fails the same
  way (`base-jit-midday-2000.log`), so this predates the TAA (MEASURED). Its march costs ~6.1 s/frame at
  1000x1000 on Dozen (below). Canonical 2000x2000 captures complete (`canon/midday-2000`). Filed as a
  proposal.

### Moving camera (ghosting), `reports/taarunner/moving-*.png`
Each panel is off (canonical at the final pose) | taa still at that pose | taa panning 0.5 deg/frame for 16
frames, ending at the same pose.
- Pan vs still differ only along 1-pixel object edges (`moving-diff-500.png`, `moving-diff-1000.png`: |pan - still| x4).
- Pan vs still MAE: 2.37 at 500, 1.96 at 1000. Pixels differing by more than 32: 0.64% and 0.35%.
- No trails or double edges are visible. Edges under motion are softer than still (bilinear history
  reprojection). MEASURED.

### GPU cost per frame (Dozen, NON-REPRESENTATIVE)
Measured with `VIXEN_PERF_CSV`, 1000x1000 jittered, frames 5-16:

| pass | Dozen time |
|---|---|
| `TemporalAntiAliasingSystem` | median ≈0.15 ms (0.141-0.157; one frame 0.49) |
| `ExposureTonemapSystem` | ≈0.055 ms |
| `esvo_traverse_shade` (march) | 5.8-6.6 s |

Native timing was not run in this lane. INFERRED native cost: 15 texel accesses per pixel at 8 B, about
120 MB/frame at 1000x1000, an order of 0.1 ms on a desktop GPU.

## SPT DISPOSITION
No SPT task id was named in the brief. op-aataa proposals this lane addresses:
- **CLOSE** "Generated-system runner: previous-slot binding, sampled views and derived allocations": a3133354
  plus the witness above.
- **CLOSE** "DescriptorSetNode binds every SAMPLED_IMAGE as SHADER_READ_ONLY_OPTIMAL": `DescriptorSetNode.cpp:636`.
- **PARTIAL** "Exposure meter is a hand pass reading the scene radiance by provider name": the meter now reads
  the resolve. It is still a hand pass, and the ordering flag stands in for derived order.
- **OPEN** "PrevCameraConfig is the reconstruction record: rename it and drop the dead Z".
- **OPEN** "AA module settings sit in RenderPathConfig".

New proposals: `.spt-proposals/op-taarunner.jsonl` (5).

## STOPs
None blocking. Two choices for a controller to confirm:
1. **The `ordersAfterGeneratedWriters` flag on `ProvideProducer`.** It is a small hand declaration of
   transitive order. It is needed because a compute stage has one ordering-wait semaphore and the hand exposure
   chain sits between the TAA and the tone map. It deviates from derived ordering (R394). The removal is to
   convert meter and reduce into generated systems, so manifest barriers order the chain. Recommendation: keep
   it until that conversion; it is used once.
2. **`taa` is the default on interactive paths.** `RenderPathConfig::antiAliasing` already defaulted to "taa";
   compiling it makes the interactive app jitter and run the motion and history modules by default.
   `VIXEN_ANTI_ALIASING=none` opts out. Canonical, parity and picking captures are unaffected (pass gate).

## Overlaps with parallel lanes
- **cx-rbs2** (transfer plan/Fill/ledger in `GeneratedSystemStage.h`): this lane changed
  - the registry's private maps (`descriptors_` and `images_` keyed by `{identity, FrameSlot}`, `DescriptorFn`);
  - `GpuSystemManifest::Parse` (allocations, slot, sampler, descriptor type);
  - `Instantiate`/`Wire`.

  All additions keep the existing call signatures (defaults). Expect textual conflicts in `Parse` and `Wire`.
- **cx-vixroot**: touches no runner API this lane changed, as far as the brief describes; the shared file is
  `BuildRenderGraph.cpp`, where the AA block moved to the top of `BuildRenderGraph()` and the exposure section
  was reordered.
- Left on disk: detached base worktree `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/taarunner-base`
  (ea3edf08, with a build) used for the base 2000 run; remove when no longer needed.
- Continuation record: `/home/liory/codeman-cases/undertow/reports/taarunner-evidence/continuation.json`.

## CONSOLIDATION ISSUES
Filed in `.spt-proposals/op-taarunner.jsonl`:
1. **Jittered march on Dozen** (red-state, tier:sol): ~6 s/frame at 1000^2 and device loss at 2000^2. The base
   fails too. This blocks 2000 temporal witnesses on Dozen.
2. **Two history mechanisms** (conversion, tier:luna): with `taa`, the reconstruction-history seed still copies
   scene radiance into history A/B each frame. The TAA's history pair should be the viewport history, or the
   seed should step aside.
3. **Gatherer sampler check** (red-state, tier:haiku): the gatherer's compile-time check rejects a VkSampler on
   a SAMPLER binding (it expects ResourceType::Buffer). The runner binds its sampler with the Execute role to
   skip it.
4. **Raw-index bindings** (conversion, tier:haiku): the gatherer learns a raw-index binding's type only after
   producers have derived usage. The runner states the type for sampled views only; it should for every
   binding.
5. **Ordering flag** (conversion, tier:luna): see STOP 1. Remove it by converting the exposure chain to
   generated systems.

Canonical-shape checklist on the 814 added lines (VIXEN; no kernel or undertow change): no hits (EntityId,
`KindRef`, `Catalog<`, ordering attributes, minted attributes, `.Baked()`, pipes, `CodegenFieldConfig`).
