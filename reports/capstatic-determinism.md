# VIXEN GPU determinism inventory (R485)

## Scope and method

This inventory follows the current production render graph in `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` and the shader/node consumers it wires. Test fixtures, archived graphs, and dormant legacy shader files are excluded unless noted. The key distinction is whether a GPU result can change semantic application state; temporal render buffers and exposure/probe histories remain visual state.

## Summary

- **State-affecting output:** one GPU output, the per-pixel pick ID. It updates the editor's in-memory `SelectionSet` after a click. I found no GPU result routed to simulation state, saves, network messages, or replays.
- **Deterministic-by-default dependency chain:** five pass families: `BodyInstanceRayMarch`, `RecipeInstanceBucketing`, optional B1 `HiZDownsample` and `InstanceOcclusionCull`, and optional B2 `ProxyIntervalPrepass`. B2 has compute and raster writers (`.comp`, or `.vert` + `.frag`).
- **Visual-only candidates:** sixteen programmable pass families listed below, plus the regular sky-sphere, blit/presentation, and UI-composition work. No shader relaxation is declared here; R485 requested an inventory only.

## Deterministic-by-default path

`BodyInstanceRayMarch.comp:46,534-541` writes the `rg32ui` pick image. `BuildRenderGraph.cpp:8254-8295` connects that image to the voxel-selection provider. `VoxelSelectionProviderNode.cpp:137-176` turns a clicked pixel into a `SelectionCandidate`; `SelectionCoordinatorNode.cpp:118-143` applies the selected candidate to its owned `SelectionSet` and publishes a selection-change event. This is editor runtime state, not persisted world state.

The pick output is not an isolated shader. Its traversal work can be gated by these upstream passes:

| Pass family | How it reaches the pick result | Registration / source |
|---|---|---|
| `RecipeInstanceBucketing` | Builds compacted per-recipe instance lists and screen-space coverage used by the ray-march dispatch. Atomic append order can vary with invocation scheduling; equal-distance candidates therefore need the existing explicit tie-break to remain stable. | `BuildRenderGraph.cpp:2569`; `RecipeInstanceBucketing.comp:312-340,389-392` |
| B1 `HiZDownsample` | Reduces the previous depth image to tile maxima for the occlusion decision. Its integer tile mapping adds no floating-point transcendental operation; it carries the prior frame's depth values. | `BuildRenderGraph.cpp:2663`; `HiZDownsample.comp:19-49` |
| B1 `InstanceOcclusionCull` | Writes the instance skip mask consumed by the ray-march path. A boundary difference can change which instance is traced. | `BuildRenderGraph.cpp:2664`; `InstanceOcclusionCull.comp:119-154` |
| B2 `ProxyIntervalPrepass` | Produces per-pixel proxy intervals and candidate masks read by `BodyInstanceRayMarch.comp` when B2 is enabled. The compute and raster writers are intended to be parity-equivalent. | `BuildRenderGraph.cpp:2679-2730`; `ProxyIntervalPrepass.comp:61-104`; `.vert:65-95`; `.frag:25-68` |

There is also an ordering risk to retain in the deterministic inventory: the optional recipe-bucket path allocates instance slots with `atomicAdd` (`RecipeInstanceBucketing.comp:312-315,339-340`), while the ray-march shader's multi-writer hit-record merge is a plain read/compare/store (`BodyInstanceRayMarch.comp:453-468`) and the ID image is written separately (`:537-541`). The source documents a tiered multi-writer mode, but this audit did not prove a cross-dispatch ordering guarantee for every mode. Keep this output deterministic-by-default and verify writer ordering/tie behavior before any future relaxation or claim of cross-device replay equivalence.

The older dense `VoxelRayMarch.comp` also contains a pick-ID write (`:573`), but it is not the production graph's active ray-march shader; the live graph registers `BodyInstanceRayMarch.comp` at `BuildRenderGraph.cpp:1860`. It is not counted as a second production state path.

## Cross-implementation floating-point and ordering risks

Vulkan does not promise bit-identical results for the following operations across implementations. The locations below are in the dependency chain above, including helper code reached by the active shader. Some locations are conditional on a recipe or optional build path. The list describes potential operations; it does not claim every listed expression executes for every frame.

