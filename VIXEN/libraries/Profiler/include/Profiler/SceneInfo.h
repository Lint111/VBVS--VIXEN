#pragma once

#include <cstdint>
#include <string>

namespace Vixen::Profiler {

// Forward declaration
struct SceneDefinition;

/// Scene configuration data for benchmark metrics
/// Used to identify scene characteristics in exported results
struct SceneInfo {
    uint32_t resolution = 0;           // Voxel grid resolution (e.g., 256 for 256^3)
    float densityPercent = 0.0f;       // Fill percentage (0-100)
    std::string sceneType;             // Type identifier: "cornell_box", "cave", "urban", etc.
    std::string sceneName;             // User-friendly display name

    /// Default scene types used in benchmarks
    static constexpr const char* TYPE_CORNELL_BOX = "cornell_box";
    static constexpr const char* TYPE_SPARSE_ARCHITECTURAL = "sparse_architectural";
    static constexpr const char* TYPE_DENSE_ORGANIC = "dense_organic";
    static constexpr const char* TYPE_CAVE = "cave";
    static constexpr const char* TYPE_URBAN = "urban";
    static constexpr const char* TYPE_TEST = "test";

    /// Create SceneInfo from TestConfiguration
    static SceneInfo FromResolutionAndDensity(
        uint32_t resolution,
        float densityPercent,
        const std::string& sceneType = TYPE_TEST,
        const std::string& sceneName = "");

    /**
     * @brief Create SceneInfo from SceneDefinition
     *
     * Maps SceneDefinition to SceneInfo, extracting scene type from
     * procedural generator name or file path.
     *
     * For Procedural type: Uses generator name as scene type
     * For File type: Extracts scene type from filename (TODO: requires file loading)
     *
     * @param sceneDef Scene definition from benchmark config
     * @param resolution Voxel grid resolution
     * @return SceneInfo configured from definition
     */
    static SceneInfo FromSceneDefinition(
        const SceneDefinition& sceneDef,
        uint32_t resolution);

    /// Validate scene info
    bool IsValid() const;

    /// Get descriptive scene name based on type if sceneName is empty
    std::string GetDisplayName() const;
};

} // namespace Vixen::Profiler
