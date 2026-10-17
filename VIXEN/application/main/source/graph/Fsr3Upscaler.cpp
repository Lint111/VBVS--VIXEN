#include "graph/Fsr3Upscaler.h"
#include "graph/UpscalerContract.h"

#include "IRenderTarget.h"
#include "Nodes/Common/SwapchainBarriers.h"
#include "VulkanDevice.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

    UpscalerProviderInitializationResult Initialize(VulkanDevice* device) {
        std::string failure = InitializeContext(device);
        if (failure.empty()) return {"fsr3", true, {}};
        Cleanup();
        return {"fsr3", false, "FSR 3.1.4 context initialization failed: " + std::move(failure)};
    }

    std::string Record(ComputeStageNode::Context& context, VkCommandBuffer commandBuffer,
                       uint32_t, uint32_t frameIndex) {
        if (!initialized_ || !device_) {
            return "FSR 3.1.4 dispatch requested before context initialization";
        }
        const auto recordStart = std::chrono::steady_clock::now();
        auto* output = context.In(ComputeStageNodeConfig::IMAGE_WRITE);
        const auto inputs = context.In(ComputeStageNodeConfig::IMAGE_READ_ARRAY);
        if (!output || inputs.size() != 4 ||
            !inputs[0] || !inputs[1] || !inputs[2] || !inputs[3]) {
            return "FSR 3.1.4 requires color, depth, motion and exposure images";
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
            return "FSR 3.1.4 frame extent exceeds its initialized context";
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
        const SharedFrameResources& shared = sharedResources_[frameIndex % sharedResources_.size()];
        dispatch.dilatedDepth = shared.dilatedDepth.resource;
        dispatch.dilatedMotionVectors = shared.dilatedMotionVectors.resource;
        dispatch.reconstructedPrevNearestDepth = shared.reconstructedPrevNearestDepth.resource;
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
        const bool recordGpuTiming = timingEnabled_ && timingSampleCount_ < kMaxGpuTimingSamples;
        const uint32_t timingSample = recordGpuTiming ? timingSampleCount_++ : 0;
        if (recordGpuTiming) {
            vkCmdResetQueryPool(commandBuffer, timingPool_, timingSample * 2, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                timingPool_, timingSample * 2);
        } else if (timingEnabled_) {
            timingOverflow_ = true;
        }
        const auto dispatchStart = std::chrono::steady_clock::now();
        const std::string dispatchFailure = FfxFailure(
            ffxFsr3UpscalerContextDispatch(&context_, &dispatch), "ffxFsr3UpscalerContextDispatch");
        if (!dispatchFailure.empty()) return dispatchFailure;
        const auto dispatchEnd = std::chrono::steady_clock::now();
        if (recordGpuTiming) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                timingPool_, timingSample * 2 + 1);
        }
        output->SetImageLayout(output->GetCurrentIndex(), VK_IMAGE_LAYOUT_GENERAL);
        if (timingEnabled_ && cpuRecordMs_.size() < kMaxGpuTimingSamples) {
            using Millis = std::chrono::duration<double, std::milli>;
            cpuRecordMs_.push_back(Millis(std::chrono::steady_clock::now() - recordStart).count());
            cpuDispatchMs_.push_back(Millis(dispatchEnd - dispatchStart).count());
        }
        return {};
    }

    void Cleanup() noexcept {
        if (device_ && device_->device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_->device);
            CollectGpuTimings();
        }
        if (initialized_) {
            ffxFsr3UpscalerContextDestroy(&context_);
            initialized_ = false;
        }
        if (device_ && device_->device != VK_NULL_HANDLE) {
            for (SharedFrameResources& frame : sharedResources_) {
                DestroySharedImage(frame.reconstructedPrevNearestDepth);
                DestroySharedImage(frame.dilatedMotionVectors);
                DestroySharedImage(frame.dilatedDepth);
            }
            if (timingPool_ != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device_->device, timingPool_, nullptr);
                timingPool_ = VK_NULL_HANDLE;
            }
        }
        scratch_.reset();
        device_ = nullptr;
    }

