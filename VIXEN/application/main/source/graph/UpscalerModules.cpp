#include "graph/UpscalerModules.h"
#include "graph/Fsr3Upscaler.h"

#include "Connection/ConnectionModifier.h"
#include "Core/NodeRegistration.h"
#include "Data/ReconstructionDepth.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SdiStageSynthesis.h"
#include "Nodes/ShaderLibraryNode.h"
#include "merged/FsrInputAA-SDI.g.h"
#include "merged/Fsr1Easu-SDI.g.h"
#include "merged/Fsr1Rcas-SDI.g.h"

#include <array>
#include <algorithm>
#include <stdexcept>
#include <span>
#include <vector>

namespace Vixen::RenderGraph {
namespace {

constexpr UpscalerDepthConsumerConvention kFsr3DepthConventions[]{
    {"fsr3", ReconstructionDepthConvention::ForwardZeroToOne},
};

class LinearUpscalerModuleType final : public SubGraphType<LinearUpscalerModuleType> {
public:
    static constexpr std::string_view TYPE_NAME = kLinearUpscalerModuleTypeName;
    static constexpr std::string_view CAPABILITY_NAME = "";
    using PortConfig = UpscalerModulePorts;
    using Params = UpscalerModuleBuildContext;

    void Build(GraphScope& scope, const Params&) {
        scope.BindOutput(UpscalerModulePorts::UPSCALED_TARGET,
                         UpscalerModulePorts::SOURCE_TARGET);
        scope.BindOutput(UpscalerModulePorts::RENDER_COMPLETE,
                         UpscalerModulePorts::ORDERING_WAIT);
    }
};

class Fsr1UpscalerModuleType final : public SubGraphType<Fsr1UpscalerModuleType> {
public:
    static constexpr std::string_view TYPE_NAME = kFsr1UpscalerModuleTypeName;
    static constexpr std::string_view CAPABILITY_NAME = "Upscaler:Fsr1";
    using PortConfig = UpscalerModulePorts;
    using Params = UpscalerModuleBuildContext;

    void Build(GraphScope& scope, const Params& params) {
        if (!params.batch || !params.makeShaderFamily || !params.registerShaderFamily ||
            !params.sourceTarget.IsValid() || !params.common.device.IsValid() ||
            !params.common.presentationTarget.IsValid()) {
            throw std::runtime_error("FSR 1 module is missing graph build inputs");
        }
        ConnectionBatch& batch = *params.batch;
        const NodeHandle inputAaTarget = scope.AddNode<RenderTargetNodeType>("input_aa_target");
        const NodeHandle easuTarget = scope.AddNode<RenderTargetNodeType>("easu_target");
        const NodeHandle outputTarget = scope.AddNode<RenderTargetNodeType>("rcas_target");
        const NodeHandle inputAaShader = scope.AddNode<ShaderLibraryNodeType>("input_aa_shader_lib");
        const NodeHandle easuShader = scope.AddNode<ShaderLibraryNodeType>("easu_shader_lib");
        const NodeHandle rcasShader = scope.AddNode<ShaderLibraryNodeType>("rcas_shader_lib");
        const NodeHandle inputAa = scope.AddNode<ComputeStageNodeType>("input_aa");
        const NodeHandle easu = scope.AddNode<ComputeStageNodeType>("easu");
        const NodeHandle rcas = scope.AddNode<ComputeStageNodeType>("rcas");
        const NodeHandle inputRead = scope.AddNode<ImageSyncGathererNodeType>("input_read_gatherer");
        const NodeHandle easuRead = scope.AddNode<ImageSyncGathererNodeType>("easu_read_gatherer");
        const NodeHandle rcasRead = scope.AddNode<ImageSyncGathererNodeType>("rcas_read_gatherer");

        for (NodeHandle gatherer : {inputRead, easuRead, rcasRead}) {
            scope.GetInstance<ImageSyncGathererNode>(gatherer)->PreRegisterImageSlots(1);
        }
        scope.BindInputIndex(UpscalerModulePorts::SOURCE_TARGET, inputRead, 0);
        scope.BindInput(UpscalerModulePorts::PRESENTATION_TARGET, inputAaTarget,
                        RenderTargetNodeConfig::EXTENT_SOURCE);
        scope.BindInput(UpscalerModulePorts::PRESENTATION_TARGET, easuTarget,
                        RenderTargetNodeConfig::EXTENT_SOURCE);
        scope.BindInput(UpscalerModulePorts::PRESENTATION_TARGET, outputTarget,
                        RenderTargetNodeConfig::EXTENT_SOURCE);
        scope.BindInput(UpscalerModulePorts::ORDERING_WAIT, inputAa,
                        ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        scope.BindInput(UpscalerModulePorts::DEVICE, inputAaTarget,
                        RenderTargetNodeConfig::VULKAN_DEVICE_IN);
        scope.BindInput(UpscalerModulePorts::DEVICE, easuTarget,
                        RenderTargetNodeConfig::VULKAN_DEVICE_IN);
        scope.BindInput(UpscalerModulePorts::DEVICE, outputTarget,
                        RenderTargetNodeConfig::VULKAN_DEVICE_IN);
        scope.BindOutput(UpscalerModulePorts::UPSCALED_TARGET, outputTarget,
                         RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(UpscalerModulePorts::RENDER_COMPLETE, rcas,
                         ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE);

        auto* inputAaTargetNode = scope.GetInstance<RenderTargetNode>(inputAaTarget);
        auto* easuTargetNode = scope.GetInstance<RenderTargetNode>(easuTarget);
        auto* outputTargetNode = scope.GetInstance<RenderTargetNode>(outputTarget);
        inputAaTargetNode->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, params.renderScale);
        easuTargetNode->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, 1.0f);
        outputTargetNode->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, 1.0f);
        for (auto* target : {inputAaTargetNode, easuTargetNode, outputTargetNode}) {
            target->SetParameter(RenderTargetNodeConfig::PARAM_IMAGE_COUNT,
                                 FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);
            target->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT,
                                 static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM));
        }

