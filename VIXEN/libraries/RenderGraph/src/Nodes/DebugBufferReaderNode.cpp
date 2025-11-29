#include "Nodes/DebugBufferReaderNode.h"
#include "Debug/IDebugExportable.h"
#include "VulkanDevice.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include "NodeLogging.h"

namespace Vixen::RenderGraph {

// ============================================================================
// NodeType
// ============================================================================

std::unique_ptr<NodeInstance> DebugBufferReaderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DebugBufferReaderNode>(instanceName, const_cast<DebugBufferReaderNodeType*>(this));
}

// ============================================================================
// DebugBufferReaderNode
// ============================================================================

DebugBufferReaderNode::DebugBufferReaderNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<DebugBufferReaderNodeConfig>(instanceName, nodeType) {
}

void DebugBufferReaderNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::SetupImpl");
}

void DebugBufferReaderNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::CompileImpl");
    // Get device wrapper reference for later use
    vulkanDevice = ctx.In(DebugBufferReaderNodeConfig::VULKAN_DEVICE_IN);
    if (vulkanDevice == nullptr) {
        NODE_LOG_ERROR("No VulkanDevice provided");
        return;
    }
}

void DebugBufferReaderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::ExecuteImpl");

    if (vulkanDevice == nullptr) {
        NODE_LOG_ERROR("No VulkanDevice available");
        return;
    }

    VkDevice device = GetDevice()->device;
    VkBuffer debugBuffer = ctx.In(DebugBufferReaderNodeConfig::DEBUG_BUFFER);

    if (device == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("No VkDevice handle");
        return;
    }

    if (debugBuffer == VK_NULL_HANDLE) {
        NODE_LOG_WARNING("No debug buffer provided - skipping readback");
        return;
    }

    // Read buffer to host
    if (!ReadBufferToHost(device, debugBuffer)) {
        NODE_LOG_ERROR("Failed to read debug buffer");
        return;
    }

    // Export if auto-export enabled
    if (autoExport && !samples.empty()) {
        ExportSamples();
    }
}

void DebugBufferReaderNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::CleanupImpl");

    // Cleanup staging buffer if it exists
    // Note: Need device handle here - store during Compile
    samples.clear();
}

// ============================================================================
// Buffer readback
// ============================================================================

bool DebugBufferReaderNode::ReadBufferToHost(VkDevice device, VkBuffer srcBuffer) {
    // For now, assume the buffer is host-visible (created with HOST_VISIBLE | HOST_COHERENT)
    // In a production implementation, we'd use a staging buffer and copy commands

    // Get buffer memory
    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

    // TODO: We need to get the buffer's memory handle
    // This requires either:
    // 1. Passing the memory handle as an additional input
    // 2. Using VMA or a custom allocator that tracks buffer->memory mappings
    // 3. Creating the debug buffer with HOST_VISIBLE memory

    // For now, we'll implement a simplified version that assumes
    // the debug buffer info is passed via a descriptor

    NODE_LOG_WARNING("ReadBufferToHost: Full implementation requires memory handle");

    // Placeholder: Create some test data for demonstration
    samples.clear();

    // In actual implementation:
    // 1. Map buffer memory
    // 2. Read header (writeIndex, capacity)
    // 3. Read samples up to writeIndex
    // 4. Unmap memory

    return true;
}

void DebugBufferReaderNode::CreateStagingBuffer(VkDevice device, VkDeviceSize size) {
    if (stagingBuffer != VK_NULL_HANDLE && stagingBufferSize >= size) {
        return; // Reuse existing buffer
    }

    // Cleanup old buffer
    DestroyStagingBuffer(device);

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create staging buffer");
        return;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

    // Allocate memory (HOST_VISIBLE | HOST_COHERENT)
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    // TODO: Find proper memory type index for HOST_VISIBLE | HOST_COHERENT

    stagingBufferSize = size;
}

void DebugBufferReaderNode::DestroyStagingBuffer(VkDevice device) {
    if (stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
    }
    if (stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, stagingMemory, nullptr);
        stagingMemory = VK_NULL_HANDLE;
    }
    stagingBufferSize = 0;
}

// ============================================================================
// Export
// ============================================================================

void DebugBufferReaderNode::ExportSamples() {
    switch (exportFormat) {
        case DebugExportFormat::Console:
            ExportToConsole();
            break;
        case DebugExportFormat::CSV:
            ExportToCSV();
            break;
        case DebugExportFormat::JSON:
            ExportToJSON();
            break;
        case DebugExportFormat::All:
            ExportToConsole();
            ExportToCSV();
            ExportToJSON();
            break;
    }
}

void DebugBufferReaderNode::ExportToConsole() {
    NODE_LOG_INFO("=== Debug Ray Samples ===");
    NODE_LOG_INFO("Total samples: %zu", samples.size());

    // Apply filter if set
    std::vector<Debug::DebugRaySample> filtered;
    if (sampleFilter) {
        std::copy_if(samples.begin(), samples.end(), std::back_inserter(filtered), sampleFilter);
    } else {
        filtered = samples;
    }

    // Limit output
    size_t count = std::min(static_cast<size_t>(maxSamples), filtered.size());
    for (size_t i = 0; i < count; ++i) {
        printf("[%zu] %s\n", i, filtered[i].ToString().c_str());
    }

    if (filtered.size() > count) {
        printf("... and %zu more samples\n", filtered.size() - count);
    }

    PrintSummary();
}

