#pragma once

#include "Data/ReconstructionJitter.h"

#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <cstdint>

namespace Vixen::RenderGraph {

inline constexpr uint32_t kPickMissSentinel = 0xFFFFFFFFu;

// One jittered primary-ray result at a read pixel. `address` is (brickIndex << 10) | (voxelIdx & 0x3FF)
// and `instance` the body instance index, both kPickMissSentinel on a miss (the pick image's packing).
// `jitter` is the sub-pixel offset (input-pixel units) the ray was traced with.
struct PickVoteSample {
    uint32_t address = kPickMissSentinel;
    uint32_t instance = kPickMissSentinel;
    glm::vec2 jitter{0.0f};
};

// Gaussian falloff of the ray's distance from the pixel centre, sigma = 0.5 px. The jitter spans
// +-0.5 px per axis, so sigma is the half pixel: a ray through the pixel centre counts 1.0, one at the
// pixel edge 0.61, one at the corner 0.37. A hard cutoff would let a corner ray out-vote a centred
// one by count alone; Gaussian stays smooth and positive, so every sample contributes and ties are rare.
inline constexpr float kPickVoteSigma = 0.5f;
inline float PickVoteWeight(const glm::vec2& jitter) {
    return std::exp(-glm::dot(jitter, jitter) / (2.0f * kPickVoteSigma * kPickVoteSigma));
}

// What the motion vector and validity under a sample say about the pixel.
//   Moved   - the camera or the instance under the pixel moved, so older samples are another surface.
//   Still   - the surface at this pixel did not move.
//   Unknown - no evidence (miss, or the motion texel was not read).
enum class PickMotion { Unknown, Still, Moved };

// Motion is previous UV minus current UV (unjittered cameras); validity 0 means no correspondence.
inline constexpr float kPickMotionTolerance = 0.05f;  // texels of render extent
inline PickMotion ClassifyPickMotion(bool hit, const glm::vec2& motionUv, float validity, const glm::vec2& extent) {
    if (!hit) return PickMotion::Unknown;
    if (validity < 0.5f) return PickMotion::Moved;
    const glm::vec2 texels = glm::abs(motionUv * extent);
    return std::fmax(texels.x, texels.y) > kPickMotionTolerance ? PickMotion::Moved : PickMotion::Still;
}

struct PickVoteResult {
    uint32_t address = kPickMissSentinel;
    uint32_t instance = kPickMissSentinel;
    float weight = 0.0f;       // summed weight of the winning ID
    uint32_t sampleCount = 0;  // samples that voted
    bool unanimous = false;    // every sample carried the winning ID
    bool Valid() const { return sampleCount != 0; }
    bool Hit() const { return Valid() && address != kPickMissSentinel; }
};

// Last kReconstructionJitterCycle samples at one pixel, one jitter cycle.
class PickVoteHistory {
public:
    void Reset() { count_ = 0; next_ = 0; }
    uint32_t Count() const { return count_; }

    // Without jitter every ray goes through the pixel centre: the vote is the current hit alone.
    void Observe(const PickVoteSample& sample, PickMotion motion, bool jittered) {
        if (!jittered || motion == PickMotion::Moved) Reset();
        samples_[next_] = sample;
        next_ = (next_ + 1u) % kReconstructionJitterCycle;
        if (count_ < kReconstructionJitterCycle) ++count_;
    }

    // Largest summed weight wins; an equal sum goes to the more recent ID.
    PickVoteResult Winner() const {
        PickVoteResult best;
        best.sampleCount = count_;
        for (uint32_t age = 0; age < count_; ++age) {  // newest first
            const PickVoteSample& candidate = At(age);
            float sum = 0.0f;
            for (uint32_t other = 0; other < count_; ++other) {
                const PickVoteSample& s = At(other);
                if (s.address == candidate.address && s.instance == candidate.instance) sum += PickVoteWeight(s.jitter);
            }
            if (sum > best.weight) {
                best.address = candidate.address;
                best.instance = candidate.instance;
                best.weight = sum;
            }
        }
        best.unanimous = true;
        for (uint32_t i = 0; i < count_; ++i) {
            best.unanimous = best.unanimous && At(i).address == best.address && At(i).instance == best.instance;
        }
        return best;
    }

private:
    const PickVoteSample& At(uint32_t age) const {
        return samples_[(next_ + kReconstructionJitterCycle - 1u - age) % kReconstructionJitterCycle];
    }

    std::array<PickVoteSample, kReconstructionJitterCycle> samples_{};
    uint32_t count_ = 0;
    uint32_t next_ = 0;
};

} // namespace Vixen::RenderGraph
