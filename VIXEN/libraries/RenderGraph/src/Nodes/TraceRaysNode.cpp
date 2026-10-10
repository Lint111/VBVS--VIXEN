#include "Nodes/Common/PresentationSynchronization.h"
#include "Nodes/TraceRaysNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> TraceRaysNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new TraceRaysNode(instanceName, const_cast<TraceRaysNodeType*>(this))
    );
}

// ============================================================================
// TRACE RAYS NODE IMPLEMENTATION
// ============================================================================

TraceRaysNode::TraceRaysNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<TraceRaysNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("TraceRaysNode constructor (Phase K)");
}

void TraceRaysNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[TraceRaysNode::SetupImpl] ENTERED");

    width_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_WIDTH, 1920u);
    height_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_HEIGHT, 1080u);
    depth_ = GetParameterValue<uint32_t>(TraceRaysNodeConfig::PARAM_DEPTH, 1u);

    NODE_LOG_INFO("TraceRays setup: dimensions=" +
                  std::to_string(width_) + "x" + std::to_string(height_) + "x" + std::to_string(depth_));

    NODE_LOG_DEBUG("[TraceRaysNode::SetupImpl] COMPLETED");
}

void TraceRaysNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[TraceRaysNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== TraceRaysNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(TraceRaysNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[TraceRaysNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Get command pool
    commandPool_ = ctx.In(TraceRaysNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[TraceRaysNode] COMMAND_POOL is null");
    }

    // Get pipeline data
    pipelineData_ = ctx.In(TraceRaysNodeConfig::RT_PIPELINE_DATA);
    if (!pipelineData_ || !pipelineData_->IsValid()) {
        throw std::runtime_error("[TraceRaysNode] RT_PIPELINE_DATA is null or invalid");
    }

    // Get acceleration structure data (still needed for SBT configuration)
    accelData_ = ctx.In(TraceRaysNodeConfig::ACCELERATION_STRUCTURE_DATA);
    if (!accelData_ || !accelData_->IsValid()) {
        throw std::runtime_error("[TraceRaysNode] ACCELERATION_STRUCTURE_DATA is null or invalid");
    }

    // Get swapchain info for command buffer allocation count (matches ComputeDispatchNode)
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(TraceRaysNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("[TraceRaysNode] SWAPCHAIN_INFO is null");
    }
    swapChainImageCount_ = swapchainInfo->GetImageCount();
    NODE_LOG_INFO("[TraceRaysNode] Swapchain image count: " + std::to_string(swapChainImageCount_));

    // Load RTX functions
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[TraceRaysNode] Failed to load RTX functions");
    }

    // NOTE: Descriptor resources are now managed by DescriptorSetNode
    // We receive descriptor sets via DESCRIPTOR_SETS input in ExecuteImpl
    // This follows the same pattern as ComputeDispatchNode

    // R486 extends the existing shader contract; offsets and types come from reflection.
    const auto& bundle = ctx.In(TraceRaysNodeConfig::SHADER_DATA_BUNDLE);
    if (!bundle || !bundle->reflectionData)
        throw std::runtime_error("TraceRaysNode: ray-generation reflection is required");
    const auto fieldOffset = [&](const char* name) {
        for (const auto& range : bundle->reflectionData->pushConstants) {
            if (!(range.stageFlags & VK_SHADER_STAGE_RAYGEN_BIT_KHR)) continue;
            for (const auto& member : range.structDef.members) {
                if (member.name != name) continue;
                if (member.type.baseType != ShaderManagement::SpirvTypeInfo::BaseType::UInt ||
                    member.type.width != 32 || member.type.vecSize != 3 || member.type.arraySize != 0 ||
                    member.offset < pipelineData_->pushConstantOffset ||
                    uint64_t(member.offset) + sizeof(uint32_t) * 3 >
                        uint64_t(pipelineData_->pushConstantOffset) + pipelineData_->pushConstantSize)
                    throw std::runtime_error(std::string("TraceRaysNode: invalid uvec3 push member ") + name);
                return member.offset - pipelineData_->pushConstantOffset;
            }
        }
        throw std::runtime_error(std::string("TraceRaysNode: missing ray-generation push member ") + name);
    };
    tileOriginOffset_ = fieldOffset("tileOrigin");
    fullLaunchExtentOffset_ = fieldOffset("fullLaunchExtent");
    if (!(pipelineData_->pushConstantStages & VK_SHADER_STAGE_RAYGEN_BIT_KHR))
        throw std::runtime_error("TraceRaysNode: push range must include ray generation");
    tileCommandBuffers_.resize(swapChainImageCount_);

    // Allocate command buffers
    if (!AllocateCommandBuffers()) {
        throw std::runtime_error("[TraceRaysNode] Failed to allocate command buffers");
    }

    // Create GPU performance logger using centralized GPUQueryManager from VulkanDevice
    // Sprint 6.3 Phase 0: All nodes share the same query manager to prevent slot conflicts
    auto* queryMgrPtr = static_cast<GPUQueryManager*>(vulkanDevice_->GetQueryManager());
    if (queryMgrPtr) {
        // Wrap raw pointer in shared_ptr with no-op deleter (VulkanDevice owns the manager)
        auto queryManager = std::shared_ptr<GPUQueryManager>(queryMgrPtr, [](GPUQueryManager*){});

        gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(instanceName, queryManager);
        gpuPerfLogger_->SetEnabled(true);
        gpuPerfLogger_->SetLogFrequency(120);
        gpuPerfLogger_->SetPrintToTerminal(false);

        if (nodeLogger) {
            nodeLogger->AddChild(gpuPerfLogger_);
        }

        if (gpuPerfLogger_->IsTimingSupported()) {
            NODE_LOG_INFO("[TraceRaysNode] GPU performance timing enabled");
        } else {
            NODE_LOG_WARNING("[TraceRaysNode] GPU timing not supported on this device");
        }
    } else {
        NODE_LOG_WARNING("[TraceRaysNode] GPUQueryManager not available from VulkanDevice");
    }

    // Sprint 6.5: Register GPU task profile for cost estimation and learning
    std::string profileId = GetInstanceName() + "_gpu_trace";
    gpuProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "raytracing");
    if (gpuProfile_) {
        RegisterPhaseProfile(TaskProfilePhase::Execute, gpuProfile_);
        NODE_LOG_INFO("[TraceRaysNode] Registered GPU profile: " + profileId);
    }

    NODE_LOG_INFO("=== TraceRaysNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[TraceRaysNode::CompileImpl] COMPLETED");
}

void TraceRaysNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // Get frame info
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(TraceRaysNodeConfig::SWAPCHAIN_INFO);
    uint32_t imageIndex = ctx.In(TraceRaysNodeConfig::IMAGE_INDEX);
    uint32_t currentFrame = ctx.In(TraceRaysNodeConfig::CURRENT_FRAME_INDEX);
    VkFence inFlightFence = ctx.In(TraceRaysNodeConfig::IN_FLIGHT_FENCE);

    // Get semaphore arrays
    const std::vector<VkSemaphore>& imageAvailableSemaphores =
        ctx.In(TraceRaysNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores =
        ctx.In(TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // Get push constants (optional)
    std::vector<uint8_t> pushConstData = ctx.In(TraceRaysNodeConfig::PUSH_CONSTANT_DATA);

    // Get descriptor sets from DescriptorSetNode (following ComputeDispatchNode pattern)
    const std::vector<VkDescriptorSet>& descriptorSets = ctx.In(TraceRaysNodeConfig::DESCRIPTOR_SETS);
    if (descriptorSets.empty()) {
        NODE_LOG_ERROR("[TraceRaysNode] No descriptor sets received from graph");
        return;
    }

    // Validate descriptor set for this image
    if (imageIndex >= descriptorSets.size()) {
        NODE_LOG_ERROR("[TraceRaysNode] Invalid descriptor set index: " +
                       std::to_string(imageIndex) + " >= " + std::to_string(descriptorSets.size()));
        return;
    }

    VkDescriptorSet descriptorSet = descriptorSets[imageIndex];
    if (descriptorSet == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[TraceRaysNode] Descriptor set is null for image " + std::to_string(imageIndex));
        return;
    }

    static int logCounter = 0;
    if (logCounter++ < 5) {
        NODE_LOG_INFO("[TraceRaysNode] Using descriptor set from graph for image " +
                      std::to_string(imageIndex) + " (frame " + std::to_string(currentFrame) + ")");
    }

    if (!swapchainInfo || imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size() ||
        imageIndex >= swapchainInfo->GetImageCount()) return;
    const auto handoffs = ResolveTargetSemaphoreHandoffs(
        swapchainInfo, imageAvailableSemaphores, renderCompleteSemaphores,
        currentFrame, imageIndex, true, true);

    VkDevice device = vulkanDevice_->device;

    // Reset fence before submitting (fence was already waited on by FrameSyncNode)
    // This matches ComputeDispatchNode pattern exactly
    VkResult resetResult = vkResetFences(device, 1, &inFlightFence);
    if (resetResult != VK_SUCCESS) {
        if (resetResult == VK_ERROR_DEVICE_LOST) {
            GetOwningGraph()->NotifyDeviceLost("TraceRaysNode::ExecuteImpl vkResetFences");
        }
        throw std::runtime_error("[TraceRaysNode::ExecuteImpl] Failed to reset fence: " + std::to_string(resetResult));
    }

    if (currentFrame >= tileQueries_.size()) {
        tileQueries_.resize(currentFrame + 1);
        tileCounts_.resize(currentFrame + 1);
        tileBudgets_.resize(currentFrame + 1);
    }
    CollectTileTiming(currentFrame);

    // Collect GPU performance results for this frame-in-flight (after fence wait)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrame);

        // Sprint 6.5: Feed GPU timing to task profile for cost learning
        if (gpuProfile_) {
            auto sample = gpuProfile_->Sample();
            float gpuTimeMs = gpuPerfLogger_->GetLastDispatchMs();
            if (gpuTimeMs > 0.0f) {
                uint64_t gpuTimeNs = static_cast<uint64_t>(gpuTimeMs * 1'000'000.0f);
                sample.Finalize(gpuTimeNs);
            } else {
                sample.Cancel();  // No valid measurement
            }
        }
    }

    // Guard against invalid image index (matches ComputeDispatchNode)
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[TraceRaysNode] Invalid image index - skipping frame");
        return;
    }

    // Get command buffer for this IMAGE (not frame) - matches ComputeDispatchNode pattern
    VkCommandBuffer cmdBuffer = commandBuffers_[imageIndex];

    if (width_ == 0 || height_ == 0 || depth_ == 0)
        throw std::runtime_error("TraceRaysNode: launch dimensions must be positive");
    const uint64_t budgetNs = GetOwningGraph()->GetCapacityTracker().GetConfig().gpuTimeBudgetNs;
    const uint32_t edge = std::max(1u, static_cast<uint32_t>(64 * std::sqrt(
        std::min(1.0, budgetNs / 16'666'666.0))));
    const uint64_t planeTiles = uint64_t(1 + (width_ - 1) / edge) * (1 + (height_ - 1) / edge);
    if (planeTiles > std::numeric_limits<uint32_t>::max() / 4u / depth_)
        throw std::runtime_error("TraceRaysNode: too many timestamped launch tiles");
    const uint32_t count = static_cast<uint32_t>(planeTiles * depth_);
    auto& extra = tileCommandBuffers_[imageIndex];
    if (extra.size() < count - 1u) {
        std::vector<VkCommandBuffer> allocated(count - 1u - extra.size());
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<uint32_t>(allocated.size());
        if (vkAllocateCommandBuffers(device, &allocation, allocated.data()) != VK_SUCCESS)
            throw std::runtime_error("TraceRaysNode: tile command allocation failed");
        extra.insert(extra.end(), allocated.begin(), allocated.end());
    }
    std::vector<VkCommandBuffer> recorded{cmdBuffer};
    recorded.insert(recorded.end(), extra.begin(), extra.begin() + (count - 1u));
    if (tileCounts_[currentFrame] != count) {
        tileQueries_[currentFrame] = std::make_unique<Vixen::Vulkan::Resources::GPUTimestampQuery>(
            vulkanDevice_, 1, count * 4);
        tileCounts_[currentFrame] = count;
    }
    tileBudgets_[currentFrame] = budgetNs;
    auto& query = *tileQueries_[currentFrame];
    pushConstData.resize(pipelineData_->pushConstantSize, 0);
    const std::array<uint32_t, 3> fullExtent{width_, height_, depth_};
    std::memcpy(pushConstData.data() + fullLaunchExtentOffset_, fullExtent.data(), sizeof(fullExtent));

    // Begin command buffer (matches ComputeDispatchNode pattern - no special flags)
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;  // Match ComputeDispatchNode - no ONE_TIME_SUBMIT

    VkResult beginResult = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS) {
        throw std::runtime_error("[TraceRaysNode::ExecuteImpl] Failed to begin command buffer: " + std::to_string(beginResult));
    }

    query.ResetQueries(cmdBuffer, 0);
    query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

    // Get output image for layout transitions
    VkImage outputImage = swapchainInfo->GetImage(imageIndex);

    // Transition image to GENERAL for ray tracing output
    // After present, swapchain images are in PRESENT_SRC_KHR layout.
    // We use UNDEFINED to discard previous contents (works for any source layout).
    // Note: First frame image might be in UNDEFINED, but UNDEFINED->GENERAL is valid.
    // If validation complains about layout mismatch, the real issue is elsewhere.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = swapchainInfo->GetImageLayout(imageIndex);
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outputImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Bind ray tracing pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineData_->pipeline);

    // Bind descriptor set from graph (DescriptorSetNode handles creation/updates)
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipelineData_->pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    // Push constants - get stage flags from pipeline data (set by RayTracingPipelineNode)
    if (!pushConstData.empty() && pipelineData_->pushConstantStages != 0) {
        vkCmdPushConstants(
            cmdBuffer,
            pipelineData_->pipelineLayout,
            pipelineData_->pushConstantStages,
            pipelineData_->pushConstantOffset,
            static_cast<uint32_t>(pushConstData.size()),
            pushConstData.data()
        );
    }

    // Setup SBT regions
    VkStridedDeviceAddressRegionKHR raygenRegion{};
    raygenRegion.deviceAddress = pipelineData_->sbt.raygenRegion.deviceAddress;
    raygenRegion.stride = pipelineData_->sbt.raygenRegion.stride;
    raygenRegion.size = pipelineData_->sbt.raygenRegion.size;

    VkStridedDeviceAddressRegionKHR missRegion{};
    missRegion.deviceAddress = pipelineData_->sbt.missRegion.deviceAddress;
    missRegion.stride = pipelineData_->sbt.missRegion.stride;
    missRegion.size = pipelineData_->sbt.missRegion.size;

    VkStridedDeviceAddressRegionKHR hitRegion{};
    hitRegion.deviceAddress = pipelineData_->sbt.hitRegion.deviceAddress;
    hitRegion.stride = pipelineData_->sbt.hitRegion.stride;
    hitRegion.size = pipelineData_->sbt.hitRegion.size;

    VkStridedDeviceAddressRegionKHR callableRegion{};

    // Begin GPU timing frame (reset queries for this frame) - CRITICAL for timing to work
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmdBuffer, currentFrame);
    }

    // Record GPU timing start
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmdBuffer, currentFrame);
    }

    uint32_t tile = 0;
    for (uint32_t z = 0; z < depth_; ++z) {
        for (uint32_t y = 0; y < height_; y += edge) {
            for (uint32_t x = 0; x < width_; x += edge) {
                const std::array<uint32_t, 3> origin{x, y, z};
                std::memcpy(pushConstData.data() + tileOriginOffset_, origin.data(), sizeof(origin));
                vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineData_->pipeline);
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    pipelineData_->pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdPushConstants(cmdBuffer, pipelineData_->pipelineLayout,
                    pipelineData_->pushConstantStages, pipelineData_->pushConstantOffset,
                    static_cast<uint32_t>(pushConstData.size()), pushConstData.data());
                query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, tile * 4 + 1);
                vkCmdTraceRaysKHR_(cmdBuffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion,
                    std::min(edge, width_ - x), std::min(edge, height_ - y), 1);
                query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, tile * 4 + 2);
                if (++tile != count) {
                    query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tile * 4 - 1);
                    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS)
                        throw std::runtime_error("TraceRaysNode: ending tile commands failed");
                    cmdBuffer = recorded[tile];
                    if (vkBeginCommandBuffer(cmdBuffer, &beginInfo) != VK_SUCCESS)
                        throw std::runtime_error("TraceRaysNode: beginning tile commands failed");
                    query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tile * 4);
                }
            }
        }
    }

    // Record GPU timing end
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(cmdBuffer, currentFrame, width_, height_);
    }

    // Transition image to PRESENT_SRC
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = swapchainInfo->UsesWsiSynchronization()
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    swapchainInfo->SetImageLayout(imageIndex, barrier.newLayout);
    query.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, count * 4 - 1);
    VkResult endResult = vkEndCommandBuffer(cmdBuffer);
    if (endResult != VK_SUCCESS) {
        throw std::runtime_error("[TraceRaysNode::ExecuteImpl] Failed to end command buffer: " + std::to_string(endResult));
    }

    // Existing typed submit channel consumes acquire once and signals completion once.
    std::vector<VkCommandBufferSubmitInfo> commands;
    for (const auto buffer : recorded) {
        VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        info.commandBuffer = buffer;
        commands.push_back(info);
    }
    VkSemaphoreSubmitInfo acquire{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    acquire.semaphore = handoffs.acquire;
    acquire.stageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    VkSemaphoreSubmitInfo present{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    present.semaphore = handoffs.present;
    present.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    info.commandBufferInfoCount = static_cast<uint32_t>(commands.size());
    info.pCommandBufferInfos = commands.data();
    info.waitSemaphoreInfoCount = handoffs.acquire != VK_NULL_HANDLE ? 1u : 0u;
    info.pWaitSemaphoreInfos = &acquire;
    info.signalSemaphoreInfoCount = handoffs.present != VK_NULL_HANDLE ? 1u : 0u;
    info.pSignalSemaphoreInfos = &present;
    VkResult result = VK_SUCCESS;
    if (!GetOwningGraph()->PublishSubmit(this, SubmitRecord::FromSubmitInfo2(info, inFlightFence))) {
        std::lock_guard submitLock(vulkanDevice_->SubmitMutex(vulkanDevice_->queue));
        result = SubmitRecord::FromSubmitInfo2(info, inFlightFence).Submit(
            vulkanDevice_->fpQueueSubmit2, vulkanDevice_->queue);
    }

    if (result != VK_SUCCESS) {
        // Matches ComputeDispatchNode: a swallowed submit failure here previously fell through to
        // ctx.Out() below, handing Present a semaphore that will never be signalled, and a
        // VK_ERROR_DEVICE_LOST on the RTX path never reached recovery (audit V-N8).
        if (result == VK_ERROR_DEVICE_LOST) {
            GetOwningGraph()->NotifyDeviceLost("TraceRaysNode::ExecuteImpl vkQueueSubmit");
        }
        throw std::runtime_error("[TraceRaysNode::ExecuteImpl] Failed to submit command buffer (vkQueueSubmit): " + std::to_string(result));
    }

    // Output command buffer and semaphore
    ctx.Out(TraceRaysNodeConfig::COMMAND_BUFFER, recorded.front());
    ctx.Out(TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORE, handoffs.present);
}

void TraceRaysNode::CollectTileTiming(uint32_t frame) {
    auto& query = tileQueries_[frame];
    if (!query || !query->ReadResults(0)) return;
    double total = 0, maximum = 0;
    for (uint32_t i = 0; i < tileCounts_[frame]; ++i) {
        const double ms = query->GetElapsedMs(0, i * 4, i * 4 + 3);
        total += ms;
        maximum = std::max(maximum, ms);
        if (std::getenv("VIXEN_GPUYIELD_TRACE")) {
            NODE_LOG_INFO("[GPUYIELD_CHUNK] node=" + GetInstanceName() +
                " flight=" + std::to_string(frame) + " chunk=" + std::to_string(i) +
                " start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4)) +
                " end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 3)) +
                " dispatch_start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 1)) +
                " dispatch_end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 2)) +
                " dispatch_gpu_ms=" + std::to_string(query->GetElapsedMs(0, i * 4 + 1, i * 4 + 2)) +
                " elapsed_gpu_ms=" + std::to_string(ms));
        }
    }
    NODE_LOG_INFO("[GPUYIELD] node=" + GetInstanceName() + " submissions=" +
        std::to_string(tileCounts_[frame]) + " total_gpu_ms=" + std::to_string(total) +
        " max_submission_ms=" + std::to_string(maximum) +
        " budget_ms=" + std::to_string(tileBudgets_[frame] / 1.0e6));
}

void TraceRaysNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("TraceRaysNode cleanup");

    // Release GPU resources (QueryPools) while device is still valid.
    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor
    // Logger object stays alive for parent log extraction.

    if (vulkanDevice_ && vulkanDevice_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkanDevice_->device);
        for (uint32_t frame = 0; frame < tileQueries_.size(); ++frame) CollectTileTiming(frame);
    }
    tileQueries_.clear();
    tileCounts_.clear();
    tileBudgets_.clear();
    DestroyResources();
}

