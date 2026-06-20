# RenderGraph Node Build-Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS (2026-06-19):** M1 ✅ (prior session) · M2 ✅ · M3 ✅ · M4 ✅ · M5 ✅ — **all milestones done**. Commits on `claude/rendergraph-node-build-decoupling`: `3ec2e6f3` (TypedConnection node-config leak removed), `ec86171b` (M2 split), `c1ee6889`+`f7635a39`+`b81064c0` (M3), `de4f9f1a` (M4 app TU split). Builds/measurements use the **`vixen-ninja` preset**, not the VS-generator commands written in the steps below. Self-registration verified on real GPU via `vixen_benchmark`. Two follow-ups: `StructSpreaderNode` is dead code (left unregistered); GUI `VIXEN.exe` window/present run is the one owed manual check.

**Goal:** Make RenderGraph builds granular so editing one node (or wiring a few nodes) does not recompile the graph core, the registry, or unrelated nodes — without weakening any compile-time guarantee.

**Architecture:** Three moves, in value order for the build-granularity goal. (1) Delete the dead central node registry (a 34-header recompile chokepoint that nothing calls). (2) Split the monolithic `RenderGraph` static library into `RenderGraphCore` (graph engine, zero concrete-node deps) + `RenderGraphNodes` (the 40 nodes), with a back-compat `RenderGraph` alias so consumers are untouched. (3) Replace the hand-maintained registration list with node self-registration (a global manifest replayed per `EngineContext`), linked whole-archive so static-lib registrars are not stripped. An optional fourth move splits the app's graph construction into per-subgraph translation units to shrink the app's recompile blast radius.

**Tech Stack:** C++23, CMake (3.15…4.2), MSVC (Windows build via `cmake.exe`), GoogleTest, static libraries.

---

## Context the executor needs (read before starting)

You are working in `/mnt/c/cpp/VBVS--VIXEN/VIXEN`. This is a Windows/MSVC project; builds run through `cmake.exe`. Key facts established by investigation (do not re-derive):

- **The render graph is built imperatively in C++.** Nodes are wired with typed compile-time slot handles: `batch.Connect(srcNode, SrcConfig::SLOT, tgtNode, TgtConfig::SLOT)`. There is no graph file format or loader. This plan does **not** change that.
- **`RenderGraph` is ONE static library** (`libraries/RenderGraph/CMakeLists.txt:341`, `add_library(RenderGraph STATIC ...)`) bundling: data-core headers, 37 node config headers, the connection system, core engine sources, ~40 node headers + sources, debug, events, UI (RmlUi), and selection.
- **The `NodeTypeRegistry` is owned per-`EngineContext`** (`src/Core/EngineContext.cpp:26`, `registry_ = std::make_unique<NodeTypeRegistry>()`), not a global singleton. It is populated through an injected callback: `EngineContext` calls `config.registerNodeTypes(*registry_)` (`EngineContext.cpp:27-29`).
- **`RegisterBuiltInNodeTypes(NodeTypeRegistry&)` in `src/Core/NodeTypeRegistry.cpp` is DEAD CODE** — nothing in the tree calls it (verified by tree-wide grep). It `#include`s all 34 node headers and is therefore recompiled whenever any node header changes, for no benefit.
- **The real registration list is in the app:** `VulkanGraphApplication::RegisterNodeTypes(NodeTypeRegistry&)` (`application/main/source/VulkanGraphApplication.cpp:485+`) calls `registry.Register<XNodeType>()` for the nodes it uses, wired via `engineCfg.registerNodeTypes = [this](auto& r){ RegisterNodeTypes(r); }` (`VulkanGraphApplication.cpp:150`).
- **Typed `AddNode<T>()` requires `T` pre-registered** (`include/Core/RenderGraph.h:133-141`): it does `typeRegistry->Get<TNodeType>()` and throws if null. So registration is on the critical path; the typed path does not instantiate directly.
- **`Register<T>()`** (`include/Core/NodeTypeRegistry.h:33-49`) constructs one `T` via `std::make_unique<T>()`, keys it by `std::type_index(typeid(T))`, and assigns a `NodeTypeId` from `GetTypeId()`.
- **`GenerateTypeId()` is a global incrementing counter** (`src/Core/NodeType.cpp:7-10`), so `NodeTypeId`s are not stable across registry instances or runs. **Name (`GetTypeName()`) is the stable lookup key** — use it in tests, never a hardcoded numeric id.
- **No Core or Connection source references any concrete node or node-config** once the dead registry file is gone (verified). This is what makes the library split clean.
- **Existing RenderGraph tests** live in `libraries/RenderGraph/tests/` (e.g. `test_multidispatch_integration.cpp`). There is currently **no** registry/EngineContext population test — this plan creates one.

**Build & test commands (run from repo root `/mnt/c/cpp/VBVS--VIXEN`):**

```bash
# Configure (once, or after CMakeLists changes):
cmake.exe -B build -S VIXEN

# Build the whole solution (engineering rule: full build must be green before commit):
cmake.exe --build build --config Debug --parallel 16

# Run a single RenderGraph test executable (replace <name>):
./build/libraries/RenderGraph/tests/Debug/<name>.exe --gtest_brief=1
```

