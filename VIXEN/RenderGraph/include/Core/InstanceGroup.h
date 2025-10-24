/**
 * @file InstanceGroup.h
 * @brief Instance Group - Logical abstraction for batch-parallelized node execution
 * 
 * ARCHITECTURAL DISTINCTION:
 * =========================
 * 
 * 1. MANUAL INSTANCES (Multi-Instance Connections):
 *    - User explicitly creates: AddNode("TextureLoader", "wood_diffuse")
 *                              AddNode("TextureLoader", "metal_normal")
 *    - Each represents DIFFERENT semantic entity (different texture file, different purpose)
 *    - Each has UNIQUE parameters, connections, resource ownership
 *    - Example: Multiple materials, multiple lights, multiple cameras
 * 
 * 2. INSTANCE GROUPS (Auto-Parallel Batch Processing):
 *    - User declares ONE logical group: CreateInstanceGroup("DiffuseTextureLoader", 
 *                                                           minInstances = 1, 
 *                                                           maxInstances = DYNAMIC)
 *    - Graph scheduler calculates optimal instance count based on:
 *      * Device parallelism (compute queues, transfer queues)
 *      * Frame budget (target frametime)
 *      * Workload size (how many textures need loading this frame)
 *      * Memory constraints (VRAM availability)
 *    - Runtime spawns: diffuse_texture_0, _1, _2, ..., _N
 *    - All instances share SAME configuration template
 *    - Workload DISTRIBUTED across instances (batch processing)
 *    - Example: Load 100 textures → spawn 6 loaders → each handles ~17 textures
 * 
 * USE CASES:
 * ==========
 * - Texture streaming (parallel I/O and upload)
 * - Mesh processing (parallel vertex buffer creation)
 * - Shadow map generation (parallel rendering for multiple lights)
 * - Culling operations (parallel frustum culling across object batches)
 * - Post-processing chains (parallel blur passes on different regions)
 * 
 * PARAMETER DISTRIBUTION CHALLENGE:
 * ==================================
 * 
 * PROBLEM: Parameters are static configuration, but instances need different data
 * Example: TextureLoader has `file_path` parameter → all instances get SAME path
 * 
 * SOLUTIONS:
 * 1. perInstanceParameters - Array of values distributed to instances (current)
 *    Limitation: Only works with Fixed scaling policy
 * 
 * 2. Input Slots - Migrate parameters to slots for dynamic data flow (future)
 *    Recommended: file_path becomes FILE_PATH(S) input slot
 *    See: documentation/GraphArchitecture/InstanceGroup_Parameter_Distribution.md
 * 
 * CURRENT USAGE: Manual multi-instance for different semantic entities
 * FUTURE USAGE: InstanceGroups for parallel workload distribution
 */

#pragma once

#include "NodeInstance.h"
#include "NodeType.h"
#include "Data/BasicDataTypes.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace Vixen::RenderGraph {

// Forward declarations
class RenderGraph;

/**
 * @brief Scaling policy for dynamic instance count calculation
 */
enum class InstanceScalingPolicy : uint32_t {
    /**
     * @brief Fixed count - Always spawn exact number (no dynamic scaling)
     * Use: Predictable workloads (e.g., always 3 shadow maps for 3 directional lights)
     */
    Fixed = 0,

    /**
     * @brief Device-based - Scale with GPU parallelism
     * Instances = min(maxInstances, device.queueFamilyCount * parallelismFactor)
     * Use: I/O operations (texture loading, buffer uploads)
     */
    DeviceParallelism = 1,

    /**
     * @brief Workload-based - Scale with input task count
     * Instances = min(maxInstances, ceil(taskCount / preferredBatchSize))
     * Use: Data-parallel operations (mesh processing, culling)
     */
    WorkloadBatching = 2,

    /**
     * @brief Budget-based - Scale to meet frame budget
     * Instances = calculate based on targetFrametime and per-instance cost
     * Use: Adaptive quality (shadow resolution, LOD generation)
     */
    FrameBudget = 3,

    /**
     * @brief Hybrid - Combine multiple policies
     * Instances = min(deviceLimit, workloadLimit, budgetLimit)
     * Use: Complex scenarios requiring multiple constraints
     */
    Hybrid = 4
};

/**
 * @brief Configuration for instance group scaling
 */
struct InstanceGroupConfig {
    // Identity
    std::string groupName = "UnnamedGroup";
    std::string nodeTypeName;  // e.g., "TextureLoader"

