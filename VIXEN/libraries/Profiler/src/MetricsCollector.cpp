#include "Profiler/MetricsCollector.h"
#include <cstring>
#include <vector>

namespace Vixen::Profiler {

struct MetricsCollector::PerFrameData {
    uint64_t startTimestamp = 0;
    uint64_t dispatchStartTimestamp = 0;
    uint64_t dispatchEndTimestamp = 0;
    uint32_t dispatchWidth = 0;
    uint32_t dispatchHeight = 0;
    bool hasData = false;
};

MetricsCollector::MetricsCollector() = default;

MetricsCollector::~MetricsCollector() {
    Shutdown();
}

void MetricsCollector::Initialize(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    framesInFlight_ = framesInFlight;

    // Get timestamp period
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    timestampPeriod_ = props.limits.timestampPeriod;

    // Check for VK_EXT_memory_budget support
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
            memoryBudgetSupported_ = true;
            break;
        }
    }

    // Create per-frame data
    frameData_ = std::make_unique<PerFrameData[]>(framesInFlight);

    // Create query pool (3 timestamps per frame: start, dispatch start, dispatch end)
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = framesInFlight * 3;

    if (vkCreateQueryPool(device, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS) {
        queryPool_ = VK_NULL_HANDLE;
    }

    // Initialize rolling stats
    rollingStats_["frame_time"] = RollingStats(300);
    rollingStats_["gpu_time"] = RollingStats(300);
    rollingStats_["mrays"] = RollingStats(300);
    rollingStats_["fps"] = RollingStats(300);
    rollingStats_["vram_usage"] = RollingStats(300);
    rollingStats_["vram_budget"] = RollingStats(300);

    profilingStartTime_ = std::chrono::high_resolution_clock::now();
}

void MetricsCollector::Shutdown() {
    if (queryPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, queryPool_, nullptr);
        queryPool_ = VK_NULL_HANDLE;
    }
    frameData_.reset();
}

void MetricsCollector::RegisterExtractor(const std::string& name, NodeMetricsExtractor extractor) {
    extractors_[name] = std::move(extractor);
}

void MetricsCollector::UnregisterExtractor(const std::string& name) {
    extractors_.erase(name);
}

void MetricsCollector::OnFrameBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    frameStartTime_ = std::chrono::high_resolution_clock::now();

    if (queryPool_ == VK_NULL_HANDLE) return;

    uint32_t baseQuery = frameIndex * 3;

    // Reset queries for this frame
    vkCmdResetQueryPool(cmdBuffer, queryPool_, baseQuery, 3);

    // Write start timestamp
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, baseQuery);

    frameData_[frameIndex].hasData = false;
}

void MetricsCollector::OnDispatchBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (queryPool_ == VK_NULL_HANDLE) return;

    uint32_t baseQuery = frameIndex * 3;
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, baseQuery + 1);
}

void MetricsCollector::OnDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                     uint32_t dispatchWidth, uint32_t dispatchHeight) {
    if (queryPool_ == VK_NULL_HANDLE) return;

    uint32_t baseQuery = frameIndex * 3;
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool_, baseQuery + 2);

    frameData_[frameIndex].dispatchWidth = dispatchWidth;
    frameData_[frameIndex].dispatchHeight = dispatchHeight;
    frameData_[frameIndex].hasData = true;
}

void MetricsCollector::OnFrameEnd(uint32_t frameIndex) {
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    auto frameDuration = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime_);

    // Collect GPU results from previous frame
    CollectGPUResults(frameIndex);

    // Collect VRAM usage (VK_EXT_memory_budget)
    CollectVRAMUsage();

    // Calculate CPU frame time
    lastFrameMetrics_.frameNumber = totalFramesCollected_;
    lastFrameMetrics_.frameTimeMs = frameDuration.count();
    lastFrameMetrics_.fps = 1000.0f / lastFrameMetrics_.frameTimeMs;

    // Calculate timestamp since profiling started
    auto sinceStart = std::chrono::duration<double, std::milli>(frameEndTime - profilingStartTime_);
    lastFrameMetrics_.timestampMs = sinceStart.count();

    // Update rolling stats (skip warmup)
    if (!IsWarmingUp()) {
        UpdateRollingStats(lastFrameMetrics_);
    }

    totalFramesCollected_++;
}