> **Engineering clean-commit gate (HARD RULE):** before every commit, the FULL build must be green and the affected tests must pass, verified from freshly-captured output this turn. Each "Commit" step below assumes you have just done that. Never commit a red or unverified state.

---

## File Structure

**Milestone 1 (delete dead code):**
- Modify: `libraries/RenderGraph/CMakeLists.txt` — keep (no node-source change yet)
- Delete: `libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp`'s `RegisterBuiltInNodeTypes` definition + its 34 node includes
- Modify: `libraries/RenderGraph/include/Core/NodeTypeRegistry.h:127` — remove the `RegisterBuiltInNodeTypes` declaration

**Milestone 2 (library split):**
- Modify: `libraries/RenderGraph/CMakeLists.txt` — split into `RenderGraphCore` + `RenderGraphNodes` + `RenderGraph` back-compat alias

**Milestone 3 (self-registration):**
- Create: `libraries/RenderGraph/include/Core/NodeRegistration.h` — manifest + `RegisterNodeFactory` helper + `RegisterAllNodes` + `VIXEN_REGISTER_NODE` macro
- Create: `libraries/RenderGraph/src/Core/NodeRegistration.cpp` — manifest singleton + `RegisterAllNodes` definition
- Create: `libraries/RenderGraph/tests/test_node_self_registration.cpp` — TDD anchor (catches stripping)
- Modify: each `libraries/RenderGraph/src/Nodes/*.cpp` (40 files) — add one self-registration line
- Modify: `application/main/source/VulkanGraphApplication.cpp:150,485+` — replace hand-list with `RegisterAllNodes`
- Modify: `libraries/RenderGraph/CMakeLists.txt` + consuming targets — whole-archive link for `RenderGraphNodes`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt` — add the new test

**Milestone 4 (optional — app TU split):**
- Create: `application/main/source/graph/BuildMainGraph.cpp`, `BuildUIGraph.cpp` (extract from `VulkanGraphApplication.cpp`)
- Modify: `application/main/CMakeLists.txt`, `VulkanGraphApplication.cpp`

---

## Milestone 0: Baseline measurement (evidence before claims)

### Task 0: Capture the current incremental-build blast radius

**Files:** none modified (measurement only).

- [ ] **Step 1: Clean-build once to warm the tree**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_baseline_clean.log
```
Expected: build succeeds (BUILD SUCCEEDED / 0 errors).

- [ ] **Step 2: Measure "edit one node header" blast radius**

Touch a single node config header, rebuild, and count recompiled translation units:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
touch VIXEN/libraries/RenderGraph/include/Data/Nodes/CameraNodeConfig.h
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_touch_camera_BEFORE.log
grep -ciE '\.cpp$|Compiling|/c ' build_touch_camera_BEFORE.log
```
Record the count (number of recompiled TUs) in the plan's results table (Milestone 5). Expected today: the `CameraNode` TU, `NodeTypeRegistry.cpp` (dead file), and every app TU that includes the config.

- [ ] **Step 3: Measure "edit one node .cpp body" blast radius**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
touch VIXEN/libraries/RenderGraph/src/Nodes/CameraNode.cpp
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_touch_cameracpp_BEFORE.log
grep -ciE 'Compiling|/c ' build_touch_cameracpp_BEFORE.log
```
Record the count.

- [ ] **Step 4: Commit the baseline logs as evidence**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add build_baseline_clean.log build_touch_camera_BEFORE.log build_touch_cameracpp_BEFORE.log
git commit -m "chore(rendergraph): capture baseline incremental-build blast radius"
```
> Note: if the repo `.gitignore` excludes `build_*.log`, instead paste the counts into `VIXEN/docs/superpowers/plans/2026-06-19-rendergraph-node-build-decoupling.md` Milestone 5 table and commit that.

---

## Milestone 1: Delete the dead central registry

This is a pure win: removes a 34-node-header recompile chokepoint that nothing calls. Do it first and in isolation.

### Task 1: Remove `RegisterBuiltInNodeTypes` and its includes

**Files:**
- Modify: `libraries/RenderGraph/include/Core/NodeTypeRegistry.h:124-127`
- Modify: `libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp` (remove the 34 `#include "Nodes/..."` lines and the `RegisterBuiltInNodeTypes` function body)

- [ ] **Step 1: Prove it is unreferenced (guard against surprise)**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
grep -rn 'RegisterBuiltInNodeTypes' VIXEN --include=*.cpp --include=*.h | grep -vE 'NodeTypeRegistry\.(h|cpp):'
```
Expected: **no output**. If there IS output, STOP — a caller exists; convert it to `RegisterAllNodes` in Milestone 3 instead of deleting. Do not proceed past this step until output is empty.

- [ ] **Step 2: Remove the declaration**

In `libraries/RenderGraph/include/Core/NodeTypeRegistry.h`, delete lines 124-127:
```cpp
/**
 * @brief Helper function to register built-in node types
 */