    // Scaling constraints
    uint32_t minInstances = 1;           // Minimum instances to spawn (always >= 1)
    uint32_t maxInstances = 0;           // Maximum instances (0 = unlimited, capped by device)
    InstanceScalingPolicy scalingPolicy = InstanceScalingPolicy::WorkloadBatching;

    // Workload distribution parameters
    uint32_t preferredBatchSize = 1;     // Tasks per instance (for WorkloadBatching)
    float parallelismFactor = 1.0f;      // Multiplier for DeviceParallelism (0.5 = half device queues)
    float targetFrameMs = 16.67f;        // Target frame time for FrameBudget policy (60fps)
    float instanceCostMs = 1.0f;         // Estimated cost per instance (for FrameBudget)

    // Shared parameters (applied to all instances in group)
    std::unordered_map<std::string, ParamTypeValue> sharedParameters;

    // Per-instance parameters (distributed across instances)
    // Key = parameter name, Value = array of values (one per instance)
    // Example: {"file_path": ["tex0.png", "tex1.png", "tex2.png", ...]}
    // Instance i receives perInstanceParameters[paramName][i]
    // NOTE: Array size determines instance count if scalingPolicy == Fixed
    std::unordered_map<std::string, std::vector<ParamTypeValue>> perInstanceParameters;

    // Connection template (defines how instances connect to rest of graph)
    // Stored as descriptors, actual wiring happens after instance spawn
    struct ConnectionTemplate {
        NodeHandle sourceNode;           // Connects FROM this node (e.g., Device)
        std::string sourceSlotName;      // Output slot name
        std::string targetSlotName;      // Input slot name for spawned instances
        bool perInstance = true;         // true = each instance gets connection, false = only first
    };
    std::vector<ConnectionTemplate> inputTemplates;
    std::vector<ConnectionTemplate> outputTemplates;
};

/**
 * @brief Instance Group - Manages batch-parallelized node instances
 * 
 * Represents a logical execution unit that spawns 1-N NodeInstance objects
 * based on runtime conditions (device capabilities, workload, frame budget).
 * 
 * LIFECYCLE:
 * 1. User creates group: graph->CreateInstanceGroup(config)
 * 2. Graph compilation: CalculateOptimalInstanceCount() determines spawn count
 * 3. Instance spawn: Creates N NodeInstance objects with auto-generated names
 * 4. Connection wiring: Applies connection templates to all instances
 * 5. Execution: Distributes workload across instances (task queue or static batching)
 * 6. Cleanup: All instances destroyed when group destroyed
 * 
 * NAMING CONVENTION:
 * - Group name: "DiffuseTextureLoader" (user-defined semantic name)
 * - Instance names: "DiffuseTextureLoader_0", "DiffuseTextureLoader_1", ...
 * - Indices stable within frame, may change across recompilations
 */
class InstanceGroup {
public:
    /**
     * @brief Construct instance group
     * @param config Group configuration
     * @param graph Parent render graph (for instance creation)
     */
    InstanceGroup(const InstanceGroupConfig& config, RenderGraph* graph);

    ~InstanceGroup();

    // Prevent copying (unique resource ownership)
    InstanceGroup(const InstanceGroup&) = delete;
    InstanceGroup& operator=(const InstanceGroup&) = delete;

    // Identity
    const std::string& GetGroupName() const { return config.groupName; }
    const std::string& GetNodeTypeName() const { return config.nodeTypeName; }
    const InstanceGroupConfig& GetConfig() const { return config; }

    // Instance management
    uint32_t GetInstanceCount() const { return static_cast<uint32_t>(instances.size()); }
    const std::vector<NodeHandle>& GetInstances() const { return instances; }
    NodeHandle GetInstance(uint32_t index) const;

    /**
     * @brief Calculate optimal instance count based on scaling policy
     * 
     * Called during graph compilation to determine how many instances to spawn.
     * 
     * @param deviceInfo Device capabilities (queue counts, memory, etc.)
     * @param workloadSize Number of tasks to distribute (for WorkloadBatching)
     * @param currentFrameMs Current frame time (for FrameBudget adaptive scaling)
     * @return Calculated instance count (clamped to [minInstances, maxInstances])
     */
    uint32_t CalculateOptimalInstanceCount(
        const DeviceInfo& deviceInfo,
        uint32_t workloadSize = 0,
        float currentFrameMs = 0.0f
    ) const;

