# hk-spirvset: SPIR-V reflection keys bindings by descriptor set

Tip: `db6f6ddb` on `lane-hk-spirvset` (based on `origin/wave/authoring-convergence` at `a48a42df`). Not pushed.

## What I verified

The defect (hubspoke R531.5) is true as stated. `SPIRVReflection.cpp` keyed `mergedBindings` by `binding->binding` alone and never read `binding->set`. Two storage buffers at `set=0, binding=0` and `set=1, binding=0` merged into one entry. No shipped VIXEN shader uses a non-zero set, so this is latent today.

## What changed

- `VIXEN/libraries/ShaderManagement/src/SPIRVReflection.cpp`: bindings are keyed by `(set, binding)` in a `std::map`. Because the map is ordered, the old explicit sort is gone. If the merged program spans more than one set, the function throws inside its existing `try`, and it returns `nullptr`.
- `VIXEN/libraries/ShaderManagement/include/SPIRVReflection.h`: doc comment updated to match.
- `VIXEN/libraries/ShaderManagement/tests/test_spirv_descriptor_layout.cpp` (new) and its CMake target in `tests/CMakeLists.txt`: one test, `SpirvDescriptorLayout.RejectsMultiSetProgramReusingBindingIndex`. It compiles a compute shader with `set=0,binding=0` and `set=1,binding=0` through real glslang, then calls `ReflectDescriptorLayout`.

Why fail closed instead of only adding the key: `DescriptorLayoutSpec` has no set field and describes one `VkDescriptorSetLayout` (`maxSets = 1`). Keying by `(set, binding)` alone would emit two bindings with the same number into one layout, which is invalid Vulkan. Callers already handle `nullptr` (`ShaderBundleBuilder.cpp:694` warns; `ShaderLibrary.cpp:504` stores null).

## Evidence

Build: `cmake --build build/wsl --target test_spirv_descriptor_layout -j4` (queued, `--resource build`), exit 0 both times.

Before (test file and CMake target added, reflection code unchanged):

```
[ RUN      ] SpirvDescriptorLayout.RejectsMultiSetProgramReusingBindingIndex
Value of: ReflectDescriptorLayout(program)
  Actual: true
Expected: false
[  FAILED  ] SpirvDescriptorLayout.RejectsMultiSetProgramReusingBindingIndex (137 ms)
[  FAILED  ] 1 test, listed below:
```

After (`--resource test`):

```
[ RUN      ] SpirvDescriptorLayout.RejectsMultiSetProgramReusingBindingIndex
[       OK ] SpirvDescriptorLayout.RejectsMultiSetProgramReusingBindingIndex (133 ms)
[  PASSED  ] 1 test.
```

Not run: the rest of the `ShaderManagementTests` target and the bundle or render-graph consumers. For single-set programs the output is the same bindings in the same ascending order as before, so I expect no change there, but I have not run those suites.

Test build tree: `build/wsl` in this worktree (gitignored), configured Release/Ninja against the main checkout's Vulkan SDK 1.4.350.1, with `VIXEN_FETCHCONTENT_CACHE=<worktree>/.tmp/fetch`.

## DECISION FOR OWNER

Fail-closed (`nullptr` for multi-set programs) is my choice, not a ruling. The alternative is per-set layouts (a set field on the spec, one layout per set). The hubspoke report says the reconciliation lane should delete the set-blind reflection path and rely on the set-aware `SpirvReflector`. If that lands, this function may be removed rather than extended. Recommendation: keep fail-closed until then.

## SPT DISPOSITION

No SPT task id was given in the brief. None to close.

## CONSOLIDATION ISSUES

- Two reflection paths disagree on sets: `SPIRVReflection.cpp` is set-blind (now fail-closed), while `SpirvReflector.cpp` is set-aware. Hubspoke R499 already calls for deleting the set-blind path. Not proposed to SPT.
