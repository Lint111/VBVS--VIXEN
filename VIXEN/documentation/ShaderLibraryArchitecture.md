# Shader Library Architecture

## Design Goals

1. **Type-safe shader management**: C++ structs with compile-time validation
2. **Multiple pipeline types**: Graphics, mesh, compute, ray tracing
3. **Per-program hot reload**: Individual shader recompilation without freezing entire library
4. **Background compilation**: Async shader compilation with controlled swap timing
5. **Device-agnostic library**: ShaderManagement lib has no VkDevice dependency
6. **Graph-side Vulkan**: ShaderLibraryNode creates VkShaderModule from SPIRV
7. **State-driven updates**: Integration with ResourceState flags for selective updates

## Architecture Overview

```
┌─────────────────────────────────────┐
│  ShaderManagement Library           │ (Static library - device-agnostic)
│  - ShaderLibrary (main API)         │
│  - BackgroundCompiler (threading)   │
│  - File I/O, SPIRV loading          │
│  - Outputs: SPIRV bytecode          │
└──────────────┬──────────────────────┘
               │ SPIRV bytecode
               ↓
┌─────────────────────────────────────┐
│  ShaderLibraryNode                  │ (RenderGraph integration)
│  - Wraps ShaderLibrary              │
│  - Creates VkShaderModule           │
│  - Outputs: ShaderProgramDescriptor │
│  - Has VkDevice access              │
└──────────────┬──────────────────────┘
               │ ShaderProgramDescriptor*
               ↓
┌─────────────────────────────────────┐
│  GraphicsPipelineNode               │
│  - Uses shader program descriptors  │
│  - Creates VkPipeline               │
└─────────────────────────────────────┘
```

## Core Data Structures

### Shader Stage Enumeration

```cpp
// ShaderStage.h
enum class ShaderStage : uint32_t {
    Vertex      = VK_SHADER_STAGE_VERTEX_BIT,
    Fragment    = VK_SHADER_STAGE_FRAGMENT_BIT,
    Compute     = VK_SHADER_STAGE_COMPUTE_BIT,
    Geometry    = VK_SHADER_STAGE_GEOMETRY_BIT,
    TessControl = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    TessEval    = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    Mesh        = VK_SHADER_STAGE_MESH_BIT_EXT,
    Task        = VK_SHADER_STAGE_TASK_BIT_EXT,
    RayGen      = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
    Miss        = VK_SHADER_STAGE_MISS_BIT_KHR,
    ClosestHit  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
    AnyHit      = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
    Intersection = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
};

inline VkShaderStageFlagBits ToVulkan(ShaderStage stage) {
    return static_cast<VkShaderStageFlagBits>(stage);
}
```

### Pipeline Type Constraints

```cpp
enum class PipelineTypeConstraint : uint8_t {
    Graphics,   // vertex+fragment required, geometry/tess optional
    Mesh,       // mesh+fragment required, task optional
    Compute,    // compute stage only
    RayTracing, // raygen+miss+closesthit required
};

// Compile-time validation helper
constexpr bool ValidateStages(PipelineTypeConstraint type,
                              const std::vector<ShaderStage>& stages) {
    switch (type) {
        case PipelineTypeConstraint::Graphics:
            return HasStage(stages, ShaderStage::Vertex) &&
                   HasStage(stages, ShaderStage::Fragment);

        case PipelineTypeConstraint::Mesh:
            return HasStage(stages, ShaderStage::Mesh) &&
                   HasStage(stages, ShaderStage::Fragment);

        case PipelineTypeConstraint::Compute:
            return stages.size() == 1 &&
                   HasStage(stages, ShaderStage::Compute);

        case PipelineTypeConstraint::RayTracing:
            return HasStage(stages, ShaderStage::RayGen) &&
                   HasStage(stages, ShaderStage::Miss) &&
                   HasStage(stages, ShaderStage::ClosestHit);
    }
    return false;
}
```

### Shader Stage Definition

```cpp
struct ShaderStageDefinition {
    ShaderStage stage;
    std::filesystem::path spirvPath;
    std::string entryPoint = "main";

    // Optional specialization constants
    std::unordered_map<uint32_t, uint32_t> specializationConstants;

    // File watching metadata
    std::filesystem::file_time_type lastModified;
    bool needsRecompile = false;
};
```

### Shader Program Definition

