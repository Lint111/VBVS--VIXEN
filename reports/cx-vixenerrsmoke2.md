# cx-vixenerrsmoke2

## LANDABLE NOW

The native VIXEN error-catalog scope is green on the Windows runner. The
targeted native build, smoke test, and alias-collision test passed. The
existing native capture label has an unchanged environment failure on both
the current tree and the clean base; the comparison is recorded below.

No renderer paths were changed.

## SHAs

- VIXEN wave base: `0d7cf27560878fde097a8bc278890b3a121e8a76`
- VIXEN lane before this report: `d291f13e`
- Kernel catalog provider: `ff83b958b14f02a1fc4f6a21fbd808fa3c2986a9`
- Paired Undertow consumer: `09a66f5961ba358ea01ed0786c837b3d9d6fd152`

## Native witnesses

All native commands used the shared `gpu-native` or `build` queue and the
paired provider checkouts. The native checkout was
`C:\vixen-native\vixenerrsmoke2`.

- `vixen_error_catalog_smoke` target build: exit 0; all targets built
  successfully (`native-target-build.log`).
- `ErrorCatalog.VixenCppSmoke`: passed, 1/1 through gpu-native.
- `Codegen.ErrorCatalogVixenAliasCollision`: passed, 1/1 through gpu-native.
- Smoke output: `VIXEN error catalog smoke: descriptors=285 kernel=PIPE001 vixen=VIXEN001 recovery=recreate formatted=376 bytes`.

The raw native target logs are under
`/home/liory/codeman-cases/undertow/reports/cx-vixenerrsmoke2-evidence/`; the
verbose smoke witness is `native-error-catalog-smoke-verbose.log`.

## Existing native suite

The current native `-L '^native$'` suite contains two aggregate tests, and
both fail before rendering with `vkCreateInstance failed (VkResult:
VK_ERROR_EXTENSION_NOT_PRESENT [-7])` (gpu-native exit 8):

- `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`
- `Application.PlanetCapture.RendersStatesDeterministically`

The same two test names and the same pre-render Vulkan diagnostic occur on the
clean `0d7cf275` checkout, also gpu-native exit 8. The base tests were built
from `/mnt/c/vixen-native/vixenerrsmoke2-base-native`; its comparison log is
`/home/liory/.local/state/undertow/undertow-box-logs/1793553540-gpu-native-cx-vixenerrsmoke2:native-base-existing-suite.log`.
This is a pre-existing native-runner/runtime finding, not a regression from
the error-catalog change.

## Baseline and recovery

The first fresh native build reached the codegen checks but stopped because
`codegen/error-catalog/index.json` was absent and no generating dependency was
present. Recovery regenerated the catalog through the queued
`error_catalog_regen` target, then the scoped `vixen_error_catalog_smoke`
build passed. This incidental target-wiring gap is recorded in the SPT
proposal below.

The clean-base comparison required a native C: checkout. The earlier mapped
WSL-path attempts failed before compilation or stalled in configure, so those
were abandoned as invocation/provisioning failures; the C: checkout then
configured and built both base capture targets successfully.

A fresh full native build also reported unrelated existing graph failures:

- the paired Undertow `deal_session/deal_step.Id` frame-target lineage campaign
  check;
- stale generated `VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h`;
- MSVC C1128 in the unrelated `vixen_editor` target, which would need `/bigobj`.

The requested error-catalog target does not depend on those editor/AppFlow/
frame-target checks, and its fresh targeted build passed after catalog
regeneration. Raw logs are under
`/home/liory/codeman-cases/undertow/reports/cx-vixenerrsmoke2-evidence/`.

## SPT DISPOSITION

This lane proposed `Make the VIXEN error-catalog check bootstrap generated outputs`
in `.spt-proposals/cx-vixenerrsmoke2.jsonl`.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Make the VIXEN error-catalog check bootstrap generated outputs
