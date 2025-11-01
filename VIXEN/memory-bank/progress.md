# Progress

## Current State: Phase 0.3 COMPLETE ✅ - Continuing Phase 0

**Last Updated**: November 1, 2025

**Phase 0.1 Per-Frame Resources**: ✅ COMPLETE
- Created PerFrameResources helper class (ring buffer pattern)
- Refactored DescriptorSetNode for per-frame UBOs
- Wired SWAPCHAIN_PUBLIC and IMAGE_INDEX connections
- Tested successfully (3 distinct UBO buffers verified)
- **Race condition ELIMINATED**: CPU writes frame N while GPU reads frame N-1

**Phase 0.2 Frame-in-Flight Synchronization**: ✅ COMPLETE
- Created FrameSyncNode managing MAX_FRAMES_IN_FLIGHT=2 sync primitives
- Per-flight pattern: 2 fences + 2 semaphore pairs (vs 3 per-swapchain-image)
- Refactored SwapChainNode: removed per-image semaphore creation
- Refactored GeometryRenderNode: uses FrameSyncNode's semaphores
- Fixed PresentNode wiring: waits on GeometryRenderNode output (not FrameSyncNode)
- **CPU-GPU race ELIMINATED**: Fences prevent CPU from running >2 frames ahead

**Phase 0.3 Command Buffer Recording Strategy**: ✅ COMPLETE
- Created `StatefulContainer<T>` - reusable state tracking container
- Command buffers track Dirty/Ready/Stale/Invalid states
- Automatic dirty detection when inputs change (pipeline, descriptor sets, buffers)
- Only re-record when dirty (saves CPU work for static scenes)
- **Descriptor set invalidation bug FIXED**: Command buffers re-recorded when descriptor sets change

**Remaining Phase 0 Tasks** (2 sub-phases, 3-5 days):
1. ~~Per-frame resource management~~ ✅ COMPLETE
2. ~~Frame-in-flight synchronization~~ ✅ COMPLETE
3. ~~Command buffer recording strategy~~ ✅ COMPLETE
4. 🔴 Multi-rate update loop missing (blocks gameplay) - 2-3 days
5. 🔴 Template method pattern missing (boilerplate elimination) - 1-2 days

**Phase A (Persistent Cache)**: ⏸️ PAUSED - Resume after Phase 0 complete

**Previously Completed**: ShaderManagement Phases 0-5 (reflection automation, descriptor layouts, push constants) ✅

**Next**: Phase 0.4 - Multi-Rate Update Loop (enables gameplay)

---

## Completed Systems ✅

### 1. ShaderManagement Library Integration - Phase 2 Complete (October 31, 2025)
**Status**: ✅ Phase 0, Phase 1, Phase 2 Complete - Data-driven pipeline creation working

**Phase 0 - Library Compilation**:
- EventBus namespace issues (`EventBus::` → `Vixen::EventBus::`)
- SPIRV-Reflect namespace conflicts (added `::` prefix to global types)
- Hash namespace collision (created standalone FNV-1a implementation)
- API mismatches (PreprocessedSource, CompilationOutput fields)
- Move semantics issues (unique_ptr → shared_ptr conversion)
- Windows.h macro conflicts (std::min replaced with ternary)

**Phase 1 - RenderGraph Integration**:
- Created raw GLSL shader files (Shaders/Draw.vert, Shaders/Draw.frag)
- Implemented ShaderLibraryNode::Compile() using ShaderBundleBuilder
- Removed MVP manual loading from VulkanGraphApplication.cpp
- CashSystem cache logging activated (CACHE HIT/MISS working)
- Rendering works (triangle appears, zero validation errors)

**Phase 2 - Data-Driven Pipeline Creation** ✅:
- **PipelineLayoutCacher** - Separate cacher for sharing VkPipelineLayout
  - Transparent architecture (explicit `pipelineLayoutWrapper` field)
  - Convenience fallback (auto-creates from descriptorSetLayout)
  - Proper cleanup in virtual `Cleanup()` method
- **SPIRV Reflection Vertex Format Extraction** - Fixed hardcoded vec4 bug
  - Now uses `input->format` from SPIRV-Reflect directly
  - Correct formats: vec4 (109), vec2 (103), stride=24 bytes
- **Dynamic Shader Stage Support** - Removed hardcoded vertex+fragment requirement
  - `BuildShaderStages()` creates stages from bundle reflection
  - Supports all 14 Vulkan shader stage types
  - Shader keys derived from `bundle->GetProgramName()`
