#pragma once

#include "Headers.h"
#include <variant>

#include "Core/ResourceConfig.h"
#include "Data/DeviceHandles.h"

// Forward declarations (outside Vixen::RenderGraph to avoid nesting)
namespace ShaderManagement {
    struct DescriptorLayoutSpec;
}

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

// Forward declarations
class NodeType;

// Type alias for const pointer (needed for variant compatibility)
using DescriptorLayoutSpecPtr = const ::ShaderManagement::DescriptorLayoutSpec*;

/**
 * @brief Depth format options for depth buffers
 */
enum class DepthFormat {
    D16,      // VK_FORMAT_D16_UNORM - 16-bit depth
    D24S8,    // VK_FORMAT_D24_UNORM_S8_UINT - 24-bit depth + 8-bit stencil
    D32       // VK_FORMAT_D32_SFLOAT - 32-bit float depth (default)
};

/**
 * @brief Attachment load operations for render passes
 */
enum class AttachmentLoadOp {
    Load,      // VK_ATTACHMENT_LOAD_OP_LOAD - Preserve existing contents
    Clear,     // VK_ATTACHMENT_LOAD_OP_CLEAR - Clear to constant value
    DontCare   // VK_ATTACHMENT_LOAD_OP_DONT_CARE - Undefined (fastest)
};

/**
 * @brief Attachment store operations for render passes
 */
enum class AttachmentStoreOp {
    Store,     // VK_ATTACHMENT_STORE_OP_STORE - Store contents for later use
    DontCare   // VK_ATTACHMENT_STORE_OP_DONT_CARE - Don't care about contents after rendering
};

/**
 * @brief Image layout options for render passes
 */
enum class ImageLayout {
    Undefined,                    // VK_IMAGE_LAYOUT_UNDEFINED
    ColorAttachment,             // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    DepthStencilAttachment,      // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    PresentSrc,                  // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    TransferSrc,                 // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    TransferDst                  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
};

/**
 * @brief Node execution state
 */
enum class NodeState {
    Created,      // Just created, not configured
    Ready,        // Configured and ready to compile
    Compiled,     // Pipelines and resources allocated
    Executing,    // Currently executing
    Complete,     // Execution finished
    Error         // Error state
};

/**
 * @brief Performance statistics for node execution
 */
struct PerformanceStats {
    uint64_t executionTimeNs = 0;         // GPU execution time
    uint64_t cpuTimeNs = 0;               // CPU time for setup
    uint32_t executionCount = 0;          // Number of times executed
    float averageExecutionTimeMs = 0.0f;
};


/**
 * @brief Parameter types MACRO set up
 */
#define PARAMETER_TYPES \
    PARAM_TYPE(Int32, int32_t) \
    PARAM_TYPE(UInt32, uint32_t) \
    PARAM_TYPE(Float, float) \
    PARAM_TYPE(Double, double) \
    PARAM_TYPE(Bool, bool) \
    PARAM_TYPE(String, std::string) \
    PARAM_TYPE(Vec2, glm::vec2) \
    PARAM_TYPE(Vec3, glm::vec3) \
    PARAM_TYPE(Vec4, glm::vec4) \
    PARAM_TYPE(Mat4, glm::mat4) \
    PARAM_TYPE(DepthFormat, DepthFormat) \
    PARAM_TYPE(AttachmentLoadOp, AttachmentLoadOp) \
    PARAM_TYPE(AttachmentStoreOp, AttachmentStoreOp) \
    PARAM_TYPE(ImageLayout, ImageLayout) \
    PARAM_TYPE_LAST(DescriptorLayoutSpecPtr, DescriptorLayoutSpecPtr)

// Generate enum from macro
enum class ParamType {
#define PARAM_TYPE(enumName, typeName) enumName,
#define PARAM_TYPE_LAST(enumName, typeName) enumName
    PARAMETER_TYPES
#undef PARAM_TYPE
#undef PARAM_TYPE_LAST
};

// Generate variant from same macro
using ParamTypeValue = std::variant<
#define PARAM_TYPE(enumName, typeName) typeName,
#define PARAM_TYPE_LAST(enumName, typeName) typeName
    PARAMETER_TYPES
#undef PARAM_TYPE
#undef PARAM_TYPE_LAST
>;

/**
 * @brief Parameter definition for node types
 */
struct ParameterDefinition {
    std::string name;
    ParamType type;
    bool required = false;
    std::string description;
    ParamTypeValue defaultValue;

    ParameterDefinition(const std::string& n, ParamType t, bool req = false, 
                       const std::string& desc = "")
    : name(n), type(t), required(req), description(desc) {
        SetDefaultValue();
    }

    // Constructor with explicit default value
    template<typename T>
    ParameterDefinition(const std::string& n, ParamType t, T&& def,
                        bool req = false, const std::string& desc = "")
        : name(n), type(t), required(req), description(desc), defaultValue(std::forward<T>(def)) {}
    
    template<typename T>
    std::optional<T> GetValueAs() const {
        if (auto* value = std::get_if<T>(&defaultValue)) {
            return *value;
        }
        return std::nullopt;
    }

    std::string GetTypeName() const {
        switch (type) {
#define PARAM_TYPE(enumName, typeName) case ParamType::enumName: return #typeName;
#define PARAM_TYPE_LAST(enumName, typeName) case ParamType::enumName: return #typeName;
            PARAMETER_TYPES
#undef PARAM_TYPE
#undef PARAM_TYPE_LAST
        }
        return "Unknown";
    }

    bool ValidValue(const ParamTypeValue& value) const {
        return value.index() == static_cast<size_t>(type);
    }

    // std::string ValueToString() const {
    //     return std::visit([](const auto& value) {
    //         using T = std::decay_t<decltype(value)>;
    //         if constexpr (std::is_same_v<T, std::string>) {
    //             return value;
    //         } else {
    //             return std::to_string(value);
    //         }
    //     }, defaultValue);
    // }

private:
    void SetDefaultValue() {
        switch (type) {
#define PARAM_TYPE(enumName, typeName) case ParamType::enumName: defaultValue = typeName{}; break;
#define PARAM_TYPE_LAST(enumName, typeName) case ParamType::enumName: defaultValue = typeName{}; break;
            PARAMETER_TYPES
#undef PARAM_TYPE
#undef PARAM_TYPE_LAST
        }
    }
};

// Cleanup macro
#undef PARAMETER_TYPES 
} // namespace Vixen::RenderGraph