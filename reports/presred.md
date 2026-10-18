# presred: VIXEN presentation red states (op-presred, Opus)

Lane `presred`, branch `lane-presred`, base `origin/wave/authoring-convergence` `f750d6e9` (already current at start and at the final check). Nothing pushed. Raw logs: `/home/liory/codeman-cases/undertow/reports/presred-evidence/` (not committed).

## T-2883: baseline look-dev Vulkan validation errors

### Reproduced (MEASURED, native AMD 8060S, `--resource gpu`)

The fsr3 lane's native binary at `af7caa3f` is code-identical to this base (`af7caa3f..f750d6e9` changes only `reports/fsr3.md`). Run: look-dev, midday, angle 0, 500x500, scale 1.0, `--upscaler=none`, 16 frames, `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (`native-base-auto.run.log`). The test builds the graph twice; each build gives:

| VUID | count (2 builds) | object |
|---|---|---|
| `VkWriteDescriptorSetAccelerationStructureKHR-pAccelerationStructures-03580` | 10 | `pDescriptorWrites[17]` acceleration structure is `VK_NULL_HANDLE`, no `nullDescriptor` |
| `vkCmdDispatch-None-08114` | 10 | set 0, binding 40, `rtQueryTlas` = `VkAccelerationStructureKHR 0x0` |
| `vkCmdDispatchBase-None-08114` | 10 | same, from the tiled (GPU-yield) march dispatch |
| `vkCmdDraw-None-09600` | 10 | one 500x500 `R8G8B8A8_UNORM` storage image expected `GENERAL`, is `UNDEFINED` |

(The counter in `uniq -c` shows 20 per VUID because the layer prints each VUID twice per message.) All occur only on the first frame of each graph build.

### Root cause of 03580 / 08114 and the fix (`cd81fb61`, `970a3a51`)

`BodyOctreeSceneNode` publishes `RTQUERY_TLAS` (binding 40, `rtQueryTlas`, statically used by the shadow wave and the RT-traversal march whenever RayQueryLighting is available). Two defects left it `VK_NULL_HANDLE`:
1. It was published at compile and built only in `ExecuteImpl`, while consumers write their descriptor sets as Compile returns. The node already states the rule for its shell buffers: "a later Execute cannot replace a placeholder handle already captured by a consumer" (`BodyOctreeSceneNode.cpp:559`). `cd81fb61` calls `EnsureRtQueryTlasBuilt(devicePtr)` in `TypedCompileImpl` before publishing (`BodyOctreeSceneNode.cpp:622-627`).
2. `EnsureRtQueryTlasBuilt` returned without a TLAS when no Stored-SDF instance had occupied bricks (and when no instances or octrees were staged). The look-dev scene is all procedural (`providerKind=1`, `LookdevSceneDefinition.h:340`), so the handle stayed null for the **whole run**: the "10 per state" counts are the validation layer's default duplicate-message cap, not a first-frame-only error. `970a3a51` builds a zero-primitive TLAS in that case (instance buffer sized for at least one record; `memcpy` skipped when empty): a valid descriptor whose ray queries hit nothing, the same result the procedural path got from the null handle on AMD. The epoch key still prevents rebuilds of an unchanged TLAS.

`cd81fb61` alone did not change the native counts (MEASURED: `native-fix-auto.run.log`, same 20/20/20/20 as base); both commits together remove them (below). No new mechanism.

### Regression test (`85689a93`)

`Application.LookdevCapture.NoNullAccelerationStructure` (`test_lookdev_capture.cmake`, after `TemporalLifecycle`): one look-dev state with the Khronos layer forced on (`VK_LAYER_PATH` from the provisioned SDK, or `%VULKAN_SDK%\Bin` on Windows); `FAIL_REGULAR_EXPRESSION` on 03580 or a dispatch using a `0x0` acceleration structure.
- The regex matches 40 lines of the pre-fix native log (would fail before).
- Dozen (WSL, `--resource gpu`): 1/1 passed. The layer was loaded (the unrelated 09600 appears 20 times in `LastTest.log`); Dozen has no RayQueryLighting, so binding 40 is compiled out and the test passes by construction there. It is meaningful only on a ray-query device.
- Native: the regex matches 0 lines after `970a3a51` (below).

### 09600: diagnosed, not fixed (PARTIAL)

