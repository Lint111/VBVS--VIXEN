#include "BodyInstanceScene.h"

#include "VulkanGraphApplication.h"
#include "ShellOctreeGpu.h"

#include <algorithm>

namespace Vixen::App {

BodyInstanceSceneStats BodyInstanceScenePiece::Update(
    std::vector<Vixen::SVO::BodyInstanceGpu> instances, std::uint64_t sectionRevision) {
    if (hasRevision_ && sectionRevision == lastRevision_)
        return {false, instances.size(), 0};
    lastRevision_ = sectionRevision;
    hasRevision_ = true;
    BodyInstanceSceneStats result;
    result.dropped = instances.size() > kMaxInstances ? instances.size() - kMaxInstances : 0;
    if (result.dropped) instances.resize(kMaxInstances); // deterministic first-row overflow policy
    result.submitted = instances.size();
    application_.SetBodyInstances(std::move(instances));
    hasInstances_ = result.submitted != 0;
    ++uploadCount_;
    result.uploaded = true;
    return result;
}

void BodyInstanceScenePiece::Clear() {
    if (!hasInstances_) return;
    application_.SetBodyInstances({});
    hasInstances_ = false;
    ++uploadCount_;
}

void BodyInstanceScenePiece::SetCamera(glm::vec3 eye, glm::vec3 target, glm::vec3 up, float verticalFov) {
    application_.SetSessionCamera(eye, target, up, verticalFov);
}

} // namespace Vixen::App