void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry);
```

- [ ] **Step 3: Remove the definition and all node includes**

In `libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp`, delete every `#include "Nodes/..."` line and the entire `void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry) { ... }` function. Keep the class-method definitions (`RegisterNodeType`, `GetNodeType`, `Clear`, etc.) and their non-node includes (`Core/NodeTypeRegistry.h`, `<algorithm>`, etc.).

- [ ] **Step 4: Full build to verify green**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m1.log
grep -iE 'error|BUILD FAILED' build_m1.log | head
```
Expected: no errors; build succeeds. (The app supplies its own registration, so nothing breaks.)

- [ ] **Step 5: Run the RenderGraph test suite to confirm no regression**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
for t in ./build/libraries/RenderGraph/tests/Debug/test_*.exe; do "$t" --gtest_brief=1 || break; done
```
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Core/NodeTypeRegistry.h VIXEN/libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp
git commit -m "refactor(rendergraph): delete dead RegisterBuiltInNodeTypes (34-header recompile chokepoint, uncalled)"
```

---

## Milestone 2: Split `RenderGraphCore` from `RenderGraphNodes`

Makes the build-granularity goal structural: Core compiles with zero concrete-node dependencies; a node change can never recompile Core. A back-compat `RenderGraph` alias keeps every existing consumer working.

### Task 2: Restructure `libraries/RenderGraph/CMakeLists.txt` into two libraries + alias

**Files:**
- Modify: `libraries/RenderGraph/CMakeLists.txt`

- [ ] **Step 1: Re-verify Core has no concrete-node dependency**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
grep -rlE '#include "Nodes/|#include "Data/Nodes/' VIXEN/libraries/RenderGraph/src/Core VIXEN/libraries/RenderGraph/src/Connection VIXEN/libraries/RenderGraph/src/Data
```
Expected: **no output** (the only offender, `NodeTypeRegistry.cpp`, was cleaned in Milestone 1). If output appears, list those files and move them to the Nodes target in Step 3.

- [ ] **Step 2: Replace the single `add_library(RenderGraph STATIC ...)` block**

In `libraries/RenderGraph/CMakeLists.txt`, replace the `add_library(RenderGraph STATIC ...)` call (currently lines ~341-358) and the subsequent `target_link_libraries(RenderGraph PUBLIC ...)` (lines ~400-415) with the two-library structure below. The `set(RENDERGRAPH_* ...)` variable groups defined earlier in the file stay as-is and are reused.

```cmake
# ============================================================================
# RenderGraphCore — graph engine, zero concrete-node dependencies
# ============================================================================
add_library(RenderGraphCore STATIC
    ${RENDERGRAPH_DATA_CORE_HEADERS}
    ${RENDERGRAPH_DATA_CORE_SOURCES}
    ${RENDERGRAPH_CONNECTION_HEADERS}
    ${RENDERGRAPH_CONNECTION_SOURCES}
    ${RENDERGRAPH_CORE_HEADERS}
    ${RENDERGRAPH_CORE_SOURCES}
    ${RENDERGRAPH_DEBUG_HEADERS}
    ${RENDERGRAPH_DEBUG_SOURCES}
    ${RENDERGRAPH_EVENT_HEADERS}
    ${RENDERGRAPH_SELECTION_HEADERS}
    ${RENDERGRAPH_SELECTION_SOURCES}
)
target_link_libraries(RenderGraphCore
    PUBLIC
        Core::Core
        VulkanResources
        Logger
        EventBus
        ResourceManagement
        CashSystem
        ShaderManagement
        SVO
        magic_enum::magic_enum
        TBB::tbb
)
target_compile_features(RenderGraphCore PUBLIC cxx_std_23)

# ============================================================================
# RenderGraphNodes — the concrete nodes (+ their heavy deps), depends on Core
# ============================================================================
add_library(RenderGraphNodes STATIC
    ${RENDERGRAPH_DATA_NODE_CONFIGS}
    ${RENDERGRAPH_NODE_HEADERS}
    ${RENDERGRAPH_NODE_SOURCES}
    ${RENDERGRAPH_UI_HEADERS}
    ${RENDERGRAPH_UI_SOURCES}
)
target_link_libraries(RenderGraphNodes
    PUBLIC
        RenderGraphCore
        glfw                   # WindowNode/InputNode/InstanceNode
        rmlui_core             # UIRenderNode
        freetype
)
target_compile_features(RenderGraphNodes PUBLIC cxx_std_23)
target_compile_definitions(RenderGraphNodes PUBLIC RMLUI_STATIC_LIB)

# ============================================================================
# RenderGraph — back-compat facade: existing consumers link this and get both.
# RenderGraphNodes is whole-archived (Milestone 3 needs the self-registrars kept).
# ============================================================================
add_library(RenderGraph INTERFACE)
target_link_libraries(RenderGraph INTERFACE
    RenderGraphCore
    RenderGraphNodes
)
```

- [ ] **Step 3: Move the include-directories, PCH, and source-group blocks to the right targets**

