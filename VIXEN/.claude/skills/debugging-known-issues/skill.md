---
name: debugging-known-issues
description: Known debugging patterns, common issues, and solutions for VIXEN codebase. Invoke when encountering runtime errors, validation layer errors, or mysterious null handles.
allowed-tools: Read, Grep, Glob, WebSearch
---

# Debugging Known Issues Skill

Quick reference for common debugging scenarios in VIXEN. Check here FIRST before deep-diving.

---

## Quick Diagnosis Checklist

When encountering an issue:

1. **Check this skill first** - is it a known pattern?
2. **Check build mode** - Debug vs Release behavior differs
3. **Check lifecycle phase** - Compile vs Execute timing matters
4. **Add logging** - Use NODE_LOG_* macros, enable with SetEnabled(true)
5. **Check resource ownership** - Who creates, who stores, who destroys?

---

## Known Issues Catalog

### Issue: VK_NULL_HANDLE in Descriptor Updates

**Symptoms:**
```
vkUpdateDescriptorSets called with invalid buffer handle
```

**Common Causes:**
1. **Wrapper type extractor not set** - `SetHandle()` creates extractor, but timing matters
2. **Resource replaced in Execute phase** - New Resource object lacks extractor
3. **PostCompile hook timing** - Runs before source node's ctx.Out() completes

**Diagnosis Steps:**
1. Add debug output to `Resource::SetHandle()` and `Resource::GetDescriptorHandle()`
2. Check if same Resource address is used throughout
3. Verify `descriptorExtractor_` is non-null when GetDescriptorHandle called

**Fix Patterns:**
- Store Resource* pointer, extract handle lazily at bind time
- Ensure topological ordering: source node compiles before consumer's PostCompile hook
- Copy extractor when replacing resource pointer

---

### Issue: MSVC Concept Caching Bug

**Symptoms:**
- Concept evaluates to false even when constraint is satisfied
- Works in GCC/Clang but not MSVC
- `HasConversionType<T>` returns false for valid types

**Cause:**
MSVC caches concept evaluations aggressively. If the concept is instantiated before the type is complete, it caches `false`.

**Fix:**
Use inline `requires` expressions instead of named concepts:
```cpp
// BAD - may be cached incorrectly
template<typename T>
concept HasConversionType = requires { typename T::conversion_type; };

if constexpr (HasConversionType<T>) { ... }

// GOOD - evaluated fresh each time
if constexpr (requires { typename T::conversion_type; }) { ... }
```

---

### Issue: Debug Build Returns Null Instead of Throwing

**Symptoms:**
- Code works in Release, fails silently in Debug
- `Get<T>()` returns T{} instead of correct value
- No exception thrown when type mismatch

**Cause:**
Debug builds may have different exception handling or return default-constructed values.

**Fix:**
Always check for null/invalid handles:
```cpp
try {
    auto buf = GetHandle<VkBuffer>();
    if (buf != VK_NULL_HANDLE) return buf;
} catch (...) {}
// Fallback path
```

---

### Issue: PostCompile Hook Timing

**Symptoms:**
- Resources not fully initialized when PostCompile hook runs
- Extractors/handles are null
- Works when called later (e.g., in Execute phase)

**Cause:**
PostCompile hooks fire after `CompileImpl()` returns but potentially before all setup completes. Hook ordering is not guaranteed relative to output slot initialization.

**Diagnosis:**
Add debug output showing when SetHandle vs GetDescriptorHandle is called:
```cpp
std::cout << "[SetHandle] this=" << this << std::endl;
std::cout << "[GetDescriptorHandle] this=" << this << ", extractor=" << (descriptorExtractor_ ? "SET" : "NULL") << std::endl;
```

**Fix Options:**
1. Defer handle extraction to first use (lazy)
2. Move initialization to Setup phase instead
3. Use explicit ordering constraints between nodes

---

### Issue: Resource Lifecycle Mismatch (Compile vs Execute)