Reproduces on Dozen too (10 errors per run, `dozen-fix/run.log`). Traced with `VK_LAYER_LUNARG_api_dump` (`dozen-apidump/apidump.txt`):
- the image is the second of a 4-image ring of 500x500 `R8G8B8A8_UNORM` `STORAGE|TRANSFER_SRC` images (wrapped handles `0x23/0x26/0x29/0x2c`, created consecutively): a render target with one image per swapchain image;
- the failing command buffers are the march's tiled `vkCmdDispatchBase` chunks (`test_dispatch`); their set `0xfc` binding 0 is that image's view (`0x28`) in `GENERAL`;
- the march's own barrier (`ComputeDispatchNode.cpp:612-616`) transitions `renderTargetInfo->GetCurrentImage()`, the render target's *current* ring index, while `DescriptorSetNode::HandleStorageImage` picks the view by the swapchain `imageIndex`. On the first frames these differ, so the march transitions one ring image and reads another. The image read is first transitioned to `GENERAL` by a later submission.
- The featureab note that `s_b2_proxy_prepass` removes it is consistent with that: the pre-pass changes which ring slot is current on the first frame (INFERRED).

The fix is to make the barrier and the descriptor select the same ring image (one index authority). That touches the render-target ring index contract shared by `RenderTargetNode`, `DescriptorSetNode` and `ComputeDispatchNode`, so I stopped at the diagnosis rather than change it without time to witness every consumer.

## T-4657: instance motion history in the producer (STOP, design)

Reproduced (MEASURED, Dozen, `Application.ReconstructionInputs.TemporalLifecycle`, `--resource gpu`, passed): `midday-angle-0.motion-validity.f32` has 0 nonzero pixels of 41,917, while the same frame has 11,584 nonzero camera vectors; `.motion.json` records `"procedural_correspondence_valid":false`. The test's oracle counts any nonzero validity as a failure (`test_lookdev_capture.cpp:1348`), so the suite currently pins validity at 0.

Why it is not wired: `InstanceMotionHistory<TKindRef>` (`libraries/SVO/include/InstanceMotionHistory.h:22`) is keyed by the source kind's generated Ref plus a slot generation, per R494.2 ("the stable identity ... is the schema kind instance behind it (its generated Ref) plus a generation counter ... No renderer-side ID registry"). No producer of `BodyInstanceGpu` has such an identity:
- VIXEN producers: `Lookdev::BodyInstances` (`LookdevSceneDefinition.h:321`, keyed only by the `Material` enum), `Planet::BodyInstances` (`PlanetSceneDefinition.h:543`), the editor's single instance (`EditorApplication.cpp:812`) and about 25 demo scenes in `BuildRenderGraph.cpp`. None carries a schema Ref.
- Undertow `origin/main-wave` (`eb2b683f4`) has no caller of `SetBodyInstances`/`BodyInstanceGpu`.
- The original commit already says so: "Procedural producers without schema rows remain invalid pending R543" (`6ee2a36d`).

Wiring it now would mean keying by a render index or an enum, which is the renderer-side identity R494.2 rules out. Options:
1. **(Recommended)** Wire it where a schema Ref exists: the undertow sim's body producer, when it feeds VIXEN, calls `ResolveFrame` with the body kind's generated Ref and slot generation. VIXEN demo and fixture scenes stay invalid by design (as today). The pick vote keeps resetting on camera motion only until then.
2. Declare look-dev fixture objects as a schema kind so the fixture has generated Refs (new declarations in VIXEN; owner ruling needed).
3. Allow authored fixture ordinals as identity for non-sim producers (contradicts R494.2; owner ruling needed).

## T-2905: agent pause vs. presented-frame counter (STOP, design)

