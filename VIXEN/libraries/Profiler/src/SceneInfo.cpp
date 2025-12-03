#include "Profiler/SceneInfo.h"

namespace Vixen::Profiler {

SceneInfo SceneInfo::FromResolutionAndDensity(
    uint32_t resolution,
    float densityPercent,
    const std::string& sceneType,
    const std::string& sceneName) {

    SceneInfo info;
    info.resolution = resolution;
    info.densityPercent = densityPercent;
    info.sceneType = sceneType;
    info.sceneName = sceneName;
    return info;
}

bool SceneInfo::IsValid() const {
    if (resolution == 0 || resolution > 4096) return false;
    if (densityPercent < 0.0f || densityPercent > 100.0f) return false;
    if (sceneType.empty()) return false;
    return true;
}

std::string SceneInfo::GetDisplayName() const {
    if (!sceneName.empty()) {
        return sceneName;
    }

    // Generate name from type and resolution
    std::string name;
    if (sceneType == TYPE_CORNELL_BOX) {
        name = "Cornell Box";
    } else if (sceneType == TYPE_SPARSE_ARCHITECTURAL) {
        name = "Sparse Architectural";
    } else if (sceneType == TYPE_DENSE_ORGANIC) {
        name = "Dense Organic";
    } else if (sceneType == TYPE_CAVE) {
        name = "Cave";
    } else if (sceneType == TYPE_URBAN) {
        name = "Urban";
    } else if (sceneType == TYPE_TEST) {
        name = "Test Scene";
    } else {
        name = sceneType;
    }

    return name + " " + std::to_string(resolution) + "^3";
}

} // namespace Vixen::Profiler
