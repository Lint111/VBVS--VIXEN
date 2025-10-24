/**
 * @file MultiInstanceConnectionExample.cpp
 * @brief Example: Creating multiple instances of the same node type with different connections
 * 
 * This demonstrates how the RenderGraph handles:
 * - Multiple instances of the same NodeType (e.g., 2 TextureLoader nodes)
 * - Different connection topologies for each instance
 * - Connection to same target node or different target nodes
 */

#include "Core/RenderGraph.h"
#include "Core/TypedConnection.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/DescriptorSetNode.h"

using namespace Vixen::RenderGraph;

/**
 * @brief Example 1: Multiple texture loaders connected to SAME descriptor set
 * 
 * Scenario: Load 2 different textures (diffuse + normal map) for a single material
 */
void Example1_MultipleTexturesToSameDescriptorSet(RenderGraph* graph) {
    // Create device node (shared resource provider)
    NodeHandle deviceNode = graph->AddNode("Device", "main_device");

    // Create 2 DIFFERENT instances of TextureLoader type
    // Key: Each has a UNIQUE instance name
    NodeHandle diffuseTextureNode = graph->AddNode("TextureLoader", "diffuse_texture");
    NodeHandle normalTextureNode = graph->AddNode("TextureLoader", "normal_texture");

    // Configure each texture loader with different parameters
    auto* diffuseLoader = static_cast<TextureLoaderNode*>(graph->GetInstance(diffuseTextureNode));
    diffuseLoader->SetParameter(TextureLoaderNodeConfig::PARAM_FILE_PATH, 
                                std::string("Assets/textures/diffuse.png"));

    auto* normalLoader = static_cast<TextureLoaderNode*>(graph->GetInstance(normalTextureNode));
    normalLoader->SetParameter(TextureLoaderNodeConfig::PARAM_FILE_PATH, 
                               std::string("Assets/textures/normal.png"));

    // Create a SINGLE descriptor set that will receive both textures
    NodeHandle descriptorSetNode = graph->AddNode("DescriptorSet", "material_descriptors");

    // Connect device to both loaders (shared resource)
    Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
            diffuseTextureNode, TextureLoaderNodeConfig::DEVICE);
    Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
            normalTextureNode, TextureLoaderNodeConfig::DEVICE);

    // Connect BOTH texture loaders to the SAME descriptor set
    // The descriptor set has array inputs for multiple textures
    ConnectionBatch batch(graph);
    batch.Connect(diffuseTextureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_VIEWS, 0)  // Array index 0
         .Connect(normalTextureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)  // Array index 1
         .RegisterAll();

    /**
     * Result topology:
     * 
     *   deviceNode (DEVICE output)
     *       ├─> diffuseTextureNode (DEVICE input) → (TEXTURE_VIEW output)
     *       │                                           └─> descriptorSetNode[0]
     *       └─> normalTextureNode (DEVICE input) → (TEXTURE_VIEW output)
     *                                                   └─> descriptorSetNode[1]
     * 
     * Graph topology identifies nodes by:
     * - NodeHandle (unique index per instance)
     * - Instance name ("diffuse_texture" vs "normal_texture")
     * 
     * Even though both are TextureLoader type, they are SEPARATE NodeInstance objects
     * with independent:
     * - Parameters (file paths)
     * - Resource ownership (VkImage, VkImageView, VkSampler)
     * - Graph edges (connections)
     */
}

/**
 * @brief Example 2: Multiple texture loaders connected to DIFFERENT descriptor sets
 * 
 * Scenario: Load textures for 2 different materials
 */
void Example2_MultipleTexturesToDifferentDescriptorSets(RenderGraph* graph) {
    NodeHandle deviceNode = graph->AddNode("Device", "main_device");

    // Material 1: Wood texture
    NodeHandle woodTextureNode = graph->AddNode("TextureLoader", "wood_texture");
    NodeHandle woodDescriptorNode = graph->AddNode("DescriptorSet", "wood_material");

    auto* woodLoader = static_cast<TextureLoaderNode*>(graph->GetInstance(woodTextureNode));
    woodLoader->SetParameter(TextureLoaderNodeConfig::PARAM_FILE_PATH, 
                            std::string("Assets/textures/wood.png"));

    // Material 2: Metal texture  
    NodeHandle metalTextureNode = graph->AddNode("TextureLoader", "metal_texture");
    NodeHandle metalDescriptorNode = graph->AddNode("DescriptorSet", "metal_material");

    auto* metalLoader = static_cast<TextureLoaderNode*>(graph->GetInstance(metalTextureNode));
    metalLoader->SetParameter(TextureLoaderNodeConfig::PARAM_FILE_PATH, 
                             std::string("Assets/textures/metal.png"));

    // Connect device to both loaders
    Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
            woodTextureNode, TextureLoaderNodeConfig::DEVICE);
    Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
            metalTextureNode, TextureLoaderNodeConfig::DEVICE);

    // Connect each texture to its OWN descriptor set
    Connect(graph, woodTextureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
            woodDescriptorNode, DescriptorSetNodeConfig::TEXTURE_VIEWS, 0);
    Connect(graph, metalTextureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
            metalDescriptorNode, DescriptorSetNodeConfig::TEXTURE_VIEWS, 0);

    /**
     * Result topology:
     * 
     *   deviceNode
     *       ├─> woodTextureNode → woodDescriptorNode
     *       └─> metalTextureNode → metalDescriptorNode
     * 
     * Completely separate material pipelines
     */
}