- The `target_include_directories(RenderGraph PUBLIC ... include ... generated/sdi ...)` block (lines ~376-392) → duplicate onto **both** `RenderGraphCore` and `RenderGraphNodes` (both need the `include/` root and the SDI dirs). Keep `PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src` on both.
- The MSVC `target_compile_options(... /FS /bigobj)` (lines ~365-367) → apply to both targets.
- The PCH block (lines ~434-446, `target_precompile_headers(RenderGraph PRIVATE RenderGraphHeaders.h)` + the `TBBVirtualTaskExecutor.cpp` skip) → apply to `RenderGraphCore` (the PCH header has no node deps; `TBBVirtualTaskExecutor.cpp` is a Core source). Add a node-appropriate PCH to `RenderGraphNodes` only if measured beneficial (skip for now — YAGNI).
- `set_target_properties(... ARCHIVE_OUTPUT_DIRECTORY .../lib FOLDER "Libraries")` → apply to both `RenderGraphCore` and `RenderGraphNodes`.
- The `source_group(...)` calls → leave as-is (cosmetic; they reference the same variable groups).

- [ ] **Step 4: Full build to verify green**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m2.log
grep -iE 'error|unresolved|BUILD FAILED' build_m2.log | head
```
Expected: no errors. Existing consumers link `RenderGraph` (the interface alias) and resolve both libs transitively.

- [ ] **Step 5: Prove Core no longer recompiles on a node change**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
touch VIXEN/libraries/RenderGraph/src/Nodes/CameraNode.cpp
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_node_after_split.log
grep -iE 'RenderGraphCore' build_node_after_split.log
```
Expected: **no `RenderGraphCore` recompilation** — only `RenderGraphNodes` (CameraNode TU) and downstream links.

- [ ] **Step 6: Run RenderGraph tests**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
for t in ./build/libraries/RenderGraph/tests/Debug/test_*.exe; do "$t" --gtest_brief=1 || break; done
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "refactor(rendergraph): split RenderGraphCore | RenderGraphNodes (Core has zero node deps; RenderGraph alias keeps consumers green)"
```

---

## Milestone 3: Node self-registration (decentralized, no hand-maintained list)

Replaces the app's 37-line `RegisterNodeTypes` hand-list with automatic registration. Each node `.cpp` self-registers into a global manifest; `EngineContext` replays the manifest into its per-instance registry via `RegisterAllNodes`. Because registrars live in a static library, `RenderGraphNodes` must be linked **whole-archive** or the linker strips the registrar objects.

### Task 3: Create the registration manifest and helper

**Files:**
- Create: `libraries/RenderGraph/include/Core/NodeRegistration.h`
- Create: `libraries/RenderGraph/src/Core/NodeRegistration.cpp`
- Modify: `libraries/RenderGraph/CMakeLists.txt` (add the new core source)

- [ ] **Step 1: Write the manifest header**

Create `libraries/RenderGraph/include/Core/NodeRegistration.h`:
```cpp
#pragma once

#include "Core/NodeTypeRegistry.h"
#include <functional>
#include <vector>

namespace Vixen::RenderGraph {

namespace detail {
    // Meyers-singleton list of node registrars. Init-on-first-use: any node TU's
    // static registrar that runs at dynamic-init safely constructs this first.
    std::vector<std::function<void(NodeTypeRegistry&)>>& NodeRegistrars();

    // Appends a registrar thunk; returns true so it can initialise a file-scope bool.
    bool RegisterNodeFactory(std::function<void(NodeTypeRegistry&)> thunk);
}

// Replays every self-registered node into the given (per-EngineContext) registry.
// Wire this into EngineConfig::registerNodeTypes.
void RegisterAllNodes(NodeTypeRegistry& registry);

// One-liner each node .cpp uses to self-register. Place at file scope.
//   VIXEN_REGISTER_NODE(CameraNodeType);
#define VIXEN_REGISTER_NODE(NodeTypeClass)                                        \
    namespace {                                                                   \
        const bool s_vixen_registered_##NodeTypeClass =                           \
            ::Vixen::RenderGraph::detail::RegisterNodeFactory(                     \
                [](::Vixen::RenderGraph::NodeTypeRegistry& reg) {                 \
                    reg.Register<NodeTypeClass>();                                 \
                });                                                               \
    }

} // namespace Vixen::RenderGraph
```

- [ ] **Step 2: Write the manifest implementation**

Create `libraries/RenderGraph/src/Core/NodeRegistration.cpp`:
```cpp
#include "Core/NodeRegistration.h"

namespace Vixen::RenderGraph {

namespace detail {
    std::vector<std::function<void(NodeTypeRegistry&)>>& NodeRegistrars() {
        static std::vector<std::function<void(NodeTypeRegistry&)>> registrars;
        return registrars;
    }

    bool RegisterNodeFactory(std::function<void(NodeTypeRegistry&)> thunk) {
        NodeRegistrars().push_back(std::move(thunk));
        return true;
    }
}

void RegisterAllNodes(NodeTypeRegistry& registry) {
    for (auto& thunk : detail::NodeRegistrars()) {
        thunk(registry);
    }
}

} // namespace Vixen::RenderGraph
```

- [ ] **Step 3: Add the source to RenderGraphCore**

In `libraries/RenderGraph/CMakeLists.txt`, add to `RENDERGRAPH_CORE_HEADERS`:
```cmake
    include/Core/NodeRegistration.h
