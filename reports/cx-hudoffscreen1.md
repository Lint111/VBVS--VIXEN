# cx-hudoffscreen1

## STATUS

HUD capture gate PASS. The implementation is committed in `27069f10b`
(`VIXEN: route HUD captures and headless boots offscreen`), and the required
native Release captures passed on the lane tree (`42975d6a28a7`, based on
`origin/wave/authoring-convergence` `c27282c48d`) and on the read-only
`lane-hudkit1` tree at exact `19b12a02a`. The two image sets are intentionally
not byte-identical; the measured pixel delta is recorded below. Hudkit1 source
was not modified.

The VIXEN-side implementation and capture witness are complete. The separate
cross-repository visible-boot witness remains STOPPED because this lane has no
assigned Undertow/launcher worktree.

Commit: `27069f10b` (`VIXEN: route HUD captures and headless boots offscreen`).
The branch includes the required merge check: `origin/wave/authoring-convergence`
was already at the branch base.

## CAUSE (file:line)

- `VIXEN/application/main/source/VulkanGraphApplicationCapture.cpp:227-248,362-430`
  polls HUD captures from the frame loop and reads the graph's presentation
  target. A capture therefore cannot produce a PNG until a graph has compiled
  and a frame has executed; the old default target was window/WSI-backed.
- `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp:111-128,626-638`
  creates the GLFW surface, negotiates present support, and then propagates the
  surface-capability error before the render loop can start.
- `VIXEN/libraries/VulkanResources/src/VulkanSwapChain.cpp:318-328` observes
  `maxImageExtent == 0` and returns `VK_ERROR_OUT_OF_DATE_KHR`. This is the
  minimized/locked/disconnected desktop failure recorded in base-red row 52.
- The documented fallback stalled before the first frame because a zero-extent
  WSI failure happened during graph preparation, before `Update()`/`PostTick()`
  could reach `PollPendingCaptures()` (`VulkanGraphApplication.cpp:547-551,2634-2649`,
  `VulkanGraphApplicationCapture.cpp:227-248`). Thus the capture request had no
  completed render target to read back; this was a pre-loop surface failure,
  not a PNG writer deadlock.
- The working look-dev precedent is
  `VIXEN/application/main/tests/test_lookdev_capture.cpp:1255-1263`, which
  selects `PresentationTarget::Offscreen` before graph construction. Its UI
  pass uses `TransferSrc` for offscreen targets
  (`VIXEN/application/main/source/graph/BuildPresentationGraph.cpp:44-49`),
  while the WSI path uses `PresentSrc`.

## CHANGE

- `BuildRenderGraph()` now selects the shared offscreen presentation target
  before creating any `WindowNode`/`SwapChainNode` when `VIXEN_HEADLESS=1`,
  `VIXEN_HUD_OFFSCREEN_CAPTURE` is set, or `VIXEN_HUD_CAPTURE_FRAMES` exists.
  The selection is at `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:418-429`.
- `PrepareWithUpscalerFallback()` retries a windowed graph on the offscreen
  target when a zero-extent surface is discovered, allowing a frame-loop-only
  native boot to run without WSI (`VIXEN/application/main/source/VulkanGraphApplicationLifecycle.cpp:304-322`).
- The zero-extent branch emits a located `[R796]` diagnostic with the supported
  headless/capture environment variables.
- `main_hud_capture.bat` now enables `VIXEN_HUD_OFFSCREEN_CAPTURE=1`.
- `CaptureHudFrameToPng()` already uses the same offscreen render target and
  readback helper as the look-dev path, so compute blit plus UI composite pixels
  remain on the selected target rather than switching capture semantics.

## VERIFICATION

- CodeGraph was attempted first; this checkout has no `.codegraph` index, so the
  source audit used the repository's tracked files afterward.
- Queued Linux Debug build: `cmake -S VIXEN --preset vixen-wsl-debug ...` plus
  `cmake --build build/wsl-debug --target VIXEN`: PASS, exit 0.
- Queued targeted build: `test_vulkanresources_basic`, `test_rendergraph_core`,
  `test_rendergraph_nodes`: PASS, exit 0.
- Queued targeted tests: VulkanResources and core RenderGraph selection,
  dependency, array-slot, and multi-producer tests: 23/23 passed (one test
  skipped by its own capability condition), exit 0.
