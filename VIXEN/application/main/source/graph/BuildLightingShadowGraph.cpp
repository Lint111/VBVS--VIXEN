#include "VulkanGraphApplication.h"

#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/StorageBufferNodeConfig.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/ProbeGridConfigNode.h"
#include "Nodes/ReservoirConfigNode.h"
#include "Nodes/StorageBufferNode.h"

void VulkanGraphApplication::ConfigureLightingShadowDispatches(
    NodeHandle probeGatherNode,
    NodeHandle shadowRayTraceNode,
    NodeHandle probeApplyNode,
    NodeHandle shadowRayRequestBuffer,
    NodeHandle shadowRayResultBuffer,
    NodeHandle probeRayPayloadBuffer,
    uint32_t shadowRaySlotCount,
    uint32_t shadowRayRequestStrideBytes,
    uint32_t shadowRayResultStrideBytes,
    uint32_t probeRayPayloadStrideBytes) {
    using namespace Vixen::RenderGraph;

    constexpr uint32_t kProbeGridDefaultCountX = 8u;
    constexpr uint32_t kProbeGridDefaultCountY = 8u;
    constexpr uint32_t kProbeGridDefaultCountZ = 8u;
    constexpr uint32_t kProbeUpdateDefaultProbeCount =
        kProbeGridDefaultCountX * kProbeGridDefaultCountY * kProbeGridDefaultCountZ;
    const uint32_t amortizationFactor = ResolveDdgiAmortizationFactor();
    const uint32_t probeUpdateDispatchX =
        (kProbeUpdateDefaultProbeCount + amortizationFactor - 1u) / amortizationFactor;
    const uint32_t shadowRayTraceDispatchX = shadowRaySlotCount / 64u;

    mainLogger->Info("[BuildRenderGraph] ProbeGather/Apply sparse dispatch (Inc6 M1 shape): probeCount=" +
                     std::to_string(kProbeUpdateDefaultProbeCount) + " amortizationFactor=" +
                     std::to_string(amortizationFactor) + " -> dispatchX=" +
                     std::to_string(probeUpdateDispatchX) + "; ShadowRayTrace waveDispatchX=" +
                     std::to_string(shadowRayTraceDispatchX) + " (slots=" +
                     std::to_string(shadowRaySlotCount) + ")");

    auto* probeGather = static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeGatherNode));
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, probeUpdateDispatchX);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    auto* shadowRayTrace = static_cast<ComputeStageNode*>(renderGraph->GetInstance(shadowRayTraceNode));
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, shadowRayTraceDispatchX);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    auto* probeApply = static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeApplyNode));
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, probeUpdateDispatchX);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    static_cast<StorageBufferNode*>(renderGraph->GetInstance(shadowRayRequestBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                       shadowRaySlotCount * shadowRayRequestStrideBytes);
    static_cast<StorageBufferNode*>(renderGraph->GetInstance(shadowRayResultBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                       shadowRaySlotCount * shadowRayResultStrideBytes);
    static_cast<StorageBufferNode*>(renderGraph->GetInstance(probeRayPayloadBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                       shadowRaySlotCount * probeRayPayloadStrideBytes);
}

void VulkanGraphApplication::FinalizeLightingShadowDispatches(
    NodeHandle directLightingNode,
    NodeHandle probeGatherNode,
    NodeHandle shadowRayTraceNode,
    NodeHandle probeApplyNode) {
    using namespace Vixen::RenderGraph;

    // These gates intentionally run at the end of BuildRenderGraph. Optional demo setup can
    // force the probe-grid environment value after the node parameters above are configured;
    // resolve the same accessors used by the per-frame config nodes only after those overrides.
    static_cast<ComputeStageNode*>(renderGraph->GetInstance(directLightingNode))
        ->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED,
                       ResolveReservoirEnabled());
    const bool probeGridEnabled = ResolveProbeGridEnabled();
    static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeGatherNode))
        ->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, probeGridEnabled);
    static_cast<ComputeStageNode*>(renderGraph->GetInstance(shadowRayTraceNode))
        ->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, probeGridEnabled);
    static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeApplyNode))
        ->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, probeGridEnabled);
}