| Source | Non-bit-exact operation sites relevant to the pick chain |
|---|---|
| `VIXEN/shaders/CameraRay.glsl` | `:19` `radians`/`tan`; `:23-27` chained multiply/add and `normalize` (square-root/reciprocal-square-root behavior). |
| `VIXEN/shaders/BodyInstanceRayMarch.comp` | `:238` floating-point UV division; `:338-340` matrix/affine multiply-adds in the hit-bounds check; `:541` is the integer ID write after floating-point tracing. |
| `VIXEN/shaders/TraceWorld.glsl` | `:185` optional ray-query path normalizes the query ray; `:254,261-266` normalize/length, divide, and reciprocal while mapping the ray to instance space; `:332-343` dot products, multiply/subtract and `sqrt` in the bound-sphere intersection; `:374` normalize for the coarse hit; `:621-622` length and multiply in world-distance reconstruction. |
| `VIXEN/shaders/ESVOCoefficients.glsl` | `:61` `exp2`; `:72-77` reciprocal/division and multiply in traversal coefficients. |
| `VIXEN/shaders/ESVOTraversal.glsl` | `:330-332,365,445-447` multiply/add/subtract expressions form traversal `t` values and branch thresholds; multiply-add contraction can move a ray across a child/voxel boundary. `:488-490,524-528` floating position/step updates feed subsequent branch decisions. |
| `VIXEN/shaders/CoordinateTransforms.glsl` | `:42,48,111` floating-point division followed by multiply/add in grid/world and brick-space conversions. |
| `VIXEN/shaders/RayGeneration.glsl` | `:70-85` slab intersection divides by direction components and reconstructs the interval with multiply/add. |
| `VIXEN/shaders/SceneBindings.glsl` | `:1111-1126` affine dot products, translation/scale multiply-add, and division in world/local point/vector transforms; `:1139-1148` dot/normalize/length for transformed normals and minimum-axis scale. These transforms feed `TraceWorld`'s ray and instance bounds. |
| `VIXEN/shaders/StoredSdf.glsl` | `:777-779,960-962,1048-1051,1102-1105` reciprocal direction components used by slab intersections. These feed stored-SDF traversal; gradient normalization at `:451-453` and packed-normal decode at `:120-126,150` are also present in the shared hit path, though they primarily affect shading. |
| `VIXEN/shaders/SdfRecipes.glsl` | `:15` `sin`; `:20,33` length/normalize; `:47-52,158-170` dot/multiply/subtract and `sqrt`; `:60` division and the decimal approximation `1.7320508`; `:211-215,242-250,295-299` multiply/add, division, `sqrt`, and normalize in the generated recipe marcher/refinement path. |
| `VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl` | `:4-5,222,282,571-572` `sin`/`cos`; `:12-68,93,190-206,290,333,466-491,534-553` `length`/`sqrt` (including reciprocal square root at `:93`); `:34-37,55-57,65-68,226,270,329,333,461,476,490-530,546` division/reciprocal; `:230,242,266` `exp`/`log`/`pow`; `:24,36-37,55-57,68,270,329-333,499-530` multiply/add chains that may contract. The smooth-union expressions at `:505,510,520,530` also fold the non-exact `1.0/6.0` constant. `precise` return temporaries do not make transcendental results bit-identical between GPU implementations. |
| `VIXEN/shaders/RayQueryTraversal.glsl` (when `VIXEN_RTQUERY_TRAVERSAL` is enabled) | `:165-178,186-199,264,391` proxy-center arithmetic, `floor`, length/normalization, reciprocal direction components, slab products, and grid-to-world division; `:362,408` submits computed hit distances to Vulkan ray query. The candidate traversal/intersection path is also implementation-dependent across ray-tracing drivers. |
| `VIXEN/shaders/RecipeInstanceBucketing.comp` | `:210-217` matrix projection and multiply/add; `:212` clip-space division; `:305-306,333-334` `length` and multiply/add thresholds; `:384-387` floor/ceil before integer coverage bounds. Atomic append order at `:312-315,339-340` is a separate scheduling-dependent ordering risk. |
| `VIXEN/shaders/InstanceOcclusionCull.comp` | `:121-122` `length`; `:132-134` matrix projection and clip-space division; `:151-154` floating-point division and `floor` for tile bounds. |
| `VIXEN/shaders/ProxyIntervalPrepass.comp` | `:66-67` division; `:79` `radians`/`tan`; `:82-84` normalize and chained multiply/add; `:104` UV division. The raster twin has the same operations at `.frag:30-31,43,46-48,62`; `.vert:92-95` transforms corners with matrix/dot multiply-adds. |

The Vulkan note in `/home/liory/codeman-cases/undertow/reports/vulkanresearch.md` §6 is the basis for treating these as cross-vendor risks: `precise`/contraction controls do not establish a complete bit-exact contract for built-ins, constant folding, and every compiler/driver path. Integer packing of the final ID is exact; the hit location and winning instance that feed that packing are not guaranteed bit-identical across vendors.

## Visual-only candidates for an explicit future relaxation

These active or optional production shader families write rendered color, temporal reservoirs, probe/exposure histories, or diagnostic/lighting accumulators. Their outputs can affect later frames of rendering but I found no path from them to semantic selection, simulation, save, network, or replay state.

1. `DirectLighting.comp`
2. `SpatialReuseShade.comp`
3. `ExposureTonemap.comp`
4. `ExposureMeter.comp`
5. `ProbeGather.comp`
6. `ShadowRayTrace.comp`
7. `ProbeApply.comp`
8. `ShadowVisibilityWave.comp`
9. `HitAccumClear.comp`
10. `HitAccumulate.comp`
11. `HitAccumCellShade.comp`
12. `SpatialReuseGather.comp`
13. `PhotonDeposit.comp` (optional photon-cell mode)
14. `PhotonCellFold.comp` (optional photon-cell mode)
15. `PhotonCellClear.comp` (optional photon-cell mode)
16. `SkyProjection.vert` + `SkyProjection.frag`

The regular sky-sphere, presentation blit, and UI composite are also visual-only GPU work, but are not included in the sixteen programmable families above. Diagnostic readbacks (ray counts, timing queries, and debug buffers) are observability outputs, not inputs to the semantic state identified in this inventory.
