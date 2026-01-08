#pragma once

#include "VulkanApplicationBase.h"
#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/TypedConnection.h"
#include "Core/CalibrationStore.h"  // Sprint 6.3: Persistence
#include "error/VulkanError.h"
#include "Time/EngineTime.h"
#include "MessageBus.h"
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class VulkanRenderer;
class VulkanSwapChain;

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
     * @brief Enable logging for a specific node (by handle)
     * @param handle Node handle
     * @param enableTerminal If true, also prints logs to console in real-time
     */
    void EnableNodeLogger(NodeHandle handle, bool enableTerminal = true);

    /**
     * @brief Enable logging for a specific node (by instance name)
     * @param nodeName Name of the node instance
     * @param enableTerminal If true, also prints logs to console in real-time
     */
    void EnableNodeLogger(const std::string& nodeName, bool enableTerminal = true);

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
    // Sprint 6.3: Calibration persistence for TaskProfiles
    std::unique_ptr<Vixen::RenderGraph::CalibrationStore> calibrationStore;

    // ====== Application State ======
    uint32_t currentFrame;                           // Current frame index
    Vixen::Core::EngineTime time;                    // Time management
    bool graphCompiled;                              // Graph compilation state
    int width, height;                               // Window dimensions

    // ====== Shutdown Management ======
    bool shutdownRequested = false;                  // User requested shutdown
    std::unordered_set<std::string> shutdownAcksPending;  // Systems that need to acknowledge
    HWND windowHandle = nullptr;                     // Cached for destruction during shutdown
    bool deinitialized = false;                      // Prevent double DeInitialize

    // ====== Phase 0.4: Loop System ======
    uint32_t physicsLoopID = 0;                      // Physics loop at 60Hz

    // NOTE: Command buffers, semaphores, and all Vulkan resources
    // are managed by the render graph nodes, not the application
};
