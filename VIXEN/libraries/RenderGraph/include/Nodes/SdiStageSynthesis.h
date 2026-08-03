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

#include <string>

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

    // Shader-lib/descriptor/pipeline chain (verbatim from the hand quintets).
    batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  s.descriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  s.pipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  s.descGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(s.descGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  s.descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  s.descriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  s.pipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(s.descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  s.pipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(common.swapChain, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  s.descriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(common.swapChain, SwapChainNodeConfig::IMAGE_INDEX,
                  s.descriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         .Connect(common.frameSync, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  s.descriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX);

    // Stage common inputs.
    batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  stage, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
         .Connect(common.commandPool, CommandPoolNodeConfig::COMMAND_POOL,
                  stage, ComputeStageNodeConfig::COMMAND_POOL)
         .Connect(s.pipeline, ComputePipelineNodeConfig::PIPELINE,
                  stage, ComputeStageNodeConfig::COMPUTE_PIPELINE)
         .Connect(s.pipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  stage, ComputeStageNodeConfig::PIPELINE_LAYOUT)
         .Connect(s.descriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
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

    // Push-constant plumbing.
    batch.Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  s.pushGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(s.pushGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  stage, ComputeStageNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(s.pushGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  stage, ComputeStageNodeConfig::PUSH_CONSTANT_RANGES);

    // The stage's members, from its merged SDI.
    WireStageFromSdi<Meta, Members>(batch, registry, s.descGatherer,
                                    s.pushGatherer, features, wireSet);
    return s;
}

} // namespace Vixen::RenderGraph
