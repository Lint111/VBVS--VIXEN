#include "Nodes/DebugBufferReaderNode.h"
#include "Debug/IDebugExportable.h"
#include "Debug/IDebugCapture.h"
#include "VulkanDevice.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include "Core/NodeLogging.h"

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

    // Read parameters
    outputPath = GetParameterValue<std::string>(DebugBufferReaderNodeConfig::PARAM_OUTPUT_PATH, outputPath);
    maxTraces = GetParameterValue<uint32_t>(DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, maxTraces);
    autoExport = GetParameterValue<bool>(DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, autoExport);

    // Read export format (stored as int for parameter system compatibility)
    auto formatInt = GetParameterValue<int>(DebugBufferReaderNodeConfig::PARAM_EXPORT_FORMAT, static_cast<int>(exportFormat));
    exportFormat = static_cast<DebugExportFormat>(formatInt);

    NODE_LOG_INFO("  outputPath: %s", outputPath.c_str());
    NODE_LOG_INFO("  maxTraces: %u", maxTraces);
    NODE_LOG_INFO("  autoExport: %s", autoExport ? "true" : "false");
    NODE_LOG_INFO("  exportFormat: %d", static_cast<int>(exportFormat));
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

    // Wait for GPU to finish before reading
    VkFence inFlightFence = ctx.In(DebugBufferReaderNodeConfig::IN_FLIGHT_FENCE);
    if (inFlightFence != VK_NULL_HANDLE) {
        vkWaitForFences(vkDevice, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    }

    // Get the capture buffer from the interface
    Debug::DebugCaptureBuffer* captureBuffer = debugCapture->GetCaptureBuffer();
    if (captureBuffer == nullptr || !captureBuffer->IsValid()) {
        NODE_LOG_WARNING("Debug capture buffer is invalid - skipping readback");
        return;
    }

    // Read ray traces from the capture buffer
    uint32_t traceCount = captureBuffer->ReadRayTraces(vkDevice);
    if (traceCount == 0) {
        NODE_LOG_INFO("No ray traces captured this frame for '%s'", debugCapture->GetDebugName().c_str());
        return;
    }

    // Copy traces to our local storage
    rayTraces = captureBuffer->rayTraces;
    totalTracesInBuffer = traceCount;

    // Log ring buffer status
    if (captureBuffer->HasWrapped()) {
        NODE_LOG_INFO("Read %u ray traces from '%s' (ring buffer wrapped, %u total writes)",
                      traceCount, debugCapture->GetDebugName().c_str(), captureBuffer->GetTotalWrites());
    } else {
        NODE_LOG_INFO("Read %u ray traces from '%s' (binding %u)",
                      traceCount, debugCapture->GetDebugName().c_str(), debugCapture->GetBindingIndex());
    }

    // Export if auto-export enabled
    if (autoExport && !rayTraces.empty()) {
        ExportRayTraces();
    }
}

void DebugBufferReaderNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("DebugBufferReaderNode::CleanupImpl");
    rayTraces.clear();
}

// ============================================================================
// Export
// ============================================================================

void DebugBufferReaderNode::ExportRayTraces() {
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
    NODE_LOG_INFO("=== Ray Traces ===");
    NODE_LOG_INFO("Total traces: %zu", rayTraces.size());

    // Limit output
    size_t count = std::min(static_cast<size_t>(maxTraces), rayTraces.size());
    for (size_t i = 0; i < count; ++i) {
        printf("%s\n", rayTraces[i].ToString().c_str());
    }

    if (rayTraces.size() > count) {
        printf("... and %zu more traces\n", rayTraces.size() - count);
    }

    PrintSummary();
}

void DebugBufferReaderNode::ExportToCSV() {
    std::string filename = outputPath + ".csv";
    NODE_LOG_INFO("CSV export for ray traces not yet implemented: %s", filename.c_str());
    // TODO: Implement CSV export for ray traces
}

void DebugBufferReaderNode::ExportToJSON() {
    std::string filename = outputPath + ".json";
    NODE_LOG_INFO("JSON export for ray traces not yet implemented: %s", filename.c_str());
    // TODO: Implement JSON export for ray traces
}

// ============================================================================
// Filtering
// ============================================================================

std::vector<Debug::RayTrace> DebugBufferReaderNode::GetHitTraces() const {
    std::vector<Debug::RayTrace> result;
    std::copy_if(rayTraces.begin(), rayTraces.end(), std::back_inserter(result),
        [](const Debug::RayTrace& t) { return t.header.IsHit(); });
    return result;
}

std::vector<Debug::RayTrace> DebugBufferReaderNode::GetMissTraces() const {
    std::vector<Debug::RayTrace> result;
    std::copy_if(rayTraces.begin(), rayTraces.end(), std::back_inserter(result),
        [](const Debug::RayTrace& t) { return !t.header.IsHit(); });
    return result;
}

void DebugBufferReaderNode::PrintSummary() const {
    if (rayTraces.empty()) {
        printf("No traces captured\n");
        return;
    }

    printf("\n=== Summary ===\n");
    printf("Total traces: %zu\n", rayTraces.size());

    // Count hits and misses
    size_t hits = 0, misses = 0;
    uint32_t minSteps = UINT32_MAX, maxSteps = 0;
    uint64_t totalSteps = 0;
    size_t overflows = 0;

    for (const auto& trace : rayTraces) {
        if (trace.header.IsHit()) hits++;
        else misses++;

        if (trace.header.HasOverflow()) overflows++;

        uint32_t steps = trace.header.stepCount;
        minSteps = std::min(minSteps, steps);
        maxSteps = std::max(maxSteps, steps);
        totalSteps += steps;
    }

    printf("Hits: %zu (%.1f%%)\n", hits, 100.0 * hits / rayTraces.size());
    printf("Misses: %zu (%.1f%%)\n", misses, 100.0 * misses / rayTraces.size());
    printf("Steps: min=%u, max=%u, avg=%.1f\n",
           minSteps, maxSteps, static_cast<double>(totalSteps) / rayTraces.size());
    if (overflows > 0) {
        printf("Overflows (>64 steps): %zu\n", overflows);
    }

    // Count step types
    std::array<size_t, 8> stepTypeCounts{};
    for (const auto& trace : rayTraces) {
        for (const auto& step : trace.steps) {
            if (step.stepType < 8) {
                stepTypeCounts[step.stepType]++;
            }
        }
    }

    printf("Step types:\n");
    const char* stepNames[] = {"PUSH", "ADVANCE", "POP", "BRICK_ENTER", "BRICK_DDA", "BRICK_EXIT", "HIT", "MISS"};
    for (size_t i = 0; i < 8; ++i) {
        if (stepTypeCounts[i] > 0) {
            printf("  %s: %zu\n", stepNames[i], stepTypeCounts[i]);
        }
    }
}

} // namespace Vixen::RenderGraph
