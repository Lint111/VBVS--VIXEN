# Lane fsroptional2 — R648/R648.1 witness

## LANDABLE NOW

- VIXEN optional-provider implementation: `487a1bf9`.
- VIXEN witness correction: `a5e23bad` — renamed the two optional-provider GoogleTest cases at `VIXEN/application/main/tests/test_upscaler_selection.cpp:106,134` so an FSR3-OFF test binary does not export `Fsr3`-named symbols. Assertions and behavior are unchanged.
- UndersetLauncher setting implementation: `ad4b507`; the lane also merged current `origin/main` at `c5851f1` (producer pin `0114d266`).
- Both assigned repositories are clean after their commits. No EntityId fields/params/casts, explicit `Id` member, `KindRef<All>`, new attribute/declaration form, or new warning was introduced by this witness correction.

## Verification

Evidence: [w9-fsroptional-evidence](/home/liory/codeman-cases/undertow/reports/w9-fsroptional-evidence/).

### VIXEN FSR3-OFF

- Queued CMake configure passed with `-DBUILD_TESTS=ON -DVIXEN_WITH_FSR3=OFF -DVIXEN_CODEGEN_GATE=ON`; `CMakeCache.txt` records `VIXEN_WITH_FSR3:BOOL=OFF` and configure printed `[FSR3] OFF: shader compiler missing; optional provider defaults off`.
- No aggregate `vixen_codegen_checks` target exists in the generated target list; the configure completed the individual codegen targets.
- Queued focused build passed for `test_upscaler_selection` and `test_vulkanresources_basic`.
- Queued `Application.UpscalerSelection`: **1/1 passed** after the symbol-name correction.
- Queued Vulkan `VulkanResources_*` CTest: **13 passed, 1 skipped, 0 failed** out of 14. The skip is the pre-existing device/driver identity case.
- `nm -C --defined-only` over the available FSR3-OFF executables and libraries found **no `FSR3` or `FidelityFX` defined symbols**. Measured focused binary sizes: `test_upscaler_selection` 664,192 bytes and `test_vulkanresources_basic` 1,272,160 bytes.
- `FidelityFX_SC.exe` was absent from `/home/liory/projects` and `/home/liory/.cache`; no FSR3-ON build was applicable.

### Required checks that did not complete

- VIXEN `VIXEN` and `vixen_editor` full target builds were submitted through the live queue. The broad attempt reached 582/679, and the final direct-compiler/no-PCH recovery reached 47/522; each ended with queue `STALL_KILLED` / exit 75 and no compiler diagnostic. Recovery also tried disabling ccache, clearing cached compiler launchers, serial compilation, and disabling PCH. The editor target could not start because its VIXEN dependency remained incomplete.
- Launcher output preparation for merged producer `0114d266` passed and produced pack version 166. The pinned KFR contract fixture then rejected the pack: expected SHA-256 `b31ed7f3ceca0f7c4087803f3d104dac589aac759582fd0ccfe0776325f14d6e`, got `a541661f033e6e21cd97c4755dd2923ef7968b9fda65526c769e2644f99ed3f3`.
- The documented launcher CMake configure was attempted through the queue and failed at `app/launcher/UndersetLoader/CMakeLists.txt:102` with the same fixture mismatch after verifying 110 pinned producer artifacts. Launcher build/tests and native boots with `--upscaler` absent or set were therefore not run.

## STOPs

- `STOPPED: baseline unobtainable: required VIXEN app/editor full build` — the supported live queue repeatedly stall-killed quiet native compilation despite configure and focused dependency/test checks passing. The required full target cannot be narrowed away; owner action is a queue/watchdog recovery and rerun.
- `STOPPED: baseline unobtainable: launcher producer/contract closure mismatch` — current launcher `origin/main` pins producer `0114d266` while the pinned KFR fixture requires a different pack. The supported exact historical producer recovery was also unusable because its manifest lacks output hashes for current materialization. Owner action is to publish a compatible producer/fixture pin pair; no generated manifest or fixture was hand-edited.

## SPT DISPOSITION

- R648/R648.1: **PARTIAL**. Optional FSR3 code and loader setting are committed; focused FSR3-OFF checks are green. Full VIXEN app/editor, launcher, native boot, and FSR3-ON witnesses remain pending the STOPs above.
- Existing run-1 queue proposal `985efb8b0dbc` remains open; this run did not duplicate the already-covered generic queue starvation issue.

## CONSOLIDATION ISSUES

- proposed: VIXEN USE_CCACHE OFF does not clear cached compiler launchers (`1cbd8a7177bc`).
- proposed: Launcher producer pin can stage output rejected by the pinned KFR fixture (`7bd87488e507`).