```cpp
struct ShaderProgramDefinition {
    uint32_t programId;
    std::string name;  // For debugging/logging
    PipelineTypeConstraint pipelineType;
    std::vector<ShaderStageDefinition> stages;

    // Validation
    bool IsValid() const {
        std::vector<ShaderStage> stageTypes;
        for (const auto& stage : stages) {
            stageTypes.push_back(stage.stage);
        }
        return ValidateStages(pipelineType, stageTypes);
    }

    // Helper accessors
    const ShaderStageDefinition* GetStage(ShaderStage stage) const {
        auto it = std::find_if(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
        return (it != stages.end()) ? &(*it) : nullptr;
    }

    bool HasStage(ShaderStage stage) const {
        return GetStage(stage) != nullptr;
    }
};
```

### Compiled Shader Program (Runtime)

```cpp
// ShaderProgramDescriptor.h
struct ShaderProgramDescriptor {
    uint32_t programId;
    std::string name;
    PipelineTypeConstraint pipelineType;

    // Compiled modules (one per stage)
    struct CompiledStage {
        ShaderStage stage;
        VkShaderModule module = VK_NULL_HANDLE;
        std::string entryPoint;
        std::vector<VkSpecializationMapEntry> specializationMap;
        std::vector<uint32_t> specializationData;

        // Generation tracking (for cache invalidation)
        uint64_t generation = 0;
    };

    std::vector<CompiledStage> stages;

    // State tracking
    ResourceState state = ResourceState::Clean;
    uint64_t generation = 0;

    // Vulkan pipeline stage create infos (cached)
    std::vector<VkPipelineShaderStageCreateInfo> vkStageInfos;

    // Helper methods
    VkShaderModule GetModule(ShaderStage stage) const {
        auto it = std::find_if(stages.begin(), stages.end(),
            [stage](const auto& s) { return s.stage == stage; });
        return (it != stages.end()) ? it->module : VK_NULL_HANDLE;
    }

    const std::vector<VkPipelineShaderStageCreateInfo>& GetVkStageInfos() const {
        return vkStageInfos;
    }

    void RebuildVkStageInfos() {
        vkStageInfos.clear();
        for (const auto& stage : stages) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = ToVulkan(stage.stage);
            stageInfo.module = stage.module;
            stageInfo.pName = stage.entryPoint.c_str();

            if (!stage.specializationData.empty()) {
                // TODO: Store VkSpecializationInfo separately
            }

            vkStageInfos.push_back(stageInfo);
        }
    }
};
```

## Background Compilation System

### Compilation Job

```cpp
// ShaderCompilationJob.h
struct ShaderCompilationJob {
    uint32_t programId;
    ShaderProgramDefinition definition;

    // Output (written by background thread)
    std::vector<std::vector<uint32_t>> spirvCode;  // One per stage
    std::vector<VkResult> results;  // Compilation results
    std::string errorMessage;

    // State
    enum class State {
        Pending,
        Compiling,
        Completed,
        Failed
    };

    std::atomic<State> state{State::Pending};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
};
```

### Background Compiler

```cpp
// ShaderCompiler.h
class ShaderCompiler {
public:
    ShaderCompiler(VkDevice device);
    ~ShaderCompiler();

    // Submit compilation job (returns job ID)
    uint32_t SubmitJob(const ShaderProgramDefinition& definition);

    // Check job status
    ShaderCompilationJob::State GetJobState(uint32_t jobId) const;

    // Retrieve completed job (removes from queue)
    std::optional<ShaderCompilationJob> TryGetCompletedJob(uint32_t jobId);

    // Cancel pending job
    void CancelJob(uint32_t jobId);

    // Shutdown compiler thread
    void Shutdown();

private:
    void CompilerThreadFunc();
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirvCode);

    VkDevice device;

    // Job queue
    std::queue<ShaderCompilationJob> pendingJobs;
    std::unordered_map<uint32_t, ShaderCompilationJob> completedJobs;

    // Threading
    std::thread compilerThread;
    std::mutex jobMutex;
    std::condition_variable jobCV;
    std::atomic<bool> shouldShutdown{false};

    std::atomic<uint32_t> nextJobId{0};
};
```

### Swap Control