    /**
     * @brief Spawn instances based on calculated count
     * 
     * Creates NodeInstance objects, applies shared parameters, wires connections.
     * Called during graph compilation after CalculateOptimalInstanceCount().
     * 
     * @param instanceCount Number of instances to spawn
     * @return true if spawning succeeded, false if failed (type not found, connection error, etc.)
     */
    bool SpawnInstances(uint32_t instanceCount);

    /**
     * @brief Destroy all spawned instances
     * 
     * Called during graph recompilation or group destruction.
     * Removes instances from graph topology, releases resources.
     */
    void DestroyInstances();

    /**
     * @brief Distribute workload across instances
     * 
     * For WorkloadBatching policy, assigns tasks to instances.
     * Example: 100 textures, 6 instances → instance 0 gets tasks [0-16], 
     *                                       instance 1 gets tasks [17-33], etc.
     * 
     * @param totalTasks Total number of tasks to distribute
     * @return Vector of task ranges per instance: [ [0,16], [17,33], [34,50], ... ]
     */
    std::vector<std::pair<uint32_t, uint32_t>> DistributeWorkload(uint32_t totalTasks) const;

    /**
     * @brief Set shared parameter for all instances
     * 
     * Updates config.sharedParameters and propagates to all spawned instances.
     * 
     * @param paramName Parameter name
     * @param value Parameter value
     */
    void SetSharedParameter(const std::string& paramName, const ParamTypeValue& value);

    /**
     * @brief Set per-instance parameter array
     * 
     * Distributes different parameter values to each instance.
     * Example: SetPerInstanceParameter("file_path", {"tex0.png", "tex1.png", "tex2.png"})
     * 
     * CRITICAL CONSTRAINT:
     * - If scalingPolicy == Fixed: Array size DETERMINES instance count
     * - Otherwise: Array size must match calculated instance count (validated at spawn)
     * 
     * @param paramName Parameter name
     * @param values Array of values (one per instance)
     */
    void SetPerInstanceParameter(const std::string& paramName, const std::vector<ParamTypeValue>& values);

    /**
     * @brief Get per-instance parameter value for specific instance
     * 
     * @param paramName Parameter name
     * @param instanceIndex Instance index (0-based)
     * @return Parameter value for this instance, or nullptr if not found
     */
    const ParamTypeValue* GetInstanceParameter(const std::string& paramName, uint32_t instanceIndex) const;

    /**
     * @brief Add input connection template
     * 
     * Defines how external nodes connect TO instances in this group.
     * Example: Device node connects to all TextureLoader instances
     * 
     * @param sourceNode Node providing input resource
     * @param sourceSlot Output slot name on source node
     * @param targetSlot Input slot name on group instances
     * @param perInstance true = each instance gets connection, false = only first
     */
    void AddInputTemplate(
        NodeHandle sourceNode, 
        const std::string& sourceSlot,
        const std::string& targetSlot,
        bool perInstance = true
    );

    /**
     * @brief Add output connection template
     * 
     * Defines how instances connect TO external nodes.
     * Example: All TextureLoader instances connect to DescriptorSet array slots
     * 
     * @param targetNode Node consuming output resources
     * @param targetSlot Input slot name on target node (often array slot)
     * @param sourceSlot Output slot name on group instances
     * @param perInstance true = each instance connects, false = only first
     */
    void AddOutputTemplate(
        NodeHandle targetNode,
        const std::string& targetSlot,
        const std::string& sourceSlot,
        bool perInstance = true
    );

    // Compilation state
    bool IsSpawned() const { return spawned; }
    void MarkDirty() { dirty = true; }
    bool IsDirty() const { return dirty; }

private:
    InstanceGroupConfig config;
    RenderGraph* graph = nullptr;

    // Spawned instances
    std::vector<NodeHandle> instances;  // Handles to spawned NodeInstance objects
    bool spawned = false;
    bool dirty = false;

    // Helper: Generate instance name
    std::string GenerateInstanceName(uint32_t index) const;

    // Helper: Apply connection templates
    bool WireConnections();
};

