#include "Nodes/PickingNode.h"
#include "Data/PickRay.h"
#include "Core/NodeLogging.h"
#include "InputEvents.h"
#include "GaiaVoxelWorld.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> PickingNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PickingNode>(instanceName, const_cast<PickingNodeType*>(this));
}

// ============================================================================
// PICKING NODE IMPLEMENTATION
// ============================================================================

PickingNode::PickingNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<PickingNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[PickingNode] constructor");
}

void PickingNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[PickingNode] setup");
    lastLeftDown_ = false;
    lastPickHit_ = 0;
}

void PickingNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pull per-frame inputs (mirror CameraNode's pull pattern). Guard nulls.
    InputStatePtr input = ctx.In(PickingNodeConfig::INPUT_STATE);
    Vixen::GaiaVoxel::GaiaVoxelWorld* world = ctx.In(PickingNodeConfig::VOXEL_WORLD);
    const CameraData cam = ctx.In(PickingNodeConfig::CAMERA_DATA);  // by value (auto strips the ref)
    const uint32_t width = ctx.In(PickingNodeConfig::VIEWPORT_WIDTH);
    const uint32_t height = ctx.In(PickingNodeConfig::VIEWPORT_HEIGHT);

    // Always publish the current status output, even on early-out frames.
    ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

    if (!input) {
        return;  // no input state this frame
    }

    // Edge-detect the left-button press: fire only on the down-edge.
    const bool leftDown = input->mouseButtons[0];
    const bool pressedThisFrame = leftDown && !lastLeftDown_;
    lastLeftDown_ = leftDown;

    if (!pressedThisFrame) {
        return;  // cheap: only march on a click edge
    }

    // From here on we are handling a genuine click. Validate the rest of the inputs.
    const glm::vec2 screen = input->mousePosition;

    if (!world || width == 0 || height == 0) {
        NODE_LOG_INFO("[PickingNode] click ignored — voxel world or viewport not ready");
        return;
    }

    // Unproject the cursor into a world-space ray.
    const PickRay ray = ComputePickRay(
        cam,
        screen.x, screen.y,
        static_cast<float>(width), static_cast<float>(height));

    // March the voxel world from the camera outward; first solid voxel wins.
    using EntityID = Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID;
    EntityID hitEntity{};
    glm::vec3 hitSamplePos(0.0f);
    bool hit = false;

    for (float t = 0.0f; t <= kMaxDistance; t += kMarchStep) {
        const glm::vec3 p = ray.origin + ray.direction * t;
        const EntityID e = world->getEntityByWorldSpace(p);
        if (world->exists(e)) {
            hitEntity = e;
            hitSamplePos = p;
            hit = true;
            break;
        }
    }

    auto* bus = GetMessageBus();

    if (hit) {
        // Prefer the voxel's stored (grid) position; fall back to the sample point.
        const glm::vec3 worldPos = world->getPosition(hitEntity).value_or(hitSamplePos);

        // Morton code of the hit cell (integer-unit grid; matches GaiaVoxelWorld snapping).
        const uint64_t morton = Vixen::Core::MortonCode64::fromWorldPos(worldPos).code;

        // gaia::ecs::Entity::value() is the full 64-bit packed identifier (unique).
        const uint64_t entityValue = static_cast<uint64_t>(hitEntity.value());

        lastPickHit_ = 1;
        ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

        NODE_LOG_INFO("[PickingNode] HIT entity=" + std::to_string(entityValue) +
                      " (id=" + std::to_string(hitEntity.id()) + ")" +
                      " pos=(" + std::to_string(worldPos.x) + ", " +
                      std::to_string(worldPos.y) + ", " + std::to_string(worldPos.z) + ")" +
                      " morton=" + std::to_string(morton) +
                      " at screen=(" + std::to_string(screen.x) + ", " +
                      std::to_string(screen.y) + ")");

        if (bus) {
            bus->Publish(std::make_unique<EventBus::PickResultEvent>(
                instanceId,
                entityValue,
                /*didHit=*/true,
                worldPos,
                morton,
                screen,
                static_cast<int>(EventBus::MouseButton::Left)));
        }
    } else {
        lastPickHit_ = 0;
        ctx.Out(PickingNodeConfig::LAST_PICK_HIT, lastPickHit_);

        NODE_LOG_INFO("[PickingNode] miss at screen=(" + std::to_string(screen.x) + ", " +
                      std::to_string(screen.y) + ")");

        if (bus) {
            bus->Publish(std::make_unique<EventBus::PickResultEvent>(
                instanceId,
                /*entity=*/0ull,
                /*didHit=*/false,
                glm::vec3(0.0f),
                /*morton=*/0ull,
                screen,
                static_cast<int>(EventBus::MouseButton::Left)));
        }
    }
}

void PickingNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[PickingNode] cleanup");
    // No owned resources. Reset edge/state so a recompile starts clean.
    lastLeftDown_ = false;
    lastPickHit_ = 0;
}

} // namespace Vixen::RenderGraph
