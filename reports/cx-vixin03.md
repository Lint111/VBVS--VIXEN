# cx-vixin03 — VIXEN runtime-data inputs

## LANDABLE NOW

- VIXEN: complete and landable after commit `feat(vixen): load federation runtime data index v2`.
- Undertow: no files changed; no commit required.
- Yeroket kernel: no files changed; no commit required.

The VIXEN change adds a native C++ reader/store for runtime-data index v2, validates
producer pins, schema and artifact identities, owns immutable artifact bytes, and
keeps reload fail-closed until an exact interface-compatible candidate is committed
at a frame boundary. `VulkanGraphApplication` loads the bundle before graph setup and
retains the generated/compiled compatibility path for members not present in the
slice-2 producer closure. No unavailable bytes were invented.

The accepted slice-2 product was `5723f2c6172d44e1492312c17c63614ce815c60b1f954da93729f66f624281a3`
with 10 artifacts. The fixed acceptance world/save identities remain
`10fc17f580f12ea7` / `d0399b15c0de4b59`.

## WITNESS

- Codegraph-first requirement: attempted `codegraph explore`; the checkout has no
  `.codegraph` index, so the tool reported that indexing is required and source
  inspection continued with repository-local tools.
- Fresh queued Core build passed:
  `cx-vixin03:core-runtime-input-build2`.
- Queued runtime-input tests passed: 5/5, including the known SHA-256 vector and
  corrupted-member, identity, reload, and compatibility failures:
  `cx-vixin03:runtime-input-tests-final`.
- Queued VIXEN application build passed, including the pinned real SHA-256 provider:
  `cx-vixin03:sha256-fallback-build2` and `cx-vixin03:app-offline-build-final`.
- Queued targeted application GPU tests passed: 10/10:
  `cx-vixin03:app-lifecycle-gpu-tests-final`.
- Queued real slice-2 runtime bundle boot passed with `DISPLAY=:0`: the app accepted
  the product and all 10 artifacts, logged the expected compatibility warnings for
  unavailable frame-target, GPU-system, SPIR-V/reflection, and viewblob members,
  built 190 nodes/913 connections, prepared, rendered one frame, and exited cleanly:
  `cx-vixin03:runtime-data-real-bundle-boot3`.
- Queued no-kernel-delta Ninja explain passed with `ninja: no work to do`; no kernel
  generator change was made and no VIXEN compilation unit rebuilt:
  `cx-vixin03:ninja-explain-no-kernel-delta`.
- `git diff --check` passed.

The required full native Undertow default/composition/fixture run was attempted but
did not reach compilation. With `UNDERSET_BOOT_JOBS=4`, provisioning rejected the
override because the live safe limit was 3. With `UNDERSET_BOOT_JOBS=3`, queue memory
admission rejected the first compile (`2209MB` available versus a `1615MB` job plus
`1024MB` headroom). Logs:

- `/home/liory/.local/state/undertow/undertow-box-logs/1793967119-build-cx-vixin03:native-default-composition-fixtures.log`
- `/home/liory/.local/state/undertow/undertow-box-logs/1793967286-build-cx-vixin03:native-default-composition-fixtures2.log`

These are provisioning/admission failures, not product failures; no native digest
delta is claimed from this lane.

## STOPs

- VIXEN implementation: none. The targeted VIXEN scope is fresh and green.
- Native Undertow default/composition/fixture acceptance witness: unavailable in this
  lane because both authorized queued attempts were stopped before compilation by
  native job/memory admission. The VIXEN runtime-bundle boot and targeted GPU scope
  are independently green.

Recovery classification: the first Core compile red was a local helper-name error and
was fixed; the first runtime-input test red was a noncanonical fixture ordering error
and was fixed; the initial real-bundle digest red exposed the existing non-SHA fallback
in `VixenHash`, which was corrected to link the pinned stbrumme implementation. Fresh
tests and the real bundle boot passed afterward. No required target remains on stale
binaries.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native boot rejects an otherwise permitted job override only after provisioning
- proposed: VIXEN auto-derive misses assigned Undertow worktree layout

## SPT DISPOSITION

`.spt-proposals/cx-vixin03.jsonl` is committed with both proposals. No push performed.
