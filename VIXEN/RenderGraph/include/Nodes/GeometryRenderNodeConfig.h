#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

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
    static constexpr size_t INPUTS = 12;  // Reduced from 15: removed VIEWPORT, SCISSOR, RENDER_WIDTH, RENDER_HEIGHT (replaced with SWAPCHAIN_INFO)
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

    // ===== INPUTS (11) =====
    // Render pass from RenderPassNode
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 0, false);

    // Framebuffers from FramebufferNode (array - one per swapchain image)
    CONSTEXPR_INPUT(FRAMEBUFFERS, FramebufferVector, 1, false);

    // Graphics pipeline from GraphicsPipelineNode
    CONSTEXPR_INPUT(PIPELINE, VkPipeline, 2, false);

    // Pipeline layout from GraphicsPipelineNode
    CONSTEXPR_INPUT(PIPELINE_LAYOUT, VkPipelineLayout, 3, false);

    // Descriptor sets from DescriptorSetNode (array)
    CONSTEXPR_INPUT(DESCRIPTOR_SETS, DescriptorSetVector, 4, false);

    // Vertex buffer from VertexBufferNode
    CONSTEXPR_INPUT(VERTEX_BUFFER, VkBuffer, 5, false);

    // Index buffer from VertexBufferNode (nullable - may not use indexed rendering)
    CONSTEXPR_INPUT(INDEX_BUFFER, VkBuffer, 6, true);

    // Swapchain info for viewport/scissor/render area
    CONSTEXPR_INPUT(SWAPCHAIN_INFO, SwapChainPublicVariablesPtr, 7, false);
    
    // Command pool for allocating command buffers
    CONSTEXPR_INPUT(COMMAND_POOL, VkCommandPool, 8, false);
    
    // VulkanDevice (for queue submission)
    CONSTEXPR_INPUT(VULKAN_DEVICE, VulkanDevicePtr, 9, false);
    
    // Current image index from SwapChainNode
    CONSTEXPR_INPUT(IMAGE_INDEX, uint32_t, 10, false);
    
    // Image available semaphore to wait on (from SwapChainNode)
    CONSTEXPR_INPUT(IMAGE_AVAILABLE_SEMAPHORE, VkSemaphore, 11, false);

    // ===== OUTPUTS (2) =====
    // Recorded command buffers (array - one per framebuffer)
    CONSTEXPR_OUTPUT(COMMAND_BUFFERS, VkCommandBuffer, 0, false);
    
    // Render complete semaphore (signaled when rendering finishes, for presentation wait)
    CONSTEXPR_OUTPUT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 1, false);

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
        
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORE, "image_available_semaphore",
            ResourceLifetime::Transient,
            BufferDescription{}
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

    // Compile-time validations
    static_assert(INPUT_COUNT == GeometryRenderNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == GeometryRenderNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == GeometryRenderNodeCounts::ARRAY_MODE, "Array mode mismatch");

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
    
    static_assert(IMAGE_AVAILABLE_SEMAPHORE_Slot::index == 11, "IMAGE_AVAILABLE_SEMAPHORE must be at index 11");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORE_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORE is required");

    static_assert(COMMAND_BUFFERS_Slot::index == 0, "COMMAND_BUFFERS must be at index 0");
    static_assert(!COMMAND_BUFFERS_Slot::nullable, "COMMAND_BUFFERS is required");
    
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 1, "RENDER_COMPLETE_SEMAPHORE must be at index 1");
    static_assert(!RENDER_COMPLETE_SEMAPHORE_Slot::nullable, "RENDER_COMPLETE_SEMAPHORE is required");

    // Type validations
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, FramebufferVector>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, DescriptorSetVector>);
    static_assert(std::is_same_v<VERTEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INDEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariablesPtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<COMMAND_BUFFERS_Slot::Type, VkCommandBuffer>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

// Global compile-time validations
static_assert(GeometryRenderNodeConfig::INPUT_COUNT == GeometryRenderNodeCounts::INPUTS);
static_assert(GeometryRenderNodeConfig::OUTPUT_COUNT == GeometryRenderNodeCounts::OUTPUTS);
static_assert(GeometryRenderNodeConfig::ALLOW_INPUT_ARRAYS); // Array mode enabled

} // namespace Vixen::RenderGraph
