# cx-assertsweep1

STATUS: COMPLETE WITH FINDINGS

## SHAs

| checkout | base SHA | implementation commit |
|---|---|---|
| VIXEN | `6444d7c2bcb3bdee03275e0fc808c58ddedb0565` | `ecac2829d` |
| Yeroket-Fantasy kernel | `061443bb162472db11f856403ea25302befe622e` | `8c5877a5e` |
| Undertow native | `aa6b95edf92bac2442ce378cb4701dc030eeb101` | `141d546b6` |

The required upstream merges were already up to date before verification: VIXEN
`origin/wave/authoring-convergence`, kernel `origin/main`, and Undertow
`origin/main-wave`.

## INVENTORY

Counts are the number of Release-sensitive `assert(...)` checks in the base
source. Line lists are base-source line numbers.

| repo | test source | checks | base lines |
|---|---|---:|---|
| VIXEN | `VIXEN/libraries/RenderGraph/tests/Nodes/test_exposure_meter_mirror.cpp` | 40 | 126, 134-137, 150, 154, 166, 174, 177, 189, 194, 196, 204-205, 209-210, 223, 226, 238, 243-244, 246, 252, 257-258, 263-265, 278, 282-283, 286-288, 291, 300, 308, 311, 315 |
| VIXEN | `VIXEN/libraries/RenderGraph/tests/Nodes/test_spatial_reuse_history_mirror.cpp` | 5 | 40, 43, 45-47 |
| VIXEN | `VIXEN/libraries/RenderGraph/tests/test_field_extraction.cpp` | 11 | 168, 176-177, 185, 193, 204, 225-227, 248-249 |
| VIXEN | `VIXEN/libraries/RenderGraph/tests/test_resource_gatherer.cpp` | 9 | 33, 38, 158, 191, 232, 254, 284, 305, 346 |
| kernel | `Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/Fixtures/NativeFrameCleanup/native_boot.cpp` | 9 | 65, 70, 72, 80, 94-98 |
| kernel | `Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/Native/ErrorCatalogCppConsumer/consumer.cpp` | 13 | 38-45, 53, 55-58 |
| Undertow | `core/tests/Fixtures/NativeQueryPaths/native_boot.cpp` | 30 | 81, 111, 132-134, 139-142, 148, 156, 161-162, 164, 172-174, 176, 192-193, 200-202, 210, 246, 252, 255, 273-275 |
| Undertow | `core/tests/native/PhysicalFieldTransferWitness.cpp` | 17 | 13, 15-23, 62-64, 66, 68-70 |

Total inventory: VIXEN 65, kernel 22, Undertow 47; 134 checks overall.
C++ `static_assert` declarations were not counted: they are compile-time
checks, not NDEBUG-dependent runtime assertions.

## CONVERTED

- VIXEN: all 65 checks now use `VIXEN_TEST_CHECK` in a new always-on helper.
  The helper prints the condition and source location, then aborts on failure.
- Kernel: all 22 checks now use `YEROKET_TEST_CHECK`. The native fixture
  staging test copies its helper header with the fixture source.
- Undertow: all 47 checks now use `UNDERTOW_TEST_CHECK`. The helper is kept
  in the NativeQueryPaths fixture and listed in its fixture sources so native
  staging carries it.
- Production-code assertions were not changed.

## LINT

- VIXEN added `VIXEN/cmake/tests/test_native_assertions.cmake`, registered as
  `Native.NoReleaseDisabledAssertions` with the CPU CTest label. It scans
  native test sources for lowercase `assert(` / `RMLUI_ASSERT(` and requires
  a framework assertion or an always-on check macro.
- Kernel added `NativeAssertionLintTests.cs`. It scans native test source
  extensions and includes a scratch assert-only source test; the seeded source
  is rejected and deleted without entering the repository.
- VIXEN direct lint and CTest lint passed. The full Release CTest also passed
  `Native.NoReleaseDisabledAssertions`. Kernel lint passed 2/2.

## WITNESSES

All commands were run through the global box queue.

- VIXEN baseline, before semantic edits: targeted Release CTest executed
  `test_spatial_reuse_history_mirror` and `test_field_extraction` (2/2
  passed); `test_exposure_meter_mirror` was disabled and
  `test_resource_gatherer` was not observed in that CTest tree.
  Baseline log:
  `1794779291-test-cx-assertsweep1:baseline-ctest.log`.