private:
    // Returns the failure text, or an empty string once the context is ready.
    std::string InitializeContext(VulkanDevice* device) {
        if (!device || device->device == VK_NULL_HANDLE || !device->gpu ||
            *device->gpu == VK_NULL_HANDLE) {
            return "FSR 3.1.4 cannot initialize without a live Vulkan device";
        }
        if (depthConvention_ != ReconstructionDepthConvention::ForwardZeroToOne &&
            depthConvention_ != ReconstructionDepthConvention::ReversedZeroToOne) {
            return "FSR 3.1.4 requires a zero-to-one depth convention";
        }
        if (renderExtent_.width == 0 || renderExtent_.height == 0 ||
            outputExtent_.width == 0 || outputExtent_.height == 0 ||
            nearPlane_ <= 0.0f || farPlane_ <= nearPlane_) {
            return "FSR 3.1.4 context dimensions or clip planes are invalid";
        }
        device_ = device;
        const size_t scratchSize = ffxGetScratchMemorySizeVK(*device_->gpu, 1);
        scratch_.reset(static_cast<uint8_t*>(std::calloc(1, scratchSize)));
        if (!scratch_) {
            return "FSR 3.1.4 scratch memory allocation failed";
        }
        VkDeviceContext vkContext{};
        vkContext.vkDevice = device_->device;
        vkContext.vkPhysicalDevice = *device_->gpu;
        vkContext.vkDeviceProcAddr = vkGetDeviceProcAddr;
        const FfxDevice ffxDevice = ffxGetDeviceVK(&vkContext);
        const FfxErrorCode interfaceResult = ffxGetInterfaceVK(
            &backendInterface_, ffxDevice, scratch_.get(), scratchSize, 1);
        if (std::string failure = FfxFailure(interfaceResult, "ffxGetInterfaceVK");
            !failure.empty()) {
            return failure;
        }

        FfxFsr3UpscalerContextDescription description{};
        if (depthConvention_ == ReconstructionDepthConvention::ReversedZeroToOne) {
            description.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
        }
        description.maxRenderSize = {renderExtent_.width, renderExtent_.height};
        description.maxUpscaleSize = {outputExtent_.width, outputExtent_.height};
        description.backendInterface = backendInterface_;
        if (std::string failure = FfxFailure(
                ffxFsr3UpscalerContextCreate(&context_, &description),
                "ffxFsr3UpscalerContextCreate");
            !failure.empty()) {
            return failure;
        }
        initialized_ = true;
        CreateTimingPool();
        return CreateSharedResources();
    }

    static constexpr size_t kFramesInFlight = 4;
    static constexpr uint32_t kMaxGpuTimingSamples = 256;

    struct SharedImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        FfxResource resource{};
    };

    struct SharedFrameResources {
        SharedImage reconstructedPrevNearestDepth;
        SharedImage dilatedMotionVectors;
        SharedImage dilatedDepth;
    };

    void CreateTimingPool() {
        if (!std::getenv("VIXEN_FSR3_GPU_TIMING")) return;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(*device_->gpu, &properties);
        if (!properties.limits.timestampComputeAndGraphics ||
            properties.limits.timestampPeriod <= 0.0f) {
            std::fprintf(stderr, "[FSR3_GPU_TIMING] unavailable: device has no compute timestamp support\n");
            return;
        }
        timestampPeriodNs_ = properties.limits.timestampPeriod;
        VkQueryPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = kMaxGpuTimingSamples * 2;
        if (vkCreateQueryPool(device_->device, &poolInfo, nullptr, &timingPool_) != VK_SUCCESS) {
            std::fprintf(stderr, "[FSR3_GPU_TIMING] unavailable: timestamp query pool creation failed\n");
            return;
        }
        timingEnabled_ = true;
    }

    static void ReportCpuTiming(const char* name, std::vector<double>& samples) noexcept {
        if (samples.empty()) return;
        std::sort(samples.begin(), samples.end());
        const size_t middle = samples.size() / 2;
        const double median = samples.size() % 2 != 0
            ? samples[middle]
            : (samples[middle - 1] + samples[middle]) * 0.5;
        std::fprintf(stderr, "[FSR3_CPU_TIMING] %s samples=%zu median_ms=%.6f max_ms=%.6f\n",
                     name, samples.size(), median, samples.back());
    }

    void CollectGpuTimings() noexcept {
        ReportCpuTiming("record", cpuRecordMs_);
        ReportCpuTiming("context_dispatch", cpuDispatchMs_);
        if (!timingEnabled_ || timingPool_ == VK_NULL_HANDLE || timingSampleCount_ == 0) return;
        std::vector<double> elapsedMs;
        elapsedMs.reserve(timingSampleCount_);
        for (uint32_t sample = 0; sample < timingSampleCount_; ++sample) {
            std::array<uint64_t, 2> ticks{};
            const VkResult result = vkGetQueryPoolResults(
                device_->device, timingPool_, sample * 2, 2, sizeof(ticks), ticks.data(),
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            if (result != VK_SUCCESS || ticks[1] < ticks[0]) continue;
            const double ms = static_cast<double>(ticks[1] - ticks[0]) *
                              static_cast<double>(timestampPeriodNs_) / 1'000'000.0;
            elapsedMs.push_back(ms);
            std::fprintf(stderr, "[FSR3_GPU_TIMING] sample=%u gpu_ms=%.6f\n", sample, ms);
        }
        std::sort(elapsedMs.begin(), elapsedMs.end());
        if (!elapsedMs.empty()) {
            const size_t middle = elapsedMs.size() / 2;
            const double median = elapsedMs.size() % 2 != 0
                ? elapsedMs[middle]
                : (elapsedMs[middle - 1] + elapsedMs[middle]) * 0.5;
            std::fprintf(stderr,
                         "[FSR3_GPU_TIMING] summary samples=%zu median_ms=%.6f max_ms=%.6f budget_ms=2.000000\n",
                         elapsedMs.size(), median, elapsedMs.back());
        }
        if (timingOverflow_) {
            std::fprintf(stderr, "[FSR3_GPU_TIMING] sample limit reached (%u)\n", kMaxGpuTimingSamples);
        }
    }

    static VkFormat ToVkFormat(FfxSurfaceFormat format) {
        switch (format) {
        case FFX_SURFACE_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
        case FFX_SURFACE_FORMAT_R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
        case FFX_SURFACE_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
        default: return VK_FORMAT_UNDEFINED;
        }
    }

    std::optional<uint32_t> FindMemoryType(uint32_t memoryTypeBits) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(*device_->gpu, &properties);
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((memoryTypeBits & (1u << i)) != 0 &&
                (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
                return i;
            }
        }
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((memoryTypeBits & (1u << i)) != 0) return i;
        }
        return std::nullopt;
    }

    std::string CreateSharedImage(const FfxCreateResourceDescription& description, SharedImage& shared) {
        const FfxResourceDescription& resourceDescription = description.resourceDescription;
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = ToVkFormat(static_cast<FfxSurfaceFormat>(resourceDescription.format));
        if (imageInfo.format == VK_FORMAT_UNDEFINED) {
            return "FSR 3.1.4 requested an unsupported shared-resource format";
        }
        imageInfo.extent = {resourceDescription.width, resourceDescription.height, 1};
        imageInfo.mipLevels = resourceDescription.mipCount;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_->device, &imageInfo, nullptr, &shared.image) != VK_SUCCESS) {
            return "FSR 3.1.4 shared Vulkan image creation failed";
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_->device, shared.image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        const std::optional<uint32_t> memoryType = FindMemoryType(requirements.memoryTypeBits);
        if (!memoryType) {
            return "FSR 3.1.4 shared image has no compatible Vulkan memory type";
        }
        allocation.memoryTypeIndex = *memoryType;
        if (vkAllocateMemory(device_->device, &allocation, nullptr, &shared.memory) != VK_SUCCESS) {
            return "FSR 3.1.4 shared Vulkan image memory allocation failed";
        }
        if (vkBindImageMemory(device_->device, shared.image, shared.memory, 0) != VK_SUCCESS) {
            return "FSR 3.1.4 shared Vulkan image memory binding failed";
        }

        const FfxResourceDescription ffxDescription = ffxGetImageResourceDescriptionVK(
            shared.image, imageInfo, resourceDescription.usage);
        shared.resource = ffxGetResourceVK(reinterpret_cast<void*>(shared.image), ffxDescription,
                                           description.name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        return {};
    }

    std::string CreateSharedResources() {
        FfxFsr3UpscalerSharedResourceDescriptions descriptions{};
        if (std::string failure = FfxFailure(
                ffxFsr3UpscalerGetSharedResourceDescriptions(&context_, &descriptions),
                "ffxFsr3UpscalerGetSharedResourceDescriptions");
            !failure.empty()) {
            return failure;
        }
        for (SharedFrameResources& frame : sharedResources_) {
            std::string failure = CreateSharedImage(descriptions.reconstructedPrevNearestDepth,
                                                    frame.reconstructedPrevNearestDepth);
            if (failure.empty()) {
                failure = CreateSharedImage(descriptions.dilatedMotionVectors,
                                            frame.dilatedMotionVectors);
            }
            if (failure.empty()) {
                failure = CreateSharedImage(descriptions.dilatedDepth, frame.dilatedDepth);
            }
            if (!failure.empty()) return failure;
        }
        return {};
    }

    void DestroySharedImage(SharedImage& shared) noexcept {
        if (shared.image != VK_NULL_HANDLE) {
            vkDestroyImage(device_->device, shared.image, nullptr);
            shared.image = VK_NULL_HANDLE;
        }
        if (shared.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_->device, shared.memory, nullptr);
            shared.memory = VK_NULL_HANDLE;
        }
        shared.resource = {};
    }

    static std::string FfxFailure(FfxErrorCode result, const char* operation) {
        if (result == FFX_OK) return {};
        return std::string(operation) + " failed with FidelityFX error " +
               std::to_string(static_cast<int>(result));
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
    std::array<SharedFrameResources, kFramesInFlight> sharedResources_{};
    VkQueryPool timingPool_ = VK_NULL_HANDLE;
    uint32_t timingSampleCount_ = 0;
    float timestampPeriodNs_ = 0.0f;
    bool timingEnabled_ = false;
    bool timingOverflow_ = false;
    std::vector<double> cpuRecordMs_;
    std::vector<double> cpuDispatchMs_;
    bool initialized_ = false;
};

} // namespace

Fsr3UpscalerHooks CreateFsr3UpscalerHooks(
    VkExtent2D renderExtent,
    VkExtent2D outputExtent,
    ReconstructionDepthConvention depthConvention,
    float nearPlane,
    float farPlane,
    std::function<void(UpscalerProviderInitializationResult)> reportRecordFailure) {
    auto state = std::make_shared<Fsr3ContextState>(
        renderExtent, outputExtent, depthConvention, nearPlane, farPlane);
    return {
        [state](VulkanDevice* device) { return state->Initialize(device); },
        [state, reportRecordFailure = std::move(reportRecordFailure)](
            ComputeStageNode::Context& context, VkCommandBuffer commandBuffer,
            uint32_t imageIndex, uint32_t frameIndex) {
            std::string failure = state->Record(context, commandBuffer, imageIndex, frameIndex);
            if (!failure.empty() && reportRecordFailure) {
                reportRecordFailure({"fsr3", false, failure});
            }
            return failure;
        },
        [state] { state->Cleanup(); }
    };
}

} // namespace Vixen::RenderGraph
