# Lane `gpudevloss` report

Base and lane head at start: `dc2ea8828b887b71c183282d09f460463e63bc00`. `origin/wave/authoring-convergence` was at the same SHA before the final witnesses, so no merge was needed.

## Finding and change

The reproducible editor crash was a command-buffer lifetime violation during dirty graph recompilation. The capture log from gpulock reports `VUID-vkFreeCommandBuffers-pCommandBuffers-00047`: `ComputeDispatchNode::CleanupImpl` freed a command buffer that was still pending. The compute node owns a ring spanning four frame flights; a frame-slot fence only protects reuse of that slot and does not make the other submitted command buffers safe to free when recompilation cleans up the whole ring.

`RenderGraph::RecompileDirtyNodes` now waits for graph devices to become idle after invalidating the old execution epoch and before calling any `CleanupImpl`. It uses the existing `WaitForGraphDevicesIdle` helper, which obtains devices from their owning `DeviceNode`s. This adds a wait at dirty recompilation waves, not during ordinary frame rendering. The source-tree sample mutation found while exercising the producer is also fixed: CMake stages `sample_tri_layer.vxd` in the build-tree capture directory and passes that path to the editor, so scripted save/reopen no longer writes `BuiltAssets/documents/sample_tri_layer.edited.vxd`.

## Reproduction and classification

The pre-fix capture evidence is in `/home/liory/.local/state/undertow/undertow-box-logs/1791545797-test-gpudevloss:standard-captures.log`. After the fix, the serialized editor/HUD producer plus nine dependent capture assertions passed **11/11** in 46.29 seconds. The tracked `.edited.vxd` remained at its original 588 bytes.

For the device-loss stress, I removed `RESOURCE_LOCK` only from generated CTest files in `build/gpudevloss-scratch`; the source lock remains enabled. The scratch build had its own capture output paths and staged editor document. Each run selected 22 cases: 11 raymarch GPU cases, two capture producers, and nine capture assertions.

| CTest parallelism | Result | CTest wall time |
| ---: | ---: | ---: |
| `-j2` | 22/22 passed; no device loss | 60.08 s |
| `-j4` | 22/22 passed; no device loss | 40.32 s |
| `-j8` | 22/22 passed; no device loss | 41.79 s |
| `-j12` | 22/22 passed; no device loss | 50.38 s |
| `-j16` | 22/22 passed; no device loss | 48.86 s |
| `-j16` retry | 22/22 passed; no device loss | 50.51 s |

That is six complete sets (78 raymarch/capture-producer GPU invocations) with **0/78 device losses**. An earlier `-j16` retry stalled below the shared queue's disk floor; cancellation admitted it and interrupted it at 11/22. After external disk cleanup, the queued retry above passed. The incomplete attempt is excluded from the reproduction rate.

The prior isolated capture fault is explained by in-flight resource cleanup, and the post-fix lock-disabled stress did not reproduce device loss. The stress therefore does not establish a separate loss mechanism. For the requested categories, there is no observed loss from which to attribute resource/descriptor exhaustion (a), a TDR (b), remaining shared-path interference (c), or a Dozen/driver defect (d). The editor fixture did have a tracked-file side effect; staging its input removes that test-path hazard, but the side effect was not evidence of GPU loss. No memory-cap or shorter-dispatch comparison was meaningful after the suspected lifetime fault was fixed and the loss stopped reproducing.

No loss occurred, so there was no `VK_EXT_device_fault` payload or Dozen `GetDeviceRemovedReason` record to capture. `nvidia-smi` and `powershell.exe` were unavailable in this WSL environment, so dedicated/shared GPU memory and Windows event-log data were unavailable as well. There is no evidence here to raise the machine TDR limit; keep the shared GPU lock.

## Build and suite witnesses

The queued scoped builds passed for `RenderGraphCore`, `RenderGraphNodes`, `VIXEN`, `vixen_editor`, `test_rendergraph_criticalnodes_windowedcapture`, and the aggregate `rendergraph_svo_tests` target. The CMake reconfigure also generated the editor test command with its staged document and retained `RESOURCE_LOCK "vixen_gpu_device"`.

The full source-lock-enabled `ctest --test-dir build/gpudevloss --output-on-failure -j8 -L "RenderGraph|SVO"` run selected 2,108 tests and finished in 332.73 seconds: 2,086 passed, two failed, 19 skipped, and one disabled. It had two failures:

- `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` consistently rejected opcode 94 with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`.
- `LODRayCastingTest.NoPerformanceRegressionWithoutLOD` failed its timing threshold under the parallel run (`regular=0.167641 ms`, `LOD=1.042451 ms`). It passed in the serial full run, so this is contention-sensitive timing evidence rather than a reproduced device-loss or functional regression.

The subsequent source-lock-enabled serial run selected the same 2,108 tests and finished in 366.42 seconds: **2,087 passed, one failed, 19 skipped, and one disabled; the LOD timing test passed**. The sole failure was the same opcode-94 parity diagnostic. This matches the known T-1449 opcode-94 symptom in gpulock's report, but that report marked its provenance against base as unclassified; this lane does not claim it is proven pre-existing. Full run logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791554807-test-gpudevloss:full-rendergraph-svo-parallel.log` and `/home/liory/.local/state/undertow/undertow-box-logs/1791555245-test-gpudevloss:full-rendergraph-svo-serial.log`.

## Workload and recovery notes

No other CTest/editor GPU process was visible when the scratch stress or full parallel suite began. A `wavebump` VIXEN clean Release build was active during the work; it was compilation, not a GPU test. The shared queue also admitted unrelated Undertow/Yeroket work while the serial run waited and ran, but no concurrent GPU process was identified. Earlier VIXEN test workloads from other lanes had ended before these witnesses.

Two scoped-build invocation problems were recovered: `RenderGraph` is an `INTERFACE` target, so the first target build had no generated rule; target help identified `RenderGraphCore` and `RenderGraphNodes`. A fresh asset-staging build also found a missing stamp parent; creating the build-tree stamp directory allowed the queued target build to pass. The low-disk queue stall is described above. These are tracked as consolidation proposals below.

## CONSOLIDATION ISSUES

- RenderGraph INTERFACE target is not directly buildable
- VIXEN asset staging must create the stamp parent on fresh builds
- Editor capture save/reopen must use a build-tree document
- Box queue should recover from low-disk admission stalls
