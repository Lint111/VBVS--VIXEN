#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/PerFrameResources.h"
#include <glm/glm.hpp>
#include <memory>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for camera management
 */
class CameraNodeType : public TypedNodeType<CameraNodeConfig> {
public:
    CameraNodeType(const std::string& typeName = "Camera")
        : TypedNodeType<CameraNodeConfig>(typeName) {}
    virtual ~CameraNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Camera data structure matching VoxelRayMarch shader
 *
 * Must match layout in generated/sdi/69bb4318e2e033bd-SDI.h:
 * struct CameraData {
 *     glm::mat4 invProjection;  // Offset 0
 *     glm::mat4 invView;        // Offset 64
 *     glm::vec3 cameraPos;      // Offset 128
 *     uint32_t gridResolution;  // Offset 140
 * };
 */
struct CameraData {
    glm::mat4 invProjection;
    glm::mat4 invView;
    glm::vec3 cameraPos;
    uint32_t gridResolution;
};

/**
 * @brief Camera uniform buffer node for raymarching shaders
 *
 * Creates per-frame uniform buffers containing camera matrices and parameters.
 * Updates camera position and orientation via parameters.
 *
 * Phase: Research implementation (voxel raymarching)
 */
class CameraNode : public TypedNode<CameraNodeConfig> {
public:
    CameraNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~CameraNode() override = default;

protected:
    using TypedSetupContext = typename Base::TypedSetupContext;
    using TypedCompileContext = typename Base::TypedCompileContext;
    using TypedExecuteContext = typename Base::TypedExecuteContext;
    using TypedCleanupContext = typename Base::TypedCleanupContext;

    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void UpdateCameraMatrices(uint32_t frameIndex, uint32_t imageIndex, float aspectRatio);

    // Device reference
    Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice = nullptr;

    // Per-frame uniform buffers using PerFrameResources helper
    PerFrameResources perFrameResources;

    // Camera state
    glm::vec3 cameraPosition{0.0f, 0.0f, 3.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    uint32_t gridResolution = 128;
};

} // namespace Vixen::RenderGraph
