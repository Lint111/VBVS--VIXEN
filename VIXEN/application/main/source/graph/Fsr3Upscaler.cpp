#include "graph/Fsr3Upscaler.h"

#include "IRenderTarget.h"
#include "Nodes/Common/SwapchainBarriers.h"
#include "VulkanDevice.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace Vixen::RenderGraph {
namespace {

class Fsr3ContextState {
public:
    Fsr3ContextState(VkExtent2D renderExtent, VkExtent2D outputExtent,
                     ReconstructionDepthConvention depthConvention,
                     float nearPlane, float farPlane)
        : renderExtent_(renderExtent), outputExtent_(outputExtent),
          depthConvention_(depthConvention), nearPlane_(nearPlane), farPlane_(farPlane) {}

    ~Fsr3ContextState() { Cleanup(); }

    void Initialize(VulkanDevice* device) {
        if (!device || device->device == VK_NULL_HANDLE || !device->gpu ||
            *device->gpu == VK_NULL_HANDLE) {
            throw std::runtime_error("FSR 3.1.4 cannot initialize without a live Vulkan device");
        }
        if (depthConvention_ != ReconstructionDepthConvention::ForwardZeroToOne &&
            depthConvention_ != ReconstructionDepthConvention::ReversedZeroToOne) {
            throw std::runtime_error("FSR 3.1.4 requires a zero-to-one depth convention");
        }
        if (renderExtent_.width == 0 || renderExtent_.height == 0 ||
            outputExtent_.width == 0 || outputExtent_.height == 0 ||
            nearPlane_ <= 0.0f || farPlane_ <= nearPlane_) {
            throw std::runtime_error("FSR 3.1.4 context dimensions or clip planes are invalid");
        }
        device_ = device;
        const size_t scratchSize = ffxGetScratchMemorySizeVK(*device_->gpu, 1);
        scratch_.reset(static_cast<uint8_t*>(std::calloc(1, scratchSize)));
        if (!scratch_) {
            throw std::bad_alloc();
        }
        VkDeviceContext vkContext{};
        vkContext.vkDevice = device_->device;
        vkContext.vkPhysicalDevice = *device_->gpu;
        vkContext.vkDeviceProcAddr = vkGetDeviceProcAddr;
        const FfxDevice ffxDevice = ffxGetDeviceVK(&vkContext);
        const FfxErrorCode interfaceResult = ffxGetInterfaceVK(
            &backendInterface_, ffxDevice, scratch_.get(), scratchSize, 1);
        CheckFfx(interfaceResult, "ffxGetInterfaceVK");

        FfxFsr3UpscalerContextDescription description{};
        if (depthConvention_ == ReconstructionDepthConvention::ReversedZeroToOne) {
            description.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
        }
        description.maxRenderSize = {renderExtent_.width, renderExtent_.height};
        description.maxUpscaleSize = {outputExtent_.width, outputExtent_.height};
        description.backendInterface = backendInterface_;
        CheckFfx(ffxFsr3UpscalerContextCreate(&context_, &description),
                 "ffxFsr3UpscalerContextCreate");
        initialized_ = true;
    }

    void Record(ComputeStageNode::Context& context, VkCommandBuffer commandBuffer,
                uint32_t, uint32_t) {
        if (!initialized_ || !device_) {
            throw std::runtime_error("FSR 3.1.4 dispatch requested before context initialization");
        }
        auto* output = context.In(ComputeStageNodeConfig::IMAGE_WRITE);
        const auto inputs = context.In(ComputeStageNodeConfig::IMAGE_READ_ARRAY);
        if (!output || inputs.size() != 4 ||
            !inputs[0] || !inputs[1] || !inputs[2] || !inputs[3]) {
            throw std::runtime_error("FSR 3.1.4 requires color, depth, motion and exposure images");
        }

        auto* color = inputs[0];
        auto* depth = inputs[1];
        auto* motion = inputs[2];
        auto* exposure = inputs[3];
        PrepareReadImage(*color, commandBuffer);
        PrepareReadImage(*depth, commandBuffer);
        PrepareReadImage(*motion, commandBuffer);
        PrepareReadImage(*exposure, commandBuffer);

        const VkExtent2D renderExtent = color->GetExtent();
        const VkExtent2D outputExtent = output->GetExtent();
        if (renderExtent.width > renderExtent_.width || renderExtent.height > renderExtent_.height ||
            outputExtent.width > outputExtent_.width || outputExtent.height > outputExtent_.height) {
            throw std::runtime_error("FSR 3.1.4 frame extent exceeds its initialized context");
        }

        const glm::vec2 jitter = context.In(ComputeStageNodeConfig::EXTERNAL_UV_JITTER);
        const uint32_t historyReset = context.In(ComputeStageNodeConfig::EXTERNAL_HISTORY_RESET);
        const CameraData& camera = context.In(ComputeStageNodeConfig::EXTERNAL_CAMERA_DATA);
        FfxFsr3UpscalerDispatchDescription dispatch{};
        dispatch.commandList = ffxGetCommandListVK(commandBuffer);
        dispatch.color = WrapImage(*color, FFX_RESOURCE_USAGE_READ_ONLY,
                                   FFX_RESOURCE_STATE_COMPUTE_READ, L"vixen_fsr3_color");
        dispatch.depth = WrapImage(*depth, FFX_RESOURCE_USAGE_READ_ONLY,
                                   FFX_RESOURCE_STATE_COMPUTE_READ, L"vixen_fsr3_depth");
        dispatch.motionVectors = WrapImage(*motion, FFX_RESOURCE_USAGE_READ_ONLY,
                                           FFX_RESOURCE_STATE_COMPUTE_READ, L"vixen_fsr3_motion");
        dispatch.exposure = WrapImage(*exposure, FFX_RESOURCE_USAGE_READ_ONLY,
                                      FFX_RESOURCE_STATE_COMPUTE_READ, L"vixen_fsr3_exposure");
        dispatch.output = WrapImage(*output, FFX_RESOURCE_USAGE_UAV,
                                    FFX_RESOURCE_STATE_UNORDERED_ACCESS, L"vixen_fsr3_output");
        // The engine stores jitter in input-pixel units, positive right/down. FidelityFX's
        // Vulkan sample programs the negated camera offset in its dispatch description.
        dispatch.jitterOffset = {-jitter.x, -jitter.y};
        dispatch.motionVectorScale = {static_cast<float>(renderExtent.width),
                                      static_cast<float>(renderExtent.height)};
        dispatch.renderSize = {renderExtent.width, renderExtent.height};
        dispatch.upscaleSize = {outputExtent.width, outputExtent.height};
        dispatch.enableSharpening = true;
        dispatch.sharpness = 0.2f;
        dispatch.frameTimeDelta = 1000.0f / 60.0f;
        dispatch.preExposure = 1.0f;
        dispatch.reset = historyReset != 0;
        dispatch.cameraNear = nearPlane_;
        dispatch.cameraFar = farPlane_;
        dispatch.cameraFovAngleVertical = glm::radians(camera.fov);
        dispatch.viewSpaceToMetersFactor = 1.0f;
        CheckFfx(ffxFsr3UpscalerContextDispatch(&context_, &dispatch),
                 "ffxFsr3UpscalerContextDispatch");
        output->SetImageLayout(output->GetCurrentIndex(), VK_IMAGE_LAYOUT_GENERAL);
    }

