# Lane agentport — R468 report

## LANDABLE NOW

R468's AppFlow session channel is implemented in the VIXEN editor. The design note is
[AppFlow-Agent-Channel.md](../VIXEN/Vixen-Docs/01-Architecture/AppFlow-Agent-Channel.md). The
channel uses local JSON lines on a loopback socket with a per-editor token because the launcher
session pipe is not yet available in this worktree. No launcher or kernel source was changed, and
no new AppFlow attribute, registry, or kind was added.

### Tips

- Start the editor with `VIXEN_APPFLOW_DESCRIPTOR` set to a session-specific JSON path; otherwise
  it writes a descriptor under the system temp directory. The descriptor carries the loopback port
  and session token and is mode `0600` on POSIX.
- Point an MCP host at `python3 VIXEN/tools/appflow_mcp_server.py --descriptor <descriptor>` using
  stdio. The server exposes `appflow.list`, `appflow.dispatch`, `appflow.read`, `appflow.subscribe`,
  `appflow.await_frame`, and `appflow.readback`, plus pause/run/step.
- For an action plus pixels, use the dispatch readback option to wait for the target presented frame
  and return the selected native-resolution pixels inline. The report transcript is
  [agentport-appflow-transcript.json](agentport-appflow-transcript.json).

### Proof

- The new editor channel gate passed end to end through MCP. It asserted mask trail `[7, 3, 7, 3]`,
  undo/redo depths `(4,0) → (5,0) → (4,1) → (5,0)`, typed dispatch results, subscription events,
  await-frame, pause/step/run, and PNG/raw readbacks. The full JSON transcript is 8,160,979 bytes.
- The low-contention latency sample changed pixel hash between frame 20 and frame 24, first observed
  five presented-frame numbers after the action frame. Sequential samples skipped frames 21–23, so
  the exact first changed frame is bounded after frame 20 and by frame 24. This did not meet R463's
  first-presented-frame target; see the renderer finding in the design note.
- Low-contention readback measured 26.4 ms cumulative frame-thread CPU for the 500×500 affected
  region, 0.48 ms for a 64×64 selection, and 11.3 ms for the full frame. MCP round trips were 1.56 s,
  0.47 s, and 0.66 s respectively. DZN required a full 1,000,000-byte image transfer for the small
  selection, then cropped it on CPU.
- The final full-suite transcript sampled the toggle at frame 21 and first observed the changed hash
  at frame 25. Under concurrent GPU rendering, editor readback round trips were 2.18 s for the
  instance region, 5.01 s for 64×64, and 7.23 s for the full frame; outer MCP round trips were 2.33,
  5.18, and 11.11 s. Those stressed timings are not the latency baseline.
- The C++ build, direct AppFlow codegen check, all configured `*_check` targets, and a no-op rebuild
  passed. Exact runtime suite status and any accepted baseline findings are recorded below after
  the final witness completes.

### Verification and recovery record

- Base / current merged wave SHA: `7e5d2f6568408c42ae5ee7cb5e2974174b5cf70f`. Refreshed and merged
  `origin/wave/authoring-convergence` before the final witness; it was already up to date.
- `cmake --build build/wsl --parallel 4` through the global queue: **PASS**, full build (552 steps).
- All 23 `*_check` targets present in this tree: **PASS**. The brief expected 22; the current tree
  contains 23, listed in the build log. `appflow_check` passed.
- Pinned kernel `CodegenTool~`: queued restore **PASS**, Release build **PASS** with 0 warnings / 0
  errors, direct `dotnet run --check` for the AppFlow schema **PASS** with no generated diff.
- Python syntax check for the client, MCP server, and gate: **PASS**. `git diff --check`: **PASS**.
- `cmake --build build/wsl --parallel 4` no-op rebuild: **PASS**, `ninja: no work to do.`
- Initial baseline CTest reported missing `build/wsl/binaries/VIXEN`; recovered by queued build of
  `VIXEN` and the windowed-capture test target. The low-contention pre-change editor producer then
  passed in 20.28 s.
- A later combined old/new editor-gate run timed out the legacy editor capture producer at 180 s
  while another worktree was running a DZN lookdev capture. The producer reached capture tick 115;
  the dependent old R6 assertion was skipped, while the new channel gate passed in 102.89 s and the
  HUD producer passed. The later full queued RenderGraph CTest passed **1,345/1,345**, with zero
  failures and 11 explicit skips. Its 11 capture witnesses passed: both fixture producers, all six
  editor capture assertions, and all three HUD capture assertions. It also included the old R6 gate
  and the new MCP gate. Caller `DISPLAY`, Wayland, and Vulkan variables were unset. The new gate
  completed in 64.77 s during concurrent GPU activity. Log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1791589589-test-agentport-rendergraph-suite.log`.
- The queued SVO CTest ran 770 enabled tests: **761 passed, 1 failed, 8 skipped**, with one
  additional disabled test. Its sole failure is the allowed opcode 94 exception:
  `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` rejects
  `M4d_Output_IsPassthrough` with `recipe gradient capability mismatch: 94` (T-1449). Log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1791590400-test-agentport-svo-suite.log`.
- The no-new-mutex check initially required the `--fix` allowlist entry and a manual acquisition
  index update for `AppFlowReadback.cpp` (`VK2-borrowed`). The check then passed with two existing
  `KernelDispatch` unclassified warnings.

### `VIXEN_EDITOR_SCRIPT` retirement boundary

The script harness remains in place as R468 requires. The later removal inventory is exact in the
design note: `EditorApplication.h`, `EditorApplication.cpp`, the editor CMake fixture assignment,
`measure-editor-latency.py`, `run-vixen-windowed-captures.sh`, `temp/run_editor_script.bat`, and the
log-based assertions/comments in `test_editor_toggle_undo_capture.cpp`. The capture fixture controls
`VIXEN_EDITOR_CAPTURE_FRAMES` and `VIXEN_EDITOR_CAPTURE_DIR` remain separate.

### STOPs

No design stop or new schema capability was needed. The R463 first-frame visibility target remains a
renderer finding. RenderGraph passed in full; SVO has only the allowed opcode 94/T-1449 failure
recorded above. The capture witnesses ran with caller `DISPLAY`, Wayland, and Vulkan variables
unset. No before/after baseline pixel comparison was performed in this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: AppFlow readback mutex classification requires manual inventory edits
- proposed: Editor capture witness needs explicit fixture executable preparation
- proposed: VIXEN GPU test locking does not coordinate across CTest worktrees
- proposed: VIXEN tools ignore rule hides required AppFlow client and gate files
