#pragma once

#include "Headers.h"



// Forward declarations (MUST be before any usage)
namespace ShaderManagement { 
    struct CompiledProgram; 
    struct DescriptorLayoutSpec;
}

// Global namespace forward declarations
struct SwapChainPublicVariables;

namespace Vixen::RenderGraph {


// ============================================================================
// BASE DESCRIPTOR CLASS
// ============================================================================

/**
 * @brief Base descriptor for resources
 * Provides validation interface for all resource descriptors
 */
struct ResourceDescriptorBase {
    virtual ~ResourceDescriptorBase() = default;
    virtual bool Validate() const { return true; }
    virtual std::unique_ptr<ResourceDescriptorBase> Clone() const = 0;
};





// ============================================================================
// SPECIFIC DESCRIPTOR TYPES
// ============================================================================

/**
 * @brief Image resource descriptor
 */
struct ImageDescriptor : ResourceDescriptorBase {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ResourceUsage usage = ResourceUsage::None;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

    bool Validate() const override {
        return width > 0 && height > 0 && format != VK_FORMAT_UNDEFINED;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<ImageDescriptor>(*this);
    }
};

/**
 * @brief Buffer resource descriptor
 */
struct BufferDescriptor : ResourceDescriptorBase {
    VkDeviceSize size = 0;
    ResourceUsage usage = ResourceUsage::None;
    VkMemoryPropertyFlags memoryProperties = 0;

    bool Validate() const override {
        return size > 0;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<BufferDescriptor>(*this);
    }
};

/**
 * @brief Simple handle descriptor (for VkSurface, VkSwapchain, etc.)
 */
struct HandleDescriptor : ResourceDescriptorBase {
    std::string handleTypeName; // For debugging

    HandleDescriptor(const std::string& typeName = "GenericHandle")
        : handleTypeName(typeName) {}

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<HandleDescriptor>(*this);
    }
};

/**
 * @brief Command pool descriptor
 */
struct CommandPoolDescriptor : ResourceDescriptorBase {
    VkCommandPoolCreateFlags flags = 0;
    uint32_t queueFamilyIndex = 0;

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<CommandPoolDescriptor>(*this);
    }
};

/**
 * @brief Shader program descriptor (pointer to external data)
 * NOTE: ShaderLibraryNodeConfig.h defines a more complete ShaderProgramDescriptor
 * This simple version is kept for basic shader resource descriptors
 */
struct ShaderProgramHandleDescriptor : ResourceDescriptorBase {
    std::string shaderName; // For debugging/identification

    ShaderProgramHandleDescriptor(const std::string& name = "")
        : shaderName(name) {}

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<ShaderProgramHandleDescriptor>(*this);
    }
};


} // namespace Vixen::RenderGraph