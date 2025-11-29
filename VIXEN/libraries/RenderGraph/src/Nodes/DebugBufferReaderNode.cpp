#include "Nodes/DebugBufferReaderNode.h"
#include "Debug/IDebugExportable.h"
#include "Debug/IDebugCapture.h"
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
    VulkanDevice* vulkanDevice = ctx.In(DebugBufferReaderNodeConfig::VULKAN_DEVICE_IN);
    if (vulkanDevice == nullptr) {
        NODE_LOG_ERROR("No VulkanDevice provided");
        return;
    }
    SetDevice(vulkanDevice);
}

void DebugBufferReaderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::ExecuteImpl");

    if (GetDevice() == nullptr) {
        NODE_LOG_ERROR("No VulkanDevice available");
        return;
    }

    VkDevice vkDevice = GetDevice()->device;

    // Get the debug capture interface
    Debug::IDebugCapture* debugCapture = ctx.In(DebugBufferReaderNodeConfig::DEBUG_CAPTURE);

    if (vkDevice == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("No VkDevice handle");
        return;
    }

    if (debugCapture == nullptr) {
        NODE_LOG_WARNING("No debug capture interface provided - skipping readback");
        return;
    }

    if (!debugCapture->IsCaptureEnabled()) {
        NODE_LOG_INFO("Debug capture is disabled for '%s'", debugCapture->GetDebugName().c_str());
        return;
    }

    // Get the capture buffer from the interface
    Debug::DebugCaptureBuffer* captureBuffer = debugCapture->GetCaptureBuffer();
    if (captureBuffer == nullptr || !captureBuffer->IsValid()) {
        NODE_LOG_WARNING("Debug capture buffer is invalid - skipping readback");
        return;
    }

    // Read samples directly from the capture buffer
    uint32_t sampleCount = captureBuffer->ReadSamples(vkDevice);
    if (sampleCount == 0) {
        NODE_LOG_INFO("No debug samples captured this frame for '%s'", debugCapture->GetDebugName().c_str());
        return;
    }

    // Copy samples to our local storage
    samples = captureBuffer->samples;
    totalSamplesInBuffer = sampleCount;

    NODE_LOG_INFO("Read %u debug samples from '%s' (binding %u)",
                  sampleCount, debugCapture->GetDebugName().c_str(), debugCapture->GetBindingIndex());

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
