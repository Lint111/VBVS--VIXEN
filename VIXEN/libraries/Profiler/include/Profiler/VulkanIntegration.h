#pragma once

#include "ProfilerSystem.h"
#include "ProfilerGraphAdapter.h"
#include "BenchmarkGraphFactory.h"
#include "BenchmarkRunner.h"

#include <vulkan/vulkan.h>
#include <functional>
#include <optional>

// Forward declarations to avoid circular dependencies
namespace Vixen::RenderGraph {
    class RenderGraph;
    class NodeInstance;
}

namespace Vixen::Profiler {

/**
 * @brief Helper for extracting Vulkan handles from RenderGraph nodes
 *
 * This helper bridges the gap between the RenderGraph's node-based architecture
 * and the Profiler's need for raw Vulkan handles. It provides type-safe extraction
 * of VkDevice, VkPhysicalDevice, and VkCommandBuffer from graph nodes.
 *
 * Usage:
 * @code
 * // After graph compilation
 * VulkanHandles handles = VulkanIntegrationHelper::ExtractFromGraph(graph);
 * if (handles.IsValid()) {
 *     ProfilerSystem::Instance().Initialize(handles.device, handles.physicalDevice);
 * }
 * @endcode
 */
struct VulkanHandles {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    uint32_t framesInFlight = 3;

    bool IsValid() const {
        return device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE;
    }
};

/**
 * @brief Helper class for integrating Profiler with real Vulkan resources
 *
 * Provides convenience methods for:
 * - Extracting Vulkan handles from compiled RenderGraph
 * - Initializing ProfilerSystem with graph resources
 * - Running benchmark suites on real GPU
 */
class VulkanIntegrationHelper {
public:
    /**
     * @brief Extract Vulkan handles from a compiled RenderGraph
     *
     * Searches for DeviceNode and extracts VkDevice, VkPhysicalDevice.
     *
     * @param graph Compiled RenderGraph containing DeviceNode
     * @param deviceNodeName Name of the device node (default: "main_device")
     * @return VulkanHandles struct with extracted handles (check IsValid())
     */
    static VulkanHandles ExtractFromGraph(
        Vixen::RenderGraph::RenderGraph* graph,
        const std::string& deviceNodeName = "main_device"
    );

    /**
     * @brief Initialize ProfilerSystem using handles from RenderGraph
     *
     * Convenience method that extracts handles and initializes the profiler.
     *
     * @param graph Compiled RenderGraph containing DeviceNode
     * @param deviceNodeName Name of the device node (default: "main_device")
     * @return true if initialization succeeded
     */
    static bool InitializeProfilerFromGraph(
        Vixen::RenderGraph::RenderGraph* graph,
        const std::string& deviceNodeName = "main_device"
    );

    /**
     * @brief Run a complete benchmark suite on real GPU
     *
     * High-level method that:
     * 1. Extracts Vulkan handles from graph
     * 2. Initializes ProfilerSystem
     * 3. Runs all configurations in the test matrix
     * 4. Exports results
     *
     * @param graph Compiled RenderGraph to use for benchmarking
     * @param configs Test configurations to run
     * @param outputDir Directory for result export
     * @param frameRenderer Function called each frame (should call graph->RenderFrame())
     * @return Number of successful tests
     */
    static size_t RunBenchmarkSuite(
        Vixen::RenderGraph::RenderGraph* graph,
        const std::vector<TestConfiguration>& configs,
        const std::filesystem::path& outputDir,
        std::function<bool()> frameRenderer
    );

    /**
     * @brief Create a ProfilerGraphAdapter wired to a graph
     *
     * Creates adapter and calls WireProfilerHooks to register lifecycle callbacks.
     *
     * @param graph RenderGraph to wire
     * @param benchGraph BenchmarkGraph structure (for node name lookup)
     * @return Configured ProfilerGraphAdapter (caller owns lifetime)
     */
    static std::unique_ptr<ProfilerGraphAdapter> CreateWiredAdapter(
        Vixen::RenderGraph::RenderGraph* graph,
        const BenchmarkGraph& benchGraph
    );

    /**
     * @brief Get VkCommandBuffer from current frame context
     *
     * Used when the application manages command buffers externally.
     * Returns the command buffer for the current frame index.
     *
     * @param graph RenderGraph with ComputeDispatchNode
     * @param dispatchNodeName Name of dispatch node (default: "test_dispatch")
     * @return VkCommandBuffer for current frame, or VK_NULL_HANDLE if not available
     */
    static VkCommandBuffer GetCurrentFrameCommandBuffer(
        Vixen::RenderGraph::RenderGraph* graph,
        const std::string& dispatchNodeName = "test_dispatch"
    );
};

/**
 * @brief RAII wrapper for profiler integration with graph
 *
 * Automatically initializes ProfilerSystem when constructed and
 * shuts it down when destroyed. Useful for scoped benchmark runs.
 *
 * Usage:
 * @code
 * {
 *     ScopedProfilerIntegration profiler(graph);
 *     if (profiler.IsValid()) {
 *         ProfilerSystem::Instance().StartTestRun(config);
 *         // ... render frames ...
 *         ProfilerSystem::Instance().EndTestRun();
 *     }
 * } // Profiler automatically shut down
 * @endcode
 */
class ScopedProfilerIntegration {
public:
    explicit ScopedProfilerIntegration(Vixen::RenderGraph::RenderGraph* graph);
    ~ScopedProfilerIntegration();

    // Non-copyable
    ScopedProfilerIntegration(const ScopedProfilerIntegration&) = delete;
    ScopedProfilerIntegration& operator=(const ScopedProfilerIntegration&) = delete;

    /// Check if initialization succeeded
    bool IsValid() const { return valid_; }

    /// Get the extracted Vulkan handles
    const VulkanHandles& GetHandles() const { return handles_; }

private:
    VulkanHandles handles_;
    bool valid_ = false;
};

} // namespace Vixen::Profiler
