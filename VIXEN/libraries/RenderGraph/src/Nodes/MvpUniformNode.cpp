#include "Nodes/MvpUniformNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
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
        std::string("[MvpUniformNode] No suitable memory type found for ") + context);
}

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ====== MvpUniformNodeType ======

std::unique_ptr<NodeInstance> MvpUniformNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<MvpUniformNode>(n, const_cast<MvpUniformNodeType*>(this));
}

// ====== MvpUniformNode ======

MvpUniformNode::MvpUniformNode(const std::string& n, NodeType* t)
    : TypedNode<MvpUniformNodeConfig>(n, t)
{
}

void MvpUniformNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access).
    NODE_LOG_DEBUG("[MvpUniformNode] Setup (graph-scope initialization)");
}

void MvpUniformNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[MvpUniformNode] Compile START");

    SetDevice(ctx.In(MvpUniformNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[MvpUniformNode] VULKAN_DEVICE_IN is null");
    }

    fovDegrees_     = GetParameterValue<float>(MvpUniformNodeConfig::PARAM_FOV_DEGREES, 50.0f);
    aspect_         = GetParameterValue<float>(MvpUniformNodeConfig::PARAM_ASPECT, 1.7777778f);
    nearZ_          = GetParameterValue<float>(MvpUniformNodeConfig::PARAM_NEAR, 0.1f);
    farZ_           = GetParameterValue<float>(MvpUniformNodeConfig::PARAM_FAR, 200.0f);
    cameraDistance_ = GetParameterValue<float>(MvpUniformNodeConfig::PARAM_CAMERA_DISTANCE, 45.0f);

    // FR-7: the buffer is persistent across recompile — only create once.
    if (buffer_ == VK_NULL_HANDLE) {
        CreateBuffer(GetDevice());
    } else {
        NODE_LOG_INFO("[MvpUniformNode] Reusing persistent MVP buffer across recompile");
    }

    ctx.Out(MvpUniformNodeConfig::MVP_BUFFER, buffer_);

    NODE_LOG_INFO("[MvpUniformNode] Output published (fov=" + std::to_string(fovDegrees_) +
                  ", aspect=" + std::to_string(aspect_) +
                  ", cameraDistance=" + std::to_string(cameraDistance_) + ")");
}

void MvpUniformNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Static UBO — filled at compile time. Nothing to do per-frame.
}

void MvpUniformNode::CleanupImpl(TypedCleanupContext& ctx) {
    // FR-7: persist across recompile; release only on final application teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
        NODE_LOG_INFO("[MvpUniformNode] Cleanup (recompile) - keeping persistent MVP buffer");
        return;
    }

    NODE_LOG_INFO("[MvpUniformNode] Cleanup (final teardown) - destroying MVP buffer");
    DestroyBuffer();
}

void MvpUniformNode::CreateBuffer(VulkanDevice* device) {
    VkDevice         vkDevice = device->device;
    VkPhysicalDevice physDev  = *device->gpu;

    const VkDeviceSize bufferSize = sizeof(glm::mat4);  // 64 bytes

    // --- Create the uniform buffer ---
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = bufferSize;
    bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("[MvpUniformNode] vkCreateBuffer failed");
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
        "MvpUniformNode MVP UBO"
    );

    if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[MvpUniformNode] vkAllocateMemory failed");
    }

    if (vkBindBufferMemory(vkDevice, buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory_, nullptr);
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        memory_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("[MvpUniformNode] vkBindBufferMemory failed");
    }

    // --- Compute mvp = proj * view ---
    // Model is per-instance in the shader; Draw.vert applies the Vulkan Y-flip + Z remap.
    glm::mat4 proj = glm::perspective(glm::radians(fovDegrees_), aspect_, nearZ_, farZ_);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cameraDistance_));
    glm::mat4 mvp  = proj * view;

    void* mapped = nullptr;
    if (vkMapMemory(vkDevice, memory_, 0, bufferSize, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("[MvpUniformNode] vkMapMemory failed");
    }
    std::memcpy(mapped, &mvp, sizeof(glm::mat4));
    vkUnmapMemory(vkDevice, memory_);

    NODE_LOG_INFO("[MvpUniformNode] Created MVP UBO (" +
                  std::to_string(static_cast<uint64_t>(bufferSize)) + " bytes)");
}

void MvpUniformNode::DestroyBuffer() {
    if (!GetDevice()) return;
    VkDevice vkDevice = GetDevice()->device;

    if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(vkDevice, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE) { vkFreeMemory   (vkDevice, memory_, nullptr); memory_ = VK_NULL_HANDLE; }

    NODE_LOG_INFO("[MvpUniformNode] MVP buffer destroyed");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::MvpUniformNodeType);
