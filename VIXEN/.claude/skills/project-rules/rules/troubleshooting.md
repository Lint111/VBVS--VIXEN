# Build Troubleshooting

<rule id="troubleshooting" reiterate="situational:build-error">

## Common Issues

### "Cannot open include file"

**Cause:** CMake cache stale or missing
**Solution:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

### LNK4099 PDB Warnings Spam

**Cause:** External libraries missing PDB files
**Solution:** Safe to ignore, filter with:
```bash
cmake --build build --config Debug --parallel 16 2>&1 | grep -v "warning LNK4099"
```

### MSBuild.exe or cl.exe Using 100% CPU

**Cause:** Zombie build processes
**Solution:**
```bash
taskkill /F /IM MSBuild.exe /T 2>nul
taskkill /F /IM cl.exe /T 2>nul
taskkill /F /IM ninja.exe /T 2>nul
```

### Build Takes 10+ Minutes

**Cause:** Not using parallel compilation or building too much
**Solution:**
- Use `--parallel 16`
- Build specific targets only
- Use incremental builds

### Unity Build Errors

**Cause:** DXT1Compressor.cpp has syntax conflicts with Unity builds
**Solution:** Unity builds are disabled. Use standard incremental builds.

### Build log written "nowhere" / `grep: build_m4.log: No such file`

**Cause:** `cmd.exe /c "_ninja_preset_build.bat > log 2>&1"` redirects relative to
the **cwd of the cmd.exe call** (the repo ROOT `/mnt/c/cpp/VBVS--VIXEN`), which is
often NOT the `VIXEN/` subdir you are reading from.
**Solution:** read the log from the repo root, or use an absolute path for the
redirect target. Always confirm with `ls -la <log>` before grepping it.

### New node `.cpp` / app graph TU silently not built

**Cause:** `libraries/RenderGraph/src/Nodes/*.cpp` and
`application/main/source/graph/Build*.cpp` are **explicit lists** in their
`CMakeLists.txt` (NOT globbed). A new file compiles into nothing and its
`VIXEN_REGISTER_NODE` never fires.
**Solution:** add the new source path to the relevant `CMakeLists.txt`. See the
`debugging-known-issues` skill "Graph Authoring & Demo Friction".

### No canonical-path workarounds: don't let a WSL2 run silently land on software Vulkan

**Rule (hard rule, not situational advice):** every VIXEN executable's `main()` MUST
call `VixenSelectWslGpuIcd()` + `VixenSelectValidationLayerPath()`
(`VulkanGlobalNames.h`) before ANY Vulkan instance is created by ANY code path in
that binary — including CLI paths like `--list-gpus` that create an instance
before the "normal" one does. On WSL2 the real GPU is reachable only through Mesa
Dozen; the Vulkan loader's default ICD discovery finds only software Vulkan
(lavapipe/llvmpipe) if nothing points it at Dozen, and it does this **silently** —
no error, no warning, just a CPU rasterizer standing in for the canonical GPU
dispatch path. This is exactly the kind of hidden fallback that hides real
GPU-path bugs and produces meaningless timing numbers (2026-07-02: an entire
benchmark verification run — 39+ tests, ~40 min wall clock — executed on
llvmpipe because `vixen_benchmark` had no Dozen wiring at all; VixenApp had it,
the benchmark tool didn't, and nobody caught the gap because llvmpipe "just
works" instead of failing loud).

**When adding a new VIXEN executable target:**
1. Add `target_compile_definitions(<target> PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")`
   guarded by `if(VIXEN_WSL_DZN_ICD)` in that target's `CMakeLists.txt` (see
   `application/main/CMakeLists.txt` / `application/benchmark/CMakeLists.txt` for
   the pattern).
2. Call both `VixenSelectWslGpuIcd()` and `VixenSelectValidationLayerPath()` at the
   very top of `main()` — before parsing args if any arg path can create a Vulkan
   instance, and definitely before the first `InstanceNode`/`vkCreateInstance` of
   any kind.
3. Verify with the device-selection log line — it must say the real adapter name
   (e.g. `Selected GPU 0: Microsoft Direct3D12 (NVIDIA GeForce RTX 3060 ...)`), not
   `llvmpipe`. If it says llvmpipe on a WSL2 box with `/dev/dxg` present, the
   wiring is missing or `VIXEN_AUTO_PROVISION_WSL_VULKAN` is OFF in that build
   directory's CMakeCache (check with
   `grep VIXEN_AUTO_PROVISION_WSL_VULKAN build/<dir>/CMakeCache.txt`) — reconfigure
   with `-DVIXEN_AUTO_PROVISION_WSL_VULKAN=ON`, don't just accept the fallback.

**Never** add a `-DVIXEN_AUTO_PROVISION_WSL_VULKAN=OFF` override, an
`if (running on CI/headless) use lavapipe` branch, or any other code path that
treats software Vulkan as an acceptable default. Software Vulkan is a debugging
tool for when the real GPU genuinely isn't reachable (native Linux without a
GPU, a CI box with no `/dev/dxg`) — it is opt-in via explicit flag, never the
silent, unexamined default the loader falls back to on its own.

## Debug Checklist

1. Check CMake configured: `build/` directory exists with VS files
2. Check no zombie processes: Task Manager > Details
3. Check building right target: `--target SVO` not entire project
4. Check parallel enabled: `--parallel 16`

</rule>

---

# Runtime Troubleshooting

<rule id="runtime-troubleshooting" reiterate="situational:validation-error">

## Vulkan Validation Errors