- **Data-Driven Vertex Input** - `BuildVertexInputsFromReflection()` extracts layout
- **Removed Legacy Fields** - Eliminated vertexShaderModule/fragmentShaderModule

**Phase 3 - Type-Safe UBO Updates via SDI Generation** ✅ (October 31, 2025):
- **SDI Split Architecture** - Generic `.si.h` (UUID-based) + shader-specific `Names.h`
  - `.si.h` contains UUID namespace with descriptor/struct definitions
  - `Names.h` provides constexpr constants and type aliases for specific shader
  - Enables interface sharing across shaders with same layout
- **UBO Struct Extraction** - Recursive SPIRV reflection extracts complete struct definitions
  - Walks `SpvReflectBlockVariable` member hierarchy
  - Extracts member names, types, offsets, strides
  - Handles nested structs, arrays, matrices
  - Correct matrix type detection (mat4, mat3x2, etc.)
- **Content-Hash UUID System** - Deterministic UUID generation from shader source
  - `GenerateContentBasedUuid()` creates stable hashes
  - Same interface = same UUID = shared `.si.h` file
  - Removed program name from generic SDI (program-agnostic)
- **Separate Output Directories** - Build-time vs runtime shader generation
  - Project-level: `generated/sdi/` (version-controlled, build-time shaders)
  - Runtime: `binaries/generated/sdi/` (app-specific, runtime shaders)
- **Index-Based Struct Linking** - `structDefIndex` prevents dangling pointer bugs
  - `SpirvDescriptorBinding::structDefIndex` references `structDefinitions[]`
  - No pointer invalidation during vector reallocation

**Phase 4 - Descriptor Set Layout Automation** ✅ (October 31, 2025):
- **DescriptorSetLayoutCacher** - Auto-creates VkDescriptorSetLayout from ShaderDataBundle
  - `ExtractBindingsFromBundle()` converts SPIR-V reflection to VkDescriptorSetLayoutBinding
  - Content-based caching using `descriptorInterfaceHash` from bundle
  - Helper functions for push constant extraction
  - Fixed namespace issues (`:Vixen::Vulkan::Resources::VulkanDevice*`)
- **GraphicsPipelineNode Integration** - Auto-generation with backward compatibility
  - Checks if manual layout provided via DESCRIPTOR_SET_LAYOUT input
  - Falls back to DescriptorSetLayoutCacher if not provided
  - Fixed variable shadowing bug (local `descriptorSetLayout` vs member)
  - Properly passes descriptor layout to PipelineCacher
- **Bug Fix** - Pipeline layout "0|0" issue resolved
  - Local variable was shadowing member variable
  - Changed to explicit `this->descriptorSetLayout` assignments
  - PipelineLayoutCacher now receives valid descriptor layout handle

**Phase 5 - Push Constants & Descriptor Pool Sizing** ✅ (October 31, 2025):
- **Push Constants Extraction** - Automatic from SPIR-V reflection
  - `ExtractPushConstantsFromReflection()` helper in DescriptorSetLayoutCacher
  - GraphicsPipelineNode extracts in Compile() method
  - Passed through PipelineCacher → PipelineLayoutCacher pipeline
  - Pipeline layout keys now include push constant data
- **PipelineCacher Updates** - Push constant support
  - Added `pushConstantRanges` field to PipelineCreateParams
  - pipeline_cacher.cpp passes to PipelineLayoutCacher (line 307)
  - GraphicsPipelineNode passes extracted ranges to params (line 706)
- **Descriptor Pool Sizing** - Reflection-based calculation
  - `CalculateDescriptorPoolSizes()` helper function
  - Analyzes SPIR-V bindings, counts by type
  - Scales by maxSets parameter (per-job allocation)
  - DescriptorSetNode uses helper (replaced manual counting)
- **Verification** - All systems working
  - Push constants: "pushConstantsColorBlock (offset=0, size=16, stages=0x10)"
  - Pool sizes: type=6 (UBO), type=1 (Sampler) calculated correctly
  - In-memory caching: CACHE HIT for repeated compilations
  - Zero validation errors, clean shutdown

**Build Status**: All systems operational, zero validation errors, application renders correctly

