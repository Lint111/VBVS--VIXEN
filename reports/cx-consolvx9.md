# cx-consolvx9

## STATUS

`PASS`: the consolidated VIXEN row, exact KFR/launcher/Undertow pin chain, and current-row Undertow native boot are complete and witnessed.

VIXEN source row SHA: `1136839c997727ea3eccbbea6254b9823858ed0f` (`chore(vixen): track landed c1e6dff4c kernel snapshot`).

## LANES

In:

- `lane-hudnative1` — Windows-native codegen dependency-path repair, source `a7b287e30d69`, report `cd77f2b27`.
- `lane-textshape1` — HarfBuzz Unicode buffer initialization, source `99b780253`.
- `lane-opcodeown2` — report-only d9 launcher-resolution findings.
- `lane-vxreds2` — report-only row-60/row-73 base-red investigation.

Dropped: none.

Excluded as instructed: the editor line (`lane-edmat3` and its editor descendants, `editorref1`, `layernodes1`, `layerwires1`, `edcapture1`), `lane-rsbuiltins1`, the `recipeedit2` chain, and work requiring a kernel lane outside landed kernel main.

## IMPLEMENTATION

- `VIXEN/cmake/VixenChecks.cmake` now centralizes WSL-to-native dependency conversion for codegen prerequisites while retaining WSL command arguments.
- `VIXEN/codegen/CMakeLists.txt` applies that helper to recipe-service inputs, fixing row 79.
- `VIXEN/libraries/Text/src/Shaper.cpp` explicitly sets `HB_BUFFER_CONTENT_TYPE_UNICODE` before shaping; the direct-codepoint regression is in `test_text_shaping.cpp`, fixing row 74.
- The VIXEN kernel pin moved from `597757236569835a7b40ed01ec04c06e286af723` to landed kernel `c1e6dff4c897dd3214322166d7da3e01b3cb3726` in commit `1136839c9`.
- No generated source or golden was hand-edited; the working tree is clean apart from this report before its commit.

## WITNESS

CodeGraph was attempted first; this checkout has no `.codegraph/index`, so the repository inspection used the permitted manual fallback.

- WSL Release configure with kernel `c1e6dff4c897dd3214322166d7da3e01b3cb3726`: pass. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794709948-build-cx-consolvx9:configure-release-kernel-c1.log`.
- WSL Release all-target build: recovery retry passed, 728/728 remaining actions, exit 0. All codegen goldens passed, including `SdfOpCodes.g.h matches canonical recipe opcodes`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794710094-build-cx-consolvx9:release-all-targets-retry.log`.
- The first WSL build attempt terminated at 673/1400 with only a shell `Terminated` line and no compiler diagnostic; the unchanged queued retry recovered it. This matches the existing `cx-textshape1` queue-resume consolidation proposal.
- Native Windows Release all targets: pass, 1393-action graph, `All targets built successfully`, exit 0. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794710682-build-cx-consolvx9:native-windows-release-all.log`.
- Full CPU CTest: `1738/1738` executed tests passed; disabled/skipped entries are listed by CTest. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794710523-test-cx-consolvx9:ctest-cpu-full.log`.
- Full GPU CTest: 1644 selected entries, 8 disabled entries, 17 active tests; `17/17` passed. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794710624-gpu-cx-consolvx9:ctest-gpu-full.log`.
- HarfBuzz targeted CTest: `TextShapingFixture` `9/9` passed, including `DirectCodepointInputCanBeShaped`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794712336-test-cx-consolvx9:text-shaping-targeted.log`.
- Recipe opcode mirror target: pass, exit 0, no work required. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794712360-build-cx-consolvx9:recipe-opcode-mirror.log`.
- Current-row Undertow native boot final graph: `10/10` artifacts passed, including default CTest `12/12`, composition CTest `13/13`, native fixtures `15/15`, both loader modes at 3 frames, and the evidence-negative check. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794714289-build-cx-consolvx9-r:native-boot-final-chain.log`; summary: `/home/liory/projects/undertow/.claude/worktrees/vx9u/.tmp/native-graph/run/summary.txt`.

### Look-dev goldens

Native state CTest passed `4/4`; native presentation CTest passed `4/4`, each with fresh-process repeat checks. The required 16 default 500×500 look-dev PNGs were compared to `reports/lookdev-visual/captures-shadow-on`: `16/16` byte-identical (`byte_diff=0`, `pixel_diff=0`). Logs:

- `/home/liory/.local/state/undertow/undertow-box-logs/1794711979-gpu-cx-consolvx9:native-lookdev-state-direct.log`
- `/home/liory/.local/state/undertow/undertow-box-logs/1794712053-gpu-cx-consolvx9:native-lookdev-presentation-direct.log`

### HUD hashes

