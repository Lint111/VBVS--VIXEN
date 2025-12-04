#include "Profiler/SceneInfo.h"
#include "Profiler/BenchmarkConfig.h"

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

SceneInfo SceneInfo::FromSceneDefinition(
    const SceneDefinition& sceneDef,
    uint32_t resolution) {

    SceneInfo info;
    info.resolution = resolution;
    info.densityPercent = 0.0f;  // Will be computed from actual scene data
    info.sceneName = sceneDef.name;

    // Map scene definition to scene type
    if (sceneDef.sourceType == SceneSourceType::Procedural) {
        // Map procedural generator to scene type
        const std::string& generator = sceneDef.procedural.generator;

        if (generator == "cornell" || generator == "cornell_box") {
            info.sceneType = TYPE_CORNELL_BOX;
        } else if (generator == "perlin3d" || generator == "noise") {
            info.sceneType = TYPE_DENSE_ORGANIC;
        } else if (generator == "voronoi_caves" || generator == "cave" || generator == "tunnels") {
            info.sceneType = TYPE_CAVE;
        } else if (generator == "buildings" || generator == "urban" || generator == "cityscape") {
            info.sceneType = TYPE_URBAN;
        } else if (generator == "sparse" || generator == "architectural") {
            info.sceneType = TYPE_SPARSE_ARCHITECTURAL;
        } else {
            // Use generator name directly as scene type for unknown generators
            info.sceneType = generator.empty() ? TYPE_TEST : generator;
        }
    } else {
        // File-based scene
        // TODO: Extract scene type from file contents or use filename
        // For now, use the scene name as type
        info.sceneType = sceneDef.name.empty() ? TYPE_TEST : sceneDef.name;
    }

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