**Key Files**:
- `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp` (BuildVertexInputsFromReflection, BuildShaderStages, auto-descriptor-layout, push constants)
- `RenderGraph/src/Nodes/DescriptorSetNode.cpp` (CalculateDescriptorPoolSizes usage)
- `CashSystem/include/CashSystem/PipelineCacher.h` (pushConstantRanges field: line 61)
- `CashSystem/src/pipeline_cacher.cpp` (push constant passing: line 307)
- `CashSystem/include/CashSystem/PipelineLayoutCacher.h`
- `CashSystem/include/CashSystem/DescriptorSetLayoutCacher.h` (helper functions: Phase 4-5)
- `CashSystem/src/DescriptorSetLayoutCacher.cpp` (ExtractPushConstantsFromReflection, CalculateDescriptorPoolSizes)
- `ShaderManagement/src/SpirvReflector.cpp` (ExtractBlockMembers, matrix detection lines 195-204)
- `ShaderManagement/src/SpirvInterfaceGenerator.cpp` (GenerateNamesHeader, index-based linking)
- `ShaderManagement/include/ShaderManagement/SpirvReflectionData.h` (structDefIndex: line 102)
- `documentation/ShaderManagement/ShaderManagement-Integration-Plan.md`

### 2. CashSystem Caching Infrastructure (October 31, 2025)
**Status**: ✅ Complete with DescriptorSetLayoutCacher (Phase 4), zero validation errors

**Features**:
- TypedCacher template with MainCacher registry
- ShaderModuleCacher with CACHE HIT/MISS logging + Cleanup()
- PipelineCacher with cache activity tracking + Cleanup()
- PipelineLayoutCacher with transparent two-mode API + Cleanup()
- **DescriptorSetLayoutCacher** (NEW - Phase 4) with automatic SPIR-V extraction + Cleanup()
- Virtual Cleanup() method in CacherBase for polymorphic resource destruction
- MainCacher orchestration (ClearDeviceCaches, CleanupGlobalCaches)
- DeviceNode integration for device-dependent cache cleanup
- FNV-1a hash-based cache keys
- Thread-safe cacher operations

**Key Files**:
- `CashSystem/src/pipeline_layout_cacher.cpp`
- `CashSystem/include/CashSystem/PipelineLayoutCacher.h`
- `CashSystem/include/CashSystem/DescriptorSetLayoutCacher.h` (NEW - Phase 4)
- `CashSystem/src/DescriptorSetLayoutCacher.cpp` (NEW - Phase 4)
- `CashSystem/src/shader_module_cacher.cpp`
- `CashSystem/src/pipeline_cacher.cpp`
- `documentation/Cleanup-Architecture.md`

### 3. Variant Resource System (October 2025)
**Status**: ✅ Complete, zero warnings

- 25+ Vulkan types registered via `RESOURCE_TYPE_REGISTRY` macro
- Single-source type definition eliminates duplication
- `ResourceHandleVariant` provides compile-time type safety
- Zero-overhead `std::variant` (no virtual dispatch)

**Key Files**: `RenderGraph/include/Core/ResourceVariant.h`, `RenderGraph/include/Data/VariantDescriptors.h`

### 4. Typed Node API (October 2025)
**Status**: ✅ Complete, API enforced

- `TypedNode<ConfigType>` template with compile-time slot validation
- `In(SlotType)` / `Out(SlotType, value)` API replaces manual accessors
- Protected legacy methods (`GetInput/GetOutput`) - graph wiring only
- All node implementations migrated (15+ nodes)

**Key Files**: `RenderGraph/include/Core/TypedNodeInstance.h`, `RenderGraph/include/Core/NodeInstance.h`

### 5. EventBus Integration (October 2025)
**Status**: ✅ Complete with full recompilation cascade

- Type-safe event payloads, queue-based processing
- Cascade invalidation pattern (WindowResize → SwapChainInvalidated → FramebufferDirty)
- Nodes subscribe to events and mark themselves dirty
- CleanupStack provides dependency graph for cascade propagation
- While-loop recompilation ensures all dirty nodes are processed

**Key Files**: `EventBus/include/EventBus.h`, `RenderGraph/src/Core/RenderGraph.cpp`, `documentation/EventBusArchitecture.md`

### 6. RenderGraph Core (October 2025)
**Status**: ✅ Complete, modular library structure

- Graph compilation phases: Validate → AnalyzeDependencies → AllocateResources → GeneratePipelines
- Topology-based execution ordering
- Resource lifetime management (graph owns resources)

