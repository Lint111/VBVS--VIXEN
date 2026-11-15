# cx-hudnative1

## STATUS

STOPPED: native Windows Release HUD witness unavailable. The d9 VIXEN tree
configures, but its Release build stops before the first compile because the new
recipe-services dependency for `RecipeElementKinds.cs` is recorded in WSL form
in the Windows Ninja graph. No `VIXEN.exe` or native HUD frames were produced;
therefore no native hash, pixel diff, or visual attribution is claimable.

Base witnessed: VIXEN `002a5f6b6adf53644e53ef947c16538440497411` (`origin/wave/authoring-convergence`),
with the d9 implementation at `f9230d0b1eafcc3979631c375fe8cdbdd6aea844` and
kernel pin `597757236569835a7b40ed01ec04c06e286af723`. The required scope was
the native Release build, `VIXEN/application/main/main_hud_capture.bat`, and
frames 5 / 45 / 75 compared with the vx8 native Release references.

## HASHES

| Frame | vx8 native Release reference | hudnative1 native Release | Prior WSLg context only |
|---:|---|---|---|
| 5 | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | not produced | `8ef581c78331f95049ebdc421687ce36993082ceb948067f75bba7b5a8fcab50` |
| 45 | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | not produced | `1707ebe27c537dd43704c7760030f7359616eb95264739d790cef475c4e9a02b` |
| 75 | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | not produced | `6ac8a82e172accd62508a38e805af76e63e0389c866d7cbd15ce9dd96b682640` |

The WSLg hashes are from the earlier `cx-opcodeown2-gpu0` capture and are not
being used as native references.

## IMAGE DESCRIPTIONS

No native hudnative1 image exists to open. I opened the three available prior
WSLg frames at
`/home/liory/.local/state/undertow/captures/cx-opcodeown2-gpu0/20261114T202825.1439653/hud/`;
all are 500×500 RGB PNGs:

- Frame 5 (`8ef581...`): dark indigo background, three central bodies, a
  lower-left `(no building)` resource panel, and an upper-right HUD panel with
  `tick 0`, `bodies 0`, `Lens: None (0)`, and empty Factions/Events sections.
- Frame 45 (`1707ebe...`): same scene after payload A; the upper-right panel
  reads `tick 30`, `bodies 3`, `Logistics (4)`, shows faction `acme` with
  value `3.5`, and event `T1 raid`.
- Frame 75 (`6ac8a8...`): same scene after payload B; the panel reads `tick
  60`, `bodies 3`, `Intel (1)`, shows `umbra` with value `0.2`, and has no
  event row.

The vx8 native reference PNG files were not present in the accessible evidence
folders, so they could not be opened independently here.

## DIFF / ATTRIBUTION

No `tools/compare-capture-pixels.py` run is possible: the native pair is absent,
so differing bbox and max channel delta are both N/A. The application never
launched, which means this run cannot attribute any visual difference to
`lighting_profile` content, opcode ownership, or HUD kit/offscreen work. No
new native references are recorded.

## RECOVERY / REDS

The first queued command was:

```text
bash /home/liory/.local/bin/with-test-lock.sh --agent cx-hudnative1 --resource build --label cx-hudnative1:native-release-build -- bash tools/vixen-native.sh --lane hudnative1 --branch lane-hudnative1 --log /home/liory/codeman-cases/undertow/reports/cx-hudnative1-evidence/native-release-build.log bat build.bat all vixen-ninja-release
```

It exited 1. Evidence:

- Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1794705246-build-cx-hudnative1:native-release-build.log`.
- Native build log: `/home/liory/codeman-cases/undertow/reports/cx-hudnative1-evidence/native-release-build.log`.
- Native sub-log: `C:\vixen-native\hudnative1\.tmp\native-build\vixen_build_-hudnative1-.log`.

The documented wrapper found the absolute Windows interop tools, created and
synced `C:\vixen-native\hudnative1`, provisioned the missing pinned Ninja
1.13.1, and completed CMake configure/generate. The pinned cache stamp matched
the kernel SHA and the source file existed at
`C:\Users\liory\AppData\Local\VIXEN\kernel-codegen\sources\yeroket-597757236569835a7b40ed01ec04c06e286af723\Packages\com.yeroket.utility.kernel-framework\Runtime\Recipe\RecipeElementKinds.cs`.
The build then failed before any target compile/link:

```text
ninja: error: '/mnt/c/Users/liory/AppData/Local/VIXEN/kernel-codegen/sources/yeroket-597757236569835a7b40ed01ec04c06e286af723/Packages/com.yeroket.utility.kernel-framework/Runtime/Recipe/RecipeElementKinds.cs', needed by 'codegen/recipe-services/GlyphOutlineInstance.g.hpp', missing and no known rule to make it
```

The generated `build.ninja` confirms the dependency is `\mnt\c\Users\...`
while the command itself correctly invokes the WSL tool with `/mnt/c/...`.
The d9 diff introduced both the new input and the bad dependency spelling at
`VIXEN/codegen/CMakeLists.txt:480,849,855`; the parent tree has no
`RecipeElementKinds` input. The pre-d9 vx8 native Release witness passed all
248 targets (`cx-consolvx8`, log
`/home/liory/.local/state/undertow/undertow-box-logs/1794669296-build-cx-consolvx8-r2:native-release-repair.log`),
so this is not a pre-existing base red and it is inside the required build
scope.

The exact queued capture route was also attempted once:

```text
bash /home/liory/.local/bin/with-test-lock.sh --agent cx-hudnative1 --resource gpu --gpu-benchmark --label cx-hudnative1:native-hud-capture -- bash tools/vixen-native.sh --lane hudnative1 --branch lane-hudnative1 --log /home/liory/codeman-cases/undertow/reports/cx-hudnative1-evidence/native-hud-capture.log bat VIXEN/application/main/main_hud_capture.bat
```

It exited 3 with `The system cannot find the path specified.` because the
Release build produced no `VIXEN/binaries/VIXEN.exe`. Queue log:
`/home/liory/.local/state/undertow/undertow-box-logs/1794706092-gpu-cx-hudnative1:native-hud-capture.log`.

Recovery outcomes:

- A — applicable invocation/provisioning: completed. Absolute System32
  interop and the live queue were used; `build.bat provision` installed Ninja;
  the documented Release route still failed at the dependency graph.
- B — fresh generation/provisioning: completed as far as supported without
  semantic edits. The native tree and Release CMake graph were freshly created;
  the immutable pinned snapshot was present and matched. No generated file was
  hand-edited.
- C — isolation: the pre-d9 vx8 native Release log passes 248/248, and the
  parent/d9 source diff isolates the new dependency. The red cannot be excluded
  because it prevents the required `VIXEN.exe` target and HUD capture.

Required owner fix: make the recipe-services `DEPENDS` entry a native Windows
path while retaining the WSL path for the command argument, or route both
through one conversion helper; then rerun the two commands above and perform
the three hash and pixel checks.

## COMMAND ROUTE

Copy-paste from this VIXEN worktree after the dependency fix:

```bash
cd /home/liory/projects/VBVS--VIXEN/.claude-worktrees/hudnative1

bash /home/liory/.local/bin/with-test-lock.sh --agent cx-hudnative1 --resource build --label cx-hudnative1:native-release-build -- \
  bash tools/vixen-native.sh --lane hudnative1 --branch lane-hudnative1 \
    --log /home/liory/codeman-cases/undertow/reports/cx-hudnative1-evidence/native-release-build.log \
    bat build.bat all vixen-ninja-release

bash /home/liory/.local/bin/with-test-lock.sh --agent cx-hudnative1 --resource gpu --gpu-benchmark --label cx-hudnative1:native-hud-capture -- \
  bash tools/vixen-native.sh --lane hudnative1 --branch lane-hudnative1 \
    --log /home/liory/codeman-cases/undertow/reports/cx-hudnative1-evidence/native-hud-capture.log \
    bat VIXEN/application/main/main_hud_capture.bat
```

The batch route sets `VIXEN_HUD_SCRIPT=A@30,B@60`,
`VIXEN_HUD_CAPTURE_FRAMES=5,45,75`, `VIXEN_HUD_OFFSCREEN_CAPTURE=1`, and
`VIXEN_EXIT_AFTER_FRAMES=85`.

## STOPs

- `STOPPED: baseline unobtainable: native Windows Release VIXEN build at d9` —
  CMake generated a Ninja dependency on the malformed WSL path for the new
  `RecipeElementKinds.cs` input and failed before compilation. The required
  native HUD executable and frames do not exist. Owner action is the dependency
  path fix above, followed by a fresh queued Release build and capture.

## SPT DISPOSITION

No dispatched SPT task IDs were supplied. Proposed lane issue
`7556ffac92c3` is OPEN: Windows native recipe-services codegen records a WSL
dependency path. Existing SPT `T-6644` covers first-run native Ninja/toolchain
provisioning; no duplicate was filed for that existing issue.

## CONSOLIDATION ISSUES

- proposed: Windows native recipe-services codegen records a WSL dependency path (`7556ffac92c3`)
- existing, not re-proposed: Native AMD lane needs manual Ninja/toolchain provisioning (`T-6644`)