```cpp
// ShaderSwapPolicy.h
enum class ShaderSwapPolicy {
    Immediate,      // Swap as soon as compilation completes (may cause stutter)
    NextFrame,      // Swap at beginning of next frame
    OnStateChange,  // Swap when entering/exiting play mode
    Manual,         // User explicitly calls SwapProgram()
};

struct ShaderSwapRequest {
    uint32_t programId;
    ShaderProgramDescriptor* newProgram;
    ShaderSwapPolicy policy;
    bool isReady = false;  // Set to true when safe to swap
};
```

## ShaderLibraryNode Implementation

### Configuration

```cpp
// ShaderLibraryNodeConfig.h
CONSTEXPR_NODE_CONFIG(ShaderLibraryNodeConfig, 0, 1, false) {
    // No parameters - programs registered via API calls

    // Output: Array of shader program descriptors
    CONSTEXPR_OUTPUT(SHADER_PROGRAMS, ShaderProgramDescriptor*, 0, false);

    ShaderLibraryNodeConfig() {
        // Output will be array of pointers to program descriptors
        INIT_OUTPUT_DESC(SHADER_PROGRAMS, "shader_programs",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handles
        );
    }
};
```

### Node Interface

```cpp
// ShaderLibraryNode.h
class ShaderLibraryNode : public TypedNode<ShaderLibraryNodeConfig> {
public:
    ShaderLibraryNode(const std::string& instanceName,
                      NodeType* nodeType,
                      VulkanDevice* device);
    ~ShaderLibraryNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // ===== Program Management API =====

    /**
     * @brief Register a shader program definition
     * @return Program ID for future reference
     */
    uint32_t RegisterProgram(const ShaderProgramDefinition& definition);

    /**
     * @brief Update program definition (triggers recompilation)
     */
    void UpdateProgram(uint32_t programId, const ShaderProgramDefinition& definition);

    /**
     * @brief Remove program from library
     */
    void RemoveProgram(uint32_t programId);

    /**
     * @brief Get compiled program descriptor
     */
    const ShaderProgramDescriptor* GetProgram(uint32_t programId) const;

    /**
     * @brief Get program by name (for debugging)
     */
    const ShaderProgramDescriptor* GetProgramByName(const std::string& name) const;

    // ===== Hot Reload API =====

    /**
     * @brief Trigger recompilation of specific program (async)
     * @param policy When to swap to new compiled version
     */
    void ReloadProgram(uint32_t programId, ShaderSwapPolicy policy = ShaderSwapPolicy::NextFrame);

    /**
     * @brief Check if program is currently recompiling
     */
    bool IsProgramReloading(uint32_t programId) const;

    /**
     * @brief Process pending shader swaps based on policy
     * @param allowSwaps If true, swaps programs marked as ready
     */
    void ProcessSwaps(bool allowSwaps = true);

    /**
     * @brief Manually swap to newly compiled program (if available)
     */
    bool SwapProgram(uint32_t programId);

    // ===== File Watching API =====

    /**
     * @brief Enable file watching for hot reload
     */
    void EnableFileWatching(bool enable = true);

    /**
     * @brief Poll file system for changes
     * @return Number of programs marked for recompilation
     */
    uint32_t CheckForFileChanges();

    // ===== Accessors =====

    size_t GetProgramCount() const { return programs.size(); }

    const std::vector<ShaderProgramDescriptor*>& GetAllPrograms() const {
        return programPointers;
    }

private:
    // Compilation helpers
    void CompileProgram(uint32_t programId, const ShaderProgramDefinition& definition);
    void CompileProgramAsync(uint32_t programId, const ShaderProgramDefinition& definition);
    VkShaderModule LoadShaderModule(const std::filesystem::path& spirvPath);
    void DestroyProgram(ShaderProgramDescriptor& program);

    // File watching
    void UpdateFileTimestamps(uint32_t programId);
    bool HasFileChanged(const ShaderStageDefinition& stage);

    // Storage
    std::unordered_map<uint32_t, ShaderProgramDefinition> definitions;
    std::unordered_map<uint32_t, ShaderProgramDescriptor> programs;
    std::unordered_map<std::string, uint32_t> nameToId;
    std::vector<ShaderProgramDescriptor*> programPointers;  // For array output

    // Background compilation
    std::unique_ptr<ShaderCompiler> compiler;
    std::unordered_map<uint32_t, uint32_t> programToJobId;  // programId -> jobId
    std::vector<ShaderSwapRequest> pendingSwaps;

    // File watching
    bool fileWatchingEnabled = false;

    uint32_t nextProgramId = 0;
};
```

