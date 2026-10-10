# capstatic lane report

## LANDABLE NOW

**Run 2 implementation tip:** 36374d1b699791ada41e37a3e37edaba3f964ce3
The VIXEN wave branch origin/wave/authoring-convergence is an ancestor of this tip.

- R487 owner ruling is implemented in the existing CapabilityGraph: the CapabilityNode base carries Runtime/BuildTime binding, and one BuildFactCapability family covers platform, product variant, queue family, and device/driver identity. Configure generates the constexpr header from the graph; no parallel declaration channel was added.
- FULL mode resolves capabilities at runtime. PERSONALIZED mode records the configure-time device and driver identity, and DeviceNode refuses startup on mismatch with a rebuild instruction. All production ResolveOptionalPath call sites use the generated compile-time path wrapper.
- Added vixen-wsl-full and vixen-wsl-personalized presets. The WSL/Dozen configure and build gates pass in both modes. Windows-native MSVC was not used; VIXEN/cmake/provision-windows-native.ps1 was inspected, but PowerShell and an inventory-only route were unavailable.
- The R485 inventory remains in [reports/capstatic-determinism.md](capstatic-determinism.md). The pick-ID path already uses explicit integer formats and values (rg32ui/uvec4); no shader or R485 bit-exact transpiler changes were made. R487.2 remains deferred.
- STOPs: none. All requested runtime checks have been run. Each broad suite has the same proven pre-existing RecipeSimdParity red, reported as a finding below.

## VERIFICATION

| Check | Result |
|---|---|
| CodeGraph explore, required first lookup | No .codegraph index in this worktree. The run 1 SPT proposal records the missing index. |
| VIXEN wave merge | Pass; origin/wave/authoring-convergence is an ancestor of the implementation tip. |
| Preset configure, FULL and PERSONALIZED | Pass from VIXEN/. Personalized probe selected Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics) through Dozen and recorded device/driver/queue identity. |
| FULL build | Pass; 507 Ninja actions. |
| PERSONALIZED build | Pass; 1149 Ninja actions, including generated-code golden checks and application/editor/test executables. |
| CapabilityGraph targeted tests, FULL | Pass: BindingTimeAndTypedBuildFactsAreStoredOnGraphNodes, ConfiguredOptionalPathUsesBuildModeBinding, and ConfiguredPathInvokesOnlyTheSelectedBody. Runtime identity rejection is skipped in FULL by design. |
| CapabilityGraph targeted tests, PERSONALIZED | Pass: all four selected tests, including ConfiguredDeviceIdentityRejectsChangedDeviceOrDriver. |
| Path-elision object inspection | Pass. FULL BodyOctreeSceneNode object: 381354 bytes and contains RT-only diagnostic strings; PERSONALIZED object: 364533 bytes and omits them. |
| FULL RenderGraph/SVO suite | FINDING: 2120 pass, 1 fail of 2121 executed. RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes fails at test_recipe_simd_parity.cpp:25 with “M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94”. Log: /home/liory/.local/state/undertow/undertow-box-logs/1791647761-test-full-rendergraph-svo.log |
| Base reproduction of suite failure | Pass as classification evidence: the same test and diagnostic reproduced on the run 1 base build. Log: /home/liory/.local/state/undertow/undertow-box-logs/1791650024-test-base-recipe-simd-parity.log. SVO source and test inputs are unchanged. This is a proven pre-existing red outside this change. |
| PERSONALIZED RenderGraph/SVO suite | 2120 pass, 1 fail of 2121 executed; vixen_multiinstance_cache_gate passed in 659.47 seconds. The only failure is the same RecipeSimdParity test and diagnostic as FULL and the base reproduction. Log: /home/liory/.local/state/undertow/undertow-box-logs/1791650050-test-personalized-rendergraph-svo.log. |
| Forced device-mismatch startup refusal | Pass. Ran the PERSONALIZED Cornell test executable with Lavapipe forced through VK_ICD_FILENAMES; it exited nonzero and emitted the required “Rebuild this application for this device and driver” message. Output: build/wsl-personalized/mismatch-startup.log. |
| Same-device Cornell render comparison across modes | Pass. Headless Cornell CTest passed in FULL (74.14 seconds) and PERSONALIZED (40.98 seconds) using Dozen. All 10 generated headless-cornell-frame PNGs compare byte-for-byte across modes. The default frame SHA-256 is cc6614e32b30ef57d6893d5278e1faab9ba4db52334f43ed1871c0aa4f067805 in both modes. |

The full-mode configure/build and test commands, and the corresponding personalized commands, all went through the live queue at /home/liory/.local/bin/with-test-lock.sh. The final runtime-gate log is /home/liory/.local/state/undertow/undertow-box-logs/1791652202-test-final-capstatic-runtime-gates.log. The CMake presets are nested under VIXEN/; an initial root-directory preset invocation failed before CMake started, then the documented nested-directory invocation succeeded.

## R485 RESULT

- State-affecting chain: 5 pass families and 7 shader source variants/stages, counting the B2 compute writer and its vertex/fragment twin.
- Visual-only candidates: 16 programmable pass families, plus sky-sphere, presentation blit, and UI composition.
- No shader changes. The integer pick-ID path was already explicit.

## SPT DISPOSITION

No SPT task IDs were assigned by the brief. The two consolidation proposals are committed in .spt-proposals/capstatic.jsonl.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Provide a CodeGraph index in VIXEN lane worktrees
- proposed: Run VIXEN build presets from their nested source directory instead of the repository root