**Key Files**: `RenderGraph/src/Core/RenderGraph.cpp`, `RenderGraph/include/Core/NodeInstance.h`

### 7. Node Implementations (15+ Nodes)
**Status**: ✅ Core nodes implemented

**Catalog**: WindowNode, DeviceNode, CommandPoolNode, SwapChainNode, DepthBufferNode, VertexBufferNode, TextureLoaderNode, RenderPassNode, FramebufferNode, GraphicsPipelineNode, DescriptorSetNode, GeometryPassNode, GeometryRenderNode, PresentNode, ShaderLibraryNode

### 8. Build System (CMake Modular Architecture)
**Status**: ✅ Clean incremental compilation

Libraries: Logger, VulkanResources, EventBus, ShaderManagement, ResourceManagement, RenderGraph, CashSystem

**Build Status**: Exit Code 0, Zero warnings in RenderGraph

### 9. Handle-Based System (October 27, 2025)
**Status**: ✅ Complete, O(1) node access

- `NodeHandle` replaces string-based lookups throughout
- CleanupStack uses `std::unordered_map<NodeHandle, ...>`
- GetAllDependents() returns `std::unordered_set<NodeHandle>`
- Recompilation cascade reduced from O(n²) to O(n)

**Key Files**: `RenderGraph/include/Core/NodeHandle.h`, `RenderGraph/include/CleanupStack.h`

### 10. Cleanup Dependency System (October 27, 2025)
**Status**: ✅ Complete, zero validation errors

- Auto-detects dependencies via input connections
- Dependency-ordered destruction (children before parents)
- Visited tracking prevents duplicate cleanup
- ResetExecuted() enables recompilation cleanup
- All nodes use base class `device` member via `SetDevice()`

**Key Files**: `RenderGraph/include/CleanupStack.h`, `RenderGraph/src/Core/RenderGraph.cpp`

### 11. Vulkan Synchronization (October 27, 2025)
**Status**: ✅ Complete, two-semaphore pattern

- `imageAvailableSemaphores[]` per swapchain image (SwapChainNode)
- `renderCompleteSemaphores[]` per swapchain image (GeometryRenderNode)
- Proper GPU-GPU sync (no CPU stalls)
- Zero validation errors on shutdown

**Key Files**: `RenderGraph/src/Nodes/SwapChainNode.cpp`, `RenderGraph/src/Nodes/GeometryRenderNode.cpp`

---

## In Progress 🔨

### 1. Phase A - Persistent Cache Infrastructure (October 31, 2025)
**Priority**: HIGH - Critical Performance Feature
**Status**: ✅ COMPLETE - Async save/load working, stable device IDs, CACHE HIT confirmed

**Completed** ✅:
- Async cascade architecture (MainCacher → DeviceRegistry → Cachers)
- Stable device hash (vendorID | deviceID ^ driverVersion)
- Manifest-based cacher pre-registration (cacher_registry.txt)
- Cache loading timing (Setup → LoadCaches → Compile)
- Cache directory fixed (binaries/cache → cache)
- Double SaveAll crash fixed
- Zero validation errors, clean shutdown

**Test Results**:
- Cold start: CACHE MISS → creates cache files
- Warm start: CACHE HIT for ShaderModuleCacher and PipelineCacher
- Async I/O parallelism working across all cachers
- Stable device paths (Device_0x10de91476520 format)

**Key Files**:
- `CashSystem/src/device_identifier.cpp` (stable hash, manifest save/load)
- `RenderGraph/src/Core/RenderGraph.cpp` (cache loading in GeneratePipelines)
- `CashSystem/include/CashSystem/MainCacher.h` (SaveAllAsync/LoadAllAsync)
- `RenderGraph/src/Nodes/DeviceNode.cpp` (device registration)

### 2. ShaderManagement Integration - Phase 6 Hot Reload (October 31, 2025)
**Priority**: LOW - Advanced Feature for Developer Experience
**Status**: ✅ Phase 0-5 complete, Phase 6 not yet started

