// ============================================================================
// EmitterSampling.glsl - count-agnostic emitter sampling (R502 / R502.2, S0)
// ============================================================================
// GPU twin of libraries/SVO/include/EmitterSampling.h. SYNC CONTRACT: every function
// below is a line-by-line port of its CPU mirror; test_emitter_sampling.cpp (seeded
// replay + closed-form checks) is the red-green gate. Change both together.
//
// Three entry points, none of which knows how many emitters exist:
//   EmitterSample sampleEmitter(inout uint rng);
//   float         emitterPHat(EmitterSample s, vec3 x, vec3 n);
//   float         emitterTotalPower();
//
// The including shader must, BEFORE including this file:
//   #include "SVOTypes.glsl"                       (getChildPointer/getValidMask/getLeafMask)
//   #include "Generated/EmitterSample.glsl"  "Generated/EmitterTotals.glsl"
//   #include "Generated/RecipeEmitterEntry.glsl" "Generated/SparseEmitterEntry.glsl"
//   #include "ReservoirCombine.glsl"               (pcgHash / rngNextFloat)
// and define these hooks (it owns the bindings):
//   EmitterTotals        emitterGetTotals();
//   RecipeEmitterEntry   emitterRecipeEntry(uint i);
//   uint                 emitterSurfaceCell(uint i);
//   SparseEmitterEntry   emitterSparseEntry(uint i);
//   uint                 emitterLaneWord(uint octreeIdx, uint wordIdx);   // sparse power lane
//   uvec2                emitterNode(uint octreeIdx, uint nodeIdx);       // ESVO descriptor
//   float                emitterRecipeSd(uint instIdx, vec3 pLocal);      // recipe-local signed distance
//   vec3                 emitterRecipeRadiance(uint instIdx);             // color * recipeParams[3]
//   vec3                 emitterToWorldPoint(uint instIdx, vec3 pLocal);  // instance-local -> world
//   vec3                 emitterRecipeNormal(uint instIdx, vec3 worldPos);// outward world normal
//
// Sparse power lane word layout (EmitterPower.h): [0]=leafCount [1]=rootPower [2]=nodeCount
// [3..3+N)=phi per node, then leafCount ascending leaf node ordinals, then 8 octant sums
// per leaf. The lane holds octree-normalized area x emission; kEmitterWorldGridSize maps
// the octree to instance-local space.
//
// Draw order (fixed per pipeline): u0 pipeline; recipe: u1 instance, u2 cell, u3..u5 point;
// sparse: u1 instance, one fresh draw per tree level (a single rescaled draw runs out of
// float mantissa after ~8 levels), one octant draw, three point draws.
// Sample key: [1:0] pipeline (1 recipe, 2 sparse) | [9:2] instIdx | [31:10] origin
// (recipe: surface cell index; sparse: leaf node ordinal << 3 | octant).
// ============================================================================
#ifndef EMITTER_SAMPLING_GLSL
#define EMITTER_SAMPLING_GLSL

const float kEmitterPi = 3.14159265358979323846;
const float kEmitterWorldGridSize = 10.0;
const uint  kEmitterHeaderWords = 3u;
const uint  kEmitterMaxDepth = 24u;

float emitterLuminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// rngNextFloat can round up to exactly 1.0 (float(2^32 - 1) == 2^32); cap it.
float emitterNext(inout uint rng) {
    return min(rngNextFloat(rng), 0.99999994);
}

float emitterLaneFloat(uint octreeIdx, uint wordIdx) {
    return uintBitsToFloat(emitterLaneWord(octreeIdx, wordIdx));
}

vec3 emitterOctantOffset(uint octant) {
    return vec3(float(octant & 1u), float((octant >> 1u) & 1u), float((octant >> 2u) & 1u));
}

uint emitterPickRecipe(float target, uint count) {
    uint lo = 0u;
    uint hi = count;
    while (lo < hi) {
        uint mid = (lo + hi) >> 1u;
        if (emitterRecipeEntry(mid).cdf <= target) lo = mid + 1u; else hi = mid;
    }
    return min(lo, count - 1u);
}

uint emitterPickSparse(float target, uint count) {
    uint lo = 0u;
    uint hi = count;
    while (lo < hi) {
        uint mid = (lo + hi) >> 1u;
        if (emitterSparseEntry(mid).cdf <= target) lo = mid + 1u; else hi = mid;
    }
    return min(lo, count - 1u);
}

EmitterSample emitterEmptySample() {
    EmitterSample s;
    s.position = vec3(0.0);
    s.key = 0u;
    s.radiance = vec3(0.0);
    s.pdf = 0.0;
    return s;
}

