# cx-vxcouple11

STATUS: STOPPED for the coupled d11 remote-boot handoff. The editor row and all pin commits are complete. WSL VIXEN codegen and CPU/GPU witnesses pass, but the required Windows Release, fresh two-material bake parity, and native composition gates remain red.

## VIXEN row

- Row commit: `196977c31bba0a9778623baa41da1f9b5ac95e9a` (`SEM_COUNT` 17 -> 18 in `libraries/SVO/tests/test_channel_format.cpp`).
- Tracked kernel pin: `a2614dee46a87d57926eb4cb8a94ac449b718e99` in `codegen/CMakeLists.txt:208`.
- The row commit is the VIXEN provider SHA used by the d11 chain. The later VIXEN report/proposal commit is report-only.

## Pin chain

- KFR: `42b6ef6d522793fb91dfd4c6e07f6c8a033a98b8`, from `500c99b75`, `vixen.pin` -> `196977c31`.
- Launcher: `38068a65d9f8d1562bdcc79fc3649dab889fc502`, from `origin/main` `db0bd815c`, `kfr.pin` -> `42b6ef6d`.
- Undertow merge: `788fb7e0e126c8e3d7de4904bceea9406f76cd03`, merging `origin/main-wave` `aa6b95edf`.
- Undertow pin: `fe557963dbae0fb2faca8871b69c9364eb7de69b`; final lane tip `e86673f7c` also registers the d11 KFR provider test in the native test set.

## Witness

- WSL Release build: pass; fresh build used the tracked kernel pin. VIXEN codegen: pass, `40/40`, including `recipe_opcode_mirror_check`.
- Full split CTest: CPU `1762 passed, 0 failed`; GPU `1510 passed, 0 failed` (GPU correctness shards all passed; 234 tests were skipped by capability selection).
- WSL HUD frames 5/45/75: pass and equal to the d10 WSL references:
  `8ef581c78331f95049ebdc421687ce36993082ceb948067f75bba7b5a8fcab50`,
  `1707ebe27c537dd43704c7760030f7359616eb95264739d790cef475c4e9a02b`,
  `6ac8a82e172accd62508a38e805af76e63e0389c866d7cbd15ce9dd96b682640`.
- Windows Release: STOP. The queued native Release build reached generated `RecipeSimd.g.hpp` and failed MSVC because Windows SDK macros rewrite the generated enum member `SetPort` to `SetPortA`/`SetPortW` (`C2838`, `C2676`). This is a new/unregistered generated-code portability red; no generated file was hand-edited. Log: `/mnt/c/vixen-native/vx11row/.tmp/native-build/vixen_build_-cx-vxcouple11-windows-release-r2-.log`.
- Look-dev goldens and a same-environment base capture were not accepted: the native Release red prevents the required native look-dev witness, and no matching d11 base checkout was available. The WSL HUD witness above is separate and does not substitute for native look-dev parity. The vx8 native HUD references were not compared with WSL output.
- Author CLI Release: pass after fresh R418 regeneration. Fresh bake parity: neutral pass, default-only hull pass, two-material STOP; VRC1 differs from the author bake at offsets 169 and 829 (the test reports first difference 169; both files are 1220 bytes). Log: `/home/liory/codeman-cases/undertow/reports/cx-vxcouple11-evidence/bake-parity-test-r2.log`.
- Undertow R418 witness: `check-content-codegen.sh --write` passed `246/246`; the seven R418 outputs were restored before the Undertow commit. Author build and all three bake commands were queued.
- Undertow native boot used the local d11 provider overrides and `KFR_ROOT` override. Provider product build, native build, fixtures `15/15`, and default CTest `15/15` passed. Composition CTest failed `headless_session_render` because `contract/assets/ui/main_menu.rml` is absent; the new d11 `headless_session_render_missing_document` test passed. Both loaders nevertheless ran three frames and produced `world=a6448a0b09e6a69d save=acc85991219fa8cb`; because the required composition gate failed, these are not accepted digest parity results against d10 `world=07b009ea59ade130 save=d25922a43cf0cc90`. Log: `/home/liory/codeman-cases/undertow/reports/cx-vxcouple11-evidence/undertow-native-boot-d11-r5.log`.
- The live queue also hit the registered row-83 `unexpected argument 'true'` path; the documented workaround `UT_QUEUE_DYNAMIC_FLOW=0 UT_QUEUE_JOBSERVER=0` was used. It is not attributed to this row.

## STOPs

1. Fix the generated Windows identifier/macro collision for `SetPort`, then rerun the native Release build and native look-dev witness.
2. Resolve the fresh d11 two-material VRC1/editor-author byte mismatch at offsets 169/829, then rerun all three parity cases.
3. Restore or deliberately contract the native composition UI asset `contract/assets/ui/main_menu.rml`, then rerun native boot and compare accepted digests to d10. The current red is in the registered native-composition/missing-UI family (base-reds row 80 is marked fixed-landed), but this run still fails and cannot be waived.

## FOR THE LANDER

Exact d11 inputs are: kernel `a2614dee46a87d57926eb4cb8a94ac449b718e99`; Undertow final lane tip `e86673f7c` (pin commit `fe557963dbae0fb2faca8871b69c9364eb7de69b`); launcher `38068a65d9f8d1562bdcc79fc3649dab889fc502`; VIXEN row `196977c31bba0a9778623baa41da1f9b5ac95e9a`; KFR `42b6ef6d522793fb91dfd4c6e07f6c8a033a98b8`.

Before a d11 remote boot, VIXEN `196977c31` must be pushed/reachable in the VIXEN authoring-convergence wave and KFR `42b6ef6d` must be pushed/reachable in its corresponding d11/authoring-convergence wave. The launcher and Undertow pins then need to resolve those remote SHAs. No push was performed by this lane.

## SPT disposition

No user-owned SPT task was worked. Open proposals filed by this lane:

- `d7fe7c9ef5b0`: KFR provider pin changes do not derive the Undertow native composition test set.
- `cecc293b8679`: VIXEN native wrapper should make the requested configuration explicit.
- `8d61855acc42`: Editor VRC1 bake parity needs a reusable d11 fixture contract.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: KFR provider pin changes do not derive the Undertow native composition test set.
- proposed: VIXEN native wrapper defaults to Debug unless the Release configuration is explicitly supplied.
- proposed: Editor VRC1 bake parity required manually reconstructed d11 manifest/render-recipe/material fixtures and output placement outside the scanned pack.
- existing: the live full-width queue path still passed boolean `true` and required the registered row-83 environment workaround.
