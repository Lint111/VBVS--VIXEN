#pragma once

// Semantic Shader Wiring S2 — synthesis half, slice A.
// (undertow docs/plans/2026-08-03-semantic-shader-wiring.md)
//
// SynthesizeComputeStage: the gatherer/descriptor-set/pipeline plumbing of a
// compute stage stops being authored surface. Given the stage's own
// declarations (its shader-lib node, its stage node, the engine-common
// quintet, the provider registry, and its feature set), the helper CREATES
// the four plumbing nodes and emits every connect the hand-written quintet
// blocks used to write — including the SDI member wires.
//
// Lives in Nodes/ (the dual-aware layer): Core/ must not include concrete
// leaf node types (AR#3/#4 one-directional layering), and this helper
// necessarily names them.

#include "Core/RenderGraph.h"
#include "Core/TypedConnection.h"
#include "Connection/SdiStageWiring.h"

#include "Core/NodeRegistration.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"

#include <functional>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief The engine-common nodes every synthesized stage plumbs to.
 */
struct SdiStageCommon {
    NodeHandle device;
    NodeHandle commandPool;
    NodeHandle swapChain;
    NodeHandle frameSync;
};

/**
 * @brief Handles of the plumbing nodes a synthesis call created.
 */
struct SynthesizedStage {
    NodeHandle descGatherer;
    NodeHandle pushGatherer;
    NodeHandle descriptorSet;
    NodeHandle pipeline;
};

/**
 * @brief One stage of a shared-chain group (SynthesizeComputeStageGroup).
 *
 * The push-gatherer name is explicit (not derived from the group name)
 * because existing groups predate the `<name>_push_gatherer` convention —
 * e.g. bucketing's `recipe_bucketing_mode_init_pc_gatherer` — and migrated
 * stages keep their log identity.
 */
struct SdiGroupStageDecl {
    NodeHandle stage;
    std::string pushGathererName;
    /// Per-stage provider additions layered on the shared registry before the
    /// push wire (e.g. bucketing's per-stage `mode` constant). May be empty.
    std::function<void(SdiProviderRegistry&)> overlayProviders;
};

/**
 * @brief Handles created for a stage group: one shared descriptor chain,
 *        one push gatherer per stage (parallel to the decl vector).
 */
struct SynthesizedStageGroup {
    NodeHandle descGatherer;
    NodeHandle descriptorSet;
    NodeHandle pipeline;
    std::vector<NodeHandle> pushGatherers;
};

