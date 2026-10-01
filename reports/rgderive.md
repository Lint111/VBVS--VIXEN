# rgderive — RenderGraph derived contracts

## Scope and starting state

Lane: `rgderive`, branch `lane-rgderive`. Starting SHA: `e0cf083a476e5f0b2df1c36d5a69bebef8e39dbd`.
The worktree started clean. All edits are in this worktree. No branch was created or switched and nothing was pushed.

Read T-1468, T-1506, T-1507 and T-1508 through SPT, and read R316, R319, P579 and P596 in the read-only Undertow register.
`codegraph explore RenderGraph` exited 1 because this worktree has no index; its diagnostic explicitly instructs agents to use normal discovery and not create an index. Discovery therefore used `rg` and file reads.

Required verification scope: all 22 `test_rendergraph_*` targets, their native dependencies and generated contracts, `test_headless_cornell_graph`, VIXEN and vixen_editor, and every configured `*_check` target. Also exercise the public offscreen UI path as an affected InputNode consumer. Run the RenderGraph tests serially after fresh HUD and editor capture producers.

Every configure/build/test uses the real box queue with stable agent `rgderive`. The baseline used `/home/liory/.local/bin/with-test-lock.sh`, the documented symlink to Undertow's wrapper. Before the final witness, this lane fetched and merged `origin/wave/authoring-convergence`, fast-forwarding to `6a116291224d0d77a669a0968b32d09cbaae24ba`. That wave delta only adds README queue documentation and `tools/with-test-lock.sh`; product source is unchanged. The final witness uses that local entry point, which discovers and execs the same real Undertow queue. Native builds use `nice -n 10` and at most `-j4`. No raw build/test command is used.

## Tasks

### T-1468 — derive readback layout

Source finding: `VIXEN/libraries/RenderGraph/include/Debug/RenderTargetReadback.h` accepts a public `VkImageLayout currentLayout` argument, defaults it to TRANSFER_SRC_OPTIMAL, and restores that supplied layout after readback. EditorApplication passes GENERAL explicitly. ComputeDispatchNode, ComputeStageNode and BlitNode each keep private image-layout maps, while render passes can choose their final layout separately. Inferring one default from the target class would not cover those paths.

The task authorizes a graph render-target readback API as an alternative to deriving layout inside the helper. The implementation must retain the actual final graph-owned layout per image, including render-pass final layouts, and use that state for a blocking copy and restoration. R319 requires deleting the old layout argument and migrating its caller at `VIXEN/application/editor/source/EditorApplication.cpp:506`.

Status: UNFINISHED — REFER-TO-ORCHESTRATOR for edit scope, as described below. Source audit completed; no implementation was made for this item.

### T-1506 — derive image usage from consumers

Source finding: `RenderTargetNodeConfig::PARAM_USAGE` and RenderTargetNode's setup reader own the mask. Three application graph-setup calls still declare it: `VIXEN/application/main/source/graph/BuildUIGraph.cpp:79` and `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:1394,1427`. The consumer slots already expose AccessKind for several storage, attachment and transfer accesses. Descriptor gatherers, image arrays and forwarded target outputs must also contribute; graph reachability alone would overcount unrelated resources.

R319 requires deleting PARAM_USAGE, its reader and those three calls. The target must allocate the union of its actual consumers' requirements and its public readback capability, then recreate images if that derived contract changes on recompile.

Keep requested usage distinct from actual usage. IRenderTarget::GetImageUsageFlags reports the actual mask; swapchain negotiation can drop STORAGE for an unsupported device/format. Deriving the request must preserve SupportsStorageImage and descriptor capability checks against that actual mask.

Status: UNFINISHED — REFER-TO-ORCHESTRATOR for edit scope, as described below. Source audit completed; no implementation was made for this item.

### T-1507 — honor optional input metadata

Source finding: WINDOW is already Optional in InputNodeConfig. ValidateInput rejects null without consulting that metadata. InputNode bypasses it through GetOptionalInput, which has exactly one code caller in this checkout.

Implemented: ValidateInput derives its null check from `SlotType::nullable`. Required inputs still throw the same descriptive error. InputNode now uses that shared validator for WINDOW. Deleted GetOptionalInput and its sole caller rather than retaining a compatibility path (R319).

Four regressions in the existing typed-helper suite cover an unconnected optional slot, the exact required-slot diagnostic, preservation of a connected pointer for either nullability, and production InputNode compilation without a window. The opaque connected pointer is only stored and compared; the test does not call GLFW with it.

