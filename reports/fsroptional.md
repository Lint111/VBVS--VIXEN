# Lane fsroptional — R648/R648.1

## LANDABLE NOW

- VIXEN commit: `487a1bf9` (`feat(vixen): make FSR3 an optional upscaler provider`).
- UndersetLauncher commit: `ad4b507` (`feat(loader): select optional upscaler setting`).
- VIXEN base: `f750d6e9d2535dfc97329584c9d067b9a9269784`; launcher base: `0f2e6d4b4790b050b71edaa0aaf22ff8e41c47ea`. Both were merged with their required origin branches before the witness attempt; both origins were already at the recorded bases.
- `VIXEN/application/main/CMakeLists.txt:18-50` detects the ignored `FidelityFX_SC.exe`, defaults `VIXEN_WITH_FSR3` ON only when it exists, forces an invalid ON request back OFF with a reason, prints one `[FSR3]` status line, and conditionally includes the SDK CMake, source, library link, shader blobs, and FSR3 test.
- `VIXEN/application/main/source/graph/UpscalerModules.cpp:1-408` and `VIXEN/libraries/VulkanResources/src/CapabilityGraph.cpp:1059-1063` omit FSR3 source registration, capability registration, and backend descriptors when the option is OFF. `test_vulkanresources_basic.cpp` asserts the capability node is absent in that configuration.
- `VIXEN/application/main/source/VulkanGraphApplication.cpp:166-182` reads the existing `VIXEN_UPSCALER` process seam. `BuildRenderGraph.cpp:9604-9613` emits one warning and falls through to FSR1/linear/native when an FSR3 request is absent from the build.
- `app/launcher/UndersetLoader/main.cpp:122-196` adds the single `--upscaler` loader setting, clears the setting when omitted, and transports explicit values through `VIXEN_UPSCALER`. The README documents the render invocation at `app/launcher/UndersetLoader/README.md:117-126`.

Static checks passed: `git diff --check`; the added-line forbidden-pattern gate found no `EntityId`, explicit `.Id`, `KindRef<All>`, new attributes, or new `throw`. The expected compiler is absent both at its SDK path and from `rg --files -g 'FidelityFX_SC.exe' /home/liory/projects /home/liory/.cache`; therefore no ON build or binary-size comparison was applicable.

Evidence directory: [w9-fsroptional-evidence](/home/liory/codeman-cases/undertow/reports/w9-fsroptional-evidence/).

## Verification

- CodeGraph was attempted first in both assigned repositories, but neither checkout has a `.codegraph/` index; the tool explicitly reported that it could not run and that initialization was not requested.
- A pre-edit build was not claimed from the first attempt: CMake reconfigured the build tree after the source edit, and the session was interrupted while compiling dependencies. An exact base source snapshot was then created from `f750d6e9` for a clean baseline attempt.
- The clean baseline build and the launcher fixture preparation were both submitted through the required live queue. They remained queued for more than 22 minutes with no admission, output, exit marker, or queue diagnostic, so only the two exact lane-owned wrappers were canceled (status 130). No raw build or test command was run.
- Consequently, VIXEN configure/build, `vixen_codegen_checks`, rendering CTest, launcher configure/build, and native boot witnesses are STOPPED by queue admission, not reported as passed. No FSR3 ON witness was possible because the compiler is not present.

### Recovery record

- A — verified `/home/liory/.local/bin/with-test-lock.sh`, `UNDERTOW_ROOT=/home/liory/projects/undertow`, the assigned working directories, and the documented VIXEN dependency cache route. The clean baseline and launcher preparation were submitted through that wrapper; the live queue did not admit either command.
- B — no generated artifact or pin regeneration was needed for this source-only change. The exact base source snapshot was prepared, but its configure could not start because the authorized queue route remained unavailable. The missing shader compiler was confirmed absent rather than provisioned or hand-waved.
- C — unavailable: without a fresh configured target, no supported narrower build/test scope could establish a trustworthy product baseline. The remaining cause is external queue admission/reservation state.

## STOPs

- `STOPPED: baseline unobtainable: required queued configure/build/test witnesses were never admitted by the live box queue after more than 22 minutes; owner action is to release/repair the stale queue reservations or rerun this lane when the queue admits commands.` This STOP covers the requested VIXEN OFF build/tests and launcher native boots. The implementation itself is committed and statically checked.

## SPT DISPOSITION

- R648/R648.1 — PARTIAL. The optional build/provider and loader seams are implemented and committed; product witnesses remain pending the queue recovery above.
- SPT proposal `985efb8b0dbc` — OPEN. It records the queue admission starvation observed by this lane.

## CONSOLIDATION ISSUES

- proposed: Queued validation can wait indefinitely behind stale box reservations — exact live-queue build/light commands waited over 22 minutes without admission, output, timeout, or reservation diagnostics; proposal `985efb8b0dbc`.