    void Cleanup() noexcept {
        if (initialized_) {
            if (device_ && device_->device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device_->device);
            }
            ffxFsr3UpscalerContextDestroy(&context_);
            initialized_ = false;
        }
        scratch_.reset();
        device_ = nullptr;
    }

private:
    static void CheckFfx(FfxErrorCode result, const char* operation) {
        if (result != FFX_OK) {
            throw std::runtime_error(std::string(operation) + " failed with FidelityFX error " +
                                     std::to_string(static_cast<int>(result)));
        }
    }

    static FfxResource WrapImage(Vixen::Vulkan::Resources::IRenderTarget& target,
                                 FfxResourceUsage extraUsage,
                                 FfxResourceStates state, const wchar_t* name) {
        const uint32_t imageIndex = target.GetCurrentIndex();
        const VkImage image = target.GetImage(imageIndex);
        const VkExtent2D extent = target.GetExtent();
        VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.format = target.GetFormat();
        createInfo.extent = {extent.width, extent.height, 1};
        createInfo.mipLevels = 1;
        createInfo.arrayLayers = 1;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        createInfo.usage = target.GetImageUsageFlags();
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.initialLayout = target.GetImageLayout(imageIndex);
        const FfxResourceDescription description =
            ffxGetImageResourceDescriptionVK(image, createInfo, extraUsage);
        return ffxGetResourceVK(reinterpret_cast<void*>(image), description, name, state);
    }

    void PrepareReadImage(Vixen::Vulkan::Resources::IRenderTarget& target,
                          VkCommandBuffer commandBuffer) {
        const uint32_t imageIndex = target.GetCurrentIndex();
        const VkImageLayout oldLayout = target.GetImageLayout(imageIndex);
        if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.GetImage(imageIndex);
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        device_->fpCmdPipelineBarrier2(commandBuffer, &dependency);
        target.SetImageLayout(imageIndex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkExtent2D renderExtent_{};
    VkExtent2D outputExtent_{};
    ReconstructionDepthConvention depthConvention_;
    float nearPlane_ = 0.1f;
    float farPlane_ = 5000.0f;
    VulkanDevice* device_ = nullptr;
    std::unique_ptr<uint8_t, decltype(&std::free)> scratch_{nullptr, &std::free};
    FfxInterface backendInterface_{};
    FfxFsr3UpscalerContext context_{};
    bool initialized_ = false;
};

} // namespace

Fsr3UpscalerHooks CreateFsr3UpscalerHooks(
    VkExtent2D renderExtent,
    VkExtent2D outputExtent,
    ReconstructionDepthConvention depthConvention,
    float nearPlane,
    float farPlane) {
    auto state = std::make_shared<Fsr3ContextState>(
        renderExtent, outputExtent, depthConvention, nearPlane, farPlane);
    return {
        [state](VulkanDevice* device) { state->Initialize(device); },
        [state](ComputeStageNode::Context& context, VkCommandBuffer commandBuffer,
                uint32_t imageIndex, uint32_t frameIndex) {
            state->Record(context, commandBuffer, imageIndex, frameIndex);
        },
        [state] { state->Cleanup(); }
    };
}

} // namespace Vixen::RenderGraph