void DebugBufferReaderNode::ExportToCSV() {
    std::string filename = outputPath + ".csv";

    // Apply filter
    std::vector<Debug::DebugRaySample> filtered;
    if (sampleFilter) {
        std::copy_if(samples.begin(), samples.end(), std::back_inserter(filtered), sampleFilter);
    } else {
        filtered = samples;
    }

    if (Debug::DebugExporter::ToCSVFile(filtered, filename)) {
        NODE_LOG_INFO("Exported %zu samples to %s", filtered.size(), filename.c_str());
    } else {
        NODE_LOG_ERROR("Failed to export to %s", filename.c_str());
    }
}

void DebugBufferReaderNode::ExportToJSON() {
    std::string filename = outputPath + ".json";

    // Apply filter
    std::vector<Debug::DebugRaySample> filtered;
    if (sampleFilter) {
        std::copy_if(samples.begin(), samples.end(), std::back_inserter(filtered), sampleFilter);
    } else {
        filtered = samples;
    }

    if (Debug::DebugExporter::ToJSONFile(filtered, filename)) {
        NODE_LOG_INFO("Exported %zu samples to %s", filtered.size(), filename.c_str());
    } else {
        NODE_LOG_ERROR("Failed to export to %s", filename.c_str());
    }
}

// ============================================================================
// Filtering
// ============================================================================

std::vector<Debug::DebugRaySample> DebugBufferReaderNode::GetSamplesByOctant(uint32_t octantMask) const {
    std::vector<Debug::DebugRaySample> result;
    std::copy_if(samples.begin(), samples.end(), std::back_inserter(result),
        [octantMask](const Debug::DebugRaySample& s) { return s.octantMask == octantMask; });
    return result;
}

std::vector<Debug::DebugRaySample> DebugBufferReaderNode::GetSamplesByExitCode(Debug::DebugExitCode code) const {
    std::vector<Debug::DebugRaySample> result;
    std::copy_if(samples.begin(), samples.end(), std::back_inserter(result),
        [code](const Debug::DebugRaySample& s) { return s.GetExitCode() == code; });
    return result;
}

std::vector<Debug::DebugRaySample> DebugBufferReaderNode::GetHitSamples() const {
    std::vector<Debug::DebugRaySample> result;
    std::copy_if(samples.begin(), samples.end(), std::back_inserter(result),
        [](const Debug::DebugRaySample& s) { return s.IsHit(); });
    return result;
}

std::vector<Debug::DebugRaySample> DebugBufferReaderNode::GetMissSamples() const {
    std::vector<Debug::DebugRaySample> result;
    std::copy_if(samples.begin(), samples.end(), std::back_inserter(result),
        [](const Debug::DebugRaySample& s) { return !s.IsHit(); });
    return result;
}

void DebugBufferReaderNode::PrintSummary() const {
    if (samples.empty()) {
        printf("No samples captured\n");
        return;
    }

    printf("\n=== Summary ===\n");
    printf("Total samples: %zu\n", samples.size());

    // Count by exit code
    std::array<size_t, 5> exitCounts{};
    for (const auto& s : samples) {
        if (s.exitCode < 5) exitCounts[s.exitCode]++;
    }

    printf("Exit codes:\n");
    printf("  NONE: %zu\n", exitCounts[0]);
    printf("  HIT: %zu\n", exitCounts[1]);
    printf("  NO_HIT: %zu\n", exitCounts[2]);
    printf("  STACK_EXIT: %zu\n", exitCounts[3]);
    printf("  INVALID_SPAN: %zu\n", exitCounts[4]);

    // Count by octant mask
    std::array<size_t, 8> octantCounts{};
    for (const auto& s : samples) {
        if (s.octantMask < 8) octantCounts[s.octantMask]++;
    }

    printf("Octant masks:\n");
    for (int i = 0; i < 8; ++i) {
        if (octantCounts[i] > 0) {
            printf("  %d: %zu (%.1f%%)\n", i, octantCounts[i],
                   100.0 * octantCounts[i] / samples.size());
        }
    }

    // Check for octant_mask correctness
    size_t correctMasks = 0;
    for (const auto& s : samples) {
        if (s.IsOctantMaskCorrect()) correctMasks++;
    }
    printf("Octant mask correctness: %zu / %zu (%.1f%%)\n",
           correctMasks, samples.size(), 100.0 * correctMasks / samples.size());

    // Iteration stats
    uint32_t minIter = UINT32_MAX, maxIter = 0;
    uint64_t totalIter = 0;
    for (const auto& s : samples) {
        minIter = std::min(minIter, s.iterationCount);
        maxIter = std::max(maxIter, s.iterationCount);
        totalIter += s.iterationCount;
    }
    printf("Iterations: min=%u, max=%u, avg=%.1f\n",
           minIter, maxIter, static_cast<double>(totalIter) / samples.size());
}

} // namespace Vixen::RenderGraph
