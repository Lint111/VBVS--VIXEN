# Lane vixtools report

Worktree: `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixtools`  
Branch: `lane-vixtools`  
Merged verification base: `1529073e471001e9fe04edb66b9d1f7c51df7d35` (`origin/wave/authoring-convergence`; merge was already up to date).

## Completed tasks

- **T-2050 — `e201a98c`** (`T-2050: Add CodeGraph worktree discovery helper`): added executable `tools/codegraph-vixen.sh` and documented it in `VIXEN/Vixen-Docs/04-Development/Build-System.md`. It checks the isolated worktree, its canonical checkout, and optional `VIXEN_CODEGRAPH_ROOT`; it does not create or commit an index. Both the lane worktree and canonical VIXEN checkout currently report `initialized:false`. The helper was exercised and exited 2 with its actionable no-index message. A live CodeGraph query remains unavailable until an index exists.
- **T-2051 — `cb3903e8`** (`T-2051: Declare WSL capture display in CTest`): added `tools/run-vixen-windowed-captures.sh`; WSL2 CTest declares `DISPLAY=:0` when the X0 socket is available and clears `WAYLAND_DISPLAY`. The `vixen_wsl_capture_witness` test passed 1/1 with caller display and Vulkan variables unset.
- **T-2052 — `65b093f8`** (`T-2052: Resolve capture apps from CMake targets`): CMake now passes capture executable directories and filenames through `$<TARGET_FILE_DIR:...>` / `$<TARGET_FILE_NAME:...>`. Capture producer tests use target-derived working directories.
- **T-2053 — `cac6771a`** (`T-2053: Add PNG capture pixel comparison tool`): added executable `tools/compare-capture-pixels.py`, which reports byte and decoded-pixel differences and returns distinct success, mismatch, and invalid-input statuses. Seven before/after capture pairs compared byte-identically and pixel-identically (exit 0). A deliberately different HUD pair reported 9,943 differing pixels and exited 1.
- **T-2069 — `b1de0357`** (`T-2069: Discover Undertow schema catalog standalone`): standalone VIXEN configure now discovers the schema catalog from supported repository locations and prints the selected absolute path; explicit `VIXEN_SCHEMA_CATALOG` remains supported. If discovery fails, the configure error names `UNDERTOW_ROOT` and `-DVIXEN_SCHEMA_CATALOG` as fixes.

## Witness

All build, configure, and test commands below were run through `tools/with-test-lock.sh` with the required resource class.

- Fresh Ninja and Unix Makefiles configurations both passed with `UNDERTOW_ROOT` unset and no `-DVIXEN_SCHEMA_CATALOG`; each reported the discovered Undertow schema path.
- The requested 22 `*_check` targets passed in the fresh Ninja tree.
- Full RenderGraph CTest passed: 1,337/1,337 tests, 11 skipped, zero failures.
- Full SVO CTest had only the expected T-1449 opcode-94 failure: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` reported `M4d_Output_IsPassthrough` capability mismatch 94. No other SVO failure remained.
- Native capture witness passed 11/11 with caller `DISPLAY`, `WAYLAND_DISPLAY`, and Vulkan environment variables unset. The before/after pixel comparison passed for all seven paired outputs.
- A final no-op Ninja rebuild completed with `ninja: no work to do` (zero units).
- The CMake configure log also printed a non-fatal `ProvisionWslVulkan` glslang-directory diagnostic; configure returned success and the required checks above passed.

The first standalone configure on the task base failed because the schema catalog had to be supplied manually; that was the T-2069 starting defect. The base was otherwise recovered and witnessed with the catalog explicitly supplied before implementation. During witness setup, sharing one FetchContent cache between Ninja and Unix Makefiles produced a glm-subbuild generator mismatch. Separate cache roots per generator resolved it; this incidental tooling gap is proposed below.

## Removed assumptions and reference check

No source files were deleted. The following hardcoded content was removed/replaced:

- `VIXEN/application/editor/CMakeLists.txt` at the T-2051 snapshot lines 81, 95, and 118 used `${VIXEN_BINARIES_DIR}` as the capture working directory; these now use target-derived output directories.
- `tools/run-vixen-windowed-captures.sh` at the T-2051 snapshot lines 26–27 constructed `VIXEN` and `vixen_editor` from a presumed runtime directory; CMake now supplies target-derived names.
- `VIXEN/codegen/CMakeLists.txt` at the base lines 131–133 only instructed standalone users to pass `-DVIXEN_SCHEMA_CATALOG=<path>/schemas.json`; automatic discovery and a fuller recovery message replaced that error.

The following checks returned no matches:

```text
rg -n 'WORKING_DIRECTORY "\$\{VIXEN_BINARIES_DIR\}"|runtime_dir/(VIXEN|vixen_editor)' VIXEN/application/editor/CMakeLists.txt tools/run-vixen-windowed-captures.sh
  => No legacy capture working-directory/executable paths remain.
rg -n 'pass -DVIXEN_SCHEMA_CATALOG=<path>/schemas.json for a standalone VIXEN configure' VIXEN/codegen/CMakeLists.txt
  => No old schema-catalog-only configure error remains.
```

## Landing actions

Do not close tasks from this lane. Recommend closing these SPT tasks at landing:

- T-2050 / P606
- T-2051 / P609
- T-2052 / P611
- T-2053 / P615
- T-2069 / P621

## Consolidation issues

- proposed: Keep VIXEN FetchContent state isolated per CMake generator
