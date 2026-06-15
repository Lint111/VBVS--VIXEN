#pragma once

#include "VulkanApplicationBase.h"
#include "Core/RenderGraph.h"
#include "Core/EngineContext.h"  // AR#7: instantiable engine aggregate
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
struct GLFWwindow;  // cross-platform window handle (GLFW); real include only in the .cpp
namespace Vixen::RenderGraph { class UIRenderNode; }  // composite HUD node; real include only in the .cpp

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
public:
    // Instantiable (AR#7): the former singleton (GetInstance + once_flag) is gone — a host
    // constructs and owns the application directly. Only main.cpp ever created it, and no
    // library code reached for the global instance, so there is nothing else to re-thread.
    VulkanGraphApplication();
    ~VulkanGraphApplication() override;

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
    inline RenderGraph* GetRenderGraph() const { return renderGraph; }

    /**
     * @brief Get the node type registry
     */
    inline NodeTypeRegistry* GetNodeTypeRegistry() const { return nodeRegistry; }

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
     * @brief Build a UI-only RmlUi demo graph (S0). Gated by the VIXEN_UI_DEMO env var.
     */
    void BuildUIGraph();

    /**
     * @brief Build an isolated instanced-cube raster demo graph (AR#31). Gated by the
     *        VIXEN_INSTANCING_DEMO env var. Renders N = gridDim^2 cubes from one mesh via
     *        a per-instance model-matrix SSBO (InstanceBufferNode) indexed by gl_InstanceIndex.
     */
    void BuildInstancingDemoGraph();

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
    virtual void RegisterNodeTypes(NodeTypeRegistry& registry);

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
    // ====== Engine (AR#7) ======
    // EngineContext OWNS the core graph subsystems (registry, bus, graph, and the autonomous
    // CalibrationStore). The app keeps non-owning views named as before so the existing call
    // sites are unchanged; they point into engine_ and are valid for its lifetime.
    std::unique_ptr<Vixen::RenderGraph::EngineContext> engine_;
    NodeTypeRegistry* nodeRegistry = nullptr;             // view: &engine_->Registry()
    RenderGraph* renderGraph = nullptr;                   // view: &engine_->Graph()
    Vixen::EventBus::MessageBus* messageBus = nullptr;    // view: &engine_->Bus()

    // ====== Application State ======
    uint32_t currentFrame;                           // Current frame index
    Vixen::Core::EngineTime time;                    // Time management
    bool graphCompiled;                              // Graph compilation state
    int width, height;                               // Window dimensions

    // ====== Shutdown Management ======
    bool shutdownRequested = false;                  // User requested shutdown
    std::unordered_set<std::string> shutdownAcksPending;  // Systems that need to acknowledge
    bool deinitialized = false;                      // Prevent double DeInitialize

    // ====== Phase 0.4: Loop System ======
    uint32_t physicsLoopID = 0;                      // Physics loop at 60Hz
    uint32_t simLoopID = 0;                          // Logic loop for the embedded sim (fixed cadence)
    NodeHandle voxelGridNode_{};                     // stored so the host can mark the scene dirty
    NodeHandle windowNode_{};                        // stored so GetWindowHandle() can query the WindowNode live
    NodeHandle uiRenderNode_{};                      // stored so GetUiRenderNode() can query the composite UI node live

    // NOTE: Command buffers, semaphores, and all Vulkan resources
    // are managed by the render graph nodes, not the application

public:
    // --- Embedded-sim driver seams (host-driven; VIXEN-agnostic) -----------------------------------
    // True when the SimLoop's fixed timestep is due this frame; outDt = that fixed timestep (seconds).
    bool ShouldStepLogic(double& outDt);
    // Mark the voxel scene for regeneration (the host re-registers its scene generator first, then
    // calls this; RecompileDirtyNodes rebuilds the SVO on the next Update()).
    void MarkVoxelSceneDirty();
    // Expose the GLFW window handle so the host can poll input (e.g. Space/period for pause/step).
    // Queries the WindowNode LIVE each call (the node owns the window post-de-own refactor + persists
    // across recompiles) — no cached handle, so no dangling-pointer window-capture bug.
    GLFWwindow* GetWindowHandle() const;
    // Expose the composite HUD node so the host can push live sim data (SetHudData) each frame. LIVE
    // lookup (like GetWindowHandle) — the node persists across recompiles; returns nullptr if unset
    // (e.g. the VIXEN_UI_DEMO path, which has no composite UI node).
    Vixen::RenderGraph::UIRenderNode* GetUiRenderNode() const;
};