/**
 * @brief Example 3: Array connection - One source to multiple targets
 * 
 * Scenario: Single shadow map texture connected to multiple material descriptor sets
 */
void Example3_OneTextureToMultipleMaterials(RenderGraph* graph) {
    NodeHandle deviceNode = graph->AddNode("Device", "main_device");
    
    // Create ONE shadow map texture
    NodeHandle shadowMapNode = graph->AddNode("TextureLoader", "shadow_map");
    auto* shadowLoader = static_cast<TextureLoaderNode*>(graph->GetInstance(shadowMapNode));
    shadowLoader->SetParameter(TextureLoaderNodeConfig::PARAM_FILE_PATH, 
                              std::string("Assets/textures/shadow_map.png"));

    // Create 3 different material descriptor sets
    NodeHandle material1 = graph->AddNode("DescriptorSet", "material_1");
    NodeHandle material2 = graph->AddNode("DescriptorSet", "material_2");
    NodeHandle material3 = graph->AddNode("DescriptorSet", "material_3");

    // Connect device to shadow loader
    Connect(graph, deviceNode, DeviceNodeConfig::DEVICE,
            shadowMapNode, TextureLoaderNodeConfig::DEVICE);

    // Connect SAME shadow map to ALL 3 materials (shared resource)
    ConnectionBatch batch(graph);
    batch.Connect(shadowMapNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  material1, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)  // Slot 1: shadow map
         .Connect(shadowMapNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  material2, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)
         .Connect(shadowMapNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  material3, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)
         .RegisterAll();

    /**
     * Result topology:
     * 
     *   deviceNode → shadowMapNode (TEXTURE_VIEW output)
     *                    ├─> material1 (TEXTURE_VIEWS[1])
     *                    ├─> material2 (TEXTURE_VIEWS[1])
     *                    └─> material3 (TEXTURE_VIEWS[1])
     * 
     * One-to-many connection: Same VkImageView resource shared across multiple consumers
     */
}

/**
 * @brief Key Concepts Demonstrated:
 * 
 * 1. NODE IDENTITY:
 *    - NodeHandle: Unique integer index (e.g., 0, 1, 2, ...)
 *    - Instance Name: Unique string identifier (e.g., "diffuse_texture", "normal_texture")
 *    - NodeType: Template/class (e.g., TextureLoaderNodeType)
 *    - Multiple instances can share same NodeType but have different handles/names
 * 
 * 2. CONNECTION IDENTIFICATION:
 *    - Source: (NodeHandle, outputSlotIndex)
 *    - Target: (NodeHandle, inputSlotIndex, arrayIndex)
 *    - GraphEdge stores NodeInstance pointers (resolved from handles during ConnectNodes)
 *    - Different instances of same type have different NodeInstance* pointers
 * 
 * 3. EDGE STORAGE:
 *    struct GraphEdge {
 *        NodeInstance* source;        // Points to specific instance (e.g., diffuseTextureNode)
 *        uint32_t sourceOutputIndex;  // Slot index (e.g., TEXTURE_VIEW = 1)
 *        NodeInstance* target;        // Points to specific instance (e.g., descriptorSetNode)
 *        uint32_t targetInputIndex;   // Slot index (e.g., TEXTURE_VIEWS = 2)
 *    };
 * 
 * 4. TOPOLOGY TRACKING:
 *    - GraphTopology::nodes: std::set<NodeInstance*>  (each instance unique)
 *    - GraphTopology::edges: std::vector<GraphEdge>    (can have multiple edges between same nodes)
 *    - RenderGraph::nameToHandle: Maps instance names to handles
 *    - RenderGraph::instancesByType: Groups instances by NodeTypeId
 * 
 * 5. RESOURCE OWNERSHIP:
 *    - Each NodeInstance owns its own Vulkan resources (VkImage, VkBuffer, etc.)
 *    - Even if 2 nodes are same type, they have separate resource lifetimes
 *    - Resources can be SHARED via connections (same VkImageView passed to multiple nodes)
 *    - But OWNERSHIP remains with source node (RAII cleanup on node destruction)
 * 
 * 6. COMPILATION IMPLICATIONS:
 *    - Topology sort treats each NodeInstance as separate graph vertex
 *    - Dependencies tracked per-instance, not per-type
 *    - Execution order: All instances topologically sorted together
 *    - Example: If diffuseLoader depends on device, it executes after device
 *               If normalLoader depends on device, it also executes after device
 *               But diffuseLoader and normalLoader have no dependency on each other
 *               (unless explicitly connected)
 */