        const auto inputAaFamily = params.makeShaderFamily("FsrInputAA.comp", "FsrInputAA");
        const auto easuFamily = params.makeShaderFamily("Fsr1Easu.comp", "Fsr1Easu");
        const auto rcasFamily = params.makeShaderFamily("Fsr1Rcas.comp", "Fsr1Rcas");
        params.registerShaderFamily(inputAaShader, inputAaFamily);
        params.registerShaderFamily(easuShader, easuFamily);
        params.registerShaderFamily(rcasShader, rcasFamily);

        const auto makeProviders = [&](NodeHandle input, NodeHandle output) {
            SdiProviderRegistry providers = params.sceneProviders;
            providers.Provide("InputTexture", input,
                              RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
            providers.Provide("OutputTexture", output,
                              RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
            return providers;
        };

        const auto inputAaSynth = SynthesizeComputeStage<ShaderInterface::FsrInputAA::Metadata,
                                                          ShaderInterface::FsrInputAA::MEMBERS>(
            &scope, batch, "input_aa", inputAaShader, inputAa, params.common,
            makeProviders(params.sourceTarget, inputAaTarget), {});
        (void)inputAaSynth;
        const auto easuSynth = SynthesizeComputeStage<ShaderInterface::Fsr1Easu::Metadata,
                                                       ShaderInterface::Fsr1Easu::MEMBERS>(
            &scope, batch, "easu", easuShader, easu, params.common,
            makeProviders(inputAaTarget, easuTarget), {});
        (void)easuSynth;
        const auto rcasSynth = SynthesizeComputeStage<ShaderInterface::Fsr1Rcas::Metadata,
                                                       ShaderInterface::Fsr1Rcas::MEMBERS>(
            &scope, batch, "rcas", rcasShader, rcas, params.common,
            makeProviders(easuTarget, outputTarget), {});
        (void)rcasSynth;

        const auto setDispatch = [&](NodeHandle handle, uint32_t dispatchWidth,
                                     uint32_t dispatchHeight) {
            auto* stage = scope.GetInstance<ComputeStageNode>(handle);
            stage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            stage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X,
                                (dispatchWidth + 7u) / 8u);
            stage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y,
                                (dispatchHeight + 7u) / 8u);
            stage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        };
        setDispatch(inputAa, params.renderExtent.width, params.renderExtent.height);
        setDispatch(easu, params.presentationExtent.width, params.presentationExtent.height);
        setDispatch(rcas, params.presentationExtent.width, params.presentationExtent.height);

