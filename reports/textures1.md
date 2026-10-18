# sn-textures1 lane report

Base: `f750d6e9d2535dfc97329584c9d067b9a9269784`

Implementation: `fd89b971` (`feat(textures): extract texture runtime module`)

Witness fixes: `705ca3ea` (`fix(textures): unblock scoped witness targets`)

## LANDABLE NOW

- `VIXEN/libraries/Textures` is a buildable runtime target for the existing
  texture model. The target and moved implementation are declared in
  `VIXEN/libraries/Textures/CMakeLists.txt:4-39`; the static STB decoder now
  returns its existing `VulkanError` without calling the instance-only logger
  (`VIXEN/libraries/Textures/src/STBTextureLoader.cpp:27-30`).
- The RenderGraph node remains a binder to the extracted module and keeps the
  existing graph output contract (`VIXEN/libraries/RenderGraph/src/Nodes/TextureLoaderNode.cpp:110-122`).
  The five new cases are in `VIXEN/libraries/Textures/tests/test_texture_module.cpp:40-74`
  and `VIXEN/libraries/RenderGraph/tests/Nodes/test_texture_loader_node.cpp:11-32`.
- The scoped RenderGraph build also exposed an existing GLM portability defect:
  `AccumulationConfigNode.cpp` includes `glm/gtx/norm.hpp` without the required
  `GLM_ENABLE_EXPERIMENTAL` opt-in. The source-local define at
  `VIXEN/libraries/RenderGraph/src/Nodes/AccumulationConfigNode.cpp:19` matches
  the established `SVOBuilder.cpp` pattern and is filed as proposal
  `add65e652f35fd0284bc8944f666f61f82473284cf3afa2da75233d5d1807d1f`.
- The hard rule scan found no new `EntityId`, `KindRef<All>`, explicit `Id`
  member, or ID cast in the changed texture/RenderGraph source. `git diff --check`
  passes, and no new warnings were emitted by the changed sources.
- R649 disposition: **PARTIAL** — extracted module and scoped runtime witnesses
  are complete; the production instancing capture remains blocked by the
  external FSR3 compiler dependency documented below.

## Verification

Measured on the merged `origin/wave/authoring-convergence` tree (already up to
date before the witness changes):

- Fresh queued CMake configure passed with `UNDERTOW_ROOT=/home/liory/projects/undertow`.
  The documented WSL preset provisioned the Vulkan SDK/windowing dependencies
  and generated `VIXEN/build/wsl`.
- The initial queued target name `RenderGraph` failed because it is an
  `INTERFACE` target; the concrete targets are `RenderGraphCore` and
  `RenderGraphNodes`. The corrected queued build reached the source targets.
  This existing issue is recorded by SPT proposal
  `7493918f6f210fdd171d295d2fc67e96bf84d2feb5e4923d0ef491c888f7f348`.
- New texture/node cases: **5/5 pass** on the GPU queue:
  three texture cases (`TextureDecode` x2 and `TextureCacher` x1) plus two
  `TextureLoaderNodeBinding` cases.
- Existing VulkanResources tests: **16 pass, 1 skipped, 0 failed** — 14 GPU
  cases (including the documented device/driver identity skip) and 3 CPU
  policy cases.
- Existing RenderGraph core tests: **42/42 pass** — 40 existing cases plus
  the two new texture-loader binding cases. The two new cases were also run
  directly as a 2/2 targeted witness.
- The full VIXEN application/editor and instancing demo object were not
  buildable: both the queued `VixenApp`/`VIXEN` build and the direct
  `BuildInstancingDemoGraph.cpp.o` target stop before compilation because
  this file is absent:
  `VIXEN/third_party/fidelityfx-sdk-1.1.4/sdk/tools/binary_store/FidelityFX_SC.exe`.
  Consequently no instancing capture or before/after byte comparison was
  produced. This is the brief-known FSR3-optional wave blocker, not a texture
  test failure. The related existing consolidation proposal is
  `1359197da1e4c6f76b760086ffb4c374855bc42bd4ed5a34cd65ef589849dbf0`.

Before (wave tip `50e49da6a`), there was no successful configured build or
CTest result, so no baseline test names were available. During the fresh
witness, the exposed failures were: the invalid `RenderGraph` aggregate target
name, the static STB logger call, and the missing GLM opt-in; after the scoped
fixes, all required library tests above pass. The only remaining failure is the
FSR3 compiler prerequisite for the production app/capture.

## STOPs

- **STOP: instancing demo capture unavailable.** The required production caller
  is `VIXEN/application/main/source/graph/BuildInstancingDemoGraph.cpp:49-80`,
  but its object target has an order-only dependency on generated FSR3 headers
  produced by the absent `FidelityFX_SC.exe`. The requested before/after
  capture therefore cannot be measured in this lane. Owner action is to land
  the documented FSR3-optional wave/provision the pinned compiler binary; no
  source workaround was added.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: RenderGraph node needs source-local GLM experimental opt-in
  (SPT proposal `add65e652f35f`)
