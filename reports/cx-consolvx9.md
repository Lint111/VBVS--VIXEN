# cx-consolvx9

## STATUS

`PARTIAL / STOPPED`: the consolidated VIXEN row is implemented and all VIXEN-local build, test, native capture, HUD, and look-dev witnesses pass. The required Undertow native boot with this row's VIXEN SHA and the corresponding KFR/launcher/Undertow scratch pin-chain commits were not run because this dispatch assigned only the VIXEN worktree; worker isolation forbids editing the unassigned provider worktrees.

Source row SHA before this report commit: `1136839c9977` (`chore(vixen): track landed c1e6dff4c kernel snapshot`).

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

The authoritative landed d9 chain is present read-only in the assigned wave state:

- KFR d9 landed commit: `3d36f1d82931b0f9568687f1ad8347ef0af4673a`.
- Launcher d9 landed repin: `c22729d367fbfd284396fe5393bdc7d9d523eae6`.
- Undertow d9 landed launcher-pin closure: `b31f395915c60865db02f23aba38f9853e077e84`.

That landed chain still points its VIXEN provider at d9 VIXEN `002a5f6b6adf53644e53ef947c16538440497411`, not this row's `1136839c9977`. The requested next chain would require KFR `vixen.pin -> 1136839c9977`, launcher `kfr.pin -> 3d36f1d82931...`, and Undertow `launcher.pin -> c22729d367...` plus `vixen.pin -> 1136839c9977`.

The already-landed d9 boot evidence reports both default and composition modes green with the expected digests `world=07b009ea59ade130` and `save=d25922a43cf0cc90` (`cx-landd9.md`, evidence under `/home/liory/codeman-cases/undertow/reports/cx-landd9-evidence-expanded-fix/batch-d9/`). It is not claimed as a current-row boot because the provider chain was not retargeted to `1136839c9977`.

## STOPs

1. **Required current-row Undertow boot and pin-chain commits are blocked by worktree authority.** No KFR, launcher, or Undertow scratch worktree was assigned in this brief, and the worker rules prohibit creating/switching/editing unassigned worktrees. Owner options: assign those three scratch worktrees and authorize the pin commits plus queued two-mode boot, or accept the landed d9 boot as sufficient for these source changes after reviewing that the VIXEN-local witnesses are boot-neutral.

No product red remains in the VIXEN-local required scope. The first WSL termination, native provisioning, and native wrapper regex failure were recovered and are not STOPs.

## LANDER NOTES

- Native first-run provisioning downloaded the pinned Ninja and extracted ccache before the native build. This is already covered by the existing proposal `Windows native capture lanes require first-run Ninja and ccache provisioning before the target build` in `.spt-proposals/cx-recipecull2.jsonl`.
- The first native look-dev wrapper invocation failed in `cmd.exe` before CTest because its regex argument transport hit the known delayed-expansion quoting bug. The documented direct `ctest.exe` route recovered the witness. Existing proposal: `Native GPU wrapper cannot pass a literal-dollar duplicate CTest exclusion through cmd.exe` in `.spt-proposals/cx-consolvx8-r3.jsonl`.
- The first WSL full build termination is covered by existing `Queued full VIXEN build resumes can terminate without a diagnostic` in `.spt-proposals/cx-textshape1.jsonl`. No duplicate proposals were added.
- Native runtime logs also contain pre-existing Vulkan debug-report lookup and cache-save warnings after the successful HUD run; they did not affect exit status or hashes.

## SPT DISPOSITION

No SPT task ID was assigned or worked. Existing proposal inbox entries cover each incidental recovery above; no new `.spt-proposals` file was necessary.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Windows native capture lanes require first-run Ninja and ccache provisioning before the target build (existing `cx-recipecull2` proposal)
- proposed: Native GPU wrapper cannot pass a literal-dollar duplicate CTest exclusion through cmd.exe (existing `cx-consolvx8-r3` proposal)
- proposed: Queued full VIXEN build resumes can terminate without a diagnostic (existing `cx-textshape1` proposal)
- proposed: Pin-chain helper should derive exact provider SHAs instead of hand-edited values (existing `cx-consolvx8-r3` proposal)
