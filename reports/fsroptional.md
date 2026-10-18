# Lane fsroptional3 — R648/R648.1 optional-FSR3 witness

## LANDABLE NOW

- VIXEN optional-provider implementation: `487a1bf9`.
- VIXEN FSR3-OFF symbol-name correction: `a5e23bad` — the two optional-provider GoogleTest names at `VIXEN/application/main/tests/test_upscaler_selection.cpp:106,134` no longer export `Fsr3`-named symbols in an OFF build.
- UndersetLauncher setting implementation: `ad4b507`.
- This run adds no product-source delta. The witness/report commit is the only new change; VIXEN is at `b919f7c1` with `origin/wave/authoring-convergence` already merged, and launcher is at `31fa814` with `origin/main` and the P796-era producer pin already merged.

## Verification

Evidence: [w9-fsroptional-evidence](/home/liory/codeman-cases/undertow/reports/w9-fsroptional-evidence/).

### VIXEN FSR3-OFF

- The fresh queued full target build passed: `cmake --build .tmp/fsroptional-off --target VIXEN vixen_editor --parallel 4`, **492/492** Ninja steps, exit 0. Both `VIXEN` and `vixen_editor` linked successfully. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1792340887-build-sn-fsroptional3:vixen-full-off.log`.
- Queued focused checks from the same implementation tree remain green: `Application.UpscalerSelection` **1/1 passed**; Vulkan `VulkanResources_*` **13 passed, 1 skipped, 0 failed** out of 14. The skip is the pre-existing device/driver identity case.
- Queued one-frame Dozen boot with `VIXEN_UPSCALER` unset exited 0 and logged `Render loop exited: frame limit reached`.
- Queued one-frame Dozen boot with `VIXEN_UPSCALER=fsr3` on the OFF build exited 0 and logged exactly one requested-provider fallback at `vixen-boot-off-fsr3.log:41`: `Requested upscaler 'fsr3' is unavailable in this build; falling back to available providers`. It then compiled the fallback graph and exited on the frame limit.
- `nm -C --defined-only` over the full OFF executables and static libraries found no `FSR3` or `FidelityFX` defined symbols. Measured executable sizes: `VIXEN` **22,765,312 bytes**; `vixen_editor` **23,583,640 bytes**. Full output is in `vixen-off-full-symbol-size.log`.
- `FidelityFX_SC.exe` remains absent from `/home/liory/projects` and `/home/liory/.cache`; an FSR3-ON build is not applicable on this host.

### Launcher recovery and checks

- The merged launcher pin is P796 producer `0114d26695f6521a4ccb2609c34c71c1acd7bad8`. Queued documented preparation passed: **110 producer manifest artifacts verified**, generated `KpbPackageBlobReader.g.h`, generated the one-member KPBL fixture, and read pack version **166**. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1792340858-light-sn-fsroptional3:launcher-pin-verify.log`.
- The current queued launcher configure reproduced the required red after the producer closure passed. At `app/launcher/UndersetLoader/CMakeLists.txt:102`, KFR's explicit fixture checker requires pack SHA `b31ed7f3ceca0f7c4087803f3d104dac589aac759582fd0ccfe0776325f14d6e`, while the P796 pack is `a541661f033e6e21cd97c4755dd2923ef7968b9fda65526c769e2644f99ed3f3`. Configure exited 1 before launcher compilation. Evidence: `launcher-configure-current.log`.
- The launcher tool-only suite, which does not require the blocked CMake contract, passed **8/8** through the queue. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1792341827-test-sn-fsroptional3:launcher-tool-tests.log`.
- Launcher C++ build/CTest and launcher native boots with `--upscaler` absent and `--upscaler fsr3` could not run because configure stops at the fixture mismatch. No launcher binary was claimed.

## STOPs

- `STOPPED: baseline unobtainable: launcher configure/build/test/native-boot scope` — Recovery A (merge current `origin/main`, use the live queue) is complete; Recovery B (documented current-output preparation and KPBL/codegen regeneration) passes; the historical fixture producer `74016d37` is not a compatible fallback because its manifest has no output content hashes (`launcher-historical-output.log`). Recovery C isolates the red to the cross-repository KFR fixture contract, but that check is required and cannot be narrowed away. Owner action: publish a compatible producer/KFR fixture pair, or regenerate the KFR-owned fixture through its supported workflow. No hash or generated manifest was hand-edited.

## SPT DISPOSITION

- R648/R648.1: **PARTIAL**. VIXEN implementation, full OFF app/editor build, both VIXEN boots, fallback behavior, symbol scan, sizes, launcher output preparation, and launcher tool tests are complete. Launcher product build/CTest/boots remain pending the producer/KFR fixture contract decision. No new source implementation is required from this lane.
- Existing run-1 queue proposal `985efb8b0dbc` remains open; this run did not duplicate the generic queue-starvation issue.

## CONSOLIDATION ISSUES

- proposed: VIXEN USE_CCACHE OFF does not clear cached compiler launchers (`1cbd8a7177bc`).
- proposed: Launcher producer pin can stage output rejected by the pinned KFR fixture (`7bd87488e507`).
