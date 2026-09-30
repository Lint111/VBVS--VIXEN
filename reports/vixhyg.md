# VIXEN build hygiene lane report

Base SHA: `4ed5144db467db3532db8f9a4013aa776a9e541f`

## Task status

### T-1436 — DONE

`VIXEN/codegen/CMakeLists.txt` now generates an invariant runtimeconfig for the .NET SDK CLI under the build directory, starts the CLI with `dotnet exec --runtimeconfig`, and builds CodegenTool with `InvariantGlobalization=true`. The serialized CMake target also passes `-m:1` to limit MSBuild's internal workers; `-nodeReuse:false` remains enabled. No globalization environment variable is used.

Evidence:

- Before edits, the queued `engine_codegen_tool` build failed with the .NET ICU diagnostic (exit 1; `/home/liory/.local/state/undertow/undertow-box-logs/1790794469-build-baseline-codegen.log`).
- After the fix, `cmake --build ../build/wsl-vixhyg-fresh --target lightingconfig_check --parallel 8` passed from a fresh configure. This built CodegenTool, then ran the LightingConfig drift check successfully.
- A separate queued `dotnet run --no-build --no-restore -- ... --check` against the built tool exited 0.
- A `dotnet run` that included restore/build stayed silent and was explicitly cancelled after more than three minutes (queue wrapper exit 76, payload exit 143). The supported CMake build-and-check path and the no-build run both passed.
- CodegenTool emits 26 nonfatal `NETSDK1188` locale-resource warnings when built invariant; the build has zero errors.

Commit: `b42a4346109035ff84d6b11b673656f7d9c92980`.

### T-1437 — DONE

`ShaderManagement` now links the fetched `spirv-reflect-static` target and explicitly depends on it. The target supplies the reflect include directory and builds `spirv_reflect.c`; the former manual source and include-directory wiring were removed.

Evidence:

- The initial baseline configure/build in `build/wsl` had already compiled `ShaderManagement` and its dependent test targets before this edit, so the reported first-build race did not reproduce here.
- Fresh configure: `cmake --preset vixen-wsl -B ../build/wsl-vixhyg-fresh -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json` — exit 0.
- Fresh affected build: `cmake --build ../build/wsl-vixhyg-fresh --target ShaderManagement test_rendergraph_core test_rendergraph_criticalnodes_gpurender1 --parallel 8` — exit 0.
- The build log shows `spirv_reflect.c` compiled at step 100 and `libspirv-reflect-static.a` linked at step 140, before `ShaderManagement` compiled `SPIRVReflection.cpp` at step 180 (`/home/liory/.local/state/undertow/undertow-box-logs/1790798014-build-fresh-affected-build.log`).

Commit: `93cac7c46ee36b4f5d7f52279b74137428bea201`.

### T-1438 — DONE

Registered `test_body_instance_raymarch_rtquery_compile` in `test_critical_nodes.cmake`. CTest invokes the bundled `glslc` with `VIXEN_GPU_TRACE_HOOKS=1` and `VIXEN_RTQUERY_TRAVERSAL=1`, and writes SPIR-V into the build tree.

Evidence:

- Fresh-build CTest: `ctest -R '^test_body_instance_raymarch_rtquery_compile$' --output-on-failure` — 1/1 passed.
- Requested focused regex (`HeadlessUiGraph|RenderTargetNodeConfigTest|BodyInstance|EditorDocumentRender|HitRecordReadback|TierCrossing`) — 50/50 passed with the configured Dozen ICD and SDK layer path.

Commit: `35bf296e8025993ac1e1555ea4d70c765e26e477`.

### T-1439 — REFER-TO-ORCHESTRATOR

The requested P579 reference is unavailable: `spt show P579` returned “no item P579”, and the local VIXEN sources do not define that Cornell offscreen presentation path. `BuildUIGraph` uses the existing offscreen target, while `BuildRenderGraph` still constructs the main window, swapchain, and present chain; the PNG capture helper is UI-target-specific.

Owner decision needed: should the production main graph gain an offscreen presentation option, or should the Cornell witness construct a dedicated headless render graph using the existing offscreen facilities? The item is unimplemented pending that choice.

### T-1346 — REFER-TO-ORCHESTRATOR

A strict consumer compile of the typed-node fixture reported `-Woverloaded-virtual`: `TypedNode` lifecycle methods hide `NodeInstance` overloads, and a normal derived `ExecuteImpl(TypedContext&)` override still hides inherited `ExecuteImpl()` overloads. Adding `using NodeInstance::...` inside `TypedNode` removed its own warnings but left the downstream override warnings, so that partial experiment was reverted. The warning comes from VIXEN-owned virtual methods, not third-party headers.

Owner decision needed: rename the typed lifecycle callbacks and migrate existing typed nodes, or preserve those names and require derived consumers to import the overload sets with `using` declarations. Neither public API change is settled by the task. No warning exemption or partial fix was committed.

## Recovery and nonblocking findings

- The initial CodegenTool baseline failure was the task's target failure, not an unrelated red. An online restore without a single-worker limit stalled at the five-minute queue limit. A cache-only restore completed; then `dotnet build` with `-m:1`, without offline-source flags, completed successfully. Adding `-m:1` to `engine_codegen_tool` made both the CMake target and fresh LightingConfig check pass.
- The required queue script is absent at `tools/with-test-lock.sh` in this worktree. Commands used `/home/liory/.local/bin/with-test-lock.sh`, the configured symlink to Undertow's real queue.
- CMake printed `Fatal Error: glslang directory not found` during successful configure runs, but the configure exit status was 0 and the affected build and tests passed.
- The required CodeGraph query could not run because this worktree has no `.codegraph` index and the tool said not to initialize one here; source inspection continued manually.

## Commits

- T-1436: `b42a4346109035ff84d6b11b673656f7d9c92980`
- T-1437: `93cac7c46ee36b4f5d7f52279b74137428bea201`
- T-1438: `35bf296e8025993ac1e1555ea4d70c765e26e477`

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN worktrees need a local entry point to the required build queue
- proposed: The CodegenTool CMake target must cap MSBuild internal workers
- proposed: The CodegenTool dotnet run check path can stall after a successful build
- proposed: GPU CTest should get the configured Dozen ICD and layer paths automatically
- proposed: VIXEN lane provisioning should include the CodeGraph index