**Symptoms:**
- Resource pointer valid in Compile, invalid in Execute
- Different Resource instances per bundle/taskIndex
- Stale pointers after recompilation

**Cause:**
Each bundle creates its own Resource instances. The Resource from Compile phase may not be the same object used in Execute phase.

**Diagnosis:**
Print Resource addresses at both phases:
```cpp
// In CompileImpl
std::cout << "Compile: resource=" << &resource << std::endl;

// In ExecuteImpl
std::cout << "Execute: resource=" << &resource << std::endl;
```

**Fix:**
- Store Resource* from Compile phase
- During Execute, verify same object or copy critical state (like extractors)
- Use lazy handle extraction that works with fresh Resources

---

## Debugging Methodology

### 1. Isolate the Layer

| Layer | How to Test |
|-------|-------------|
| Type Traits | `static_assert`, compile-time checks |
| Resource | Unit test SetHandle/GetDescriptorHandle |
| Node | Add NODE_LOG_* statements |
| Pipeline | Run single pipeline, not full benchmark |

### 2. Add Minimal Tracing

```cpp
// In headers (when NODE_LOG not available)
std::cout << "[ClassName::Method] key=" << value << std::endl;

// In nodes
NODE_LOG_DEBUG("MethodName: key=" + std::to_string(value));
```

### 3. Bisect with Assertions

```cpp
assert(resource != nullptr && "Resource should not be null");
assert(descriptorExtractor_ && "Extractor should be set before extraction");
```

### 4. Check Vulkan Validation Messages

Validation errors include binding indices. Map to shader:
```
binding 8 → VoxelRayMarch_Compute_Names.h → shaderCounters
```

---

## Prevention Strategies

### 1. Lifecycle Documentation
Document when extractors are set, when handles are extracted.

### 2. Defensive Null Checks
Always check for null before using handles/extractors.

### 3. Unit Tests for Wrapper Types
```cpp
TEST(Resource, WrapperTypeExtraction) {
    Resource res;
    ShaderCountersBuffer* wrapper = createWrapper();
    res.SetHandle(wrapper);
    auto handle = res.GetDescriptorHandle();
    EXPECT_NE(std::get<VkBuffer>(handle), VK_NULL_HANDLE);
}
```

### 4. Tracing Infrastructure
Add optional compile-time tracing:
```cpp
#ifdef VIXEN_TRACE_RESOURCES
#define TRACE_RESOURCE(msg) std::cout << "[Resource] " << msg << std::endl
#else
#define TRACE_RESOURCE(msg)
#endif
```

---

## Graph Authoring & Demo Friction (read before adding a node or a demo graph)

These are recurring papercuts when extending the RenderGraph or adding an
env-gated demo graph. Each cost real time; check here first.

### A new node's .cpp does not link / type is "not registered"

**Symptom:** `registry.Has<MyNodeType>()` is false at runtime, or unresolved
`VIXEN_REGISTER_NODE` symbol, even though the file exists and looks correct.
**Cause:** `libraries/RenderGraph/src/Nodes/*.cpp` are an **EXPLICIT list** in
`libraries/RenderGraph/CMakeLists.txt` — they are NOT globbed. Self-registration
(`VIXEN_REGISTER_NODE`) only fires if the TU is in the whole-archived target.
**Fix:** add `src/Nodes/MyNode.cpp` to that CMakeLists list. Same for app graph
TUs: add `source/graph/BuildXxx.cpp` to `application/main/CMakeLists.txt`.

### Adding an input/output slot trips a static_assert / "count mismatch"

**Cause:** A node config carries TWO sources of truth for slot counts: the
`XxxNodeCounts::INPUTS/OUTPUTS` constants AND `VALIDATE_NODE_CONFIG(...)` plus any
hand-written `static_assert(SLOT_Slot::index == N)`. Bumping the slot count in one
place but not the others fails to compile.
**Fix:** when adding a slot, update (1) `Counts::INPUTS`, (2) the new
`INPUT_SLOT(..., index, ...)` with the next index, (3) its `INIT_INPUT_DESC` in
the config constructor. The `VALIDATE_NODE_CONFIG` macro then re-checks counts.
Optional slots (`SlotNullability::Optional`) can be left unwired without breaking
construct-only / smoke tests.

