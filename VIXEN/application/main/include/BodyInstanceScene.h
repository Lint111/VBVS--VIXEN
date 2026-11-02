#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Vixen::SVO { struct BodyInstanceGpu; }
class VulkanGraphApplication;

namespace Vixen::App {

// Shared scene ingress used by the production application and external renderer hosts. The
// ray marcher remains BuildRenderGraph's BodyOctreeSceneNode; this piece only owns the stable
// host-facing instance/camera seam and its bounded update policy.
struct BodyInstanceSceneStats {
    bool uploaded = false;
    std::size_t submitted = 0;
    std::size_t dropped = 0;
};

class BodyInstanceScenePiece final {
public:
    static constexpr std::size_t kMaxInstances = 192; // R213 bounded scene upload

    explicit BodyInstanceScenePiece(VulkanGraphApplication& application) : application_(application) {}

    BodyInstanceSceneStats Update(std::vector<Vixen::SVO::BodyInstanceGpu> instances,
                                  std::uint64_t sectionRevision);
    void Clear();
    void SetCamera(glm::vec3 eye, glm::vec3 target, glm::vec3 up, float verticalFov);

    std::uint64_t uploadCount() const { return uploadCount_; }
    bool hasInstances() const { return hasInstances_; }

private:
    VulkanGraphApplication& application_;
    std::uint64_t lastRevision_ = 0;
    bool hasRevision_ = false;
    bool hasInstances_ = false;
    std::uint64_t uploadCount_ = 0;
};

} // namespace Vixen::App
