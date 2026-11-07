# cx-vixexport1 — immutable VIXEN/KFR provider export

## LANDABLE NOW

- Launcher `lane-loader06`: `4d04fa9fc57f364132180344f292204ed2b1a508` — adds the invariant, launcher-owned `Underset.Tooling.NativeProduct` publisher client and its contract test.
- VIXEN `lane-vixexport1`: `0986621aea5c04c2ad05f5c5a2fefc6c1762ab3e` — includes the required `lane-vixrebuild1` merge and the regular-file native provider install closure.
- KFR `lane-vixexport1`: `14af9a6caf175f23ae6af456e8be34b639aecd18` — adds the opt-in CMake staging/publish target and the generator-refresh commit `9569d66`.
- Undertow `lane-loader06`: `a07726a246dd6838a35346b7a573829ee7098d0b` — defaults prepared native boot environments to the launcher product client.

Required upstream merges were performed once before final checks; all four worktrees are clean.

## WITNESS

- Clean KFR/VIXEN configure with exact merged pins passed.
- Queued `kfr_native_product_export` cold export passed. Product namespace: `clean`; full key: `edb28ff79437d719fc964a618a63a4dbf7b2945193a93d871cc5461c041e6f9c`.
- Manifest contains the KFR, VIXEN, and Gaia commits, compiler/toolchain/profile/features, SHA-256 and size for every output, and 491 outputs. Staged and published payloads contain no symlinks.
- A forced second same-key export passed as `Reused` with the identical full key.
- Launcher full suite passed: 36/36. Native product client contract test passed and versioned `.so.N` output classification was exercised.
- Undertow `test-native-product-forwarder.sh` passed and `test-native-boot-prepare.sh` passed.
- VIXEN `test_stage_gpu_schema_source.cmake` passed: identical staging preserved timestamps and deleted sources were removed.

## MEASUREMENTS

- Initial cold producer closure compiled 714 Ninja steps; the final clean export reused the built closure and completed the codegen/export path without a source rebuild after the merged-pin configure.
- Final published payload: 491 files, including archives/libraries, headers, and native metadata.
- The queue recorded a 24,839 MB native-build estimate/organic peak without hitting a memory cap.
- The required second Undertow lane default/composition/fixtures run, zero VIXEN/KFR compilation-unit count, under-60-second staging timing, and accepted default/composition/fixture/world/save digests were not run in this lane.

## STOPs

- No blocking stop for the producer/client scoped change. The two-lane end-to-end loader claim remains unverified here; the existing `check-native-boot.sh` builds source views and is not a product-consumer witness for that claim.

## CONSOLIDATION ISSUES

SPT proposal submission was unavailable because no `.spt/config.yaml` exists above the assigned worktrees. Fallback proposals:

- The launcher native-product apphost required an explicit invariant-globalization project setting because the witness host has no ICU runtime; the launcher tooling project should make its host-runtime requirements explicit. | why: apphost aborted before publication with missing ICU | file: `UndersetLauncher/app/framework/Underset.Tooling.NativeProduct/Underset.Tooling.NativeProduct.csproj` | suggested consolidation: publish a host-runtime contract or invariant tooling profile for launcher-owned command clients.
- The queue failure breaker keys only the KFR tree, so a VIXEN-only fix required `--no-breaker` to rerun the exact KFR export command. | why: cross-repo producer inputs are absent from the breaker identity | file: queue invocation `cx-vixexport1:kfr-native-product-export-build-regular-files` | suggested consolidation: let queued commands declare all cross-repo input revisions in the replay/breaker key.
- A fresh KFR export configure exposed stale `CoreAppFlow.g.h` and required the documented `kfr_appflow_regen` target before the export could build. | why: export configuration does not enforce or refresh generated appflow output before its required check | file: `KernelFederationRenderer/codegen/CMakeLists.txt` | suggested consolidation: make the native export target depend on the generated-output contract or provide one producer preflight target.
