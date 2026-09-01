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
// E1-T1 stencil slice 0: image-inert composition histograms in the existing
// TraceBufferHeader terminal-pixel ring. Env-gated; no capability requirement.
inline constexpr ShaderFeature kFeatureCompositionCounters{"VIXEN_COMPOSITION_COUNTERS"};
// E7-T1: materialize the primary ray's policy stencil in HitRecord._pad0[2]
// bits 8..15 and preserve/read back that byte across both shadow writers.
// Env-gated; no capability or descriptor-interface requirement.
inline constexpr ShaderFeature kFeaturePolicyStencil{"VIXEN_POLICY_STENCIL"};
// E11-T1: reduce the per-pixel policy stencil (VIXEN_POLICY_STENCIL) into a
// per-8x8-tile word (one word per BodyInstanceRayMarch workgroup) and use it
// to skip ShadowVisibilityWave's evaluator work for source axes provably
// absent from the whole tile. Adds a new binding (PolicyStencilTileBuffer),
// so unlike kFeaturePolicyStencil this axis IS listed in sdi-variants.json.
// Requires kFeaturePolicyStencil; env-gated, no capability requirement.
inline constexpr ShaderFeature kFeaturePolicyStencilTiles{"VIXEN_POLICY_STENCIL_TILES"};
inline constexpr ShaderFeature kFeatureB1OcclusionCull{"VIXEN_B1_OCCLUSION_CULL"};
// Raster-proxy B2: same-frame graphics pre-pass emits a compact union interval
// plus ordered 192-bit candidate mask consumed only by the primary march.
inline constexpr ShaderFeature kFeatureB2ProxyPrepass{"VIXEN_B2_PROXY_PREPASS"};
// Wavefront W2a: ShadowVisibilityWave's SECOND dispatch — the post-gather
// reservoir phase (answers HitRecord._pad0[2] bit 4 from the combined
// reservoir). Same program, own compiled variant; no device requirement.
inline constexpr ShaderFeature kFeatureWaveReservoirPhase{"VIXEN_WAVE_RESERVOIR_PHASE"};
// kFeatureHitAccumFused (VIXEN_HIT_ACCUM_FUSED) RETIRED at W-SPLIT: the
// accumulate tail moved back to its own HitAccumulate.comp dispatch — the
// fusion cost more (+7-8 ms structural) than the re-read it was meant to
// avoid (~2 ms). The wave is unconditionally plain again.

/** @brief B2 (docs/plans/2026-08-04-wavefront-recipe-shading.md): shared-
 *  memory same-key pre-merge inside HitAccumulate.comp's own 64-wide
 *  workgroup — one global atomic per distinct table slot per workgroup
 *  instead of one per thread. No device requirement (plain shared memory +
 *  barrier()). */
inline constexpr ShaderFeature kFeatureHitAccumPremerge{"VIXEN_HIT_ACCUM_PREMERGE"};

/** @brief W-LEAN L3: SpatialReuseShade's cell-resolve fold (the retired
 *  standalone HitAccumResolve stage as a tail of the shade — bindings 36/37/38
 *  gated on this). */
inline constexpr ShaderFeature kFeatureSrsCellResolve{"VIXEN_SRS_CELL_RESOLVE"};

/** @brief W-BRICKMAP Slice 2: coarse-grid DDA backend for the ESVO leaf march
 *  (traverseCoarseGridInstancedSdf[AnyHit] in SceneBindings.glsl, FORMAT_STORED_SDF
 *  only — FORMAT_BINARY bodies fall back to the ESVO path per-instance at
 *  runtime, since brickGridLookup/brickLookupBase are only populated by the
 *  SDF serialization path (round-3 retarget; round 2 had this inverted).
 *  Env-gated at the BuildRenderGraph march registration site via
 *  VIXEN_BRICKMAP_TRAVERSAL; flag-off (env unset) never pushes this feature,
 *  so the source is byte-identical to pre-slice-2. */
inline constexpr ShaderFeature kFeatureBrickmapTraversal{"VIXEN_BRICKMAP_TRAVERSAL"};

/** @brief W-BRICKMAP Gate-B bisection: closest-hit-only diagnostic overwrite of
 *  hitColor for a hardcoded pixel rect around the two Gate-B divergent silhouette
 *  pixels (see traverseCoarseGridInstancedSdf's VIXEN_BRICKMAP_DEBUG block,
 *  SceneBindings.glsl). Env-gated separately from VIXEN_BRICKMAP_TRAVERSAL so a
 *  non-debug ON boot is unaffected; requires VIXEN_BRICKMAP_TRAVERSAL to also be
 *  set (the debug block lives inside the DDA backend it instruments). */
inline constexpr ShaderFeature kFeatureBrickmapDebug{"VIXEN_BRICKMAP_DEBUG"};

/** @brief W-RTQUERY Slice A: VK_KHR_ray_query per-brick-AABB TLAS traversal backend
 *  (traverseRayQueryInstancedSdf[AnyHit] in RayQueryTraversal.glsl, FORMAT_STORED_SDF
 *  only -- same scope restriction as kFeatureBrickmapTraversal, since the TLAS is built
 *  from the identical brickGridLookup source). Env-gated at the BuildRenderGraph march
 *  registration site via VIXEN_RTQUERY_TRAVERSAL; flag-off never pushes this feature, so
 *  the source is byte-identical to pre-W-RTQUERY. When BOTH VIXEN_BRICKMAP_TRAVERSAL and
 *  VIXEN_RTQUERY_TRAVERSAL are set, RTQUERY wins (see TraceWorld.glsl's dispatch order) --
 *  this is a THIRD search backend (ESVO / DDA / RT), not a replacement, isolating the
 *  search phase across all three while the sampling tail (marchBrickSdfCell) stays shared.
 */
inline constexpr ShaderFeature kFeatureRtQueryTraversal{"VIXEN_RTQUERY_TRAVERSAL"};

/** @brief W-COMPOSED: the role ruling made code -- RT-traversal + DDA-leaf +
 *  ESVO-data-access are complementary tiers of ONE traversal, not rival
 *  backends. Env-gated via VIXEN_COMPOSED_TRAVERSAL at the BuildRenderGraph
 *  march registration site: picks EITHER kFeatureRtQueryTraversal (device has
 *  RTXCapabilities.rayQuery) OR kFeatureBrickmapTraversal (software DDA
 *  fallback) as the near-field search phase -- never both, no new GLSL
 *  branching, the existing TraceWorld.glsl #ifdef/#elif chain already
 *  expresses the choice. The far-field (footprint > brick) tier mirrors the
 *  ESVO screen-space cutoff (pc.raySizeCoef) at the candidate-cell level in
 *  both traverseCoarseGridInstancedSdf (SceneBindings.glsl) and
 *  traverseRayQueryWorld (RayQueryTraversal.glsl). The three single-backend
 *  flags (kFeatureBrickmapTraversal/kFeatureRtQueryTraversal/ESVO-default)
 *  keep working unchanged for A/B instrumentation -- this is a fourth,
 *  additive axis, not a replacement. */
inline constexpr ShaderFeature kFeatureComposedTraversal{"VIXEN_COMPOSED_TRAVERSAL"};

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
