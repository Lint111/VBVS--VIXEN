#pragma once

#include "Profiler/ProfilerSystem.h"
#include <functional>
#include <string>

namespace Vixen::Profiler {

/**
 * @brief Adapter for connecting ProfilerSystem to RenderGraph lifecycle hooks
 *
 * This adapter provides callbacks that can be registered with GraphLifecycleHooks
 * without creating a dependency from Profiler to RenderGraph.
 *
 * Usage in application:
 * @code
 * ProfilerGraphAdapter adapter;
 * adapter.SetCurrentFrameIndex(frameIndex);
 *
 * // Register with GraphLifecycleHooks
 * hooks.RegisterNodeHook(NodeLifecyclePhase::PreExecute,
 *     [&adapter](NodeInstance* node) { adapter.OnNodePreExecute(node->GetName()); });
 * hooks.RegisterNodeHook(NodeLifecyclePhase::PostExecute,
 *     [&adapter](NodeInstance* node) { adapter.OnNodePostExecute(node->GetName()); });
 * hooks.RegisterNodeHook(NodeLifecyclePhase::PreCleanup,
 *     [&adapter](NodeInstance* node) { adapter.OnNodePreCleanup(node->GetName()); });
 * @endcode
 */
class ProfilerGraphAdapter {
public:
    ProfilerGraphAdapter() = default;
    ~ProfilerGraphAdapter() = default;

    /**
     * @brief Set frame context for profiling
     * Call at the start of each frame before any hooks fire
     */
    void SetFrameContext(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
        currentCmdBuffer_ = cmdBuffer;
        currentFrameIndex_ = frameIndex;
    }

    /**
     * @brief Called at frame start (after SetFrameContext)
     */
    void OnFrameBegin() {
        ProfilerSystem::Instance().OnFrameBegin(currentCmdBuffer_, currentFrameIndex_);
    }

    /**
     * @brief Called at frame end
     */
    void OnFrameEnd() {
        ProfilerSystem::Instance().OnFrameEnd(currentFrameIndex_);
    }

    /**
     * @brief Hook callback: Called before node execution
     * @param nodeName Name of the node about to execute
     */
    void OnNodePreExecute(const std::string& nodeName) {
        // Currently tracking frame-level, not node-level
        // Future: Could track per-node GPU time
        (void)nodeName;
    }

    /**
     * @brief Hook callback: Called after node execution
     * @param nodeName Name of the node that executed
     */
    void OnNodePostExecute(const std::string& nodeName) {
        (void)nodeName;
    }

    /**
     * @brief Hook callback: Called before node cleanup (for extracting node-specific metrics)
     * @param nodeName Name of the node about to cleanup
     */
    void OnNodePreCleanup(const std::string& nodeName) {
        (void)nodeName;
    }

    /**
     * @brief Called before dispatch (for GPU timing)
     */
    void OnDispatchBegin() {
        ProfilerSystem::Instance().OnDispatchBegin(currentCmdBuffer_, currentFrameIndex_);
    }

    /**
     * @brief Called after dispatch (for GPU timing)
     */
    void OnDispatchEnd(uint32_t dispatchWidth, uint32_t dispatchHeight) {
        ProfilerSystem::Instance().OnDispatchEnd(currentCmdBuffer_, currentFrameIndex_,
                                                  dispatchWidth, dispatchHeight);
    }

    /**
     * @brief Called before graph cleanup (extract metrics from nodes)
     */
    void OnPreGraphCleanup() {
        ProfilerSystem::Instance().OnPreCleanup();
    }

    /**
     * @brief Register a custom metrics extractor
     *
     * Use this to extract scene-specific data (voxel resolution, density) from nodes
     * before they are destroyed.
     */
    void RegisterExtractor(const std::string& name, NodeMetricsExtractor extractor) {
        ProfilerSystem::Instance().RegisterExtractor(name, std::move(extractor));
    }

    void UnregisterExtractor(const std::string& name) {
        ProfilerSystem::Instance().UnregisterExtractor(name);
    }

private:
    VkCommandBuffer currentCmdBuffer_ = VK_NULL_HANDLE;
    uint32_t currentFrameIndex_ = 0;
};

} // namespace Vixen::Profiler
