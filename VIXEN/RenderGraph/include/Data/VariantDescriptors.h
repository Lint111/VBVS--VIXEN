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

/**
 * @brief Storage image descriptor (for compute shader output)
 * Phase G.2: Compute shader writes to storage images
 */
struct StorageImageDescriptor : ResourceDescriptorBase {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;  // Required for storage images

    bool Validate() const override {
        return width > 0 && height > 0 && format != VK_FORMAT_UNDEFINED;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<StorageImageDescriptor>(*this);
    }
};

/**
 * @brief 3D texture descriptor (for voxel data)
 * Phase G.2: Compute shader samples from 3D textures
 */
struct Texture3DDescriptor : ResourceDescriptorBase {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t mipLevels = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

    bool Validate() const override {
        return width > 0 && height > 0 && depth > 0 && format != VK_FORMAT_UNDEFINED;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<Texture3DDescriptor>(*this);
    }
};

// ============================================================================
// RUNTIME STRUCT DESCRIPTOR (Phase H: Discovery System)
// ============================================================================

/**
 * @brief Shader scalar type classification for runtime reflection
 */
enum class ShaderScalarType : uint8_t {
    Float,
    Int,
    UInt,
    Bool,
    Mat,
    Vec,
    Unknown
};

/**
 * @brief Runtime field information for shader struct reflection
 *
 * Describes a single field in a shader struct (from SPIRV reflection).
 */
struct RuntimeFieldInfo {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    ShaderScalarType baseType = ShaderScalarType::Unknown;
    uint32_t componentCount = 0;
    bool isArray = false;
    uint32_t arraySize = 0;
};

/**
 * @brief Runtime struct descriptor for shader UBO/SSBO layouts
 *
 * Phase H: Hybrid discovery system
 * - Holds struct layout extracted from SPIRV reflection
 * - layoutHash enables discovery of unknown types at startup
 * - User can promote to compile-time by registering in RESOURCE_TYPE_REGISTRY
 */
struct RuntimeStructDescriptor : ResourceDescriptorBase {
    std::string structName;
    uint32_t totalSize = 0;
    std::vector<RuntimeFieldInfo> fields;
    std::unordered_map<std::string, size_t> fieldIndexByName;
    uint64_t layoutHash = 0;  // Hash of (name, offset, size, type) for discovery

    /**
     * @brief Build field lookup map (call after adding fields)
     */
    void BuildLookup() {
        fieldIndexByName.clear();
        for (size_t i = 0; i < fields.size(); ++i) {
            fieldIndexByName[fields[i].name] = i;
        }
    }

    /**
     * @brief Find field by name
     */
    std::optional<const RuntimeFieldInfo*> FindField(const std::string& name) const {
        auto it = fieldIndexByName.find(name);
        if (it != fieldIndexByName.end()) {
            return &fields[it->second];
        }
        return std::nullopt;
    }

    bool Validate() const override {
        return totalSize > 0 && !fields.empty();
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<RuntimeStructDescriptor>(*this);
    }
};

/**
 * @brief Runtime struct buffer with typed field access
 *
 * Holds actual data for a runtime-described struct.
 * Used when descriptor layout is unknown at compile-time.
 */
struct RuntimeStructBuffer : ResourceDescriptorBase {
    RuntimeStructDescriptor desc;
    std::vector<uint8_t> data;

    RuntimeStructBuffer() = default;

    explicit RuntimeStructBuffer(const RuntimeStructDescriptor& d)
        : desc(d), data(d.totalSize, 0) {}

    /**
     * @brief Set field by name (runtime type-checked)
     */
    bool SetFieldByName(const std::string& name, const void* src, uint32_t srcSize) {
        auto fieldOpt = desc.FindField(name);
        if (!fieldOpt.has_value()) {
            return false;
        }

        const RuntimeFieldInfo* field = fieldOpt.value();
        if (field->offset + srcSize > data.size()) {
            return false;  // Out of bounds
        }

        std::memcpy(data.data() + field->offset, src, srcSize);
        return true;
    }

    /**
     * @brief Set field with compile-time type safety
     */
    template<typename T>
    bool SetField(const std::string& name, const T& value) {
        return SetFieldByName(name, &value, sizeof(T));
    }

    bool Validate() const override {
        return desc.Validate() && data.size() == desc.totalSize;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<RuntimeStructBuffer>(*this);
    }
};


// ============================================================================
// DESCRIPTOR VARIANT TYPE
// ============================================================================

/**
 * @brief Variant type holding all possible resource descriptors
 * Auto-generated from unique descriptors in RESOURCE_TYPE_REGISTRY
 *
 * Note: We list each descriptor type once, even if multiple handle types use it
 */
using ResourceDescriptorVariant = std::variant<
    std::monostate,
    ImageDescriptor,
    BufferDescriptor,
    HandleDescriptor,
    CommandPoolDescriptor,
    ShaderProgramHandleDescriptor,
    StorageImageDescriptor,  // Phase G.2: Compute shader storage images
    Texture3DDescriptor,     // Phase G.2: 3D voxel textures
    RuntimeStructDescriptor, // Phase H: Runtime-discovered struct layouts
    RuntimeStructBuffer      // Phase H: Runtime struct data storage
>;


} // namespace Vixen::RenderGraph