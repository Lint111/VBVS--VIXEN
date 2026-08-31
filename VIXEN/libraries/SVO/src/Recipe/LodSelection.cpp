#include "Recipe/LodSelection.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Vixen::SVO {
namespace {

[[nodiscard]] uint32_t FloatBits(float value) noexcept {
    return std::bit_cast<uint32_t>(value);
}

} // namespace

bool IsPositiveInfinity(float value) noexcept {
    return FloatBits(value) == 0x7F800000u;
}

bool IsFiniteFloat(float value) noexcept {
    return (FloatBits(value) & 0x7F800000u) != 0x7F800000u;
}

bool IsNaNFloat(float value) noexcept {
    const uint32_t bits = FloatBits(value);
    return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
}

namespace {

[[nodiscard]] bool IsValidStrategy(uint8_t raw) noexcept {
    return raw <= static_cast<uint8_t>(LodStrategy::Skip);
}

[[nodiscard]] float SafeQ(float q) noexcept {
    if (IsNaNFloat(q)) return 0.0f;
    return std::max(0.0f, q);
}

[[nodiscard]] float SmoothStep(float edge0, float edge1, float value) noexcept {
    if (!(edge1 > edge0)) return value >= edge1 ? 1.0f : 0.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

LodValidationError ValidateLodLadder(std::span<const LodBand> ladder) noexcept {
    if (ladder.empty()) return LodValidationError::Empty;
    if (ladder.size() > kMaxLodBands) return LodValidationError::TooManyBands;

    float previousMaxQ = -std::numeric_limits<float>::infinity();
    uint8_t previousUploadSet = kAllLodUploadUnits;
    for (std::size_t i = 0; i < ladder.size(); ++i) {
        const LodBand& band = ladder[i];
        if (IsNaNFloat(band.maxQ) || band.maxQ < 0.0f)
            return LodValidationError::NegativeOrNaNThreshold;
        if (i != 0 && !(band.maxQ > previousMaxQ))
            return LodValidationError::NonAscendingThresholds;
        if (i + 1 == ladder.size() && !IsPositiveInfinity(band.maxQ))
            return LodValidationError::FinalBandMustBeInfinite;
        if (i + 1 != ladder.size() && IsPositiveInfinity(band.maxQ))
            return LodValidationError::NonAscendingThresholds;
        if (!IsValidStrategy(band.strategy)) return LodValidationError::InvalidStrategy;
        if (band.paramTier > static_cast<uint8_t>(LodParamTier::Half))
            return LodValidationError::InvalidParamTier;
        if ((band.uploadSet & previousUploadSet) != band.uploadSet)
            return LodValidationError::UploadSetNotMonotone;
        previousMaxQ = band.maxQ;
        previousUploadSet = band.uploadSet;
    }
    return LodValidationError::None;
}

std::size_t SelectLodBand(std::span<const LodBand> ladder, float q) noexcept {
    if (ladder.empty()) return 0;
    const float safeQ = SafeQ(q);
    for (std::size_t i = 0; i < ladder.size(); ++i) {
        if (safeQ < ladder[i].maxQ) return i;
    }
    return ladder.size() - 1;
}

std::size_t SelectLodBandForRegime(
    std::span<const LodBand> ladder, float q, BodyFootprintRegime regime) noexcept {
    if (ladder.empty()) return 0;

    const std::size_t qBand = SelectLodBand(ladder, q);
    const std::size_t regimeFloor = std::min(
        static_cast<std::size_t>(regime), ladder.size() - 1);
    return std::max(qBand, regimeFloor);
}

std::size_t SelectLodBand(
    std::span<const LodBand> ladder,
    float q,
    float cameraDistance,
    float bodyRadius) noexcept {
    return SelectLodBandForRegime(
        ladder, q, ClassifyBodyFootprintRegime(cameraDistance, bodyRadius));
}

float ComputeLodQ(float entryDistance, float boundRadius, const LODParameters& camera) noexcept {
    if (!(boundRadius > 0.0f) || !IsFiniteFloat(boundRadius)) return 0.0f;
    const float distance = std::max(0.0f, entryDistance);
    const float footprint = std::max(0.0f, camera.getProjectedPixelSize(distance));
    return footprint / (2.0f * boundRadius);
}

float ComputeLodQ(
    const glm::vec3& cameraPosition,
    const glm::vec3& bodyPosition,
    float boundRadius,
    const LODParameters& camera) noexcept {
    const float centerDistance = glm::length(bodyPosition - cameraPosition);
    return ComputeLodQ(std::max(0.0f, centerDistance - boundRadius), boundRadius, camera);
}

LodTransition::LodTransition(LodTransitionConfig config) noexcept : config_(config) {
    config_.hysteresisFraction = std::clamp(config_.hysteresisFraction, 0.0f, 1.0f);
    config_.blendWindowFraction = std::clamp(config_.blendWindowFraction, 0.0f, 1.0f);
}

LodBandSelection LodTransition::Update(
    std::span<const LodBand> ladder, float q) noexcept {
    LodBandSelection result{};
    result.q = SafeQ(q);
    if (ladder.empty()) return result;

    const std::size_t desiredBand = SelectLodBand(ladder, result.q);
    return UpdateDesiredBand(ladder, result.q, desiredBand);
}

LodBandSelection LodTransition::UpdateDesiredBand(
    std::span<const LodBand> ladder,
    float q,
    std::size_t desiredBand) noexcept {
    LodBandSelection result{};
    result.q = SafeQ(q);
    if (ladder.empty()) return result;

    desiredBand = std::min(desiredBand, ladder.size() - 1);
    if (!initialized_) {
        currentBand_ = desiredBand;
        pendingBand_ = desiredBand;
        pendingFrames_ = 0;
        initialized_ = true;
        result.bandIndex = currentBand_;
        return result;
    }

    currentBand_ = std::min(currentBand_, ladder.size() - 1);
    if (desiredBand != currentBand_) {
        const std::size_t edgeBand = desiredBand > currentBand_ ? currentBand_ : desiredBand;
        const float edge = ladder[edgeBand].maxQ;
        const bool beyondDeadZone = desiredBand > currentBand_
            ? result.q >= edge * (1.0f + config_.hysteresisFraction)
            : result.q <= edge * (1.0f - config_.hysteresisFraction);

        if (!beyondDeadZone) {
            pendingBand_ = currentBand_;
            pendingFrames_ = 0;
        } else if (pendingBand_ != desiredBand) {
            pendingBand_ = desiredBand;
            pendingFrames_ = 1;
        } else {
            ++pendingFrames_;
        }

        if (beyondDeadZone &&
            (config_.dwellFrames <= 1 || pendingFrames_ >= config_.dwellFrames)) {
            currentBand_ = desiredBand;
            pendingBand_ = desiredBand;
            pendingFrames_ = 0;
            result.changed = true;
        }
    } else {
        pendingBand_ = currentBand_;
        pendingFrames_ = 0;
    }

    result.bandIndex = currentBand_;
    return result;
}

LodBandSelection LodTransition::Update(
    std::span<const LodBand> ladder,
    float q,
    float cameraDistance,
    float bodyRadius) noexcept {
    LodBandSelection result{};
    result.q = SafeQ(q);
    if (ladder.empty()) return result;

    const BodyFootprintRegime regime = ClassifyBodyFootprintRegime(cameraDistance, bodyRadius);
    const std::size_t desiredBand = SelectLodBand(
        ladder, result.q, cameraDistance, bodyRadius);

    // A regime is an authority floor, not a visual-hysteresis suggestion.  If the
    // camera jumps across a scale boundary, immediately retire any finer band before
    // applying the ordinary q hysteresis to movement within the coarser regime.
    const std::size_t regimeFloor = std::min(
        static_cast<std::size_t>(regime), ladder.size() - 1);
    const bool forcedCoarsening = initialized_ && currentBand_ < regimeFloor;
    if (forcedCoarsening) {
        currentBand_ = regimeFloor;
        pendingBand_ = regimeFloor;
        pendingFrames_ = 0;
    }

    result = UpdateDesiredBand(ladder, result.q, desiredBand);
    result.changed = result.changed || forcedCoarsening;
    return result;
}

void LodTransition::Reset() noexcept {
    currentBand_ = 0;
    pendingBand_ = 0;
    pendingFrames_ = 0;
    initialized_ = false;
}

void LodTransition::Reset(std::size_t bandIndex) noexcept {
    currentBand_ = bandIndex;
    pendingBand_ = bandIndex;
    pendingFrames_ = 0;
    initialized_ = true;
}

LodTransitionBlend ComputeLodBlend(
    std::span<const LodBand> ladder,
    float q,
    float blendWindowFraction) noexcept {
    LodTransitionBlend result{};
    if (ladder.size() < 2) return result;

    const float safeQ = SafeQ(q);
    const float beta = std::clamp(blendWindowFraction, 0.0f, 1.0f);
    for (std::size_t finer = 0; finer + 1 < ladder.size(); ++finer) {
        const float edge = ladder[finer].maxQ;
        if (!IsFiniteFloat(edge)) continue;
        const float lo = edge * (1.0f - beta);
        const float hi = edge * (1.0f + beta);
        if (safeQ < lo || safeQ > hi) continue;

        result.active = true;
        result.finerBand = finer;
        result.coarserBand = finer + 1;
        result.coarserWeight = SmoothStep(lo, hi, safeQ);
        return result;
    }
    return result;
}

LodUploadDecision GateLodUploads(
    const LodBand& band,
    bool graceFrame,
    bool previousWasSkipped) noexcept {
    const bool skipped = static_cast<LodStrategy>(band.strategy) == LodStrategy::Skip;
    if (skipped && !graceFrame) return {false, 0, 0};

    uint8_t uploadSet = band.uploadSet;
    if (previousWasSkipped && !skipped)
        uploadSet = static_cast<uint8_t>(uploadSet | LodUploadBit(LodUploadUnit::Instance));
    return {true, uploadSet, band.blockMask};
}

} // namespace Vixen::SVO