- Queued Linux offscreen HUD producer: 76-frame run completed and wrote all
  requested PNGs, exit 0. Diagnostic hashes (WSL Debug/D3D12, not the required
  native Release witness):

  | frame | WSL diagnostic SHA-256 |
  |---|---|
  | 5 | `8ef581c78331f95049ebdc421687ce36993082ceb948067f75bba7b5a8fcab50` |
  | 45 | `1707ebe27c537dd43704c7760030f7359616eb95264739d790cef475c4e9a02b` |
  | 75 | `6ac8a82e172accd62508a38e805af76e63e0389c866d7cbd15ce9dd96b682640` |

- Queued native base Release build: PASS, exit 0; `VIXEN.exe` linked and the
  offscreen `main_hud_capture.bat` run exited 0 with all three PNGs.
- Queued native hudkit1 Release build at exact `19b12a02a`: PASS on the retry;
  the first attempt's stale-output lock failure was recovered without changing
  the hudkit1 worktree. Queued native hudkit1 capture retry: PASS, exit 0.

## BASE VS HUDKIT1 HUD HASHES

Fresh native Release PNGs were produced with no interactive desktop. The base
capture used the queued `gpu` job and the lane-hudkit1 capture used the queued
`gpu-native` job with a quoted Windows batch path and a process-local offscreen
environment override. Both runs exited 0.

| frame | base/reference SHA-256 | lane-hudkit1 (`19b12a02a`) |
|---|---|---|
| 5 | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | `67bb4d21bb5336272ca43657bc34a5491ed90a9389f47371d5d441e874bb872c` |
| 45 | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | `ce29db877deba24677f582468c0c94f67b4bcfa296a932d9383222893a920703` |
| 75 | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | `0453c443482f8fa1580b2ee3ddcd2c9ace0f5d38fedfac0247a1dd5955365645` |

The sets are **not byte-identical**. All three frames have the same measured
pixel delta: 240 pixels / 720 RGB channels differ, bounding box
`x=37..213, y=442..467`, maximum channel delta 23. This is the localized HUD
region; no hudkit1 source change was made in response.

Native evidence: base build
`.tmp/vixen-native/hudoffscreen1-build-20261114-001141.log`, base capture
`.tmp/vixen-native/hudoffscreen1-bat-20261114-002808.log`, and queued logs
`/home/liory/.local/state/undertow/undertow-box-logs/1794608888-gpu-cx-hudoffscreen1:native-hud-base-capture.log`
and
`/home/liory/.local/state/undertow/undertow-box-logs/1794609748-gpu-native-cx-hudoffscreen1:native-hudkit1-capture-retry.log`.

## REGISTER ROW 52 UPDATE TEXT

`VIXEN row 52: implementation fixed in lane-hudoffscreen1 27069f10b — HUD
captures and explicit headless frame loops select the offscreen target before
WSI construction; zero-extent surfaces emit located R796 and windowed prepare
has an offscreen retry. Queued native Release HUD captures on the current base
wave and exact lane-hudkit1 19b12a02a both exit 0 and produce frames 5/45/75;
hashes are not byte-identical (240 pixels differ in the HUD bounding box
x=37..213,y=442..467 on every frame). Keep row open only for the separate
cross-repository visible-boot witness.`

## STOPs

- Native visible boot STOP: no assigned Undertow/launcher worktree was available
  for the cross-repository boot witness; this lane provides the VIXEN-side
  `VIXEN_HEADLESS=1` selection, zero-surface retry, and a successful native
  frame-loop HUD capture.

## REDS

- The Linux offscreen run emitted existing D3D12 validation diagnostics and a
  cache-save warning during teardown, but exited 0; no new source warning was
  observed in the build.
- The first hudkit1 native build attempt was a recoverable environment red: the
  final copy failed because a stale validated PID `30372` owned
  `C:\vixen-native\hudkit1\VIXEN\binaries\VIXEN.exe`. After terminating that
  exact process, the queued retry passed. Base row 52's pre-existing
  `0x0`/`VK_ERROR_OUT_OF_DATE_KHR` evidence remains the motivating red; the
  native offscreen capture no longer reproduces it.
- An initial queued `cmd.exe` bridge exited 0 with only the WSL UNC-directory
  warning and wrote no PNGs; the corrected queued `powershell.exe` bridge
  invoked the quoted batch path and produced all three PNGs. This invocation
  gap is filed as the first consolidation proposal below.

## SPT DISPOSITION

No task id was assigned to this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native bat bridge lacks per-run environment forwarding
- proposed: Native VIXEN build lacks stale executable lock preflight
