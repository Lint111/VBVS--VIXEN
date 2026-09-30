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

## CONSOLIDATION ISSUES

- None. No incidental workaround or non-facade implementation delta was needed.