Before witness: added and freshly built these tests against the original production validator. The first fixture build exposed an incorrect explicit forwarding-reference call to `Resource::SetHandle`; fixed the fixture to let the pointer type be deduced and rebuilt successfully. This was this lane's test-code error, not an environmental red. Logs: `1790860032-build-rgderive:nullability-regression-build.log` (failed), `1790860316-build-rgderive:nullability-regression-build-fixed.log` (passed).

The queued focused before run used that fresh binary, whose SHA-256 remained `84e015400e8ef7f2a1cc44da5414a20d11c178ed93a895608b5978b99653aa5c` after the production source edit; no rebuild occurred before that run. `test_rendergraph_core --gtest_filter=TypedInputValidation.*` exited 1: OptionalWindowMayBeUnconnected threw `Required input 'Window' is null`; the other three regressions passed. Log: `1790861014-test-rgderive:nullability-regression-before.log`. The full unchanged-source baseline had already passed before adding these tests.

After witness: complete. All four regressions pass in the merged-tree serial suite below. Implementation commit: `6bb63b128cab268780e9132476e364c73ce79c6f`.

### T-1508 — target synchronization capability

Source finding: SdiStageCommon duplicates target identity into `usesOffscreenTarget` and `hasWsiSemaphores`. `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` supplies both booleans at three aggregate-initialization sites, lines 7822, 7970 and 9016. Execution also infers WSI from semaphore-array presence in ComputeStageNode, ComputeDispatchNode, BlitNode and UIRenderNode. IRenderTarget exposes image indices and usage, but no synchronization capability. The existing shared interface and RenderTargetData are in `VIXEN/libraries/VulkanResources/include/IRenderTarget.h`, also outside the assigned edit scope.

The replacement must declare acquire/present synchronization once on the window/offscreen target contract and derive wiring and submission behavior from it. A missing semaphore on a window target must not be interpreted as an offscreen capability. R319 requires deleting both SdiStageCommon booleans and migrating the three application initializers.

Status: UNFINISHED — REFER-TO-ORCHESTRATOR for edit scope, as described below. Source audit completed; no implementation was made for this item.

## Baseline and recovery

No semantic source changes were made before these checks.

