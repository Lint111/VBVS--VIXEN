# ringidx: render-target ring image vs. descriptor layout (T-2883 VUID 09600; op-ringidx, Opus)

Lane `ringidx`, branch `lane-ringidx`, base `lane-presred` `fd46df8e`. `origin/wave/authoring-convergence` (`f750d6e9`) was already contained at the final check, so there was nothing to merge. No kernel or undertow change. Nothing pushed. Raw logs: `/home/liory/codeman-cases/undertow/reports/ringidx-evidence/` (not committed).

## The brief's premise was wrong (MEASURED)

The brief, following `reports/presred.md`, said the march transitions the render target's current ring image while its descriptor binds the view for the swapchain image index. Neither half holds for the look-dev graph:
- **The march transitions nothing.** `test_dispatch` has no `RENDER_TARGET_INFO`: its own log says `Dispatch: 63x63x1 for swapchain 500x500`. It also has `PARAM_WRITES_NO_IMAGE=true` (`BuildRenderGraph.cpp:7746`), so neither branch of the entry barrier (`ComputeDispatchNode.cpp:613`) runs.
- **The descriptor does not use the swapchain index.** Binding 0 (`outputImage`) is `compute_render_target`'s `CURRENT_VIEW`, a plain `VkImageView`. `RenderTargetNode::TypedExecuteImpl` republishes it from the same `currentIndex` every frame (`RenderTargetNode.cpp:144`, the KI-009 fix `8ae04b6a`). SpatialReuse's `IMAGE_WRITE` transitions `GetCurrentImage()` from that same index. **There is already one index authority**; no split exists to remove.

**Actual defect.** The march binds `outputImage` (declared `writeonly`, used only for `imageSize()`, `BodyInstanceRayMarch.comp:247`) but declares no access to it, so no transition is recorded for it. The first pass that does transition the image is SpatialReuse, which is submitted after the march. Each ring image is therefore UNDEFINED the first frame it is current.

Evidence (MEASURED):
- presred's api_dump (`presred-evidence/dozen-apidump/apidump.txt`): on frame 0, all 80 march tile submits are submitted before the first barrier on image `0x26`, which is UNDEFINED→GENERAL in a later command buffer.
- My Dozen base run with the duplicate cap off (`dozen-base/run.log`): 324 × `VUID-vkCmdDraw-None-09600`, all from `test_dispatch`. That is 81 tile submits × the 4 ring images `0x23/0x26/0x29/0x2c`, every one `current layout is VK_IMAGE_LAYOUT_UNDEFINED`. After first use the images stay in GENERAL, so later frames don't fire.

Because of that, I fixed the missing declaration rather than an index authority.

## Fix (`25d08998`)

The march now declares every storage image it binds `writeonly` in one image-write gatherer, through its existing `IMAGE_WRITE_ARRAY` slot. That slot already transitions each target's current image to GENERAL from its tracked layout (`ComputeDispatchNode.cpp:625`).
- `BuildRenderGraph.cpp:9022` adds `march_image_writes`: slot 0 is `compute_render_target`'s `RENDER_TARGET`, and it feeds `test_dispatch`'s `IMAGE_WRITE_ARRAY`.
- `ReconstructionMotion.h:23-51`: the motion module used to own a two-slot gatherer on that same march slot (a slot takes one connection). It now fills slots 1–2 of the march's gatherer (`marchWrites`, `firstWriteSlot`; called at `BuildRenderGraph.cpp:9033`).
- **Reuse, not new constructs:** the slot, the gatherer node type and the "gatherer → `IMAGE_WRITE_ARRAY`" shape are the motion module's own landed form (`6ee2a36d`). I added no slot, attribute, declaration form or abstraction.

**Shader-side alternative, rejected.** The other fix would drop binding 0 from the march and size from `idOutputImage`. I rejected it: about six RenderGraph/SVO tests dispatch the march with hand-built sets (`test_shadow_correctness`, `test_body_instance_raymarch_render`, …), and it would need regenerating the committed merged SDI.

## Regression test (`b95a34b8`)

`Application.LookdevCapture.StorageImageLayoutsMatchDescriptors` (`test_lookdev_capture.cmake:83`) uses presred's validation harness: midday, 500x500, 8 frames (covers all four ring images), `FAIL_REGULAR_EXPRESSION "None-09600"`.
- Base binary, Dozen: **Failed**, `Error regular expression found in output. Regex=[None-09600]` (`ctest-red.log`).
- Fixed binary, Dozen: **Passed** (`dozen-gpu-recon.log`).

## Witness

### 09600 per look-dev run (VUID mentions, duplicate cap off)

| | base | fix |
|---|---|---|
| Dozen (`dozen-base` / `dozen-fix`) | 324 | **0** (0 validation errors of any kind) |
| Native AMD 8060S, `C:\vixen-native\presred` (`native-base-auto` / `native-fix-auto`) | 324 | **0** (0 of any kind) |

