# rgderive run 2 — derive render-target contracts

## Scope and result

Worktree: `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/rgderive`; branch `lane-rgderive`.
Run-2 base: `bc2b34dd4d73b8e86e1f521e947f91260ebdb702`. Started clean at 2026-10-01 14:08:18 UTC.
T-1507's run-1 implementation (`6bb63b128cab268780e9132476e364c73ce79c6f`) stands. Its earlier report is preserved at `bc2b34dd:reports/rgderive.md`.
The controller's run-2 scope expansion resolves the three earlier edit-scope referrals. T-1468, T-1506 and T-1508 are completed in `c02173826757b5c64422cc93a8810e006862dd4d`; T-1507 remains complete.
All source, temporary witnesses and report changes are inside this worktree. No branch switch, push, Undertow/kernel/KFR source edit, or edit to codegen recipes, AppFlow or CMake infrastructure.

Read all four SPT tasks and the read-only register rulings R316, R319, P579 and P596.
CodeGraph FIRST: `codegraph explore RenderTargetReadback` exited 1 because the worktree has no index; its diagnostic permits ordinary discovery and prohibits agent indexing. Used `rg` after that diagnostic. The run-1 CodeGraph proposal already records this hole.

Fetched and ran `git merge --no-edit origin/wave/authoring-convergence` before the final witness. It reports `Already up to date`: wave tip `6a116291224d0d77a669a0968b32d09cbaae24ba` is already an ancestor of this lane. No merge conflict or source delta.

## Tasks

### T-1468 — readback derives the last graph-owned layout

Each physical `RenderTargetBuffer` and `SwapChainBuffer` now owns its recorded layout through `IRenderTarget`. Recreated buffers start UNDEFINED. Deleted the three private per-node image-layout maps in ComputeDispatchNode, ComputeStageNode and BlitNode; their barriers now consult the shared physical buffer's history. Blit records its actual GENERAL/PRESENT boundary rather than predicting a downstream UI transition.

RenderPassNode registers its compiled final layout in the graph. Geometry, UI, sky projection, proxy raster and grouped render-pass execution publish that final layout to the target. Initialization and ray-tracing transitions also record their actual layouts. Registration occurs during the existing serial compile phase; no new mutex was introduced.

The sole public PNG helper derives `GetCurrentLayout()`, rejects an image with no graph-produced contents, and restores the derived layout after its blocking transfer. Deleted the caller-supplied layout argument and the duplicate swapchain-specific readback implementation. Migrated editor and main application callers. Existing BGRA conversion remains in the shared helper.

Evidence: two new layout-history regressions cover allocation recreation and compiled render-pass registration/clear. The production headless UI witness renders three frames and captures each twice, checking that capture preserves its TRANSFER_SRC layout. Cornell still renders five frames and captures the same image twice with identical decoded bytes and stable shared-wall seams. Window HUD capture exercises the same public helper on a WSI target.

### T-1506 — target allocation derives usage from consumers

Deleted `RenderTargetNodeConfig::PARAM_USAGE`, its setup reader and all three application declarations, including the conditional proxy-attachment mask. Target compile derives the union of direct image/view consumers, descriptor binding types and explicitly forwarded image-array consumers, plus the public readback TRANSFER_SRC requirement. An extent-only dependency does not forward image provenance. Static input indices and variadic descriptor bindings are kept distinct in GraphEdge.

Consumer creation requirements supplement existing AccessKind metadata where a consumer lacks a scheduling access declaration: framebuffer and geometry attachment, blit destination and ray-tracing storage. These requirements do not alter existing submission scheduling or declare an internal blit layout as a pass boundary. Actual usage reporting and negotiated swapchain storage capability checks remain the authority for descriptor support.

Target images persist when extent and derived usage are unchanged; either change recreates them. A transfer-only allocation creates no illegal Vulkan image view. Image forwarding is available before allocation; its currently handwritten gatherer override is an SPT consolidation proposal.