/**
 * @brief Example 4: Complex multi-material pipeline
 * 
 * Real-world scenario combining all concepts
 */
void Example4_CompleteMultiMaterialPipeline(RenderGraph* graph) {
    // Shared infrastructure
    NodeHandle deviceNode = graph->AddNode("Device", "main_device");
    NodeHandle renderPassNode = graph->AddNode("RenderPass", "main_pass");

    // Material 1: Wood (diffuse + normal + roughness)
    NodeHandle woodDiffuse = graph->AddNode("TextureLoader", "wood_diffuse");
    NodeHandle woodNormal = graph->AddNode("TextureLoader", "wood_normal");
    NodeHandle woodRoughness = graph->AddNode("TextureLoader", "wood_roughness");
    NodeHandle woodDescriptor = graph->AddNode("DescriptorSet", "wood_material");
    NodeHandle woodPipeline = graph->AddNode("GraphicsPipeline", "wood_pipeline");

    // Material 2: Metal (diffuse + normal + metallic)
    NodeHandle metalDiffuse = graph->AddNode("TextureLoader", "metal_diffuse");
    NodeHandle metalNormal = graph->AddNode("TextureLoader", "metal_normal");
    NodeHandle metalMetallic = graph->AddNode("TextureLoader", "metal_metallic");
    NodeHandle metalDescriptor = graph->AddNode("DescriptorSet", "metal_material");
    NodeHandle metalPipeline = graph->AddNode("GraphicsPipeline", "metal_pipeline");

    // Shared shadow map
    NodeHandle shadowMap = graph->AddNode("TextureLoader", "shared_shadow_map");

    // Use ConnectionBatch for complex wiring
    ConnectionBatch batch(graph);

    // Connect device to all loaders (7 total)
    batch.Connect(deviceNode, DeviceNodeConfig::DEVICE, woodDiffuse, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, woodNormal, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, woodRoughness, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, metalDiffuse, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, metalNormal, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, metalMetallic, TextureLoaderNodeConfig::DEVICE)
         .Connect(deviceNode, DeviceNodeConfig::DEVICE, shadowMap, TextureLoaderNodeConfig::DEVICE)
         
         // Wood material textures → wood descriptor set
         .Connect(woodDiffuse, TextureLoaderNodeConfig::TEXTURE_VIEW, 
                  woodDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 0)
         .Connect(woodNormal, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  woodDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)
         .Connect(woodRoughness, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  woodDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 2)
         .Connect(shadowMap, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  woodDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 3)  // Shared shadow map
         
         // Metal material textures → metal descriptor set
         .Connect(metalDiffuse, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  metalDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 0)
         .Connect(metalNormal, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  metalDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 1)
         .Connect(metalMetallic, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  metalDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 2)
         .Connect(shadowMap, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  metalDescriptor, DescriptorSetNodeConfig::TEXTURE_VIEWS, 3)  // Shared shadow map
         
         // Descriptor sets → pipelines
         .Connect(woodDescriptor, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  woodPipeline, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(metalDescriptor, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  metalPipeline, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         
         // Shared render pass → both pipelines
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  woodPipeline, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  metalPipeline, GraphicsPipelineNodeConfig::RENDER_PASS)
         
         .RegisterAll();

    /**
     * Final topology:
     * 
     * 14 unique NodeInstance objects:
     * - 1 Device
     * - 1 RenderPass
     * - 7 TextureLoaders (woodDiffuse, woodNormal, woodRoughness, metalDiffuse, metalNormal, metalMetallic, shadowMap)
     * - 2 DescriptorSets (woodDescriptor, metalDescriptor)
     * - 2 GraphicsPipelines (woodPipeline, metalPipeline)
     * 
     * Shared resources:
     * - deviceNode: Provides VkDevice to all 7 texture loaders
     * - shadowMap: Provides VkImageView to both descriptor sets
     * - renderPassNode: Provides VkRenderPass to both pipelines
     * 
     * Separate pipelines:
     * - Wood: 3 unique textures → wood descriptor → wood pipeline
     * - Metal: 3 unique textures → metal descriptor → metal pipeline
     * 
     * Execution order (example - actual order determined by topological sort):
     * 1. deviceNode.Compile()
     * 2. renderPassNode.Compile()
     * 3. All 7 textureLoaders.Compile() (parallel - no dependencies between them)
     * 4. woodDescriptor.Compile() (depends on wood textures + shadow map)
     * 5. metalDescriptor.Compile() (depends on metal textures + shadow map)
     * 6. woodPipeline.Compile() (depends on woodDescriptor + renderPass)
     * 7. metalPipeline.Compile() (depends on metalDescriptor + renderPass)
     */
}
