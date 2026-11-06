# cx-tu01 — VIXEN application-driver split

Base: `44a9cd876de0cb674e79f20e76e44550220b6957`, including the required fast-forward of `origin/lane-vixrebuild1`. The branch remains `lane-tu01`; no push was performed.

## LANDABLE NOW

Yes. `VulkanGraphApplication.cpp` was split without changing method bodies or runtime contracts:

- lifecycle, preparation, render-graph compilation, and shutdown request handling moved to `VulkanGraphApplicationLifecycle.cpp`;
- teardown moved to `VulkanGraphApplicationTeardown.cpp`;
- readback and frame-capture services moved to `VulkanGraphApplicationCapture.cpp`;
- recipe dispatch/registration and shader-promotion services moved to `VulkanGraphApplicationRecipes.cpp`;
- the four units were added to `VIXEN/application/main/CMakeLists.txt`.

The main driver is 2,988 lines after the split, versus 5,093 lines at the baseline. No shared header or generated contract was changed.

## BEFORE/AFTER

All measurements used the dedicated `build-tu01` tree, GCC Release, `ninja -j4`, and the live box queue.

| Measure | Before | After |
| --- | ---: | ---: |
| Main driver lines | 5,093 | 2,988 |
| Main-driver preprocessed bytes | 14,979,010 | 14,047,048 |
| Main-driver include edges | 675 | 612 |
| Clean `VixenApp` wall | 774.85 s | 77.91 s* |

The after preprocessed size is 931,962 bytes smaller (6.22%); include edges are down by 63 (9.33%). The clean after build used the warmed compiler/codegen cache from the baseline, so its wall-time delta is not attributable solely to the TU split; the exact wall times are retained for reproducibility.

The uncached compile records captured during the split are:

| Translation unit | Compile seconds |
| --- | ---: |
| `VulkanGraphApplication.cpp` | 47.646 |
| `VulkanGraphApplicationLifecycle.cpp` | 18.577 |
| `VulkanGraphApplicationTeardown.cpp` | 15.304 |
| `VulkanGraphApplicationCapture.cpp` | 27.634 |
| `VulkanGraphApplicationRecipes.cpp` | 62.348 |

The capture-only incremental witness rebuilt `VulkanGraphApplicationCapture.cpp.o` and the library, with no lifecycle object rebuild. Its log also records the incidental codegen freshness issue described below.

Evidence: `/home/liory/codeman-cases/undertow/reports/cx-tu01-evidence/` (`baseline/`, `final/`, `after-full-build.log`, `isolation-capture-actual.log`).

## WITNESS

- `VixenApp` clean baseline: pass, `baseline/ninja-build.log`.
- `VixenApp` final split build: pass, `after-full-build.log`.
- Codegen/golden checks: pass during the final build; merged SDI headers remained up to date.
- App tick witness: 6/6 passed in `app-tick-witness.log`.
- GPU render witness: `HeadlessCornellGraph.ProductionGraphRendersDeterministicCornellWithStableSharedWallSeams`, 1/1 passed in `headless-cornell-witness.log`, using the provisioned Dozen ICD.
- The CMake configure reported the existing 67-node manifest; no node registration or generated node manifest was edited.
- CodeGraph was attempted first, but this checkout has no `.codegraph` index; source inspection then used the repository files directly.

The exact supplied world/save native digest pair was not independently rerun in this lane; the targeted CPU app witness and GPU production Cornell witness passed, and the split contains no render or capture logic changes.

## STOPs

None. No required check failed after implementation. The full repository suite was not rerun in this lane; the landing/batch witness owns that full run.

## SPT DISPOSITION

No SPT task id was assigned in the brief. The two incidental tooling holes are recorded in the consolidation inbox and committed with this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN needs a declared per-TU preprocessing and timing witness
- proposed: VIXEN incremental app builds rerun stale codegen from provider timestamps
