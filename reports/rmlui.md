Pinned VIXEN's RmlUi dependency to upstream 6.3. Its `robin_hood.h` includes `<cstdint>`, and the GCC 15 build now compiles and links `rmlui_debugger`, including the previously failing `DebuggerSystemInterface.cpp`. The requested focused suites pass 62/62. The full build still stops at an unrelated AppFlow schema contract error, reproduced with the original dependency pin at base SHA `0e8b5bbab11a6b972faa7a93a9442773262a802b`.

## Root fix

Changed `VIXEN/dependencies/CMakeLists.txt` from `GIT_TAG 6.0` to `GIT_TAG 6.3`. I reviewed upstream's [6.3 release notes](https://github.com/mikke89/RmlUi/releases/tag/6.3); its [tagged `robin_hood.h`](https://github.com/mikke89/RmlUi/blob/6.3/Include/RmlUi/Core/Containers/robin_hood.h) adds `<cstdint>`. The built source revision was `ba95ffe8bfb6370efb2cdcca927eaad4710c5413`.

VIXEN does not reference or link the debugger target; it links RmlUi core. RmlUi 6.3 nevertheless adds `Source/Debugger` unconditionally, and it exposes no supported `RMLUI_DEBUGGER`-style switch. A supported upstream option to omit the unused debugger would reduce this build surface, but no such option exists. I kept the dependency target graph intact and fixed the header at its upstream source.

## Verification

Base: `0e8b5bbab11a6b972faa7a93a9442773262a802b`.

- Before the source change, configure succeeded. The corrected full build from the worktree root, `nice -n 10 cmake --build build/wsl --parallel 8`, exited 1 in `rmlui_debugger`; `nice -n 10 cmake --build build/wsl --target rmlui_debugger --parallel 8` isolated the failure to missing `uint*_t` declarations in the RmlUi 6.0 `robin_hood.h`, with the compiler noting `<cstdint>`. The first attempt from `VIXEN/` used the wrong relative build path and did not reach compilation.
- From `VIXEN/`, a clean configure into `build/wsl-witness` succeeded (exit 0): `VIXEN_FETCHCONTENT_CACHE=/home/liory/projects/VBVS--VIXEN/.claude-worktrees/rmlui/build/fetchcontent-witness nice -n 10 cmake --preset vixen-wsl -B ../build/wsl-witness -U FETCHCONTENT_SOURCE_DIR_RMLUI -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`. At that point `RmlUi_SOURCE_DIR` resolved to tag 6.3 under the isolated fresh cache. Its full build compiled the debugger, then stopped at `appflow_check`.
- During the base comparison, I temporarily restored the 6.0 pin and ran `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 nice -n 10 cmake --build build/wsl --target appflow_check --parallel 8`. It failed with the same `DepletionMaterialRow` exception. The captured output is `build/wsl-witness/base-appflow.log`; the target consumes `/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`, outside this lane's worktree.
- After restoring 6.3, the captured full build `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 nice -n 10 cmake --build build/wsl-witness --parallel 8` exited 1 at `appflow_check` (`build/wsl-witness/final-full-build.log`). The full graph compiled and linked `librmlui_debugger.a`; the failure is `RuntimeState 'Undertow.Content.Core.Systems.Economy.DepletionMaterialRow' declares gaia:true without a row scope or ref-field pairing`. The temporary base comparison had changed the CMakeLists timestamp, so Ninja regenerated and reset the cache to the shared FetchContent root when the environment override was absent. I confirmed that shared RmlUi source was still tag 6.3 at `ba95ffe8bfb6370efb2cdcca927eaad4710c5413`, with `<cstdint>`, and that its debugger compiled; the earlier isolated-cache build had also compiled the debugger.
- The full build needed `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` because the CodegenTool bootstrap initially failed with “Couldn't find a valid ICU package installed.” With that environment setting, the tool built and the graph reached its AppFlow check.
- Focused GPU and CPU tests passed: 62/62 using `ctest --test-dir ../build/wsl-witness -R "HeadlessUiGraph|RenderTargetNodeConfigTest|BodyInstance|EditorDocumentRender|HitRecordReadback|TierCrossing" --output-on-failure`. The final output is `build/wsl-witness/focused-tests-final.log`. GPU runs used the DZN ICD, Vulkan layer directory, and `LD_LIBRARY_PATH=/usr/lib/wsl/lib:/home/liory/.local/lib`.

## REFER-TO-ORCHESTRATOR

The full-build gate still needs an owner decision on the external RuntimeState contract: should the Gaia-backed `DepletionMaterialRow` be represented as a scoped row with ref-field pairing, or should this unscoped nested kind be excluded from the RuntimeState/AppFlow view catalog? The current AppFlow invocation has no documented compatibility option: its catalogue reader validates `schemas.json` before Gaia emitter options apply, and `--gaia-access-manifest` does not change that validation. This VIXEN-only lane cannot edit the Undertow schema producer or invent the missing row identity. The RmlUi change and focused witness are complete; the explicitly required full-build witness remains red pending that ruling.

## CONSOLIDATION ISSUES

- proposed: Preserve pin-specific FetchContent roots across reconfigure
- proposed: Make CodegenTool bootstrap independent of host ICU configuration
- proposed: Keep Dozen local libraries in the documented GPU test environment
