# cx-scenecost1 — R888.1 per-scene GPU cost cache

## STATUS

IMPLEMENTED and committed. CPU/build/unit witnesses are green. The required native-Windows GPU
witness is STOPPED because this lane only has the repository's WSL Mesa Dozen ICD; the native
preset requires an MSVC/Windows environment.

## SHAs

| Item | SHA / result |
|---|---|
| Base | `57a94e7a5e7a6639478a4b55dfff91c62e1f3cd2` |
| Required VIXEN merge base | `origin/wave/authoring-convergence` = `7daa62d892b7631e019c6970cb68aaa036c65cb9`; already up to date |
| Lane implementation commit | `577cdc381` |

## CHANGE

`GpuChunkCostCacher` is a new device-scoped CashSystem cacher. `TiledDispatch` registers it through
`MainCacher`, performs exact-key lookup, launches cold at `kLaunchFloor` on a miss, and stores the
measured `GpuChunkCost` envelope after readback. The old `SimpleTaskProfile` chunk map and its
device+extent-only nearest/previous seeding are retired; ordinary task-profile calibration remains.

## KEY DEFINITION

The cache key hashes the following fields in this order; calibration values are payload, not key
material:

| Field | Source / reason |
|---|---|
| Scene identity | `VIXEN_SCENE` (default `cornell`), plus GlyphSDF scene/variant/RTL/text/bake inputs and the isolated demo selectors collected by `BuildRenderGraph` |
| Pass identity | `TiledDispatch` node instance name (`test_dispatch`, `shadow_visibility_wave`, etc.) |
| Extent | Full dispatch workgroup extent `x`, `y`, and `z` |
| Quality profile | Render-path scale, upscaler, AA provider, history weight, temporal-jitter flag, and sampling path |
| Device | `CashSystem::DeviceIdentifier(device).GetHash()` |

This prevents the RTL scene and another scene, profile, pass, extent, or adapter from sharing a
learned envelope. A missing exact entry cannot import a previous extent or scene.

## WITNESS

| Scope | Command/result |
|---|---|
| Base build | Queued `cmake --build build/wsl-debug --target test_voxel_scene_key test_rendergraph_dispatch`; exit 0 |
| Changed build | Queued target build for `VixenApp`, `test_gpu_chunk_cost_cacher`, and `test_rendergraph_dispatch`; exit 0 |
| Cacher tests | 3/3 passed: different scenes, serialized same-configuration reuse, deliberate extent break |
| Existing dispatch suite | 197/197 passed |
| Remaining `SimpleTaskProfile` tests | 15/15 passed after retiring the old chunk map |
| Runtime cache exercise | Dozen RTL supplemental run wrote `GpuChunkCostCacher.cache` (2,185 bytes) under the device cache directory |
| Registered RTL guard | CTest found `Application.LookdevCapture.GlyphSdf.RtlProof.SubmitGuard` but reported it disabled by `VIXEN_WSL_DZN_ICD` policy |
| Supplemental Dozen guard | Failed on translated `shadow_visibility_wave` at 1,096 ms versus the 500 ms guard. This is not a native-Windows result and is not attributed to this change. |

## RECOVERY / BASELINE

- CodeGraph was queried first; no index was available in this worktree, so git inspection was used.
- The first preset invocation from the worktree root failed because `CMakePresets.json` is under
  `VIXEN/`. Recovery A reran the same queued configure from `VIXEN/` successfully.
- A post-change build invocation from `VIXEN/` used `VIXEN/build/wsl-debug`, while the configured
  tree is the documented worktree-level `build/wsl-debug`; the corrected queued build passed.
- CTest discovery returned “No tests were found” for the focused gtest targets. The built gtest
  executables were then invoked directly through the queue; all required CPU tests passed.

## STOPS

1. Native GPU correctness/timing remains unverified in this lane. The WSL Dozen configuration sets
   `VIXEN_WSL_DZN_ICD`, and `vixen_lock_gpu_tests` intentionally disables the plain native capture
   tests. The native `vixen-ninja-release` preset is Windows/MSVC-only. Owner action: run the eight
   requested shared-GPU runs on native Windows: RTL 1080p/4K before and after, cold/warm cache,
   look-dev 16 plus HUD 3 byte comparisons, guard alone/first/last/in arbitrary order, and full
   native GPU CTest.
2. The supplemental Dozen run also logged an unrelated `DescriptorSetLayoutCacher` save warning;
   the new GPU cost cache itself serialized successfully.

## SPT DISPOSITION

`cx-scenecost1` was not present in the scoped SPT catalogue. Two incidental velocity/consolidation
proposals were appended to `.spt-proposals/cx-scenecost1.jsonl`.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN preset and build-tree working-directory contract is ambiguous
- proposed: VIXEN CTest discovery omits the configured gtest targets
