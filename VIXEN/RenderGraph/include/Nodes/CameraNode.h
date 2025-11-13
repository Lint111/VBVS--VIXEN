#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/CameraNodeConfig.h"
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
 * @brief Camera data structure for both push constants and uniform buffers
 *
 * Contains camera-related fields that can be used for push constants or UBOs:
 * - Camera position and orientation vectors
 * - Projection parameters (fov, aspect)
 * - Matrix fields for uniform buffers (invProjection, invView)
 */
struct CameraData {
    // Camera fields (for ray generation push constants)
    glm::vec3 cameraPos;         // Offset 0, 12 bytes
    float fov;                   // Offset 12, 4 bytes
    glm::vec3 cameraDir;         // Offset 16, 12 bytes
    float aspect;                // Offset 28, 4 bytes
    glm::vec3 cameraUp;          // Offset 32, 12 bytes
    float lodBias;               // Offset 44, 4 bytes
    glm::vec3 cameraRight;       // Offset 48, 12 bytes
    uint32_t gridResolution;     // Offset 60, 4 bytes

    // Matrix fields (for uniform buffers)
    glm::mat4 invProjection;     // Offset 64, 64 bytes
    glm::mat4 invView;           // Offset 128, 64 bytes
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
    void UpdateCameraData(float aspectRatio);

    // Apply accumulated input deltas to camera state
    void ApplyInputDeltas(float deltaTime);


    // Current camera data struct
    CameraData currentCameraData;

    // Camera state
    glm::vec3 cameraPosition{0.0f, 0.0f, 3.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    uint32_t gridResolution = 128;

    // Accumulated input deltas (cleared after applying)
    glm::vec3 movementDelta{0.0f};  // Local-space WASD + global Y for QE
    glm::vec2 rotationDelta{0.0f};  // Yaw/pitch from mouse (raw accumulation)
    glm::vec2 smoothedRotationDelta{0.0f};  // Smoothed rotation for jitter reduction

    // Camera control parameters
    float moveSpeed = 30.0f;      // Horizontal movement: units per second (3x faster)
    float verticalSpeed = 30.0f;  // Vertical movement (QE): units per second (3x faster)
    float mouseSensitivity = 0.004f;  // Radians per pixel (3.3x more sensitive)
    float mouseSmoothingFactor = 0.6f;  // 0=no smoothing, 1=instant (0.6 = responsive)
    float maxRotationDeltaPerFrame = 100.0f;  // Max pixels per frame to prevent jumps

    // Setup state tracking (prevent camera reset on recompilation)
    bool initialSetupComplete = false;
};

} // namespace Vixen::RenderGraph
