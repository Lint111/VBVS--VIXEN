#pragma once

#include "VulkanApplicationBase.h"
#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "error/VulkanError.h"
#include "Time/EngineTime.h"
#include "EventBus/MessageBus.h"
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class VulkanRenderer;
class VulkanSwapChain;
class VulkanShader;

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
     * @brief Handle shutdown request from user (X button clicked)
     */
    void HandleShutdownRequest();

    /**
     * @brief Handle shutdown acknowledgment from a system
     */
    void HandleShutdownAck(const std::string& systemName);

    /**
     * @brief Complete shutdown after all systems acknowledged
     */
    void CompleteShutdown();

private:
    // ====== Graph Components ======
    std::unique_ptr<NodeTypeRegistry> nodeRegistry;  // Node type registry
    std::unique_ptr<RenderGraph> renderGraph;        // Render graph instance
    // Owned message bus for cross-system event dispatch (injected into RenderGraph)
    std::unique_ptr<Vixen::EventBus::MessageBus> messageBus;

    // ====== Application State ======
    uint32_t currentFrame;                           // Current frame index
    Vixen::Core::EngineTime time;                    // Time management
    bool graphCompiled;                              // Graph compilation state
    int width, height;                               // Window dimensions

    // ====== Shutdown Management ======
    bool shutdownRequested = false;                  // User requested shutdown
    std::unordered_set<std::string> shutdownAcksPending;  // Systems that need to acknowledge
    HWND windowHandle = nullptr;                     // Cached for destruction during shutdown
    NodeHandle windowNodeHandle;                     // Cached for accessing window HWND

    // ====== MVP Shader Management ======
    VulkanShader* triangleShader = nullptr;          // MVP: Direct shader (temporary)
    NodeHandle deviceNodeHandle;                     // MVP: Cached for post-compile shader loading
    NodeHandle pipelineNodeHandle;                   // MVP: Cached for post-compile connection
    NodeHandle shaderConstantNodeHandle;             // MVP: ConstantNode holding shader pointer

    // NOTE: Command buffers, semaphores, and all Vulkan resources
    // are managed by the render graph nodes, not the application
};
