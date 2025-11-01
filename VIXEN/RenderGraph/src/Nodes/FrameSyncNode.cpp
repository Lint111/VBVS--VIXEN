#include "Nodes/FrameSyncNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// ====== FrameSyncNodeType ======

FrameSyncNodeType::FrameSyncNodeType(const std::string& typeName)
    : NodeType(typeName)
{
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = false;  // Only one frame sync manager per device
    maxInstances = 1;

    // Populate schemas from Config
    FrameSyncNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 512;  // Small - just sync primitives
    workloadMetrics.estimatedComputeCost = 0.1f;     // Very cheap to create
    workloadMetrics.estimatedBandwidthCost = 0.0f;   // No bandwidth
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> FrameSyncNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FrameSyncNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== FrameSyncNode ======

FrameSyncNode::FrameSyncNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FrameSyncNodeConfig>(instanceName, nodeType)
{
}

FrameSyncNode::~FrameSyncNode() {
    Cleanup();
}

void FrameSyncNode::Setup() {
    VulkanDevicePtr devicePtr = In(FrameSyncNodeConfig::VULKAN_DEVICE);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FrameSyncNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);
}

void FrameSyncNode::Compile() {
    NODE_LOG_INFO("Creating frame-in-flight synchronization primitives (MAX_FRAMES_IN_FLIGHT="
                  + std::to_string(FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT) + ")");

    // Resize vector to hold MAX_FRAMES_IN_FLIGHT entries
    frameSyncData.resize(FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);

    // Create fences and semaphores for each frame in flight
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT; i++) {
        // Create fence
        if (vkCreateFence(device->device, &fenceInfo, nullptr, &frameSyncData[i].inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence for frame " + std::to_string(i));
        }

        // Create image available semaphore
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &frameSyncData[i].imageAvailable) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imageAvailable semaphore for frame " + std::to_string(i));
        }

        // Create render complete semaphore
        if (vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &frameSyncData[i].renderComplete) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create renderComplete semaphore for frame " + std::to_string(i));
        }

        NODE_LOG_INFO("Frame " + std::to_string(i) + ": fence=0x"
                      + std::to_string(reinterpret_cast<uint64_t>(frameSyncData[i].inFlightFence)));
    }

    isCreated = true;

    // Initialize current frame index to 0
    currentFrameIndex = 0;

    // Set initial outputs (frame 0)
    Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData[currentFrameIndex].inFlightFence);
    Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORE, frameSyncData[currentFrameIndex].imageAvailable);
    Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORE, frameSyncData[currentFrameIndex].renderComplete);

    NODE_LOG_INFO("Frame synchronization primitives created successfully");

    // Register cleanup with automatic device dependency resolution
    RegisterCleanup();
}

void FrameSyncNode::Execute(VkCommandBuffer commandBuffer) {
    // Phase 0.2: Advance frame index and update outputs
    // NOTE: Fence waiting/resetting happens in GeometryRenderNode before work submission

    // Advance to next frame in flight (ring buffer)
    currentFrameIndex = (currentFrameIndex + 1) % FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

    // Update outputs with current frame's sync primitives
    Out(FrameSyncNodeConfig::CURRENT_FRAME_INDEX, currentFrameIndex);
    Out(FrameSyncNodeConfig::IN_FLIGHT_FENCE, frameSyncData[currentFrameIndex].inFlightFence);
    Out(FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORE, frameSyncData[currentFrameIndex].imageAvailable);
    Out(FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORE, frameSyncData[currentFrameIndex].renderComplete);
}

void FrameSyncNode::CleanupImpl() {
    if (isCreated && device != nullptr && device->device != VK_NULL_HANDLE) {
        NODE_LOG_INFO("Destroying frame synchronization primitives");

        for (auto& sync : frameSyncData) {
            if (sync.inFlightFence != VK_NULL_HANDLE) {
                vkDestroyFence(device->device, sync.inFlightFence, nullptr);
                sync.inFlightFence = VK_NULL_HANDLE;
            }
            if (sync.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(device->device, sync.imageAvailable, nullptr);
                sync.imageAvailable = VK_NULL_HANDLE;
            }
            if (sync.renderComplete != VK_NULL_HANDLE) {
                vkDestroySemaphore(device->device, sync.renderComplete, nullptr);
                sync.renderComplete = VK_NULL_HANDLE;
            }
        }

        frameSyncData.clear();
        currentFrameIndex = 0;
        isCreated = false;

        NODE_LOG_INFO("Frame synchronization primitives destroyed");
    }
}

} // namespace Vixen::RenderGraph