Evidence: six new usage regressions cover unioning storage/blit/attachment requirements, descriptor requirements changing from sampled to storage, image-array forwarding, isolation of extent-only dependencies, transfer-destination usage and static/variadic index separation. Removed the obsolete PARAM_USAGE test. Headless UI asserts the actual allocation is precisely COLOR_ATTACHMENT | TRANSFER_SRC; Cornell asserts its derived storage and transfer-destination requirements.

### T-1507 — optional slots

Already completed in run 1. Shared ValidateInput honors SlotType::nullable; InputNode uses it and the replaced GetOptionalInput form is deleted. All four run-1 regressions are retained in the run-2 full witness.

### T-1508 — synchronization belongs to the target contract

Window and offscreen target types declare WsiAcquirePresent or Offscreen once, shared by runtime IRenderTarget and their preallocation node contracts. ConnectionBatch consumes target ports through the existing connection rules. Deleted SdiStageCommon's `usesOffscreenTarget` and `hasWsiSemaphores` booleans and migrated every aggregate caller. Main and UI graph device presentation setup, target-port wiring, semaphore wiring and presentation layouts follow the selected target contract.

The common handoff resolver returns no binary WSI handoffs for an offscreen target, even if arrays were provided. A window role requiring acquire or present rejects missing, null or out-of-range handles. Producer/consumer ownership remains distinct from whether a target supports WSI. Compute, blit, geometry, ray tracing, grouped passes, sky and UI submissions use this contract. Geometry, TraceRays and PassGroup semaphore ports become optional; the resolver enforces the window-only requirement. UI creates composite present semaphores only for WSI targets. Fence and timeline ownership remains in the existing roles.

Evidence: six new synchronization regressions cover offscreen targets with and without arrays, frame-indexed acquire versus image-indexed present, absent/null/out-of-range window handles, producer roles needing neither handoff, and agreement between node wiring and runtime capability. Fresh window HUD and offscreen editor/headless producers exercise both production target classes.

## Required verification and baseline

Required scope: all 22 RenderGraph test executables and their native/generated dependencies; `test_headless_cornell_graph`; affected `test_headless_ui_graph`; VIXEN and vixen_editor; all 22 configured `*_check` targets. Serial RenderGraph tests require fresh HUD/editor capture fixtures. No required test or check is excluded.

Every configure/build/test runs through the real `tools/with-test-lock.sh` entry point, stable agent `rgderive`. Native commands use `nice -n 10`, at most `-j4`, and `VIXEN_FETCHCONTENT_CACHE=<worktree>/.tmp/fetch`. GPU witnesses run under `bash -lc` with `VK_ICD_FILENAMES=$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json`.
Queue logs below live in `/home/liory/.local/state/undertow/undertow-box-logs/`.

Before semantic edits:

- Configure from `VIXEN/`: `bash ../tools/with-test-lock.sh --agent rgderive --resource build --label rgderive:r2-baseline-configure -- env VIXEN_FETCHCONTENT_CACHE=<worktree>/.tmp/fetch nice -n 10 cmake --preset vixen-wsl -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`; exit 0. Log `1790863775-build-rgderive:r2-baseline-configure.log`.
- Fresh build: queued label `rgderive:r2-baseline-build`, payload `nice -n 10 cmake --build ../build/wsl -j4 --target <all test_rendergraph_* and *_check targets> test_headless_cornell_graph test_headless_ui_graph VIXEN -- -k 0`, then `nice -n 10 cmake --build ../build/wsl -j4 --target vixen_editor`; exit 0. Editor builds sequentially to avoid application asset staging overlap. Log `1790864064-build-rgderive:r2-baseline-build.log`. Target inventory is `.tmp/rgderive/run2/targets.txt`.
- All 22 generation/check targets passed using the freshly built pinned CodegenTool. Catalogue SHA-256 `85d375534c072010ec1c50ffabc4911c5cd4de689c2ea6ceb536cec7b3eac8fd`; kernel codegen pin `769fb232bd861a7e57fe73fd12b0aed716c3adde`. No generated source changes or hand-edited goldens/pins.
- First red: unchanged base `bc2b34dd`, queued label `rgderive:r2-baseline-captures-serial`, command `bash tools/with-test-lock.sh --agent rgderive --resource test --label rgderive:r2-baseline-captures-serial -- bash -lc 'exec bash <worktree>/.tmp/rgderive/run2/witness.sh before'`. The runner incorrectly used `VIXEN_EDITOR_EXIT_AFTER_FRAMES=120`, but the editor reads `VIXEN_EXIT_AFTER_FRAMES`. Captures were written but the producer did not exit; queue idle-hard termination after 184 seconds, wrapper exit 75, payload 143. Log `1790864259-test-rgderive:r2-baseline-captures-serial.log`.
- Recovery A: inspected the editor's runner implementation, fixed the frame-limit variable to `VIXEN_EXIT_AFTER_FRAMES=120` and kept serial CTest output visible to the queue. Repeated queued label `rgderive:r2-baseline-captures-serial-fixed`; exit 0. Log `1790864630-test-rgderive:r2-baseline-captures-serial-fixed.log`. Recovery B: not applicable, binaries and generation checks were already fresh/green. Recovery C: unnecessary after invocation recovery; no remaining product failure needed isolation.
- Recovered serial baseline: 994 selected, 989 passed, five skipped, zero failures, 86.33 seconds. Before-failing set: empty. JUnit and capture artifacts: `.tmp/rgderive/run2/before/`. The brief's original 988 RenderGraph cases plus four run-1 regressions and two affected headless app cases explain the selected count.