Diagnosis (from code, not a run): under `appflow.pause`, `PreTick`/`Update` return early (`EditorApplication.cpp:979`, `:1132`) but `PostTick` still increments `channelPresentedFrame_` (`:1267`). A dispatch while paused records `channelActionFrames_[key] = channelPresentedFrame_ + 1` and a readback at that frame (`:1683`, `:1697`), but no tick applies the action until a step, so `await_frame`/readback resolve on a frame that does not contain the change (the ticket's "zero changed pixels"). `appflow.step` itself is consistent: budget `N` runs exactly the next `N` rendered frames' ticks, so `targetFrame = presented + N` holds.

The fix changes the channel contract: a dispatch's frame is not knowable while paused (it depends on when the client steps), so either the response reports a pending tick and the frame is resolved at the first presented frame after the tick that applies it, or a new atomic dispatch-and-step request is added (the ticket asks for one). Both change the R468 synchronize surface that external clients (agentport MCP wrapper, latency gates) use, and the ticket itself says "the lane does not patch the R468 implementation". Recommendation: resolve action and readback frames at the tick that applies them (frame field becomes the presented frame after the applying tick; `await_frame` waits while unresolved), no new request kind. Needs an owner ruling on the response field.

## T-4658: FSR3 1.41x at 1080p @ 1/2 (OPEN, not started)

Not attempted: it needs native `--gpu-benchmark` A/B/A rounds with FSR3 and per-pass timing, and the time went to T-2883. Nothing measured.

## T-2981: declarative pass-output readback snapshots (OPEN, not started)

A graph-level ownership and lifetime model for readback snapshots is a feature/design task (new graph declaration), larger than this lane's red-state scope. Not started.

## Native witness

Own native checkout `C:\vixen-native\presred` at `970a3a51`, `vixen_lookdev_capture` Release, AMD 8060S, `--resource gpu`, same run as the reproduction (`native-fix2-auto.run.log`):

| VUID | base `af7caa3f` | `cd81fb61` only | `970a3a51` |
|---|---|---|---|
| 03580 (null AS write) | 20 | 20 | 0 |
| `vkCmdDispatch-None-08114` (`rtQueryTlas` 0x0) | 20 | 20 | 0 |
| `vkCmdDispatchBase-None-08114` | 20 | 20 | 0 |
| `vkCmdDraw-None-09600` | 20 | 20 | 20 (open, see above) |

(counts are VUID mentions; each message names its VUID twice.) Test PASSED in all three. Capture `midday-angle-0.png` before and after is byte-identical (sha256 `57cfdebc30f6f84e...`, `native-base-midday.png`, `native-fix2-midday.png`); I looked at it: the normal look-dev midday scene. Native build: 0 `warning C` lines in the incremental rebuild.

## Tests

Dozen (WSL), each through the queue with `--resource gpu`, on the final tree `970a3a51`:
- `BodyInstanceRt*` + `BodyOctreeLifetimeTest.*` (6), `Application.ReconstructionInputs.*` (6: Depth.native, Depth.reduced-odd, Depth.fixed-exposure, TemporalLifecycle, Jitter.1.0, Jitter.0.5) and the new `Application.LookdevCapture.NoNullAccelerationStructure` (1): 13 selected, 13 passed (`dozen-final.log`).
- `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`: passed (539 s) after `cd81fb61`; not rerun after `970a3a51` (that change only runs on RayQueryLighting devices, which Dozen is not).
- Failing names before / after: none in this selection on either side. The first lifetime run failed 5/6 with `[harness setup] ... libVkLayer_vixen_gpu_witness.so: cannot open shared object file` (an unbuilt target in a fresh tree, not a code failure); after building `vixen_gpu_witness_layer`, 6/6 passed.
- Warnings: 0 in `BodyOctreeSceneNode.cpp`; the only warning in the `test_rendergraph_criticalnodes_infra2` build is the pre-existing `DeviceNode.cpp:684` deprecation.

## R412 canonical-shape check on added lines

`f750d6e9..HEAD`, excluding reports: no `throw`, attribute, `EntityId`, `Catalog<`, `KindRef`, pipe, ordering hint or `static readonly` table in added lines; the C++ change is one call, a removed early return, a sized buffer and comments; the CMake change is one `add_test` with existing properties. No new warnings in `BodyOctreeSceneNode.cpp` (the build's 94 warning lines are in `vk_mem_alloc.h`, `DynamicVoxelStruct.h`, `ffx_vk.cpp` and other untouched files).

## LANDABLE NOW

Repo VBVS--VIXEN, branch `lane-presred`:
- `cd81fb61` fix: build the ray-query TLAS before compile publishes it (T-2883)
- `85689a93` test: fail look-dev under validation when a null ray-query TLAS is bound (T-2883)
- `970a3a51` fix: give a scene with nothing to ray-query an empty TLAS, not a null one (T-2883)
- the commit carrying this report

## STOPs

- T-4657: no producer carries the R494.2 identity; options above.
- T-2905: fixing it changes the R468 channel response contract; recommendation above.
- T-2883 09600: ring-index authority split between the barrier and the descriptor; diagnosis above, not fixed.
- The brief says ws lacks `FidelityFX_SC.exe`; other worktrees hold it as an ignored vendor binary. I copied it (and `glslangValidator.exe`) into this worktree's ignored `third_party/.../binary_store/` to build `vixen_lookdev_capture`; nothing committed.

## SPT DISPOSITION

- T-2883: PARTIAL. 03580 and 08114 fixed with a regression test (native witness above); 09600 diagnosed, not fixed.
- T-4657: OPEN (STOP, design; reproduction evidence above).
- T-2905: OPEN (STOP, design).
- T-4658: OPEN (not started).
- T-2981: OPEN (not started).

## CONSOLIDATION ISSUES

- Render-target ring index: `ComputeDispatchNode` transitions `GetCurrentImage()` while `DescriptorSetNode` binds the view for the swapchain `imageIndex`; one index authority would remove T-2883's 09600 and any similar first-frame layout mismatch.
- Every `BodyInstanceGpu` producer in VIXEN is a demo or fixture with no schema identity, so per-instance history (motion validity, R612's instance-motion reset) cannot work anywhere until the sim producer feeds VIXEN (T-4657).
- Native setup cost: a new native checkout cannot reuse another tree's `FetchContent` cache (`*-subbuild` caches embed the source tree path); only `*-src` copies are reusable.
