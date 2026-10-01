# vixmake — VIXEN CodegenTool under Unix Makefiles

Lane: `vixmake`  
Base: `4a4c447359da42ce69b11f2fca7dd830f0bdd34b`  
Implementation commit: `a6e082001b760e262cb87bc6779390b28e2f7477`

## Change

`VIXEN/codegen/CMakeLists.txt` now declares the local `CodegenTool.dll` as a real
`OUTPUT` alongside the `.complete` stamp. The DLL remains a file dependency of
`engine_codegen_tool` and of every codegen check. Its `.deps.json` and
`runtimeconfig.json` remain `BYPRODUCTS` because no target depends on those paths.
The materialization command and stamp gating are unchanged.

Makefiles only gets a file rule for a dependency when CMake declares it as an
output. Ninja also tracks declared byproducts, which is why earlier VIXEN Ninja
witnesses did not expose the missing DLL rule. Declaring both real outputs keeps
the check dependencies intact and retains no-op behavior under both generators.

The required CodeGraph-first query was attempted in both VIXEN and KFR, but each
checkout has no `.codegraph` index. The tool instructed me not to initialize one,
so I used the allowed source-reading fallback.

## CMake dependency audit

I scanned the repository-owned `CMakeLists.txt` and `*.cmake` files under
`VIXEN` for custom commands with `OUTPUT`/`BYPRODUCTS` and custom targets or
commands whose `DEPENDS` refer to those files. The only dependency on a
custom-command byproduct was the CodegenTool DLL described above; it is now an
`OUTPUT`. No other VIXEN-owned cross-directory custom-command output dependency
was found.

All other matched output-to-dependency edges are local to their CMake source and
binary directory:

- `VIXEN/cmake/VixenAssets.cmake`: the asset-stage target depends on its
  command's `_stage_stamp` output.
- `VIXEN/cmake/VixenChecks.cmake`: checked targets depend on their command's
  `_stamp` output. The codegen checks use this helper and depend on the CodegenTool
  DLL and stamp from the `codegen` directory.
- `VIXEN/libraries/RenderGraph/tests/test_critical_nodes.cmake`: the
  `body_instance_raymarch_spv`, `_b1`, and `_b2` targets; `shell_derive_spv`;
  `recipe_instance_bucketing_spv`; `hiz_downsample_spv`;
  `instance_occlusion_cull_spv`; `proxy_interval_prepass_spv` (vertex, fragment,
  and compute outputs); and `shadow_ray_trace_spv` each depend on outputs declared
  beside them in that same file/directory.
- `VIXEN/libraries/ShaderManagement/cmake/ShaderToolUtils.cmake`: the shader
  bundle and registry targets depend on their respective `OUTPUT_BUNDLE` and
  `OUTPUT_FILE` custom-command outputs in the same directory.

Three other `DEPENDS` references in `test_critical_nodes.cmake` name
`shaders/Generated/OctreeConfig.glsl`. That is a tracked source input, not an
output of a custom command, so those references are ordinary file dependencies.

## Baseline and Makefiles witness

The unchanged base commit was `4a4c4473`. A fresh Unix Makefiles configure
completed successfully with exit 0:

```bash
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:baseline-make-configure -- env VIXEN_FETCHCONTENT_CACHE=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/.tmp/fetch nice -n 10 cmake -S VIXEN -B .tmp/vixmake-make-before -G "Unix Makefiles" -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json
```

Before the edit, this queued target build failed with exit 2:

```text
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:baseline-make-tool -- nice -n 10 cmake --build .tmp/vixmake-make-before --target engine_codegen_tool -j4
gmake[3]: *** No rule to make target 'codegen/tool/CodegenTool.dll', needed by 'codegen/CMakeFiles/engine_codegen_tool'.  Stop.
```

The command then materialized the cached tool, confirming the stamp command
worked; Make had no rule for the byproduct dependency. Queue log:
`/home/liory/.local/state/undertow/undertow-box-logs/1790886666-build-vixmake:baseline-make-tool.log`.

After the change, a fresh `-G "Unix Makefiles"` configure completed with exit 0:

```bash
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:make-after-configure -- env VIXEN_FETCHCONTENT_CACHE=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/.tmp/fetch nice -n 10 cmake -S VIXEN -B .tmp/vixmake-make-after -G "Unix Makefiles" -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json
```

The 22 `*_check` targets (21 codegen checks plus `no_new_mutex_check`) and
`engine_codegen_tool` all built successfully with `-j4`. The same queued target
build was repeated and completed with only up-to-date target messages and no
executed build units.

## Ninja witness

The first fresh Ninja configure reused the Makefiles tree's FetchContent base and
failed before generation: CMake rejected the existing `glm` sub-build because it
was generated for Unix Makefiles. This was an invocation/cache collision, not a
VIXEN source failure. Queue log:
`/home/liory/.local/state/undertow/undertow-box-logs/1790887179-build-vixmake:ninja-configure.log`.

The failed configure used `.tmp/fetch`; the recovery used a separate Ninja base,
`.tmp/fetch-ninja`:

