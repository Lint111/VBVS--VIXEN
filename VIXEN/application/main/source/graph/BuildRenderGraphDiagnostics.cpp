#include "VulkanGraphApplication.h"

#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Data/Nodes/DebugBufferReaderNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/DebugBufferReaderNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Nodes/VoxelGridNode.h"

void VulkanGraphApplication::ConfigureRenderGraphDiagnostics(
    NodeHandle voxelGridNode,
    NodeHandle descriptorGatherer,
    NodeHandle computeDispatch,
    NodeHandle pushConstantGatherer,
    NodeHandle debugCaptureNode,
    bool debugCaptureEnabled) {
    using namespace Vixen::RenderGraph;

    // R851.7 (design D10): the node loggers' terminal output is a debugging aid, and a capture's
    // length is a bug in itself; they are enabled only for a debug-capture session
    // (VIXEN_DEBUG_CAPTURE=1), never unconditionally.
    if (debugCaptureEnabled) {
        auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
        if (auto* voxelLogger = voxelGrid->GetLogger()) {
            voxelLogger->SetEnabled(true);
            voxelLogger->SetTerminalOutput(true);
        }

        auto* descGatherer = static_cast<DescriptorResourceGathererNode*>(renderGraph->GetInstance(descriptorGatherer));
        if (auto* gathererLogger = descGatherer->GetLogger()) {
            gathererLogger->SetEnabled(true);
            gathererLogger->SetTerminalOutput(true);
        }

        auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
        if (auto* dispatchLogger = dispatch->GetLogger()) {
            dispatchLogger->SetEnabled(true);
            dispatchLogger->SetTerminalOutput(true);
        }
    }

    // Enable logging for push constant gatherer to see packing.
    auto* pcGatherer = static_cast<PushConstantGathererNode*>(renderGraph->GetInstance(pushConstantGatherer));
    if (auto* pcLogger = pcGatherer->GetLogger()) {
        pcLogger->SetEnabled(false);
        pcLogger->SetTerminalOutput(false);
    }

    // Task 0.2 (Baked-Content Perf Audit D2): default OFF. VIXEN_DEBUG_CAPTURE=1
    // re-enables the blocking debug export path for an actual debugging session.
    auto* debugCapture = static_cast<DebugBufferReaderNode*>(renderGraph->GetInstance(debugCaptureNode));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, 1000u);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, debugCaptureEnabled);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_EXPORT_FORMAT,
                               static_cast<int>(DebugExportFormat::JSON));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_OUTPUT_PATH,
                               std::string("binaries/compute_debug_output"));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_FRAMES_PER_EXPORT, 10u);
    if (auto* debugLogger = debugCapture->GetLogger()) {
        debugLogger->SetEnabled(debugCaptureEnabled);
        debugLogger->SetTerminalOutput(debugCaptureEnabled);
    }
}