**Phase 0-5 Completed** ✅:
- ShaderManagement library enabled and compiling
- ShaderLibraryNode loading shaders via ShaderBundleBuilder
- CashSystem cache logging activated (in-memory caching working)
- Data-driven pipeline creation working
- SPIRV reflection extracting correct vertex formats
- All 14 shader stage types supported
- PipelineLayoutCacher sharing layouts
- **SDI generation with UBO struct extraction** ✅ (Phase 3)
- **Split architecture (generic `.si.h` + `Names.h`)** ✅ (Phase 3)
- **Content-hash UUID system** ✅ (Phase 3)
- **DescriptorSetLayoutCacher with automatic extraction** ✅ (Phase 4)
- **GraphicsPipelineNode auto-generation** ✅ (Phase 4)
- **Push constants extraction and propagation** ✅ (Phase 5)
- **Descriptor pool sizing from reflection** ✅ (Phase 5)
- Zero validation errors, rendering works

**Phase 6 Tasks** (Future Enhancement):
1. Implement file watching system for shader changes
2. Trigger ShaderManagement recompilation on file change
3. Create new ShaderDataBundle with updated reflection
4. Invalidate affected cache entries (pipelines, layouts)
5. Rebuild pipelines without frame drops
6. Handle resource synchronization during hot-swap
7. Preserve application state across shader reload

**Phase 6 Goal**: Enable runtime shader editing with instant feedback (no app restart).

**Phase 6 Deferred**: Requires additional infrastructure (file watcher, sync primitives).

**See**: `documentation/ShaderManagement/ShaderManagement-Integration-Plan.md` for complete roadmap

### 3. Architectural Improvements (Identified - October 31, 2025)
**Priority**: MIXED - Defer lifecycle, prioritize update loop
**Status**: ⏳ NOT STARTED - Identified during Phase A implementation

**A. Graph Lifecycle Phases Cleanup** (P2 - Cosmetic):
- **Effort**: 1-2 days (grows linearly with node count)
- **Trigger**: When node count > 30-50 OR before major graph refactor
- **Changes**:
  - Split `Setup()` into `RegisterCachers()` and `Setup()`
  - Three separate loops: RegisterCachers → LoadCaches → Setup → Compile
  - Cleaner separation of concerns
- **Verdict**: Defer until node count warrants it (currently 15 nodes)
- **Risk**: Low - complexity scales linearly, refactor cost grows slowly

**B. Update Loop Architecture** (P0 - Fundamental):
- **Effort**: 2-3 days
- **Priority**: HIGH - Foundation for gameplay features
- **Changes**:
  - Add `Update()` virtual method to NodeInstance
  - Multi-rate update scheduler in RenderGraph
  - Support PerFrame, Fixed60Hz, Fixed120Hz, FixedDelta update rates
  - Move main loop control into RenderGraph
- **Why Now**:
  - Fundamental execution model change
  - Harder to bolt on after gameplay nodes exist
  - Enables physics (fixed timestep), animation (per-frame), AI (lower rate)
- **Verdict**: Implement BEFORE Phase B (next major feature)
- **Risk**: HIGH if deferred - deep API changes across all future nodes

**See**: Architectural discussion from Phase A session (October 31, 2025)

### 4. Documentation Updates
**Priority**: MEDIUM

**Tasks**:
1. ⏳ Update systemPatterns.md with PipelineLayoutCacher pattern
2. ⏳ Document data-driven pipeline creation patterns
3. ⏳ Create Phase 2 architecture document

### 5. Node Expansion
**Priority**: MEDIUM

**Remaining**: ComputePipelineNode, ShadowMapPassNode, PostProcessNode, CopyImageNode

### 6. Example Scenes
**Priority**: LOW

**Planned**: Triangle scene, Textured mesh, Shadow mapping

---

## Architectural State

### Strengths ✅
1. **Type Safety**: Compile-time checking throughout resource system
2. **Data-Driven**: Zero hardcoded shader assumptions, all from reflection
3. **Transparent Caching**: Clear dependency tracking in CreateParams
4. **Resource Ownership**: Crystal clear RAII model
5. **Abstraction Layers**: Clean separation (RenderGraph → NodeInstance → TypedNode → ConcreteNode)
6. **EventBus**: Decoupled invalidation architecture
7. **Zero Warnings**: Professional codebase quality

### Known Limitations ⚠️
1. **Manual Descriptor Setup**: Nodes still create descriptor layouts manually (Phase 4 will automate)
2. **Parallelization**: No wave metadata for multi-threading
3. **Memory Aliasing**: No transient resource optimization
4. **Virtual Dispatch**: `Execute()` overhead (~2-5ns per call, acceptable <200 nodes)
5. **Memory Budget**: No allocation tracking

### Performance Characteristics
- **Current Capacity**: 100-200 nodes per graph
- **Bottleneck**: Virtual dispatch
- **Threading**: Single-threaded execution