namespace Detail {

/// Shader-lib/descriptor/pipeline chain (verbatim from the hand quintets).
inline void WireSdiQuintetChain(ConnectionBatch& batch, const SdiStageCommon& common,
                                NodeHandle shaderLib, NodeHandle descGatherer,
                                NodeHandle descriptorSet, NodeHandle pipeline) {
    batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  descriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(descGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  pipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         // DescriptorSetNode reads swapChainImageCount at Compile to size its
         // descriptor-set ring — SWAPCHAIN_INFO/IMAGE_INDEX are required even
         // for stages that never touch the swapchain.
         .Connect(common.swapChain, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  descriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(common.swapChain, SwapChainNodeConfig::IMAGE_INDEX,
                  descriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // Set ring == flight ring: without this the producer indexes by
         // IMAGE_INDEX while consumers select by frame index (sync-reuse fix).
         .Connect(common.frameSync, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  descriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX);
}

/// A stage node's engine-common inputs.
inline void WireSdiStageCommons(ConnectionBatch& batch, const SdiStageCommon& common,
                                NodeHandle shaderLib, NodeHandle descriptorSet,
                                NodeHandle pipeline, NodeHandle stage) {
    batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  stage, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.commandPool, CommandPoolNodeConfig::COMMAND_POOL,
                  stage, ComputeStageNodeConfig::COMMAND_POOL)
         .Connect(pipeline, ComputePipelineNodeConfig::PIPELINE,
                  stage, ComputeStageNodeConfig::COMPUTE_PIPELINE)
         .Connect(pipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  stage, ComputeStageNodeConfig::PIPELINE_LAYOUT)
         .Connect(descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  stage, ComputeStageNodeConfig::DESCRIPTOR_SETS)
         .Connect(common.swapChain, SwapChainNodeConfig::IMAGE_INDEX,
                  stage, ComputeStageNodeConfig::IMAGE_INDEX)
         .Connect(common.frameSync, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  stage, ComputeStageNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(common.frameSync, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  stage, ComputeStageNodeConfig::IN_FLIGHT_FENCE)
         .Connect(common.frameSync, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  stage, ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(common.swapChain, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  stage, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  stage, ComputeStageNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(common.frameSync, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  stage, ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(common.frameSync, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  stage, ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);
}

/// Push-constant plumbing (bundle -> gatherer; data/ranges -> stage).
inline void WireSdiPushPlumbing(ConnectionBatch& batch, NodeHandle shaderLib,
                                NodeHandle pushGatherer, NodeHandle stage) {
    batch.Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pushGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(pushGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  stage, ComputeStageNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(pushGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  stage, ComputeStageNodeConfig::PUSH_CONSTANT_RANGES);
}

} // namespace Detail

/**
 * @brief Create and wire a compute stage's plumbing from its merged SDI.
 *
 * Node names follow the established convention so migrated stages keep their
 * log identity: `<name>_desc_gatherer`, `<name>_push_gatherer`,
 * `<name>_descriptors`, `<name>_pipeline`.
 *
 * The shader-lib and stage nodes stay AUTHORED (shader identity and dispatch
 * dimensions are the pass's own declarations); everything between them is
 * synthesized.
 */
template<typename Meta, const auto& Members>
SynthesizedStage SynthesizeComputeStage(
    RenderGraph* graph, ConnectionBatch& batch, const std::string& name,
    NodeHandle shaderLib, NodeHandle stage, const SdiStageCommon& common,
    const SdiProviderRegistry& registry, const SdiFeatureSet& features,
    SdiWireSet wireSet = SdiWireSet::All)
{
    SynthesizedStage s;
    s.descGatherer  = graph->AddNode<DescriptorResourceGathererNodeType>(name + "_desc_gatherer");
    s.pushGatherer  = graph->AddNode<PushConstantGathererNodeType>(name + "_push_gatherer");
    s.descriptorSet = graph->AddNode<DescriptorSetNodeType>(name + "_descriptors");
    s.pipeline      = graph->AddNode<ComputePipelineNodeType>(name + "_pipeline");

    Detail::WireSdiQuintetChain(batch, common, shaderLib, s.descGatherer,
                                s.descriptorSet, s.pipeline);
    Detail::WireSdiStageCommons(batch, common, shaderLib, s.descriptorSet,
                                s.pipeline, stage);
    Detail::WireSdiPushPlumbing(batch, shaderLib, s.pushGatherer, stage);

    // The stage's members, from its merged SDI.
    WireStageFromSdi<Meta, Members>(batch, registry, s.descGatherer,
                                    s.pushGatherer, features, wireSet);
    return s;
}

/**
 * @brief Create and wire a stage GROUP: one shared descriptor chain
 *        (desc-gatherer/descriptor-set/pipeline off one shader lib) serving
 *        N stage nodes, each with its OWN push gatherer.
 *
 * The shape bucketing authored by hand: three mode stages share
 * `recipe_bucketing_{desc_gatherer,descriptors,pipeline}` — the shader's
 * push-constant `mode` field, not a different pipeline, selects behavior —
 * while each stage's push gatherer wires from the shared registry plus that
 * stage's own overlay providers.
 *
 * Descriptor members wire ONCE (against the shared gatherer); push members
 * wire per stage. Shared node names follow the `<name>_*` convention.
 */
template<typename Meta, const auto& Members>
SynthesizedStageGroup SynthesizeComputeStageGroup(
    RenderGraph* graph, ConnectionBatch& batch, const std::string& name,
    NodeHandle shaderLib, const std::vector<SdiGroupStageDecl>& stages,
    const SdiStageCommon& common, const SdiProviderRegistry& registry,
    const SdiFeatureSet& features)
{
    SynthesizedStageGroup g;
    g.descGatherer  = graph->AddNode<DescriptorResourceGathererNodeType>(name + "_desc_gatherer");
    g.descriptorSet = graph->AddNode<DescriptorSetNodeType>(name + "_descriptors");
    g.pipeline      = graph->AddNode<ComputePipelineNodeType>(name + "_pipeline");

    Detail::WireSdiQuintetChain(batch, common, shaderLib, g.descGatherer,
                                g.descriptorSet, g.pipeline);

    // Descriptors once, for the whole group (the push-gatherer argument is
    // unused under DescriptorsOnly — same convention the hand call sites used).
    WireStageFromSdi<Meta, Members>(batch, registry, g.descGatherer,
                                    g.descGatherer, features,
                                    SdiWireSet::DescriptorsOnly);

    g.pushGatherers.reserve(stages.size());
    for (const auto& decl : stages) {
        NodeHandle pushGatherer =
            graph->AddNode<PushConstantGathererNodeType>(decl.pushGathererName);
        g.pushGatherers.push_back(pushGatherer);

        Detail::WireSdiStageCommons(batch, common, shaderLib, g.descriptorSet,
                                    g.pipeline, decl.stage);
        Detail::WireSdiPushPlumbing(batch, shaderLib, pushGatherer, decl.stage);

        SdiProviderRegistry stageRegistry = registry;
        if (decl.overlayProviders) {
            decl.overlayProviders(stageRegistry);
        }
        WireStageFromSdi<Meta, Members>(batch, stageRegistry, g.descGatherer,
                                        pushGatherer, features,
                                        SdiWireSet::PushOnly);
    }
    return g;
}

} // namespace Vixen::RenderGraph
