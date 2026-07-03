#include "Nodes/InstanceBufferNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <vector>
#include <stdexcept>

// Local helper — mirrors RenderGraph::NodeHelpers::FindMemoryType from BufferHelpers.h
// but defined here to avoid emitting inline-function COMDAT into this TU that conflicts
// when the same inline is instantiated in other TUs with different COMDAT selection.
static uint32_t FindSuitableMemoryType(
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
        std::string("[InstanceBufferNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== InstanceBufferNodeType ======

std::unique_ptr<NodeInstance> InstanceBufferNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<InstanceBufferNode>(n, const_cast<InstanceBufferNodeType*>(this));
}

// ====== InstanceBufferNode ======

InstanceBufferNode::InstanceBufferNode(const std::string& n, NodeType* t)
    : TypedNode<InstanceBufferNodeConfig>(n, t)
{
}

void InstanceBufferNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access).
    NODE_LOG_DEBUG("[InstanceBufferNode] Setup (graph-scope initialization)");
}

void InstanceBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[InstanceBufferNode] Compile START");

    SetDevice(ctx.In(InstanceBufferNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[InstanceBufferNode] VULKAN_DEVICE_IN is null");
    }

    gridDim_ = GetParameterValue<uint32_t>(InstanceBufferNodeConfig::PARAM_GRID_DIM, 8u);
    spacing_ = GetParameterValue<float>(InstanceBufferNodeConfig::PARAM_SPACING, 2.0f);
    if (gridDim_ == 0) {
        throw std::runtime_error("[InstanceBufferNode] gridDim must be > 0");
    }
    instanceCount_ = gridDim_ * gridDim_;

    // FR-7: the buffer is persistent across recompile — only create once.
    if (buffer_ == VK_NULL_HANDLE) {
        CreateBuffer(GetDevice());
    } else {
        NODE_LOG_INFO("[InstanceBufferNode] Reusing persistent instance buffer across recompile");
    }

    ctx.Out(InstanceBufferNodeConfig::INSTANCE_BUFFER, buffer_);
    ctx.Out(InstanceBufferNodeConfig::INSTANCE_COUNT, instanceCount_);

    NODE_LOG_INFO("[InstanceBufferNode] Outputs published (gridDim=" + std::to_string(gridDim_) +
                  ", instances=" + std::to_string(instanceCount_) + ")");
}

void InstanceBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Static buffer — filled at compile time. Nothing to do per-frame.
}

void InstanceBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    // FR-7: persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[InstanceBufferNode] Cleanup (recompile) - keeping persistent instance buffer");
        return;
    }

    NODE_LOG_INFO("[InstanceBufferNode] Cleanup (final teardown) - destroying instance buffer");
    DestroyBuffer();
}

void InstanceBufferNode::CreateBuffer(VulkanDevice* device) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    const VkDeviceSize bufferSize =
        static_cast<VkDeviceSize>(instanceCount_) * sizeof(glm::mat4);

    // --- Create the storage buffer ---
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = bufferSize;
    bufferInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("[InstanceBufferNode] vkCreateBuffer failed");
    }

    // --- Allocate host-visible / host-coherent memory via real memory-type selection ---
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vkDevice, buffer_, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = req.size;
    allocInfo.memoryTypeIndex = FindSuitableMemoryType(
        memProps,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "InstanceBufferNode instance SSBO"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[InstanceBufferNode] vkAllocateMemory failed");
    }

    if (vkBindBufferMemory(vkDevice, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory_, nullptr);
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        memory_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[InstanceBufferNode] vkBindBufferMemory failed");
    }

    // --- Fill the gridDim x gridDim planar grid of translation matrices ---
    std::vector<glm::mat4> transforms;
    transforms.reserve(instanceCount_);
    const float half = gridDim_ / 2.0f;
    for (uint32_t y = 0; y < gridDim_; ++y) {
        for (uint32_t x = 0; x < gridDim_; ++x) {
            const glm::vec3 pos(
                (static_cast<float>(x) - half) * spacing_,
                (static_cast<float>(y) - half) * spacing_,
                0.0f);
            transforms.push_back(glm::translate(glm::mat4(1.0f), pos));
        }
    }

    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, memory_, 0, bufferSize, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("[InstanceBufferNode] vkMapMemory failed");
    }
    std::memcpy(mapped, transforms.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(vkDevice, memory_);

    NODE_LOG_INFO("[InstanceBufferNode] Created instance SSBO with " +
                  std::to_string(instanceCount_) + " transforms (" +
                  std::to_string(static_cast<uint64_t>(bufferSize)) + " bytes)");
}

void InstanceBufferNode::DestroyBuffer() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory   (vkDevice, memory_, nullptr); memory_ = VK_NULL_HANDLE; }

    NODE_LOG_INFO("[InstanceBufferNode] Instance buffer destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::InstanceBufferNodeType);