/**
 * @brief USAGE EXAMPLES
 * 
 * Example 1: Fixed instance count (shadow maps for 3 directional lights)
 * ========================================================================
 * 
 * InstanceGroupConfig shadowConfig;
 * shadowConfig.groupName = "DirectionalShadowMaps";
 * shadowConfig.nodeTypeName = "ShadowMapPass";
 * shadowConfig.scalingPolicy = InstanceScalingPolicy::Fixed;
 * shadowConfig.minInstances = 3;
 * shadowConfig.maxInstances = 3;
 * shadowConfig.sharedParameters["resolution"] = 2048;
 * 
 * auto shadowGroup = graph->CreateInstanceGroup(shadowConfig);
 * shadowGroup->AddInputTemplate(deviceNode, "DEVICE", "DEVICE");
 * shadowGroup->AddOutputTemplate(shadowCompositeNode, "SHADOW_MAPS", "SHADOW_MAP", true);
 * 
 * // Result: Always spawns exactly 3 instances
 * // DirectionalShadowMaps_0, _1, _2
 * // All share resolution=2048 parameter
 * 
 * 
 * Example 2: Device-parallel texture loading (PER-INSTANCE PARAMETERS)
 * =====================================================================
 * 
 * InstanceGroupConfig textureConfig;
 * textureConfig.groupName = "SpecificTextureLoaders";
 * textureConfig.nodeTypeName = "TextureLoader";
 * textureConfig.scalingPolicy = InstanceScalingPolicy::Fixed;
 * textureConfig.minInstances = 4;
 * textureConfig.maxInstances = 4;
 * 
 * // Shared parameters (same for all instances)
 * textureConfig.sharedParameters["format"] = VK_FORMAT_R8G8B8A8_SRGB;
 * textureConfig.sharedParameters["mip_levels"] = 8;
 * 
 * // Per-instance parameters (different for each instance)
 * textureConfig.perInstanceParameters["file_path"] = {
 *     std::string("Assets/textures/wood_diffuse.png"),   // Instance 0
 *     std::string("Assets/textures/wood_normal.png"),    // Instance 1
 *     std::string("Assets/textures/metal_diffuse.png"),  // Instance 2
 *     std::string("Assets/textures/metal_normal.png")    // Instance 3
 * };
 * 
 * auto textureGroup = graph->CreateInstanceGroup(textureConfig);
 * 
 * // During SpawnInstances():
 * // SpecificTextureLoaders_0.SetParameter("file_path", "Assets/textures/wood_diffuse.png")
 * // SpecificTextureLoaders_1.SetParameter("file_path", "Assets/textures/wood_normal.png")
 * // SpecificTextureLoaders_2.SetParameter("file_path", "Assets/textures/metal_diffuse.png")
 * // SpecificTextureLoaders_3.SetParameter("file_path", "Assets/textures/metal_normal.png")
 * 
 * 
 * Example 3: Workload-batched mesh processing (SHARED WORKLOAD QUEUE)
 * ====================================================================
 * 
 * InstanceGroupConfig meshConfig;
 * meshConfig.groupName = "MeshProcessors";
 * meshConfig.nodeTypeName = "MeshProcessor";
 * meshConfig.scalingPolicy = InstanceScalingPolicy::WorkloadBatching;
 * meshConfig.minInstances = 1;
 * meshConfig.maxInstances = 16;
 * meshConfig.preferredBatchSize = 50;  // 50 meshes per instance
 * 
 * auto meshGroup = graph->CreateInstanceGroup(meshConfig);
 * 
 * // 100 meshes to process → spawns ceil(100/50) = 2 instances
 * // 800 meshes to process → spawns ceil(800/50) = 16 instances (capped at maxInstances)
 * 
 * // Distribute workload:
 * auto ranges = meshGroup->DistributeWorkload(800);
 * // ranges[0] = [0, 49]   (50 meshes)
 * // ranges[1] = [50, 99]  (50 meshes)
 * // ...
 * // ranges[15] = [750, 799] (50 meshes)
 * 
 * // NOTE: For workload batching, instances typically pull from SHARED QUEUE
 * //       rather than receiving per-instance parameters.
 * //       file_path would be provided via INPUT SLOT instead of parameter.
 * 
 * 
 * Example 4: Frame budget adaptive scaling
 * =========================================
 * 
 * InstanceGroupConfig lodConfig;
 * lodConfig.groupName = "LODGenerators";
 * lodConfig.nodeTypeName = "LODGenerator";
 * lodConfig.scalingPolicy = InstanceScalingPolicy::FrameBudget;
 * lodConfig.minInstances = 1;
 * lodConfig.maxInstances = 8;
 * lodConfig.targetFrameMs = 16.67f;  // 60fps target
 * lodConfig.instanceCostMs = 2.5f;   // Each instance adds ~2.5ms
 * 
 * auto lodGroup = graph->CreateInstanceGroup(lodConfig);
 * 
 * // Frame running at 10ms → can spawn floor((16.67-10)/2.5) = 2 instances
 * // Frame running at 14ms → can spawn floor((16.67-14)/2.5) = 1 instance
 * // Frame running at 17ms → already over budget, spawn minInstances = 1
 * 
 * 
 * PARAMETER DISTRIBUTION PATTERNS:
 * ================================
 * 
 * Pattern A: SHARED PARAMETERS (All instances identical)
 * -------------------------------------------------------
 * Use: Settings that apply uniformly to all instances
 * Examples: texture format, buffer usage flags, shader variant
 * 
 * config.sharedParameters["format"] = VK_FORMAT_R8G8B8A8_SRGB;
 * config.sharedParameters["usage"] = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
 * 
 * 
 * Pattern B: PER-INSTANCE PARAMETERS (Fixed instance count)
 * ----------------------------------------------------------
 * Use: Known set of different inputs (e.g., specific texture paths)
 * Constraint: scalingPolicy MUST be Fixed
 * Constraint: perInstanceParameters array size determines instance count
 * 
 * config.scalingPolicy = InstanceScalingPolicy::Fixed;
 * config.perInstanceParameters["file_path"] = {
 *     "texture_0.png", "texture_1.png", "texture_2.png"
 * };
 * // Spawns exactly 3 instances
 * 
 * 
 * Pattern C: INPUT SLOT (Dynamic instance count + per-instance data)
 * -------------------------------------------------------------------
 * Use: Variable workload where data comes from graph (not parameters)
 * Solution: Pass data through ARRAY INPUT SLOT instead of parameters
 * 
 * // Node config has array input slot
 * struct TextureLoaderConfig {
 *     static constexpr auto FILE_PATHS = ResourceSlot<std::vector<std::string>, 0, false>{};
 *     static constexpr auto DEVICE = ResourceSlot<VkDevice, 1, false>{};
 *     static constexpr auto TEXTURE_VIEW = ResourceSlot<VkImageView, 0, false>{};
 * };
 * 
 * // Upstream node provides array of file paths
 * NodeHandle pathProviderNode = graph->AddNode("FilePathProvider", "texture_paths");
 * pathProviderNode->SetParameter("paths", std::vector<std::string>{
 *     "tex0.png", "tex1.png", ..., "tex199.png"  // 200 paths
 * });
 * 
 * // Instance group receives paths via slot, spawns instances based on array size
 * auto loaderGroup = graph->CreateInstanceGroup(textureConfig);
 * loaderGroup->AddInputTemplate(pathProviderNode, "FILE_PATHS", "FILE_PATHS");
 * 
 * // During compilation:
 * // 1. Read FILE_PATHS slot → sees 200 elements
 * // 2. Calculate instances: ceil(200 / preferredBatchSize) = 4
 * // 3. Spawn 4 instances
 * // 4. Distribute: instance 0 gets paths[0-49], instance 1 gets paths[50-99], etc.
 * 
 * RECOMMENDATION: Use Pattern C (input slots) for dynamic workloads in future implementation
 * 
 * 
 * FUTURE ENHANCEMENT: Slot-Based Parameter Distribution
 * ======================================================
 * 
 * Problem: Parameters are compile-time static, but instance count is runtime dynamic
 * Solution: Add INPUT SLOT for per-instance data
 * 
 * struct TextureLoaderNodeConfig {
 *     // Option 1: Receive ARRAY of file paths (instance i processes paths[i])
 *     static constexpr auto FILE_PATH_ARRAY = ResourceSlot<std::vector<std::string>, 0, false>{};
 *     
 *     // Option 2: Receive SINGLE file path (for manual instances)
 *     static constexpr auto FILE_PATH = ResourceSlot<std::string, 1, true>{};  // Nullable
 *     
 *     // Outputs
 *     static constexpr auto TEXTURE_VIEW = ResourceSlot<VkImageView, 0, false>{};
 * };
 * 
 * Instance decides at compile time:
 * - If FILE_PATH_ARRAY connected → use array[instanceIndex]
 * - If FILE_PATH parameter set → use parameter
 * - If FILE_PATH slot connected → use single path (manual instance)
 * 
 * This enables:
 * - Manual instances: node->SetParameter("file_path", "specific_texture.png")
 * - Fixed groups: perInstanceParameters["file_path"] = {paths...}
 * - Dynamic groups: Connect(pathProvider, Config::PATHS, group, Config::FILE_PATH_ARRAY)
 */

} // namespace Vixen::RenderGraph
