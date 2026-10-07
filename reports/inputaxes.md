# Lane report — `inputaxes` (T-1164)

Base: `7f4db6877bac7062502b1cb603b68464cf42da21`

Branch: `lane-inputaxes`

## Implementation

- `VIXEN/libraries/RenderGraph/include/Data/InputState.h:27` defines five axes and a constexpr key-binding table for A/D, S/W, Q/E, and the arrow keys. `InputState::UpdateAxesFromKeyState()` sums active bindings and clamps each value to `[-1, 1]`. Existing `GetAxis*()` methods remain available and read those per-frame values.
- `InputNode::PopulateInputState()` rebuilds axes after the existing key-state fold. `BeginFrame()` clears axes and mouse delta before the node copies current held keys and cursor motion. Focus loss clears held keys, axes, pending mouse delta, and queued key/cursor events.
- The `InputState*` still uses `InputNodeConfig::INPUT_STATE` (`VIXEN/libraries/RenderGraph/include/Data/Nodes/InputNodeConfig.h:57`). AppFlow action bindings and the key edge maps remain separate.
- Added `test_input_axes`, gated by `VIXEN_FAIL_SCENARIOS`, with synthetic key, cursor, and focus-loss events. The tests cover opposite-key cancellation, both signs and bounds for all axes, key release, raw mouse delta reset, and focus-loss cleanup, including queued stale events.
- Documented the axis and mouse-delta contract in `VIXEN/Vixen-Docs/Libraries/RenderGraph.md:271`.

## Witness

The fresh baseline at the recorded base passed before implementation: RenderGraph core **16/16**, fail-scenario registry **5/5**, and AppFlow key-chord, input-profile, and action-stack suites **9/9**. The baseline configure and targeted build also exited successfully.

After implementation, the WSL CMake configure completed successfully. The queued build of `test_input_axes`, `test_rendergraph_core`, `test_fail_scenario_registry`, the three AppFlow targets, and `octreeconfig_check` completed successfully. Its first attempt caught an unqualified `EventBus` name in the new test; the test namespace was corrected and the queued retry passed. The OctreeConfig golden check passed.

The final queued test run passed **34/34**: input axes **4/4**, RenderGraph core **16/16**, fail-scenario registry **5/5**, and the AppFlow discrete-action suites **9/9**.

The dispatch requested a direct CodegenTool restore/build/check. `dotnet run --project ... --artifacts-path <worktree> ... --check` restored and built into `.tmp/codegen-tool-artifacts`, then failed to launch because `dotnet run` looked for an apphost in the pinned snapshot's default `bin/Release/net8.0` path. Running the newly built DLL directly with the same `OctreeConfig --check` arguments passed. The CMake `octreeconfig_check` also passed using the pinned cached tool. All generated artifacts from the direct invocation stayed under this worktree.

The direct run and its successful DLL retry were:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent inputaxes --resource build --label inputaxes-codegen-run-check -- dotnet run --project /home/liory/.cache/vixen/kernel-codegen/sources/yeroket-a2b2d6b9e1e93b37b1adc0d7dd99e1c63bf96bad/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj -c Release --artifacts-path /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/.tmp/codegen-tool-artifacts --disable-build-servers -p:InvariantGlobalization=true -p:NodeReuse=false -- --schema /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/codegen/config-schemas --struct OctreeConfig --out-cpp /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/libraries/SVO/include/Generated/OctreeConfig.g.h --out-glsl /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/shaders/Generated/OctreeConfig.glsl --check

bash /home/liory/.local/bin/with-test-lock.sh --agent inputaxes --resource light --label inputaxes-codegen-artifact-check -- dotnet /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/.tmp/codegen-tool-artifacts/bin/CodegenTool/release/CodegenTool.dll --schema /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/codegen/config-schemas --struct OctreeConfig --out-cpp /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/libraries/SVO/include/Generated/OctreeConfig.g.h --out-glsl /home/liory/projects/VBVS--VIXEN/.claude-worktrees/inputaxes/VIXEN/shaders/Generated/OctreeConfig.glsl --check
```

No rendered capture was needed for this input-only change. The full VIXEN suite was not run; verification targeted the affected engine and discrete-action test groups.

## Integration and scope notes

- For T-1165/T-1166, typed simulation ingress can read `InputState::GetAxis(...)` and `mouseDelta` from the existing `InputNodeConfig::INPUT_STATE` output. The current KFR read site is `KernelFederationRenderer/app/src/session_renderer.cpp:107`, where the renderer obtains `input->GetInputState()`; sim ingress remains out of scope here.
- `CameraNode` already reads `mouseDelta` and the existing keyboard-axis getters (`VIXEN/libraries/RenderGraph/src/Nodes/CameraNode.cpp:231`, `251`, `271`, `280`).
- `git fetch origin` was followed by no-op merges of `origin/main` and `origin/wave/authoring-convergence`; both already contained the lane base. This VIXEN remote has no `origin/main-wave` ref.
- The prescribed `SPT show T-1164` scope points at a missing `yeroket-stack.local.json`; the alternate installed manifest does not contain T-1164. The dispatch brief supplied the complete task scope and was used as authority.
- The WSL configure log prints the known Vulkan SDK `Include/glslang` case diagnostic while exiting 0; it is recorded as FR-25 in `VIXEN/Vixen-Docs/05-Progress/features/consumer-feedback-undertow.md:371`.
- No `.codegraph/` index exists for this worktree, so navigation used the repository docs and targeted `rg` queries.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: InputNode's synthetic test seam only injects mouse buttons
- proposed: Queue defaults over-reserve short configure and test jobs
- proposed: CodegenTool `dotnet run --artifacts-path` launches from the snapshot's default output path
