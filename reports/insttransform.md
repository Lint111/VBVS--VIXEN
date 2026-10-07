# Instance transforms

R424 replaces the instance quaternion and scalar-scale path with one affine transform pair. `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:482` defines each matrix as three packed 16-byte rows, and `:495` groups `localToWorld` with its stored `worldToLocal` inverse. `BodyInstanceGpu` is the 144-byte host submission row; the renderer splits it before upload.

`BodyOctreeSceneNode::SetInstances` at `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:230` separates transform and material records. The transform ring is a bounded, fixed-capacity stream. `ExecuteImpl` copies one contiguous transform span per frame at `:604`; it does not allocate per instance during the frame. The per-ring-slot `materialDirty_` flags at `VIXEN/libraries/RenderGraph/include/Nodes/BodyOctreeSceneNode.h:553` mark cold material records for upload only after material changes. The transform bytes per frame are `96 * N`; cold material bytes are `48 * N` per changed ring slot.

`Affine3x4Gpu` is 48 bytes, `BodyInstanceTransformGpu` is 96 bytes, `BodyInstanceMaterialGpu` is 48 bytes, and the host-side `BodyInstanceGpu` is 144 bytes. The shader transform stream is binding 47 and the cold material stream is binding 10. Capacity checks and bounded copies preserve the existing R213 ring behavior.

The transform pair now drives ray marching, recipe bounds, sorting, culling, proxy intervals, picking, star lighting, and mining-beam endpoints. `VIXEN/shaders/SceneBindings.glsl:1081` reads the shared transform record and provides point, vector, and normal transforms. `VIXEN/shaders/TraceWorld.glsl:244` transforms rays into instance space, scales SDF steps by the affine minimum axis scale, and transforms normals by the inverse-transpose. The shadow path uses the same rules. The uniform-scale matrix specialization preserves the legacy arithmetic order for existing procedural scenes. `BodyOctreeSceneNode.cpp:2036` passes `localToWorld` directly to the ray-tracing instance without transposition or TRS recomposition.

`lane-minebeam` T-1170 was merged as `cf34e8e8`. Its `SpatialReuseShade.comp` source was manually combined with the starlight changes, and the generated SDI header was regenerated from that source. Mining-beam local endpoint offsets now pass through the source and target instance transforms.

The focused R424 witnesses cover a rotated asymmetric SDF against quarter-turn math, a rotated non-uniform SDF against a conservative minimum-axis-step reference, pick selection on a rotated instance, point-star lighting on a rotated and non-uniformly scaled body, transformed mining-beam endpoints, and direct RTAS row equality. The final focused run passed 5/5 tests, including `RenderStoredSdfBodiesNoHoles`, `PickSelectionReturnsInstanceForTwoBodiesAndNoneForBackground`, `EmissivePointLightFacesThreeBodiesTowardTheStar`, `CopiesLocalToWorldRowsWithoutTranspose`, and the production Cornell beam test. The separate asymmetric recipe quarter-turn test, `RenderRecipeBakedBody`, then passed 1/1 on the same final binary.

The fresh configure and build succeeded in `build/insttransform-logs/fresh-configure.log` and `fresh-build.log`; the build completed 1,138 steps. All 22 codegen `_check` targets passed: `accumulationconfig_check`, `appflow_check`, `callables_check`, `lightingconfig_check`, `lighttreebuffer_check`, `miningbeambuffer_check`, `octreeconfig_check`, `prevcameraconfig_check`, `probegridconfig_check`, `recipe_opcode_mirror_check`, `recipe_simd_check`, `recipeparams_check`, `reservoirconfig_check`, `reservoirrecord_check`, `sdf_core_kernels_check`, `shadowconfig_check`, `view_editor_layers_check`, `view_hud_blob_check`, `view_hud_check`, `view_hud_markup_check`, `view_hud_writer_check`, and `view_noun_enum_check`. `no_new_mutex_check` also passed. Codegen restore, build, and the MiningBeamBuffer `--check` passed.

The full RenderGraph CTest passed 1,341/1,341 with 6 registered skips and 0 failures. The 11 capture checks passed: four editor toggle/undo captures, three HUD captures, the production Cornell capture, and three starfield capture checks. The separate headless UI capture passed, both editor/HUD capture producers passed, and the native WSL capture witness passed 1/1. All seven existing editor/HUD PNGs were byte-identical to the merged-base captures from `c439440f`.

The full SVO CTest had one failure, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. The untouched-base SVO run at `c439440f` records the same test and diagnostic in `build/insttransform-logs/baseline-svo.log`, so this is the documented T-1449 finding outside this lane's scope. The final post-merge build recompiled the touched test binaries, and the following queued rebuild reported `ninja: no work to do`.

The VIXEN CodeGraph helper reported that this worktree and its canonical checkout have no CodeGraph index; navigation used the documented `rg` fallback. This is a mechanism lane with identity defaults, so the byte-identical capture check was the applicable visual gate.

The KFR follow-up belongs in `/home/liory/projects/KernelFederationRenderer/app/src/session_renderer.cpp`. `SessionRenderer::BuildRenderGraph()` at line 28 is where the production graph is assembled, and `SessionRenderer::PreTick()` at line 86 is the per-frame write site for the transform stream. KFR was not edited. T-1123 remains open for the other half of the sim-to-render contract; this lane does not design the view snapshot.

## SPT disposition

T-1150 should close at landing as replaced by R424, with the batch retaining only the base-proven T-1449 SVO finding. T-1170 should close at landing because the merged mining-beam path and rotated-endpoint witness pass. T-1123 remains open for the view-snapshot half of the sim-to-render contract.

## CONSOLIDATION ISSUES

- Use the configured CTest environment for WSL captures.
- Use non-destructive unique paths for queued witness status files.
- Generate identity-equivalent affine SDF shader specialization.
