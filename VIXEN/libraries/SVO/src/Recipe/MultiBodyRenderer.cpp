#include "Recipe/MultiBodyRenderer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

#include "Recipe/RecipeRegistry.h"

namespace Vixen::SVO {
namespace {

[[nodiscard]] bool IsFiniteDouble(double value) noexcept {
    constexpr uint64_t kExponentMask = 0x7FF0000000000000ull;
    return (std::bit_cast<uint64_t>(value) & kExponentMask) != kExponentMask;
}

[[nodiscard]] bool IsFiniteScalarFloat(float value) noexcept {
    constexpr uint32_t kExponentMask = 0x7F800000u;
    return (std::bit_cast<uint32_t>(value) & kExponentMask) != kExponentMask;
}

[[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept {
    return IsFiniteDouble(value.x) && IsFiniteDouble(value.y) && IsFiniteDouble(value.z);
}

[[nodiscard]] float ToFloatDistance(double distance) noexcept {
    if (!(distance >= 0.0)) return 0.0f;
    if (distance > static_cast<double>(std::numeric_limits<float>::max()))
        return std::numeric_limits<float>::infinity();
    return static_cast<float>(distance);
}

} // namespace

CelestialBodyUpdateResult CelestialBodyRegistry::Upsert(const CelestialBody& body) noexcept {
    if (!(body.radius > 0.0f) || !IsFiniteScalarFloat(body.radius))
        return CelestialBodyUpdateResult::InvalidRadius;
    // Reject system-scale placement corruption before it can enter the deterministic map.
    if (!IsFinite(body.position))
        return CelestialBodyUpdateResult::NonFinitePosition;

    const auto [it, inserted] = bodies_.insert_or_assign(body.id, body);
    (void)it;
    return inserted ? CelestialBodyUpdateResult::Added : CelestialBodyUpdateResult::Updated;
}

bool CelestialBodyRegistry::Remove(uint64_t bodyId) noexcept {
    return bodies_.erase(bodyId) != 0;
}

const CelestialBody* CelestialBodyRegistry::Find(uint64_t bodyId) const noexcept {
    const auto it = bodies_.find(bodyId);
    return it == bodies_.end() ? nullptr : &it->second;
}

std::vector<CelestialBody> CelestialBodyRegistry::Snapshot() const {
    std::vector<CelestialBody> result;
    result.reserve(bodies_.size());
    for (const auto& [bodyId, body] : bodies_) {
        (void)bodyId;
        result.push_back(body);
    }
    return result;
}

CelestialRenderListBuilder::CelestialRenderListBuilder(
    const RecipeRegistry& recipes, LodTransitionConfig transitionConfig) noexcept
    : recipes_(recipes), transitionConfig_(transitionConfig) {}

CelestialRenderList CelestialRenderListBuilder::Build(
    const CelestialBodyRegistry& bodies,
    const glm::dvec3& cameraPosition,
    const LODParameters& camera) {
    CelestialRenderList result;
    const auto snapshot = bodies.Snapshot();

    std::map<uint64_t, bool> liveBodies;
    result.items.reserve(snapshot.size());
    result.skippedBodyIds.reserve(snapshot.size());
    result.missingRecipeBodyIds.reserve(snapshot.size());

    for (const CelestialBody& body : snapshot) {
        liveBodies.emplace(body.id, true);
        const RecipeRegistry::RecipeEntry* recipe = recipes_.Get(body.recipeId);
        if (recipe == nullptr || recipe->lodLadder.empty()) {
            result.missingRecipeBodyIds.push_back(body.id);
            transitions_.erase(body.id);
            previouslySkipped_.erase(body.id);
            continue;
        }

        const glm::dvec3 delta = body.position - cameraPosition;
        const double centerDistance = glm::length(delta);
        const double surfaceDistance = std::max(0.0, centerDistance - static_cast<double>(body.radius));
        const float cameraDistance = ToFloatDistance(centerDistance);
        const float lodRadius = recipe->boundRadius > 0.0f ? recipe->boundRadius : body.radius;
        const float q = ComputeLodQ(
            ToFloatDistance(surfaceDistance), lodRadius, camera);

        auto transitionIt = transitions_.find(body.id);
        if (transitionIt == transitions_.end()) {
            transitionIt = transitions_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(body.id),
                std::forward_as_tuple(transitionConfig_)).first;
        }

        const LodBandSelection selection = transitionIt->second.Update(
            recipe->lodLadder, q, cameraDistance, body.radius);
        const std::size_t bandIndex = std::min(selection.bandIndex, recipe->lodLadder.size() - 1);
        const LodBand& band = recipe->lodLadder[bandIndex];
        const bool wasSkipped = previouslySkipped_[body.id];
        const LodUploadDecision upload = GateLodUploads(band, false, wasSkipped);
        previouslySkipped_[body.id] = !upload.emitInstance;

        if (!upload.emitInstance) {
            result.skippedBodyIds.push_back(body.id);
            continue;
        }

        CelestialRenderItem item;
        item.bodyId = body.id;
        item.recipeId = body.recipeId;
        item.worldPosition = body.position;
        item.cameraRelativePosition = glm::vec3(delta);
        item.surfaceDistance = surfaceDistance;
        item.cameraDistance = cameraDistance;
        item.lodQ = selection.q;
        item.radius = body.radius;
        item.regime = ClassifyFootprintRegime(cameraDistance, body.radius);
        item.bandIndex = bandIndex;
        item.band = band;
        item.upload = upload;
        result.items.push_back(item);
    }

    for (auto it = transitions_.begin(); it != transitions_.end();) {
        if (!liveBodies.count(it->first)) {
            previouslySkipped_.erase(it->first);
            it = transitions_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = previouslySkipped_.begin(); it != previouslySkipped_.end();) {
        if (!liveBodies.count(it->first)) it = previouslySkipped_.erase(it);
        else ++it;
    }

    std::stable_sort(result.items.begin(), result.items.end(),
        [](const CelestialRenderItem& lhs, const CelestialRenderItem& rhs) {
            if (lhs.surfaceDistance != rhs.surfaceDistance)
                return lhs.surfaceDistance < rhs.surfaceDistance;
            return lhs.bodyId < rhs.bodyId;
        });
    return result;
}

void CelestialRenderListBuilder::ResetTransitions() noexcept {
    transitions_.clear();
    previouslySkipped_.clear();
}

} // namespace Vixen::SVO