## Usage Examples

### Registering Programs (Setup Phase)

```cpp
// In application setup or node initialization
ShaderLibraryNode* shaderLib = GetNode<ShaderLibraryNode>("shader_library");

// Graphics pipeline shader
ShaderProgramDefinition pbrProgram;
pbrProgram.name = "pbr_standard";
pbrProgram.pipelineType = PipelineTypeConstraint::Graphics;
pbrProgram.stages = {
    {ShaderStage::Vertex, "shaders/pbr.vert.spv"},
    {ShaderStage::Fragment, "shaders/pbr.frag.spv"}
};
uint32_t pbrProgramId = shaderLib->RegisterProgram(pbrProgram);

// Mesh shader pipeline
ShaderProgramDefinition terrainProgram;
terrainProgram.name = "terrain_mesh";
terrainProgram.pipelineType = PipelineTypeConstraint::Mesh;
terrainProgram.stages = {
    {ShaderStage::Task, "shaders/terrain.task.spv"},
    {ShaderStage::Mesh, "shaders/terrain.mesh.spv"},
    {ShaderStage::Fragment, "shaders/terrain.frag.spv"}
};
uint32_t terrainProgramId = shaderLib->RegisterProgram(terrainProgram);

// Compute shader
ShaderProgramDefinition particleCompute;
particleCompute.name = "particle_sim";
particleCompute.pipelineType = PipelineTypeConstraint::Compute;
particleCompute.stages = {
    {ShaderStage::Compute, "shaders/particles.comp.spv"}
};
uint32_t particleProgramId = shaderLib->RegisterProgram(particleCompute);
```

### Using in GraphicsPipelineNode

```cpp
// GraphicsPipelineNode::Compile()
void GraphicsPipelineNode::Compile() {
    // Get shader library output
    const auto& shaderPrograms = GetInputArray(SHADER_PROGRAMS);

    // Get specific program by ID (passed as parameter)
    uint32_t programId = GetParameterValue<uint32_t>("shaderProgramId", 0);

    ShaderProgramDescriptor* program = nullptr;
    for (auto* prog : shaderPrograms) {
        if (prog->programId == programId) {
            program = prog;
            break;
        }
    }

    if (!program) {
        NODE_LOG_ERROR("Shader program " + std::to_string(programId) + " not found");
        throw std::runtime_error("Invalid shader program ID");
    }

    // Validate pipeline type
    if (program->pipelineType != PipelineTypeConstraint::Graphics) {
        NODE_LOG_ERROR("Shader program not compatible with graphics pipeline");
        throw std::runtime_error("Invalid pipeline type");
    }

    // Check if shader changed (regenerate pipeline)
    if (program->state == ResourceState::Dirty ||
        program->generation != lastShaderGeneration) {

        NODE_LOG_INFO("Shader changed, rebuilding graphics pipeline");

        // Destroy old pipeline
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device->device, pipeline, nullptr);
        }

        // Create new pipeline with updated shaders
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(program->vkStageInfos.size());
        pipelineInfo.pStages = program->vkStageInfos.data();
        // ... rest of pipeline creation

        lastShaderGeneration = program->generation;
    }
}
```

### Hot Reload Workflow

```cpp
// Editor hot reload button callback
void OnShaderReloadRequested(uint32_t programId) {
    shaderLib->ReloadProgram(programId, ShaderSwapPolicy::NextFrame);

    // Show notification
    ShowEditorNotification("Recompiling shader program " +
                          std::to_string(programId) + "...");
}

// Game loop - beginning of frame
void BeginFrame() {
    // Check for file changes (if file watching enabled)
    uint32_t changedCount = shaderLib->CheckForFileChanges();
    if (changedCount > 0) {
        NODE_LOG_INFO(std::to_string(changedCount) + " shader(s) changed on disk");
    }

    // Process swaps (allows NextFrame policy swaps)
    shaderLib->ProcessSwaps(true);
}

// Play mode transition
void EnterPlayMode() {
    // Process swaps marked OnStateChange
    shaderLib->ProcessSwaps(true);
}

void ExitPlayMode() {
    // Process swaps marked OnStateChange
    shaderLib->ProcessSwaps(true);
}
```

### File Watching Integration

