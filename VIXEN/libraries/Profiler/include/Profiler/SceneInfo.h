#pragma once

#include <cstdint>
#include <string>

namespace Vixen::Profiler {

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

    /// Validate scene info
    bool IsValid() const;

    /// Get descriptive scene name based on type if sceneName is empty
    std::string GetDisplayName() const;
};

} // namespace Vixen::Profiler