vec3 emitterRecipeGradient(uint instIdx, vec3 p, float h) {
    float dx = emitterRecipeSd(instIdx, p + vec3(h, 0.0, 0.0)) - emitterRecipeSd(instIdx, p - vec3(h, 0.0, 0.0));
    float dy = emitterRecipeSd(instIdx, p + vec3(0.0, h, 0.0)) - emitterRecipeSd(instIdx, p - vec3(0.0, h, 0.0));
    float dz = emitterRecipeSd(instIdx, p + vec3(0.0, 0.0, h)) - emitterRecipeSd(instIdx, p - vec3(0.0, 0.0, h));
    vec3 g = vec3(dx, dy, dz);
    float len = length(g);
    return len > 1e-12 ? g / len : vec3(0.0, 1.0, 0.0);
}

EmitterSample emitterSampleRecipe(inout uint rng, float pPipeline) {
    EmitterTotals t = emitterGetTotals();
    float u1 = emitterNext(rng);
    float u2 = emitterNext(rng);
    float u3 = emitterNext(rng);
    float u4 = emitterNext(rng);
    float u5 = emitterNext(rng);

    uint k = emitterPickRecipe(u1 * t.phiRecipe, t.recipeEntryCount);
    RecipeEmitterEntry e = emitterRecipeEntry(k);

    vec3 pLocal;
    uint origin = 0u;
    if (e.cellCount > 0u) {
        uint pick = min(uint(u2 * float(e.cellCount)), e.cellCount - 1u);
        uint ci = emitterSurfaceCell(e.cellOffset + pick);
        uint dim = e.gridDim;
        uint cx = ci % dim;
        uint cy = (ci / dim) % dim;
        uint cz = ci / (dim * dim);
        pLocal = e.gridAabbMin + (vec3(float(cx), float(cy), float(cz)) + vec3(u3, u4, u5)) * e.cellSize;
        float h = 0.25 * e.cellSize;
        for (uint i = 0u; i < 3u; ++i) {
            float sd = emitterRecipeSd(e.instIdx, pLocal);
            pLocal -= sd * emitterRecipeGradient(e.instIdx, pLocal, h);
        }
        origin = ci;
    } else {
        // No occupancy grid: gridAabbMin = bound centre, cellSize = bound radius; sample the
        // bound sphere (conservative, flagged by flags bit 0).
        float z = 1.0 - 2.0 * u2;
        float r = sqrt(max(0.0, 1.0 - z * z));
        float ph = 2.0 * kEmitterPi * u3;
        pLocal = e.gridAabbMin + e.cellSize * vec3(r * cos(ph), r * sin(ph), z);
    }

    EmitterSample s;
    s.position = emitterToWorldPoint(e.instIdx, pLocal);
    s.radiance = emitterRecipeRadiance(e.instIdx);
    s.pdf = pPipeline * (e.phi / t.phiRecipe) / e.areaInst;
    s.key = 1u | ((e.instIdx & 0xFFu) << 2u) | ((origin & 0x3FFFFFu) << 10u);
    return s;
}

