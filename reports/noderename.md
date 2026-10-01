# R331: Typed lifecycle callback names

## Change

Typed and variadic callbacks now have names distinct from `NodeInstance` lifecycle virtuals. The old typed callback spellings have no compatibility aliases.

| Old callback | New callback |
| --- | --- |
| `SetupImpl(TypedSetupContext&)` | `TypedSetupImpl(TypedSetupContext&)` |
| `CompileImpl(TypedCompileContext&)` | `TypedCompileImpl(TypedCompileContext&)` |
| `ExecuteImpl(TypedExecuteContext&)` | `TypedExecuteImpl(TypedExecuteContext&)` |
| `CleanupImpl(TypedCleanupContext&)` | `TypedCleanupImpl(TypedCleanupContext&)` |
| `SetupImpl(VariadicSetupContext&)` | `VariadicSetupImpl(VariadicSetupContext&)` |
| `CompileImpl(VariadicCompileContext&)` | `VariadicCompileImpl(VariadicCompileContext&)` |
| `ExecuteImpl(VariadicExecuteContext&)` | `VariadicExecuteImpl(VariadicExecuteContext&)` |
| `CleanupImpl(VariadicCleanupContext&)` | `VariadicCleanupImpl(VariadicCleanupContext&)` |

The naming follows the existing `Typed*Context` and `Variadic*Context` families. The no-argument lifecycle entry points remain `SetupImpl()`, `CompileImpl()`, `ExecuteImpl()`, and `CleanupImpl()`; the typed provider bases dispatch each task through the renamed callbacks. Provider-internal `using` declarations keep the untyped overloads visible. No consumer node uses a `using Base::Method` declaration.

## Measured surface and migrated sites

- Typed provider API: [TypedNodeInstance.h](../VIXEN/libraries/RenderGraph/include/Core/TypedNodeInstance.h#L540) at `TypedSetupImpl`, line 549 `TypedCompileImpl`, line 571 `TypedExecuteImpl`, and line 589 `TypedCleanupImpl`.
- Variadic provider API: [VariadicTypedNode.h](../VIXEN/libraries/RenderGraph/include/Core/VariadicTypedNode.h#L648) at `VariadicSetupImpl`, line 655 `VariadicCompileImpl`, line 662 `VariadicExecuteImpl`, and line 669 `VariadicCleanupImpl`.
- The colliding untyped overloads are in [NodeInstance.h](../VIXEN/libraries/RenderGraph/include/Core/NodeInstance.h#L674): `SetupImpl()` / `SetupImpl(SetupContext&)` at 674/690; `CompileImpl()` / `CompileImpl(CompileContext&)` at 702/721; `ExecuteImpl()` / `ExecuteImpl(ExecuteContext&)` at 734/756; and `CleanupImpl()` / `CleanupImpl(CleanupContext&)` at 768/787.

Before edits, VIXEN had 264 typed callback override sites across 69 declaring classes: 245 regular typed callbacks (60 setup, 60 compile, 64 execute, 60 cleanup, plus one execute-only `Context&` alias) and 19 variadic callbacks (4 setup, 5 compile, 5 execute, 5 cleanup). All 264 were class members; none were declared in structs. All were migrated. The new strict fixture adds eight more overrides across two test classes.

KFR had zero concrete typed lifecycle callback overrides. Its `-Wno-error=overloaded-virtual` exemption was removed from `app/CMakeLists.txt`; `kfr_renderer` retains `-Wall -Wextra -Werror`. UndersetLauncher had zero typed-node or callback hits and remains unchanged.

## Strict consumer check

Added `TypedNodeLifecycle.StrictConsumerCallbacksCompileWithoutOverloadHiding` in `test_typed_node_lifecycle.cpp`. It derives consumers from both `TypedNode` and `VariadicTypedNode`, overrides all callbacks, and declares no `using` statements. Its translation unit compiles with `-Woverloaded-virtual -Werror` on GNU/Clang. The strict test passed in the final run.

## Verification and test name set

The initial baseline was VIXEN `a653e3421ed6e31137bf4d8d6a72ebf4a68f48aa`. Before the final witness, the current `origin/wave/authoring-convergence` tip `b476fb9de916cfb5b2f84e751ef823f00b13626d` was merged. That upstream update changed SVO codegen and shader inputs; configure, build, capture generation, and ctest were rerun on the merged tree. The implementation commit is `b684a03bb8b3f6e6dc646d1c4bbe97b99d682ea6`.

| Target CTest name set | Before first edit | Final merged tree |
| --- | ---: | ---: |
| Registered `test_rendergraph_*` cases | 987 | 988 |
| Added | — | `TypedNodeLifecycle.StrictConsumerCallbacksCompileWithoutOverloadHiding` |
| Removed | — | None |

The pass/fail outcomes for the 984 comparable cases are unchanged: before, 978 passed, 5 skipped, and one failed; after, 979 passed, the same 5 skipped, and the same one failed. The only added case passed. The unchanged failure is `EditorToggleUndoCapture.FirstEditReachesRenderPipeline` (T-1455): both runs measured `wholeImageDiffPixels(png5,png45)=0`, below the expected threshold of 20. The three other editor capture tests passed both times.

The five skipped cases were `GPUQueryManagerIntegration.Placeholder`, `PushConstantGathererNodeTest.RuntimeFieldDiscovery`, `PushConstantGathererNodeTest.ValidateFieldTypes`, `PushConstantGathererNodeTest.HandleNullShaderBundle`, and `PushConstantGathererNodeTest.HandleEmptyPushConstantMembers`.

The three `HudRenderCapture` tests were excluded from both runnable sets because they require main-app PNG captures. The fresh `VIXEN` capture runner exited 255 before rendering with `glfwInit failed: X11: The DISPLAY environment variable is missing`. The editor's supported offscreen runner generated its four captures successfully.

After the wave merge, `vixen-wsl` configure exited 0, all 22 requested `test_rendergraph_*` targets plus `vixen_editor` and `VIXEN` built successfully with `nice -n 10` and `--parallel 8`, and the 985 runnable CTests completed. Configure printed `Fatal Error: glslang directory not found` but returned success and the build completed; this diagnostic is filed as a consolidation issue.

KFR was already current with `origin/main`. Its fresh configure and full build used an ignored source overlay pinned to the exact VIXEN implementation commit above. The build passed with the overload-warning exemption removed; CTest passed 8/8, including `headless_session_render`. The CodegenTool restore and build passed, and a direct `dotnet run --project ... --check` for `OctreeConfig` passed. The provider build's generated-file checks also passed.

## STOPs and limitations

- No naming/design STOP was needed; the typed and variadic context prefixes provided a clear existing convention.
- Finding: `EditorToggleUndoCapture.FirstEditReachesRenderPipeline` remains the documented T-1455 red on both trees.
- Not run: the three main HUD file-capture tests, because the required windowed runner cannot initialize X11 without `DISPLAY` in this environment.

## CONSOLIDATION ISSUES

- proposed: Provide CodeGraph indexes in disposable lane worktrees
- proposed: Default the VIXEN FetchContent cache to the worktree-local build tree
- proposed: Add an offscreen Linux runner for main HUD capture tests
- proposed: Align the Vulkan glslang probe diagnostic with configure outcome
- proposed: Allow KFR witnesses to use a committed local VIXEN provider without copying source
- proposed: Reuse provisioned Vulkan SDKs across KFR scratch build trees
- proposed: Reuse staged X11 development packages across KFR scratch build trees
- proposed: Provide direct CodegenTool checks without a host ICU environment override
