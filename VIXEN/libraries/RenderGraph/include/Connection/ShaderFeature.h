#pragma once

// Semantic Shader Wiring — typed shader features (user direction, 2026-08-03:
// "the feature set should be a proper type instead of a string; if a feature
// requires a device or other kind of capabilities we can add an array of
// requirements referencing the requirement graph").
//
// A ShaderFeature's IDENTITY is its GLSL define — that string crosses the
// shader/codegen boundary and never goes away. The TYPE wraps it with an
// array of CapabilityGraph node names, so "can this feature be on, on this
// device" is answered by the same requirement graph everything else uses —
// not by scattered env checks. No-strings-for-types at the call sites; the
// define string survives only at the generated-header boundary.

#include "CapabilityGraph.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace Vixen::RenderGraph {

/**
 * @brief A shader feature axis: one GLSL define + its capability requirements.
 */
struct ShaderFeature {
    const char* define;                                 // GLSL define (identity)
    const char* const* requiredCapabilities = nullptr;  // CapabilityGraph node names
    uint32_t requiredCapabilityCount = 0;
};

// ---------------------------------------------------------------------------
// Engine feature vocabulary — ONE declaration per axis define (the same axes
// shaders/sdi-variants.json enumerates for the merged-SDI variant compiles).
// A feature that gains a device requirement adds it HERE, and every
// EnableIfAvailable call site inherits the gate.
// ---------------------------------------------------------------------------

inline constexpr ShaderFeature kFeatureGpuTraceHooks{"VIXEN_GPU_TRACE_HOOKS"};
inline constexpr ShaderFeature kFeatureB1OcclusionCull{"VIXEN_B1_OCCLUSION_CULL"};
// Wavefront W2a: ShadowVisibilityWave's SECOND dispatch — the post-gather
// reservoir phase (answers HitRecord._pad0[2] bit 4 from the combined
// reservoir). Same program, own compiled variant; no device requirement.
inline constexpr ShaderFeature kFeatureWaveReservoirPhase{"VIXEN_WAVE_RESERVOIR_PHASE"};
// kFeatureHitAccumFused (VIXEN_HIT_ACCUM_FUSED) RETIRED at W-SPLIT: the
// accumulate tail moved back to its own HitAccumulate.comp dispatch — the
// fusion cost more (+7-8 ms structural) than the re-read it was meant to
// avoid (~2 ms). The wave is unconditionally plain again.

/** @brief W-LEAN L3: SpatialReuseShade's cell-resolve fold (the retired
 *  standalone HitAccumResolve stage as a tail of the shade — bindings 36/37/38
 *  gated on this). */
inline constexpr ShaderFeature kFeatureSrsCellResolve{"VIXEN_SRS_CELL_RESOLVE"};

/**
 * @brief The set of active shader features for a stage/frame.
 *
 * Typed at the call sites; generated merged-SDI headers speak defines, so
 * membership is queried by define at that boundary.
 */
class SdiFeatureSet {
public:
    SdiFeatureSet() = default;

    void Enable(const ShaderFeature& feature) { defines_.insert(feature.define); }

    /**
     * @brief Enable only when every required capability is available.
     * @return true if enabled (requirement-free features always enable).
     */
    bool EnableIfAvailable(const ShaderFeature& feature,
                           const Vixen::CapabilityGraph& capabilities) {
        for (uint32_t i = 0; i < feature.requiredCapabilityCount; ++i) {
            if (!capabilities.IsCapabilityAvailable(feature.requiredCapabilities[i])) {
                return false;
            }
        }
        Enable(feature);
        return true;
    }

    bool Contains(const char* define) const { return defines_.count(define) > 0; }
    bool Empty() const { return defines_.empty(); }

private:
    std::unordered_set<std::string> defines_;
};

} // namespace Vixen::RenderGraph
