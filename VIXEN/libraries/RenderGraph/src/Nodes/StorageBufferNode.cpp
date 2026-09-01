// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync P4 M4: generic arbitrary-sized storage buffer (SSBO) node.

#include "Nodes/StorageBufferNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include "IRenderTarget.h"   // Vixen::Vulkan::Resources::IRenderTarget (extent-driven sizing)
#include <cstring>
#include <vector>
#include <stdexcept>

// Local helper — same selection logic as InstanceBufferNode's FindSuitableMemoryType,
// but a distinct symbol name to avoid inline COMDAT conflicts across TUs.
static uint32_t StorageBuffer_FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& memProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags required,
    const char* context)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error(
        std::string("[StorageBufferNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== StorageBufferNodeType ======

std::unique_ptr<NodeInstance> StorageBufferNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<StorageBufferNode>(n, const_cast<StorageBufferNodeType*>(this));
}

// ====== StorageBufferNode ======

StorageBufferNode::StorageBufferNode(const std::string& n, NodeType* t)
    : TypedNode<StorageBufferNodeConfig>(n, t)
{
}

void StorageBufferNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[StorageBufferNode] Setup (graph-scope initialization)");
}

void StorageBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[StorageBufferNode] Compile START");

    SetDevice(ctx.In(StorageBufferNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[StorageBufferNode] VULKAN_DEVICE_IN is null");
    }

    // Resolve requested size at compile time, in priority order:
    //   1. SWAPCHAIN_INFO connected -> extent.w * extent.h * bytesPerPixel (resize cascade)
    //   2. PARAM_SIZE_BYTES
    //   3. PARAM_ELEMENT_COUNT * PARAM_ELEMENT_STRIDE
    uint32_t sizeBytesParam     = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_SIZE_BYTES, 0u);
    uint32_t elementCountParam  = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_ELEMENT_COUNT, 0u);
    uint32_t elementStrideParam = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_ELEMENT_STRIDE, 0u);
    uint32_t bytesPerPixelParam = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 0u);
    uint32_t frameRingSizeParam  = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_FRAME_RING_SIZE, 0u);

    VkDeviceSize requested = 0;

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo =
        ctx.In(StorageBufferNodeConfig::SWAPCHAIN_INFO);
    if (swapchainInfo && bytesPerPixelParam != 0u) {
        VkExtent2D extent = swapchainInfo->GetExtent();
        requested = static_cast<VkDeviceSize>(extent.width) *
                    static_cast<VkDeviceSize>(extent.height) *
                    static_cast<VkDeviceSize>(bytesPerPixelParam);
        NODE_LOG_INFO("[StorageBufferNode] Extent-driven size: " +
                      std::to_string(extent.width) + "x" + std::to_string(extent.height) +
                      " * " + std::to_string(bytesPerPixelParam) + " bpp");
    } else if (sizeBytesParam != 0u) {
        requested = static_cast<VkDeviceSize>(sizeBytesParam);
    } else {
        requested = static_cast<VkDeviceSize>(elementCountParam) *
                    static_cast<VkDeviceSize>(elementStrideParam);
    }

    if (requested == 0) {
        throw std::runtime_error("[StorageBufferNode] requested size is 0 — connect SWAPCHAIN_INFO "
                                 "(+bytesPerPixel), or set sizeBytes, or elementCount*elementStride");
    }

    if (frameRingSizeParam != 0u) {
        // Row A: persistent per-flight storage buffers. The graph's FrameSync fence owns the
        // selected slot, so PreTick can write it without map/unmap churn or a device-wide wait.
        const bool ringShapeChanged = !perFrame_.IsInitialized() ||
                                       perFrame_.GetFrameCount() != frameRingSizeParam;
        const bool ringNeedsGrowth = perFrame_.IsInitialized() &&
                                      perFrame_.GetFrameData(0).uniformBufferSize < requested;
        if (buffer_ != VK_NULL_HANDLE) {
            DestroyBuffer();
        }
        if (ringShapeChanged || ringNeedsGrowth) {
            if (perFrame_.IsInitialized()) {
                perFrame_.Cleanup();
            }
            perFrame_.Initialize(GetDevice(), frameRingSizeParam);
            for (uint32_t i = 0; i < frameRingSizeParam; ++i) {
                perFrame_.CreateStorageBuffer(i, requested);
                std::memset(perFrame_.GetUniformBufferMapped(i), 0, static_cast<size_t>(requested));
            }
            NODE_LOG_INFO("[StorageBufferNode] Allocated persistent frame ring of " +
                          std::to_string(frameRingSizeParam) + " storage buffers (" +
                          std::to_string(static_cast<uint64_t>(requested)) + " bytes each)");
        } else {
            NODE_LOG_INFO("[StorageBufferNode] Reusing persistent frame ring (" +
                          std::to_string(frameRingSizeParam) + " slots, " +
                          std::to_string(static_cast<uint64_t>(perFrame_.GetFrameData(0).uniformBufferSize)) +
                          " bytes each)");
        }
        sizeBytes_ = perFrame_.GetFrameData(0).uniformBufferSize;
    } else {
        // Original single-buffer behavior remains unchanged for every non-ring instance.
        if (perFrame_.IsInitialized()) {
            perFrame_.Cleanup();
        }
        if (buffer_ == VK_NULL_HANDLE || requested > sizeBytes_) {
            if (buffer_ != VK_NULL_HANDLE) {
                NODE_LOG_INFO("[StorageBufferNode] Growing storage buffer from " +
                              std::to_string(static_cast<uint64_t>(sizeBytes_)) + " to " +
                              std::to_string(static_cast<uint64_t>(requested)) + " bytes");
                DestroyBuffer();
            }
            CreateBuffer(GetDevice(), requested);
        } else {
            NODE_LOG_INFO("[StorageBufferNode] Reusing storage buffer (" +
                          std::to_string(static_cast<uint64_t>(sizeBytes_)) + " bytes >= requested " +
                          std::to_string(static_cast<uint64_t>(requested)) + ")");
        }
    }

    ctx.Out(StorageBufferNodeConfig::STORAGE_BUFFER,
            perFrame_.IsInitialized() ? perFrame_.GetUniformBuffer(0) : buffer_);
    ctx.Out(StorageBufferNodeConfig::BUFFER_SIZE, static_cast<uint32_t>(sizeBytes_));
    ctx.Out(StorageBufferNodeConfig::FRAME_STORAGE_BUFFER,
            perFrame_.IsInitialized() ? perFrame_.GetUniformBuffer(0) : buffer_);

    NODE_LOG_INFO("[StorageBufferNode] Outputs published (size=" +
                  std::to_string(static_cast<uint64_t>(sizeBytes_)) + " bytes)");
}

void StorageBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frameRingSize = GetParameterValue<uint32_t>(
        StorageBufferNodeConfig::PARAM_FRAME_RING_SIZE, 0u);
    if (frameRingSize != 0u && perFrame_.IsInitialized()) {
        const uint32_t frameIndex = ctx.In(StorageBufferNodeConfig::CURRENT_FRAME_INDEX) % frameRingSize;
        ctx.Out(StorageBufferNodeConfig::FRAME_STORAGE_BUFFER,
                perFrame_.GetUniformBuffer(frameIndex));
    } else {
        // Static buffer — produced/zeroed at compile time. Keep the additive frame output valid
        // for callers that inspect the generic node without enabling the ring.
        ctx.Out(StorageBufferNodeConfig::FRAME_STORAGE_BUFFER, buffer_);
    }
}

void StorageBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[StorageBufferNode] Cleanup (recompile) - keeping persistent storage buffer");
        return;
    }

    NODE_LOG_INFO("[StorageBufferNode] Cleanup (final teardown) - destroying storage buffer");
    perFrame_.Cleanup();
    DestroyBuffer();
}

void* StorageBufferNode::MapForReadback(VulkanDevice* device) const {
    if (memory_ == VK_NULL_HANDLE || !device) return nullptr;
    void* mapped = nullptr;
    if (vkMapMemory(device->device, memory_, 0, sizeBytes_, 0, &mapped) != VK_SUCCESS) {
        return nullptr;
    }
    return mapped;
}

void StorageBufferNode::UnmapReadback(VulkanDevice* device) const {
    if (memory_ == VK_NULL_HANDLE || !device) return;
    vkUnmapMemory(device->device, memory_);
}

void* StorageBufferNode::MapCurrentForWrite(uint32_t frameIndex) const {
    if (!perFrame_.IsInitialized()) return nullptr;
    return perFrame_.GetUniformBufferMapped(frameIndex % perFrame_.GetFrameCount());
}

VkBuffer StorageBufferNode::GetBufferHandle(uint32_t frameIndex) const {
    if (perFrame_.IsInitialized()) {
        return perFrame_.GetUniformBuffer(frameIndex % perFrame_.GetFrameCount());
    }
    return buffer_;
}

void StorageBufferNode::CreateBuffer(VulkanDevice* device, VkDeviceSize sizeBytes) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    // --- Create the storage buffer ---
    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: additive extra usage bits (e.g.
    // VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT), default 0 -- every pre-M3 consumer gets the exact
    // same STORAGE_BUFFER_BIT-only buffer as before.
    const uint32_t extraUsageFlags = GetParameterValue<uint32_t>(StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS, 0u);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = sizeBytes;
    bufferInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | static_cast<VkBufferUsageFlags>(extraUsageFlags);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("[StorageBufferNode] vkCreateBuffer failed");
    }

    // --- Allocate host-visible / host-coherent memory via real memory-type selection ---
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, buffer_, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = StorageBuffer_FindMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "StorageBufferNode SSBO"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[StorageBufferNode] vkAllocateMemory failed");
    }

    if (vkBindBufferMemory(vkDevice, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory_, nullptr);
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        memory_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[StorageBufferNode] vkBindBufferMemory failed");
    }

    // --- Zero-initialise the buffer ---
    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, memory_, 0, sizeBytes, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("[StorageBufferNode] vkMapMemory failed");
    }
    std::memset(mapped, 0, static_cast<size_t>(sizeBytes));
    vkUnmapMemory(vkDevice, memory_);

    sizeBytes_ = sizeBytes;

    NODE_LOG_INFO("[StorageBufferNode] Created storage SSBO (" +
                  std::to_string(static_cast<uint64_t>(sizeBytes)) + " bytes, zero-initialised)");
}

void StorageBufferNode::DestroyBuffer() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory   (vkDevice, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
    sizeBytes_ = 0;

    NODE_LOG_INFO("[StorageBufferNode] Storage buffer destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::StorageBufferNodeType);