```cpp
// Enable automatic hot reload
shaderLib->EnableFileWatching(true);

// In game loop
void Update() {
    // Automatically check for changes every frame
    if (shaderLib->CheckForFileChanges() > 0) {
        // Changes detected, recompilation triggered automatically
        // with NextFrame policy
    }
}
```

## Integration with ResourceState

```cpp
// ShaderLibraryNode::Compile()
void ShaderLibraryNode::Compile() {
    // Process completed compilation jobs
    for (auto& [programId, jobId] : programToJobId) {
        auto completedJob = compiler->TryGetCompletedJob(jobId);
        if (!completedJob) continue;

        if (completedJob->state == ShaderCompilationJob::State::Completed) {
            // Create VkShaderModule from compiled SPIRV
            ShaderProgramDescriptor& program = programs[programId];

            // Mark old modules for deletion
            for (auto& stage : program.stages) {
                if (stage.module != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device->device, stage.module, nullptr);
                }
            }

            // Create new modules
            program.stages.clear();
            for (size_t i = 0; i < completedJob->spirvCode.size(); i++) {
                VkShaderModule module = CreateShaderModule(completedJob->spirvCode[i]);

                ShaderProgramDescriptor::CompiledStage compiledStage;
                compiledStage.stage = definitions[programId].stages[i].stage;
                compiledStage.module = module;
                compiledStage.entryPoint = definitions[programId].stages[i].entryPoint;
                compiledStage.generation++;

                program.stages.push_back(compiledStage);
            }

            // Rebuild Vulkan stage infos
            program.RebuildVkStageInfos();

            // Mark program as dirty (downstream pipelines need update)
            program.state = ResourceState::Dirty;
            program.generation++;

            NODE_LOG_INFO("Shader program " + program.name + " recompiled successfully");

            // Add to swap queue
            ShaderSwapRequest swap;
            swap.programId = programId;
            swap.newProgram = &program;
            swap.policy = /* get from somewhere */;
            swap.isReady = true;
            pendingSwaps.push_back(swap);
        }
        else if (completedJob->state == ShaderCompilationJob::State::Failed) {
            NODE_LOG_ERROR("Shader program " + std::to_string(programId) +
                          " compilation failed: " + completedJob->errorMessage);
        }
    }

    // Mark outputs
    for (auto& [id, program] : programs) {
        if (program.state == ResourceState::Dirty) {
            // Downstream nodes will see dirty flag and rebuild pipelines
            NODE_LOG_DEBUG("Shader program " + program.name + " marked dirty");
        }
    }
}
```

## Performance Characteristics

### Synchronous Compilation (Compile phase)
- **Initial load**: All programs compiled in Compile() phase
- **Blocking**: Stalls render graph compilation
- **Use case**: First-time load, no existing pipeline

### Asynchronous Compilation (Hot reload)
- **Background thread**: Compilation happens off main thread
- **Non-blocking**: Render graph continues with old shaders
- **Swap overhead**: ~1-2ms for pipeline recreation (only affected programs)
- **Use case**: Development, shader iteration

### File Watching Overhead
- **Per-frame cost**: ~0.1ms for 100 shader files (filesystem timestamp check)
- **Optimization**: Only check modified programs, not all files every frame

## Future Enhancements

1. **Shader cache**: Store compiled SPIRV to disk, skip recompilation if unchanged
2. **Pipeline cache**: Vulkan pipeline cache integration for faster pipeline creation
3. **Shader variants**: Compile multiple versions with different specialization constants
4. **Error recovery**: Fallback to last known good shader on compilation failure
5. **Hot reload notification**: EventBus integration to notify dependent nodes
6. **Shader includes**: Handle #include directives in GLSL before SPIRV compilation
7. **Parallel compilation**: Multiple compiler threads for large shader libraries

## Implementation Checklist

- [ ] Create ShaderStage.h with enums
- [ ] Create ShaderProgramDescriptor.h with runtime structures
- [ ] Create ShaderCompiler.h/cpp with background compilation
- [ ] Create ShaderLibraryNodeConfig.h
- [ ] Create ShaderLibraryNode.h/cpp
- [ ] Implement RegisterProgram/UpdateProgram/RemoveProgram
- [ ] Implement ReloadProgram with async compilation
- [ ] Implement ProcessSwaps with policy-based timing
- [ ] Implement file watching (CheckForFileChanges)
- [ ] Update GraphicsPipelineNode to use shader library
- [ ] Add ResourceState integration for dirty propagation
- [ ] Test hot reload in development environment