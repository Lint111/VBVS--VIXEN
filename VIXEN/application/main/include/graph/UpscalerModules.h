#pragma once

#include "Connection/SdiStageWiring.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "Core/RenderGraph.h"
#include "Nodes/SdiStageSynthesis.h"
#include "ShaderFamily.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

namespace Vixen::RenderGraph {

class GPUPerformanceLogger;

CONSTEXPR_NODE_CONFIG(UpscalerModulePorts, 4, 2, SlotArrayMode::Single) {
    INPUT_SLOT(SOURCE_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 0,
               SlotNullability::Required, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(PRESENTATION_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 1,
               SlotNullability::Optional, SlotRole::Dependency,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(ORDERING_WAIT, VkSemaphore, 2,
               SlotNullability::Required, SlotRole::Dependency,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(DEVICE, Vixen::Vulkan::Resources::VulkanDevice*, 3,
               SlotNullability::Optional, SlotRole::Dependency,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(UPSCALED_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(RENDER_COMPLETE, VkSemaphore, 1,
                SlotNullability::Required, SlotMutability::WriteOnly);

    // SubGraphType publishes the runtime descriptors to RenderGraph, while the
    // typed slots above remain the source of truth for optionality. Keep those
    // views aligned so GraphScope::Finalize does not mistake optional module
    // inputs for required inputs.
    UpscalerModulePorts() {
        inputs[SOURCE_TARGET_Slot::index].nullable = SOURCE_TARGET_Slot::nullable;
        inputs[PRESENTATION_TARGET_Slot::index].nullable = PRESENTATION_TARGET_Slot::nullable;
        inputs[ORDERING_WAIT_Slot::index].nullable = ORDERING_WAIT_Slot::nullable;
        inputs[DEVICE_Slot::index].nullable = DEVICE_Slot::nullable;
    }
};

static_assert(std::is_same_v<UpscalerModulePorts::UPSCALED_TARGET_Slot::Type,
                             BlitNodeConfig::UPSCALED_IMAGE_READ_Slot::Type>);

struct LinearUpscalerParams {};

struct Fsr1UpscalerParams {
    ConnectionBatch* batch = nullptr;
    SdiStageCommon common{};
    SdiProviderRegistry sceneProviders;
    NodeHandle sourceTarget{};
    VkExtent2D renderExtent{};
    uint32_t presentationWidth = 0;
    uint32_t presentationHeight = 0;
    float renderScale = 1.0f;
    std::function<std::shared_ptr<ShaderManagement::ShaderFamily>(
        const std::string&, const std::string&)> makeShaderFamily;
    std::function<void(NodeHandle, std::shared_ptr<ShaderManagement::ShaderFamily>)> registerShaderFamily;
    std::function<void(std::string, GPUPerformanceLogger*)> registerPassMetric;
};

inline constexpr char kLinearUpscalerModuleTypeName[] = "Upscaler.Linear";
inline constexpr char kFsr1UpscalerModuleTypeName[] = "Upscaler.Fsr1";

} // namespace Vixen::RenderGraph
