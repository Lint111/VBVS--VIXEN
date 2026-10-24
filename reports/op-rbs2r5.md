# op-rbs2r5: render bridge S2 round 5 (Claude, continuing cx-rbs2r5)

Evidence: `/home/liory/codeman-cases/undertow/reports/op-rbs2r5-evidence/`.

## What changed
- The host settings write produces the six settings records and uploads them through
  `TransferPlan::Fill`, called just before `RenderFrame`
  (`application/main/source/VulkanGraphApplication.cpp:475`, `:839`). The records are resolved in
  `RefreshResolvedLookSettings` (`application/main/source/graph/BuildRenderGraph.cpp:578`). Each
  record has a generic `StorageBufferNode` ring sized from the manifest (`BuildRenderGraph.cpp:914`).
- Deleted: the five hand upload nodes (per-frame `memcpy` at e2c41b6c: `LightingConfigNode.cpp:231`,
  `ShadowConfigNode.cpp:122`, `AccumulationConfigNode.cpp:160`, `ReservoirConfigNode.cpp:150`,
  `ProbeGridConfigNode.cpp:213`), their headers and node configs, their graph nodes
  (`BuildRenderGraph.cpp:695/721/856/885/911` at e2c41b6c) and the legacy `*Config` schema structs.
- `SceneLookSettings.frameSeed` is removed. The tonemap dither seeds from the monotonic `_Frame`.
- Fixed two regressions introduced by earlier rounds of this lane: the grayscale lookdev and Cornell
  seam red, and the planet fixed-exposure readback of 0.0.
- `EditorApplication` overrides `PreTick`, so the fill had to move from `PreTick` to `Render`.
- `ShadowCorrectnessTest` now encodes its settings through `TransferPlan::Fill`.

## Witness (MEASURED)
- Ledger: the old code wrote 396 B every frame. Now, per process: frames 0-3 write
  376 B payload (612 B including headers), once per ring slot. Steady state is 0 B. One
  shadow change writes 16 B (32 B with header) of `state:shadow-settings` only, in each of 4
  slots, then 0 B.
- Images compared with the pre-lane base 96196995:
  - Cornell 10/10 PNGs are byte-identical.
  - Lookdev 4/4 PNGs and planet C1 differ only by the dither reseed: max 8 LSB, mean about 0.
    With the dither zeroed in both trees they are byte-identical 5/5.
  - Planet hits/meter are identical.
  - Round 4 alone (cc201709 vs e2c41b6c) changed no image.
- ExposureTonemapParity: 32/32 passed.
- RenderGraph label: 15 failures at e2c41b6c and the same 15 names after.
- Kernel, paired: SG 1208/29/0 and CG 2629/17/0.

## SPT DISPOSITION
No SPT task ids were assigned to this lane brief.

## CONSOLIDATION ISSUES
See `.spt-proposals/op-rbs2r5.jsonl` (4 proposals).