// ============================================================================
// RTX FUNCTION LOADING
// ============================================================================

bool TraceRaysNode::LoadRTXFunctions() {
    VkDevice device = vulkanDevice_->device;

    vkCmdTraceRaysKHR_ =
        (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");

    if (!vkCmdTraceRaysKHR_) {
        NODE_LOG_ERROR("Failed to load vkCmdTraceRaysKHR");
        return false;
    }

    return true;
}

// ============================================================================
// DESCRIPTOR RESOURCES - REMOVED
// ============================================================================
// NOTE: Descriptor pool/set creation is now handled by DescriptorSetNode.
// TraceRaysNode receives descriptor sets via DESCRIPTOR_SETS input slot.
// This follows the same pattern as ComputeDispatchNode for consistency.

bool TraceRaysNode::AllocateCommandBuffers() {
    VkDevice device = vulkanDevice_->device;

    // Allocate one command buffer per swapchain image (matches ComputeDispatchNode pattern)
    // swapChainImageCount_ is set during CompileImpl from SWAPCHAIN_INFO
    commandBuffers_.resize(swapChainImageCount_);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = swapChainImageCount_;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to allocate command buffers");
        return false;
    }

    NODE_LOG_INFO("Allocated " + std::to_string(swapChainImageCount_) + " command buffers (one per swapchain image)");
    return true;
}

void TraceRaysNode::DestroyResources() {
    if (!vulkanDevice_) {
        return;
    }

    VkDevice device = vulkanDevice_->device;

    // Free command buffers
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    for (auto& extra : tileCommandBuffers_) {
        if (!extra.empty() && commandPool_ != VK_NULL_HANDLE)
            vkFreeCommandBuffers(device, commandPool_, static_cast<uint32_t>(extra.size()), extra.data());
    }
    tileCommandBuffers_.clear();

    // NOTE: Descriptor pool/set cleanup is handled by DescriptorSetNode
    // TraceRaysNode no longer owns descriptor resources

    NODE_LOG_INFO("Destroyed TraceRaysNode resources");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::TraceRaysNodeType);