```
and to `RENDERGRAPH_CORE_SOURCES`:
```cmake
    src/Core/NodeRegistration.cpp
```
> Rationale: the manifest and `RegisterAllNodes` are node-agnostic (they only know `NodeTypeRegistry`), so they belong in Core. The per-node registrars (Task 5) live in the Nodes lib.

- [ ] **Step 4: Build to verify the new core source compiles**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m3_t3.log
grep -iE 'error|BUILD FAILED' build_m3_t3.log | head
```
Expected: no errors.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/include/Core/NodeRegistration.h VIXEN/libraries/RenderGraph/src/Core/NodeRegistration.cpp VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(rendergraph): node self-registration manifest (RegisterAllNodes + VIXEN_REGISTER_NODE)"
```

### Task 4: Write the TDD anchor that also catches static-lib stripping

**Files:**
- Create: `libraries/RenderGraph/tests/test_node_self_registration.cpp`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libraries/RenderGraph/tests/test_node_self_registration.cpp`:
```cpp
#include <gtest/gtest.h>
#include "Core/NodeTypeRegistry.h"
#include "Core/NodeRegistration.h"

using namespace Vixen::RenderGraph;

// If RenderGraphNodes is NOT whole-archived, the per-node static registrars are
// stripped and the registry comes back nearly empty — this test catches that.
TEST(NodeSelfRegistration, RegistersAllBuiltInNodes) {
    NodeTypeRegistry registry;
    RegisterAllNodes(registry);

    // 32 node types are expected to self-register (see RegisterNodeTypes audit).
    // Use >= so adding a node does not break the test; the stripping bug drives it to ~0.
    EXPECT_GE(registry.GetNodeTypeCount(), 32u);

    // Spot-check stable, name-keyed lookups across node groups:
    EXPECT_TRUE(registry.HasNodeType("Camera"));
    EXPECT_TRUE(registry.HasNodeType("Device"));
    EXPECT_TRUE(registry.HasNodeType("VoxelGrid"));
    EXPECT_TRUE(registry.HasNodeType("Present"));
}
```
> Note: the name strings are the `NodeType` constructor names (e.g. `DeviceNodeType() : TypedNodeType<DeviceNodeConfig>("Device") {}`). If a spot-checked name differs, read the node's `*NodeType` constructor and use its exact string. Do not assert on numeric `NodeTypeId` (it is a global counter — unstable).

- [ ] **Step 2: Register the test in CMake**

In `libraries/RenderGraph/tests/CMakeLists.txt`, follow the existing per-test pattern (copy the block used by `test_multidispatch_integration`) to add an executable for `test_node_self_registration.cpp`. It must link the **whole-archived** `RenderGraphNodes` (the consuming-target change in Task 6 must apply here too) plus `GTest::gtest_main`. Concretely, the test target's link must use the whole-archive form from Task 6, Step 1.

- [ ] **Step 3: Run the test — expect FAIL (registrars not added yet)**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 --target test_node_self_registration 2>&1 | tee build_m3_t4.log
./build/libraries/RenderGraph/tests/Debug/test_node_self_registration.exe --gtest_brief=1
```
Expected: **FAIL** — `GetNodeTypeCount()` is 0 because no node calls `VIXEN_REGISTER_NODE` yet. (This confirms the test has teeth.)

- [ ] **Step 4: Commit the failing test**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/tests/test_node_self_registration.cpp VIXEN/libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "test(rendergraph): self-registration anchor (red — no registrars yet)"
```

### Task 5: Convert all node `.cpp` files to self-register

**Files (40):** every file in `libraries/RenderGraph/src/Nodes/*.cpp`. The transformation is identical per file.

- [ ] **Step 1: Apply the registration line to each node source**

