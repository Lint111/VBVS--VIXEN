# cx-vixenrebase — VIXEN chained merge rebase

## Result

**LANDABLE NOW**

The branch contains the 42 clean VIXEN branches followed by all 14 conflict branches in the supplied age order. Each source label has one `merge(lane-<label>): ...` first-parent commit. No branch was skipped.

- VIXEN branch: `lane-vixenrebase`
- Rows: [reports/cx-vixenrebase-rows.txt](./cx-vixenrebase-rows.txt)
- Count: 56 rows = 42 clean + 14 conflict merges
- Generated FrameTargets follow-up: `8ea60e6e`
- No push performed

## Provider pairing

Final configure and FrameTargets checks used:

- Undertow `origin/main-wave` worktree: `9576d42e5fe7683ce576a7d4a44c19421bf9dc4d`
- Yeroket provider root: `94ad257ef96226e66d36be26415389f42028201a`

The regenerated FrameTargets artifacts match `origin/lane-frametargetschema` byte-for-byte by SHA:

- `FrameTargets.g.cs`: `ffd51cc5cad182236afd91ec45297c52b41795e6c95b19f4cacdab89c7d6ea59`
- `frame-targets.schemas.json`: `e5740050da9fa9aad73320987d63ffa8e12499c974a62e5dd0fdd775f97a0a71`

## Verification

All heavy commands used the live `/home/liory/.local/bin/with-test-lock.sh` queue.

- Configure: passed.
- Initial scoped build reached the FrameTargets gate and reported stale checked-in closure JSON.
- Prescribed `frame_targets_refresh`: regenerated the closure JSON and typed refs, then stopped at the existing seven `UTSYS014` GPU-system diagnostics.
- FrameTargets golden checks `frame_targets_schema_check` and `frame_targets_ref_check`: passed.
- CPU target build for `ShaderManagementTests` and `test_run_data_paths`: passed.
- `Provision.WindowingFailure`: passed.
- `ShaderManagementTests`: 47 passed, 1 disabled.
- `test_run_data_paths`: 4 passed.
- Focused script tests: `test_vixen_native` 2 passed, `test_run_vixen_windowed_captures` 4 passed, `test_build_wsl` 6 passed, `test_run_tests_split` 4 passed.

## Findings

The full scoped target build could not complete because `gpu_systems_regen` emits seven `UTSYS014` diagnostics in the existing `ExposureTonemapSystem` and `TemporalAntiAliasingSystem` signatures. The paired frametargetschema report records the same seven diagnostics against the pre-change all-of-core baseline, proving this is pre-existing and outside the FrameTargets schema/ref scope. It is not attributed to this rebase.

## Skipped branches

None.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

None.

