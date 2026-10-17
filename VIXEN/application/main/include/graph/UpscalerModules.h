#pragma once

#include "Connection/SdiStageWiring.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "Core/RenderGraph.h"
#include "graph/UpscalerContract.h"
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

        HandleDescriptor renderTargetDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(UPSCALED_TARGET, "upscaled_target",
                         ResourceLifetime::Persistent, renderTargetDesc);

        HandleDescriptor completionSemaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE, "render_complete",
                         ResourceLifetime::Transient, completionSemaphoreDesc);
    }
};

static_assert(std::is_same_v<UpscalerModulePorts::UPSCALED_TARGET_Slot::Type,
                             BlitNodeConfig::UPSCALED_IMAGE_READ_Slot::Type>);

// Engine-owned inputs and services needed while a backend adds its work to the graph. Provider
// settings remain in the backend implementation; this context carries only frame resources,
// graph services, extents and generic execution recording.
struct UpscalerModuleBuildContext {
    ConnectionBatch* batch = nullptr;
    SdiStageCommon common{};
    SdiProviderRegistry sceneProviders;
    NodeHandle sourceTarget{};
    VkExtent2D renderExtent{};
    VkExtent2D presentationExtent{};
    float renderScale = 1.0f;
    std::function<std::shared_ptr<ShaderManagement::ShaderFamily>(
        const std::string&, const std::string&)> makeShaderFamily;
    std::function<void(NodeHandle, std::shared_ptr<ShaderManagement::ShaderFamily>)> registerShaderFamily;
    // RenderGraph nodes record commands; the host records their identities here so it can attach
    // GPU timing after graph compilation without inspecting a backend's private graph members.
    std::function<void(std::string, NodeHandle)> recordExecution;
};

struct UpscalerBackendDescriptor {
    std::string_view id;
    std::string_view moduleTypeName;
    std::string_view runtimeCapability;
    const UpscalerContract* contract = nullptr;
    bool compiled = false;
    bool nativeFallback = false;
};

std::span<const UpscalerBackendDescriptor> GetUpscalerBackendDescriptors() noexcept;
const UpscalerBackendDescriptor* FindUpscalerBackend(std::string_view id) noexcept;

inline constexpr char kLinearUpscalerModuleTypeName[] = "Upscaler.Linear";
inline constexpr char kFsr1UpscalerModuleTypeName[] = "Upscaler.Fsr1";

} // namespace Vixen::RenderGraph
