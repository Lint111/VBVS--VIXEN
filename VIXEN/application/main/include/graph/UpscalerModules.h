#pragma once

#include "Connection/SdiStageWiring.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "Core/RenderGraph.h"
#include "Data/CameraData.h"
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

CONSTEXPR_NODE_CONFIG(UpscalerModulePorts, 10, 2, SlotArrayMode::Single) {
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
    INPUT_SLOT(DEPTH_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 4,
               SlotNullability::Optional, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(MOTION_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 5,
               SlotNullability::Optional, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(EXPOSURE_TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 6,
               SlotNullability::Optional, SlotRole::Dependency | SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(JITTER, glm::vec2, 7,
               SlotNullability::Optional, SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(HISTORY_RESET, uint32_t, 8,
               SlotNullability::Optional, SlotRole::Execute,
               SlotMutability::ReadOnly, SlotScope::NodeLevel);
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 9,
               SlotNullability::Optional, SlotRole::Execute,
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
        inputs[DEPTH_TARGET_Slot::index].nullable = DEPTH_TARGET_Slot::nullable;
        inputs[MOTION_TARGET_Slot::index].nullable = MOTION_TARGET_Slot::nullable;
        inputs[EXPOSURE_TARGET_Slot::index].nullable = EXPOSURE_TARGET_Slot::nullable;
        inputs[JITTER_Slot::index].nullable = JITTER_Slot::nullable;
        inputs[HISTORY_RESET_Slot::index].nullable = HISTORY_RESET_Slot::nullable;
        inputs[CAMERA_DATA_Slot::index].nullable = CAMERA_DATA_Slot::nullable;

        HandleDescriptor renderTargetDesc{"IRenderTarget*"};
        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_OUTPUT_DESC(UPSCALED_TARGET, "upscaled_target",
                         ResourceLifetime::Persistent, renderTargetDesc);

        HandleDescriptor completionSemaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE, "render_complete",
                         ResourceLifetime::Transient, completionSemaphoreDesc);
        INIT_INPUT_DESC(DEPTH_TARGET, "depth_target", ResourceLifetime::Persistent, renderTargetDesc);
        INIT_INPUT_DESC(MOTION_TARGET, "motion_target", ResourceLifetime::Persistent, renderTargetDesc);
        INIT_INPUT_DESC(EXPOSURE_TARGET, "exposure_target", ResourceLifetime::Persistent, renderTargetDesc);
        INIT_INPUT_DESC(JITTER, "jitter", ResourceLifetime::Transient, HandleDescriptor{"glm::vec2"});
        inputs[JITTER_Slot::index].acceptsVisualRelaxation = true;
        INIT_INPUT_DESC(HISTORY_RESET, "history_reset", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Transient,
                        HandleDescriptor{"CameraDataPtr"});
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
    // Called once a provider's real context has been initialized or has failed to initialize
    // during graph compilation; the host decides whether to fall back.
    std::function<void(UpscalerProviderInitializationResult)> providerInitializationReported;
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 5000.0f;
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
inline constexpr char kFsr3UpscalerModuleTypeName[] = "Upscaler.Fsr3.1.4";

} // namespace Vixen::RenderGraph
