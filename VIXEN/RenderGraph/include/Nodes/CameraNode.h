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
    glm::mat4 invProjection;     // Offset 0, 64 bytes
    glm::mat4 invView;           // Offset 64, 64 bytes
    glm::vec3 cameraPos;         // Offset 128, 12 bytes
    float _padding0;             // Offset 140, 4 bytes (vec3 aligns to 16)
    uint32_t gridResolution;     // Offset 144, 4 bytes
    float lodBias;               // Offset 148, 4 bytes
    float _padding1[2];          // Offset 152, 8 bytes (align struct to 16)
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
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void UpdateCameraMatrices(uint32_t frameIndex, uint32_t imageIndex, float aspectRatio);

    // Event handlers
    bool OnKeyEvent(const Vixen::EventBus::BaseEventMessage& msg);
    bool OnMouseMove(const Vixen::EventBus::BaseEventMessage& msg);
    bool OnMouseMoveStart(const Vixen::EventBus::BaseEventMessage& msg);

    // Apply accumulated input deltas to camera state
    void ApplyInputDeltas(float deltaTime);

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

    // Event subscriptions tracked automatically by NodeInstance base class

    // Accumulated input deltas (cleared after applying)
    glm::vec3 movementDelta{0.0f};  // Local-space WASD + global Y for QE
    glm::vec2 rotationDelta{0.0f};  // Yaw/pitch from mouse

    // Camera control parameters
    float moveSpeed = 10.0f;      // Horizontal movement: units per second
    float verticalSpeed = 10.0f;  // Vertical movement (QE): units per second
    float mouseSensitivity = 0.0015f;  // Radians per pixel (increased for faster look)

    // Setup state tracking (prevent camera reset on recompilation)
    bool initialSetupComplete = false;
};

} // namespace Vixen::RenderGraph