### "Invalid VkBuffer Object" / "Invalid VkImageView Object"

**Symptoms:**
- Validation layer error: `vkUpdateDescriptorSets(): pDescriptorWrites[N].pBufferInfo[0].buffer Invalid VkBuffer Object 0xXXXXXXX`
- Garbage-looking handle values (e.g., 0xd500000000d5)
- Error occurs during descriptor set updates, not resource creation

**CRITICAL: Check SFINAE/conversion_type Detection First**

If the resource uses a **wrapper type** with `using conversion_type = VkBuffer`:

1. **Check header inclusion order** - The wrapper class MUST be fully defined (not forward-declared) where `Resource::SetHandle<WrapperType*>()` is called
2. **Add static_assert to verify detection:**
   ```cpp
   static_assert(HasConversionType_v<WrapperType>, "conversion_type not detected - check includes!");
   ```
3. **Common failure pattern:**
   ```cpp
   // BROKEN - NodeConfig.h has forward declaration
   namespace Debug { class ShaderCountersBuffer; }  // SFINAE can't see conversion_type

   // FIXED - NodeConfig.h includes full header
   #include "Debug/ShaderCountersBuffer.h"  // Full class definition visible
   ```

**Why this is hard to debug:**
- SFINAE fails **silently** - no compile error, just wrong behavior
- Symptom (garbage handles) suggests memory corruption, not template issues
- Multiple indirection layers between cause (SetHandle) and symptom (vkUpdateDescriptorSets)
- Template instantiation point depends on include chain visibility

**Checklist for wrapper types with conversion_type:**
1. [ ] Wrapper declares `using conversion_type = VkBuffer/VkImageView/etc`
2. [ ] Wrapper has `operator conversion_type() const` conversion operator
3. [ ] NodeConfig.h includes full wrapper header (NOT forward declaration)
4. [ ] static_assert(HasConversionType_v<Wrapper>) passes at usage site
5. [ ] descriptorExtractor_ is non-null after SetHandle (check with debugger or tracking)

**Reference:** HacknPlan #61 - took ~10 hours over 2 days to debug. Root cause was forward-declaration in VoxelGridNodeConfig.h preventing HasConversionType_v from detecting ShaderCountersBuffer::conversion_type.

### Other "Invalid VkHandle" Causes

If NOT a wrapper type issue:
- **Resource destroyed before descriptor update** - Check cleanup order in CleanupImpl
- **Use-after-free** - Check Resource::Clear() called before wrapper destruction
- **Swapchain resize timing** - Check PostCompile hooks refresh descriptors

## Debug Infrastructure

### DescriptorResourceTracker.h

Enable comprehensive tracking in Debug builds:
```cpp
#define VIXEN_DEBUG_DESCRIPTOR_TRACKING 1  // Auto-enabled in Debug

// Track events:
TRACK_RESOURCE_CREATED(trackingId, binding, handleValue, handleType, nodeName);
TRACK_HANDLE_EXTRACTED(trackingId, binding, handleValue, handleType, nodeName, info);
TRACK_EXTRACTOR_CALLED(trackingId, binding, handleValue, handleType, nodeName);

// Dump all events:
DUMP_RESOURCE_TRACKING();
DUMP_BINDING_TRACKING(8);  // Dump events for specific binding
```

**Key events to look for:**
- `ExtractorCreated` - Confirms descriptorExtractor_ lambda was captured
- `ExtractorCalled` - Confirms extractor is being invoked during GetHandle()
- `HandleExtracted` - Shows what value was actually extracted

If `ExtractorCreated` is missing for a wrapper type, the SFINAE detection failed.

</rule>

---

# Running an Env-Gated Demo Graph

<rule id="demo-run-troubleshooting" reiterate="situational:demo-run">

VIXEN ships several isolated, env-gated demo graphs (the live voxel path is the
default). Each is selected by an env var checked at the top of
`VulkanGraphApplication::BuildRenderGraph()`:

| Env var | Graph |
|---|---|
| `VIXEN_UI_DEMO` | UI-only RmlUi demo |
| `VIXEN_INSTANCING_DEMO` | Instanced-cube raster demo |
| `VIXEN_AUTOSYNC_DEMO` | Auto-sync FrameGraph demo (compute->compute->render, PassGroupNode) |
| `VIXEN_VULKAN_VALIDATION` | Enable validation + synchronization-validation layers |

### Env vars do NOT reach the Windows .exe from a bash prefix

**Symptom:** `VIXEN_AUTOSYNC_DEMO=1 ./binaries/VIXEN.exe` runs the DEFAULT graph;
the env var is silently dropped.
**Cause:** A WSL bash `VAR=1 ./x.exe` prefix is not seen by a Windows process.
**Solution:** set the var inside the cmd.exe session:
```bash
cmd.exe /c "set VIXEN_AUTOSYNC_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"
```
Note `set VAR=1&&` with NO space before `&&`. Use `taskkill /F /IM VIXEN.exe` to
reap a windowed run.

### A demo graph builds but "shows the wrong scene" / nothing renders

**Checklist:**
1. Confirm the env var actually reached the exe (log line "VIXEN_* set - building
   ... demo graph"). If absent, see the env-passing note above.
2. App shaders compile at runtime — a shader error throws during `Compile()`, not
   the C++ build. Check the log for the shader program name and a glslang error.
3. The demo's swapchain/extent-dependent resources (e.g. an extent-sized SSBO)
   re-derive on resize via the recompile cascade; a wrong size means the swapchain
   input (not just a build-time param) must drive the size.

</rule>
