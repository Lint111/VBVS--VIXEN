# cx-hudoffscreen1

## STATUS

STOPPED at the required native visual gate. The implementation is committed and
the Linux offscreen producer is green, but the queued Windows-native build never
reached checkout creation because another lane held the single Windows-native
capacity slot. No native PNG or visible-boot result is claimed.

Commit: `27069f10b` (`VIXEN: route HUD captures and headless boots offscreen`).
The branch includes the required merge check: `origin/wave/authoring-convergence`
was already at the branch base.

## CAUSE (file:line)

- `VIXEN/application/main/source/VulkanGraphApplicationCapture.cpp:374-375`
  sent HUD captures to `main_swapchain` unless the presentation target had
  already been set to `Offscreen`; the swapchain path therefore depended on WSI.
- `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp:111-128,626-638`
  creates the GLFW surface, negotiates present support, and then propagates the
  surface-capability error before the render loop can start.
- `VIXEN/libraries/VulkanResources/src/VulkanSwapChain.cpp:318-328` observes
  `maxImageExtent == 0` and returns `VK_ERROR_OUT_OF_DATE_KHR`. This is the
  minimized/locked/disconnected desktop failure recorded in base-red row 52.
- The documented fallback did not itself prove a deadlock: once it selected the
  offscreen path it reached shader/render-graph startup, but no PNG can be
  produced until the first frame reaches `Update()`/`PollPendingCaptures`
  (`VulkanGraphApplication.cpp:2634-2649`, `VulkanGraphApplicationCapture.cpp:227-248`).
  The cx-hudkit1 run was canceled while still in that startup phase.
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
- `PrepareWithUpscalerFallback()` retries a windowed graph on the offscreen
  target when a zero-extent surface is discovered, allowing a frame-loop-only
  native boot to run without WSI.
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

## BASE VS HUDKIT1 HUD HASHES

The native sets are unavailable, so byte identity is **UNVERIFIED**. The
reference/base hashes recorded by cx-hudkit1 are:

| frame | base/reference SHA-256 | lane-hudkit1 (`19b12a02a`) |
|---|---|---|
| 5 | `513e03efe104fbbf6cd5510ebe2e04b164c194cedd5feec029a6fc4c4e98cefa` | not produced |
| 45 | `8822405f57c40e0cf79bceaabe49c967c5129b9a55580f877c6f8610e2b9ceb1` | not produced |
| 75 | `aa4569d2a1b5ff02885a939ac1924506d9c062fbc7ee98e1c53c4f30e049efc4` | not produced |

No pixel diff is claimed because neither native lane PNG set was produced.
The hudkit1 worktree was not modified.

## REGISTER ROW 52 UPDATE TEXT

`VIXEN row 52: implementation fixed in lane-hudoffscreen1 27069f10b — HUD
captures and explicit headless frame loops select the offscreen target before
WSI construction; zero-extent surfaces emit located R796 and windowed prepare
has an offscreen retry. Linux offscreen producer is green. Keep row open for the
required native base-vs-hudkit1 PNG/hash witness; the queued Windows-native build
was not admitted because the single native-capacity slot was occupied.`

## STOPs

- Required native capture/build STOP: the documented queued command
  `with-test-lock.sh --agent cx-hudoffscreen1 --resource build
  --label cx-hudoffscreen1:native-build -- bash tools/vixen-native.sh
  --lane hudoffscreen1 build all vixen-ninja-release VIXEN` remained queued.
  The active holder's log was
  `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/rsbuiltins1/.tmp/vixen-native/rsbuiltins1-build-20261113-230305.log`,
  still at 965/1369 targets after about an hour. Our request was canceled with
  exit 130 before any native checkout or Windows process was started. Required
  next action: rerun the queued native Release build and `main_hud_capture.bat`
  after Windows-native capacity is released, then run the same capture against
  exact `lane-hudkit1` `19b12a02a` without editing that lane.
- Native visible boot STOP: no assigned Undertow/launcher worktree was available
  for the cross-repository boot witness; this lane only provides the VIXEN-side
  `VIXEN_HEADLESS=1` selection and zero-surface retry.

## REDS

- The Linux offscreen run emitted existing D3D12 validation diagnostics and a
  cache-save warning during teardown, but exited 0; no new source warning was
  observed in the build.
- The native required gate is unclassified, not a product regression: it did not
  execute. Base row 52's pre-existing `0x0`/`VK_ERROR_OUT_OF_DATE_KHR` evidence
  remains the motivating red.

## SPT DISPOSITION

No incidental non-facade workaround was needed and no SPT proposal was filed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- None.