- Configure: from `VIXEN/`, queued `env VIXEN_FETCHCONTENT_CACHE=<worktree>/.tmp/fetch nice -n 10 cmake --preset vixen-wsl -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`; exit 0. Worktree-local SDK/windowing provisioning completed. CMake printed the older “Fatal Error: glslang directory not found” diagnostic but found the SDK libraries and completed configuration. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790856909-build-rgderive:baseline-configure.log`.
- Generation/check contract: queued `nice -n 10 cmake --build build/wsl -j4 --target <all 22 configured *_check targets> -- -k 0`; exit 0. The pinned kernel CodegenTool was freshly built, then every check passed. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790857132-build-rgderive:baseline-checks.log`.
- Catalogue SHA-256: `85d375534c072010ec1c50ffabc4911c5cd4de689c2ea6ceb536cec7b3eac8fd`. Kernel codegen pin: `769fb232bd861a7e57fe73fd12b0aed716c3adde`.
- The earlier T-1491/ViewNounId stale finding does not reproduce here: view_noun_enum_check passed. No generated source changes were required.
- Fresh native build: all 22 `test_rendergraph_*` targets plus `test_headless_cornell_graph` passed (923 build steps); VIXEN plus `test_headless_ui_graph` passed; vixen_editor passed. Logs are `1790857382-build-rgderive:baseline-rendergraph-build.log`, `1790858128-build-rgderive:baseline-main-build.log` and `1790858278-build-rgderive:baseline-editor-build.log` in the same queue log directory above.
- Fresh HUD producer: queued from `VIXEN/` under `bash -lc`, with `VK_ICD_FILENAMES=$HOME/.cache/vixen/wsl-vulkan/dzn_icd.json`, `DISPLAY=:0`, `VIXEN_HUD_SCRIPT=A@30,B@60`, frames `5,45,75`, absolute capture directory `<worktree>/.tmp/rgderive/baseline/hud`, `VIXEN_EXIT_AFTER_FRAMES=85` and `timeout 180 ./binaries/VIXEN`. Exit 0; all three PNGs were written. The subsequent full baseline suite passed the capture assertions.
- First red, before semantic edits: the editor producer used the same source-side executable convention, `timeout 180 ./binaries/vixen_editor`, from `VIXEN/`. Exit 127: `timeout: failed to execute process: No such file or directory (os error 2)`. Base remained `e0cf083a`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790858687-test-rgderive:baseline-editor-capture.log`.
- Recovery A: checked the editor CMake runtime-output properties and both directories. The fresh executable exists at `<worktree>/build/wsl/binaries/vixen_editor`, whereas the main executable is also staged into `VIXEN/binaries/`. Reran through the real queue with that absolute CMake-built editor path, retaining `VIXEN/` as the asset working directory. The recovered producer redirects stdout/stderr to the documented `run_editor_script.log` in its capture directory. Exit 0; four fresh PNGs and the log were written. The log records masks 7 -> 3 -> 7 -> 3, correct undo/redo stack depths and afterBack=0. PNG file hashes match for frames 5/75 and 45/105 and differ between the edit states. Runner inputs: offscreen capture=1, camera=top-down, script `toggle:2@30,undo@60,redo@90,settings@100,back@110`, captures `5,45,75,105`, exit after 120 frames. Queue log: `1790858904-test-rgderive:baseline-editor-capture-recovered.log`.
- Recovery B for that red: not applicable; the executable was already freshly built and all generation checks passed. Recovery C: not needed once the invocation is corrected; exit 127 is not a test-target or product failure.
- The queued `ctest --show-only=json-v1` inventory succeeded. Selecting test names by owning command path yields exactly 988 cases from all 22 RenderGraph executables plus the Cornell and offscreen UI cases: 990 cases in 24 executables. The serial command uses `--tests-from-file <worktree>/.tmp/rgderive/baseline/tests.txt --output-on-failure --output-junit <worktree>/.tmp/rgderive/baseline/results.xml -j1`, with absolute fresh HUD/editor capture directories and the Dozen ICD under `bash -lc`. Exit 0: 990 selected CTest entries, 985 passed, 5 existing skips, no failures, 87.48 seconds. The before-failing set is empty. Queue log: `1790859656-test-rgderive:baseline-serial.log`.

Recovery ladder so far: A resolved the documented real queue and configured from the actual preset directory with an isolated FetchContent root; success. B was unnecessary because all generation checks were green and the tool was freshly built through the normal CMake dependency. C has no remaining generation failure to isolate. The required native builds and serial baseline now pass after invocation recovery. CONTINUE; no baseline STOP and no required red was excluded.

## Final witness on the merged tree

Configure passed using the same preset, catalogue and worktree-local FetchContent root through the new local queue entry point. Log: `1790861561-build-rgderive:final-configure.log` in `/home/liory/.local/state/undertow/undertow-box-logs/`.

Build passed: all 22 RenderGraph test executables, both headless application witnesses, VIXEN, vixen_editor and all 22 configured `*_check` targets. The queued payload discovers targets from `build/wsl/build.ninja`, runs `nice -n 10 cmake --build ../build/wsl -j4 --target <RenderGraph targets> <check targets> test_headless_cornell_graph test_headless_ui_graph VIXEN -- -k 0` from `VIXEN/`, and then builds vixen_editor sequentially to avoid app asset-staging races. It rebuilt the changed production nodes and regression executable and relinked every affected executable. The pinned CodegenTool was rebuilt through CMake and every generated comparison passed. No generated source drift. Catalogue hash remains the baseline hash. Log: `1790861747-build-rgderive:final-build.log`.

Fresh after producers passed through one queued test job, under `bash -lc`, using the same Dozen ICD, scripts, frame numbers, timeouts and capture-camera settings as the baseline. Both use the absolute rebuilt CMake target path (`build/wsl/binaries/VIXEN` and `build/wsl/binaries/vixen_editor`) with `VIXEN/` as their asset working directory. Main retains `DISPLAY=:0`; editor retains `VIXEN_EDITOR_OFFSCREEN_CAPTURE=1` and `VIXEN_EDITOR_TEST_CAMERA=top-down`. HUD wrote three new PNGs plus its log; editor wrote four new PNGs plus `run_editor_script.log`. Directories: `<worktree>/.tmp/rgderive/after/hud` and `.../after/editor`. Producer log: `1790862686-test-rgderive:final-captures.log`.

The final queued test payload first runs `ctest --test-dir <worktree>/build/wsl --show-only=json-v1`, saves the inventory, and selects by owning command path exactly as at baseline. It finds 24 owners and 994 entries: the original 988 RenderGraph entries, four new regressions, and two headless application entries. It runs `ctest --test-dir <worktree>/build/wsl --tests-from-file <worktree>/.tmp/rgderive/after/tests.txt --output-on-failure --output-junit <worktree>/.tmp/rgderive/after/results.xml -j1`, with the Dozen ICD and absolute after-capture directories under a login shell. Exit 0, 139.16 seconds. Log: `1790862793-test-rgderive:final-serial.log`.

| Witness | Selected | Passed | Skipped | Failing set |
| --- | ---: | ---: | ---: | --- |
| Fresh original full scope | 990 | 985 | 5 | empty |
| New regressions against original validator | 4 | 3 | 0 | OptionalWindowMayBeUnconnected |
| Fresh final full scope | 994 | 989 | 5 | empty |

The full before/after failing sets are empty. The four new cases all pass after the fix. The five skips are unchanged: GPUQueryManagerIntegration.Placeholder; PushConstantGathererNodeTest.RuntimeFieldDiscovery; ValidateFieldTypes; HandleNullShaderBundle; HandleEmptyPushConstantMembers. No required test was excluded to obtain green.

HUD PNG hashes match the baseline. Editor frame 5 equals 75 and frame 45 equals 105 within each run; the edit-state images differ, and all existing pixel/state assertions passed. Editor PNG hashes differ across baseline and final runs. Decoded RGB comparison finds 94 of 250,000 pixels changed in frame 5 (maximum channel delta 63), and 1,024 in frame 45 (maximum delta 50), both bounded to (234,232)-(265,263). The cause of this cross-run variation is unclassified; this report does not claim byte-identical editor rendering. No captures or golden expectations were rewritten to clear a check. An optional Pillow comparison command failed because PIL is unavailable; standard-library struct/zlib inspection supplied the measurements, and a tooling proposal records that manual step.

## Baseline findings

The fresh main/HUD producer on unchanged `e0cf083a` logged `shadow_visibility_wave_desc_gatherer` errors: bindings 40 and 43 exceed `resourceArray_.size()=36`. The producer exited 0 and wrote the expected PNGs. The fresh serial baseline passed all HUD/editor pixel assertions despite these diagnostics. They are findings on unchanged source, not excluded required failures. The final producers log the same binding errors and missing vkCreateDebugReportCallbackEXT diagnostic; both complete successfully and the full required suite passes. The missing debug-report entry point also appears in both baseline producer logs, including `1790858594-test-rgderive:baseline-hud-capture.log`. No shader or descriptor plumbing has been edited by this lane.

## Scope decision and remaining work

The brief explicitly says “stay in `libraries/RenderGraph` and its tests.” R319 deletion for T-1468, T-1506 and T-1508 also requires the small application caller migrations listed above. A narrow scope clarification was requested while baseline work continued. Without an answer, this lane retains the explicit RenderGraph-only boundary.

REFER-TO-ORCHESTRATOR (scope), not STOPPED: baseline unobtainable. Options: authorize those mechanical application migrations and the shared target-interface change alongside RenderGraph work, or dispatch the three dependent items to a lane whose scope includes those files. Recommendation: authorize that narrow expansion. Keeping the retired argument, flags or duplicated booleans would violate R319. The task rulings settle the intended behavior; the unresolved point is edit ownership.

Exact stopping point: T-1507 is implemented and fully witnessed; T-1468/T-1506/T-1508 have source audits and caller/interface anchors, but no semantic changes. Remaining work is to implement the three derived contracts, delete each retired form and migrate its affected callers, then rerun the same complete generation/build/capture/serial witness. No answer to the scope clarification arrived during the lane. Work is being committed before the three-hour checkpoint; nothing was abandoned because of an unrelated red.

The only product edits are the three T-1507 RenderGraph files. Restored this lane's runtime-only manifest churn after captures. `git diff --check` passed. Report and all nine SPT inbox entries are validated as artifacts and committed separately from the implementation. The queue-entry-point hole observed at the starting base is now addressed by the merged T-1477 wave change; its proposal preserves the earlier discovery record. No push.

## CONSOLIDATION ISSUES

- proposed: Expose the real box queue to isolated VIXEN worktrees
- proposed: Provide CodeGraph discovery for isolated VIXEN worktrees
- proposed: Default VIXEN FetchContent to the active worktree
- proposed: Label RenderGraph CTest cases by their owning suite
- proposed: Declare the WSL capture runner display environment
- proposed: Run VIXEN capture producers as CTest fixtures with their logs
- proposed: Resolve capture executables from CMake's runtime output directory
- proposed: Keep runtime cache manifests out of tracked VIXEN source
- proposed: Provide a native before/after capture pixel comparison command