### Reading a node's COMPILED handle from host/graph-build code

**Symptom:** You need a concrete `VkPipeline`/`VkRenderPass`/`VkFramebuffer`/
`VkDescriptorSet[]` in a graph-build function, but those are produced by NODES at
**compile** time, while the build function runs **before** compile.
**Fix (idiomatic):**
```cpp
// Topological order guarantees producers compile before consumers wired to them.
// Read a compiled output host-side:
Resource* r = graph->GetInstance(handle)->GetOutput(SomeConfig::SLOT_Slot::index, 0);
auto value  = r->GetHandle<VkPipeline>();   // vector<...> types work too
```
To ACT once a node has compiled (e.g. assemble a downstream node from many
compiled handles), register a post-compile callback — it fires after each node
compiles, in topo order:
```cpp
graph->RegisterPostNodeCompileCallback([&](NodeInstance* justCompiled){ ... });
```
To force one node to compile AFTER another when there is no natural data edge,
add a topology edge (any connected input slot creates one). There is NO separate
"ordering-only" edge API — ordering is derived purely from slot connections
(`GraphTopology::TopologicalSort`).

### One descriptor set shared across compute AND graphics pipelines

**Symptom:** Want binding 0 (an SSBO) visible to a compute pipeline and a
graphics pipeline via one shared layout — but the layouts come out incompatible.
**Cause:** `DescriptorSetNode` builds its layout ONLY from a single shader
bundle's SPIR-V reflection. The explicit `DescriptorLayoutSpec` / `PARAM_LAYOUT_SPEC`
path is declared but **dead** (the include is commented "TEMPORARILY REMOVED" and
the impl never reads it). Compute and vert+frag are separate bundles, so binding 0
reflects `stageFlags=COMPUTE` in one and `=FRAGMENT` in the other → two different
`VkDescriptorSetLayout`s.
**Fix:** use THREE separate descriptor sets (one per bundle's reflected layout),
all `pBufferInfo` pointing at the SAME `VkBuffer`, and rebind per pipeline. Legal
Vulkan (each set matches its own pipeline layout) and an identical hazard for
syncval (synchronization tracks the buffer's memory, not the set identity). Do NOT
revive the shared-layout path for this.

### App shaders compile at RUNTIME, not at build time

**Symptom:** Looking for a `.spv` build step / glslang invocation for an app
shader in the build log and not finding it.
**Cause:** `shaders/` is auto-globbed for IDE listing only. App shaders are
compiled to SPIR-V at graph-COMPILE time by `ShaderLibraryNode`
(`RegisterShaderBuilder` + `AddStageFromFile`). Shader compile errors therefore
surface at runtime/first-compile, NOT in the C++ build.
**Implication:** a green C++ build does not validate shader source; run the app
(M5-style) to compile/validate shaders. There is no need to register new shader
filenames anywhere — drop them in `shaders/` and reference by name in the builder.

### No generic "storage buffer of N bytes" node existed (now there is)

`InstanceBufferNode`/`DynamicInstanceBufferNode` are hard-sized to
`gridDim^2 * sizeof(mat4)`. For an arbitrary-sized SSBO use **`StorageBufferNode`**
(`PARAM_SIZE_BYTES`, or `elementCount*elementStride`, or connect `SWAPCHAIN_INFO`
+ `PARAM_BYTES_PER_PIXEL` for extent-driven sizing that re-sizes through the
swapchain resize/recompile cascade). Zero-initialised.

## Related Documentation

- [[Vixen-Docs/05-Progress/features/DescriptorResourceRefactor-DebugSession]]
- [[Vixen-Docs/05-Progress/features/DescriptorResourceRefactor]]
- [[logging skill]] - For proper logging patterns
- project-rules `rules/troubleshooting.md` - build / runtime / env-run friction
