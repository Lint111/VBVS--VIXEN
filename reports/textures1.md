# sn-textures1 lane report

Base: `f750d6e9d2535dfc97329584c9d067b9a9269784`

Implementation commit: `fd89b971` (`feat(textures): extract texture runtime module`)

## LANDABLE NOW

- Added `VIXEN/libraries/Textures` as the runtime module for the existing kernel texture model. The moved loader, STB decoder, texture cacher, residency façade, and CMake target are declared in `VIXEN/libraries/Textures/CMakeLists.txt:4-39`.
- Kept the node as a binder: `VIXEN/libraries/RenderGraph/src/Nodes/TextureLoaderNode.cpp:110-122` asks `TextureResidency` for existing cached image/sampler resources and emits the unchanged graph outputs.
- Deleted GLI loader/dependency and the three dead node parameters; removed loader sampler creation and linear upload. The honest slice-1 mip and synchronization notes are in `VIXEN/libraries/Textures/README.md:8-15`.
- Pinned stb to `2c980bb59875b0d32144a71867fbdebb2f77cd20` in `VIXEN/dependencies/CMakeLists.txt:29-37`, with the MIT/public-domain notice in `VIXEN/libraries/Textures/include/Textures/STBTextureLoader.h:16-19` and the module README.
- Added five test cases: two CPU decode cases and one cacher-hit case in `VIXEN/libraries/Textures/tests/test_texture_module.cpp:40-74`, plus two node-binding cases in `VIXEN/libraries/RenderGraph/tests/Nodes/test_texture_loader_node.cpp:11-32`.
- Updated the moved loader’s no-new-mutex policy entry at `VIXEN/scripts/no-new-mutex.allowlist:91`.
- SPT disposition: R649 is the accepted ruling/design for this slice. The local SPT CLI had no configured workspace for a direct `show R649`, so no close event was written.

## Verification

Measured:

- `git diff --check`: pass before commit.
- Forbidden-surface scan over live VIXEN CMake/source paths: no GLI dependency, old loader path, deleted node parameter, `EntityId`, or `KindRef<All>` additions found. Historical archive/documentation references were not changed.
- `git merge --no-edit origin/wave/authoring-convergence`: already up to date before the implementation commit.
- Fixture is a checked-in `500x500`, 8-bit RGB PNG; the decode test requests RGBA8 and checks extent plus the first 16 decoded bytes.

Not executed:

- VIXEN CMake configure/build: 0 successful runs. The first queued configure stayed at admission for 625 seconds with no CMake process or cache (`baseline-configure.log`); the retry stayed at admission for 419 seconds (`configure1.log`). Both were stopped by exact-PID cleanup after queue history showed a long-held cross-lane build blocker.
- New CTests: 0/5 executed; no configured build tree existed.
- Instancing demo GPU capture and before/after byte comparison: not run because the required executable could not be configured/built.

## STOPs

`STOPPED: baseline unobtainable: VIXEN configure/build/CTest/capture scope; the live queue held both documented configure attempts at admission with no target result or build cache.` Recovery A checked the documented live queue path, `UNDERTOW_ROOT`, FetchContent cache location, and the configured CMake invocation; a second queued retry was attempted. Recovery B was not applicable because no generated output or cache was created. Recovery C was unavailable because no supported target build could be produced without the queued configure. This is an external queue blocker, not a source diagnostic.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Bounded admission diagnostics for queued VIXEN configure/build jobs (SPT proposal `fd1815e66206`)
