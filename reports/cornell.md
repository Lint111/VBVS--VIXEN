# Cornell fixture extraction report

**Status: STOPPED before source edits.** The active material-ID fallback is used by shared rendering and voxel baking. Removing its scene palette requires the material data-set contract called out by T-1365; the current audit says material identity does not yet flow through render view data to a response table. I did not choose or invent that contract.

Base SHA: `44d728e138a7a237cc6a6a97480c3eff2f404b1e` (`lane-cornell`).

## Summary

No shader, C++ source, fixture, or golden capture was changed. The required extraction cannot be completed safely until the owner decides how fixture material identity and response data reach both GPU rendering and CPU voxel baking.

The CodeGraph-first query was attempted (`codegraph explore "Cornell-specific palette material table tie-break rules shared shaders C++ fixtures"`), but this worktree has no `.codegraph/` index. Per CodeGraph's output, I did not initialize one.

## Sites reviewed

| Site | Evidence | Status |
|---|---|---|
| `VIXEN/shaders/Materials.glsl:18-45` | Defines ID-to-color palettes for Cornell, tunnel, and city materials. `VIXEN/shaders/SceneBindings.glsl:40` includes it; the dense voxel hit path calls `getMaterialColor` at `SceneBindings.glsl:1304`. The production `BodyInstanceRayMarch.comp` includes `SceneBindings.glsl` at line 122. | **STOP:** this is a shared rendering input. Externalizing the lookup requires a declared material data source and binding contract. |
| `VIXEN/libraries/SVO/src/SVORebuild.cpp:68-120,730` | `MaterialIdToColor` duplicates the palette and is used when voxel data has a material ID but no explicit color. | **STOP:** the CPU bake path also needs the same authored data or an agreed pre-baked color/channel representation. |
| `VIXEN/libraries/CashSystem/src/VoxelSceneCacher.cpp:441-470` | Constructs a Cornell material palette in scene-cache output. | Fixture-owned scene content is a candidate to move, but its relationship to the shared CPU/GPU material contract must be settled with the blocked sites. |
| `VIXEN/shaders/TraceWorld.glsl:27-52,213,329-332,602-609,743` | Shared traversal applies the relative epsilon and lower-instance-index tie rule; the file documents that it was added for abutting Cornell walls. | Shared production behavior. Moving the rule requires an explicit source and binding point for fixture-selected tie policy while preserving deterministic selection for other scenes. |
| `VIXEN/shaders/TraceWorld.glsl:398-401,757` and `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:6181-6197,6368-6374` | The procedural Cornell variant supplies its light emission through `recipeParams[3]`, which `TraceWorld` reads. The baked/stored variant also writes that slot, but its own comment says the stored-provider path does not read it; the stored hit instead uses the `SEM_EMISSION` channel. | Not changed. Resolve with the same material-data decision if this value is part of the requested scene-specific content. |

The audit's §4 lists three possible material seams (specialization constants with compact parameters, emitted recipe variants, or data-driven records). The owner must select the compatible material identity/data contract under T-1365 before this lane can bind fixture-owned values. No option was implemented here.

## Image comparison

Not run: no rendering source or fixture input changed, so there is no before/after image pair to compare and no pixel-diff result to report.

## Verification

- No CMake configure, build, or GPU test was run because implementation stopped at the required design gate.
- Focused witness suites remain unrun: `HeadlessUiGraph`, `RenderTargetNodeConfigTest`, `BodyInstance`, `EditorDocumentRender`, `HitRecordReadback`, `TierCrossing`, and Cornell-scene tests.
- This report is the only artifact and is validated with `git diff --check` before commit.

## STOP

**Decision needed:** define how fixture material identity and response data are supplied consistently to the GPU dense-voxel lookup and the CPU SVO rebuild fallback, and where the Cornell tie policy is selected for a dispatch.

Options for owner review:

1. Bind fixture-owned material records to the shared render/bake inputs, with a declared identity key and fallback behavior.
2. Have fixtures bake explicit color/material channels before the shared renderer consumes them, and define how legacy ID-only voxel inputs are handled.

Until that choice is made, deleting the active palette would change production rendering/baking, while replacing it with a guessed table or implicit default would violate the image-preservation requirement.

## CONSOLIDATION ISSUES

None. The CodeGraph index is absent in this worktree; no tooling workaround was used.
