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

    auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
    // Enable logging for VoxelGridNode to see octree generation.
    if (auto* voxelLogger = voxelGrid->GetLogger()) {
        voxelLogger->SetEnabled(true);
        voxelLogger->SetTerminalOutput(true);
    }

    // Enable logging for descriptor gatherer to debug bindings.
    auto* descGatherer = static_cast<DescriptorResourceGathererNode*>(renderGraph->GetInstance(descriptorGatherer));
    if (auto* gathererLogger = descGatherer->GetLogger()) {
        gathererLogger->SetEnabled(true);  // TEMP DEBUG: KI-009 follow-up
        gathererLogger->SetTerminalOutput(true);
    }

    // Enable logging for compute dispatch to see execution.
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    if (auto* dispatchLogger = dispatch->GetLogger()) {
        dispatchLogger->SetEnabled(true);
        dispatchLogger->SetTerminalOutput(true);
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
        debugLogger->SetEnabled(true);
        debugLogger->SetTerminalOutput(true);
    }
}
