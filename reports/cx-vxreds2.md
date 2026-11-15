# cx-vxreds2 — VIXEN base-red investigation

Status: **STOPPED on a new unregistered full-GPU red; assigned rows are not reproducible on the current base.**

Base witnessed: VIXEN `002a5f6b6adf53644e53ef947c16538440497411` (`lane-vxreds2`, already at `origin/wave/authoring-convergence`). The configured VIXEN kernel pin was `597757236569835a7b40ed01ec04c06e286af723`. CodeGraph was attempted first, but this checkout has no CodeGraph index, so normal repository inspection was used.

## Scope and result

No VIXEN product or test source was changed. The current tree already passes the assigned row-60 checks and the direct Dozen row-73 witness. The old row-60 failure used VIXEN `3fb71ea3a` with kernel `ba0c30fc6`; the relevant VIXEN merge/emission sources and tests have no diff from that VIXEN revision, while the current kernel pin is different. This is therefore a stale paired-base failure, not a current product/test regression.

### Row 60 — `SdiVariantMerge`

- Fresh target build passed: `build/wsl-debug/libraries/ShaderManagement/tests/test_sdi_variant_merge`.
- The three registered tests passed individually **3/3 each**, with `--no-replay`:
  `PushConstantMemberGating`, `PushMemberOffsetConflictFails`, and `MergedEmissionUsesTypedIntegerVectorsInPushBlock`.
- Focused CTest passed `3/3`; the complete `SdiVariantMerge` suite passed `17/17`.
- Fresh full CPU CTest passed `1738/1738` executed tests (`22` skipped/disabled), including all three registered row-60 tests. Evidence: `/home/liory/.local/state/undertow/undertow-box-logs/1794689246-test-cx-vxreds2:base-full-cpu.log`.

Conclusion: no row-60 implementation or assertion change is justified on this base; the registered failure is not reproducible after the current kernel pairing.

### Row 73 — `HeadlessStarfieldGraph.CompareIndependentCaptures`

The current test and CMake files have no source delta from the landed failing wave `dc324d0f5`. Under the current WSL Dozen configuration, the three CTest entries are intentionally `Disabled`, so direct queued Dozen execution was used for the product witness. The device was `Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics)` through the configured Dozen ICD.

Three independent rounds were run with distinct capture prefixes and `--no-replay`; each round had separate queued GPU jobs for capture 1, capture 2, and comparison. All 9 jobs passed. The comparison asserted exact off bytes, exact on RGB pixels, stable wall pixels, and the constrained changed-pixel count. Every off capture had SHA-256 `97d4b03089c9fff7c15cc21b6094cc43a78f5ad28cbb59891634a90bc077e4b2`; every on capture had SHA-256 `841052cbf8e99f324bd2df9d3667eda47bfa9bc02572cea98d9f7728a3b39fb0`.

Conclusion: row 73 is not reproducible on the current Dozen state. No comparison was loosened and no R796 was added because the direct comparison is deterministic and green. The registered 3/3 failure remains an environmental/stale-state finding from the prior wave, not a current test or renderer failure.

## Recovery ladder

### A — invocation/provisioning

The documented global queue path was used for every build/test/capture. The first queued full CPU build failed in the live wrapper after `566/727` actions because `/home/liory/Github/box-queue/queue/with-test-lock.sh` invoked the pinned `box-queue-core hostcpu` parser with `--describe true`; the parser accepts `--describe` as a flag and rejected the trailing `true` (exit 70). Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794688638-build-cx-vxreds2:build-base-full-cpu.log`.

The live wrapper/core paths and parser contract were inspected. The optional consumer-registry check was unavailable because the registry is not present on this host. Recovery used the supported Linux host-CPU path, still through the live queue: `UT_QUEUE_HOST_CPU_WSL=0` plus the same build command. The retry completed all `151` remaining actions and the full CPU build passed. This queue contract hole is proposed below.

VIXEN configure/build also required the documented worktree-local `VIXEN_FETCHCONTENT_CACHE`; the same provisioning gap is already covered by existing VIXEN SPT proposals, so no duplicate proposal was filed.

### B — regeneration

No tracked generated source was stale or missing. A fresh CMake configure completed, selected the current tracked kernel pin, and the target/full build completed after the queue recovery. No generated source diff was introduced.

### C — isolation

The assigned row-60 target scope passes fresh build, focused tests, full `SdiVariantMerge`, and full CPU CTest. The assigned row-73 scope passes direct Dozen capture/compare 3/3; the corresponding plain CTest entries are disabled by the current translated-Dozen policy. The only remaining required-gate failure is the separate full-GPU TextShaping red below.

## REDS

### New unregistered required-gate red — STOP

Queued full GPU CTest selected `1627` tests and executed `1502`; five `TextShapingFixture` tests aborted in every occurrence at the HarfBuzz assertion `hb-buffer.hh:578` (`content_type == HB_BUFFER_CONTENT_TYPE_UNICODE`). The exact failures were:

- `LatinGlyphIdsAndByteClustersAreStable`
- `WorldTextPositionsShapedGlyphsAndCarriesRtlMirror`
- `ArabicGlyphIdsAndByteClustersAreStable`
- `RmlFontEngineUsesAndClearsPresetShaping`
- `RtlVisualRunOrderIsPreservedBeforeShaping`

Full-GPU log: `/home/liory/.local/state/undertow/undertow-box-logs/1794689382-gpu-cx-vxreds2:base-full-gpu.log`. A queued targeted isolation reproduced the same five aborts (`exit=8`): `/home/liory/.local/state/undertow/undertow-box-logs/1794689686-gpu-cx-vxreds2:base-text-shaping-isolation.log`.

The common call site is `VIXEN/libraries/Text/src/Shaper.cpp:112-113`, where `hb_buffer_add` is used before `hb_shape`; the fetched HarfBuzz 14.5 implementation does not transition the buffer content type in that function, matching the assertion. This is outside rows 60/73 and was not changed. Per lane policy, the unregistered full-GPU red blocks a clean final witness. Owner action options: register/dispatch the TextShaping fix, or explicitly authorize a scoped exclusion after a paired baseline establishes it as pre-existing.

## LANDABLE NOW

The independently useful report and queue consolidation proposal are ready to land. No product fix SHA exists because neither assigned base red reproduced on the current tree.

## SPT DISPOSITION

None — no SPT task id was worked.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Queue host-CPU fallback rejects the boolean describe argument