- VIXEN targeted after conversion: 4/4 passed, including the exposure mirror
  now classified into the CPU partition:
  `1794780325-test-cx-assertsweep1:vixen-after-ctest.log`.
- VIXEN clean Release full build passed 733/733 steps, including codegen
  golden checks and asset publication:
  `1794785503-build-cx-assertsweep1:vixen-full-cpu-build.log`.
- VIXEN final full Release CTest scheduled 1,762 entries; 1,740 executed and
  all 1,740 passed. The other 22 were Disabled or Skipped. The four converted
  tests and the lint were among the passing tests:
  `1794786523-test-cx-assertsweep1:vixen-full-cpu-ctest-retry.log`.
- Kernel Release build passed. Focused native consumer tests passed 16/16:
  `1794780019-test-cx-assertsweep1:kernel-native-consumers-retry3.log`.
- Undertow field-transfer witness passed its 16-tick check:
  `tools/check-field-transfer-witness.sh`.
- Undertow native boot staging passed 25/25 configured tests; native build
  passed all 835 build units. Default native CTest passed 12/12:
  `1794784782-test-cx-assertsweep1:native-boot-ctest-default.log`.
  Native fixture CTest passed 15/15, including `native_query_paths`:
  `1794784827-short-cx-assertsweep1:native-boot-ctest-fixtures.log`.

## NEWLY EXPOSED FAILURES

None. Every converted test passed with an always-on check.

The optional native compose CTest separately reproduced
`headless_session_render` as 1 failure out of 13. This is outside the
converted sources and matches the documented base windowing/view priming
defect (`reports/vel-937-kernel.md`, `reports/op-d7bheld.md`, and
`reports/sn-bootprod1.md`). Its optional queued compose boot phase was
cancelled after the CTest result because the GPU queue was blocked; no
required changed-scope witness depended on it.

## RECOVERY / FINDINGS

- The first queued VIXEN baseline invocation used the documented positional
  `Release` form but the wrapper rejected it before compilation (exit 2,
  log `1794778654-build-cx-assertsweep1:baseline-build.log`). Retrying with
  the wrapper's accepted `--target` form recovered the baseline.
- Repeated VIXEN target arguments built only the first target, so separate
  queued target builds were used.
- The exposure mirror initially remained GPU-classified; its header-only
  CPU target was given explicit include paths, then recovered missing Vulkan
  and GLM include directories in two queued retries.
- The first kernel consumer run lacked `UNDERTOW_ROOT`; setting the assigned
  Undertow checkout recovered the test.
- A native-boot graph hit a live queue-core mismatch before compilation;
  a queued health probe passed and the graph was retried.
- Native fixture staging initially omitted the helper header; the helper was
  moved into the fixture and declared in fixture sources.
- The first full VIXEN CPU CTest used a stale partial tree and reported eight
  missing executables/asset-manifest failures. A fresh queued full build
  materialized the targets and assets; the final full CTest then passed.

These were recovered environmental/provisioning findings, not assertion
conversion failures.

## STOPS

None. No converted test started failing, and no required verification scope
remains red.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Build wrapper rejects its documented Release positional argument
  (`741859983caf`)
- proposed: Build wrapper does not reliably dispatch repeated target arguments
  (`0c3d6fa29da`)
- proposed: Header-only CPU mirrors need a declarative CTest classification
  (`6af15ccac633`)
- proposed: CPU CTest must materialize all registered targets before a full
  Release witness (`6947d256179e`)
- proposed: Kernel native consumer tests require an implicit Undertow root
  environment (`ea56befb23dc`)
- proposed: Native dispatcher fixture should carry helper headers with copied
  fixture sources (`435e1afc66e`)
- proposed: Native boot graph should pin queue-core compatibility for the whole
  run (`ddfcb29bae3`)
- proposed: Native fixture staging should include headers referenced by fixture
  sources (`80c395f84627`)

SPT search was unavailable because this checkout has no `.spt/config.yaml`;
the proposals were appended to each lane's `.spt-proposals/cx-assertsweep1.jsonl`.

