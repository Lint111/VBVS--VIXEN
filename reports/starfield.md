# T-1138 — Starfield background pass

## Result

The sky-sphere scaffold now produces a deterministic procedural starfield and a soft galaxy band. The default graph keeps it disabled (`enabled = 0`), and the optional `star_list` input remains disconnected while owner question Q7 is open.

The starfield is sampled only for ray misses in `SpatialReuseShade`; hit pixels remain on the existing body-lighting path. Runtime controls are the node parameters `enabled`, `seed`, and `brightness`. The graph uses seed `0x5a17f13d` and brightness `0.45`. The generator uses 1,536 deterministic stars, warm/cool white linear-light tints, a muted blue-violet band, `kStarGain = 0.30`, and `kGalaxyBandGain = 0.065`. At default brightness, the peak star contribution is at most 0.135 linear radiance and the band is below 0.030.

The enabled Cornell capture is [starfield-capture.png](starfield-capture.png); the default-off capture is [starfield-capture-off.png](starfield-capture-off.png).

## Implementation

- Replaced the unwired `SkySphereNode` scaffold with a persistent 1024×512 RGBA16F octahedral cache. Seed, brightness, enabled state, or changed optional star-list values invalidate the generated image; `refresh_cadence_frames` controls polling of a connected list.
- Added a source-neutral optional `SkySphereStarList` slot. The default graph does not connect it, preserving Q7 for its owner.
- Wired the image through the existing image-sync gatherer and `SpatialReuseShade` provider set. The shader adds the image only after assigning the existing background in the ray-miss branch.
- Regenerated `SpatialReuseShade-SDI.g.h` with the documented `sdi_tool merge-variants shaders/sdi-variants.json` generator; binding 35 is now represented in the generated descriptor table.
- Added isolated-process headless captures and a pixel comparison test. The two capture processes each use the production Cornell graph, capture while disabled, then enable the node after frame 5 and capture after frame 6.

## Verification

- CodeGraph was queried before source discovery, but this VIXEN worktree has no CodeGraph index; source discovery continued with `rg` and file reads.
- SDI generation and `sdi_tool merge-variants shaders/sdi-variants.json --check` passed through the global queue. Only `SpatialReuseShade-SDI.g.h` changed.
- The queued full `cmake --build build --parallel 4` passed. The final queued `cmake --build build --target VIXEN test_headless_starfield_graph --parallel 4` also passed, so both the regular application and witness executable link the final node library. `git diff --check` passed.
- Final queued `ctest --test-dir build -R HeadlessStarfieldGraph --output-on-failure -j1`: 3/3 passed. It checks two independent captures, byte-equal off captures, identical decoded RGB pixels for enabled runs, unchanged red and green Cornell wall pixels at `(151, 200)` and `(348, 200)`, and fewer than one third of frame pixels changed.
- The final 500×500 off capture and its repeat both have SHA-256 `4140960aaaa40c60dcece3f0e8752e5a407d43b74324fd4c8c4b1a60463309d3`, matching the pre-change `headless-cornell-frame-a.png` baseline byte-for-byte.
- The final enabled capture and repeat are pixel-identical. Comparing off to on changes 16,016/250,000 pixels (6.41%); 203 pixels gain at least 16/255, and the peak channel addition is 29/255.
- DZN's validation output still contains descriptor-update-while-pending and command-buffer-reuse VUIDs in the main graph. These do not name binding 35, and no `skySphereImage` unbound or undefined-layout diagnostic remains. They did not fail the focused CTest selection; their classification against an unchanged pre-edit runtime log is **unclassified**.

## Baseline red and recovery

- Base SHA: `7f4db6877bac7062502b1cb603b68464cf42da21`.
- Before implementation, the full RenderGraph CTest selection ran 1,334 cases: 1,316 passed, 11 were skipped, and 7 windowed capture cases failed (four `EditorToggleUndoCapture.*` and three `HudRenderCapture.*`). The failures require missing generated editor/HUD capture fixtures; their capture scripts were not run by those cases.
- The documented queued window-capture recovery was attempted with DZN. The first run exited with `VK_ERROR_INCOMPATIBLE_DRIVER`; `VK_LOADER_DEBUG` identified missing `libxcb-keysyms.so.1`. Setting `LD_LIBRARY_PATH` to the provisioned `VIXEN/.windowing-deps/usr/lib/x86_64-linux-gnu` directory allowed the app to start, but the baseline graph still failed to produce the editor/HUD files, with existing shadow descriptor bindings 40/43 out of range and HUD `stbi_write_png` errors. The offscreen target witnesses passed independently. This remains a pre-existing windowed-fixture finding outside the requested headless starfield witness.
- The first queued full build failed because `cmake -E touch` could not create `build/application/main/vixen_stage_assets/510925ea10d7f0cf.stamp` without its parent directory. `mkdir -p build/application/main/vixen_stage_assets` followed by the same queued full build passed; this setup hole is proposed for consolidation.
- The literal `origin/main-wave` ref is not advertised by this VIXEN mirror (`git fetch origin main-wave` reports “couldn't find remote ref”). The published `origin/wave/authoring-convergence` is at the dispatched base SHA; `git merge --no-edit origin/wave/authoring-convergence` reported “Already up to date.”

## STOPs

None. Q7 remains open and the default star-list slot is unconnected.

## CONSOLIDATION ISSUES

- proposed: CMake asset staging should create its stamp directory
- proposed: Windowed capture should include provisioned X11 libraries in its runtime path

Both proposals are recorded in `.spt-proposals/starfield.jsonl`.
