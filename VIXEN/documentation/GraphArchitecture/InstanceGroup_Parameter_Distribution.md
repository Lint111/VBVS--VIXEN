/**
 * @file InstanceGroup_Parameter_Distribution.md
 * @brief Solutions for per-instance parameter distribution in InstanceGroups
 * 
 * PROBLEM STATEMENT:
 * ==================
 * 
 * TextureLoaderNode has `file_path` as a PARAMETER (compile-time static):
 * 
 * ```cpp
 * struct TextureLoaderNodeConfig {
 *     // Parameter: Set once at node creation
 *     static constexpr ParamType PARAM_FILE_PATH = ParamType::String;
 *     
 *     // Inputs (resource slots)
 *     static constexpr auto DEVICE = ResourceSlot<VkDevice, 0, false>{};
 *     
 *     // Outputs
 *     static constexpr auto TEXTURE_VIEW = ResourceSlot<VkImageView, 0, false>{};
 * };
 * ```
 * 
 * For manual instances: Works perfectly
 * ```cpp
 * NodeHandle tex1 = graph->AddNode("TextureLoader", "wood_diffuse");
 * tex1->SetParameter("file_path", "wood.png");  // ✅ Different path per instance
 * 
 * NodeHandle tex2 = graph->AddNode("TextureLoader", "metal_normal");
 * tex2->SetParameter("file_path", "metal.png");  // ✅ Different path per instance
 * ```
 * 
 * For InstanceGroups: PROBLEM
 * ```cpp
 * InstanceGroupConfig config;
 * config.sharedParameters["file_path"] = "texture.png";  // ❌ All instances get SAME path
 * 
 * auto group = graph->CreateInstanceGroup(config);
 * // Spawns 6 instances → all load "texture.png"
 * // NO WAY to specify different paths for different instances
 * ```
 * 
 * 
 * SOLUTION EVOLUTION:
 * ===================
 * 
 * Solution 1: Per-Instance Parameter Arrays (Current Implementation)
 * -------------------------------------------------------------------
 * 
 * Add `perInstanceParameters` to InstanceGroupConfig:
 * 
 * ```cpp
 * InstanceGroupConfig config;
 * config.scalingPolicy = InstanceScalingPolicy::Fixed;
 * config.perInstanceParameters["file_path"] = {
 *     "texture_0.png",  // Instance 0
 *     "texture_1.png",  // Instance 1
 *     "texture_2.png",  // Instance 2
 *     "texture_3.png",  // Instance 3
 * };
 * 
 * auto group = graph->CreateInstanceGroup(config);
 * // Spawns 4 instances (array size determines count)
 * // Instance i receives perInstanceParameters["file_path"][i]
 * ```
 * 
 * PROS:
 * + Simple to implement
 * + Works for fixed instance counts
 * + No schema changes required
 * 
 * CONS:
 * - Only works with InstanceScalingPolicy::Fixed
 * - Instance count determined by parameter array size (inflexible)
 * - No way to distribute dynamic workloads (200 textures → 6 instances)
 * - Parameter arrays stored in config (memory overhead)
 * 
 * USE CASE:
 * Known set of specific textures (e.g., UI elements, skybox faces)
 * 
 * 
 * Solution 2: Input Slots for Per-Instance Data (RECOMMENDED - Future)
 * ---------------------------------------------------------------------
 * 
 * Change `file_path` from PARAMETER to INPUT SLOT:
 * 
 * ```cpp
 * struct TextureLoaderNodeConfig {
 *     // INPUTS
 *     static constexpr auto DEVICE = ResourceSlot<VkDevice, 0, false>{};
 *     static constexpr auto FILE_PATH = ResourceSlot<std::string, 1, true>{};  // ✅ Now a slot
 *     // or array variant:
 *     static constexpr auto FILE_PATHS = ResourceSlot<std::vector<std::string>, 2, true>{};
 *     
 *     // OUTPUTS
 *     static constexpr auto TEXTURE_VIEW = ResourceSlot<VkImageView, 0, false>{};
 * };
 * ```
 * 
 * Manual instance usage (unchanged):
 * ```cpp
 * NodeHandle pathProvider = graph->AddNode("StringConstant", "path_wood");
 * pathProvider->SetParameter("value", "wood.png");
 * 
 * NodeHandle tex1 = graph->AddNode("TextureLoader", "wood_texture");
 * Connect(graph, pathProvider, StringConstantConfig::OUTPUT,
 *               tex1, TextureLoaderConfig::FILE_PATH);
 * ```
 * 
 * InstanceGroup usage (DYNAMIC WORKLOAD):
 * ```cpp
 * // Upstream node provides array of paths
 * NodeHandle pathArray = graph->AddNode("TexturePathProvider", "streaming_paths");
 * pathArray->SetParameter("directory", "Assets/textures/");
 * pathArray->SetParameter("pattern", "*.png");
 * // Outputs: FILE_PATHS = ["tex0.png", "tex1.png", ..., "tex199.png"]  (200 paths)
 * 
 * // Instance group with workload batching
 * InstanceGroupConfig config;
 * config.scalingPolicy = InstanceScalingPolicy::WorkloadBatching;
 * config.preferredBatchSize = 50;  // 50 textures per loader
 * 
 * auto loaderGroup = graph->CreateInstanceGroup(config);
 * loaderGroup->AddInputTemplate(pathArray, 
 *                               TexturePathProviderConfig::FILE_PATHS,
 *                               TextureLoaderConfig::FILE_PATHS);
 * 
 * // During graph compilation:
 * // 1. Compiler sees FILE_PATHS slot connected
 * // 2. Reads array from upstream node → 200 elements
 * // 3. Calculates instances: ceil(200 / 50) = 4
 * // 4. Spawns 4 loader instances
 * // 5. Distributes paths:
 * //    - Instance 0 processes paths[0-49]
 * //    - Instance 1 processes paths[50-99]
 * //    - Instance 2 processes paths[100-149]
 * //    - Instance 3 processes paths[150-199]
 * // 6. Each instance's Execute() reads its assigned path range from slot
 * ```
 * 
 * PROS:
 * + Works with ALL scaling policies
 * + Dynamic instance count based on actual workload
 * + Data flows through graph (unified with rest of system)
 * + Memory efficient (no duplication in config)
 * + Compile-time type safety via ResourceSlot
 * 
 * CONS:
 * - Requires schema changes (parameter → slot migration)
 * - Slightly more complex graph wiring
 * - Needs workload distribution logic in node execution
 * 
 * USE CASE:
 * Dynamic texture streaming, asset loading pipelines, data-parallel processing
 * 
 * 
 * HYBRID APPROACH (Backward Compatible):
 * =======================================
 * 
 * Support BOTH parameter and slot:
 * 
 * ```cpp
 * struct TextureLoaderNodeConfig {
 *     // Parameter (legacy, for simple manual instances)
 *     static constexpr ParamType PARAM_FILE_PATH = ParamType::String;
 *     
 *     // Inputs
 *     static constexpr auto DEVICE = ResourceSlot<VkDevice, 0, false>{};
 *     static constexpr auto FILE_PATH = ResourceSlot<std::string, 1, true>{};       // Single path
 *     static constexpr auto FILE_PATHS = ResourceSlot<std::vector<std::string>, 2, true>{};  // Array
 *     
 *     // Outputs
 *     static constexpr auto TEXTURE_VIEW = ResourceSlot<VkImageView, 0, false>{};
 * };
 * ```
 * 
 * Priority during compilation:
 * ```cpp
 * std::string TextureLoaderNode::ResolveFilePath() {
 *     // Priority 1: FILE_PATH slot (explicit connection)
 *     if (auto* pathSlot = In<std::string>(FILE_PATH)) {
 *         return *pathSlot;
 *     }
 *     
 *     // Priority 2: FILE_PATHS slot with instance index
 *     if (auto* pathsSlot = In<std::vector<std::string>>(FILE_PATHS)) {
 *         uint32_t instanceIdx = GetInstanceIndexInGroup();  // From InstanceGroup context
 *         return (*pathsSlot)[instanceIdx];
 *     }
 *     
 *     // Priority 3: file_path parameter (legacy)
 *     if (auto* pathParam = GetParameter("file_path")) {
 *         return std::get<std::string>(*pathParam);
 *     }
 *     
 *     throw std::runtime_error("No file path provided");
 * }
 * ```
 * 
 * This enables:
 * - Manual instances: Use parameter (simple API)
 * - Fixed groups: Use perInstanceParameters array (Solution 1)
 * - Dynamic groups: Use FILE_PATHS slot (Solution 2)
 * 
 * 
 * IMPLEMENTATION ROADMAP:
 * =======================
 * 
 * Phase 1 (Current - Manual Instances):
 * - ✅ Parameters only
 * - ✅ Manual AddNode() for each texture
 * - ✅ TypedConnection wiring
 * 
 * Phase 2 (Near Future - Fixed Groups):
 * - Add perInstanceParameters to InstanceGroupConfig
 * - Implement SpawnInstances() with parameter distribution
 * - Support InstanceScalingPolicy::Fixed
 * - Use case: Known set of textures (UI, skybox, specific materials)
 * 
 * Phase 3 (Future - Dynamic Groups):
 * - Migrate critical parameters to input slots:
 *   * TextureLoader: file_path → FILE_PATH(S)
 *   * MeshProcessor: mesh_data → MESH_DATA(S)
 *   * ShaderCompiler: source_code → SOURCE_CODE(S)
 * - Implement workload distribution in node execution
 * - Support all InstanceScalingPolicy types
 * - Add upstream "provider" nodes (FilePathProvider, MeshDataProvider, etc.)
 * - Use case: Streaming systems, dynamic asset loading
 * 
 * Phase 4 (Far Future - Advanced Features):
 * - Task stealing between instances (load balancing)
 * - Multi-threaded execution with shared task queues
 * - GPU-driven instance spawning (indirect dispatch)
 * - Cross-frame persistence (texture cache across frames)
 * 
 * 
 * CURRENT RECOMMENDATION:
 * =======================
 * 
 * For NOW (current chapter):
 * - Use manual multi-instance with parameters
 * - Explicitly create each texture loader node
 * - Wire with TypedConnection API
 * 
 * Example:
 * ```cpp
 * // Current approach (works today)
 * NodeHandle woodDiffuse = graph->AddNode("TextureLoader", "wood_diffuse");
 * woodDiffuse->SetParameter("file_path", "Assets/textures/wood_diffuse.png");
 * 
 * NodeHandle woodNormal = graph->AddNode("TextureLoader", "wood_normal");
 * woodNormal->SetParameter("file_path", "Assets/textures/wood_normal.png");
 * 
 * Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
 *               woodDiffuse, TextureLoaderConfig::DEVICE);
 * Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
 *               woodNormal, TextureLoaderConfig::DEVICE);
 * ```
 * 
 * For FUTURE (InstanceGroup implementation):
 * - Document need for slot-based approach (Solution 2)
 * - Implement perInstanceParameters as stopgap (Solution 1)
 * - Plan migration path: parameter → slot for key nodes
 * 
 * ARCHITECTURAL INSIGHT:
 * ======================
 * 
 * The fundamental issue is:
 * 
 * PARAMETERS = Static configuration (compile-time decisions)
 * SLOTS = Dynamic data flow (runtime resource passing)
 * 
 * InstanceGroups need DYNAMIC per-instance data → SLOTS are correct abstraction
 * 
 * Parameters work for:
 * - Node behavior configuration (shader variant, buffer usage flags)
 * - Shared group settings (all instances use same format)
 * 
 * Slots work for:
 * - Per-instance workload data (which texture to load)
 * - Runtime workload distribution (task queue)
 * - Variable-size datasets (200 textures this frame, 50 next frame)
 * 
 * This aligns with graph philosophy: Data flows through edges, configuration via parameters.
 */