void MetricsCollector::OnPreCleanup() {
    // Call all registered extractors
    for (const auto& [name, extractor] : extractors_) {
        extractor(lastFrameMetrics_);
    }
}

const RollingStats* MetricsCollector::GetRollingStats(const std::string& metricName) const {
    auto it = rollingStats_.find(metricName);
    if (it != rollingStats_.end()) {
        return &it->second;
    }
    return nullptr;
}

void MetricsCollector::Reset() {
    for (auto& [name, stats] : rollingStats_) {
        stats.Reset();
    }
    totalFramesCollected_ = 0;
    lastFrameMetrics_ = FrameMetrics{};
    profilingStartTime_ = std::chrono::high_resolution_clock::now();
}

void MetricsCollector::CollectGPUResults(uint32_t frameIndex) {
    if (queryPool_ == VK_NULL_HANDLE || !frameData_[frameIndex].hasData) {
        return;
    }

    uint32_t baseQuery = frameIndex * 3;
    uint64_t timestamps[3] = {0, 0, 0};

    VkResult result = vkGetQueryPoolResults(
        device_, queryPool_, baseQuery, 3,
        sizeof(timestamps), timestamps, sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    if (result == VK_SUCCESS) {
        // Calculate GPU dispatch time
        uint64_t dispatchTicks = timestamps[2] - timestamps[1];
        float dispatchNs = static_cast<float>(dispatchTicks) * timestampPeriod_;
        lastFrameMetrics_.gpuTimeMs = dispatchNs / 1e6f;

        // Calculate MRays/s
        uint32_t totalRays = frameData_[frameIndex].dispatchWidth * frameData_[frameIndex].dispatchHeight;
        if (lastFrameMetrics_.gpuTimeMs > 0.0f) {
            lastFrameMetrics_.mRaysPerSec = (totalRays / 1e6f) / (lastFrameMetrics_.gpuTimeMs / 1000.0f);
        }

        lastFrameMetrics_.screenWidth = frameData_[frameIndex].dispatchWidth;
        lastFrameMetrics_.screenHeight = frameData_[frameIndex].dispatchHeight;
    }
}

void MetricsCollector::CollectVRAMUsage() {
    if (!memoryBudgetSupported_ || physicalDevice_ == VK_NULL_HANDLE) {
        lastFrameMetrics_.vramUsageMB = 0;
        lastFrameMetrics_.vramBudgetMB = 0;
        return;
    }

    // Query memory budget using VK_EXT_memory_budget
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
    budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 memProps2{};
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memProps2.pNext = &budgetProps;

    vkGetPhysicalDeviceMemoryProperties2(physicalDevice_, &memProps2);

    // Sum up device-local heap usage and budget
    uint64_t totalUsage = 0;
    uint64_t totalBudget = 0;

    for (uint32_t i = 0; i < memProps2.memoryProperties.memoryHeapCount; ++i) {
        if (memProps2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalUsage += budgetProps.heapUsage[i];
            totalBudget += budgetProps.heapBudget[i];
        }
    }

    lastFrameMetrics_.vramUsageMB = totalUsage / (1024 * 1024);
    lastFrameMetrics_.vramBudgetMB = totalBudget / (1024 * 1024);
}

void MetricsCollector::UpdateRollingStats(const FrameMetrics& metrics) {
    rollingStats_["frame_time"].AddSample(metrics.frameTimeMs);
    rollingStats_["gpu_time"].AddSample(metrics.gpuTimeMs);
    rollingStats_["mrays"].AddSample(metrics.mRaysPerSec);
    rollingStats_["fps"].AddSample(metrics.fps);
    rollingStats_["vram_usage"].AddSample(static_cast<float>(metrics.vramUsageMB));
    rollingStats_["vram_budget"].AddSample(static_cast<float>(metrics.vramBudgetMB));
}

} // namespace Vixen::Profiler
