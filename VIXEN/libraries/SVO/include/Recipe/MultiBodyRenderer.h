#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "Recipe/BodyFootprintRegime.h"
#include "Recipe/LodSelection.h"

namespace Vixen::SVO {

class RecipeRegistry;

// Presentation-owned body placement.  Positions stay double precision until the
// render list is built so system-scale coordinates can be narrowed relative to the
// current camera without losing local precision.
struct CelestialBody {
    uint64_t id = 0;
    glm::dvec3 position = glm::dvec3(0.0);
    float radius = 1.0f;
    uint32_t recipeId = 0;
};

enum class CelestialBodyUpdateResult : uint8_t {
    Added,
    Updated,
    InvalidRadius,
    NonFinitePosition,
};

class CelestialBodyRegistry {
public:
    [[nodiscard]] CelestialBodyUpdateResult Upsert(const CelestialBody& body) noexcept;
    [[nodiscard]] bool Remove(uint64_t bodyId) noexcept;
    [[nodiscard]] const CelestialBody* Find(uint64_t bodyId) const noexcept;

    // std::map provides deterministic id order for snapshots and transition cleanup.
    [[nodiscard]] std::vector<CelestialBody> Snapshot() const;
    [[nodiscard]] std::size_t Size() const noexcept { return bodies_.size(); }
    void Clear() noexcept { bodies_.clear(); }

private:
    std::map<uint64_t, CelestialBody> bodies_;
};

struct CelestialRenderItem {
    uint64_t bodyId = 0;
    uint32_t recipeId = 0;
    glm::dvec3 worldPosition = glm::dvec3(0.0);
    glm::vec3 cameraRelativePosition = glm::vec3(0.0f);
    double surfaceDistance = 0.0;
    float cameraDistance = 0.0f;
    float lodQ = 0.0f;
    float radius = 0.0f;
    BodyFootprintRegime regime = BodyFootprintRegime::DeepField;
    std::size_t bandIndex = 0;
    LodBand band{};
    LodUploadDecision upload{};
};

struct CelestialRenderList {
    // Drawable items are sorted near-to-far by surface distance, then body id.
    std::vector<CelestialRenderItem> items;
    std::vector<uint64_t> skippedBodyIds;
    std::vector<uint64_t> missingRecipeBodyIds;
};

// Builds the per-frame body draw list from body placements and registered recipes.
// The builder is stateful only for LOD hysteresis and skip re-entry upload tracking;
// all body placement remains in CelestialBodyRegistry.
class CelestialRenderListBuilder {
public:
    explicit CelestialRenderListBuilder(
        const RecipeRegistry& recipes,
        LodTransitionConfig transitionConfig = {}) noexcept;

    [[nodiscard]] CelestialRenderList Build(
        const CelestialBodyRegistry& bodies,
        const glm::dvec3& cameraPosition,
        const LODParameters& camera);

    void ResetTransitions() noexcept;

private:
    const RecipeRegistry& recipes_;
    LodTransitionConfig transitionConfig_;
    std::map<uint64_t, LodTransition> transitions_;
    std::map<uint64_t, bool> previouslySkipped_;
};

} // namespace Vixen::SVO
