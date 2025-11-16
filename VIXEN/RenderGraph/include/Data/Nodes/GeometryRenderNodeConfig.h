#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for GeometryRenderNode
 *
 * Inputs:
 * - RENDER_PASS (VkRenderPass) - Render pass from RenderPassNode
 * - FRAMEBUFFERS (VkFramebuffer[]) - Framebuffers from FramebufferNode (array)
 * - PIPELINE (VkPipeline) - Graphics pipeline from GraphicsPipelineNode
 * - PIPELINE_LAYOUT (VkPipelineLayout) - Pipeline layout from GraphicsPipelineNode
 * - DESCRIPTOR_SETS (VkDescriptorSet[]) - Descriptor sets from DescriptorSetNode (array)
 * - VERTEX_BUFFER (VkBuffer) - Vertex buffer from VertexBufferNode
 * - INDEX_BUFFER (VkBuffer) - Index buffer from VertexBufferNode (nullable)
 * - VIEWPORT (VkViewport*) - Viewport configuration
 * - SCISSOR (VkRect2D*) - Scissor rectangle
 * - RENDER_WIDTH (uint32_t) - Render area width
 * - RENDER_HEIGHT (uint32_t) - Render area height
 *
 * Outputs:
 * - COMMAND_BUFFERS (VkCommandBuffer[]) - Recorded command buffers (array output)
 *
 * Parameters:
 * - VERTEX_COUNT (uint32_t) - Number of vertices to draw
 * - INSTANCE_COUNT (uint32_t) - Number of instances (default: 1)
 * - FIRST_VERTEX (uint32_t) - First vertex index (default: 0)
 * - FIRST_INSTANCE (uint32_t) - First instance index (default: 0)
 * - USE_INDEX_BUFFER (bool) - Whether to use indexed rendering (default: false)
 * - INDEX_COUNT (uint32_t) - Number of indices (if using index buffer)
 * - CLEAR_COLOR_R/G/B/A (float) - Clear color values (default: 0,0,0,1)
 * - CLEAR_DEPTH (float) - Clear depth value (default: 1.0)
 * - CLEAR_STENCIL (uint32_t) - Clear stencil value (default: 0)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace GeometryRenderNodeCounts {
    static constexpr size_t INPUTS = 15;  // Phase 0.5: Added CURRENT_FRAME_INDEX for semaphore array indexing
    static constexpr size_t OUTPUTS = 2;  // COMMAND_BUFFERS, RENDER_COMPLETE_SEMAPHORE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array; // Framebuffers + descriptor sets are arrays
}