```bash
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:ninja-configure -- env VIXEN_FETCHCONTENT_CACHE=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/.tmp/fetch nice -n 10 cmake -S VIXEN -B .tmp/vixmake-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:ninja-configure-retry-isolated-fetch -- env VIXEN_FETCHCONTENT_CACHE=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/.tmp/fetch-ninja nice -n 10 cmake -S VIXEN -B .tmp/vixmake-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json
```

The recovery was to keep the Ninja tree's FetchContent base separate at
`.tmp/fetch-ninja`. That fresh Release/Ninja configure completed with exit 0 and
printed the tracked Yeroket pin `b8dddb7a54620e32610d33392af3f0ea2ecba876`.
All 22 check targets passed, and `engine_codegen_tool` materialized the cached
CodegenTool. The full build completed all 1,111 steps with exit 0.

```bash
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:ninja-full-build -- nice -n 10 cmake --build .tmp/vixmake-ninja --target all -j4
```

GPU and capture results, run serially under `bash -lc` with the Dozen ICD set and
`DISPLAY`/`WAYLAND_DISPLAY` unset:

```bash
bash tools/with-test-lock.sh --agent vixmake --resource test --label vixmake:ctest-rendergraph -- bash -lc 'unset DISPLAY WAYLAND_DISPLAY; export VK_ICD_FILENAMES=/home/liory/.cache/vixen/wsl-vulkan/dzn_icd.json; ctest --test-dir .tmp/vixmake-ninja -L RenderGraph --output-on-failure -j1'
bash tools/with-test-lock.sh --agent vixmake --resource test --label vixmake:ctest-svo -- bash -lc 'unset DISPLAY WAYLAND_DISPLAY; export VK_ICD_FILENAMES=/home/liory/.cache/vixen/wsl-vulkan/dzn_icd.json; ctest --test-dir .tmp/vixmake-ninja -L SVO --output-on-failure -j1'
```

- `ctest -L RenderGraph`: 1,337 selected, 0 failures.
- Captures: 11/11 passed — editor and HUD fixture producers, four editor capture
  assertions, three HUD capture assertions, and the two headless graph captures.
- `ctest -L SVO`: the only failure was the accepted
  `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` case,
  reporting `recipe gradient capability mismatch: 94` (opcode 94, T-1449).
  The other runnable tests passed; configured skips and the disabled test were
  reported by CTest. Queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1790888737-test-vixmake:ctest-svo.log`.
- After CTest, the full Ninja build reported `ninja: no work to do.`

The dispatch also asked for a direct restore/build/check of the pinned CodegenTool
project. An initial `dotnet run` using `--artifacts-path
.tmp/vixmake-dotnet-artifacts` exited 1 after the build because `dotnet run`
looked for the apphost under the project's default `bin/Release/net8.0` path.
Queue log:
`/home/liory/.local/state/undertow/undertow-box-logs/1790889522-build-vixmake:dotnet-run-octree-check.log`.
Removing the optional artifacts-path let the authorized pinned snapshot use its
default output location; restore/build and the OctreeConfig `--check` then passed
with exit 0:

```bash
bash tools/with-test-lock.sh --agent vixmake --resource build --label vixmake:dotnet-run-octree-check-default-output -- nice -n 10 dotnet run --project /home/liory/.cache/vixen/kernel-codegen/sources/yeroket-b8dddb7a54620e32610d33392af3f0ea2ecba876/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj -c Release -- --schema /home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/VIXEN/codegen/config-schemas --struct OctreeConfig --out-cpp /home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/VIXEN/libraries/SVO/include/Generated/OctreeConfig.g.h --out-glsl /home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/VIXEN/shaders/Generated/OctreeConfig.glsl --check
```

After this direct run, I repeated the full Ninja build through the queue; it
again reported `ninja: no work to do.`

Each configure also printed VIXEN's non-terminating `Fatal Error: glslang
directory not found .../Include/glslang` diagnostic. The message is not a CMake
fatal error: configure exited 0, CMake found `glslc`/`glslangValidator`, and the
fresh full native build passed.

The opcode 94 result matches the accepted P611/P615 SVO witness. No codegen or
generated-file drift was reported; the tracked VIXEN change was only the CMake
output declaration.

## KFR composition attempt

KFR `main` was at `ff1dac716642dbca566a2744c53b38500333d3a5` and remained
unchanged. Its resolver accepted this lane's exact committed VIXEN revision:

```text
KFR_VIXEN_REVISION=a6e082001b760e262cb87bc6779390b28e2f7477
VIXEN_ROOT=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/vixmake/VIXEN
bash tools/vixen-root.sh --explain
=> MATCH (VIXEN_ROOT override): a6e082001b760e262cb87bc6779390b28e2f7477
```

I did not configure or build KFR. Its top-level configure requires an external
`KFR_PACK` and contract directory prepared from the launcher's native output;
the read-only KFR checkout contains no `.pack` artifact, and no authorized pack
input was supplied. Preparing one would require another repository's native
output, outside this lane's worktrees.

## STOPs

None in the required VIXEN scope. The optional KFR composition build lacked its
required external pack input, as described above.

## CONSOLIDATION ISSUES

- proposed: VIXEN FetchContent caches need generator-specific sub-build directories
- proposed: Standalone VIXEN configure needs a manual Undertow schema catalogue path
- proposed: Direct CodegenTool dotnet run needs a supported custom output path
