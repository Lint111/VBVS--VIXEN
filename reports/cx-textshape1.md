# cx-textshape1 — TextShapingFixture HarfBuzz abort

## Register

> 74: five `TextShapingFixture` tests abort at HarfBuzz `hb-buffer.hh:578` in the GPU CTest set (cx-vxreds2 on VIXEN wave 002a5f6b6; report `~/codeman-cases/undertow/reports/cx-vxreds2.md`). Earlier lanes (cx-opcodeown1, cx-consolvx8) saw text-shaping reds and assumed they were registered; they were not.

## Result

Fixed and committed in the caller. `Shaper.cpp` now sets `HB_BUFFER_CONTENT_TYPE_UNICODE` on every fresh HarfBuzz buffer before the existing low-level `hb_buffer_add` loop. Added `TextShapingFixture.DirectCodepointInputCanBeShaped`, which exercises a one-codepoint direct-input shape and checks the glyph ID and cluster.

## Root cause

The fetched HarfBuzz source is tag `14.5.0` (commit `863d3f7787c6df18d20e4535c5906bf3eb803bd5`). At `.tmp/fetch/harfbuzz-src/src/hb-buffer.hh:578`, `assert_unicode()` requires either Unicode content or an empty invalid buffer:

```
assert ((content_type == HB_BUFFER_CONTENT_TYPE_UNICODE) ||
        (!len && (content_type == HB_BUFFER_CONTENT_TYPE_INVALID)));
```

VIXEN created a fresh buffer, set direction/script/language/cluster level, then called `hb_buffer_add` directly. HarfBuzz’s `hb_buffer_add` implementation appends the item but does not set `content_type`; the nonempty buffer therefore remained `INVALID`. The subsequent shaping path called `assert_unicode()` and aborted. This was not buffer reuse, invalid clusters, a missing font blob, or a threading race.

The one-line fix is at `VIXEN/libraries/Text/src/Shaper.cpp:104`. The regression is at `VIXEN/libraries/Text/tests/test_text_shaping.cpp:108`.

## Wave bound

Base: `002a5f6b6adf53644e53ef947c16538440497411`.

The relevant source, test, and dependency diffs are empty for `c27282c48..3fb71ea3a` and `3fb71ea3a..002a5f6b6`. All three waves pin HarfBuzz `14.5.0`. The shaping feature was introduced by `3b6d312ca` (`Add preset-aware text shaping and bidi support`), so this is an original caller defect, not a dependency-version regression.

The added regression was built and run against the unchanged `3fb71ea3a` worktree before the fix; it aborted at `hb-buffer.hh:578` (exit 8), log:

`/home/liory/.local/state/undertow/undertow-box-logs/1794693397-gpu-cx-textshape1:old-3fb-regression-before.log`

## Verification

All build and GPU commands used the live `/home/liory/.local/bin/with-test-lock.sh` queue.

- The five original tests each passed 3/3 after the fix. Logs: `1794690990`, `1794691006`, `1794691023`, `1794691038`, and `1794691053` under `/home/liory/.local/state/undertow/undertow-box-logs/`.
- `DirectCodepointInputCanBeShaped` passed 3/3; log `1794691069-gpu-cx-textshape1:fix-repro-6.log`.
- The configured VIXEN build completed with exit 0; log `1794692356-build-cx-textshape1:build-full-gpu-scope-final.log`.
- Full GPU CTest completed with exit 0: `100% tests passed, 0 tests failed out of 1503` (1628 selected including skipped/disabled tests). Log `1794693066-gpu-cx-textshape1:full-gpu-ctest-retry.log`.
- The full log shows all six text-shaping tests passing.
- HUD/UI capture fixtures were unchanged: `HudRenderCapture.*`, `vixen_hud_capture_producer`, and `vixen_text_shader_capture_producer` were explicitly disabled in this configured suite, so no capture bytes were regenerated. Git status contains no capture or UI asset changes.

The first full-build queue invocation hit the live queue parser’s `unexpected argument 'true'` while parsing `--describe true`; setting `UT_QUEUE_HOST_CPU_WSL=0` on the same queued command recovered it. Three resumed builds were then externally terminated without a compiler diagnostic before the final queued retry completed. These are recorded as consolidation proposals below, not product failures.

## REDS

None remaining in the requested product scope. The pre-fix HarfBuzz abort is resolved. The queue/provisioning incidents were recovered and are documented as consolidation issues.

## SPT DISPOSITION

No SPT task was opened or closed. The required `spt search` could not run because this checkout has no `.spt/config.yaml`; the lane-local inbox was written with `spt propose` and validated with `spt propose --check`.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Live box queue rejects its documented boolean host-CPU fallback
- proposed: Queued full VIXEN build resumes can terminate without a diagnostic