**Verdict**: Ready for production single-threaded data-driven rendering. SDI generation complete with type-safe UBO structs. Next: descriptor automation.

---

## Next Immediate Steps

### Phase B: Update Loop Architecture (HIGH PRIORITY - Before next features)
**Duration**: 2-3 days
**Rationale**: Fundamental execution model change - must be done before gameplay nodes

**Tasks**:
1. Add `Update(float deltaTime)` virtual method to NodeInstance
2. Implement `UpdateScheduler` in RenderGraph
3. Support update rate policies:
   - `PerFrame` (runs every frame)
   - `Fixed60Hz` (physics simulation at 60 Hz)
   - `Fixed120Hz` (high-rate systems)
   - `FixedDelta(float seconds)` (custom rates)
4. Implement fixed-timestep accumulator pattern
5. Move main loop control into RenderGraph (RenderGraph::RunFrame())
6. Update VulkanGraphApplication to use RenderGraph::RunFrame()

**Goal**: Enable multi-rate update systems (physics at 60Hz, render at variable rate, AI at 10Hz)

**See**: `documentation/UpdateLoopArchitecture.md` (to be created during implementation)

### Phase C: Descriptor & Buffer Automation (2 weeks)
1. Auto-allocate uniform buffers from SPIR-V reflection
2. Auto-bind descriptor sets in GraphicsPipelineNode
3. Type-safe buffer updates via SDI headers
4. Remove manual descriptor setup from nodes

### Phase D: Cache Validation & Metrics (1 week)
1. Add cache metrics and statistics
2. Implement cache eviction policy (LRU)
3. Add cache warming strategies

### Phase E: Hot Reload (2 weeks - Optional)
1. Runtime shader recompilation
2. Pipeline invalidation on shader change
3. Automatic graph recompilation

---

## Documentation Status

### Memory Bank
- ✅ projectbrief.md - Updated
- ✅ progress.md - This file
- ✅ activeContext.md - Updated with Phase 2 completion
- ⏳ systemPatterns.md - Needs PipelineLayoutCacher pattern
- ⏳ techContext.md - Needs data-driven pipeline docs

### Documentation Folder
- ✅ EventBusArchitecture.md - Complete
- ✅ Cleanup-Architecture.md - CashSystem cleanup patterns
- ✅ ShaderManagement-Integration-Plan.md - 6-phase roadmap
- ✅ GraphArchitecture/ - 20+ docs
- ⏳ Phase2-DataDrivenPipelines.md - NEW (should document Phase 2 achievements)

---

## Success Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Build Warnings | 0 | 0 (RenderGraph) | ✅ |
| Type Safety | Compile-time | Compile-time | ✅ |
| Data-Driven Pipelines | Yes | Yes (Phase 2 ✅) | ✅ |
| SDI Generation | Yes | Yes (Phase 3 ✅) | ✅ |
| Descriptor Automation | Yes | Yes (Phase 4 ✅) | ✅ |
| Node Count | 20+ | 15+ | 🟡 75% |
| Code Quality | <200 instructions/class | Compliant | ✅ |
| Documentation | Complete | 85% | 🟡 |
| Example Scenes | 3+ | 0 | ❌ |

---

## Historical Context

**Origin**: Chapter-based Vulkan learning (Chapters 3-7)
**Pivot**: October 2025 - Graph-based architecture
**Milestone 1**: October 23, 2025 - Variant migration complete, typed API enforced, zero warnings
**Milestone 2**: October 31, 2025 - Data-driven pipeline creation complete (Phase 2)
**Milestone 3**: October 31, 2025 - SDI generation with type-safe UBO structs (Phase 3)
**Milestone 4**: October 31, 2025 - Descriptor set layout automation (Phase 4)
**Milestone 5**: October 31, 2025 - Push constants & descriptor pool automation (Phase 5)

**Key Decisions**:
1. Macro-based type registry (zero-overhead type safety)
2. Graph-owns-resources pattern
3. Protected legacy methods (enforces single API)
4. EventBus for invalidation
5. PipelineLayoutCacher for transparent layout sharing
6. SPIRV-Reflect format field for vertex input extraction
7. Data-driven pipeline creation (zero hardcoded assumptions)

See `documentation/ArchitecturalReview-2025-10.md` for industry comparison (Unity HDRP, Unreal RDG, Frostbite).
