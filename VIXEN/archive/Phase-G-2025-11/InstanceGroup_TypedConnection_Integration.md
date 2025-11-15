/**
 * @file InstanceGroup_TypedConnection_Integration.md
 * @brief How TypedConnection works with InstanceGroups
 * 
 * ARCHITECTURAL LAYERS:
 * =====================
 * 
 * Layer 1: Manual Multi-Instance (Current Implementation)
 * --------------------------------------------------------
 * User creates SEMANTICALLY DIFFERENT nodes:
 * 
 * ```cpp
 * NodeHandle woodDiffuse = graph->AddNode("TextureLoader", "wood_diffuse");
 * NodeHandle metalNormal = graph->AddNode("TextureLoader", "metal_normal");
 * 
 * Connect(graph, woodDiffuse, TextureLoaderConfig::TEXTURE_VIEW,
 *               materialNode, MaterialConfig::DIFFUSE_SLOT);
 * Connect(graph, metalNormal, TextureLoaderConfig::TEXTURE_VIEW,
 *               materialNode, MaterialConfig::NORMAL_SLOT);
 * ```
 * 
 * Each represents DIFFERENT semantic entity (different texture file, different purpose).
 * TypedConnection API handles each as independent graph vertex.
 * 
 * 
 * Layer 2: InstanceGroup Auto-Parallel (New System)
 * --------------------------------------------------
 * User creates LOGICAL GROUP, graph spawns IDENTICAL WORKERS:
 * 
 * ```cpp
 * // Create instance group (1-N instances based on runtime conditions)
 * InstanceGroupConfig config;
 * config.groupName = "StreamingTextureLoaders";
 * config.nodeTypeName = "TextureLoader";
 * config.scalingPolicy = InstanceScalingPolicy::WorkloadBatching;
 * config.preferredBatchSize = 50;  // 50 textures per loader
 * 
 * auto loaderGroup = graph->CreateInstanceGroup(config);
 * 
 * // Define connection template (applied to ALL instances)
 * loaderGroup->AddInputTemplate(deviceNode, "DEVICE", "DEVICE", true);
 * loaderGroup->AddOutputTemplate(descriptorPool, "TEXTURE_VIEWS", "TEXTURE_VIEW", true);
 * 
 * // Compilation: Graph calculates optimal instance count
 * // 200 textures to load → spawns ceil(200/50) = 4 instances
 * // Internally creates:
 * //   StreamingTextureLoaders_0 (NodeInstance)
 * //   StreamingTextureLoaders_1 (NodeInstance)
 * //   StreamingTextureLoaders_2 (NodeInstance)
 * //   StreamingTextureLoaders_3 (NodeInstance)
 * 
 * // Auto-wires connections:
 * //   deviceNode → StreamingTextureLoaders_0
 * //   deviceNode → StreamingTextureLoaders_1
 * //   deviceNode → StreamingTextureLoaders_2
 * //   deviceNode → StreamingTextureLoaders_3
 * //   StreamingTextureLoaders_0 → descriptorPool[0]
 * //   StreamingTextureLoaders_1 → descriptorPool[1]
 * //   StreamingTextureLoaders_2 → descriptorPool[2]
 * //   StreamingTextureLoaders_3 → descriptorPool[3]
 * ```
 * 
 * All instances IDENTICAL except for workload distribution (task queue).
 * TypedConnection still operates at NodeHandle level (each instance is separate vertex).
 * InstanceGroup abstracts spawning/wiring logic.
 * 
 * 
 * KEY DIFFERENCES:
 * ================
 * 
 * | Aspect                  | Manual Multi-Instance          | InstanceGroup Auto-Parallel    |
 * |-------------------------|--------------------------------|--------------------------------|
 * | **Purpose**             | Different semantic entities    | Parallel workers for same task |
 * | **Creation**            | Explicit AddNode() calls       | CreateInstanceGroup() + spawn  |
 * | **Configuration**       | Different parameters per node  | Shared parameters across group |
 * | **Connections**         | Manually wired per node        | Template applied to all        |
 * | **Instance Count**      | User-controlled (static)       | Graph-calculated (dynamic)     |
 * | **Example**             | 3 different textures (wood,    | 6 texture loaders processing   |
 *                          | metal, stone)                  | 200 textures in parallel       |
 * | **Workload**            | Each has unique task           | Shared task queue or batching  |
 * | **Recompilation**       | Instances persist              | May respawn with different N   |
 * 
 * 
 * INTEGRATION POINTS:
 * ===================
 * 
 * 1. InstanceGroup::SpawnInstances() Implementation:
 * 
 * ```cpp
 * bool InstanceGroup::SpawnInstances(uint32_t instanceCount) {
 *     // Clear old instances
 *     DestroyInstances();
 *     
 *     // Spawn new instances
 *     for (uint32_t i = 0; i < instanceCount; ++i) {
 *         std::string instanceName = GenerateInstanceName(i);  // e.g., "StreamingTextureLoaders_3"
 *         NodeHandle handle = graph->AddNode(config.nodeTypeName, instanceName);
 *         
 *         // Apply shared parameters
 *         auto* instance = graph->GetInstance(handle);
 *         for (const auto& [paramName, paramValue] : config.sharedParameters) {
 *             instance->SetParameter(paramName, paramValue);
 *         }
 *         
 *         instances.push_back(handle);
 *     }
 *     
 *     // Wire connections using TypedConnection
 *     return WireConnections();
 * }
 * ```
 * 
 * 2. InstanceGroup::WireConnections() - TypedConnection Integration:
 * 
 * ```cpp
 * bool InstanceGroup::WireConnections() {
 *     ConnectionBatch batch(graph);
 *     
 *     // Apply input templates to all instances
 *     for (const auto& inputTemplate : config.inputTemplates) {
 *         if (inputTemplate.perInstance) {
 *             // Connect source to EACH instance
 *             for (NodeHandle instanceHandle : instances) {
 *                 // Use TypedConnection::Connect() - types deduced from slots
 *                 // Note: We need slot indices, not string names here
 *                 // This is where ConnectionTemplate needs enhancement
 *                 batch.Connect(
 *                     inputTemplate.sourceNode, 
 *                     GetSlotByName(inputTemplate.sourceSlotName),  // Resolve name→slot
 *                     instanceHandle,
 *                     GetSlotByName(inputTemplate.targetSlotName)
 *                 );
 *             }
 *         } else {
 *             // Connect source to ONLY FIRST instance
 *             batch.Connect(
 *                 inputTemplate.sourceNode,
 *                 GetSlotByName(inputTemplate.sourceSlotName),
 *                 instances[0],
 *                 GetSlotByName(inputTemplate.targetSlotName)
 *             );
 *         }
 *     }
 *     
 *     // Apply output templates
 *     for (const auto& outputTemplate : config.outputTemplates) {
 *         if (outputTemplate.perInstance) {
 *             // Each instance connects to target (often array slot)
 *             for (size_t i = 0; i < instances.size(); ++i) {
 *                 batch.ConnectToArray(
 *                     instances[i],
 *                     GetSlotByName(outputTemplate.sourceSlotName),
 *                     outputTemplate.targetNode,
 *                     GetSlotByName(outputTemplate.targetSlotName),
 *                     {static_cast<uint32_t>(i)}  // Array index = instance index
 *                 );
 *             }
 *         } else {
 *             // Only first instance connects
 *             batch.Connect(
 *                 instances[0],
 *                 GetSlotByName(outputTemplate.sourceSlotName),
 *                 outputTemplate.targetNode,
 *                 GetSlotByName(outputTemplate.targetSlotName)
 *             );
 *         }
 *     }
 *     
 *     // Atomically register all connections
 *     return batch.RegisterAll();
 * }
 * ```
 * 
 * 
 * CHALLENGE: String-Based vs Typed Slots
 * =======================================
 * 
 * Current TypedConnection API uses compile-time slot constants:
 * ```cpp
 * Connect(graph, node1, TextureLoaderConfig::TEXTURE_VIEW, ...);
 *                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *                       Compile-time constant with type info
 * ```
 * 
 * InstanceGroup templates use string names (runtime resolution):
 * ```cpp
 * AddInputTemplate(deviceNode, "DEVICE", "DEVICE");
 *                              ^^^^^^^^  ^^^^^^^^
 *                              String names (flexible but no compile-time checking)
 * ```
 * 
 * SOLUTION OPTIONS:
 * 
 * Option A: Hybrid Approach (Recommended)
 * ----------------------------------------
 * 1. Keep TypedConnection for manual multi-instance (compile-time safety)
 * 2. Add runtime slot resolution for InstanceGroup:
 * 
 * ```cpp
 * template<typename SourceSlot, typename TargetSlot>
 * void AddInputTemplate(
 *     NodeHandle sourceNode,
 *     const SourceSlot& sourceSlot,
 *     const TargetSlot& targetSlot,
 *     bool perInstance = true
 * ) {
 *     // Store slot indices (resolved at template registration time)
 *     ConnectionTemplate tmpl;
 *     tmpl.sourceNode = sourceNode;
 *     tmpl.sourceSlotIndex = SourceSlot::index;
 *     tmpl.targetSlotIndex = TargetSlot::index;
 *     tmpl.perInstance = perInstance;
 *     
 *     // Type validation
 *     static_assert(std::is_same_v<typename SourceSlot::Type, typename TargetSlot::Type>,
 *                   "Source and target slot types must match");
 *     
 *     config.inputTemplates.push_back(tmpl);
 * }
 * ```
 * 
 * Usage:
 * ```cpp
 * loaderGroup->AddInputTemplate(
 *     deviceNode, 
 *     DeviceNodeConfig::DEVICE,        // Compile-time constant
 *     TextureLoaderConfig::DEVICE      // Compile-time constant
 * );
 * ```
 * 
 * Benefits:
 * - Type safety at template registration (catches errors early)
 * - No string lookup overhead during WireConnections()
 * - Consistent with TypedConnection API philosophy
 * 
 * 
 * Option B: String-Based with Validation
 * ---------------------------------------
 * Keep string-based templates, validate at spawn time:
 * 
 * ```cpp
 * bool InstanceGroup::WireConnections() {
 *     // Resolve slot names to indices using NodeType schema
 *     for (const auto& tmpl : config.inputTemplates) {
 *         auto* sourceType = graph->GetInstance(tmpl.sourceNode)->GetNodeType();
 *         auto* targetType = graph->GetNodeType(config.nodeTypeName);
 *         
 *         uint32_t sourceSlot = sourceType->GetOutputIndex(tmpl.sourceSlotName);
 *         uint32_t targetSlot = targetType->GetInputIndex(tmpl.targetSlotName);
 *         
 *         // Validate types match
 *         auto sourceDesc = sourceType->GetOutputDescriptor(sourceSlot);
 *         auto targetDesc = targetType->GetInputDescriptor(targetSlot);
 *         if (sourceDesc->resourceType != targetDesc->resourceType) {
 *             throw std::runtime_error("Type mismatch in connection template");
 *         }
 *         
 *         // Wire using resolved indices
 *         for (NodeHandle instance : instances) {
 *             graph->ConnectNodes(tmpl.sourceNode, sourceSlot, instance, targetSlot);
 *         }
 *     }
 * }
 * ```
 * 
 * Benefits:
 * - More flexible (no compile-time dependency on config types)
 * - Easier for scripting/serialization
 * 
 * Drawbacks:
 * - Errors caught at runtime (during spawn)
 * - String lookup overhead
 * - Loss of compile-time guarantees
 * 
 * 
 * RECOMMENDED ARCHITECTURE:
 * =========================
 * 
 * Use **Option A (Hybrid)** with enhancement:
 * 
 * 1. TypedConnection for manual multi-instance (compile-time safety)
 * 2. Typed templates for InstanceGroup (compile-time safety + flexibility)
 * 3. Optional string-based API for scripting/serialization:
 * 
 * ```cpp
 * class InstanceGroup {
 * public:
 *     // Typed API (compile-time safe, recommended)
 *     template<typename SourceSlot, typename TargetSlot>
 *     void AddInputTemplate(NodeHandle source, const SourceSlot&, const TargetSlot&, bool perInstance = true);
 *     
 *     // String API (runtime resolution, for scripting)
 *     void AddInputTemplateByName(NodeHandle source, const std::string& sourceSlot, 
 *                                 const std::string& targetSlot, bool perInstance = true);
 * };
 * ```
 * 
 * This gives best of both worlds:
 * - C++ users get compile-time safety
 * - Scripting/serialization gets flexibility
 * - Same underlying storage (ConnectionTemplate with indices)
 * 
 * 
 * FINAL INTEGRATION EXAMPLE:
 * ==========================
 * 
 * ```cpp
 * // Manual multi-instance (different textures)
 * NodeHandle woodTexture = graph->AddNode("TextureLoader", "wood_diffuse");
 * NodeHandle metalTexture = graph->AddNode("TextureLoader", "metal_normal");
 * 
 * Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
 *               woodTexture, TextureLoaderConfig::DEVICE);
 * Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
 *               metalTexture, TextureLoaderConfig::DEVICE);
 * 
 * woodTexture->SetParameter("file_path", "textures/wood_diffuse.png");
 * metalTexture->SetParameter("file_path", "textures/metal_normal.png");
 * 
 * 
 * // InstanceGroup auto-parallel (streaming workload)
 * InstanceGroupConfig streamConfig;
 * streamConfig.groupName = "StreamingLoaders";
 * streamConfig.nodeTypeName = "TextureLoader";
 * streamConfig.scalingPolicy = InstanceScalingPolicy::WorkloadBatching;
 * streamConfig.preferredBatchSize = 50;
 * 
 * auto streamGroup = graph->CreateInstanceGroup(streamConfig);
 * 
 * // Typed connection templates
 * streamGroup->AddInputTemplate(
 *     deviceNode,
 *     DeviceNodeConfig::DEVICE,
 *     TextureLoaderConfig::DEVICE,
 *     true  // All instances connect to device
 * );
 * 
 * streamGroup->AddOutputTemplate(
 *     textureArrayNode,
 *     TextureArrayConfig::TEXTURE_VIEWS,  // Array input slot
 *     TextureLoaderConfig::TEXTURE_VIEW,
 *     true  // Each instance connects to array[i]
 * );
 * 
 * // During compilation:
 * // - Graph sees 200 textures need loading
 * // - Spawns ceil(200/50) = 4 instances
 * // - Wires: device → [StreamingLoaders_0, _1, _2, _3]
 * // - Wires: [StreamingLoaders_0, _1, _2, _3] → textureArray[0-3]
 * // - Distributes tasks: [0-49], [50-99], [100-149], [150-199]
 * 
 * // Result graph topology:
 * //   woodTexture (NodeInstance)
 * //   metalTexture (NodeInstance)
 * //   StreamingLoaders_0 (NodeInstance from group)
 * //   StreamingLoaders_1 (NodeInstance from group)
 * //   StreamingLoaders_2 (NodeInstance from group)
 * //   StreamingLoaders_3 (NodeInstance from group)
 * //
 * // All 6 are NodeInstance objects
 * // All 6 participate in topology sort
 * // But StreamingLoaders_* managed as group with shared config/workload
 * ```
 */
