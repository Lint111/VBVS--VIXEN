#pragma once

#include "VulkanApplicationBase.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/NodeTypeRegistry.h"
#include "error/VulkanError.h"
#include <memory>

using namespace Vixen::Vulkan::Resources;
using namespace Vixen::RenderGraph;

/**
 * @brief Graph-based Vulkan application using RenderGraph architecture
 * 
 * Uses RenderGraph for declarative, node-based rendering pipeline.
 * Supports advanced features like:
 * - Automatic resource management
 * - Frame graph optimization
 * - Multi-GPU rendering
 * - Dynamic pipeline reconfiguration
 */
class VulkanGraphApplication : public VulkanApplicationBase {
private:
    VulkanGraphApplication();

public:
    ~VulkanGraphApplication() override;

private:
    // Singleton pattern
    static std::unique_ptr<VulkanGraphApplication> instance;
    static std::once_flag onlyOnce;

public:
    static VulkanGraphApplication* GetInstance();

    // ====== Lifecycle Methods ======
    
    void Initialize() override;
    void DeInitialize() override;
    void Prepare() override;
    void Update() override;
    bool Render() override;

    // ====== Graph Management ======

    /**
     * @brief Get the render graph
     */
    inline RenderGraph* GetRenderGraph() const { return renderGraph.get(); }

    /**
     * @brief Get the node type registry
     */
    inline NodeTypeRegistry* GetNodeTypeRegistry() const { return nodeRegistry.get(); }

    /**
     * @brief Build the render graph
     * 
     * Override this method to construct your specific render graph.
     * Called during Prepare() phase.
     */
    virtual void BuildRenderGraph();

    /**
     * @brief Compile the render graph
     * 
     * Validates, optimizes, and prepares the graph for execution.
     */
    void CompileRenderGraph();

protected:
    /**
     * @brief Register all node types
     * 
     * Override to register custom node types with the registry.
     */
    virtual void RegisterNodeTypes();

    /**
     * @brief Create command pool for rendering
     */
    void CreateCommandPool();

    /**
     * @brief Create command buffers
     */
    void CreateCommandBuffers();

    /**
     * @brief Destroy command pool
     */
    void DestroyCommandPool();

    /**
     * @brief Destroy command buffers
     */
    void DestroyCommandBuffers();

    /**
     * @brief Record command buffer for a frame
     */
    void RecordCommandBuffer(uint32_t imageIndex);

private:
    // ====== Graph Components ======
    std::unique_ptr<NodeTypeRegistry> nodeRegistry;  // Node type registry
    std::unique_ptr<RenderGraph> renderGraph;        // Render graph instance

    // ====== Command Execution ======
    VkCommandPool commandPool;                       // Command pool
    std::vector<VkCommandBuffer> commandBuffers;     // Command buffers

    // ====== State ======
    bool graphCompiled;                              // Graph compilation state
};