In each `src/Nodes/<X>.cpp`, add at the top (after existing includes):
```cpp
#include "Core/NodeRegistration.h"
```
and at file scope (after the node's `*NodeType` class is fully visible — i.e. after its header is included), add:
```cpp
VIXEN_REGISTER_NODE(<X>NodeType);
```
Worked example — `src/Nodes/CameraNode.cpp`:
```cpp
#include "Nodes/CameraNode.h"
#include "Core/NodeRegistration.h"
// ... other includes ...

VIXEN_REGISTER_NODE(CameraNodeType);

// ... existing CameraNode / CameraNodeType definitions unchanged ...
```

Apply to all 40, using each file's own `*NodeType` symbol (the type registered for that node). The complete list of source files and the `*NodeType` symbol each must register:

| Source file | Register |
|---|---|
| `BoolOpNode.cpp` | `BoolOpNodeType` |
| `CameraNode.cpp` | `CameraNodeType` |
| `CommandPoolNode.cpp` | `CommandPoolNodeType` |
| `ComputeDispatchNode.cpp` | `ComputeDispatchNodeType` |
| `ComputePipelineNode.cpp` | `ComputePipelineNodeType` |
| `MultiDispatchNode.cpp` | `MultiDispatchNodeType` |
| `DebugBufferReaderNode.cpp` | `DebugBufferReaderNodeType` |
| `DepthBufferNode.cpp` | `DepthBufferNodeType` |
| `RenderTargetNode.cpp` | `RenderTargetNodeType` |
| `PickIdTargetNode.cpp` | `PickIdTargetNodeType` |
| `InstanceBufferNode.cpp` | `InstanceBufferNodeType` |
| `DynamicInstanceBufferNode.cpp` | `DynamicInstanceBufferNodeType` |
| `MvpUniformNode.cpp` | `MvpUniformNodeType` |
| `DescriptorResourceGathererNode.cpp` | `DescriptorResourceGathererNodeType` |
| `DescriptorSetNode.cpp` | `DescriptorSetNodeType` |
| `DeviceNode.cpp` | `DeviceNodeType` |
| `FrameSyncNode.cpp` | `FrameSyncNodeType` |
| `FramebufferNode.cpp` | `FramebufferNodeType` |
| `GeometryRenderNode.cpp` | `GeometryRenderNodeType` |
| `GraphicsPipelineNode.cpp` | `GraphicsPipelineNodeType` |
| `InputNode.cpp` | `InputNodeType` |
| `InstanceNode.cpp` | `InstanceNodeType` |
| `LoopBridgeNode.cpp` | `LoopBridgeNodeType` |
| `SelectionCoordinatorNode.cpp` | `SelectionCoordinatorNodeType` |
| `VoxelSelectionProviderNode.cpp` | `VoxelSelectionProviderNodeType` |
| `PresentNode.cpp` | `PresentNodeType` |
| `PushConstantGathererNode.cpp` | `PushConstantGathererNodeType` |
| `RenderPassNode.cpp` | `RenderPassNodeType` |
| `ShaderLibraryNode.cpp` | `ShaderLibraryNodeType` |
| `StructSpreaderNode.cpp` | `StructSpreaderNodeType` |
| `SwapChainNode.cpp` | `SwapChainNodeType` |
| `TextureLoaderNode.cpp` | `TextureLoaderNodeType` |
| `VertexBufferNode.cpp` | `VertexBufferNodeType` |
| `VoxelGridNode.cpp` | `VoxelGridNodeType` |
| `VoxelAABBConverterNode.cpp` | `VoxelAABBConverterNodeType` |
| `AccelerationStructureNode.cpp` | `AccelerationStructureNodeType` |
| `RayTracingPipelineNode.cpp` | `RayTracingPipelineNodeType` |
| `TraceRaysNode.cpp` | `TraceRaysNodeType` |
| `WindowNode.cpp` | `WindowNodeType` |
| `UIRenderNode.cpp` | `UIRenderNodeType` |

> Special case — `ConstantNode`: the old code comment said "ConstantNodeType must be registered in application code (circular dependency)". Its type lives in `include/Nodes/ConstantNodeType.h` and there is no `ConstantNode.cpp` in the node sources list. Verify whether a `.cpp` exists; if `ConstantNodeType` is header-only, add `VIXEN_REGISTER_NODE(ConstantNodeType);` to a small new TU `src/Nodes/ConstantNode.cpp` (include `Nodes/ConstantNodeType.h` + `Core/NodeRegistration.h`) and add it to `RENDERGRAPH_NODE_SOURCES`. Confirm the former circular dependency was on the *app* (now moot, since registration moved into the lib).

- [ ] **Step 2: Build (test will still fail until whole-archive in Task 6)**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m3_t5.log
grep -iE 'error|BUILD FAILED' build_m3_t5.log | head
```
Expected: no compile errors. The self-registration test may still fail here if the test target does not yet whole-archive `RenderGraphNodes` — that is fixed in Task 6.

- [ ] **Step 3: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/src/Nodes/ VIXEN/libraries/RenderGraph/CMakeLists.txt
git commit -m "feat(rendergraph): every node self-registers via VIXEN_REGISTER_NODE"
```

### Task 6: Whole-archive the Nodes lib so registrars survive linking, then green the test

**Files:**
- Modify: `libraries/RenderGraph/CMakeLists.txt` (the `RenderGraph` interface alias) and any target that links `RenderGraphNodes` directly (the test target).

- [ ] **Step 1: Make the back-compat alias link Nodes whole-archive**

In `libraries/RenderGraph/CMakeLists.txt`, change the `RenderGraph` interface target to whole-archive `RenderGraphNodes`. Use the CMake 3.24+ generator expression with a raw-flag fallback:
```cmake
add_library(RenderGraph INTERFACE)
target_link_libraries(RenderGraph INTERFACE RenderGraphCore)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(RenderGraph INTERFACE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,RenderGraphNodes>")
else()
    if(MSVC)
        target_link_libraries(RenderGraph INTERFACE RenderGraphNodes)
        target_link_options(RenderGraph INTERFACE
            "/WHOLEARCHIVE:$<TARGET_FILE:RenderGraphNodes>")
    else()
        target_link_libraries(RenderGraph INTERFACE
            -Wl,--whole-archive RenderGraphNodes -Wl,--no-whole-archive)
    endif()
endif()
```
> Check the configured CMake version: `cmake.exe --version`. The README states 3.21+; if it is ≥3.24 the generator-expression path is used automatically.

- [ ] **Step 2: Ensure the test target uses the whole-archived facade**

In `libraries/RenderGraph/tests/CMakeLists.txt`, the `test_node_self_registration` target must link `RenderGraph` (the interface facade, which now whole-archives Nodes), not `RenderGraphNodes` directly. Confirm its `target_link_libraries(... RenderGraph GTest::gtest_main)`.

- [ ] **Step 3: Rebuild and run the anchor test — expect PASS**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 --target test_node_self_registration 2>&1 | tee build_m3_t6.log
./build/libraries/RenderGraph/tests/Debug/test_node_self_registration.exe --gtest_brief=1
```
Expected: **PASS** — `GetNodeTypeCount() >= 32` and the name lookups succeed. If it still reports ~0, whole-archive is not taking effect: verify the linker flag actually appears in the test target's link command (`grep -i wholearchive build_m3_t6.log`).

- [ ] **Step 4: Switch the app to `RegisterAllNodes` and delete the hand-list**

In `application/main/source/VulkanGraphApplication.cpp`:
- Line ~150: replace
  ```cpp
  engineCfg.registerNodeTypes = [this](NodeTypeRegistry& reg) { RegisterNodeTypes(reg); };
  ```
  with
  ```cpp
  engineCfg.registerNodeTypes = &Vixen::RenderGraph::RegisterAllNodes;
  ```
  (add `#include "Core/NodeRegistration.h"`).
- Delete the `VulkanGraphApplication::RegisterNodeTypes` definition (line ~485+) and its declaration in `application/main/include/VulkanGraphApplication.h:109`.
- Remove now-unused per-node `#include "Nodes/...Type.h"` lines that existed *only* for registration. **Keep** every node header the app still references for `AddNode<T>` / `Connect(..., Config::SLOT, ...)` wiring (those are still compile-time dependencies).

- [ ] **Step 5: Full build + run the app once to verify behaviour**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m3_t6_app.log
grep -iE 'error|unresolved|BUILD FAILED' build_m3_t6_app.log | head
```
Then run the app per the project's run procedure and confirm it starts and renders (the graph must still find every node type it wires — now via `RegisterAllNodes`). Engineering rule: a change touching a runnable artifact must be observed running once.

- [ ] **Step 6: Run the full RenderGraph test suite**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
for t in ./build/libraries/RenderGraph/tests/Debug/test_*.exe; do "$t" --gtest_brief=1 || break; done
```
Expected: all pass, including `test_node_self_registration`.

- [ ] **Step 7: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/libraries/RenderGraph/CMakeLists.txt VIXEN/libraries/RenderGraph/tests/CMakeLists.txt VIXEN/application/main/source/VulkanGraphApplication.cpp VIXEN/application/main/include/VulkanGraphApplication.h
git commit -m "refactor(app): drop hand-maintained RegisterNodeTypes; use RegisterAllNodes (whole-archive keeps registrars)"
```

---

## Milestone 4 (OPTIONAL): Shrink the app's recompile blast radius

Only do this if Milestone 5 measurement shows the app TU is still the dominant cost on a node-header edit. The app includes ~37 node headers because it wires them — inherent to compile-time wiring — but splitting construction into per-subgraph TUs means editing one subgraph's wiring recompiles one small TU.

### Task 7: Extract per-subgraph construction into separate translation units

**Files:**
- Create: `application/main/source/graph/BuildMainGraph.cpp`, `application/main/source/graph/BuildUIGraph.cpp`
- Modify: `application/main/source/VulkanGraphApplication.cpp`, `application/main/CMakeLists.txt`

- [ ] **Step 1: Identify the construction methods**

Run:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
grep -nE 'VulkanGraphApplication::Build[A-Za-z]*Graph' VIXEN/application/main/source/VulkanGraphApplication.cpp
```
Expected: the existing `BuildMainGraph` / `BuildUIGraph` (or equivalently named) member definitions.

- [ ] **Step 2: Move each Build*Graph method body into its own .cpp**

For each, create `application/main/source/graph/Build<Name>Graph.cpp` containing only that method definition (`void VulkanGraphApplication::Build<Name>Graph(...) { ... }`) plus the node headers that subgraph wires. Leave the method declarations in `VulkanGraphApplication.h`. Remove the moved bodies from `VulkanGraphApplication.cpp`, and remove from `VulkanGraphApplication.cpp` any `#include "Nodes/..."` now used only by the moved bodies.

- [ ] **Step 3: Add the new TUs to the app target**

In `application/main/CMakeLists.txt`, add `source/graph/BuildMainGraph.cpp` and `source/graph/BuildUIGraph.cpp` to the app's source list.

- [ ] **Step 4: Full build green**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
cmake.exe -B build -S VIXEN
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m4.log
grep -iE 'error|BUILD FAILED' build_m4.log | head
```
Expected: no errors.

- [ ] **Step 5: Verify the blast-radius improvement**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
touch VIXEN/libraries/RenderGraph/include/Data/Nodes/CameraNodeConfig.h
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_m4_touch.log
grep -ciE 'Compiling|/c ' build_m4_touch.log
```
Expected: only the subgraph TU(s) that wire Camera recompile, not the whole app.

- [ ] **Step 6: Run the app once + tests, then commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
# run the app per project run procedure; confirm it renders
for t in ./build/libraries/RenderGraph/tests/Debug/test_*.exe; do "$t" --gtest_brief=1 || break; done
git add VIXEN/application/main/
git commit -m "refactor(app): split graph construction into per-subgraph TUs (shrinks node-edit recompile)"
```

---

## Milestone 5: Re-measure and document the win

### Task 8: Quantify improvement and record it

**Files:**
- Modify: this plan (results table) and/or `VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md`

- [ ] **Step 1: Re-run the two blast-radius measurements**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
touch VIXEN/libraries/RenderGraph/include/Data/Nodes/CameraNodeConfig.h
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_touch_camera_AFTER.log
grep -ciE 'Compiling|/c ' build_touch_camera_AFTER.log
touch VIXEN/libraries/RenderGraph/src/Nodes/CameraNode.cpp
cmake.exe --build build --config Debug --parallel 16 2>&1 | tee build_touch_cameracpp_AFTER.log
grep -ciE 'Compiling|/c ' build_touch_cameracpp_AFTER.log
```

- [ ] **Step 2: Fill in the results table**

| Scenario | TUs recompiled BEFORE | TUs recompiled AFTER (M2+M3, Ninja preset) |
|---|---|---|
| Edit one node config header | ~119 (VS baseline) | **5** (node + the app/benchmark/test TUs that wire it) |
| Edit one node `.cpp` body | ~119 (VS baseline) | **1** (just that node's TU) |
| `RenderGraphCore` recompiles on node change? | yes (dead registry) | **no — never** |

> Measured 2026-06-19 via the `vixen-ninja` preset (touch → rebuild → count
> `Building CXX object`). The 5-TU config-header cost is inherent to compile-time wiring
> (the app/benchmark/anchor-test TUs that `AddNode<>`/`Connect` Camera must see its config).
> Shrinking the app's single large `VulkanGraphApplication.cpp` TU is the optional **M4**, deferred.

- [ ] **Step 3: Update architecture docs (obsidian-first rule)**

In `VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md`, add a short section: the Core/Nodes library split, the self-registration mechanism (`VIXEN_REGISTER_NODE` + `RegisterAllNodes`), and the whole-archive requirement (why it exists — static-lib registrar stripping). Note that adding a node now needs only: create `Node.h/.cpp/Config.h`, add `VIXEN_REGISTER_NODE(...)`, and add the sources to `RENDERGRAPH_NODE_*` in CMake — no central registry edit.

- [ ] **Step 4: Commit**

```bash
cd /mnt/c/cpp/VBVS--VIXEN
git add VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md VIXEN/docs/superpowers/plans/2026-06-19-rendergraph-node-build-decoupling.md
git commit -m "docs(rendergraph): document Core/Nodes split + self-registration; record build-granularity results"
```

---

## Risks & Notes

- **Static-lib stripping is the #1 failure mode.** Self-registrars in a static library are dropped by the linker unless the archive is whole-linked. Milestone 3 Task 6 handles this; `test_node_self_registration` is the guard. If the test goes red after a CMake change, check the linker command for the whole-archive flag first.
- **Whole-archive pulls all node objects into every binary linking the facade** (no dead-strip of unused nodes). Acceptable for the app (it wires most nodes) and for the modding direction (all nodes available). If a future minimal tool needs only some nodes, it can link `RenderGraphCore` + a curated node set directly instead of the `RenderGraph` facade.
- **This plan does not add DLLs.** It is the pure build-decoupling path; it preserves every compile-time guarantee. DLL-per-node (runtime plugin nodes) is a separate, later effort tied to the modding/Meta-App work, and trades away compile-time slot-handle checking — do not conflate it with this.
- **`NodeTypeId` is a global counter** — never assert on it; use `GetTypeName()`.
- **Measurement caveat:** TU counts from build logs are a proxy; MSVC log formats vary. If `grep -c 'Compiling'` is noisy, count distinct `.obj` targets instead. The qualitative claims (Core no longer recompiles; registry chokepoint gone) are exact regardless.

---

## Self-Review

- **Spec coverage:** self-registration (M3) ✓, RenderGraphCore/Nodes split (M2) ✓, optional per-subgraph wiring (M4) ✓, measure incremental build (M0 + M5) ✓, plus the bonus dead-code removal (M1) that the investigation surfaced.
- **Placeholder scan:** every code/CMake step shows actual content; the 40-file node conversion gives the exact template + the complete file/symbol table (mechanical repetition, not a "similar to" hand-wave).
- **Type consistency:** `RegisterNodeFactory`, `NodeRegistrars`, `RegisterAllNodes`, `VIXEN_REGISTER_NODE`, the `RenderGraph`/`RenderGraphCore`/`RenderGraphNodes` target names, and `GetNodeTypeCount`/`HasNodeType` are used identically across tasks and match the real API in `NodeTypeRegistry.h`.