The native Windows offscreen capture passed. The three required files are byte-identical to the accepted references:

- frame 5: `754bcd8e7e3e433dfc42b5635283611d55c4b8821099b19a70fb04c19a157`
- frame 45: `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801`
- frame 75: `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3`

Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794712188-gpu-cx-consolvx9:native-hud-capture.log`.

## ROW VERDICTS

- Row 60: **close / not reproduced**. Full CPU passed; the three SdiVariantMerge failures were absent, matching `cx-vxreds2`'s fresh 3/3 and full-CPU result.
- Row 73: **close / not reproduced**. The current GPU run had no starfield failure; the registered starfield captures are disabled under the current Dozen configuration, and `cx-vxreds2`'s three independent direct Dozen rounds passed exact comparison.
- Row 74: **fixed**. The HarfBuzz content-type initialization is present; targeted text shaping is 9/9 and full builds are green.
- Row 79: **fixed**. Native Windows Release all targets completed without the missing WSL-spelled prerequisite failure; WSL Release also passed.

## PIN CHAIN / BOOT

The exact current-row chain, measured from each checkout, is:

- KFR `lane-vx9kfr`: commit `ab9745825477b626975ace5aba582402c5a3ec04`; `toolchain/providers/vixen.pin -> 1136839c997727ea3eccbbea6254b9823858ed0f`.
- Launcher `lane-vx9l`: commit `a7658919d1d05cc468de3f35b195e2cfd3f991a4`; `toolchain/providers/kfr.pin -> ab9745825477b626975ace5aba582402c5a3ec04`.
- Undertow `lane-vx9u`: commit `a95ed02c2bdd87a7697c6c3644eab780f8a79e11`; `toolchain/providers/launcher.pin -> a7658919d1d05cc468de3f35b195e2cfd3f991a4` and `vixen.pin -> 1136839c997727ea3eccbbea6254b9823858ed0f`.

Final queued native boot used `KFR_ROOT=/home/liory/projects/KernelFederationRenderer/.claude-worktrees/vx9kfr` so the launcher could consume the assigned unpublished KFR commit. Both modes observed 3 frames, `scheduled=39`, `in-plan=21`, `ticked=63`, with no blocked lifecycle arms. Default and composition both produced `world=07b009ea59ade130 save=d25922a43cf0cc90`.

## STOPs

None. The initial no-override boot staging failure and one concurrent DZN `headless_session_render` segfault were both recovered by documented/provider-safe queued retries; the final graph is green.

## LANDER NOTES

- Native first-run provisioning downloaded the pinned Ninja and extracted ccache before the native build. This is already covered by the existing proposal `Windows native capture lanes require first-run Ninja and ccache provisioning before the target build` in `.spt-proposals/cx-recipecull2.jsonl`.
- The first native look-dev wrapper invocation failed in `cmd.exe` before CTest because its regex argument transport hit the known delayed-expansion quoting bug. The documented direct `ctest.exe` route recovered the witness. Existing proposal: `Native GPU wrapper cannot pass a literal-dollar duplicate CTest exclusion through cmd.exe` in `.spt-proposals/cx-consolvx8-r3.jsonl`.
- The first WSL full build termination is covered by existing `Queued full VIXEN build resumes can terminate without a diagnostic` in `.spt-proposals/cx-textshape1.jsonl`. No duplicate proposals were added.
- The first current-row boot could not resolve unpublished KFR `ab974582...` from the public branch; the documented `KFR_ROOT` exact-worktree override recovered it. New proposal: `d1f80908b6d4`.
- The first concurrent composition CTest saw one DZN `headless_session_render` segfault; the isolated named test and the complete 13-test composition set both passed, then the final native graph passed all 10 artifacts.
- Native runtime logs also contain pre-existing Vulkan debug-report lookup and cache-save warnings after the successful HUD run; they did not affect exit status or hashes.

## SPT DISPOSITION

New proposal `d1f80908b6d4` was appended to `.spt-proposals/cx-consolvx9.jsonl` for the unpublished KFR pairing gap. Existing proposal inbox entries cover the other incidental recoveries.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Windows native capture lanes require first-run Ninja and ccache provisioning before the target build (existing `cx-recipecull2` proposal)
- proposed: Native GPU wrapper cannot pass a literal-dollar duplicate CTest exclusion through cmd.exe (existing `cx-consolvx8-r3` proposal)
- proposed: Queued full VIXEN build resumes can terminate without a diagnostic (existing `cx-textshape1` proposal)
- proposed: Pin-chain helper should derive exact provider SHAs instead of hand-edited values (existing `cx-consolvx8-r3` proposal)
- proposed: Native boot does not pair an assigned unpublished KFR pin into launcher kfr-root (`d1f80908b6d4`)