How the runs were made:
- Both platforms: 16 frames, midday, angle 0, `--resource gpu`.
- Native base is presred's built `970a3a51` binary (same code as this base). Native fix is that checkout synced to `25d08998` and rebuilt incrementally: 0 `warning C` lines, `BuildRenderGraph.cpp` recompiled.
- Positive controls on the fixed binary, so 0 means 0: the loader log shows `Insert instance layer "VK_LAYER_KHRONOS_validation"` on both Dozen (`dozen-fix-loader`) and native (`native-fix-loader`).

Paths that changed wiring, validated on Dozen with the fix:
- Reconstruction on (`VIXEN_RECONSTRUCTION_DEPTH=1`): the motion module is active and the run has 0 validation errors (`dozen-fix-recon`). The motion/validity images are written only by the march, so if the shared gatherer had dropped them they would fire 09600 as well (INFERRED; the variadic connections are not logged).
- `VIXEN_RESERVOIR_CONFIG_ENABLED=1`, which T-2883 says doubled the 09600 count, has 0 errors (`dozen-fix-reservoir`). DirectLighting's matching `imageSize`-only binding (`DirectLighting.comp:216`) now finds the image already in GENERAL. I did not run a base reservoir run in this lane.

### Capture image

`midday-angle-0.png` is byte-identical before and after on both platforms:
- Dozen: sha256 `8513c0d6b62f50a4…`.
- Native: sha256 `57cfdebc30f6f84e…`, the same hash presred recorded.

I looked at both: the normal look-dev midday scene (temple, tree, figures, pool).

### Tests (Dozen, each group its own `--resource gpu` job, on the final tree `25d08998`)

- `Application.ReconstructionInputs.*` (6), `Application.LookdevCapture.NoNullAccelerationStructure` and `Application.LookdevCapture.StorageImageLayoutsMatchDescriptors`: **8/8 passed** (`dozen-gpu-recon.log`).
- `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`: **1/1 passed** in 533 s (`dozen-gpu-fourstates.log`).

**Failing names, before → after:**
- Before, measured here on the base binary: `Application.LookdevCapture.StorageImageLayoutsMatchDescriptors`.
- The other 9 on the base: presred measured them all passing on the same code (`presred-evidence/dozen-final.log`, plus their four-states run). I did not rerun them on the base.
- After: **none**.

**Warnings:** the WSL incremental build's only warning is the existing `DeviceNode.cpp:684` deprecation; the native build had 0.

## R412 shape check (added lines, `fd46df8e..HEAD`, reports excluded)

No `throw`, attribute, `EntityId`, `KindRef`, `Catalog<`, pipe, ordering hint or `static readonly` table. The C++ change is one gatherer node with two connections, and module params that name the gatherer to fill. The CMake change is one `add_test` with existing properties.

## LANDABLE NOW

Repo VBVS--VIXEN, branch `lane-ringidx`:
- `b95a34b8` test: fail look-dev under validation when a bound storage image is in the wrong layout
- `25d08998` fix: declare the march's render-target binding so its ring image is GENERAL first
- the commit carrying this report

## STOPs

None for the fix.

One premise correction, for the controller: the "one index authority" refactor the brief asked for is not needed. That authority already exists (`RenderTargetNode.cpp:144`, `8ae04b6a`). The 09600 came from a missing layout declaration, as shown above.

## SPT DISPOSITION

- T-2883: **CLOSE.** presred fixed 03580 and 08114. This lane fixes 09600: 324 → 0 uncapped on Dozen and native, the reservoir switch is clean, and there is a regression test that was red on the base.

## CONSOLIDATION ISSUES

- **Proposal `1a62bb1067c3`** (tier:luna, effort:M): a pass can bind a storage image that no sync slot transitions, and nothing reports it. Remaining instances of the class:
  - DirectLighting's `imageSize`-only binding now depends on the march's transition;
  - `ComputeStageNode::IMAGE_READ_ARRAY` records no transition, so any reader ordered before an image's first writer reproduces this bug.
  - `SdiHazardCensus` already holds each stage's image members and their providers, and could flag these cases.
- The validation layer's default cap of 10 reports per VUID hid the scale here: 10 reported vs. 324 real. Validation tests that count VUIDs should set `VK_KHRONOS_VALIDATION_ENABLE_MESSAGE_LIMIT=false` (`ringidx-evidence/dozen-validate.sh`, `validate.bat`).
- **Queue/native setup friction:** in a queue payload, `cmd.exe` must be called by absolute path (`/mnt/c/Windows/System32/cmd.exe`); the bare name exited 127. The native checkout `C:\vixen-native\presred` is now on local branch `ringidx-native` at `25d08998`.
