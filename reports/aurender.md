T-1140 is paused before implementation pending an engine API and coordinate-origin ruling. The required precision behavior is clear, but the current public instance and camera paths expose only floats. Correctly rebasing them requires choosing how double poses enter the renderer and how the same frame origin reaches rendering, culling, ray setup, acceleration structures, and temporal reprojection. No semantic source changes were made.

## Scope and starting point

- Lane: `aurender`; branch: `lane-aurender`.
- Base SHA: `d3316e7d05ab2f53ff582ea1932bdc0b6a2781e2`.
- CodeGraph was queried first with `codegraph explore "VIXEN scene node instance positions camera pose narrowing GPU buffers shaders octree culling ray setup"`; it reported no `.codegraph/` index. The documented fallback was `rg`.
- Required implementation scope, once the API boundary is decided: `BodyOctreeSceneNode` and its SVO instance data, `CameraNode`/camera inputs, CPU sorting and residency culling, shader ray setup/traversal, RT-query TLAS transforms, and temporal reprojection. Fresh VIXEN RenderGraph/SVO builds, the focused sort/frustum tests, affected GPU render tests, and a close-up capture are needed after implementation.

## Measured position path before changes

1. **Instance storage and entry seam:** [`ShellOctreeGpu.h`](VIXEN/libraries/SVO/include/ShellOctreeGpu.h#L494) defines `BodyInstanceGpu::worldPos` as `float[3]`; the record is fixed at 64 bytes by assertions at lines 515–518. [`BodyOctreeSceneNode.h`](VIXEN/libraries/RenderGraph/include/Nodes/BodyOctreeSceneNode.h#L98) exposes `SetInstances(std::vector<BodyInstanceGpu>)`, and [`BodyOctreeSceneNode.cpp`](VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp#L230) stores that already-narrowed record. The existing `PackInstances` helper in `ShellOctreeGpu.h` (lines 1421–1428) is a byte copy, not a conversion.
2. **GPU upload:** `BodyOctreeSceneNode.cpp` lines 585–615 copies `instances_` directly into the per-frame SSBO. There is no camera subtraction on this path.
3. **Sorting:** `BodyOctreeSceneNode.cpp` lines 251–280 constructs `glm::vec3` from the float fields. `InstanceSort.h` lines 23–30 subtracts a `glm::vec3` camera position and compares float squared distances.
4. **CPU culling/residency:** `VulkanGraphApplication.cpp` lines 4404–4423 reads the float `CameraData` pose; lines 4457–4497 constructs float instance centers, applies float frustum/resolvability/occlusion checks, and measures float camera distance. `FrustumCull.h` lines 44–99 builds and tests all six planes with `glm::vec3`/`float`.
5. **RT-query transform:** `BodyOctreeSceneNode.cpp` lines 1991–2000 converts `worldPos` to `glm::vec3`, then a float `glm::mat4` translation before writing TLAS instance transforms.
6. **Camera pose and matrices:** [`CameraData.h`](VIXEN/libraries/RenderGraph/include/Data/CameraData.h#L17) has float `glm::vec3` camera fields at fixed offsets and documents that its field order matches shader push constants. `CameraNode.h` line 215 stores `cameraPosition` as `glm::vec3`; `CameraNode.cpp` lines 144–179 and 315–385 builds its view and inverse matrices with float GLM types. `CameraNodeConfig.h` lines 67–72 says the `CameraData` layout is frozen.
7. **Ray setup and traversal:** `BodyInstanceRayMarch.comp` lines 237–239 starts with `rayOrigin = pc.cameraPos`. `TraceWorld.glsl` lines 468–470 subtracts `inst.worldPos` from the ray in shader `vec3` math before scaling into instance space. Thus both operands have already lost AU-scale precision before subtraction.
8. **Temporal consumers:** `DirectLighting.comp` lines 245–255 projects `bestWorldPos` through the previous frame's view-projection matrix. A per-frame origin change needs an explicit previous/current-origin relation for this path.

## Decision needed before implementation

The slice contract requires double world positions through the renderer, subtraction of the camera origin in double precision, and only then float GPU values. It does not specify the public CPU input seam or the authoritative owner/transport for the per-frame origin. The current `SetInstances` API accepts GPU-layout data, while `CameraData` is a frozen float/shader ABI. Adding a double pose path therefore changes or extends public engine APIs; putting doubles into the GPU record would break the existing shader ABI and its 64-byte layout.

Please decide which engine boundary should carry the high-precision values:

- **Additive host-pose API:** keep the 64-byte `BodyInstanceGpu` ABI and legacy setter, add a CPU-side double-position input seam using existing `glm::dvec3`, and add a camera world-pose/origin setter. The owner must specify how the body node and camera share one origin each frame.
- **RenderGraph pose resource:** publish an authoritative double camera pose/origin through a new graph resource consumed by both camera and body-scene paths, with a separately specified CPU instance input seam. This has more graph wiring but avoids independently synchronized setters.

Either option also needs to state how the origin is represented for previous-frame reprojection. No option was selected because the task explicitly says to stop on an unruled public API/design choice.

## Changes and verification

- No production code or tests were changed; no baseline build/test command was run before stopping. This is a design stop, not a reported build failure or an `unobtainable baseline` classification.
- The 30-AU narrowing error is not measured because the required conversion path and test do not exist yet. Documentation edits are limited to this report and the VIXEN active-context note; both are included in the lane commit.
- The existing headless renderer test is registered as `test_rendergraph_criticalnodes_gpurender1` in `libraries/RenderGraph/tests/test_critical_nodes.cmake` (lines 284–305); its source documents a 512×512 PNG at `/tmp/glsl_shader_near.png`. `/dev/dxg` and the WSL Dozen ICD file are present. `glslc` is not currently on `PATH`, and `VIXEN/.vulkan-sdk` is absent; the documented `vixen-wsl` configure preset auto-provisions the SDK. No close-up capture was produced because implementation is paused pending the ruling above.

## Run 2

### Summary

Implemented the smallest missing engine increment for AU-scale observer addressing: a CPU resolver follows a `TierAddress` through each tree's own `TierRefTable` slice and returns the active octree together with the already tier-local camera offset. The 30-AU fixture resolves four `2^-10` hops into a 16.327016496 m tier, then renders a close-up through the existing GPU crossing and LOD path. The double value stays tier-local; there is no flattened AU coordinate, floating origin, or `BodyInstanceGpu` layout change.

### Shipped vs. missing map

The base history includes the Inc2 merge (`2d67840e`) and the Inc3 scale/crossing work (`892bb38a`, `bad30727`, `519e30e2`); each is an ancestor of `origin/wave/authoring-convergence`. The Inc2 plan records `TierRef`/`TierRefTable`, `farBit` crossings, traversal restart, and LOD/residency as shipped (§M1–M4, lines 120–175). The Inc3 plan's early M4 status says the epic gate was still open, but its later M8 Task 24 progress log closes the true two-hop magnified gate (lines 1831–1855). The current wave contains that later code and progress.

**Already shipped on this base:**

- `TierRef::childOriginLocal` and `childScale` express one child origin and scale in the parent tier's local frame (`VIXEN/libraries/SVO/include/TierRef.h:46–51`). The concatenated per-tree table and counts are in `ConcatenatedOctrees` (`ShellOctreeGpu.h:475–492`); the observer design says only the CPU chain needs double/fixed-point precision (§3.3, lines 153–162).
- The GPU ABI binds the table at binding 15 (`VIXEN/shaders/SceneBindings.glsl:118–135`). The leaf path checks `farBit`, resolves the current tree's table slice, and treats that leaf as a tier crossing (`SceneBindings.glsl:1757–1786`).
- Child-scale-aware magnification, camera-distance LOD, and child residency fallback are implemented in `SceneBindings.glsl:1802–1862` and `1877–1898`. The LOD gate stays local to each hop and uses the child size in world units.
- Chained crossings use `MAX_TIER_HOPS = 5` and repeat the ordinary traversal (`SceneBindings.glsl:2203–2252`). The crossing wrapper remaps the ray and selects the child's config (`SceneBindings.glsl:2313–2342`); hit distance is composed through `cumulativeDirLen` (`SceneBindings.glsl:2268–2273`, `2333–2382`). The Inc3 plan records the five-hop loop and live two-hop discriminator at lines 197–219.
- `TierAddress` already exists as an in-process hop identity (`VIXEN/libraries/SVO/include/TierAddress.h:53–83`). Undertow wire reconciliation remains explicitly deferred (`TierAddress.h:22–24`; observer design §4, lines 187–193).

**Missing before this run / still missing after it:**

- There was no CPU helper to resolve a `TierAddress` against the concatenated `TierRefTable` slices. Added `VIXEN/libraries/SVO/include/TierAddressResolver.h:35–71`; it checks the root, address hops, table ranges, scales, origins, and finite tier-local position, then returns the active octree index while preserving that position unchanged.
- The runtime camera/body producer does not call that helper yet. `BodyOctreeSceneNode::SetInstances` still accepts the 64-byte GPU record and `SetRecipePool` accepts the pool (`RenderGraph/include/Nodes/BodyOctreeSceneNode.h:98,154`; implementation `RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:230–249,299–305`). A repository search finds the new resolver used only by its tests. The production caller that supplies observer address + tier-local offset and authors per-system body refs remains the next integration increment.
- `BodyInstanceGpu` remains 64 bytes. Its `worldPos` comment now says it is valid only as the active tier's local position and must not hold a flattened AU coordinate (`ShellOctreeGpu.h:500–516`). No new double world-position API, floating origin, TierRef upload shape, or Undertow wire format was introduced.

### Change

- Added the CPU `ResolveTierAddressPosition` bridge without composing ancestor origins into a global coordinate. The 30-AU test uses a 120-AU system frame whose first child origin is 1.75, followed by four `2^-10` parent-local hops. It resolves to tree 4 and carries the local camera point unchanged.
- Added the 30-AU headless GPU test in `VIXEN/libraries/RenderGraph/tests/Nodes/test_tier_crossing_lod_residency.cpp:1165–1357`. The four address hops resolve on the CPU; GPU traversal starts at that active tier and exercises the existing detail-child crossing. At the active span of 16.327016496 m, the GPU hit is 74.148475647 m away, and the test asserts the hit remains in the requested 1–100 m range. Moving the tier-local camera exactly 1 m changes `hitT` by 1.000000000 m (0 m error) and leaves the hit position unchanged (0 m drift). The same fixture sets a coarse LOD footprint and verifies that the detail-child pixels disappear.
- The separate CPU float-boundary check measures a 1 m camera clearance with `9.82662444748e-8 m` error (`test_tier_address.cpp`, `TierAddressResolver.ThirtyAuAddressKeepsCloseupOffsetInTheActiveTier`). That is about `0.0983 µm`, comfortably below the 1 mm target.
- Corrected the three test-only C++ push-constant mirrors to 96 bytes with an explicit final pad. The shader payload ends at byte 92, but std430 rounds its block to 16-byte alignment. This removed `VUID-VkComputePipelineCreateInfo-layout-10069` from the affected GPU fixtures.

### Verification and capture

- Relevant SVO suites passed: `test_tier_ref` 5/5, `test_tier_ref_table` 5/5, `test_tier_crossing_construction` 5/5, `test_tier_crossing_mirror_parity` 6/6, `test_tier_address` 17/17, `test_tier_direction` 5/5, `test_tier_math` 9/9, and `test_tier_magnitude` 10/10 (62 tests total).
- `test_rendergraph_criticalnodes_gpurender1`: 11/11 passed. `test_rendergraph_criticalnodes_gpurender2`: 8/8 passed, including the three tier crossing/LOD tests and the editor render regressions. The editor suite was also rerun alone: 3/3 passed with no push-constant range VUID.
- Rebuilt the changed `gpurender2` target after the final mirror fix with `nice -n 10 cmake --build ../.tmp/vixen-wsl --target test_rendergraph_criticalnodes_gpurender2 --parallel 8` from `VIXEN/`. Both GPU binaries used the documented `vixen-wsl` Dozen ICD and Vulkan layer path. The first full launch from the worktree root failed at `vkCreateInstance` with `-9` for all eight tests; launching the same binary from `VIXEN/` with the same ICD/layer paths succeeded, and the full suite was rerun there.
- The live capture is [reports/aurender-30au-closeup.png](aurender-30au-closeup.png), 501×501. It shows the distinct magenta child tree in the close-up.

One validation diagnostic remains on every GPU shader creation: `VUID-VkShaderModuleCreateInfo-pCode-08737`, because the test command compiles SPIR-V 1.6 with `--target-env=vulkan1.3`, while the selected Dozen runtime validates under Vulkan 1.2 semantics. The GoogleTest runs still exit zero; this diagnostic also appears in the unchanged `gpurender1` binary, so it is outside this change. Its compiler/runtime contract has been filed as a consolidation proposal. The earlier schema-catalog configure failure was recovered by supplying `-DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json` before building.

## CONSOLIDATION ISSUES

- proposed: Resolve the Undertow schema catalogue from the VIXEN WSL preset
- proposed: Generate C++ GPU-test push-constant mirrors from the shared shader ABI
- proposed: Match test SPIR-V target environment to the Vulkan runtime
- proposed: Let the tier-crossing GPU harness address arbitrary child octree indices
- proposed: Provide a canonical launcher for WSL Dozen headless tests
- proposed: Write shareable PNG captures directly from headless GPU tests