Witness runner inputs, identical before/after:

| Producer | Executable under `<worktree>/build/wsl/binaries/` | Environment |
| --- | --- | --- |
| HUD | VIXEN | DISPLAY=:0; VIXEN_HUD_SCRIPT=A@30,B@60; VIXEN_HUD_CAPTURE_FRAMES=5,45,75; VIXEN_EXIT_AFTER_FRAMES=85 |
| Editor | vixen_editor | VIXEN_EDITOR_OFFSCREEN_CAPTURE=1; VIXEN_EDITOR_TEST_CAMERA=top-down; VIXEN_EDITOR_SCRIPT=toggle:2@30,undo@60,redo@90,settings@100,back@110; VIXEN_EDITOR_CAPTURE_FRAMES=5,45,75,105; VIXEN_EXIT_AFTER_FRAMES=120 |

Both use timeout 180, `VIXEN/` as the asset directory, absolute per-phase capture directories and producer logs `run_hud.log` / `run_editor_script.log`. Serial selection comes from fresh `ctest --show-only=json-v1`, matching test command owners to every `test_rendergraph_*` executable and both headless apps. Command: `ctest --test-dir <worktree>/build/wsl --tests-from-file <worktree>/.tmp/rgderive/run2/<phase>/tests.txt --output-on-failure --output-junit <worktree>/.tmp/rgderive/run2/<phase>/results.xml -j1`.

## Implementation checks and final witness

Initial implementation build `rgderive:r2-contract-build` exited 1 on this lane's two missing complete RenderGraph includes and SkyProjection's call to a private typed raw-input accessor. Log `1790866003-build-rgderive:r2-contract-build.log`. Added the includes and changed the presence query. The next build `rgderive:r2-contract-build-fixed` exited 1 because the typed convenience input-count helper is also private; log `1790866848-build-rgderive:r2-contract-build-fixed.log`. Corrected it to the public qualified NodeInstance input-count method. These are introduced compile failures, fixed in this lane; they are not environmental or pre-existing baseline reds. Neither failed build supplies test evidence. Both completed the unchanged generated checks successfully.

Final configure/build on the merged tree: queued label `rgderive:r2-final-build`, same worktree-local FetchContent root and catalogue. Payload from `VIXEN/`: the exact preset configure above, then the full native build/target inventory above, then the sequential editor build. Exit 0. Log `1790867468-build-rgderive:r2-final-build.log`. All 22 RenderGraph executables, both headless app witnesses, VIXEN, vixen_editor and all 22 configured check targets passed. The pinned CodegenTool built successfully with zero warnings/errors, every generated comparison passed, and the catalogue digest stayed unchanged. No generated drift or regeneration workaround.