CONSTEXPR_NODE_CONFIG(GeometryRenderNodeConfig, 
                      GeometryRenderNodeCounts::INPUTS, 
                      GeometryRenderNodeCounts::OUTPUTS, 
                      GeometryRenderNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* VERTEX_COUNT = "vertexCount";
    static constexpr const char* INSTANCE_COUNT = "instanceCount";
    static constexpr const char* FIRST_VERTEX = "firstVertex";
    static constexpr const char* FIRST_INSTANCE = "firstInstance";
    static constexpr const char* USE_INDEX_BUFFER = "useIndexBuffer";
    static constexpr const char* INDEX_COUNT = "indexCount";
    static constexpr const char* CLEAR_COLOR_R = "clearColorR";
    static constexpr const char* CLEAR_COLOR_G = "clearColorG";
    static constexpr const char* CLEAR_COLOR_B = "clearColorB";
    static constexpr const char* CLEAR_COLOR_A = "clearColorA";
    static constexpr const char* CLEAR_DEPTH = "clearDepth";
    static constexpr const char* CLEAR_STENCIL = "clearStencil";

    // ===== INPUTS (15) =====
    INPUT_SLOT(RENDER_PASS, VkRenderPass, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(FRAMEBUFFERS, std::vector<VkFramebuffer>, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(PIPELINE, VkPipeline, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(PIPELINE_LAYOUT, VkPipelineLayout, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VERTEX_BUFFER, VkBuffer, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(INDEX_BUFFER, VkBuffer, 6,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 7,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 8,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 10,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 11,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 12,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 13,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 14,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(COMMAND_BUFFERS, VkCommandBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    GeometryRenderNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(FRAMEBUFFERS, "framebuffers",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(PIPELINE, "pipeline",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(VERTEX_BUFFER, "vertex_buffer",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(INDEX_BUFFER, "index_buffer",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        HandleDescriptor swapchainInfoDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent,
            swapchainInfoDesc
        );
        
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );
        
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );
        
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        // Phase 0.5: In-flight fence input from FrameSyncNode
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        // Phase 0.5: Semaphore arrays for per-image synchronization
        HandleDescriptor semaphoreArrayDesc{"VkSemaphore*"};

        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent,
            semaphoreArrayDesc
        );

        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array",
            ResourceLifetime::Persistent,
            semaphoreArrayDesc
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(COMMAND_BUFFERS, "command_buffers",
            ResourceLifetime::Transient,
            BufferDescription{}
        );
        
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Transient,
            BufferDescription{}
        );
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(GeometryRenderNodeConfig, GeometryRenderNodeCounts);

    static_assert(RENDER_PASS_Slot::index == 0, "RENDER_PASS must be at index 0");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(FRAMEBUFFERS_Slot::index == 1, "FRAMEBUFFERS must be at index 1");
    static_assert(!FRAMEBUFFERS_Slot::nullable, "FRAMEBUFFERS is required");

    static_assert(PIPELINE_Slot::index == 2, "PIPELINE must be at index 2");
    static_assert(!PIPELINE_Slot::nullable, "PIPELINE is required");

    static_assert(PIPELINE_LAYOUT_Slot::index == 3, "PIPELINE_LAYOUT must be at index 3");
    static_assert(!PIPELINE_LAYOUT_Slot::nullable, "PIPELINE_LAYOUT is required");

    static_assert(DESCRIPTOR_SETS_Slot::index == 4, "DESCRIPTOR_SETS must be at index 4");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");

    static_assert(VERTEX_BUFFER_Slot::index == 5, "VERTEX_BUFFER must be at index 5");
    static_assert(!VERTEX_BUFFER_Slot::nullable, "VERTEX_BUFFER is required");

    static_assert(INDEX_BUFFER_Slot::index == 6, "INDEX_BUFFER must be at index 6");
    static_assert(INDEX_BUFFER_Slot::nullable, "INDEX_BUFFER is optional");

    static_assert(SWAPCHAIN_INFO_Slot::index == 7, "SWAPCHAIN_INFO must be at index 7");
    static_assert(!SWAPCHAIN_INFO_Slot::nullable, "SWAPCHAIN_INFO is required");
    
    static_assert(COMMAND_POOL_Slot::index == 8, "COMMAND_POOL must be at index 8");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL is required");
    
    static_assert(VULKAN_DEVICE_Slot::index == 9, "VULKAN_DEVICE must be at index 9");
    static_assert(!VULKAN_DEVICE_Slot::nullable, "VULKAN_DEVICE is required");
    
    static_assert(IMAGE_INDEX_Slot::index == 10, "IMAGE_INDEX must be at index 10");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 11, "CURRENT_FRAME_INDEX must be at index 11");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX is required");

    static_assert(IN_FLIGHT_FENCE_Slot::index == 12, "IN_FLIGHT_FENCE must be at index 12");
    static_assert(!IN_FLIGHT_FENCE_Slot::nullable, "IN_FLIGHT_FENCE is required");

    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 13, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 13");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY is required");

    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 14, "RENDER_COMPLETE_SEMAPHORES_ARRAY must be at index 14");
    static_assert(!RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::nullable, "RENDER_COMPLETE_SEMAPHORES_ARRAY is required");

    static_assert(COMMAND_BUFFERS_Slot::index == 0, "COMMAND_BUFFERS must be at index 0");
    static_assert(!COMMAND_BUFFERS_Slot::nullable, "COMMAND_BUFFERS is required");
    
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 1, "RENDER_COMPLETE_SEMAPHORE must be at index 1");
    static_assert(!RENDER_COMPLETE_SEMAPHORE_Slot::nullable, "RENDER_COMPLETE_SEMAPHORE is required");

    // Type validations
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, std::vector<VkFramebuffer>>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, std::vector<VkDescriptorSet>>);
    static_assert(std::is_same_v<VERTEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INDEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariables*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<COMMAND_BUFFERS_Slot::Type, VkCommandBuffer>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

// Global compile-time validations
static_assert(GeometryRenderNodeConfig::ALLOW_INPUT_ARRAYS); // Array mode enabled

} // namespace Vixen::RenderGraph


