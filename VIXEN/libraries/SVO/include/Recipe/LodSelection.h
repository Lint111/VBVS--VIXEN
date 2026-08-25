#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>

#include "SVOLOD.h"

namespace Vixen::SVO {

// These numeric values are part of the recipe contract and must remain stable.
enum class LodStrategy : uint8_t {
    MarchFull = 0,
    MarchVariant = 1,
    Baked = 2,
    Impostor = 3,
    FarField = 4,
    Skip = 5,
};

enum class LodParamTier : uint8_t {
    Full = 0,
    Half = 1,
};

enum class LodUploadUnit : uint8_t {
    Instance = 1u << 0,
    RecipeParams = 1u << 1,
    RecipeParamsHalf = 1u << 2,
    Occupancy = 1u << 3,
    Bricks = 1u << 4,
    BlockParams = 1u << 5,
};

constexpr uint8_t LodUploadBit(LodUploadUnit unit) noexcept {
    return static_cast<uint8_t>(unit);
}

constexpr uint8_t kAllLodUploadUnits = 0xFFu;
constexpr std::size_t kMaxLodBands = 4;

[[nodiscard]] bool IsPositiveInfinity(float value) noexcept;
[[nodiscard]] bool IsFiniteFloat(float value) noexcept;
[[nodiscard]] bool IsNaNFloat(float value) noexcept;

// A band is active while q < maxQ. The final band must use +infinity.
struct LodBand {
    float maxQ = 0.0f;
    uint8_t strategy = static_cast<uint8_t>(LodStrategy::MarchFull);
    uint32_t variantId = 0;
    uint16_t blockMask = 0xFFFFu;
    uint8_t paramTier = static_cast<uint8_t>(LodParamTier::Full);
    uint8_t uploadSet = kAllLodUploadUnits;
};

enum class LodValidationError : uint8_t {
    None,
    Empty,
    TooManyBands,
    NegativeOrNaNThreshold,
    NonAscendingThresholds,
    FinalBandMustBeInfinite,
    InvalidStrategy,
    InvalidParamTier,
    UploadSetNotMonotone,
};

[[nodiscard]] LodValidationError ValidateLodLadder(
    std::span<const LodBand> ladder) noexcept;

[[nodiscard]] std::size_t SelectLodBand(
    std::span<const LodBand> ladder, float q) noexcept;

// CPU q uses the same cone footprint as the ray path, evaluated at the
// conservative lower-bound entry distance to avoid under-uploading.
[[nodiscard]] float ComputeLodQ(
    float entryDistance, float boundRadius, const LODParameters& camera) noexcept;

[[nodiscard]] float ComputeLodQ(
    const glm::vec3& cameraPosition,
    const glm::vec3& bodyPosition,
    float boundRadius,
    const LODParameters& camera) noexcept;

struct LodTransitionConfig {
    float hysteresisFraction = 0.10f;
    uint32_t dwellFrames = 3;
    float blendWindowFraction = 0.15f;
};

struct LodBandSelection {
    std::size_t bandIndex = 0;
    float q = 0.0f;
    bool changed = false;
};

struct LodTransitionBlend {
    bool active = false;
    std::size_t finerBand = 0;
    std::size_t coarserBand = 0;
    float coarserWeight = 0.0f;
};

class LodTransition {
public:
    explicit LodTransition(LodTransitionConfig config = {}) noexcept;

    [[nodiscard]] LodBandSelection Update(
        std::span<const LodBand> ladder, float q) noexcept;

    void Reset() noexcept;
    void Reset(std::size_t bandIndex) noexcept;

    [[nodiscard]] std::size_t CurrentBand() const noexcept { return currentBand_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    LodTransitionConfig config_;
    std::size_t currentBand_ = 0;
    std::size_t pendingBand_ = 0;
    uint32_t pendingFrames_ = 0;
    bool initialized_ = false;
};

[[nodiscard]] LodTransitionBlend ComputeLodBlend(
    std::span<const LodBand> ladder,
    float q,
    float blendWindowFraction = 0.15f) noexcept;

struct LodUploadDecision {
    bool emitInstance = true;
    uint8_t uploadSet = 0;
    uint16_t blockMask = 0;
};

[[nodiscard]] LodUploadDecision GateLodUploads(
    const LodBand& band,
    bool graceFrame = false,
    bool previousWasSkipped = false) noexcept;

} // namespace Vixen::SVO