Final GPU command: `bash tools/with-test-lock.sh --agent rgderive --resource test --label rgderive:r2-final-captures-serial -- bash -lc 'exec bash /home/liory/projects/VBVS--VIXEN/.claude-worktrees/rgderive/.tmp/rgderive/run2/witness.sh after'`; exit 0. Both capture producers completed before the serial CTest run. Log `1790867893-test-rgderive:r2-final-captures-serial.log`. Artifacts: `.tmp/rgderive/run2/after/results.xml`, `inventory.json`, `tests.txt`, `hud/`, `editor/`, and `.tmp/rgderive/run2/capture-comparison.json`.

| Witness | Selected | Passed | Skipped | Failing set |
| --- | ---: | ---: | ---: | --- |
| Fresh run-2 base | 994 | 989 | 5 | empty |
| Fresh final merged tree | 1007 | 1002 | 5 | empty |

Final serial time: 102.32 seconds. The count grows by 14 new regressions minus one obsolete PARAM_USAGE case. All four run-1 optional-slot regressions pass. The five skips are unchanged: GPUQueryManagerIntegration.Placeholder; PushConstantGathererNodeTest.RuntimeFieldDiscovery; ValidateFieldTypes; HandleNullShaderBundle; HandleEmptyPushConstantMembers. No test failure is hidden by a changed selection or a stale executable.

All three HUD PNGs and all four editor PNGs are byte-identical before/after. Standard-library PNG decoding also finds zero changed RGB pixels in every pair. Editor edit/undo/redo state and the existing capture pixel assertions all pass. Production headless UI passes with repeated capture across three frames; production Cornell passes its two identical readbacks and stable shared-wall seams. No capture goldens were rewritten.

Two final header comment edits document usage-triggered recreation and consumer creation requirements; they do not change compiled behavior. No semantic source changes were made after the successful native build. `git diff --check` validates those documentation edits with the complete product diff.


## Findings and remaining work

The unchanged baseline capture producers log `shadow_visibility_wave_desc_gatherer` bindings 40/43 beyond resourceArray size 36 and the missing vkCreateDebugReportCallbackEXT diagnostic. They finish successfully and all required capture assertions pass. These are existing diagnostics on the recorded base, with no required gate excluded. CMake also prints the older glslang-directory diagnostic while finding SDK libraries and successfully configuring. The no-new-mutex check reports the existing two unclassified KernelDispatch inventory rows while passing its gate.

No STOP or unresolved design referral. No remaining requested item. This run finishes before the three-hour checkpoint. The exact stopping point is completed implementation plus the full merged configure/build/check/capture/serial witness and committed report.

Read-only KFR source discovery found no callers of the retired readback/target-usage/stage-flag APIs or local IRenderTarget subclasses. No KFR edit was needed. All incidental work is recorded in the SPT inbox; its 12 entries (nine retained and three new) parse as JSON. Restored this lane's runtime-only `VIXEN/cache/global/manifest.txt` churn after captures. Implementation and proposals are committed in `c02173826757b5c64422cc93a8810e006862dd4d`; this report is committed separately. Final repository status is checked after the report commit. No push.


## CONSOLIDATION ISSUES

New proposals in this run; committed with the existing run-1 inbox entries:

- proposed: Provide one queued capture witness entry point with the shared frame-limit knob
- proposed: Declare image forwarding in node configs before resource allocation
- proposed: Derive target creation requirements without duplicating missing GPU access metadata

Run-1 proposals retained:

- proposed: Expose the real box queue to isolated VIXEN worktrees
- proposed: Provide CodeGraph discovery for isolated VIXEN worktrees
- proposed: Default VIXEN FetchContent to the active worktree
- proposed: Label RenderGraph CTest cases by their owning suite
- proposed: Declare the WSL capture runner display environment
- proposed: Run VIXEN capture producers as CTest fixtures with their logs
- proposed: Resolve capture executables from CMake's runtime output directory
- proposed: Keep runtime cache manifests out of tracked VIXEN source
- proposed: Provide a native before/after capture pixel comparison command