EmitterSample emitterSampleSparse(inout uint rng, float pPipeline) {
    EmitterTotals t = emitterGetTotals();
    float u1 = emitterNext(rng);
    uint k = emitterPickSparse(u1 * t.phiSparse, t.sparseEntryCount);
    SparseEmitterEntry e = emitterSparseEntry(k);
    uint oi = e.octreeIdx;

    float rootPhi = emitterLaneFloat(oi, 1u);
    uint leafCount = emitterLaneWord(oi, 0u);
    uint nodeCount = emitterLaneWord(oi, 2u);
    if (!(rootPhi > 0.0) || leafCount == 0u) return emitterEmptySample();

    // Stochastic light-tree descent: child chosen in proportion to its stored power.
    uint node = 0u;
    vec3 cellMin = vec3(0.0);
    float size = 1.0;
    bool atLeaf = false;
    for (uint depth = 0u; depth < kEmitterMaxDepth && !atLeaf; ++depth) {
        uvec2 d = emitterNode(oi, node);
        uint valid = getValidMask(d);
        uint leafMask = getLeafMask(d);
        uint nonLeaf = valid & ~leafMask;
        uint totalNonLeaf = bitCount(nonLeaf);
        float target = emitterNext(rng) * emitterLaneFloat(oi, kEmitterHeaderWords + node);

        float acc = 0.0;
        bool have = false;
        uint chosenOct = 0u;
        uint chosenIdx = 0u;
        bool chosenLeaf = false;
        for (uint oct = 0u; oct < 8u; ++oct) {
            if (((valid >> oct) & 1u) == 0u) continue;
            uint below = (1u << oct) - 1u;
            bool isLeaf = ((leafMask >> oct) & 1u) != 0u;
            uint cidx = isLeaf ? getChildPointer(d) + totalNonLeaf + bitCount(valid & leafMask & below)
                               : getChildPointer(d) + bitCount(nonLeaf & below);
            float phi = emitterLaneFloat(oi, kEmitterHeaderWords + cidx);
            if (!(phi > 0.0)) continue;
            acc += phi;
            chosenOct = oct;
            chosenIdx = cidx;
            chosenLeaf = isLeaf;
            have = true;
            if (target < acc) break;
        }
        if (!have) return emitterEmptySample();
        cellMin += emitterOctantOffset(chosenOct) * (size * 0.5);
        size *= 0.5;
        node = chosenIdx;
        atLeaf = chosenLeaf;
    }
    if (!atLeaf) return emitterEmptySample();

    // Leaf slot by binary search over the ascending emissive-leaf ordinals.
    uint lo = 0u;
    uint hi = leafCount;
    while (lo < hi) {
        uint mid = (lo + hi) >> 1u;
        if (emitterLaneWord(oi, kEmitterHeaderWords + nodeCount + mid) < node) lo = mid + 1u; else hi = mid;
    }
    if (lo >= leafCount || emitterLaneWord(oi, kEmitterHeaderWords + nodeCount + lo) != node)
        return emitterEmptySample();
    uint octBase = kEmitterHeaderWords + nodeCount + leafCount + lo * 8u;

    float octTotal = 0.0;
    for (uint o = 0u; o < 8u; ++o) octTotal += emitterLaneFloat(oi, octBase + o);
    float octTarget = emitterNext(rng) * octTotal;
    float acc2 = 0.0;
    bool haveOct = false;
    uint octant = 0u;
    float octPhi = 0.0;
    for (uint o = 0u; o < 8u; ++o) {
        float phi = emitterLaneFloat(oi, octBase + o);
        if (!(phi > 0.0)) continue;
        acc2 += phi;
        octant = o;
        octPhi = phi;
        haveOct = true;
        if (octTarget < acc2) break;
    }
    if (!haveOct) return emitterEmptySample();

    float u3 = emitterNext(rng);
    float u4 = emitterNext(rng);
    float u5 = emitterNext(rng);
    float side = size * 0.5;
    vec3 pNorm = cellMin + emitterOctantOffset(octant) * side + vec3(u3, u4, u5) * side;

    // Cluster treated as a sphere emitter of area 3*side^2 (half the cube surface); the
    // radiance that conserves the stored power is phi_oct / (3*side^2), grey.
    float areaNorm = 3.0 * side * side;
    float areaWorld = areaNorm * e.areaScale;

    EmitterSample s;
    s.position = emitterToWorldPoint(e.instIdx, pNorm * kEmitterWorldGridSize);
    s.radiance = vec3(octPhi / areaNorm);
    s.pdf = pPipeline * (e.phi / t.phiSparse) * (octPhi / rootPhi) / areaWorld;
    s.key = 2u | ((e.instIdx & 0xFFu) << 2u) | ((((node & 0x7FFFFu) << 3u) | octant) << 10u);
    return s;
}

float emitterTotalPower() {
    EmitterTotals t = emitterGetTotals();
    return t.phiRecipe + t.phiSparse;
}

EmitterSample sampleEmitter(inout uint rng) {
    EmitterTotals t = emitterGetTotals();
    float total = t.phiRecipe + t.phiSparse;
    if (!(total > 0.0)) return emitterEmptySample();
    float u0 = emitterNext(rng);
    if (u0 * total < t.phiRecipe) return emitterSampleRecipe(rng, t.phiRecipe / total);
    return emitterSampleSparse(rng, t.phiSparse / total);
}

// Unshadowed target |L_e| * max(n.w, 0) * cosE / dist^2 at receiver x with normal n.
// Recipe emitters re-derive their normal from the key's instance (cosE); sparse clusters
// are sphere emitters (cosE = 1).
float emitterPHat(EmitterSample s, vec3 x, vec3 n) {
    if (!(s.pdf > 0.0)) return 0.0;
    vec3 d = s.position - x;
    float dist2 = max(dot(d, d), 1e-6);
    vec3 w = d * inversesqrt(dist2);
    float cosR = max(dot(n, w), 0.0);
    float cosE = 1.0;
    if ((s.key & 3u) == 1u) {
        cosE = max(dot(emitterRecipeNormal((s.key >> 2u) & 0xFFu, s.position), -w), 0.0);
    }
    return emitterLuminance(s.radiance) * cosR * cosE / dist2;
}

#endif // EMITTER_SAMPLING_GLSL
