#include "Profiler/VulkanIntegration.h"
#include "Profiler/ProfilerSystem.h"
#include "Profiler/BenchmarkGraphFactory.h"

// RenderGraph includes (for node access)
#include <Core/RenderGraph.h>
#include <Nodes/DeviceNode.h>
#include <Nodes/ComputeDispatchNode.h>
#include "VulkanDevice.h"

#include <stdexcept>

namespace Vixen::Profiler {

namespace RG = Vixen::RenderGraph;

VulkanHandles VulkanIntegrationHelper::ExtractFromGraph(
    RG::RenderGraph* graph,
    const std::string& deviceNodeName)
{
    VulkanHandles handles;

    if (!graph) {
        return handles;
    }

    // Find DeviceNode by name
    auto* nodeInstance = graph->GetInstanceByName(deviceNodeName);
    if (!nodeInstance) {
        return handles;
    }

    // Cast to DeviceNode
    auto* deviceNode = dynamic_cast<RG::DeviceNode*>(nodeInstance);
    if (!deviceNode) {
        return handles;
    }

    // Get VulkanDevice wrapper
    auto* vulkanDevice = deviceNode->GetVulkanDevice();
    if (!vulkanDevice) {
        return handles;
    }

    // Extract Vulkan handles
    handles.device = vulkanDevice->device;
    handles.physicalDevice = vulkanDevice->gpu ? *vulkanDevice->gpu : VK_NULL_HANDLE;
    handles.graphicsQueue = vulkanDevice->queue;
    handles.graphicsQueueFamily = vulkanDevice->graphicsQueueIndex;
    handles.framesInFlight = 3; // Default, could be extracted from FrameSyncNode

    return handles;
}

bool VulkanIntegrationHelper::InitializeProfilerFromGraph(
    RG::RenderGraph* graph,
    const std::string& deviceNodeName)
{
    VulkanHandles handles = ExtractFromGraph(graph, deviceNodeName);

    if (!handles.IsValid()) {
        return false;
    }

    ProfilerSystem::Instance().Initialize(
        handles.device,
        handles.physicalDevice,
        handles.framesInFlight
    );

    return true;
}

size_t VulkanIntegrationHelper::RunBenchmarkSuite(
    RG::RenderGraph* graph,
    const std::vector<TestConfiguration>& configs,
    const std::filesystem::path& outputDir,
    std::function<bool()> frameRenderer)
{
    if (!graph || configs.empty()) {
        return 0;
    }

    // Extract Vulkan handles and initialize profiler
    VulkanHandles handles = ExtractFromGraph(graph);
    if (!handles.IsValid()) {
        return 0;
    }

    auto& profiler = ProfilerSystem::Instance();

    // Initialize if not already
    if (!profiler.IsInitialized()) {
        profiler.Initialize(handles.device, handles.physicalDevice, handles.framesInFlight);
    }

    profiler.SetOutputDirectory(outputDir);
    profiler.StartTestSuite("Vulkan Integration Benchmark");

    size_t successCount = 0;

    for (const auto& config : configs) {
        profiler.StartTestRun(config);

        uint32_t totalFrames = config.warmupFrames + config.measurementFrames;
        bool success = true;

        for (uint32_t frame = 0; frame < totalFrames && success; ++frame) {
            success = frameRenderer();
        }

        if (success && profiler.IsTestRunActive()) {
            profiler.EndTestRun(true);
            successCount++;
        } else if (profiler.IsTestRunActive()) {
            profiler.EndTestRun(false);
        }
    }

    profiler.EndTestSuite();

    return successCount;
}

std::unique_ptr<ProfilerGraphAdapter> VulkanIntegrationHelper::CreateWiredAdapter(
    RG::RenderGraph* graph,
    const BenchmarkGraph& benchGraph)
{
    auto adapter = std::make_unique<ProfilerGraphAdapter>();

    if (graph) {
        BenchmarkGraphFactory::WireProfilerHooks(graph, *adapter, benchGraph);
    }

    return adapter;
}

VkCommandBuffer VulkanIntegrationHelper::GetCurrentFrameCommandBuffer(
    RG::RenderGraph* graph,
    const std::string& dispatchNodeName)
{
    if (!graph) {
        return VK_NULL_HANDLE;
    }

    auto* nodeInstance = graph->GetInstanceByName(dispatchNodeName);
    if (!nodeInstance) {
        return VK_NULL_HANDLE;
    }

    auto* dispatchNode = dynamic_cast<RG::ComputeDispatchNode*>(nodeInstance);
    if (!dispatchNode) {
        return VK_NULL_HANDLE;
    }

    // ComputeDispatchNode manages command buffers internally
    // Return the current command buffer from the node
    // Note: The actual command buffer is recorded during Execute phase
    // This method is for cases where the command buffer is needed externally
    return VK_NULL_HANDLE; // ComputeDispatchNode doesn't expose command buffer directly
}

// ============================================================================
// ScopedProfilerIntegration implementation
// ============================================================================

ScopedProfilerIntegration::ScopedProfilerIntegration(RG::RenderGraph* graph)
{
    if (!graph) {
        return;
    }

    handles_ = VulkanIntegrationHelper::ExtractFromGraph(graph);

    if (handles_.IsValid()) {
        ProfilerSystem::Instance().Initialize(
            handles_.device,
            handles_.physicalDevice,
            handles_.framesInFlight
        );
        valid_ = ProfilerSystem::Instance().IsInitialized();
    }
}

ScopedProfilerIntegration::~ScopedProfilerIntegration()
{
    if (valid_) {
        ProfilerSystem::Instance().Shutdown();
    }
}

} // namespace Vixen::Profiler