        batch.Connect(inputRead, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      inputAa, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(inputAaTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      inputAa, ComputeStageNodeConfig::IMAGE_WRITE,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(inputAaTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      easuRead, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(easuRead, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      easu, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(easuTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      easu, ComputeStageNodeConfig::IMAGE_WRITE,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(easuTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      rcasRead, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(rcasRead, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      rcas, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(outputTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      rcas, ComputeStageNodeConfig::IMAGE_WRITE,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(inputAa, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      easu, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE)
             .Connect(easu, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      rcas, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);

        if (params.recordExecution) {
            params.recordExecution("fsr_input_aa", inputAa);
            params.recordExecution("fsr1_easu", easu);
            params.recordExecution("fsr1_rcas", rcas);
        }
    }
};

class Fsr3UpscalerModuleType final : public SubGraphType<Fsr3UpscalerModuleType> {
public:
    static constexpr std::string_view TYPE_NAME = kFsr3UpscalerModuleTypeName;
    static constexpr std::string_view CAPABILITY_NAME = "Upscaler:Fsr3.1.4";
    using PortConfig = UpscalerModulePorts;
    using Params = UpscalerModuleBuildContext;

    void Build(GraphScope& scope, const Params& params) {
        if (!params.batch || !params.makeShaderFamily || !params.registerShaderFamily ||
            !params.sourceTarget.IsValid() || !params.common.device.IsValid() ||
            !params.common.presentationTarget.IsValid()) {
            throw std::runtime_error("FSR 3.1.4 module is missing graph build inputs");
        }
        ConnectionBatch& batch = *params.batch;
        const NodeHandle inputRead = scope.AddNode<ImageSyncGathererNodeType>("ffx_input_read");
        const NodeHandle outputTarget = scope.AddNode<RenderTargetNodeType>("ffx_output");
        const NodeHandle shader = scope.AddNode<ShaderLibraryNodeType>("ffx_dispatch_shader");
        const NodeHandle stage = scope.AddNode<ComputeStageNodeType>("fsr3_temporal");
        scope.GetInstance<ImageSyncGathererNode>(inputRead)->PreRegisterImageSlots(4);
        scope.BindInputIndex(UpscalerModulePorts::SOURCE_TARGET, inputRead, 0);
        scope.BindInputIndex(UpscalerModulePorts::DEPTH_TARGET, inputRead, 1);
        scope.BindInputIndex(UpscalerModulePorts::MOTION_TARGET, inputRead, 2);
        scope.BindInputIndex(UpscalerModulePorts::EXPOSURE_TARGET, inputRead, 3);
        scope.BindInput(UpscalerModulePorts::PRESENTATION_TARGET, outputTarget,
                        RenderTargetNodeConfig::EXTENT_SOURCE);
        scope.BindInput(UpscalerModulePorts::DEVICE, outputTarget,
                        RenderTargetNodeConfig::VULKAN_DEVICE_IN);
        scope.BindInput(UpscalerModulePorts::ORDERING_WAIT, stage,
                        ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        scope.BindInput(UpscalerModulePorts::JITTER, stage,
                        ComputeStageNodeConfig::EXTERNAL_UV_JITTER);
        scope.BindInput(UpscalerModulePorts::HISTORY_RESET, stage,
                        ComputeStageNodeConfig::EXTERNAL_HISTORY_RESET);
        scope.BindInput(UpscalerModulePorts::CAMERA_DATA, stage,
                        ComputeStageNodeConfig::EXTERNAL_CAMERA_DATA);
        scope.BindOutput(UpscalerModulePorts::UPSCALED_TARGET, outputTarget,
                         RenderTargetNodeConfig::RENDER_TARGET);
        scope.BindOutput(UpscalerModulePorts::RENDER_COMPLETE, stage,
                         ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE);

        auto* output = scope.GetInstance<RenderTargetNode>(outputTarget);
        output->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, 1.0f);
        output->SetParameter(RenderTargetNodeConfig::PARAM_IMAGE_COUNT,
                             FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);
        output->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT,
                             static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM));

        const auto shaderFamily = params.makeShaderFamily("Fsr1Easu.comp", "Fsr1Easu");
        params.registerShaderFamily(shader, shaderFamily);
        SdiProviderRegistry providers = params.sceneProviders;
        providers.Provide("InputTexture", params.sourceTarget,
                          RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        providers.Provide("OutputTexture", outputTarget,
                          RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        const auto synthesized = SynthesizeComputeStage<ShaderInterface::Fsr1Easu::Metadata,
                                                         ShaderInterface::Fsr1Easu::MEMBERS>(
            &scope, batch, "fsr3_external_dispatch", shader, stage, params.common,
            providers, {});
        (void)synthesized;

        auto* compute = scope.GetInstance<ComputeStageNode>(stage);
        compute->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X,
                              (params.presentationExtent.width + 7u) / 8u);
        compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y,
                              (params.presentationExtent.height + 7u) / 8u);
        compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        const auto hooks = CreateFsr3UpscalerHooks(
            params.renderExtent, params.presentationExtent,
            kFsr3DepthConventions[0].convention,
            params.cameraNearPlane, params.cameraFarPlane);
        compute->SetExternalCommandRecorder(hooks.record, hooks.initialize, hooks.cleanup);

        batch.Connect(inputRead, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      stage, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(outputTarget, RenderTargetNodeConfig::RENDER_TARGET,
                      stage, ComputeStageNodeConfig::IMAGE_WRITE,
                      SlotRoleModifier(SlotRole::Execute));

        if (params.recordExecution) {
            params.recordExecution("fsr3_temporal", stage);
        }
    }
};

void ResetNoUpscalerHistory(void*, UpscalerHistoryResetCause) noexcept {}

constexpr UpscalerImageFormat kRgba8Format[] = {UpscalerImageFormat::Rgba8Unorm};
constexpr UpscalerFrameInput kColorInput[] = {UpscalerFrameInput::Color};
constexpr UpscalerFrameInput kFsr3Inputs[]{
    UpscalerFrameInput::Color,
    UpscalerFrameInput::Depth,
    UpscalerFrameInput::MotionVectors,
    UpscalerFrameInput::Jitter,
    UpscalerFrameInput::Exposure,
};
constexpr UpscalerExternalResourceDependency kNativeDependencies[] = {
    {UpscalerExternalResource::SourceColor, true},
    {UpscalerExternalResource::PresentationTarget, true},
    {UpscalerExternalResource::Device, true},
    {UpscalerExternalResource::ExecutionOrder, true}
};
constexpr UpscalerExternalResourceDependency kLinearDependencies[] = {
    {UpscalerExternalResource::SourceColor, true},
    {UpscalerExternalResource::ExecutionOrder, true}
};
constexpr UpscalerExternalResourceDependency kFsr1Dependencies[] = {
    {UpscalerExternalResource::SourceColor, true},
    {UpscalerExternalResource::PresentationTarget, true},
    {UpscalerExternalResource::Device, true},
    {UpscalerExternalResource::ExecutionOrder, true}
};
constexpr UpscalerExternalResourceDependency kFsr3Dependencies[] = {
    {UpscalerExternalResource::SourceColor, true},
    {UpscalerExternalResource::PresentationTarget, true},
    {UpscalerExternalResource::Device, true},
    {UpscalerExternalResource::ExecutionOrder, true}
};

const UpscalerContract kNativeContract{
    .inputFormats = kRgba8Format,
    .outputFormats = kRgba8Format,
    .requiredFrameInputs = kColorInput,
    .contextLifetime = UpscalerContextLifetime::None,
    .retainsHistory = false,
    .resetHistory = &ResetNoUpscalerHistory,
    .executionRecording = UpscalerExecutionRecording::RenderGraph,
    .externalResourceDependencies = kNativeDependencies
};
const UpscalerContract kLinearContract{
    .inputFormats = kRgba8Format,
    .outputFormats = kRgba8Format,
    .requiredFrameInputs = kColorInput,
    .contextLifetime = UpscalerContextLifetime::None,
    .retainsHistory = false,
    .resetHistory = &ResetNoUpscalerHistory,
    .executionRecording = UpscalerExecutionRecording::RenderGraph,
    .externalResourceDependencies = kLinearDependencies
};
const UpscalerContract kFsr1Contract{
    .inputFormats = kRgba8Format,
    .outputFormats = kRgba8Format,
    .requiredFrameInputs = kColorInput,
    .contextLifetime = UpscalerContextLifetime::RenderGraph,
    .retainsHistory = false,
    .resetHistory = &ResetNoUpscalerHistory,
    .executionRecording = UpscalerExecutionRecording::RenderGraph,
    .externalResourceDependencies = kFsr1Dependencies
};

const UpscalerContract kFsr3Contract{
    .inputFormats = kRgba8Format,
    .outputFormats = kRgba8Format,
    .requiredFrameInputs = kFsr3Inputs,
    .depthConventions = kFsr3DepthConventions,
    .contextLifetime = UpscalerContextLifetime::RenderGraph,
    .retainsHistory = true,
    .executionRecording = UpscalerExecutionRecording::ExternalCommandBuffer,
    .externalResourceDependencies = kFsr3Dependencies
};

const std::array<UpscalerBackendDescriptor, 4> kUpscalerBackends{{
    {"native", "", "", &kNativeContract, true, true},
    {"linear", kLinearUpscalerModuleTypeName, "", &kLinearContract, true, false},
    {"fsr1", kFsr1UpscalerModuleTypeName, "Upscaler:Fsr1", &kFsr1Contract, true, false},
    {"fsr3", kFsr3UpscalerModuleTypeName, "Upscaler:Fsr3.1.4", &kFsr3Contract,
#if defined(VIXEN_CONFIGURED_FSR3_UPSCALER)
     true,
#else
     false,
#endif
     false}
}};

} // namespace

std::span<const UpscalerBackendDescriptor> GetUpscalerBackendDescriptors() noexcept {
    return kUpscalerBackends;
}

const UpscalerBackendDescriptor* FindUpscalerBackend(std::string_view id) noexcept {
    const auto found = std::find_if(kUpscalerBackends.begin(), kUpscalerBackends.end(),
                                    [id](const auto& backend) { return backend.id == id; });
    return found == kUpscalerBackends.end() ? nullptr : &*found;
}

VIXEN_REGISTER_NODE(LinearUpscalerModuleType);
VIXEN_REGISTER_NODE(Fsr1UpscalerModuleType);
VIXEN_REGISTER_NODE(Fsr3UpscalerModuleType);

} // namespace Vixen::RenderGraph
