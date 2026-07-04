#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <utility>

namespace Vixen::RenderGraph {

/// Rebuild a full camera transform (position + orientation) from scratch every call — the ONLY
/// way orientation is ever derived. Never incrementally rotate an existing transform: rebuilding
/// from the angle inputs each time means there is nothing to accumulate, so no drift is possible.
inline glm::mat4 ComposeTransform(glm::vec3 position, float yaw, float pitch,
                                   glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f)) {
    glm::vec3 forward;
    forward.x = std::cos(pitch) * std::sin(yaw);
    forward.y = std::sin(pitch);
    forward.z = -std::cos(pitch) * std::cos(yaw);
    forward = glm::normalize(forward);
    return glm::inverse(glm::lookAt(position, position + forward, worldUp));
}

/// Inverse of ComposeTransform's orientation: recover (yaw, pitch) from a transform's forward
/// vector (transform's 3rd column, negated — glm::lookAt convention). Used only when something
/// needs to READ angles back out of a transform it didn't just compose itself (e.g. seeding a
/// mode transition from the other mode's current pose).
inline std::pair<float, float> ExtractYawPitch(const glm::mat4& transform) {
    const glm::vec3 forward = glm::normalize(-glm::vec3(transform[2]));
    const float pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    const float yaw = std::atan2(forward.x, -forward.z);
    return { yaw, pitch };
}

inline glm::vec3 ExtractPosition(const glm::mat4& transform) {
    return glm::vec3(transform[3]);
}

/// Replace a transform's translation, keeping its existing orientation untouched.
inline void SetPosition(glm::mat4& transform, glm::vec3 position) {
    transform[3] = glm::vec4(position, 1.0f);
}

} // namespace Vixen::RenderGraph
