// Render Graph Usage Example
// This file demonstrates how to use the Render Graph system

#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/NodeTypeRegistry.h"
#include "RenderGraph/Nodes/GeometryPassNode.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph::Examples {

/**
 * @brief Simple example: Create a basic render graph with a geometry pass
 * 
 * This example shows:
 * 1. Setting up the node type registry
 * 2. Creating a render graph
 * 3. Adding nodes
 * 4. Compiling the graph
 * 5. Executing the graph
 */
void SimpleGeometryPassExample(Vixen::Vulkan::Resources::VulkanDevice* device) {
    // Step 1: Create and setup the node type registry
    NodeTypeRegistry registry;
    
    // Register built-in node types
    registry.RegisterNodeType(std::make_unique<GeometryPassNodeType>());
    
    // Step 2: Create the render graph
    RenderGraph graph(device, &registry);
    
    // Step 3: Add a geometry pass node
    NodeHandle geometryNode = graph.AddNode("GeometryPass", "MainScene");
    
    // Get the node instance to configure parameters
    NodeInstance* instance = graph.GetInstance(geometryNode);
    if (instance) {
        // Set any parameters for the node
        instance->SetParameter("clearColor", glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
        instance->SetParameter("enableDepth", true);
    }
    
    // Step 4: Compile the graph
    // This analyzes dependencies, allocates resources, and creates pipelines
    try {
        graph.Compile();
    } catch (const std::exception& e) {
        // Handle compilation errors
        // In a real application, you'd log this properly
        return;
    }
    
    // Step 5: Execute the graph (during rendering)
    // VkCommandBuffer commandBuffer = ...; // Get your command buffer
    // graph.Execute(commandBuffer);
}

/**
 * @brief Advanced example: Multi-pass rendering with dependencies
 * 
 * This example shows:
 * - Creating multiple nodes
 * - Connecting nodes (establishing dependencies)
 * - Multi-pass rendering
 */
void MultiPassExample(Vixen::Vulkan::Resources::VulkanDevice* device) {
    NodeTypeRegistry registry;
    
    // Register node types
    registry.RegisterNodeType(std::make_unique<GeometryPassNodeType>());
    // In a real application, you'd register more types:
    // registry.RegisterNodeType(std::make_unique<ShadowMapNodeType>());
    // registry.RegisterNodeType(std::make_unique<PostProcessNodeType>());
    
    RenderGraph graph(device, &registry);
    
    // Add nodes
    NodeHandle geometryPass = graph.AddNode("GeometryPass", "MainScene");
    // NodeHandle shadowPass = graph.AddNode("ShadowMapPass", "Shadow_Light0");
    // NodeHandle postProcess = graph.AddNode("PostProcessPass", "ToneMapping");
    
    // Connect nodes (output -> input)
    // graph.ConnectNodes(shadowPass, 0, geometryPass, 0);  // Shadow map to geometry
    // graph.ConnectNodes(geometryPass, 0, postProcess, 0); // Geometry to post-process
    
    // Compile and execute
    try {
        graph.Compile();
        // graph.Execute(commandBuffer);
    } catch (const std::exception& e) {
        // Handle errors
    }
}

/**
 * @brief Helper function to register all built-in node types
 * 
 * In your application initialization, call this to setup the registry
 */
void RegisterAllBuiltInTypes(NodeTypeRegistry& registry) {
    // Geometry rendering
    registry.RegisterNodeType(std::make_unique<GeometryPassNodeType>());
    
    // TODO: Add more node types as they are implemented:
    // registry.RegisterNodeType(std::make_unique<ShadowMapNodeType>());
    // registry.RegisterNodeType(std::make_unique<PostProcessNodeType>());
    // registry.RegisterNodeType(std::make_unique<ComputeNodeType>());
    // registry.RegisterNodeType(std::make_unique<BlurNodeType>());
}

/**
 * @brief Integration example: Using render graph in your application
 * 
 * This shows how to integrate the render graph into your existing
 * Vulkan application structure.
 */
class RenderGraphIntegrationExample {
public:
    RenderGraphIntegrationExample(Vixen::Vulkan::Resources::VulkanDevice* device)
        : device(device)
        , registry(std::make_unique<NodeTypeRegistry>())
        , renderGraph(std::make_unique<RenderGraph>(device, registry.get()))
    {
        // Register all node types
        RegisterAllBuiltInTypes(*registry);
    }
    
    void SetupScene() {
        // Clear any existing graph
        renderGraph->Clear();
        
        // Build your scene graph
        NodeHandle mainPass = renderGraph->AddNode("GeometryPass", "MainScene");
        
        // Configure nodes
        if (auto* node = renderGraph->GetInstance(mainPass)) {
            node->SetParameter("clearColor", glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
        }
        
        // Compile once scene is built
        try {
            renderGraph->Compile();
            sceneCompiled = true;
        } catch (const std::exception& e) {
            sceneCompiled = false;
        }
    }
    
    void Render(VkCommandBuffer commandBuffer) {
        if (sceneCompiled) {
            renderGraph->Execute(commandBuffer);
        }
    }
    
    void Cleanup() {
        renderGraph->Clear();
        sceneCompiled = false;
    }
    
private:
    Vixen::Vulkan::Resources::VulkanDevice* device;
    std::unique_ptr<NodeTypeRegistry> registry;
    std::unique_ptr<RenderGraph> renderGraph;
    bool sceneCompiled = false;
};

} // namespace Vixen::RenderGraph::Examples

/*
 * USAGE NOTES:
 * 
 * 1. Node Type Registration:
 *    - Create a NodeTypeRegistry once at application start
 *    - Register all node types you'll use
 *    - Keep the registry alive for the entire application lifetime
 * 
 * 2. Render Graph Lifecycle:
 *    - Create RenderGraph with device and registry
 *    - Build graph by adding nodes and connecting them
 *    - Call Compile() to optimize and prepare for rendering
 *    - Call Execute() each frame to render
 *    - Call Clear() to rebuild the graph or Cleanup() when done
 * 
 * 3. Node Parameters:
 *    - Set parameters on node instances using SetParameter()
 *    - Parameters can be changed and the graph recompiled
 *    - Changing parameters invalidates cache
 * 
 * 4. Resource Management:
 *    - Resources are created automatically based on node schemas
 *    - Transient resources are aliased to save memory
 *    - Persistent resources are kept across frames
 * 
 * 5. Performance:
 *    - Compile() is expensive - do it only when the graph changes
 *    - Execute() is fast - can be called every frame
 *    - Use cache to avoid recompiling identical pipelines
 * 
 * 6. Multi-GPU:
 *    - Specify device when adding nodes: AddNode(type, name, device)
 *    - Graph automatically inserts transfer nodes between devices
 *    - Device affinity propagates through dependencies
 */
