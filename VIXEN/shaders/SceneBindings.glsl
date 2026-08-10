// ============================================================================
// SceneBindings.glsl - Shared scene bindings + traversal machinery
// ============================================================================
// Sampled Lighting Inc3 M1 (KI-018): extracted VERBATIM from BodyInstanceRayMarch.comp
// so both the march pass and the new DirectLighting.comp pass declare the SAME
// scene SSBOs/globals/traversal functions from ONE source instead of two
// hand-synced copies. TraceWorldShadow (TraceWorld.glsl, included at the end of
// this file) needs the FULL traversal machinery -- bodyInstances, the concatenated
// esvoNodes/brickData/materials/channelPool/brickLookup/mipPool/tierRefTable
// buffers, the g_octreeIdx/g_brickArrayBase globals, the octreeConfig macro, and
// every traversal helper down to traverseOctreeInstanced -- to cast a shadow ray
// against the same scene the march traversed. Byte-for-byte identical to the
// pre-extraction inline block; this is a pure move, not a rewrite (mirrors
// TraceWorld.glsl's own M1 "pure extraction" precedent).
//
// NOT included here (stay per-shader, differ by consumer):
//   - binding 0 (outputImage) / binding 9 (idOutputImage) -- march writes both;
//     DirectLighting.comp writes its own outputImage (same view) and does not
//     touch idOutputImage at all (the march already owns that write).
//   - bindings 16-21 (LightingConfig/HitRecord/ShadowConfig/AccumulationConfig/
//     historyImage/PrevCameraConfig) -- each shader declares only the ones it
//     actually reads/writes.
//
// Must be #included after SVOTypes.glsl/Materials.glsl/VoxelChannelFormat.glsl
// and the local_size_x/y/z layout qualifier (identical to the pre-extraction
// include order in BodyInstanceRayMarch.comp).
// ============================================================================

// W-RTQUERY Slice A fix 5: GL_EXT_ray_query must be declared as early as this
// file can offer -- RayQueryTraversal.glsl is #included ~400 lines below (after
// configs[]/brickLookup/StoredSdf's marchBrickSdfCell are already declared, its
// real dependency order), which put the #extension mid-translation-unit. Hoisted
// here, at this file's own head, guarded by the identical #ifdef so a flag-off
// build never emits it (byte-identical source in that case).
#ifdef VIXEN_RTQUERY_TRAVERSAL
#extension GL_EXT_ray_query : require
#endif

#include "SVOTypes.glsl"
#include "Materials.glsl"
#include "VoxelChannelFormat.glsl"   // Inc3 M3: SEM_* and FK_* defines
// Concatenated ESVO node descriptors for ALL octrees (≤3), with per-octree
// nodeArrayBase offsets stored in configs[i].nodeArrayBase.
layout(std430, binding = 1) readonly buffer ESVOBuffer {
    uvec2 esvoNodes[];
};

// Concatenated brick voxel data for ALL octrees; per-octree brickArrayBase
// stored in configs[i].brickArrayBase.
layout(std430, binding = 2) readonly buffer BrickBuffer {
    uint brickData[];
};

layout(std430, binding = 3) readonly buffer MaterialBuffer {
    Material materials[];
};

// ============================================================================
// STORED-SDF BUFFERS (Inc2 M3 — bindings 11 and 12)
// ============================================================================
// Bound as placeholders (1-byte SSBOs) for binary/Procedural bodies. The shader
// only accesses these when OctreeConfig.formatId == FORMAT_STORED_SDF (1u).
// Inc3 M3: binding 11 renamed sdfData → channelPool (generic multi-channel SoA pool).
layout(std430, binding = 11) readonly buffer ChannelPoolBuffer { float channelPool[]; };
layout(std430, binding = 12) readonly buffer BrickLookupBuffer { uint  brickLookup[]; };

// ============================================================================
// SPARSE-MIP ESVO LOD BUFFERS (Inc1 M3 — binding 13)
// ============================================================================
// Per-level filtered value sample, one per octree node, one lane per live
// channel — mipPool[mipPoolBase + nodeIdx*channelCount + channelIdx] packs
// a MipSample (2 floats: value, coverage). Bound as a 1-byte placeholder
// when a tree was never mip-baked (concatenated_.mipPool empty); the shader
// only reads it when a leaf's brick is not resident (Task 7) or the LOD
// cutoff says stop (Task 8) — dead code otherwise.
layout(std430, binding = 13) readonly buffer MipPoolBuffer { float mipPool[]; };

// ============================================================================
// PER-INSTANCE ITERATION DEBUG BUFFER (Inc1 M4b — binding 14)
// ============================================================================
// One uint per instance slot: the traversal iteration count that instance's
// traverseOctreeInstanced call performed for the CURRENT pixel's ray, written
// unconditionally each instance-loop iteration (last writer wins across the
// dispatch — only meaningful for a single-pixel dispatch, which is exactly
// what the occlusion-reject unit test uses). Bound as a 1-byte placeholder in
// production; the write is a single non-atomic store, negligible next to the
// traversal it instruments.
//
// Baked-perf-pipeline M2 (audit D1, Task 2.2): the 7 instanceIterCount[] stores
// in TraceWorld.glsl are UB when this buffer is bound as the 1-byte production
// placeholder (robustBufferAccess is NOT enabled — see VulkanDevice's feature
// setup) — the store address depends on instIdx, which can exceed byte 0. Gated
// on VIXEN_GPU_TRACE_HOOKS (same define as TraceRecording.glsl/
// snapshotTraversalState): the default-off path contains zero stores into this
// buffer. Tests that read instanceIterCount back (test_body_instance_occlusion_
// reject.cpp, test_tier_crossing_lod_residency.cpp) compile BodyInstanceRayMarch.comp
// with VIXEN_GPU_TRACE_HOOKS defined (see body_instance_raymarch_spv's glslc -D flag)
// and bind the buffer at its real size, so the UB case never arises in either
// configuration.
//
// Semantic-wiring S2 (2026-08-03): the DECLARATION now shares the same gate as
// the stores. The old unconditional-declaration defense ("the reflected
// interface never changes shape, unlike ENABLE_SHADER_COUNTERS' binding-8
// removal") predates the feature-tagged merged SDI: binding 14 is now a
// DECLARED feature member (FEATURES = {VIXEN_GPU_TRACE_HOOKS} in
// generated/sdi/merged/*, checked by ctest sdi_merged_drift_check), so the
// wiring layer sees exactly the members each compiled variant has — a
// feature-shaped interface is the CONTRACT now, not a drift hazard. Default
// builds drop the 1-byte placeholder binding (and its UB guard) entirely.
#ifdef VIXEN_GPU_TRACE_HOOKS
layout(std430, binding = 14) writeonly buffer InstanceIterDebugBuffer { uint instanceIterCount[]; };
#endif

// ============================================================================
// TIER-CROSSING REFERENCE TABLE (Tiered-ESVO Inc2 M3 — binding 15)
// ============================================================================
// One TierRef per registered tier-crossing leaf, concatenated across all
// resident octrees in the SAME order ConcatenatedOctrees::tierRefTable is
// built CPU-side (ShellOctreeGpu.h) — mirrors mipPool's per-octree-base +
// concatenated-buffer convention exactly. A farBit==1 leaf's contourPointer
// (getTierRefIndex above) is an index into the CURRENT octree's own slice,
// offset by configs[g_octreeIdx].tierRefTableBase — NOT a global index.
// Layout must be byte-for-byte identical to Vixen::SVO::TierRef (TierRef.h):
// childOctreeIndex(uint,@0), childOriginLocal(float[3],@4), childScale(float,@16),
// 20 bytes/element, no hidden padding (std430 does not pad a scalar array).
// Bound as a 1-byte placeholder when no tree in the scene has any
// tier-crossing leaves (concatenated_.tierRefTable empty) — the shader only
// reads it after confirming farBit==1 on an actual leaf hit, and bounds-checks
// against tierRefTable.length() below, exactly like MipPoolBuffer above.
struct TierRef {
    uint  childOctreeIndex;
    float childOriginLocal[3];
    float childScale;
};
layout(std430, binding = 15) readonly buffer TierRefTableBuffer { TierRef tierRefTable[]; };

// ============================================================================
// PER-INSTANCE SKIP BITMASK (Recipe-Live-App-Bucketed-Dispatch Inc4 M1 — binding 35)
// ============================================================================
// One bit per instance index (bit (instIdx & 31) of word instIdx>>5), read once at the
// top of TraceWorld's and TraceWorldShadow's instance-loop body: a set bit means "some
// OTHER pass already owns this instance this frame, skip it here" — an early-continue,
// cheap enough to be unconditional. Only a CONSUMER of this mechanism (a future
// bucketed-dispatch integration, not built in this milestone) ever sets a bit; this
// milestone only builds the read side + the skip semantics.
//
// Bound as a 256-byte ZEROED placeholder in every scene/test that doesn't populate it
// (both the production graph's own instance_skip_mask_buffer, BuildRenderGraph.cpp, and
// all 11 GTest Vulkan harnesses exercising this shader use the SAME 256-byte/zeroed
// convention — one no-op shape everywhere, not two). This differs from MipPoolBuffer/
// TierRefTableBuffer/OccupancyGridBuffer's 1-byte-when-empty convention (see those
// buffers' comments above) because those buffers are CONCATENATED CPU-side vectors that
// are sometimes genuinely empty (no tree ever produced data for them), whereas this
// buffer's placeholder is a fixed, always-allocated size independent of any CPU-side
// vector — 1 byte would be equally safe (the bounds-check below tolerates any size) but
// 256 bytes was chosen so production and every test harness share one literal buffer
// size. The read bounds-checks against skipMask.length() before indexing, so the no-op
// holds by CONTENT (every word is zero), not by length: with the 256-byte placeholder,
// skipMask.length()==64 (256B / 4B-per-uint) is nonzero, so the bounds-check passes and
// the read genuinely happens — it just always reads a zero word, so no bit is ever set
// and every instance is always marched. (A 1-byte placeholder would instead make the
// no-op hold via skipMask.length()==0 — a std430 runtime-sized array's element count
// over a too-small binding truncates to 0 — short-circuiting the bounds-check instead;
// both shapes are safe, this file's declared convention is the zeroed-content one.)
//
// Binding number 35 (not the next free number in THIS file's own local sequence, 23):
// SceneBindings.glsl is #included by DirectLighting.comp/ProbeUpdate.comp/
// SpatialReuseShade.comp TOO, each of which separately declares its OWN bindings in the
// 23-34 range (ReservoirConfigSSBO, LightTreeBufferSSBO, ProbeGridConfigSSBO, the DDGI
// leak-gate debug buffers, the probe irradiance/visibility atlases, ...) — since a single
// compiled shader's reflected descriptor set is the UNION of every binding declared in
// its translation unit, a number already used by one of those shaders would collide the
// moment this file's declaration and that shader's own declaration are both in scope.
// Confirmed via grep across every shaders/*.comp: 23-34 are all taken by at least one
// SceneBindings.glsl includer; 35 is free everywhere as of this milestone.
layout(std430, binding = 35) readonly buffer InstanceSkipMaskBuffer { uint skipMask[]; };

// Returns true iff instIdx's bit is set in skipMask[] — false (never skip) whenever the
// bound buffer is the 256-byte zeroed placeholder (every word reads 0, so no bit is ever
// set) or instIdx's word is simply beyond whatever was actually populated, so a caller
// that never populates this buffer at all gets byte-identical behavior to a build that
// never had this mechanism.
bool isInstanceSkipped(int instIdx) {
    uint wordIdx = uint(instIdx) >> 5u;
    if (wordIdx >= skipMask.length()) return false;
    uint bitIdx = uint(instIdx) & 31u;
    return (skipMask[wordIdx] & (1u << bitIdx)) != 0u;
}

// Recipe-Live-App-Bucketed-Dispatch Inc4 M2: true iff ANY instance is currently skip-masked
// (word-scanned, not per-instance — cheap since the mask covers at most 3*64=192 instances,
// i.e. 6 words, matching TraceWorld's own instance-count cap). Used by BodyInstanceRayMarch.comp
// to decide whether tier-0's own instance loop is exhaustive this frame: with the mask entirely
// empty (every word zero — the always-true case until M3 wires a real second writer), tier-0
// marches every instance and its own hit/miss determination is authoritative, so a miss must
// unconditionally clear any stale prior-frame HitRecord content. The MOMENT any instance is
// skip-masked, tier-0 is no longer exhaustive — a miss at a pixel whose only geometry was a
// skipped instance is NOT "nothing is here," it's "I didn't check this," and must defer to
// whatever a same-frame second writer (the skipped instance's own bucketed-dispatch pass)
// already wrote, rather than clobbering it. See BodyInstanceRayMarch.comp's HitRecord write for
// the full derivation (a test scene with a skip-masked instance + a stubbed second writer caught
// this exact miss-clobbers-hit bug during Inc4 M2's gate).
// Computed once per invocation (frame-global content, not a per-pixel quantity) rather than
// tracked as a local inside TraceWorld's instance loop -- deliberately, not an oversight. The two
// forms are PROVABLY the same value for every pixel today, not merely a close approximation of
// each other, because of three facts that all hold simultaneously in TraceWorld.glsl's instance
// loop (`for (int instIdx = 0; instIdx < numInstances; ++instIdx)`):
//   1. The loop bound (numInstances = clamp(pc.instanceCount, ...)) comes from a push constant --
//      frame-global, not per-pixel/per-ray.
//   2. isInstanceSkipped(instIdx) takes ONLY instIdx and the skip-mask buffer's content as input --
//      no per-pixel/per-ray input reaches it anywhere.
//   3. The loop body never `break`s or early-`return`s before reaching a later index (every branch
//      is `continue` or falls through) -- so every pixel's invocation visits the SAME index range
//      and evaluates the SAME skip decision per index, regardless of ray direction/origin.
// Given all three, "did this pixel's march skip an instance" cannot vary across pixels in the
// current code -- it's frame-global content computed identically no matter where in the shader you
// evaluate it, so hoisting it to one frame-level call here (instead of a per-invocation tracked
// bool threaded through both the procedural and ESVO branches of the loop) is zero behavioral
// difference, less surface area.
// THIS EQUIVALENCE BREAKS the moment ANY ONE of the three facts above stops holding -- e.g. a
// future change makes the instance range or skip decision genuinely ray/pixel-dependent, or adds an
// early-break/return to the loop before its last index (plausible territory for M3's real bucketing
// scheme). If you are that future change: this frame-global call is no longer correct and the
// "did I skip anything" signal MUST move to a real per-invocation tracked bool inside the loop
// instead -- re-derive from scratch, don't assume this function still applies unmodified.
bool anyInstanceSkipped() {
    uint wordCount = skipMask.length();
    // 3*64=192 instances / 32 bits-per-word = 6 words covers TraceWorld's own instance-count cap;
    // clamp defensively in case a future caller ever binds a larger buffer.
    uint scanWords = min(wordCount, 6u);
    for (uint w = 0u; w < scanWords; ++w) {
        if (skipMask[w] != 0u) return true;
    }
    return false;
}

#define FORMAT_BINARY     0u
#define FORMAT_STORED_SDF 1u

// TRACE-PLUMBING NOTE (S3 hazard observer, 2026-08-04): this declaration is
// deliberately UNCONDITIONAL and plain-`buffer` even though its only writers
// live in TraceRecording.glsl's VIXEN_GPU_TRACE_HOOKS blocks — the graph
// wiring must be variant-independent (the same bound buffer serves plain and
// trace-compiled shaders; the define decides whether writes exist). Qualifier-
// based access reflection therefore reads RW here in every variant, which the
// S3 hazard census would misread as an all-pairs cross-stage writer; the
// census excludes it by explicit annotation instead (see the lighting census
// sites in BuildRenderGraph.cpp).
layout(std430, binding = 4) buffer RayTraceBuffer {
    uint traceWriteIndex;
    uint traceCapacity;
    // W-COMPOSED round-3 fix item 3: unconditional far-field-cutoff counter
    // (mirrors C++ TraceBufferHeader::farFieldCount, DebugRaySample.h) --
    // NOT gated on VIXEN_GPU_TRACE_HOOKS/ENABLE_SHADER_COUNTERS, since both far-
    // field call sites (SceneBindings.glsl/RayQueryTraversal.glsl) increment it
    // unconditionally on every real firing. CPU-side Reset() (RayTraceBuffer.cpp)
    // deliberately never clears it, so it accumulates for the whole boot.
    uint farFieldCount;
    // Round-5 far-field diagnostics (validator-mandated): candidates REACHING the
    // gate test, vs farFieldCount above which only counts firings that PASS it.
    // Bumped immediately before each far-field if-test (RayQueryTraversal.glsl,
    // SceneBindings.glsl's traverseCoarseGridInstancedSdf) -- distinguishes "the
    // candidate loop yields nothing" from "the gate expression rejects everything"
    // per the round-5 interpretation tree. Same unconditional/never-reset
    // discipline as farFieldCount; repurposes the former _padding1 tail slot so
    // TraceBufferHeader stays 16 B (DebugRaySample.h).
    uint farFieldCandidates;
    // Round-6 DDA-threshold-degeneracy probe (blocker 1): min/max of BOTH
    // gate operands, bit-encoded via floatBitsToUint (values are always
    // non-negative distances -- raw bit order == float order, no sign trick
    // needed) and combined with atomicMin/atomicMax. lhs = the projected
    // footprint (worldDistToCell*raySizeCoef+raySizeBias); rhs = the actual
    // cell size (cellWorldSize). Never reset -- accumulates for the boot.
    uint farFieldLhsMinBits;
    uint farFieldLhsMaxBits;
    uint farFieldRhsMinBits;
    uint farFieldRhsMaxBits;
    // Round-6 blocker-2 localization probe: raw TLAS candidate-loop entries
    // (RayQueryTraversal.glsl's traverseRayQueryWorld, FIRST statement inside
    // while(rayQueryProceedEXT) -- before the AABB-type continue), separate
    // from farFieldCandidates above (which counts candidates reaching the
    // far-field GATE specifically, several continues later). Never reset.
    uint rtLoopEntries;
    // Round-7 blocker-1 probe: does descendToNodeOrdinal+shadeFromMipSample
    // actually resolve a shaded mip sample, or fall through to the
    // vec3(0.5)/-farDirN placeholder? Bumped immediately after that check in
    // both far-field twins (mirrors incrFarFieldCandidates' call-site
    // discipline). Discriminates hypothesis (a) from (b)/(c) -- see
    // farFieldMipSuccess/Fail in DebugRaySample.h's TraceBufferHeader. Never
    // reset.
    uint farFieldMipSuccess;
    uint farFieldMipFail;
    // Round-7 blocker-1 probe #2: how many times the M5/M5b tight-bounds
    // far-hit-rejection guard (BodyInstanceRayMarch.comp, "anyHit = false"
    // near the getOctreeTraceBounds check) actually discards a hit. If this
    // tracks farFieldCount closely, the far-field's own hitWorldPos is
    // routinely landing outside the winning instance's tight trace bounds --
    // the mechanism losing the visible result downstream of a successful mip
    // resolve. Never reset.
    uint farFieldRejectedByBounds;
    // Round-7 blocker-1 probe #3: how many far-field firings (g_lastHitWasFarField)
    // actually WIN TraceWorld's isCloserHit competition against every other
    // instance/candidate for the SAME pixel, i.e. become the pixel's final
    // bestInstIdx. Bumped in TraceWorld.glsl right after each call site's own
    // isCloserHit branch. If this stays near 0 while farFieldCount is huge, the
    // far-field firing is real but never SURVIVES to be the pixel's winning
    // hit -- a closer ordinary hit (same or another instance) always beats it,
    // confirming hypothesis (b). Never reset.
    uint farFieldWon;
    // Round 9: the per-pixel TERMINAL far-field count -- how many pixels'
    // FINAL rendered HitRecord (post far-hit-rejection, post cross-instance
    // isCloserHit resolution) actually carries HITRECORD_FLAG_FAR_FIELD.
    // farFieldWon (above) counts per-instance-loop local wins; this counts
    // survivors of the WHOLE pixel, i.e. what a screenshot could show.
    // Bumped once in BodyInstanceRayMarch.comp right after rec.flags is
    // finalized (after the far-hit-rejection block, so a rejected far-field
    // hit is correctly NOT counted). Never reset.
    uint farFieldTerminal;
    // Batch 10: splits MipFallback.glsl's shadeFromMipSample colorSample.y>0.0
    // branch (see its comment) -- farFieldMipSuccess above proves ONLY that
    // shadeFromMipSample returned true (both the resolved-color arm AND the
    // vec3(0.5) grey-fallback arm return true; only SDF-coverage-missing
    // returns false). These two counters discriminate "a real SEM_COLOR mip
    // sample was resolved" from "fell through to the flat grey placeholder".
    // Never reset. Grows the header by one 16 B block (80 B total) since
    // round-7's single-uint _padRound7B tail had no free slot.
    uint farFieldColorResolved;
    uint farFieldColorFallback;
    // Round 13 probe: splits farFieldMipFail into "descendToNodeOrdinal never
    // reached the brick level" vs "reached it but shadeFromMipSample found no
    // coverage" -- see DebugRaySample.h's TraceBufferHeader for the full
    // rationale. Never reset.
    uint farFieldDescentFail;
    // Round 13 probe #2: min/max of the LEVEL descendToNodeOrdinal was at when
    // it returned false (childExists/farBit/internal-node-at-brick guards),
    // bit-packed as (depth - level) so a level-0 (brick-adjacent) failure
    // reads as a LARGE "hops taken" value and a root-adjacent failure reads
    // small -- lets a single min/max pair localize where in the descent every
    // failure clusters. Never reset.
    uint farFieldDescentFailLevelMin;
    uint farFieldDescentFailLevelMax;
    // (no _padRound10 remainder -- these two fields exactly consumed the
    // slack; struct stays 80 B, matches C++ TraceBufferHeader's alignas(16).)
    // Round 11 (dispatch attribution): see DebugRaySample.h's TraceBufferHeader
    // for the full rationale. index 0 = primary march (BodyInstanceRayMarch.comp),
    // index 1 = probe gather (ProbeGather.comp) -- the ONLY two callers of
    // TraceWorld, itself the ONLY far-field-carrying call path on either
    // backend. Never reset.
    uint farFieldCandidatesByTag[2];
    uint farFieldCountByTag[2];
    uint farFieldColorResolvedByTag[2];
    uint farFieldColorFallbackByTag[2];
    // Round 11 STEP 2: primary-march-only pixel decode ring, see
    // DebugRaySample.h's TraceBufferHeader for layout. Packed (x<<16)|y.
    uint farFieldTerminalPixelWriteCount;
    // Round 17 probe: repurposes _padRound11Pixels[3] (see DebugRaySample.h's
    // TraceBufferHeader for the full rationale) -- octree-3-only candidate-loop
    // breakdown for traverseRayQueryWorld.
    uint rtLoopEntriesOct3;
    uint farFieldGateRejectOct3;
    uint farFieldCandidatesOct3;
    uint farFieldTerminalPixels[32];
    // Batch-24 FARGEN funnel: rect-scoped generation counters (must mirror
    // DebugRaySample.h's TraceBufferHeader field order exactly -- this SSBO
    // layout and the C++ struct read the SAME buffer).
    uint rectRays;
    uint rectCellEntries;
    uint rectGateCross;
    uint _padRound24;
    // Batch-25 JOB 2: 8-bucket histogram of FarFieldGateLhs, rect-scoped
    // (isFarGenRectPixel), edges around the 0.9375 gate threshold:
    // <0.25,<0.5,<0.75,<0.9375,<1.25,<2,<4,>=4. Never reset.
    uint rectLhsHistogram[8];
    // Batch-27 JOB 2: ESVO's OWN cutoff criterion (traverseOctreeInstancedOnce
    // ~SceneBindings.glsl:1497-1498, "tv_max*coef+bias >= scale_exp2"), logged
    // side-by-side with the far-field gate above for the SAME rect pixels, same
    // boot. tv_max/scale_exp2 are LOCAL/NORMALIZED octree-space quantities (no
    // world scale applied) -- contrast with worldDistToCell/cellWorldSize above,
    // which are already world-space. Min/max as float bits (recordFarFieldGateOperands'
    // convention: both operands non-negative, ordering-preserving). Histogram uses
    // the SAME 8 edges as rectLhsHistogram so the two populations are directly
    // comparable. Crossing level = state.scale, recorded at EVERY evaluation of the
    // test (not just crossings) so min/max bracket the full range of depths the
    // test runs at for rect rays. Never reset.
    uint esvoLhsMinBits;
    uint esvoLhsMaxBits;
    uint esvoRhsMinBits;
    uint esvoRhsMaxBits;
    uint esvoLhsHistogram[8];
    uint esvoCrossLevelMin;
    uint esvoCrossLevelMax;
    uint _padBatch27[2];  // keeps the struct 16 B-aligned (alignas(16) TraceBufferHeader)
    // Batch-29 JOB 3 (deep-field mip-accessor policy, regime-2 level ladder):
    // rect-agnostic (whole-frame) 8-bucket histogram of the LEVEL
    // mipPolicyLevel (SVOTypes.glsl) resolves to at every descendToNodeOrdinal
    // call, VIXEN_MIP_POLICY only. Buckets 0-6 = levels 0..6 above brick,
    // bucket 7 = level>=7 (deep/coarse tail). Lets a gate see the ladder
    // actually in use (single-brick-rung shipped behavior would show 100% in
    // bucket 0/1; a working ladder should spread across buckets as far
    // clusters resolve coarser). Never reset (same discipline as the rest of
    // this family).
    uint policyLevelHistogram[8];
    // Batch-29 JOB 4: rect-scoped attribution for ESVO's shadeFromMipSample
    // arms (SceneBindings.glsl) -- the batch-28b validator's ask ("make
    // attribution measured"). Indices match the arm order documented at each
    // recordEsvoMipArm call site. Batch-30 stream B adds index 5 (policy-level
    // arm, VIXEN_MIP_POLICY only) so the gate can see the streaming-grace(1)
    // -> policy(5) arm shift the deep-field mip-policy design doc calls for.
    uint esvoMipArmHits[6];
    uint _padBatch29[2];  // keeps the struct 16 B-aligned (alignas(16) TraceBufferHeader)
    // Batch-32 JOB 1: level-sensitive far-field counter -- min/max/sum/count
    // of the LEVEL that actually fed a shaded far pixel, recorded at the mip-
    // sample call site (both far-field twins), so a level-selection change
    // CAN move this even when the population counters around the policy
    // branch (FarField*/Esvo*/Rect*) can't. Must mirror
    // DebugRaySample.h's field order exactly. Never reset.
    uint farFieldSampledLevelMin;
    uint farFieldSampledLevelMax;
    uint farFieldSampledLevelSum;
    uint farFieldSampledLevelCount;
    // Batch-33 JOB 2: [FarFieldSampleIntensity] -- see DebugRaySample.h's
    // mirror of this struct for the full rationale. Fixed-point sum (not
    // float-bits) because GLSL has no portable atomic float add here.
    uint farFieldSampleIntensityMinBits;
    uint farFieldSampleIntensityMaxBits;
    uint farFieldSampleIntensityFixedSum;
    uint _padBatch33;
    // Batch-35: [PolicyEntryDispatch] -- proves the entry-point inversion
    // (deep-field-mip-policy design doc's "DDA is the exception" ruling)
    // actually happened. Recorded once per instance ray at the DDA's
    // traverseCoarseGridInstancedSdf entry (VIXEN_MIP_POLICY only, before
    // the per-cell march loop): mip = the entry-footprint test picked the
    // mip path directly (no march at all), march = footprint still in the
    // detail regime, falls through to the exact per-cell march unchanged.
    // Plain accumulators, zero-init correct, never reset.
    uint policyEntryDispatchMip;
    uint policyEntryDispatchMarch;
    // BATCH 38: the entry-dispatch gate's OWN LHS probe. Batch 37 proved the
    // existing recordFarFieldGateOperands is BLIND to the entry decision (its two
    // call sites are the mid-march safety net + the RT twin; it fired 7,200x while
    // the entry path ran 409,500x). These two slots repurpose _padBatch35 — no
    // struct growth. Float-bits min/max, same encoding as the round-6 probe.
    uint entryGateLhsMinBits;
    uint entryGateLhsMaxBits;
    // BATCH 39: [PolicyEntryDispatch] third bucket -- splits policyEntryDispatchMarch's
    // conflated population. See DebugRaySample.h's mirror of this struct for the full
    // rationale. Must mirror its field order exactly. Never reset.
    uint policyEntryDispatchEmptyEntry;
    uint _padBatch39;
    // Regime-3 (cosmic accumulation) first slice, deep-field-mip-policy design doc: proves
    // rays actually take the accumulation walk and terminate via the early-out. Must mirror
    // DebugRaySample.h's TraceBufferHeader field order exactly. Plain accumulators, never reset.
    uint regime3EntryCount;
    uint regime3EarlyOutCount;
    // Compositing-slice part 1 (walkCov source audit): repurposes _padRegime3
    // for walkCov min/max (readMipSample(SEM_SDF).y clamped [0,1], the same
    // walkCov the regime-3 walk already computes at SceneBindings.glsl's
    // sample call site) plus walkSampledLevel min/max. Must mirror
    // DebugRaySample.h's TraceBufferHeader field order exactly.
    uint walkCovMinBits;
    uint walkCovMaxBits;
    uint walkSampledLevelMin;
    uint walkSampledLevelMax;
    // B50-T1 follow-up (C3 gate probe): repurposes _padWalkCov, no growth.
    // Must mirror DebugRaySample.h's TraceBufferHeader field order exactly.
    uint compositeBlends;
    uint compositeBehindMaxBits;
    // E6-T2 fixed-capacity per-serialized-body/per-mip-level read ledger.
    // The body index is g_octreeIdx; the level is the mip level selected by
    // descendToNodeOrdinal (0 = brick level).  It is reset per frame by the
    // existing RayTraceBuffer reset path and never grows in shader code.
    uint mipReadBytes[192][16];
    uint mipReadSamples[192][16];
    uint mipReadCounterOverflow;
    uint _mipReadPadding[3];
    uint traceData[];
};

// Fixed-point scale for farFieldSampleIntensityFixedSum -- luminance is a
// small [0,~2] float (shaded mip color channel range), so this keeps ~6
// decimal digits of precision in a uint sum well under overflow for any
// realistic per-frame far-field sample count.
const uint kIntensityFixedPointScale = 1000000u;

// recordFarFieldTerminalPixel: round 11 STEP 2, primary-march-only pixel
// decode. Called from BodyInstanceRayMarch.comp right alongside
// incrFarFieldTerminal() -- gl_GlobalInvocationID.xy IS the pixel coordinate
// at that call site (one invocation per pixel). 32-slot ring, wraps by %32;
// a slot collision just overwrites an earlier sample (a decode aid, not a
// census -- farFieldTerminal already gives the exact count).
void recordFarFieldTerminalPixel(uvec2 pixel) {
#ifdef VIXEN_COMPOSITION_COUNTERS
    // Slice 0 owns this historical ring while enabled; the exact terminal
    // pixel coordinate decode is intentionally unavailable on probe boots.
    return;
#else
    uint slot = atomicAdd(farFieldTerminalPixelWriteCount, 1u) % 32u;
    farFieldTerminalPixels[slot] = (pixel.x << 16) | (pixel.y & 0xFFFFu);
#endif
}

// E6-T1: the shared FootprintRegime classifier (stencil doc's prerequisite +
// residency-unification design's step 1). ZERO behavior change: this is the
// exact three-comparison arithmetic that was previously duplicated inline at
// this file's entry dispatch and at TraceWorld.glsl's two composition-probe
// call sites, now named ONE function every call site routes through. Returns
// 1=Surface, 2=MipHit, 3=Cosmic (E1-T1's stencil-slot numbering, preserved
// bit-for-bit). `raySizeCoef`/`raySizeBias`/`cosmicK` come straight from the
// frame's push constants; the /8.0 brick divisor is the entry-cell subdivision
// this composition probe has always used (matches SceneBindings.glsl's real
// (non-probe) entry-dispatch gate's constant at this same file, and
// TraceWorld.glsl's two former inline copies).
uint classifyFootprintRegime(float worldDist, float cellWorldSize,
                              float raySizeCoef, float raySizeBias, float cosmicK) {
    float footprint = worldDist * raySizeCoef + raySizeBias;
    return (raySizeCoef <= 0.0 || footprint < cellWorldSize / 8.0)
        ? 1u
        : (footprint < cosmicK * cellWorldSize ? 2u : 3u);
}

// E1-T1 stencil slice 0. The design's authoritative values are kept local to
// this probe until FootprintRegime exists as production code: 1=Surface,
// 2=MipHit, 3=Cosmic; source bits 1=virtual, 2=materialized.
#ifdef VIXEN_COMPOSITION_COUNTERS
uint compositionSourceClass(uint sourceMask) {
    return sourceMask == 1u ? 0u : (sourceMask == 2u ? 1u : 2u);
}

void recordCompositionPixel(uint regime, uint sourceMask) {
    if (regime < 1u || regime > 3u || sourceMask == 0u) return;
    atomicAdd(farFieldTerminalPixels[(regime - 1u) * 3u + compositionSourceClass(sourceMask)], 1u);
}

void recordCompositionShadowWave(uint waveType, uint regime, uint sourceMask) {
    if (waveType > 1u || regime < 1u || regime > 3u || sourceMask == 0u) return;
    uint slot = 9u + waveType * 9u + (regime - 1u) * 3u + compositionSourceClass(sourceMask);
    atomicAdd(farFieldTerminalPixels[slot], 1u);
}

void recordCompositionRelaxedRay() {
    // E2-T1 owns the first of slice 0's formerly five reserved slots. TraceWorld calls
    // this at most once per primary ray whose old entry-cull is overridden.
    atomicAdd(farFieldTerminalPixels[27], 1u);
}
#else
void recordCompositionPixel(uint regime, uint sourceMask) {}
void recordCompositionShadowWave(uint waveType, uint regime, uint sourceMask) {}
void recordCompositionRelaxedRay() {}
#endif

// g_dispatchIsPrimaryMarch: round 11, set once (compile-time constant folds
// to a single branch) from VIXEN_DISPATCH_IS_PRIMARY_MARCH, which is spliced
// into BodyInstanceRayMarch.comp only (see BuildRenderGraph.cpp's
// ReadShaderSourceWithTraceHooksGate precedent). Every other TraceWorld
// caller (currently just ProbeGather.comp) leaves it undefined -> tag 1.
#ifdef VIXEN_DISPATCH_IS_PRIMARY_MARCH
const uint g_dispatchTag = 0u;
#else
const uint g_dispatchTag = 1u;
#endif

// incrFarFieldCount: single atomic call site for both far-field twins (DDA in
// SceneBindings.glsl, RT in RayQueryTraversal.glsl) -- see the struct comment
// above for why this is unconditional rather than #ifdef-gated.
void incrFarFieldCount() {
    atomicAdd(farFieldCount, 1u);
    atomicAdd(farFieldCountByTag[g_dispatchTag], 1u);  // round 11
}

// incrFarFieldCandidates: mirrors incrFarFieldCount but counts candidates
// REACHING the far-field gate test, called immediately before the if() in both
// twins -- see the struct comment above.
void incrFarFieldCandidates() {
    atomicAdd(farFieldCandidates, 1u);
    atomicAdd(farFieldCandidatesByTag[g_dispatchTag], 1u);  // round 11
}

// recordFarFieldGateOperands: round-6 min/max probe, called immediately
// before the gate if() in both far-field twins (mirrors incrFarFieldCandidates'
// call-site discipline). lhs/rhs are non-negative world-space distances, so
// floatBitsToUint preserves ordering and plain atomicMin/atomicMax work with
// no sign-bit correction.
// recordEntryGateLhs (batch 38): min/max of the ENTRY dispatch's footprint LHS.
// Separate counter from recordFarFieldGateOperands on purpose -- that one is
// called only from the mid-march safety net and the RT twin, so it cannot see
// the entry decision at all (batch-37 finding). Non-negative by construction,
// so raw float bits preserve ordering for atomicMin/Max.
void recordEntryGateLhs(float lhs) {
    uint bits = floatBitsToUint(max(lhs, 0.0));
    atomicMin(entryGateLhsMinBits, bits);
    atomicMax(entryGateLhsMaxBits, bits);
}

void recordFarFieldGateOperands(float lhs, float rhs) {
    atomicMin(farFieldLhsMinBits, floatBitsToUint(lhs));
    atomicMax(farFieldLhsMaxBits, floatBitsToUint(lhs));
    atomicMin(farFieldRhsMinBits, floatBitsToUint(rhs));
    atomicMax(farFieldRhsMaxBits, floatBitsToUint(rhs));
}

// incrRtLoopEntries: round-6 blocker-2 probe, see rtLoopEntries field comment.
void incrRtLoopEntries() {
    atomicAdd(rtLoopEntries, 1u);
}

// Batch-24 FARGEN funnel: rect-scoped generation counters, TIGHTENED batch-25
// JOB 2 from the loose 28x22 box (x363-390,y239-260, 616px) to the exact
// census rects c1∪c2 (c1: 366,240-376,259; c2: 380,240-389,259; 420px total)
// so the funnel and the ESVO/dda/composed census agree on the SAME pixel
// population. gl_GlobalInvocationID.xy IS the pixel coordinate in this
// compute stage (same convention as recordFarFieldTerminalPixel above) -- no
// plumbing needed, just a bounds check at each call site.
bool isFarGenRectPixel() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    bool inC1 = p.x >= 366 && p.x <= 376 && p.y >= 240 && p.y <= 259;
    bool inC2 = p.x >= 380 && p.x <= 389 && p.y >= 240 && p.y <= 259;
    return inC1 || inC2;
}

// incrFarGenRectRay: bump once per invocation landing in the rect (census).
void incrFarGenRectRay() {
    if (isFarGenRectPixel()) atomicAdd(rectRays, 1u);
}

// incrFarGenRectCellEntry: bump at the far-field candidate-loop entry point
// (mirrors incrFarFieldCandidates' call-site discipline) for rect rays only.
void incrFarGenRectCellEntry() {
    if (isFarGenRectPixel()) atomicAdd(rectCellEntries, 1u);
}

// incrFarGenRectGateCross: bump when a rect ray's candidate PASSES the
// far-field footprint gate (mirrors the gate if() call sites).
void incrFarGenRectGateCross() {
    if (isFarGenRectPixel()) atomicAdd(rectGateCross, 1u);
}

// recordFarGenRectLhsHistogram: batch-25 JOB 2. Bucket lhs against edges
// around the 0.9375 gate threshold; rect-scoped only. Called alongside
// recordFarFieldGateOperands in both far-field twins.
void recordFarGenRectLhsHistogram(float lhs) {
    if (!isFarGenRectPixel()) return;
    uint b = lhs < 0.25 ? 0u : lhs < 0.5 ? 1u : lhs < 0.75 ? 2u : lhs < 0.9375 ? 3u :
             lhs < 1.25 ? 4u : lhs < 2.0 ? 5u : lhs < 4.0 ? 6u : 7u;
    atomicAdd(rectLhsHistogram[b], 1u);
}

// recordEsvoCutoffOperands: batch-27 JOB 2. Called at ESVO's own LOD cutoff
// test (traverseOctreeInstancedOnce, ~line 1497-1498), rect-scoped only —
// side-by-side with recordFarFieldGateOperands/recordFarGenRectLhsHistogram
// so both criteria's operands come out of the SAME boot for the SAME rays.
// lhs/rhs are non-negative local/normalized quantities (tv_max, scale_exp2),
// same ordering-preserving floatBitsToUint trick as the far-field probe.
// level = state.scale, the octree depth the test evaluated at.
void recordEsvoCutoffOperands(float lhs, float rhs, uint level) {
    if (!isFarGenRectPixel()) return;
    atomicMin(esvoLhsMinBits, floatBitsToUint(lhs));
    atomicMax(esvoLhsMaxBits, floatBitsToUint(lhs));
    atomicMin(esvoRhsMinBits, floatBitsToUint(rhs));
    atomicMax(esvoRhsMaxBits, floatBitsToUint(rhs));
    uint b = lhs < 0.25 ? 0u : lhs < 0.5 ? 1u : lhs < 0.75 ? 2u : lhs < 0.9375 ? 3u :
             lhs < 1.25 ? 4u : lhs < 2.0 ? 5u : lhs < 4.0 ? 6u : 7u;
    atomicAdd(esvoLhsHistogram[b], 1u);
    atomicMin(esvoCrossLevelMin, level);
    atomicMax(esvoCrossLevelMax, level);
}

// Round-17 probe: octree-3-only breakdown of traverseRayQueryWorld's
// candidate loop, see rtLoopEntriesOct3/farFieldGateRejectOct3/
// farFieldCandidatesOct3 field comment (DebugRaySample.h).
void incrRtLoopEntriesOct3() {
    atomicAdd(rtLoopEntriesOct3, 1u);
}
void incrFarFieldGateRejectOct3() {
    atomicAdd(farFieldGateRejectOct3, 1u);
}
void incrFarFieldCandidatesOct3() {
    atomicAdd(farFieldCandidatesOct3, 1u);
}

// incrFarFieldWon: round-7 blocker-1 probe #3, see farFieldWon field comment.
void incrFarFieldWon() {
    atomicAdd(farFieldWon, 1u);
}

// recordFarFieldMipResolve: round-7 blocker-1 probe, see farFieldMipSuccess/
// Fail field comment. Called immediately after the descendToNodeOrdinal +
// shadeFromMipSample check in both far-field twins.
void recordFarFieldMipResolve(bool resolved) {
    if (resolved) {
        atomicAdd(farFieldMipSuccess, 1u);
    } else {
        atomicAdd(farFieldMipFail, 1u);
    }
}

// incrFarFieldDescentFail: round-13 probe, see farFieldDescentFail field
// comment. Called when descendToNodeOrdinal itself returns false (before the
// short-circuited shadeFromMipSample call) -- splits farFieldMipFail's two
// sub-causes apart.
void incrFarFieldDescentFail() {
    atomicAdd(farFieldDescentFail, 1u);
}

// recordFarFieldDescentFailLevel: round-13 probe #2, see
// farFieldDescentFailLevelMin/Max field comment. `hops` = depth - level at
// the point of failure (0 == failed immediately at the root's own first
// child-existence check, larger == failed deeper/closer to brick level).
void recordFarFieldDescentFailLevel(uint hops) {
    atomicMin(farFieldDescentFailLevelMin, hops);
    atomicMax(farFieldDescentFailLevelMax, hops);
}

// recordPolicyLevel: batch-29 JOB 3, see policyLevelHistogram field comment
// (DebugRaySample.h/this file's TraceBufferHeader). Called from
// descendToNodeOrdinal (ESVOTraversal.glsl) every VIXEN_MIP_POLICY call,
// rect-agnostic (whole-frame) -- this is the "is the ladder in use" gate,
// not an attribution probe, so it deliberately isn't scoped to the far
// clusters like the rectLhsHistogram family above.
void recordPolicyLevel(uint level) {
    atomicAdd(policyLevelHistogram[min(level, 7u)], 1u);
}

// recordFarFieldSampledLevel: batch-32 JOB 1, see farFieldSampledLevelMin/
// Max/Sum/Count field comment. Called at the mip-sample call site (both
// far-field twins) right after descendToNodeOrdinal returns + shadeFromMipSample
// resolves -- level is the ACTUAL level the sample was read at (depth - the
// loop level descendToNodeOrdinal stopped on), so unlike PolicyLevelHistogram
// (every policy decision, rect-agnostic) this only counts levels that fed a
// real shaded far pixel. Rect-agnostic like PolicyLevelHistogram (whole-frame
// liveness signal, not an attribution probe).
void recordFarFieldSampledLevel(uint level) {
    atomicMin(farFieldSampledLevelMin, level);
    atomicMax(farFieldSampledLevelMax, level);
    atomicAdd(farFieldSampledLevelSum, level);
    atomicAdd(farFieldSampledLevelCount, 1u);
}

// recordFarFieldSampleIntensity: batch-33 JOB 2. Called alongside
// recordFarFieldSampledLevel (same call site, both far-field twins) so
// [FarFieldSampleIntensity] and [FarFieldSampledLevel] share one count
// (farFieldSampledLevelCount) -- distinguishes "level chosen" from "sample
// value", per the batch-32 open ruling (ESVO dimmed under policy while the
// level/count census stayed flat). `luminance` is the shaded mip color's
// luminance (dot with standard Rec.709 weights) at the point
// recordFarFieldSampledLevel is called. min/max via floatBitsToUint (valid
// since luminance >= 0); sum via fixed-point uint (see kIntensityFixedPointScale).
void recordFarFieldSampleIntensity(float luminance) {
    uint bits = floatBitsToUint(luminance);
    atomicMin(farFieldSampleIntensityMinBits, bits);
    atomicMax(farFieldSampleIntensityMaxBits, bits);
    atomicAdd(farFieldSampleIntensityFixedSum, uint(clamp(luminance, 0.0, 4000.0) * float(kIntensityFixedPointScale)));
}

// recordPolicyEntryDispatch: batch-35, see policyEntryDispatchMip/March field
// comment. Called ONCE per instance ray at the DDA's entry-point regime
// decision (before the per-cell march loop) -- proves the dispatch inversion
// happened: mip=true means the entry footprint already covered a voxel-or-
// coarser rung and the ray took the mip path with NO march; mip=false means
// genuine detail was in view and the ray fell through to the exact march.
void recordPolicyEntryDispatch(bool tookMip) {
    if (tookMip) {
        atomicAdd(policyEntryDispatchMip, 1u);
    } else {
        atomicAdd(policyEntryDispatchMarch, 1u);
    }
}

// recordPolicyEntryEmptyEntry: batch-39, see policyEntryDispatchEmptyEntry field
// comment. Called ONLY for the admitted-but-empty-entry-cell population, right
// where that fall-through happens -- BEFORE the shared recordPolicyEntryDispatch(false)
// call, so march (unchanged) still counts this population too (additive split, not
// a move): march = detail-regime rays + empty-entry rays; emptyEntry = the subset of
// march that was actually admitted but had no brick at the entry cell.
void recordPolicyEntryEmptyEntry() {
    atomicAdd(policyEntryDispatchEmptyEntry, 1u);
}

// recordRegime3Entry / recordRegime3EarlyOut: regime-3 (cosmic accumulation)
// first slice, see regime3EntryCount/regime3EarlyOutCount field comment.
// recordRegime3Entry is called once per ray that takes the accumulation walk
// (VIXEN_REGIME3 && footprint >= K*cell at entry dispatch); recordRegime3EarlyOut
// once for the subset that terminates via the T~eps early-out rather than
// exhausting the walk's cell budget.
void recordRegime3Entry() {
    atomicAdd(regime3EntryCount, 1u);
}

void recordRegime3EarlyOut() {
    atomicAdd(regime3EarlyOutCount, 1u);
}

// recordWalkCov / recordWalkSampledLevel: compositing-slice part 1 probe.
// Called at the regime-3 walk's SEM_SDF mip-sample call site (walkCov is
// already computed there for the accumulation math) to establish whether
// coverage tracks bake sparsity or saturates regardless of it. Non-negative
// by construction (clamp(...,0,1)), so floatBitsToUint preserves ordering --
// same encoding/seeding discipline as recordEntryGateLhs.
void recordWalkCov(float cov) {
    uint bits = floatBitsToUint(max(cov, 0.0));
    atomicMin(walkCovMinBits, bits);
    atomicMax(walkCovMaxBits, bits);
}

void recordWalkSampledLevel(uint level) {
    atomicMin(walkSampledLevelMin, level);
    atomicMax(walkSampledLevelMax, level);
}

// recordCompositeBlend: B50-T1 follow-up C3 probe. Called from
// BodyInstanceRayMarch.comp at the composite blend itself, INSIDE the
// residualT in-range gate, so the count is exactly "blends that executed"
// and the max is over exactly those blends' behindColor. behindColor is
// non-negative by construction (a product of mip colors and instance tints),
// so floatBitsToUint preserves ordering -- same encoding as recordWalkCov.
void recordCompositeBlend(vec3 behindColor) {
    atomicAdd(compositeBlends, 1u);
    float mag = max(max(behindColor.r, behindColor.g), behindColor.b);
    atomicMax(compositeBehindMaxBits, floatBitsToUint(max(mag, 0.0)));
}

// recordEsvoMipArm: batch-29 JOB 4, see esvoMipArmHits field comment. Called
// at each of ESVO's shadeFromMipSample call sites (rect-scoped, same
// isFarGenRectPixel population as the rest of the rect family) so the
// batch-28b validator's "make attribution measured" ask has real per-arm
// counts instead of an elimination argument. Arm indices:
//   0 = tier-crossing subpixel-footprint/childNotResident (~line 1413+K)
//   1 = streaming-grace non-resident brick (~line 1478+K)
//   2 = deliberate LOD screen-space cutoff (~line 1546+K, ESVO's OWN
//       criterion -- the regime-2 mip-hit site proper)
//   3 = tier-crossing child-miss fallback (~line 1761+K)
//   4 = hop-budget-exhausted fallback (~line 1864+K, the SAME site the
//       batch-28b elimination argument pointed at as "lights far content
//       today by incidental fallback, not the policy")
//   5 = batch-30 stream B: policy-level arm (VIXEN_MIP_POLICY only) -- the
//       streaming-grace site (arm 1) resolved a policy level > 0 and sampled
//       an ancestor node instead of the leaf. Flag-off this index is never
//       written (arm 1 fires unconditionally, matching pre-batch-30 behavior
//       byte-exact); flag-on the gate reads the arm-1-vs-arm-5 split.
void recordEsvoMipArm(uint armIndex) {
    if (!isFarGenRectPixel()) return;
    atomicAdd(esvoMipArmHits[min(armIndex, 5u)], 1u);
}

// incrFarFieldRejectedByBounds: round-7 blocker-1 probe #2, see
// farFieldRejectedByBounds field comment. Called in BodyInstanceRayMarch.comp
// when the M5/M5b tight-bounds far-hit-rejection guard discards a hit whose
// worldHit.t traces back to a TRACE_STEP_FAR_FIELD_CUTOFF sample.
void incrFarFieldRejectedByBounds() {
    atomicAdd(farFieldRejectedByBounds, 1u);
}

// incrFarFieldTerminal: round 9, see farFieldTerminal field comment. Called
// once in BodyInstanceRayMarch.comp right after rec.flags is finalized
// (after the far-hit-rejection block), when the pixel's FINAL HitRecord
// carries HITRECORD_FLAG_FAR_FIELD.
void incrFarFieldTerminal() {
    atomicAdd(farFieldTerminal, 1u);
}

// incrFarFieldColorResolved/incrFarFieldColorFallback: batch 10, see
// farFieldColorResolved/farFieldColorFallback field comment. Called from
// MipFallback.glsl's shadeFromMipSample on the resolved-color arm vs the
// vec3(0.5) grey-fallback arm of its colorSample.y>0.0 branch.
void incrFarFieldColorResolved() {
    atomicAdd(farFieldColorResolved, 1u);
    atomicAdd(farFieldColorResolvedByTag[g_dispatchTag], 1u);  // round 11
}

void incrFarFieldColorFallback() {
    atomicAdd(farFieldColorFallback, 1u);
    atomicAdd(farFieldColorFallbackByTag[g_dispatchTag], 1u);  // round 11
}

// ============================================================================
// OCTREE CONFIG SSBO (binding 5, std430, 432 B / element, N elements)
// ============================================================================
// Struct layout must be byte-for-byte identical to the C++ OctreeConfig (432 B).
//
// LAYOUT NOTE (I3.2 — UBO→SSBO migration):
//   Under std430, scalar arrays (float[], uint[]) stride at 4 B per element.
//   The tail was previously `float _tailPad[5]` (80 B under std140's 16 B/elem stride).
//   Under std430 that would be only 5*4=20 B, shrinking the struct to 372 B — WRONG.
//   Fix: use `uvec4 _tailPad[5]` (vec4-aligned type, 16 B/elem under BOTH std140 and
//   std430) → 5*16=80 B in both layouts → struct stays 432 B. ✓
//   All other fields use vec4/mat4/uvec4 types that have identical alignment under
//   both layouts (scalar ints/uints that are NOT in bare arrays are unaffected).
//
// Byte offsets (std430 SSBO — identical to the old std140 UBO for all read fields):
//   0   esvoMaxScale        int
//   4   userMaxLevels       int
//   8   brickDepthLevels    int
//   12  brickSize           int
//   16  minESVOScale        int
//   20  brickESVOScale      int
//   24  bricksPerAxis       int
//   28  _padding1           int
//   32  gridMin             vec3  (+4 B pad → 48)
//   48  gridMax             vec3  (+4 B pad → 64)
//   64  localToWorld        mat4  (64 B → 128)
//   128 worldToLocal        mat4  (64 B → 192)
//   192 nodeArrayBase       int
//   196 brickArrayBase      int
//   200 formatId            uint  ← 0=FORMAT_BINARY, 1=FORMAT_STORED_SDF
//   204 bricksPerAxisSdf    uint  ← brick-grid cube side length
//   208 poolBrickBase       uint  ← float-element offset into channelPool[]
//   212 channelCount        uint  ← number of live channels in channels[]
//   216 brickStrideFloats   uint  ← floats per brick (sum over all channels)
//   220 _padChannels        uint  ← explicit 4-byte pad (std430: arrays are 16-aligned)
//   224 channels[8]         uvec4 ← 8*16=128 B → ends at byte 352
//   352 _tailPad[5]         uvec4   5*16=80 B (uvec4 stride=16 under std430) → 432 B ✓
// ============================================================================
#include "Generated/OctreeConfig.glsl"   // generated single-source struct (Phase C); was an inline copy

layout(std430, binding = 5) readonly buffer OctreeConfigsSSBO {
    OctreeConfig configs[];
};

// ============================================================================
// BODY INSTANCE SSBO (binding 10, std430, 64 B / element)
// ============================================================================
// Declared before C++ wires it (Task 8); glslc accepts unmatched bindings at
// compile time — the descriptor wiring is deferred to the next milestone.
struct BodyInstance {
    vec3  worldPos;          // 0
    float renderScale;       // 12
    vec3  color;             // 16
    uint  octreeIndex;       // 28
    uint  providerKind;      // 32  (0 = Stored/ESVO, 1 = Procedural)
    uint  recipeId;          // 36
    float recipeParams[6];   // 40..63  (params.xyz = radius, amp, freq)
};
// std430 SSBO record, 64 bytes — byte-for-byte identical to C++ BodyInstanceGpu.

layout(std430, binding = 10) readonly buffer BodyInstanceBuffer {
    BodyInstance bodyInstances[];
};

// ============================================================================
// SHADER COUNTERS (Performance Metrics)
// ============================================================================
// The live app compiles this shader WITHOUT ENABLE_SHADER_COUNTERS, so the
// calls below resolve to the no-op stubs in ShaderCounters.glsl and binding 8
// does not exist in the reflected interface. There is NO runtime opt-in:
// ShaderBundleBuilder::SetStageDefines does token substitution, not #define
// injection (see BuildRenderGraph.cpp), so re-enabling counters means
// hand-adding `#define ENABLE_SHADER_COUNTERS` here (and re-wiring binding 8).
#define SHADER_COUNTERS_BINDING 8
#include "ShaderCounters.glsl"

// ============================================================================
// PUSH CONSTANTS
// ============================================================================
// W1b: the PushConstants block + DEBUG_MODE vocabulary + getRayDir moved
// VERBATIM to SceneCommon.glsl (one source) so non-traversal consumers can
// take the camera/push interface without this file's traversal machinery.
// Included here at the block's original position — every traversal consumer
// sees the identical interface.
#include "SceneCommon.glsl"

// ============================================================================
// PER-DISPATCH GLOBALS
// ============================================================================
// These are set once per instance iteration before calling traversal helpers.
// g_octreeIdx is used by the octreeConfig macro below.
// g_esvoNodeBase is declared in ESVOTraversal.glsl (defaults to 0 for the dense
// path); we set it here per-instance so fetchESVONode() addresses the right
// sub-range of the concatenated esvoNodes[] buffer.
// g_brickArrayBase applies the per-octree brick offset in marchBrickInstanced().
int g_octreeIdx      = 0;   // index into configs[] for the active octree
int g_brickArrayBase = 0;   // configs[g_octreeIdx].brickArrayBase
uint g_mipSampleLevel = 0u; // last resolved mip level for readMipSample accounting
// Round-7 blocker-1 probe #3: set true immediately before the far-field
// early-return in both twins (traverseCoarseGridInstancedSdf below,
// traverseRayQueryWorld in RayQueryTraversal.glsl), read by TraceWorld.glsl
// right after each per-instance call to count how many far-field firings
// actually WIN isCloserHit (survive to bestT/bestColor) vs lose to a nearer
// hit from another instance/candidate -- discriminates hypothesis (b).
// Per-invocation global (same convention as g_octreeIdx above); reset to
// false at the top of TraceWorld() before the loop, like anyHit.
bool g_lastHitWasFarField = false;

// Slice-0 call-boundary results. These are probe-only consumers of the exact
// inline FootprintRegime formula; no production classifier abstraction exists
// yet (Deep-Field-Residency-Unification §B).
uint g_lastFootprintRegime = 1u;
uint g_lastShadowCompositionRegime = 1u;
uint g_lastShadowCompositionSourceMask = 0u;

// Regime-3 compositing (VIXEN_REGIME3_COMPOSITE), part 2: residual transmittance
// T left over from the regime-3 accumulation walk's early-out/budget-exhaustion
// (SceneBindings.glsl's regime3Admits block below) -- 1.0 (fully transparent,
// i.e. this candidate contributed no coverage) whenever the winning hit did NOT
// come from that walk. Same per-invocation-global / reset-before-call convention
// as g_lastHitWasFarField above; consumed+reset by TraceWorld.glsl right after
// each per-instance call. Flag-off (VIXEN_REGIME3_COMPOSITE unset): never
// written away from its 1.0 default, so the composite blend at the shade
// resolve is always a no-op -- see BodyInstanceRayMarch.comp's blend site.
float g_lastRegime3ResidualT = 1.0;

#ifdef VIXEN_SHADOW_DBG
// M10 shadow-diagnostic (env-gated, off by default): populated ONLY when the
// shade pass sets g_shadowDbgArm=1 for one target pixel just before its shadow
// trace. marchBrickSdfAnyHit records, at the FIRST crossing it registers, the
// d value it crossed on, the grid-arc-length of the crossing, and the step
// index; TraceWorldShadow records which instIdx that occluder was. Zero cost
// and zero behavior change when g_shadowDbgArm==0 (every store is guarded).
int   g_shadowDbgArm      = 0;    // 1 = capture for THIS pixel's shadow trace
int   g_shadowDbgCurInst  = -1;   // instIdx of the instance currently being marched
int   g_shadowDbgInst     = -1;   // occluding instIdx (-1 = none/lit)
int   g_shadowDbgLeafKind = -1;   // 0 = SDF march, 1 = binary DDA, 2 = procedural
float g_shadowDbgD        = 1e9;  // d value at the crossing sample
float g_shadowDbgSHitGrid = -1.0; // grid-arc-length to crossing (from brick entry)
int   g_shadowDbgStep     = -1;   // step index within the brick where it crossed
int   g_shadowDbgHops     = 0;    // brick-hops taken before crossing
#endif

// ============================================================================
// MACRO OVERRIDE: octreeConfig
// ============================================================================
// The shared includes (ESVOTraversal.glsl, ESVOCoefficients.glsl,
// RayGeneration.glsl, CoordinateTransforms.glsl) all reference a symbol named
// "octreeConfig".  By defining it as a macro BEFORE including those files we
// redirect every "octreeConfig.field" to "configs[g_octreeIdx].field" without
// touching the shared source.
#define octreeConfig configs[g_octreeIdx]

// ============================================================================
// SHARED INCLUDES (order matters — macros above must be defined first)
// ============================================================================
// Generated/LightingConfig.glsl (Sampled Lighting Inc3 M1 fix): Lighting.glsl's
// data-driven computeLighting(...,LightingConfig) overload needs the
// LightingConfig/Light STRUCT TYPES declared before it's parsed. This is a
// type-only include (no `layout(binding=...)` — see the file itself), safe to
// pull in here even though the actual LightingConfigSSBO binding stays
// per-shader (each consumer #includes Generated/LightingConfig.glsl again for
// its own binding declaration; the struct has an include guard, so no
// redefinition error). Pre-extraction this worked by accident of file order
// (BodyInstanceRayMarch.comp declared the binding, and therefore this type,
// earlier in the same TU); the split broke that implicit ordering, so it is
// made explicit here instead.
#include "Generated/LightingConfig.glsl"
#include "CoordinateTransforms.glsl"
#include "RayGeneration.glsl"
#include "ESVOCoefficients.glsl"
#include "TraceRecording.glsl"
#include "ESVOTraversal.glsl"
#include "Lighting.glsl"
#include "SdfRecipes.glsl"
#include "StoredSdf.glsl"    // Inc2 M4: trilinear SDF fetch + sphere-trace handler
#include "MipFallback.glsl"  // Inc1 M3: sparse-mip ESVO LOD shader-side fallback read
// W-RTQUERY Slice A: VK_KHR_ray_query per-brick-AABB TLAS backend (third search
// backend alongside ESVO/DDA). Self-#ifdef-guarded on VIXEN_RTQUERY_TRAVERSAL --
// entirely compiled out (including its TLAS binding) on a flag-off build, exactly
// like the coarse-grid DDA backend below stays compiled out without
// VIXEN_BRICKMAP_TRAVERSAL. Included here (after configs[]/brickLookup/StoredSdf's
// marchBrickSdfCell, all declared above) so it can call marchBrickSdfCell/
// brickLocalToGrid directly, same dependency order the DDA backend relies on.
#include "RayQueryTraversal.glsl"
// Provider kinds (mirror ShellOctreeGpu.h ProviderKind + SdfRecipes.h).
#define PROVIDER_STORED     0u
#define PROVIDER_PROCEDURAL 1u

// ============================================================================
// LOD CONTROL (Task 7)
// ============================================================================
// Screen-space LOD termination: stops descending when the voxel's projected
// footprint covers ≥ 1 pixel, returning a coarse hit instead.
//
// Formula (Laine & Karras 2010, Section 4.4 / CUDA Raycast.inl L181):
//   tc_max * raySizeCoef + raySizeBias >= scale_exp2
// where:
//   raySizeCoef = 2 * tan(fov / screenHeight / 2)  (cone spread per unit dist)
//   raySizeBias = 0 for a pinhole camera
//   tc_max      = tv_max (exit-t of the voxel being tested)
//   scale_exp2  = 2^(scale - esvoMaxScale)  (normalized voxel size)
//
// Set pc.raySizeCoef = 0.0 to disable (full-detail traversal regardless).
// The condition is gated on raySizeCoef > 0.0 to make the zero case free.
#define LOD_ENABLED

// ============================================================================
// BRICK DDA MARCHING (instanced — uses g_brickArrayBase)
// ============================================================================

bool marchBrickInstanced(vec3 rayDir, vec3 posInBrick, uint localBrickIndex,
                         out vec3 hitColor, out vec3 hitNormal, out uint axisMask,
                         out vec3 hitBrickLocalPos, out uint hitVoxelLinearIdx) {
    hitVoxelLinearIdx = 0u;
    ivec3 currentVoxel = clamp(ivec3(floor(posInBrick)), ivec3(0), ivec3(7));

    ivec3 step = ivec3(sign(rayDir));
    if (step.x == 0) step.x = 1;
    if (step.y == 0) step.y = 1;
    if (step.z == 0) step.z = 1;

    int BRICK_SIZE_VAL = octreeConfig.brickSize;

    if ((posInBrick.x <= 0.001 && rayDir.x < 0.0) ||
        (posInBrick.x >= float(BRICK_SIZE_VAL) - 0.001 && rayDir.x > 0.0) ||
        (posInBrick.y <= 0.001 && rayDir.y < 0.0) ||
        (posInBrick.y >= float(BRICK_SIZE_VAL) - 0.001 && rayDir.y > 0.0) ||
        (posInBrick.z <= 0.001 && rayDir.z < 0.0) ||
        (posInBrick.z >= float(BRICK_SIZE_VAL) - 0.001 && rayDir.z > 0.0)) {
        return false;
    }

    vec3 deltaDist;
    deltaDist.x = abs(rayDir.x) > DIR_EPSILON ? 1.0 / abs(rayDir.x) : 1e20;
    deltaDist.y = abs(rayDir.y) > DIR_EPSILON ? 1.0 / abs(rayDir.y) : 1e20;
    deltaDist.z = abs(rayDir.z) > DIR_EPSILON ? 1.0 / abs(rayDir.z) : 1e20;

    vec3 tMax;
    const float MIN_DIST = 0.0001;
    for (int axis = 0; axis < 3; axis++) {
        if (abs(rayDir[axis]) < DIR_EPSILON) {
            tMax[axis] = 1e20;
        } else {
            float posLocal  = posInBrick[axis];
            float distToNext = (rayDir[axis] > 0.0)
                ? float(currentVoxel[axis] + 1) - posLocal
                : posLocal - float(currentVoxel[axis]);
            distToNext = max(distToNext, MIN_DIST);
            tMax[axis] = distToNext / abs(rayDir[axis]);
        }
    }

    // The absolute brick index in the concatenated brickData[] array
    // = brickArrayBase (in brick units) + localBrickIndex
    uint absBrickIndex = uint(g_brickArrayBase) + localBrickIndex;

    axisMask = 0u;
    const int MAX_STEPS = 300;
    for (int i = 0; i < MAX_STEPS; i++) {
        if (any(lessThan(currentVoxel, ivec3(0))) ||
            any(greaterThanEqual(currentVoxel, ivec3(8)))) {
            break;
        }

        uint voxelLinearIdx = uint(currentVoxel.z * 64 + currentVoxel.y * 8 + currentVoxel.x);
        uint voxelData = brickData[absBrickIndex * 512u + voxelLinearIdx];

        if (voxelData != 0u) {
            uint matID  = voxelData & 0xFFu;
            hitColor    = getMaterialColor(matID);
            hitNormal   = vec3(0.0);
            if (axisMask == 1u)      hitNormal.x = -float(step.x);
            else if (axisMask == 2u) hitNormal.y = -float(step.y);
            else                     hitNormal.z = -float(step.z);

            if (i == 0) {
                vec3 absDir = abs(rayDir);
                if      (absDir.x > absDir.y && absDir.x > absDir.z) hitNormal = vec3(-sign(rayDir.x), 0.0, 0.0);
                else if (absDir.y > absDir.z)                         hitNormal = vec3(0.0, -sign(rayDir.y), 0.0);
                else                                                   hitNormal = vec3(0.0, 0.0, -sign(rayDir.z));
            }

            hitBrickLocalPos  = vec3(currentVoxel) + vec3(0.5);
            hitVoxelLinearIdx = voxelLinearIdx;
            return true;
        }

        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            currentVoxel.x += step.x; tMax.x += deltaDist.x; axisMask = 1u;
        } else if (tMax.y < tMax.z) {
            currentVoxel.y += step.y; tMax.y += deltaDist.y; axisMask = 2u;
        } else {
            currentVoxel.z += step.z; tMax.z += deltaDist.z; axisMask = 4u;
        }
    }
    return false;
}

// ============================================================================
// LEAF HIT HANDLING (instanced — applies g_brickArrayBase)
// ============================================================================

bool handleLeafHitInstanced(TraversalState state, RayCoefficients coef,
                             vec3 rayStartWorld, vec3 rayDir, float tBias,
                             uvec2 parentDescriptor, uint validMask, uint leafMask,
                             inout StackEntry stack[STACK_SIZE],
                             out vec3 hitColor, out vec3 hitNormal, out float hitT,
                             out uint hitBrickIndex, out uint hitVoxelLinearIdx) {
    hitBrickIndex      = 0u;
    hitVoxelLinearIdx  = 0u;

    int BRICK_SIZE_VAL = octreeConfig.brickSize;

    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
    if (localChildIdx < 0 || localChildIdx > 7) return false;

    uint leafDescriptorIndex = resolveLeafDescriptorIndex(parentDescriptor, validMask, leafMask,
                                                          localChildIdx);

    uvec2 leafDescriptor = fetchESVONode(leafDescriptorIndex);  // uses our macro → base-offset
    uint  localBrickIdx  = getContourPointer(leafDescriptor);

    if (localBrickIdx == SVO_INVALID_INDEX) return false;

    float tHit      = state.t_min;
    vec3  rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    vec3  hitPos12    = coef.normOrigin + rayDirLocal * tHit;

    vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2,
                                        coef.octant_mask, BRICK_SIZE_VAL);
    posInBrick = clamp(posInBrick, vec3(0.0), vec3(float(BRICK_SIZE_VAL) - 0.001));

    vec3 brickColor, brickNormal;
    uint axisMask;
    vec3 hitBrickLocalPos;
    uint voxelLinearIdx;
    if (marchBrickInstanced(rayDir, posInBrick, localBrickIdx,
                            brickColor, brickNormal, axisMask,
                            hitBrickLocalPos, voxelLinearIdx)) {
        hitColor         = brickColor;
        hitNormal        = brickNormal;
        hitT             = tBias + tHit;
        // Absolute brick index for the ID buffer: base + local
        hitBrickIndex    = uint(g_brickArrayBase) + localBrickIdx;
        hitVoxelLinearIdx = voxelLinearIdx;
        return true;
    }
    return false;
}

// ============================================================================
// STORED-SDF LEAF HIT (instanced) — Inc3 M3 (updated from Inc2 M6)
// ============================================================================
// SDF variant of handleLeafHitInstanced. Reached from traverseOctreeInstanced at
// an ESVO leaf when configs[g_octreeIdx].formatId == FORMAT_STORED_SDF. Bridges
// the leaf entry from ESVO [1,2]^3 space into the SDF's true grid-voxel frame and
// marches the trilinear iso-surface within that ONE brick (marchBrickSdf).
//
// Inc3 M3: after the iso-surface hit, sample per-voxel color (SEM_COLOR) and
// roughness (SEM_ROUGHNESS) from the generic channel pool via trilinear gather.
// hitColor is the voxel color (NOT the vec3(1) tint placeholder); hitRoughness
// is the per-voxel roughness. Instance tint is still applied in main().
bool handleLeafHitInstancedSdf(TraversalState state, RayCoefficients coef,
                               vec3 rayDir, float tBias,
                               inout StackEntry stack[STACK_SIZE],
                               out vec3 hitColor, out vec3 hitNormal, out float hitT,
                               out float hitRoughness,
                               out uint hitBrickIndex, out uint hitVoxelLinearIdx) {
    hitColor          = vec3(1.0);
    hitNormal         = vec3(0.0, 1.0, 0.0);
    hitT              = 0.0;
    hitRoughness      = 0.5;
    hitBrickIndex     = 0u;
    hitVoxelLinearIdx = 0u;

    int bpa = int(octreeConfig.bricksPerAxisSdf);
    if (bpa <= 0) return false;
    const int BRICK_SIZE_SDF = 8;

    // Bridge ESVO [1,2]^3 → true geometric grid-voxel space.
    vec3 rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    vec3 hitPos12    = coef.normOrigin + rayDirLocal * state.t_min;   // [1,2]^3
    float dirLen     = length(rayDirLocal);
    if (dirLen < 1e-12) return false;
    vec3 gridDirN    = rayDirLocal / dirLen;                          // normalized

    // One ESVO leaf == exactly one 8^3 brick here, so state.pos (the leaf node's own min
    // corner, authoritatively known to the traversal) IS this brick's origin — unmirror it
    // the same way computePosInBrick does internally to get the brick's OWN integer grid
    // coordinate, instead of re-deriving "which brick" from hitPos12 (which sits, up to float
    // error, exactly ON the entry-face boundary for every leaf hit and is genuinely ambiguous
    // right at that boundary). Re-deriving from hitPos12 (the old approach, via a directional
    // nudge + floor) let adjacent pixels resolve to different bricks whenever the transverse
    // ray-direction components were too small to move the nudge off the boundary — most
    // visibly when the view direction runs close to a world axis, so a whole line of pixels
    // grazes the SAME brick-boundary plane at once (axis-parallel seam/dropout artifact).
    vec3 brickOriginMirrored = state.pos;
    if ((coef.octant_mask & 1) == 0) brickOriginMirrored.x = 3.0 - state.scale_exp2 - brickOriginMirrored.x;
    if ((coef.octant_mask & 2) == 0) brickOriginMirrored.y = 3.0 - state.scale_exp2 - brickOriginMirrored.y;
    if ((coef.octant_mask & 4) == 0) brickOriginMirrored.z = 3.0 - state.scale_exp2 - brickOriginMirrored.z;
    ivec3 brick = ivec3(round((brickOriginMirrored - vec3(1.0)) * float(bpa)));
    brick = clamp(brick, ivec3(0), ivec3(bpa - 1));

    // Brick-local hit position (authoritative, clamped — same helper + inputs the binary path
    // uses), then combine with the KNOWN brick coordinate for an unambiguous global gridEntry.
    vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2,
                                        coef.octant_mask, BRICK_SIZE_SDF);
    posInBrick = clamp(posInBrick, vec3(0.0), vec3(float(BRICK_SIZE_SDF) - 0.001));
    vec3 gridEntry = brickLocalToGrid(posInBrick, brick, BRICK_SIZE_SDF);

    // marchBrickSdf now sphere-traces starting at THIS leaf brick and, on a brick exit
    // without a crossing, continues into real adjacent leaf bricks via the ESVO traversal
    // machinery (passed a LOCAL COPY of state/coef/stack so the outer loop is untouched).
    // sHit is the total arc-length from gridEntry across however many bricks were traversed.
    vec3  nrm;
    float sHit;
    ivec3 hitBrick;
    if (!marchBrickSdf(g_octreeIdx, brick, gridEntry, gridDirN, state, coef, stack, nrm, sHit, hitBrick)) {
        return false;
    }
    hitNormal = nrm;

    // hitT in the SAME parametrization as the binary leaf (tBias + local-t): convert
    // the grid-voxel arc-length sHit back to [1,2]^3 t-units. gridPos advances by
    // |rayDirLocal|*gridScale per unit t, so Δt = sHit / (|rayDirLocal|*gridScale).
    float gridScale = float(bpa * 8);
    float tHitLocal = state.t_min + sHit / (dirLen * gridScale);
    hitT = tBias + tHitLocal;

    // Inc3 M3: sample per-voxel color and roughness at the hit grid position.
    // M3 perf package (audit A3): resolve the cell's brick address ONCE and read
    // both channels from it, instead of two independent trilinear gathers each
    // re-deriving the same brickCoord/brickLookup for the identical gridHit.
    vec3 gridHit = gridEntry + gridDirN * sHit;
    sampleHitShadingChannels(gridHit, vec3(1.0), 0.5, hitColor, hitRoughness);

    // Best-effort pick/ID: the flat grid index of the brick the crossing was ACTUALLY found in
    // (hitBrick, which may be an adjacent brick reached via the seam-spanning continuation), not
    // just the entry brick — so picking/queries report the true surface brick.
    uint flatIdx = _gridToLookupIdx(hitBrick, bpa);
    hitBrickIndex     = (flatIdx == 0xFFFFFFFFu) ? 0u : flatIdx;
    hitVoxelLinearIdx = 0u;
    return true;
}

// ============================================================================
// ANY-HIT LEAF HANDLERS (Baked-Perf M4 Task 4.2 / audit C1-C2 / Top #7)
// ============================================================================
// Occlusion-only counterparts of handleLeafHitInstanced/handleLeafHitInstancedSdf
// above: same leaf resolution + brick addressing, but never compute a gradient,
// color, or roughness -- TraceWorldShadow (TraceWorld.glsl) only ever needs the
// boolean "is there an occluder here," and both original handlers do real work
// (a full analytic gradient, or up to 32 pool reads for the color+roughness
// trilinear channels) that a shadow/probe-occlusion ray discards immediately.
// hitT is still returned (needed for the caller's [tmin,tmax] span check).

bool handleLeafHitInstancedAnyHit(TraversalState state, RayCoefficients coef,
                                   vec3 rayDir, float tBias,
                                   uvec2 parentDescriptor, uint validMask, uint leafMask,
                                   out float hitT) {
    hitT = 0.0;
    int BRICK_SIZE_VAL = octreeConfig.brickSize;

    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
    if (localChildIdx < 0 || localChildIdx > 7) return false;

    uint leafDescriptorIndex = resolveLeafDescriptorIndex(parentDescriptor, validMask, leafMask,
                                                          localChildIdx);
    uvec2 leafDescriptor = fetchESVONode(leafDescriptorIndex);
    uint  localBrickIdx  = getContourPointer(leafDescriptor);
    if (localBrickIdx == SVO_INVALID_INDEX) return false;

    // Same leaf-entry bridging as handleLeafHitInstanced (identical math, needed to
    // find the DDA's starting voxel) -- only the shading outputs are dropped below.
    float tHit        = state.t_min;
    vec3  rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    vec3  hitPos12    = coef.normOrigin + rayDirLocal * tHit;

    vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2,
                                        coef.octant_mask, BRICK_SIZE_VAL);
    posInBrick = clamp(posInBrick, vec3(0.0), vec3(float(BRICK_SIZE_VAL) - 0.001));

    // marchBrickInstanced's DDA is already a bare occupancy test (no gradient work) --
    // its color/normal/axisMask/voxel outputs are simply discarded here, unused by any
    // any-hit caller.
    vec3 discardColor, discardNormal;
    uint discardAxisMask, discardVoxelIdx;
    vec3 discardBrickLocalPos;
    if (marchBrickInstanced(rayDir, posInBrick, localBrickIdx,
                            discardColor, discardNormal, discardAxisMask,
                            discardBrickLocalPos, discardVoxelIdx)) {
        hitT = tBias + tHit;
#ifdef VIXEN_SHADOW_DBG
        if (g_shadowDbgArm != 0) {
            g_shadowDbgInst     = g_shadowDbgCurInst;
            g_shadowDbgLeafKind = 1;  // binary DDA occupancy
        }
#endif
        return true;
    }
    return false;
}

// SDF variant: mirrors handleLeafHitInstancedSdf's leaf-entry bridging exactly
// (needed to compute gridEntry/gridDirN correctly), but calls marchBrickSdfAnyHit
// instead of marchBrickSdf -- no gradient, no sampleHitShadingChannels.
// sMaxLimitWorld is the caller's remaining [tmin,tmax] budget in WORLD units;
// converted to this brick's grid-arc-length unit the same way marchBrickSdf's
// caller converts sHit back to t (hitT = tBias + t_min + sHit/(dirLen*gridScale)),
// inverted here to bound the march before it ever crosses the light.
bool handleLeafHitInstancedSdfAnyHit(TraversalState state, RayCoefficients coef,
                                     vec3 rayDir, float tBias, float tmax,
                                     inout StackEntry stack[STACK_SIZE],
                                     out float hitT) {
    hitT = 0.0;

    int bpa = int(octreeConfig.bricksPerAxisSdf);
    if (bpa <= 0) return false;
    const int BRICK_SIZE_SDF = 8;

    vec3 rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    vec3 hitPos12    = coef.normOrigin + rayDirLocal * state.t_min;
    float dirLen     = length(rayDirLocal);
    if (dirLen < 1e-12) return false;
    vec3 gridDirN    = rayDirLocal / dirLen;

    vec3 brickOriginMirrored = state.pos;
    if ((coef.octant_mask & 1) == 0) brickOriginMirrored.x = 3.0 - state.scale_exp2 - brickOriginMirrored.x;
    if ((coef.octant_mask & 2) == 0) brickOriginMirrored.y = 3.0 - state.scale_exp2 - brickOriginMirrored.y;
    if ((coef.octant_mask & 4) == 0) brickOriginMirrored.z = 3.0 - state.scale_exp2 - brickOriginMirrored.z;
    ivec3 brick = ivec3(round((brickOriginMirrored - vec3(1.0)) * float(bpa)));
    brick = clamp(brick, ivec3(0), ivec3(bpa - 1));

    vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2,
                                        coef.octant_mask, BRICK_SIZE_SDF);
    posInBrick = clamp(posInBrick, vec3(0.0), vec3(float(BRICK_SIZE_SDF) - 0.001));
    vec3 gridEntry = brickLocalToGrid(posInBrick, brick, BRICK_SIZE_SDF);

    // tmax (world/local-t units, same frame as tBias+t_min) -> remaining grid-arc-length
    // budget, inverting marchBrickSdf's own hitT = tBias + t_min + sHit/(dirLen*gridScale):
    //   sMaxLimit = (tmax - tBias - t_min) * dirLen * gridScale
    // tmax <= 0 (disabled/unbounded caller) maps to a large sentinel span (no clamp).
    float gridScale = float(bpa * 8);
    float sMaxLimit = (tmax > 0.0)
        ? max((tmax - tBias - state.t_min) * dirLen * gridScale, 0.0)
        : 1e6;
    if (tmax > 0.0 && sMaxLimit <= 0.0) return false;  // light is at/behind this leaf's entry -- no room for an occluder

    float sHit;
    if (!marchBrickSdfAnyHit(g_octreeIdx, brick, gridEntry, gridDirN, sMaxLimit, state, coef, stack, sHit)) {
        return false;
    }

    float tHitLocal = state.t_min + sHit / (dirLen * gridScale);
    hitT = tBias + tHitLocal;
    return true;
}

// ============================================================================
// TRAVERSAL LOOP (instanced + LOD)
// ============================================================================
// Mirrors traverseOctree() from VoxelRayMarch.comp, extended with:
//   • base-offset node fetch (via the fetchESVONode macro)
//   • screen-space LOD termination before every non-leaf PUSH
//
// The LOD condition (Laine & Karras 2010, Section 4.4 / CUDA Raycast.inl L181):
//   tc_max * raySizeCoef + raySizeBias >= scale_exp2
// When true the voxel subtends < 1 pixel at the current distance; we shade the
// current (coarser) node instead of descending further.
// raySizeCoef == 0.0 disables LOD entirely (full-detail traversal).

// rayOriginLocal/rayDirLocal/gridT are the CALLER's already-computed ray-vs-AABB-cube
// intersection (octreeConfig.worldToLocal applied to rayOrigin/rayDir, tested against the
// unit cube) — passed in rather than recomputed here, so there is exactly ONE evaluation of
// this math per ray/instance. Two textually-identical GLSL expressions evaluated at different
// call sites are not guaranteed to produce bit-identical floats (the compiler is free to
// reorder/fuse floating-point ops differently per call site absent a `precise` qualifier), so
// recomputing this AABB test a second time here could — right at the razor's-edge silhouette
// of the AABB, where gridT sits within an ULP of 0 — disagree with the caller's already-passed
// cull check and return a false miss for a ray the caller determined does enter the volume.
// This was a real, data-affecting bug (not cosmetic): the SAME traversal is used to query
// voxel data, so a false miss here silently drops real geometry from any caller relying on it.
// ============================================================================
// TIER-CROSSING RAY REMAP (Tiered-ESVO Inc2 M3 Task 6)
// ============================================================================
// Transform a ray from the CURRENT tree's local [1,2) frame into the child
// tree's own [1,2) frame, using ONE TierRef's scale+offset (§3.3 float32-safety
// discipline — a single scale+offset, never an accumulated world matrix).
//
// Mathematical inverse of TierDirection.h's composition (Inc1, ComposeLocalDirection
// / SumTail): that CPU-side code composes a CHILD-frame point into the PARENT's
// frame as  parentPoint = childOriginLocal + (childLocalPoint - 1.5) * childScale
// (SumTail's "centered = localPos - 1.5, accum += centered * scaleCm", read as
// the contribution one hop's local position makes to the ancestor frame). Ray
// traversal needs the OPPOSITE direction (parent point -> child-local point);
// solving the above for childLocalPoint gives:
//   childLocalPoint = (parentPoint - childOriginLocal) / childScale + 1.5
// Direction has no origin/center term (a direction is a derivative of position,
// so the constant "+1.5" drops out exactly as it does in the forward composition):
//   childLocalDir = parentLocalDir / childScale
// (left un-normalized: initRayCoefficients/the traversal are scale-tolerant —
// see the instOrigin/instDir invScale comment in main() for the same argument.)
void remapRayIntoChildFrame(vec3 parentLocalOrigin, vec3 parentLocalDir,
                            TierRef ref,
                            out vec3 childLocalOrigin, out vec3 childLocalDir) {
    vec3 childOrigin = vec3(ref.childOriginLocal[0], ref.childOriginLocal[1], ref.childOriginLocal[2]);
    float invScale = 1.0 / ref.childScale;
    childLocalOrigin = (parentLocalOrigin - childOrigin) * invScale + vec3(1.5);
    childLocalDir    = parentLocalDir * invScale;
}

// ============================================================================
// SINGLE-TREE TRAVERSAL BODY (Tiered-ESVO Inc2 M3 Task 7 — factored out so the
// restart wrapper below can call it twice: once for the ray's home octree, once
// more for a tier-crossing leaf's child octree, WITHOUT recursion — GLSL has
// none). Identical to the pre-M3 traverseOctreeInstanced in every way EXCEPT
// the tier-crossing leaf branch: hitting a farBit==1 leaf exits this function
// as a miss (hit=false) but reports the crossing via tierCrossHit/tierCrossRef
// so the wrapper can remap the ray and re-enter against the child tree — the
// wrapper's job, not this function's (this function only ever talks to ONE
// octree/OctreeConfig, selected by the caller via g_octreeIdx/g_esvoNodeBase/
// g_brickArrayBase exactly as before M3).
// ============================================================================
// Inc3 M8 Task 23: two hop-threaded floats give the crossing LOD gate a genuinely
// camera-anchored, world-unit-correct footprint at EVERY hop (see the Task 23
// derivation doc, Tiered-ESVO-Inc3-M8-Task23-Crossing-Gate-Derivation.md):
//   tWorldBase      — true-world distance from the CAMERA to this hop's own ray start
//                     (0.0 at hop 0; the wrapper accumulates each crossing's own
//                     tierCrossWorldT converted to world units).
//   tLocalUnitWorld — one unit of THIS tree's local [1,2) frame, in true-world units
//                     (1/length(rayDirLocal) at hop 0; *= childScale per crossing —
//                     the remap contract's own physical scale composition, independent
//                     of any child cfg.localToWorld re-embedding convention).
// tierCrossLeafNodeIndex reports the crossing leaf's own descriptor ordinal so the
// wrapper can mip-shade THIS leaf if the child tree is entered and then missed
// (design doc §5.3 semantics — same shadeFromMipSample addressing the LOD/residency
// early-outs already use).
bool traverseOctreeInstancedOnce(vec3 rayOrigin, vec3 rayDir,
                              vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                              float tWorldBase, float tLocalUnitWorld,
                              out vec3 hitColor, out vec3 hitNormal, out float hitT,
                              out float hitRoughness,
                              out uint hitBrickIndex, out uint hitVoxelLinearIdx,
                              out bool tierCrossHit, out uint tierCrossRefIndex,
                              out vec3 tierCrossParentLocalOrigin, out vec3 tierCrossParentLocalDir,
                              out float tierCrossWorldT, out uint tierCrossLeafNodeIndex,
                              inout DebugRaySample debugInfo) {
    hitBrickIndex     = 0u;
    hitVoxelLinearIdx = 0u;
    hitRoughness      = 0.5;
    tierCrossHit       = false;
    tierCrossRefIndex  = 0u;
    tierCrossParentLocalOrigin = vec3(0.0);
    tierCrossParentLocalDir    = vec3(0.0);
    tierCrossWorldT            = 0.0;
    tierCrossLeafNodeIndex     = 0u;

    debugInfo.hitFlag      = 0u;
    debugInfo.exitCode     = DEBUG_EXIT_NONE;
    debugInfo.lastStepMask = 0u;
    debugInfo.iterationCount = 0u;

    if (gridT.y < 0.0) {
        debugInfo.exitCode = DEBUG_EXIT_INVALID_SPAN;
        debugInfo.tMin = gridT.x;
        debugInfo.tMax = gridT.y;
        return false;
    }

    bool rayStartsInside = (gridT.x < 0.0);
    vec3 rayStartWorld;
    float tEntryWorld = 0.0;
    if (rayStartsInside) {
        rayStartWorld = rayOrigin;
        tEntryWorld   = 0.0;
    } else {
        vec3 entryPointLocal = rayOriginLocal + rayDirLocal * (gridT.x + EPSILON);
        rayStartWorld = (octreeConfig.localToWorld * vec4(entryPointLocal, 1.0)).xyz;
        tEntryWorld   = length(rayStartWorld - rayOrigin);
    }

    RayCoefficients coef  = initRayCoefficients(rayDir, rayStartWorld);
    debugInfo.octantMask  = uint(coef.octant_mask);

    StackEntry stack[STACK_SIZE];
    TraversalState state = initTraversalState(coef, stack, rayStartsInside);
    snapshotTraversalState(state, coef, debugInfo);

    ivec2 pixelCoords = ivec2(debugInfo.pixel);
    bool isTracing    = beginRayTrace(pixelCoords);

    if (state.t_min >= state.t_max) {
        debugInfo.exitCode     = DEBUG_EXIT_INVALID_SPAN;
        debugInfo.iterationCount = 0u;
        endRayTrace(false);
        return false;
    }

    int iter = 0;
    for (; iter < MAX_ITERS && state.scale <= octreeConfig.esvoMaxScale; ++iter) {

        uvec2 parent_descriptor = fetchESVONode(state.parentPtr);  // base-offset via macro
        uint validMask   = getValidMask(parent_descriptor);
        uint leafMask    = getLeafMask(parent_descriptor);
        uint childPointer = getChildPointer(parent_descriptor);

        bool isLeaf;
        float tv_max, tx_center, ty_center, tz_center;

        if (checkChildValidity(state, coef, validMask, leafMask,
                               isLeaf, tv_max, tx_center, ty_center, tz_center)) {

            if (isLeaf) {
                float tBias = tEntryWorld;
                recordTraceStep(TRACE_STEP_BRICK_ENTER, state.parentPtr, state.scale,
                                 uint(coef.octant_mask), state.pos, state.t_min, tv_max,
                                 uvec2(0u, 0u));

                // Tiered-ESVO Inc2 M3 Task 6/7 (+ M4 Task 9/10): a farBit==1 leaf is a
                // tier-crossing reference, NOT a brick — checked BEFORE the
                // brickResident/formatId dispatch below (same insertion point
                // Sparse-Mip's own streaming-grace check uses) so a tier-crossing
                // leaf's contourPointer is never misread as a brick index or a
                // mip-fallback node ordinal. This function does not itself remap the
                // ray or re-enter the child tree — that is the wrapper's
                // (traverseOctreeInstanced) job, since a fresh traversal call needs
                // its OWN local state/stack, not this one's (§10: no growing
                // MAX_STACK_DEPTH, no merged stacks). We resolve WHICH leaf child slot
                // was hit here (the same resolveLeafDescriptorIndex the mip-fallback
                // path above already uses) and fetch that leaf's descriptor to check
                // farBit before doing anything brick-related with it. M4 adds two
                // early-outs BEFORE ever reporting a crossing: a screen-space LOD gate
                // (Task 9) and a child-residency check (Task 10) — both fall back to
                // shading this leaf from the PARENT's own mip sample, never touching
                // the child tree at all.
                {
                    int localChildIdxTc = mirroredToLocalOctant(state.idx, coef.octant_mask);
                    if (localChildIdxTc >= 0 && localChildIdxTc <= 7) {
                        uint leafDescriptorIndexTc = resolveLeafDescriptorIndex(
                            parent_descriptor, validMask, leafMask, localChildIdxTc);
                        uvec2 leafDescriptorTc = fetchESVONode(leafDescriptorIndexTc);
                        if (getFarBit(leafDescriptorTc)) {
                            uint tierRefIdxInSlice = getTierRefIndex(leafDescriptorTc);
                            uint absoluteTierRefIdx = octreeConfig.tierRefTableBase + tierRefIdxInSlice;
                            // Bounds-check against the bound buffer's real length — a scene
                            // with NO tier-crossing leaves anywhere binds this as a 1-byte
                            // placeholder (tierRefTable.length()==0), exactly like MipPoolBuffer.
                            if (absoluteTierRefIdx < tierRefTable.length()) {
                                // (History: originally reused the non-leaf LOD-cutoff's own
                                // tv_max-based cone-spread inputs; Inc3 M8 Task 23 re-derived
                                // the DISTANCE argument — see the Task 23 block below — after
                                // Task 20/22 proved the tv_max form is chord-floored and
                                // depth-invariant, i.e. structurally unable to fire at deep
                                // childScale ratios.) The gate's decision is checked
                                // BEFORE ever reporting a crossing. Sub-pixel footprint means
                                // "shade from the PARENT tier's own mip sample at this leaf
                                // node" (the same shadeFromMipSample/nodeIdx-addressing the
                                // streaming-grace/LOD-cutoff paths already use, just applied
                                // to THIS leaf's ordinal instead of a non-leaf parentPtr) —
                                // the child tree is never remapped into or restarted at all.
                                // Gated on raySizeCoef>0.0 exactly like the non-leaf branch
                                // (LOD disabled -> always cross, never skip).
                                //
                                // Inc3 M1 Task 2: gates on the CHILD tree's own finest resolvable
                                // detail (childScale*scale_exp2), not just the parent leaf's own
                                // footprint (scale_exp2 alone, Inc2's childScale==1.0-only gate).
                                // TierRef.childScale is, by definition, "the child cube's size IN
                                // PARENT-LOCAL UNITS" — i.e. exactly the same normalized-size unit
                                // scale_exp2 already uses for the parent leaf. Multiplying (not
                                // dividing) scale_exp2 by childScale is required so the RHS SHRINKS
                                // for a smaller/finer (more magnified) child: a finer child must
                                // resolve to a smaller screen footprint before falling back to the
                                // parent's coarse mip, matching "the child's own finest resolvable
                                // detail" framing. (Task 23 keeps this validated RHS semantics but
                                // expresses it in world units and replaces the LHS distance — see
                                // the Task 23 block below; the pre-Task-23 unity-reduction note is
                                // in the git history.)
                                //
                                // Inc3 M8 Task 23 — the gate's DISTANCE argument was the structural
                                // defect (full derivation: Tiered-ESVO-Inc3-M8-Task23-Crossing-Gate-
                                // Derivation.md). The pre-Task-23 form compared tv_max (this LEAF's
                                // exit-t, FLOORED by the leaf's own chord for any interior-traversing
                                // ray — Task 20's finding) against childScale*scale_exp2. For a child
                                // childScale× smaller than its hosting leaf, the correct firing
                                // distance is far SMALLER than the leaf chord at deep ratios (2^-10),
                                // so the chord floor kept the gate permanently declined at any camera
                                // distance and any construction depth (Task 22's algebraic proof:
                                // 2^-depth cancels from both sides). The footprint model
                                // footprint(D)=D*coef+bias needs D = CAMERA distance to the CHILD
                                // CONTENT, in the same (world) units raySizeCoef is calibrated for:
                                //   tChild          — along-ray t of the child cube's center
                                //                     (childOriginLocal, the proven M5 remap input,
                                //                     lives in the same unmirrored [1,2) space as
                                //                     coef.normOrigin), clamped to this leaf's own
                                //                     [t_min, tv_max] span; replaces tv_max.
                                //   kPhys           — true-world distance per t-unit at THIS hop
                                //                     (== 1.0 exactly at hop 0 by construction).
                                //   worldDistToChild — camera-anchored (tEntryWorld, audit point 2 of
                                //                     the derivation doc: shipped t is CUBE-ENTRY-
                                //                     anchored, so gates were camera-independent for
                                //                     outside cameras) + hop-composed (tWorldBase).
                                //   childWorldSize  — keeps M1 Task 2's validated RHS semantics
                                //                     ("child's finest resolvable detail" =
                                //                     childScale × leaf size) but expressed in WORLD
                                //                     units via tLocalUnitWorld, so a world footprint
                                //                     is finally compared against a world size.
                                // At the real, unoverridden raySizeCoef this makes the crossing a
                                // genuine distance-driven handoff (the child appears at ~4px and
                                // grows continuously — the self-similar law holds at every hop),
                                // while the ordinary non-leaf gate below is UNTOUCHED.
                                float tcChildScale = tierRefTable[absoluteTierRefIdx].childScale;
                                vec3 tcChildOriginLocal = vec3(
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[0],
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[1],
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[2]);
                                float tcDirLen2 = max(dot(rayDirLocal, rayDirLocal), 1e-30);
                                float tChild = clamp(
                                    dot(tcChildOriginLocal - coef.normOrigin, rayDirLocal) / tcDirLen2,
                                    state.t_min, tv_max);
                                float kPhys = sqrt(tcDirLen2) * tLocalUnitWorld;
                                float worldDistToChild = tWorldBase + (tEntryWorld + tChild) * kPhys;
                                float childWorldSize = tcChildScale * state.scale_exp2 * tLocalUnitWorld;
                                bool subPixelFootprint = (pc.raySizeCoef > 0.0 &&
                                    worldDistToChild * pc.raySizeCoef + pc.raySizeBias >= childWorldSize);

                                // Tiered-ESVO Inc2 M4 Task 10: residency reuse. A TierRef
                                // whose child octree is not (yet) brick-resident is, per the
                                // design doc §5.3, "just another miss, serve the parent's mip
                                // sample" case — identical in shape to Task 9's LOD gate,
                                // reusing the SAME mip-fallback exit below rather than a new
                                // state machine. childOctreeIndex is always a valid index into
                                // configs[] (the child tree is uploaded/resident as a sibling
                                // tree exactly like any other octree, per §3.2 — only its BRICK
                                // tier's residency is in question here, mirroring the existing
                                // per-tree brickResident flag every ordinary leaf already checks
                                // below via octreeConfig.brickResident before this swap ever
                                // happens). Peek the child's config directly (configs[] is a
                                // global SSBO array, no g_octreeIdx swap needed to read it).
                                uint childOctreeIdxTc = tierRefTable[absoluteTierRefIdx].childOctreeIndex;
                                bool childNotResident = (configs[childOctreeIdxTc].brickResident == 0u);

                                if (subPixelFootprint || childNotResident) {
                                    hitT = tEntryWorld + state.t_min;
                                    recordEsvoMipArm(0u);  // batch-29 JOB 4: tier-crossing subpixel/non-resident arm
                                    g_mipSampleLevel = 0u; // this crossing leaf is the finest mip rung
                                    if (!shadeFromMipSample(leafDescriptorIndexTc, hitColor, hitNormal)) {
                                        hitColor  = vec3(0.5);
                                        hitNormal = vec3(0.0, 1.0, 0.0);
                                    }
                                    hitBrickIndex     = 0u;
                                    hitVoxelLinearIdx = 0u;
                                    recordTraceStep(TRACE_STEP_HIT, state.parentPtr, state.scale,
                                                     uint(coef.octant_mask), state.pos, state.t_min, hitT,
                                                     uvec2(0u, 0u));
                                    endRayTrace(true);
                                    snapshotTraversalState(state, coef, debugInfo);
                                    debugInfo.hitFlag      = 1u;
                                    debugInfo.exitCode     = DEBUG_EXIT_HIT;
                                    debugInfo.iterationCount = uint(iter + 1);
                                    return true;
                                }

                                tierCrossHit      = true;
                                tierCrossRefIndex = absoluteTierRefIdx;
                                // Inc3 M8 Task 23: this leaf's own descriptor ordinal, so the
                                // wrapper can mip-shade THIS leaf if the child is entered and
                                // then missed (instead of turning the whole leaf into a sky
                                // hole around a childScale-sized child — see wrapper).
                                tierCrossLeafNodeIndex = leafDescriptorIndexTc;
                                // Ray-remap input (Task 6): the CURRENT tree's local [1,2)-frame
                                // ray position/direction at the point of the crossing. rayDirLocal
                                // is already this tree's worldToLocal-rotated direction (computed
                                // once by the caller, passed in); the parent-local ORIGIN at the
                                // hit point is coef.normOrigin + rayDirLocal * t_min (the same
                                // "hitPos12" expression handleLeafHitInstanced/handleLeafHitInstancedSdf
                                // both use to enter brick-local space) — i.e. exactly the point on
                                // the ray where it enters this tier-crossing leaf's [1,2) cell.
                                tierCrossParentLocalOrigin = coef.normOrigin + rayDirLocal * state.t_min;
                                tierCrossParentLocalDir    = rayDirLocal;
                                // The crossing point's own real-world-consistent t (SAME units
                                // handleLeafHitInstanced's hitT=tBias+tHit uses) — the wrapper adds
                                // the child call's own hitT (which comes back as a distance FROM
                                // this crossing point, by construction of the child ray's
                                // parametrization — see traverseOctreeInstanced's derivation
                                // comment) to THIS to get a world-consistent final hitT.
                                tierCrossWorldT = tBias + state.t_min;
                                recordTraceStep(TRACE_STEP_BRICK_EXIT, state.parentPtr, state.scale,
                                                 uint(coef.octant_mask), state.pos, state.t_min, tv_max,
                                                 uvec2(0u, 0u));
                                endRayTrace(false);
                                debugInfo.exitCode     = DEBUG_EXIT_NO_HIT;
                                debugInfo.iterationCount = uint(iter + 1);
                                return false;
                            }
                        }
                    }
                }

                // Sparse-Mip ESVO LOD Inc1 M3 Task 7: "streaming grace" trigger.
                // A non-resident brick (allocated but not yet uploaded — M2) must
                // NOT march (marchBrickInstanced/marchBrickSdf would read
                // zero-filled/garbage buffer contents); fall back to this leaf's
                // own mip sample instead. Checked BEFORE the formatId dispatch so
                // both content formats share the identical mip[nodeIdx] read path.
                bool leafHit = false;
                if (octreeConfig.brickResident == 0u) {
                    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
                    if (localChildIdx >= 0 && localChildIdx <= 7) {
                        uint leafDescriptorIndex = resolveLeafDescriptorIndex(
                            parent_descriptor, validMask, leafMask, localChildIdx);
#ifdef VIXEN_MIP_POLICY
                        // Deep-Field Mip-Accessor Policy (batch-30 stream B): the
                        // streaming-grace fallback (Sparse-Mip Inc1 M3 Task 7) always
                        // sampled THIS leaf's own mip node regardless of the ray's
                        // actual footprint -- correct only by accident at the single
                        // brick rung. Consult the SAME shared footprint->level function
                        // ESVO's ladder walk uses (mipPolicyLevel, SVOTypes.glsl):
                        // leafWorldSize is this leaf's own normalized size (parent's
                        // scale_exp2, halved once -- the same halving executePushPhase
                        // performs on descent), footprint is ESVO's own criterion units
                        // (tv_max*coef+bias, identical to the deliberate-LOD arm below
                        // so both consult the ladder in the SAME space). policyLevel==0
                        // means "the leaf itself is already the correct level" -- same
                        // node the flag-off code samples, so leafDescriptorIndex is left
                        // unchanged (no behavior change, no extra fallback needed).
                        // policyLevel>0 means the footprint has grown past this leaf;
                        // walk up the stack this traversal already populated
                        // (executePushPhase writes stack[state.scale] before every
                        // descent) to the ancestor node at that level and sample IT
                        // instead. If the requested level is out of the stack's
                        // populated range (shouldn't happen -- state.scale bounds the
                        // walk -- but guarded per the fallback contract: policy data
                        // unavailable falls back to the streaming-grace leaf sample,
                        // counted as arm 1 same as today) the streaming-grace leaf
                        // sample is used unchanged.
                        float leafWorldSize = state.scale_exp2 * 0.5;
                        float policyFootprint = tv_max * pc.raySizeCoef + pc.raySizeBias;
                        int policyLevel = mipPolicyLevel(policyFootprint, leafWorldSize,
                                                          state.scale);
                        uint policyNodeOrdinal = leafDescriptorIndex;
                        bool policyLevelAvailable = true;
                        if (policyLevel > 0) {
                            int ancestorScale = state.scale + policyLevel - 1;
                            if (ancestorScale >= 0 && ancestorScale < STACK_SIZE) {
                                policyNodeOrdinal = stack[ancestorScale].parentPtr;
                            } else {
                                policyLevelAvailable = false;
                            }
                        }
                        if (policyLevelAvailable) {
                            recordEsvoMipArm(5u);  // batch-30 stream B: policy-level arm
                            g_mipSampleLevel = uint(policyLevel);
                            leafHit = shadeFromMipSample(policyNodeOrdinal, hitColor, hitNormal);
                        } else {
                            recordEsvoMipArm(1u);  // batch-29 JOB 4: streaming-grace fallback (policy data unavailable)
                            g_mipSampleLevel = 0u;
                            leafHit = shadeFromMipSample(leafDescriptorIndex, hitColor, hitNormal);
                        }
#else
                        recordEsvoMipArm(1u);  // batch-29 JOB 4: streaming-grace non-resident-brick arm
                        g_mipSampleLevel = 0u;
                        leafHit = shadeFromMipSample(leafDescriptorIndex, hitColor, hitNormal);
#endif
                        if (leafHit) {
                            hitT              = tEntryWorld + state.t_min;
                            hitRoughness      = 0.5;
                            hitBrickIndex     = 0u;
                            hitVoxelLinearIdx = 0u;
                        }
                    }
                } else {
                    // Inc3 M3: dispatch the leaf hit-test by content format. Stored-SDF
                    // bricks march the trilinear iso-surface and sample per-voxel color +
                    // roughness; binary bricks DDA voxels (roughness defaults to 0.5).
                    if (octreeConfig.formatId == FORMAT_STORED_SDF) {
                        leafHit = handleLeafHitInstancedSdf(state, coef, rayDir, tBias,
                                                            stack,
                                                            hitColor, hitNormal, hitT,
                                                            hitRoughness,
                                                            hitBrickIndex, hitVoxelLinearIdx);
                    } else {
                        hitRoughness = 0.5;  // binary path: default roughness
                        leafHit = handleLeafHitInstanced(state, coef, rayStartWorld, rayDir, tBias,
                                                         parent_descriptor, validMask, leafMask,
                                                         stack, hitColor, hitNormal, hitT,
                                                         hitBrickIndex, hitVoxelLinearIdx);
                    }
                }
                if (leafHit) {
                    recordTraceStep(TRACE_STEP_HIT, hitBrickIndex, state.scale,
                                     uint(coef.octant_mask), state.pos, state.t_min, hitT,
                                     uvec2(0u, 0u));
                    endRayTrace(true);
                    snapshotTraversalState(state, coef, debugInfo);
                    debugInfo.hitFlag      = 1u;
                    debugInfo.exitCode     = DEBUG_EXIT_HIT;
                    debugInfo.iterationCount = uint(iter + 1);
                    return true;
                }
                recordTraceStep(TRACE_STEP_BRICK_EXIT, state.parentPtr, state.scale,
                                 uint(coef.octant_mask), state.pos, state.t_min, tv_max,
                                 uvec2(0u, 0u));
                state.t_min = tv_max;
                snapshotTraversalState(state, coef, debugInfo);

            } else {
#ifdef LOD_ENABLED
                // Screen-space LOD termination (Laine & Karras 2010, Section 4.4).
                // tc_max is the exit-t of the current voxel; scale_exp2 is its
                // normalized size.  When the projected footprint (tv_max*coef +
                // bias) covers ≥ 1 pixel we stop descending and shade here.
                //
                // Sparse-Mip ESVO LOD Inc1 M3 Task 8: the "deliberate LOD" trigger —
                // distinct from Task 7's "streaming grace" trigger (brickResident==0u
                // above), but landing on the IDENTICAL mip[nodeIdx] read path
                // (shadeFromMipSample) so the two triggers cannot drift apart. This
                // check does NOT look at brick residency at all: even a fully-resident
                // tree stops here once its footprint is sub-pixel — state.parentPtr is
                // this non-leaf node's own index (just fetched at the top of this
                // iteration), the same ordinal MipBake.h bakes a sample for.
                // Batch-27 JOB 2: log ESVO's own operands for rect pixels, same call-
                // site discipline as recordFarFieldGateOperands (called right before the
                // gate's if()). Level (state.scale) is the depth this exact evaluation
                // ran at, recorded unconditionally so both the crossing AND rejecting
                // levels are visible via min/max.
                recordEsvoCutoffOperands(tv_max * pc.raySizeCoef + pc.raySizeBias,
                                          state.scale_exp2, uint(state.scale));
                if (pc.raySizeCoef > 0.0 &&
                    tv_max * pc.raySizeCoef + pc.raySizeBias >= state.scale_exp2) {
                    hitT = tEntryWorld + state.t_min;
                    recordEsvoMipArm(2u);  // batch-29 JOB 4: deliberate LOD screen-space cutoff arm
                    g_mipSampleLevel = uint(max(state.scale - octreeConfig.brickESVOScale, 0));
                    if (!shadeFromMipSample(state.parentPtr, hitColor, hitNormal)) {
                        // No mip coverage (binary/Procedural bodies, or an SDF octree
                        // with no baked mip pool): fall back to the pre-M3 neutral-grey
                        // placeholder shade — no visual regression for those bodies.
                        hitColor  = vec3(0.5);
                        hitNormal = vec3(0.0, 1.0, 0.0);
                    }
                    hitBrickIndex     = 0u;
                    hitVoxelLinearIdx = 0u;
                    endRayTrace(true);
                    snapshotTraversalState(state, coef, debugInfo);
                    debugInfo.hitFlag      = 1u;
                    debugInfo.exitCode     = DEBUG_EXIT_HIT;
                    debugInfo.iterationCount = uint(iter + 1);
                    return true;
                }
#endif
                executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                                 tv_max, tx_center, ty_center, tz_center);
                recordTraceStep(TRACE_STEP_PUSH, state.parentPtr, state.scale,
                                 uint(coef.octant_mask), state.pos, state.t_min, state.t_max,
                                 uvec2(0u, 0u));
                snapshotTraversalState(state, coef, debugInfo);
                continue;
            }
        }

        int step_mask;
        int advanceResult = executeAdvancePhase(state, coef, step_mask);
        debugInfo.lastStepMask = uint(step_mask);
        recordTraceStep(TRACE_STEP_ADVANCE, state.parentPtr, state.scale,
                         uint(coef.octant_mask), state.pos, state.t_min, state.t_max,
                         uvec2(uint(step_mask), uint(advanceResult)));
        snapshotTraversalState(state, coef, debugInfo);

        if (advanceResult == 0) {
            if (state.scale < octreeConfig.esvoMaxScale) {
                state.t_max = stack[state.scale + 1].t_max;
            }
        }

        if (advanceResult == 1) {
            int popResult = executePopPhase(state, coef, stack, step_mask);
            recordTraceStep(TRACE_STEP_POP, state.parentPtr, state.scale,
                             uint(coef.octant_mask), state.pos, state.t_min, state.t_max,
                             uvec2(uint(popResult), 0u));
            snapshotTraversalState(state, coef, debugInfo);
            if (popResult == 1) {
                endRayTrace(false);
                debugInfo.exitCode     = DEBUG_EXIT_STACK;
                debugInfo.iterationCount = uint(iter + 1);
                return false;
            }
        }
    }

    recordTraceStep(TRACE_STEP_MISS, state.parentPtr, state.scale,
                     uint(coef.octant_mask), state.pos, state.t_min, state.t_max,
                     uvec2(0u, 0u));
    endRayTrace(false);
    debugInfo.exitCode     = DEBUG_EXIT_NO_HIT;
    debugInfo.iterationCount = uint(iter);
    return false;
}


// ============================================================================
// TRAVERSAL-RESTART WRAPPER (Tiered-ESVO Inc2 M3 Task 7; generalized to a
// bounded hop LOOP by Inc3 M3 Task 5)
// ============================================================================
// Public entry point (same name/signature as before — main()'s call site is
// UNCHANGED). Runs traverseOctreeInstancedOnce against the ray's home tree
// (g_octreeIdx/g_esvoNodeBase/g_brickArrayBase as set by main() before this
// call). If that call reports a tier-crossing leaf, the wrapper remaps the
// ray into the child tree's local frame (Task 6) and re-enters
// traverseOctreeInstancedOnce against the child — NOT a recursive call (GLSL
// has none): each hop is a fresh, sequential, top-level call with its own
// local StackEntry stack[STACK_SIZE]/TraversalState, so no two tiers are ever
// concurrently live (design doc §10's "parent parked, child active" — never
// simultaneous full stacks).
//
// Inc3 M3: a farBit==1 leaf encountered DURING a child traversal is no longer
// an automatic miss — the SAME restart logic re-fires, hop after hop, up to
// MAX_TIER_HOPS times. The "parked chain" this generalizes to is deliberately
// NOT a stack of full TraversalState/stack[] records (§10 rejects that): the
// only state that ever needs to survive across a hop is this loop's own
// locals (the running hitT composition + the three globals, which are always
// fully restored from a single saved parent-of-hop-0 snapshot at the end) —
// there is nothing per-hop to park beyond what the loop variables below
// already hold, exactly as the M1 Progress Log's single-hop version had
// nothing to park beyond tierCrossWorldT/childRayDirWorldLen.
//
// hitT composition across N hops: hop i's own crossing t (tierCrossWorldT_i)
// is measured in units of hop i's OWN incoming ray direction (curRayDir at
// the time hop i's traverseOctreeInstancedOnce is called) — and that
// direction's magnitude ALREADY reflects the full compounding from every
// earlier hop (childRayDirWorld is built from parentLocalDir/curRayDir, which
// carries every prior hop's own scaling forward). So cumulativeDirLen must be
// the ABSOLUTE magnitude of the CURRENT hop's incoming direction — i.e.
// cumulativeDirLen = |childRayDirWorld_i| (an ASSIGNMENT each hop), NOT a
// running PRODUCT of every |childRayDirWorld| seen so far. (An earlier version
// of this comment/code claimed the product form; GpuTraversalMirror.h's own
// 3-tree chained parity test caught the resulting bug directly — a 2-hop
// chain at childScale=0.5 measured a final multiplier of 8 where the correct,
// independently-derived value is 4, i.e. (1/childScale)^2 double-counted into
// (1/childScale)^3.) The fold is still: hitT = 0; for each hop i: hitT = hitT
// + (tierCrossWorldT_i or finalHit_i.t) * cumulativeDirLen_i, where
// cumulativeDirLen_i is that hop's OWN pre-update value (1.0 at hop 0 — the
// top-level ray's own native, unscaled world units).
//
// Off-boundary tEntryWorld invariant (carried forward from M1's Progress Log):
// traverseOctreeInstancedOnce folds a real arc-length tEntryWorld into its
// returned t/tierCrossWorldT ONLY when the ray starts OUTSIDE the [0,1]^3 grid
// (gridT.x >= 0.0 branch) — see its own rayStartsInside/tEntryWorld comment.
// Every hop's remapped child ray is constructed so childLocalOrigin/
// childGridOrigin puts the ray's start EXACTLY at the tier-crossing point
// (tierCrossParentLocalOrigin, which is itself defined as the point ON the
// ray where it enters the crossing leaf's own [1,2) cell — see
// traverseOctreeInstancedOnce's tierCrossParentLocalOrigin comment), i.e. by
// construction the remapped ray starts AT/INSIDE the child's [0,1]^3 grid
// (rayStartsInside true, or gridT.x essentially 0 at worst due to float
// rounding) for every hop, not just the first. This is NOT a new invariant
// this milestone introduces — it is the SAME invariant Inc2 M3's single hop
// already relied on (tEntryWorld==0 in the child call, hence the plain
// addition being correct-by-construction there too); the hop loop below does
// nothing that could push a LATER hop's entry macroscopically outside its own
// child grid (each hop's remap is the SAME remapRayIntoChildFrame formula
// applied to that hop's own crossing point), so the invariant holds
// uniformly across the whole chain, not merely at hop 0.
const int MAX_TIER_HOPS = 5;

bool traverseOctreeInstanced(vec3 rayOrigin, vec3 rayDir,
                              vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                              out vec3 hitColor, out vec3 hitNormal, out float hitT,
                              out float hitRoughness,
                              out uint hitBrickIndex, out uint hitVoxelLinearIdx,
                              inout DebugRaySample debugInfo) {
    // Save the ORIGINAL (hop-0) per-tree globals once — restored unconditionally
    // before every return, so a caller (main()'s instance loop) never observes
    // any hop's binding state leaking into later per-pixel/per-instance work.
    int originOctreeIdx      = g_octreeIdx;
    int originEsvoNodeBase   = g_esvoNodeBase;
    int originBrickArrayBase = g_brickArrayBase;

    vec3 curRayOrigin = rayOrigin;
    vec3 curRayDir    = rayDir;
    vec3 curRayOriginLocal = rayOriginLocal;
    vec3 curRayDirLocal    = rayDirLocal;
    vec2 curGridT = gridT;

    // Cumulative product of every |childRayDirWorld| seen so far (see the
    // function-header derivation) — 1.0 at hop 0 (the parent tree's own
    // native world units need no scaling), multiplied in per hop thereafter.
    float cumulativeDirLen = 1.0;
    float runningHitT = 0.0;

    // Inc3 M8 Task 23: hop-threaded inputs for the camera-anchored crossing LOD
    // gate (see traverseOctreeInstancedOnce's header + the Task 23 derivation
    // doc). tWorldBase = true-world camera distance to this hop's ray start;
    // tLocalUnitWorld = this hop's local [1,2) unit in true-world units
    // (composes by ×childScale per the remap contract, independent of any
    // child cfg.localToWorld re-embedding convention — NOT the same quantity
    // as cumulativeDirLen, which serves the shipped hitT composition and is
    // untouched).
    float tWorldBase = 0.0;
    float tLocalUnitWorld = 1.0 / max(length(rayDirLocal), 1e-30);

    // Inc3 M8 Task 23: deepest parked crossing leaf, for the child-miss mip
    // fallback (design doc §5.3 — "just another miss, serve the parent's mip
    // sample", the same semantics the LOD/residency early-outs already have).
    // Without this, a taken crossing whose child tree then misses returned a
    // whole-ray MISS, turning the hosting leaf into a sky hole around a
    // childScale-sized child and making the LOD handoff a hard pop.
    bool  fallbackValid = false;
    uint  fallbackLeafNodeIndex = 0u;
    int   fallbackOctreeIdx = 0, fallbackEsvoNodeBase = 0, fallbackBrickArrayBase = 0;
    float fallbackHitT = 0.0;

    for (int hop = 0; hop < MAX_TIER_HOPS; ++hop) {
        bool tierCrossHit;
        uint tierCrossRefIndex;
        vec3 tierCrossParentLocalOrigin, tierCrossParentLocalDir;
        float tierCrossWorldT;
        uint tierCrossLeafNodeIndex;

        bool hit = traverseOctreeInstancedOnce(curRayOrigin, curRayDir, curRayOriginLocal, curRayDirLocal, curGridT,
                                               tWorldBase, tLocalUnitWorld,
                                               hitColor, hitNormal, hitT, hitRoughness,
                                               hitBrickIndex, hitVoxelLinearIdx,
                                               tierCrossHit, tierCrossRefIndex,
                                               tierCrossParentLocalOrigin, tierCrossParentLocalDir,
                                               tierCrossWorldT, tierCrossLeafNodeIndex,
                                               debugInfo);

        if (hit) {
            hitT = runningHitT + hitT * cumulativeDirLen;
            g_octreeIdx      = originOctreeIdx;
            g_esvoNodeBase   = originEsvoNodeBase;
            g_brickArrayBase = originBrickArrayBase;
            return true;
        }
        if (!tierCrossHit) {
            // Ordinary miss at this hop (no further crossing). For hop 0 this
            // is a plain whole-ray miss, exactly as before. For hop >= 1 —
            // i.e. a crossing WAS taken and the child tree then missed — fall
            // back to the deepest parked crossing leaf's own mip sample
            // (Task 23, §5.3 semantics), but ONLY if that leaf has real mip
            // coverage: a mip-less (or genuinely empty-at-coarse-scale) leaf
            // still misses to sky, which keeps every mip-less crossing scene
            // byte-identical to the pre-Task-23 behavior.
            if (fallbackValid) {
                g_octreeIdx      = fallbackOctreeIdx;
                g_esvoNodeBase   = fallbackEsvoNodeBase;
                g_brickArrayBase = fallbackBrickArrayBase;
                vec3 mipColor; vec3 mipNormal;
                recordEsvoMipArm(3u);  // batch-29 JOB 4: tier-crossing child-miss fallback arm
                g_mipSampleLevel = 0u;
                bool mipShaded = shadeFromMipSample(fallbackLeafNodeIndex, mipColor, mipNormal);
                g_octreeIdx      = originOctreeIdx;
                g_esvoNodeBase   = originEsvoNodeBase;
                g_brickArrayBase = originBrickArrayBase;
                if (mipShaded) {
                    hitColor          = mipColor;
                    hitNormal         = mipNormal;
                    hitT              = fallbackHitT;
                    hitRoughness      = 0.5;
                    hitBrickIndex     = 0u;
                    hitVoxelLinearIdx = 0u;
                    return true;
                }
                return false;
            }
            g_octreeIdx      = originOctreeIdx;
            g_esvoNodeBase   = originEsvoNodeBase;
            g_brickArrayBase = originBrickArrayBase;
            return false;
        }

        // --- Tier-crossing restart (Task 6 + 7), one more hop -----------------
        TierRef ref = tierRefTable[tierCrossRefIndex];

        // Inc3 M8 Task 23: park THIS crossing leaf as the (deepest) mip fallback
        // for a downstream child miss, BEFORE the globals swap below. fallbackHitT
        // uses the SHIPPED hitT composition — identical to what Once's own decline
        // path would have produced for this leaf, composed through the chain.
        fallbackValid          = true;
        fallbackLeafNodeIndex  = tierCrossLeafNodeIndex;
        fallbackOctreeIdx      = g_octreeIdx;
        fallbackEsvoNodeBase   = g_esvoNodeBase;
        fallbackBrickArrayBase = g_brickArrayBase;
        fallbackHitT           = runningHitT + tierCrossWorldT * cumulativeDirLen;

        // Inc3 M8 Task 23: hop-composition of the camera-anchored gate inputs.
        // kPhys (true-world per t-unit at the CURRENT hop) == 1.0 exactly at
        // hop 0; tierCrossWorldT is this hop's camera/entry-anchored raw t.
        tWorldBase += tierCrossWorldT * (length(curRayDirLocal) * tLocalUnitWorld);
        tLocalUnitWorld *= ref.childScale;

        vec3 childLocalOrigin, childLocalDir;
        remapRayIntoChildFrame(tierCrossParentLocalOrigin, tierCrossParentLocalDir, ref,
                               childLocalOrigin, childLocalDir);

        // Swap to the child octree exactly the way main()'s instance loop
        // selects ANY octree — mechanically identical to today's per-instance
        // configs[octreeIndex] selection, no new resource type.
        g_octreeIdx      = int(ref.childOctreeIndex);
        g_esvoNodeBase   = configs[g_octreeIdx].nodeArrayBase;
        g_brickArrayBase = configs[g_octreeIdx].brickArrayBase;

        // See traverseOctreeInstancedOnce's own contract: it re-derives its
        // local ray from a WORLD-space origin/dir via octreeConfig.worldToLocal
        // (now the CHILD's config, after the swap above), so a purely-local
        // remapped ray must be round-tripped through the CHILD's own
        // localToWorld to synthesize a matching world-space origin/direction
        // (matrix inverse recovers exactly childLocalOrigin/childLocalDir
        // inside the next call) — identical to the single-hop wrapper's own
        // documented approach, just re-applied every hop.
        mat4 childLocalToWorld = configs[g_octreeIdx].localToWorld;
        vec3 childRayOriginWorld = (childLocalToWorld * vec4(childLocalOrigin - vec3(1.0), 1.0)).xyz;
        vec3 childRayDirWorld    = mat3(childLocalToWorld) * childLocalDir;

        vec3 childGridOrigin = childLocalOrigin - vec3(1.0);
        vec2 childGridT = rayAABBIntersection(childGridOrigin, childLocalDir, vec3(0.0), vec3(1.0));

        // hitT NORMALIZATION (Inc3 M1 Task 1, folded into the running
        // composition per this function's header derivation): this hop's own
        // crossing-point world-t must be scaled by every |childRayDirWorld|
        // accumulated by EARLIER hops (cumulativeDirLen, still at its
        // pre-this-hop value here) before being added to the running total —
        // it is measured in the PREVIOUS hop's world-t units, not the true
        // top-level world frame.
        runningHitT += tierCrossWorldT * cumulativeDirLen;
        // ASSIGN, not multiply-in: childRayDirWorld's own magnitude ALREADY
        // reflects the full compounding from every earlier hop (childLocalDir
        // is built from parentLocalDir, itself derived from curRayDir — the
        // incoming ray for THIS hop, which already carries every prior hop's
        // scaling). Multiplying the OLD cumulativeDirLen into this new
        // (already-absolute, already-compounded) length double-counts every
        // hop beyond the first — caught by GpuTraversalMirror.h's own chained
        // parity test (hop 2's multiplier measured 8 instead of the correct 4
        // at childScale=0.5; see that file's identical fix for the full trace).
        cumulativeDirLen = length(childRayDirWorld);

        curRayOrigin      = childRayOriginWorld;
        curRayDir         = childRayDirWorld;
        curRayOriginLocal = childGridOrigin;
        curRayDirLocal    = childLocalDir;
        curGridT          = childGridT;
    }

    // Hop budget exhausted (MAX_TIER_HOPS consecutive crossings, no leaf/miss
    // resolution) — same discipline as any other bounded-loop exhaustion in
    // this traversal (MAX_ITERS/STACK_SIZE). Inc3 M8 Task 23: an exhausted
    // chain has, by definition, a parked crossing leaf — serve its mip sample
    // (same §5.3 fallback as the child-miss case above) rather than a sky hole.
    if (fallbackValid) {
        g_octreeIdx      = fallbackOctreeIdx;
        g_esvoNodeBase   = fallbackEsvoNodeBase;
        g_brickArrayBase = fallbackBrickArrayBase;
        vec3 mipColor; vec3 mipNormal;
        recordEsvoMipArm(4u);  // batch-29 JOB 4: hop-budget-exhausted fallback arm
        g_mipSampleLevel = 0u;
        bool mipShaded = shadeFromMipSample(fallbackLeafNodeIndex, mipColor, mipNormal);
        g_octreeIdx      = originOctreeIdx;
        g_esvoNodeBase   = originEsvoNodeBase;
        g_brickArrayBase = originBrickArrayBase;
        if (mipShaded) {
            hitColor          = mipColor;
            hitNormal         = mipNormal;
            hitT              = fallbackHitT;
            hitRoughness      = 0.5;
            hitBrickIndex     = 0u;
            hitVoxelLinearIdx = 0u;
            return true;
        }
        return false;
    }
    g_octreeIdx      = originOctreeIdx;
    g_esvoNodeBase   = originEsvoNodeBase;
    g_brickArrayBase = originBrickArrayBase;
    return false;
}

// ============================================================================
// ANY-HIT TRAVERSAL (Baked-Perf M4 Task 4.2 / audit C1-C2 / Top #7)
// ============================================================================
// Occlusion-only counterpart of traverseOctreeInstancedOnce above: SAME control
// flow (leaf/non-leaf dispatch, tier-crossing detection, LOD cutoff, PUSH/
// ADVANCE/POP state machine -- all reused verbatim via the shared
// checkChildValidity/executePushPhase/executeAdvancePhase/executePopPhase
// helpers), but:
//   (a) leaf hits go through the any-hit leaf handlers (no gradient/color/
//       roughness payload) instead of handleLeafHitInstanced[Sdf];
//   (b) the streaming-grace and LOD-cutoff mip-fallback cases use
//       mipHasCoverage (one float read) instead of shadeFromMipSample (which
//       also reads+returns a color sample this caller would discard);
//   (c) tmax is a REAL parameter here (traverseOctreeInstancedOnce's callers
//       never passed one -- TraceWorld's nearest-hit accumulation used its own
//       bestT reject instead): a leaf/mip hit whose hitT falls outside
//       [tmin,tmax] is not reported as an occluder, matching TraceWorldShadow's
//       existing post-hoc tmin/tmax check but catching it at the SOURCE so the
//       any-hit march itself is bounded by the light distance, not just its
//       return value discarded after a full unbounded march (audit C2's own
//       "span clamped at light distance" spec).
// No debugInfo/DebugRaySample threading -- any-hit shadow/probe rays are not a
// debug-visualization target (TraceWorldShadow's own contract already has no
// pixel/dbg concept to snapshot); recordTraceStep/beginRayTrace/endRayTrace are
// simply omitted rather than passed a synthesized dummy.
// ============================================================================
bool traverseOctreeInstancedOnceAnyHit(vec3 rayOrigin, vec3 rayDir,
                              vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                              float tmin, float tmax,
                              float tWorldBase, float tLocalUnitWorld,
                              out bool tierCrossHit, out uint tierCrossRefIndex,
                              out vec3 tierCrossParentLocalOrigin, out vec3 tierCrossParentLocalDir,
                              out float tierCrossWorldT) {
    tierCrossHit       = false;
    tierCrossRefIndex  = 0u;
    tierCrossParentLocalOrigin = vec3(0.0);
    tierCrossParentLocalDir    = vec3(0.0);
    tierCrossWorldT            = 0.0;

    if (gridT.y < 0.0) return false;

    bool rayStartsInside = (gridT.x < 0.0);
    vec3 rayStartWorld;
    float tEntryWorld = 0.0;
    if (rayStartsInside) {
        rayStartWorld = rayOrigin;
        tEntryWorld   = 0.0;
    } else {
        vec3 entryPointLocal = rayOriginLocal + rayDirLocal * (gridT.x + EPSILON);
        rayStartWorld = (octreeConfig.localToWorld * vec4(entryPointLocal, 1.0)).xyz;
        tEntryWorld   = length(rayStartWorld - rayOrigin);
    }

    RayCoefficients coef  = initRayCoefficients(rayDir, rayStartWorld);

    StackEntry stack[STACK_SIZE];
    TraversalState state = initTraversalState(coef, stack, rayStartsInside);

    if (state.t_min >= state.t_max) return false;

    int iter = 0;
    for (; iter < MAX_ITERS && state.scale <= octreeConfig.esvoMaxScale; ++iter) {

        uvec2 parent_descriptor = fetchESVONode(state.parentPtr);
        uint validMask   = getValidMask(parent_descriptor);
        uint leafMask    = getLeafMask(parent_descriptor);
        uint childPointer = getChildPointer(parent_descriptor);

        bool isLeaf;
        float tv_max, tx_center, ty_center, tz_center;

        if (checkChildValidity(state, coef, validMask, leafMask,
                               isLeaf, tv_max, tx_center, ty_center, tz_center)) {

            if (isLeaf) {
                float tBias = tEntryWorld;

                // Tier-crossing check: identical structure to traverseOctreeInstancedOnce's
                // own block (see that function for the full derivation) -- only the
                // sub-pixel/non-resident fallback's PAYLOAD differs (coverage test, not shade).
                {
                    int localChildIdxTc = mirroredToLocalOctant(state.idx, coef.octant_mask);
                    if (localChildIdxTc >= 0 && localChildIdxTc <= 7) {
                        uint leafDescriptorIndexTc = resolveLeafDescriptorIndex(
                            parent_descriptor, validMask, leafMask, localChildIdxTc);
                        uvec2 leafDescriptorTc = fetchESVONode(leafDescriptorIndexTc);
                        if (getFarBit(leafDescriptorTc)) {
                            uint tierRefIdxInSlice = getTierRefIndex(leafDescriptorTc);
                            uint absoluteTierRefIdx = octreeConfig.tierRefTableBase + tierRefIdxInSlice;
                            if (absoluteTierRefIdx < tierRefTable.length()) {
                                float tcChildScale = tierRefTable[absoluteTierRefIdx].childScale;
                                vec3 tcChildOriginLocal = vec3(
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[0],
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[1],
                                    tierRefTable[absoluteTierRefIdx].childOriginLocal[2]);
                                float tcDirLen2 = max(dot(rayDirLocal, rayDirLocal), 1e-30);
                                float tChild = clamp(
                                    dot(tcChildOriginLocal - coef.normOrigin, rayDirLocal) / tcDirLen2,
                                    state.t_min, tv_max);
                                // tWorldBase/tLocalUnitWorld (the M8 Task 23 camera-anchored gate)
                                // are this function's own parameters -- the any-hit wrapper below
                                // hop-threads them exactly the same way the shading wrapper does.
                                float kPhys = sqrt(tcDirLen2) * tLocalUnitWorld;
                                float worldDistToChild = tWorldBase + (tEntryWorld + tChild) * kPhys;
                                float childWorldSize = tcChildScale * state.scale_exp2 * tLocalUnitWorld;
                                bool subPixelFootprint = (pc.raySizeCoef > 0.0 &&
                                    worldDistToChild * pc.raySizeCoef + pc.raySizeBias >= childWorldSize);

                                uint childOctreeIdxTc = tierRefTable[absoluteTierRefIdx].childOctreeIndex;
                                bool childNotResident = (configs[childOctreeIdxTc].brickResident == 0u);

                                if (subPixelFootprint || childNotResident) {
                                    // M10 Task 10.2 MECH 1 fix: a coarse mipHasCoverage bit reports
                                    // "occupied somewhere in this leaf's footprint," which for a
                                    // dilated-band baked brick (kBand exterior shell, see
                                    // marchBrickSdfAnyHit below) is true across the whole +band
                                    // shell -- NOT "the true surface is here." That conservative
                                    // coverage-bit fallback is correct for the PRIMARY visible-hit
                                    // path (worst case: one extra shaded sample) but is a false
                                    // occluder for a shadow/any-hit ray, which only cares about a
                                    // real surface crossing. Any-hit rays therefore never accept
                                    // this fallback as a hit; the causation A/B (env
                                    // VIXEN_SHADOW_NO_MIP_ANYHIT, proven live) recovered exactly
                                    // this 25/57-pixel false-occlusion set, so `return false`
                                    // unconditionally reproduces that recovery as the default path.
                                    float hitTMip = tEntryWorld + state.t_min;
#ifdef VIXEN_SHADOW_DBG
                                    if (g_shadowDbgArm != 0 && mipHasCoverage(leafDescriptorIndexTc) &&
                                        hitTMip >= tmin && hitTMip <= tmax) {
                                        g_shadowDbgInst = g_shadowDbgCurInst; g_shadowDbgLeafKind = 3; g_shadowDbgSHitGrid = hitTMip;
                                    }
#endif
                                    return false;
                                }

                                tierCrossHit      = true;
                                tierCrossRefIndex = absoluteTierRefIdx;
                                tierCrossParentLocalOrigin = coef.normOrigin + rayDirLocal * state.t_min;
                                tierCrossParentLocalDir    = rayDirLocal;
                                tierCrossWorldT = tBias + state.t_min;
                                return false;
                            }
                        }
                    }
                }

                bool leafHit = false;
                float hitTLeaf = 0.0;
                if (octreeConfig.brickResident == 0u) {
                    // M10 Task 10.2 MECH 1 fix: same coarse-coverage rationale as the
                    // tier-crossing site above -- a not-yet-resident brick's mip coverage
                    // bit is a conservative "occupied somewhere here" signal, not a real
                    // surface crossing, so an any-hit/shadow ray must not accept it as an
                    // occluder. leafHit stays false; only the debug snapshot still fires.
                    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
                    if (localChildIdx >= 0 && localChildIdx <= 7) {
                        uint leafDescriptorIndex = resolveLeafDescriptorIndex(
                            parent_descriptor, validMask, leafMask, localChildIdx);
#ifdef VIXEN_SHADOW_DBG
                        if (g_shadowDbgArm != 0 && mipHasCoverage(leafDescriptorIndex)) {
                            hitTLeaf = tEntryWorld + state.t_min;
                            g_shadowDbgInst = g_shadowDbgCurInst; g_shadowDbgLeafKind = 4; g_shadowDbgSHitGrid = hitTLeaf;
                        }
#endif
                    }
                } else {
                    if (octreeConfig.formatId == FORMAT_STORED_SDF) {
                        leafHit = handleLeafHitInstancedSdfAnyHit(state, coef, rayDir, tBias, tmax,
                                                                  stack, hitTLeaf);
                    } else {
                        leafHit = handleLeafHitInstancedAnyHit(state, coef, rayDir, tBias,
                                                               parent_descriptor, validMask, leafMask,
                                                               hitTLeaf);
                    }
                }
                if (leafHit && hitTLeaf >= tmin && hitTLeaf <= tmax) {
                    return true;
                }
                state.t_min = tv_max;

            } else {
#ifdef LOD_ENABLED
                if (pc.raySizeCoef > 0.0 &&
                    tv_max * pc.raySizeCoef + pc.raySizeBias >= state.scale_exp2) {
                    // M10 Task 10.2 MECH 1 fix: same coarse-coverage rationale -- a
                    // LOD sub-pixel non-leaf's whole-footprint coverage bit is not a
                    // real surface crossing, so any-hit rays must not accept it.
                    float hitTMip = tEntryWorld + state.t_min;
#ifdef VIXEN_SHADOW_DBG
                    if (g_shadowDbgArm != 0 && mipHasCoverage(state.parentPtr) &&
                        hitTMip >= tmin && hitTMip <= tmax) {
                        g_shadowDbgInst = g_shadowDbgCurInst; g_shadowDbgLeafKind = 5; g_shadowDbgSHitGrid = hitTMip;
                    }
#endif
                    return false;
                }
#endif
                executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                                 tv_max, tx_center, ty_center, tz_center);
                continue;
            }
        }

        int step_mask;
        int advanceResult = executeAdvancePhase(state, coef, step_mask);

        if (advanceResult == 0) {
            if (state.scale < octreeConfig.esvoMaxScale) {
                state.t_max = stack[state.scale + 1].t_max;
            }
        }

        if (advanceResult == 1) {
            int popResult = executePopPhase(state, coef, stack, step_mask);
            if (popResult == 1) return false;
        }
    }

    return false;
}

// ============================================================================
// ANY-HIT TRAVERSAL-RESTART WRAPPER (Baked-Perf M4 Task 4.2)
// ============================================================================
// Occlusion-only counterpart of traverseOctreeInstanced below: same bounded
// tier-hop loop (MAX_TIER_HOPS, remapRayIntoChildFrame, the identical hitT/
// cumulativeDirLen composition this file's derivation comment above already
// covers) reused verbatim, calling traverseOctreeInstancedOnceAnyHit per hop
// instead of the shading "once". tmin/tmax are re-expressed in EACH hop's own
// local t-units the same way the shading wrapper's tWorldBase/tLocalUnitWorld
// convert a world distance into hop-local units, just inverted (world tmax ->
// this hop's local tmax) since the any-hit "once" function's own reject uses
// hop-local t, not world t (a leaf hit's hitT there is tBias+tHit, in the
// CURRENT hop's own frame -- see traverseOctreeInstancedOnce's identical
// hitT=tBias+tHit convention).
// ============================================================================
bool traverseOctreeInstancedAnyHit(vec3 rayOrigin, vec3 rayDir,
                              vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                              float tmin, float tmax) {
    int originOctreeIdx      = g_octreeIdx;
    int originEsvoNodeBase   = g_esvoNodeBase;
    int originBrickArrayBase = g_brickArrayBase;

    vec3 curRayOrigin = rayOrigin;
    vec3 curRayDir    = rayDir;
    vec3 curRayOriginLocal = rayOriginLocal;
    vec3 curRayDirLocal    = rayDirLocal;
    vec2 curGridT = gridT;

    float cumulativeDirLen = 1.0;
    float runningHitT = 0.0;

    // Hop-threaded camera-anchored gate inputs (mirrors the shading wrapper's
    // tWorldBase/tLocalUnitWorld, M8 Task 23) -- plain locals updated per hop,
    // passed to traverseOctreeInstancedOnceAnyHit as real parameters.
    float tWorldBase = 0.0;
    float tLocalUnitWorld = 1.0 / max(length(rayDirLocal), 1e-30);

    for (int hop = 0; hop < MAX_TIER_HOPS; ++hop) {
        bool tierCrossHit;
        uint tierCrossRefIndex;
        vec3 tierCrossParentLocalOrigin, tierCrossParentLocalDir;
        float tierCrossWorldT;

        // This hop's own [tmin,tmax] window, in ITS local hitT convention
        // (tBias+tHit, tBias being THIS hop's tEntryWorld -- see the shading
        // wrapper's tierCrossWorldT/runningHitT derivation, which composes the
        // SAME way): world tmin/tmax minus everything already walked
        // (runningHitT), scaled back by this hop's own direction magnitude.
        float hopTmin = (tmin - runningHitT) / max(cumulativeDirLen, 1e-30);
        float hopTmax = (tmax - runningHitT) / max(cumulativeDirLen, 1e-30);

        bool hit = traverseOctreeInstancedOnceAnyHit(curRayOrigin, curRayDir, curRayOriginLocal, curRayDirLocal, curGridT,
                                               hopTmin, hopTmax,
                                               tWorldBase, tLocalUnitWorld,
                                               tierCrossHit, tierCrossRefIndex,
                                               tierCrossParentLocalOrigin, tierCrossParentLocalDir,
                                               tierCrossWorldT);

        if (hit) {
            g_octreeIdx      = originOctreeIdx;
            g_esvoNodeBase   = originEsvoNodeBase;
            g_brickArrayBase = originBrickArrayBase;
            return true;
        }
        if (!tierCrossHit) {
            g_octreeIdx      = originOctreeIdx;
            g_esvoNodeBase   = originEsvoNodeBase;
            g_brickArrayBase = originBrickArrayBase;
            return false;
        }

        TierRef ref = tierRefTable[tierCrossRefIndex];

        tWorldBase += tierCrossWorldT * (length(curRayDirLocal) * tLocalUnitWorld);
        tLocalUnitWorld *= ref.childScale;

        vec3 childLocalOrigin, childLocalDir;
        remapRayIntoChildFrame(tierCrossParentLocalOrigin, tierCrossParentLocalDir, ref,
                               childLocalOrigin, childLocalDir);

        g_octreeIdx      = int(ref.childOctreeIndex);
        g_esvoNodeBase   = configs[g_octreeIdx].nodeArrayBase;
        g_brickArrayBase = configs[g_octreeIdx].brickArrayBase;

        mat4 childLocalToWorld = configs[g_octreeIdx].localToWorld;
        vec3 childRayOriginWorld = (childLocalToWorld * vec4(childLocalOrigin - vec3(1.0), 1.0)).xyz;
        vec3 childRayDirWorld    = mat3(childLocalToWorld) * childLocalDir;

        vec3 childGridOrigin = childLocalOrigin - vec3(1.0);
        vec2 childGridT = rayAABBIntersection(childGridOrigin, childLocalDir, vec3(0.0), vec3(1.0));

        runningHitT += tierCrossWorldT * cumulativeDirLen;
        cumulativeDirLen = length(childRayDirWorld);

        curRayOrigin      = childRayOriginWorld;
        curRayDir         = childRayDirWorld;
        curRayOriginLocal = childGridOrigin;
        curRayDirLocal    = childLocalDir;
        curGridT          = childGridT;
    }

    g_octreeIdx      = originOctreeIdx;
    g_esvoNodeBase   = originEsvoNodeBase;
    g_brickArrayBase = originBrickArrayBase;
    return false;
}

// The FORMAT_BINARY coarse-grid DDA twins (traverseCoarseGridInstanced/AnyHit)
// were DELETED (slice-2 round-3 validator): they can never be correctly invoked
// today — the binary Serialize()/Concatenate() never populate
// brickGridLookup/brickLookupBase, so a FORMAT_BINARY octree has no lookup table
// to DDA over (it binds a 1-byte placeholder; every read was out-of-bounds).
// Re-introduce them only after the binary bake writes the lookup (invert
// brickGridToBrickView exactly as SerializeSdf does, stamp brickLookupBase in
// Concatenate(), upload a real buffer in BodyOctreeSceneNode). Git blame on this
// comment finds the last working copy.

// ============================================================================
// COARSE-GRID DDA TRAVERSAL -- STORED_SDF (W-BRICKMAP Slice 2, round 3 retarget)
// ============================================================================
// The coarse-grid DDA backend (STORED_SDF only). Round-2 scoped this
// backend to FORMAT_BINARY only; verified WRONG (round-3 root-cause finding):
// brickGridLookup/brickLookupBase are populated ONLY by the SDF serialization
// path (SerializeSdf/ConcatenateSdf, ShellOctreeGpu.h) -- the binary Serialize/
// Concatenate never touch them, so a FORMAT_BINARY octree's brickLookup reads
// were out-of-bounds against a 1-byte placeholder. STORED_SDF is the format
// this backend can actually serve; FORMAT_BINARY now stays on the ESVO path
// (see the retargeted call-site condition in TraceWorld.glsl).
//
// Same DDA loop as the binary twin (identical cell/step/enterAxis machinery --
// bricksPerAxis/brickLookupBase/brickSize are ALL populated for STORED_SDF too:
// SerializeSdf stamps c.bricksPerAxis = oct->bricksPerAxis (the SAME field the
// binary path uses, byte 24) in addition to bricksPerAxisSdf (byte 204, the
// ESVO-leaf-hit path's own field) -- both hold the identical brick-grid side
// length for an SDF octree, so this loop's bpa/lookup indexing is unchanged
// from the binary twin, byte-for-byte). Only the per-cell refine call differs:
// marchBrickSdfCell (StoredSdf.glsl) instead of marchBrickInstanced -- a
// single-brick trilinear sphere-trace with NO ESVO-hop continuation (the DDA's
// own cell loop already provides that continuation, exactly as it already does
// for a marchBrickInstanced miss below).
// ============================================================================
// ROUND-5 NOTE (validator-mandated): this whole function is COMPILED OUT on any
// composed boot where the device has VK_KHR_ray_query (TraceWorld.glsl:549
// gives VIXEN_RTQUERY_TRAVERSAL precedence in the #ifdef/#elif chain) -- on
// this machine's 3060, VIXEN_COMPOSED_TRAVERSAL always resolves to RTQUERY
// (BuildRenderGraph.cpp:1610-1631), so edits here are dead code for every
// composed boot; only a STANDALONE VIXEN_BRICKMAP_TRAVERSAL boot (no RTQUERY,
// no COMPOSED) actually executes this body. The instRenderScale fix below is
// correct arithmetic and stays live on non-rayQuery machines / forced-DDA
// boots -- KEEP it.
bool traverseCoarseGridInstancedSdf(vec3 rayOrigin, vec3 rayDir,
                                     vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                                     float instRenderScale,
                                     out vec3 hitColor, out vec3 hitNormal, out float hitT,
                                     out float hitRoughness,
                                     out uint hitBrickIndex, out uint hitVoxelLinearIdx,
                                     inout DebugRaySample debugInfo) {
    hitColor = vec3(0.0); hitNormal = vec3(0.0); hitT = 0.0; hitRoughness = 0.5;
    hitBrickIndex = 0u; hitVoxelLinearIdx = 0u;
    g_lastFootprintRegime = 1u;
    g_mipSampleLevel = 0u;
    debugInfo.hitFlag = 0u;
    debugInfo.exitCode = DEBUG_EXIT_NONE;

    if (gridT.y < 0.0) { debugInfo.exitCode = DEBUG_EXIT_INVALID_SPAN; return false; }

    int bpa = octreeConfig.bricksPerAxis;
    if (bpa <= 0) { debugInfo.exitCode = DEBUG_EXIT_NONE; return false; }
    float bpaF = float(bpa);

    bool rayStartsInside = (gridT.x < 0.0);
    float tEnter = rayStartsInside ? 0.0 : (gridT.x + EPSILON);
    vec3 pos = rayOriginLocal + rayDirLocal * tEnter;
    ivec3 cell = clamp(ivec3(floor(pos * bpaF)), ivec3(0), ivec3(bpa - 1));

    ivec3 stepDir; vec3 tDelta; vec3 tMax;
    for (int axis = 0; axis < 3; ++axis) {
        float d = rayDirLocal[axis];
        if (abs(d) < DIR_EPSILON) {
            stepDir[axis] = 0; tDelta[axis] = 1e30; tMax[axis] = 1e30;
        } else {
            stepDir[axis] = d > 0.0 ? 1 : -1;
            tDelta[axis] = (1.0 / bpaF) / abs(d);
            float nextBoundary = (float(cell[axis]) + (d > 0.0 ? 1.0 : 0.0)) / bpaF;
            tMax[axis] = (nextBoundary - rayOriginLocal[axis]) / d;
        }
    }

    vec3 rayStartWorld;
    if (rayStartsInside) { rayStartWorld = rayOrigin; }
    else { rayStartWorld = (octreeConfig.localToWorld * vec4(pos, 1.0)).xyz; }
    float tBias = length(rayStartWorld - rayOrigin);

#ifdef VIXEN_COMPOSITION_COUNTERS
    // E6-T1: routed through the shared classifyFootprintRegime (SceneBindings.glsl,
    // defined above) instead of the former inline duplicate. Same formula, same inputs.
    float compositionEntryDirLen = length(rayDirLocal);
    if (compositionEntryDirLen >= 1e-12) {
        float compositionWorldDist = 0.5 * (tEnter + gridT.y) * instRenderScale;
        float compositionCellWorldSize =
            ((1.0 / bpaF) / compositionEntryDirLen) * instRenderScale;
        g_lastFootprintRegime = classifyFootprintRegime(
            compositionWorldDist, compositionCellWorldSize, pc.raySizeCoef, pc.raySizeBias, pc.cosmicK);
    }
#endif

#ifdef VIXEN_MIP_POLICY
    // BATCH-35 ENTRY-POINT DISPATCH (USER RULING 2026-08-08 "DDA IS THE
    // EXCEPTION, MIP SAMPLING IS THE DEFAULT" -- deep-field-mip-policy design
    // doc): decide the regime BEFORE the march starts, not mid-march per-cell
    // (the batch-33/34 gate below, now demoted to a SAFETY NET -- see its own
    // comment).
    //
    // BATCH-36 ANCHOR FIX (deep-field-mip-policy design doc, "Anchoring
    // rule"): batch 35 anchored the footprint at tEnter -- the ray's nearest
    // approach to the instance AABB (gridT.x) -- which systematically
    // UNDER-estimates the footprint (measured: gate LHS 1.86984 -> 0.375855,
    // -80%), since tEnter is the closest point on the whole span the ray
    // could possibly resolve at, not where a non-marching ray actually
    // resolves. A ray that takes the mip path here never marches at all, so
    // there is no single "resolved cell" to anchor on the way a DDA hit-cell
    // or an RT candidate's per-instance slab test would give one for free;
    // the representative distance for the whole traversed span is its
    // MIDPOINT: gridT.x is already tEnter (this function's own local, see
    // above), and gridT.y is the same slab test's far-face exit t for this
    // instance -- both already computed by the caller before this function
    // runs, so the midpoint costs one add and one multiply, no extra trace.
    // This mirrors the SEMANTICS RayQueryTraversal.glsl's per-candidate gate
    // achieves structurally (each RT candidate's tCellEnter is its own
    // slab-test entry into ITS candidate span, which lands far deeper into
    // the scene than DDA's single whole-grid tEnter) without introducing a
    // second traversal: candidate (c) from the design doc ("resolve the
    // candidate cell first, then anchor there") would require walking the
    // DDA to find the first occupied cell before deciding, which defeats the
    // entire march-avoidance point of an entry-point decision. `cell` (used
    // below for the brick-lookup index) stays anchored at the true entry
    // point -- only the DISTANCE used to size the footprint moves to
    // mid-span; the design doc's "anchoring rule" targets the footprint,
    // not which cell the dispatch starts scanning from.
    {
        float entryDirLen = length(rayDirLocal);
        if (entryDirLen >= 1e-12) {
            float entryMidT = 0.5 * (tEnter + gridT.y);
            float entryWorldDist = entryMidT * instRenderScale;
            float entryCellWorldSize = ((1.0 / bpaF) / entryDirLen) * instRenderScale;
            const float kEntryBrickSize = 8.0;  // BRICK_SIZE_SDF, matches the mid-march gate
            float entryAdmitFootprint = entryCellWorldSize / kEntryBrickSize;  // finest ladder rung == one voxel
            float entryGateLhs = entryWorldDist * pc.raySizeCoef + pc.raySizeBias;
            recordEntryGateLhs(entryGateLhs);  // batch-38: the entry gate's OWN probe
            bool entryPolicyAdmits = pc.raySizeCoef > 0.0 &&
                entryGateLhs >= entryAdmitFootprint;
#ifdef VIXEN_REGIME3
            // Deep-field-mip-policy design doc, regime 3 (cosmic accumulation) first
            // slice: when the footprint has grown past K*cell (strictly coarser than
            // the regime-2 mip-hit rung, entryAdmitFootprint above), the ray stops
            // being a hit-finder and becomes a transmittance accumulator -- per coarse
            // cell at the matched level, sample (color, coverage), C += T*cov*color,
            // T *= (1-cov), early-out at T~eps. This slice does NOT composite over
            // deeper content (out of scope per the task ledger) -- it commits the
            // accumulated color as the hit, same return shape as the regime-2 mip-hit
            // path just below, so it is independently testable (counters + census)
            // without the composed/RT integration a later slice adds.
            bool regime3Admits = entryPolicyAdmits &&
                entryGateLhs >= pc.cosmicK * entryCellWorldSize;
            if (regime3Admits) {
                recordRegime3Entry();
                const float kRegime3Eps = 0.02;      // T~eps early-out (design doc)
                const int kRegime3MaxCells = 64;      // cell-budget cap, mirrors maxIters' role below
                vec3 accumColor = vec3(0.0);
                float T = 1.0;
                ivec3 walkCell = cell;
                float walkTMax_x = tMax.x, walkTMax_y = tMax.y, walkTMax_z = tMax.z;
                bool anySampled = false;
                bool earlyOut = false;
                for (int walkIter = 0; walkIter < kRegime3MaxCells; ++walkIter) {
                    if (any(lessThan(walkCell, ivec3(0))) || any(greaterThanEqual(walkCell, ivec3(bpa)))) break;
                    // Advance to this cell's exit t (same DDA step the ordinary march
                    // below performs) BEFORE sampling, so worldDistToCell below anchors
                    // on the cell's own span like the entry/mid-march gates do.
                    float cellTEnter = (walkIter == 0) ? tEnter : min(walkTMax_x, min(walkTMax_y, walkTMax_z));
                    int walkAxis = (walkTMax_x <= walkTMax_y && walkTMax_x <= walkTMax_z) ? 0
                                  : (walkTMax_y <= walkTMax_z) ? 1 : 2;
                    float cellTExit = walkTMax_x;
                    if (walkAxis == 1) cellTExit = walkTMax_y;
                    else if (walkAxis == 2) cellTExit = walkTMax_z;
                    float cellMidT = 0.5 * (cellTEnter + cellTExit);
                    float cellWorldDist = cellMidT * instRenderScale;

                    uint walkLookupBase = octreeConfig.brickLookupBase;
                    uint walkFlatIdx = uint(walkCell.z * bpa * bpa + walkCell.y * bpa + walkCell.x);
                    uint walkLocalBrickIdx = brickLookup[walkLookupBase + walkFlatIdx];
                    if (walkLocalBrickIdx != 0xFFFFFFFFu) {
                        int walkOctantMask = octantMaskFromDir(rayDirLocal);
                        int walkDepth = farFieldDescentDepth(octreeConfig.bricksPerAxis);
                        uint walkNodeOrdinal;
                        uint walkSampledLevel;
                        float walkWorldDist = cellWorldDist;
#ifdef VIXEN_REGIME3_LEVEL_FLOOR
                        // Batch-48 level-floor probe: raise only the walk's
                        // effective footprint before the existing canonical
                        // descendToNodeOrdinal/mipPolicyLevel selection. This is
                        // equivalent to max(footprint level, L), while the tree
                        // depth cap prevents a shift beyond the available ladder.
                        int walkFloorLevel = clamp(VIXEN_REGIME3_LEVEL_FLOOR, 0, walkDepth);
                        float walkFloorFootprint = entryCellWorldSize *
                            float(1u << uint(walkFloorLevel));
                        float walkFootprint = cellWorldDist * pc.raySizeCoef + pc.raySizeBias;
                        walkFootprint = max(walkFootprint, walkFloorFootprint);
                        if (pc.raySizeCoef > 0.0) {
                            walkWorldDist = max(cellWorldDist,
                                (walkFootprint - pc.raySizeBias) / pc.raySizeCoef);
                        }
#endif
                        bool walkReachedBrick = descendToNodeOrdinal(walkCell, walkOctantMask, walkDepth,
                                                                      walkWorldDist, entryCellWorldSize,
                                                                      walkNodeOrdinal, walkSampledLevel);
                        if (walkReachedBrick) {
                            g_mipSampleLevel = walkSampledLevel;
                            vec2 walkSdf = readMipSample(walkNodeOrdinal, SEM_SDF, 0.0);
                            if (walkSdf.y > 0.0) {
                                vec2 walkColorSample = readMipSample(walkNodeOrdinal, SEM_COLOR, 0.5);
                                vec3 walkColor = walkColorSample.y > 0.0 ? vec3(walkColorSample.x) : vec3(0.5);
                                float walkCov = clamp(walkSdf.y, 0.0, 1.0);
                                recordWalkCov(walkCov);  // part-1 audit probe
                                recordWalkSampledLevel(walkSampledLevel);  // part-1 audit probe
                                accumColor += T * walkCov * walkColor;
                                T *= (1.0 - walkCov);
                                anySampled = true;
                                if (T < kRegime3Eps) { earlyOut = true; break; }
                            }
                        }
                    }

                    // Step the DDA state (same increment shape as the ordinary march
                    // loop below) to reach the next cell along the ray.
                    if (walkAxis == 0) { walkCell.x += stepDir.x; walkTMax_x += tDelta.x; }
                    else if (walkAxis == 1) { walkCell.y += stepDir.y; walkTMax_y += tDelta.y; }
                    else { walkCell.z += stepDir.z; walkTMax_z += tDelta.z; }
                    if (cellTExit > gridT.y) break;  // left the instance span
                }
                if (earlyOut) recordRegime3EarlyOut();
                if (anySampled) {
                    hitColor          = accumColor;
                    hitNormal         = -(rayDirLocal / entryDirLen);
                    hitT              = tEnter;
                    hitBrickIndex     = uint(g_brickArrayBase);
                    hitVoxelLinearIdx = 0u;
                    debugInfo.hitFlag  = 1u;
                    debugInfo.exitCode = DEBUG_EXIT_HIT;
                    recordTraceStep(TRACE_STEP_FAR_FIELD_CUTOFF, 0u, 0,
                                     0u, vec3(cell), tEnter, tEnter, uvec2(0u, 0u));
                    g_lastHitWasFarField = true;
#ifdef VIXEN_REGIME3_COMPOSITE
                    // Compositing slice part 2: carry the walk's residual T out via the
                    // per-invocation global (g_lastRegime3ResidualT's header comment) --
                    // slice-1 behavior (VIXEN_REGIME3_COMPOSITE unset) never touches this,
                    // so accumColor/T stay a pure opaque-hit commit exactly as before.
                    g_lastRegime3ResidualT = T;
#endif
                    return true;
                }
                // Nothing sampled along the whole walk (every cell empty/no coverage)
                // -- fall through to the ordinary march exactly like the regime-2
                // empty-entry case below (the mid-march safety-net gate re-evaluates
                // once a real cell is found). Do NOT return false: that would report a
                // definitive miss for a ray the ordinary march might still resolve.
                recordPolicyEntryEmptyEntry();
                // NOTE deliberately NO recordPolicyEntryDispatch(false) here: control
                // falls through to the shared outer call below, which already counts
                // this ray. The inner call double-counted every no-sample walk
                // (march read 5400 for 2700 rays -- luna3's reconciliation, batch 45).
            } else
#endif
            if (entryPolicyAdmits) {
                // Regime 2+ at entry: no real detail in view for this whole
                // ray -- resolve via the mip ladder directly, no march at all.
                uint entryLookupBase = octreeConfig.brickLookupBase;
                uint entryFlatIdx = uint(cell.z * bpa * bpa + cell.y * bpa + cell.x);
                uint entryLocalBrickIdx = brickLookup[entryLookupBase + entryFlatIdx];
                if (entryLocalBrickIdx != 0xFFFFFFFFu) {
                    recordPolicyEntryDispatch(true);  // batch-35: [PolicyEntryDispatch] mip
                    int entryOctantMask = octantMaskFromDir(rayDirLocal);
                    int entryDepth = farFieldDescentDepth(octreeConfig.bricksPerAxis);
                    uint entryNodeOrdinal;
                    uint entrySampledLevel;
                    bool entryReachedBrick = descendToNodeOrdinal(cell, entryOctantMask, entryDepth,
                                                                   entryWorldDist, entryCellWorldSize,
                                                                   entryNodeOrdinal, entrySampledLevel);
                    incrFarFieldCount();
                    if (!entryReachedBrick) incrFarFieldDescentFail();
                    g_mipSampleLevel = entrySampledLevel;
                    bool entryMipResolved = entryReachedBrick && shadeFromMipSample(entryNodeOrdinal, hitColor, hitNormal);
                    recordFarFieldMipResolve(entryMipResolved);
                    if (entryMipResolved) {
                        recordFarFieldSampledLevel(entrySampledLevel);
                        recordFarFieldSampleIntensity(dot(hitColor, vec3(0.2126, 0.7152, 0.0722)));
                    }
                    if (!entryMipResolved) {
                        hitColor  = vec3(0.5);
                        hitNormal = -(rayDirLocal / entryDirLen);
                    }
                    hitT              = tEnter;
                    hitBrickIndex     = uint(g_brickArrayBase) + entryLocalBrickIdx;
                    hitVoxelLinearIdx = 0u;
                    debugInfo.hitFlag  = 1u;
                    debugInfo.exitCode = DEBUG_EXIT_HIT;
                    recordTraceStep(TRACE_STEP_FAR_FIELD_CUTOFF, entryNodeOrdinal, 0,
                                     0u, vec3(cell), tEnter, tEnter, uvec2(0u, 0u));
                    g_lastHitWasFarField = true;
                    return true;
                }
                // Entry cell itself is empty (no brick there yet) -- fall
                // through to the ordinary march, which already knows how to
                // skip empty cells; the mid-march safety-net gate below will
                // re-evaluate once a real cell is found.
                recordPolicyEntryEmptyEntry();  // batch-39: admitted-but-empty-entry-cell subset of march
            }
        }
        recordPolicyEntryDispatch(false);  // detail regime (or empty entry cell): falls through to the exact march
    }
#endif

    uint lookupBase = octreeConfig.brickLookupBase;
    float tCellEnter = tEnter;
    int enterAxis = -1;
    int maxIters = 3 * bpa;
    for (int iter = 0; iter < maxIters; ++iter) {
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(bpa)))) break;

        uint flatIdx = uint(cell.z * bpa * bpa + cell.y * bpa + cell.x);
        uint localBrickIdx = brickLookup[lookupBase + flatIdx];
        if (localBrickIdx != 0xFFFFFFFFu) {
// ROUND-5 FIX: widened from bare VIXEN_COMPOSED_TRAVERSAL -- the far-field
// gate's math is backend-agnostic (world-frame distance vs world-frame cell
// size, no composed-identity dependency), but it only compiled under the
// composed identity define, which a STANDALONE VIXEN_BRICKMAP_TRAVERSAL boot
// (no VIXEN_COMPOSED_TRAVERSAL) never pushes (BuildRenderGraph.cpp:1623-1631)
// -- so this whole tier was dead code on every non-composed DDA boot (round-5
// symptom 2, [FarFieldCount]=0 on a VIXEN_BRICKMAP_TRAVERSAL=1-only boot even
// though the block IS compiled in for composed). Also compiling under the
// single-backend flag directly makes the tier reachable whenever THIS
// backend is actually selected, composed or not.
#if defined(VIXEN_COMPOSED_TRAVERSAL) || defined(VIXEN_BRICKMAP_TRAVERSAL)
            // W-COMPOSED far-field tier: mirror the ESVO screen-space LOD cutoff
            // (SceneBindings.glsl's non-leaf LOD_ENABLED check, ~line 1195: "tv_max *
            // raySizeCoef + raySizeBias >= scale_exp2"), expressed in WORLD units --
            // the same fix applied at the RT twin (RayQueryTraversal.glsl ~217).
            //
            // ROUND-15 FIX, comment corrected ROUND-16 (supersedes round-5's
            // "tBias + tCellEnter" -- that was a DOUBLE-COUNT, proven both
            // algebraically and by a raw-probe A/B against the RT twin on the SAME
            // scene/geometry, batch-15 investigation; the round-15 agent's own
            // comment here overstated the mechanism as a renderScale-proportional
            // relationship -- corrected below per the round-15 CORRECTIONS ledger
            // entry, validator-verified):
            // tCellEnter starts as tEnter (line ~2182), and tEnter is EXACTLY the
            // slab parameter used to build `pos` (line 2160), which `rayStartWorld`/
            // `tBias` (lines 2176-2179) then RE-DERIVE by reprojecting that SAME
            // point through octreeConfig.localToWorld and measuring its distance
            // from the camera. tBias and tCellEnter are BOTH already inst-frame
            // (localToWorld carries no renderScale -- pure kWorldGridSize scale,
            // ShellOctreeGpu.h:588-590 -- and instDir is unit), so the two are
            // EXACTLY EQUAL, not merely proportional by instRenderScale: tBias ==
            // tCellEnter to the ULP (round-15 numeric check: tBias=613.5,
            // tCellEnter=818 was measured BEFORE the raySizeCoef/bias multiply was
            // stripped from the probe; the two quantities' ratio 818/613.5 is
            // instRenderScale's RECIPROCAL from an artifact of that probe's units,
            // not a genuine scale relationship -- see the round-15 CORRECTIONS
            // entry: round-5's (tBias+tCellEnter)*instRenderScale double-counted by
            // EXACTLY 2.0x, which only follows if tBias and tCellEnter are equal
            // BEFORE either is scaled). The prior fix's "hitT = tBias + tHitLocal"
            // proof (still true) is for a DIFFERENT quantity: tHitLocal there is an
            // IN-CELL MARCH REMAINDER measured from the entry point tBias already
            // reached -- genuinely additive. tCellEnter here is not a remainder, it
            // IS the entry point tBias already represents, so summing them double-
            // counts the camera-to-entry leg. The correct world distance is
            // tCellEnter alone, scaled to world (this also generalizes correctly
            // past the first cell: the DDA loop advances tCellEnter through the
            // SAME rayDirLocal-parameterized frame via tMax[axis], so the identity
            // extends to every stepped cell, not just the entry one). tBias is
            // still used unmodified below by the ordinary (non-far-field) hit path,
            // where it correctly represents the bias added to a genuine in-cell
            // march distance -- only this far-field branch's use of it was wrong.
            float farDirLen = length(rayDirLocal);
            if (farDirLen < 1e-12) { debugInfo.exitCode = DEBUG_EXIT_NONE; return false; }
            float worldDistToCell = tCellEnter * instRenderScale;
            float cellWorldSize = ((1.0 / bpaF) / farDirLen) * instRenderScale;
            incrFarFieldCandidates();  // round-5: counts candidates reaching the gate test, before it runs
            incrFarGenRectCellEntry();  // batch-24 FARGEN: rect-scoped cell-entry funnel
            recordFarFieldGateOperands(worldDistToCell * pc.raySizeCoef + pc.raySizeBias, cellWorldSize);  // round-6 probe
            recordFarGenRectLhsHistogram(worldDistToCell * pc.raySizeCoef + pc.raySizeBias);  // batch-25 JOB 2
            // BATCH-35: SAFETY NET, not the primary dispatch anymore. The
            // entry-point decision above (this function's head, right after
            // tBias) now handles the common case -- a ray whose footprint
            // already covers a voxel-or-coarser rung AT ENTRY never reaches
            // this loop iteration at all (recordPolicyEntryDispatch(true)
            // returned before the march started). This gate still fires for
            // rays that started in the detail regime (genuine march, entry
            // footprint < voxel) but travel far enough that a LATER cell's
            // footprint crosses the same threshold -- e.g. grazing a near
            // body then continuing outward past it. Left unchanged below.
            // This if() is the regime-1(SURFACE)/regime-2(MIP HIT) TRANSITION test
            // (deep-field mip-accessor policy design doc §regimes: regime-1 is
            // "footprint < voxel size", exact fine march -- NOT "footprint <
            // cell/brick size"). BATCH-33 FIX: the old test admitted a candidate
            // only when the footprint already covered a WHOLE BRICK CELL
            // (worldDistToCell*coef+bias >= cellWorldSize) -- that's ONE rung of
            // the ladder (the brick rung), not "does the policy have ANY level to
            // serve here". Every candidate whose footprint fell between voxel size
            // and cell size was rejected and fell through to the exact fine march,
            // even though the ladder has finer-than-brick rungs the policy could
            // still serve (this was the measured admission ceiling: dda dropped
            // 99.5% of candidates here). Under VIXEN_MIP_POLICY, admit whenever the
            // footprint covers ANY level of the full ladder (mipPolicyLevel walked
            // with the ladder's true maxLevel, not the degenerate single-level
            // case) -- i.e. footprint >= voxelWorldSize (level 0, the finest
            // servable rung), letting descendToNodeOrdinal (which already walks
            // the whole ladder, batch-29/30) resolve the actual level. The
            // regime-1 boundary itself does NOT move: a footprint smaller than a
            // leaf/voxel (voxelWorldSize = cellWorldSize/brickSize, brickSize=8
            // per BRICK_SIZE_SDF below) still takes the exact fine march --
            // policyAdmitFootprint below is exactly cellWorldSize/8, i.e. the
            // voxel size, not the cell size, so this widens admission strictly
            // WITHIN regime-2/3 territory and cannot swallow a regime-1 pixel.
            // D=612 near-body parity is unaffected: those are regime-1/2 boundary
            // pixels (footprint ~= voxel size), and the boundary test's threshold
            // (voxel size) is unchanged from what descendToNodeOrdinal/the
            // ordinary ESVO gate already used at that boundary.
            // Flag-off keeps the original single-rung (cell-size) test byte-
            // identical in the #else.
#ifdef VIXEN_MIP_POLICY
            const float kFarFieldBrickSize = 8.0;  // BRICK_SIZE_SDF, see brickSize below
            float policyAdmitFootprint = cellWorldSize / kFarFieldBrickSize;  // finest ladder rung == one voxel
            bool policyAdmits = pc.raySizeCoef > 0.0 &&
                worldDistToCell * pc.raySizeCoef + pc.raySizeBias >= policyAdmitFootprint;
            if (policyAdmits) {
#else
            if (pc.raySizeCoef > 0.0 && worldDistToCell * pc.raySizeCoef + pc.raySizeBias >= cellWorldSize) {
#endif
                incrFarGenRectGateCross();  // batch-24 FARGEN: rect-scoped gate-cross funnel
                vec3 farEntryPos = rayOriginLocal + rayDirLocal * tCellEnter;
                vec3 farDirN = rayDirLocal / farDirLen;

                // Coordinate-bit descent to the REAL ESVO mip sample (replaces the
                // degenerate sampleHitShadingChannels entry-point resolution -- see
                // descendToNodeOrdinal's header, ESVOTraversal.glsl, for the full
                // derivation incl. the mirror-frame conversion AND the round-3
                // level-selection/farBit/depth fixes. `cell` is already canonical/
                // unmirrored (see the comment at the ordinary-hit gridEntry
                // construction just below); depth is the brick level's distance
                // from root (farFieldDescentDepth, SVOTypes.glsl -- NOT the old
                // findMSB(bpa), see its header for why). worldDistToCell/
                // cellWorldSize are the SAME quantities the footprint test just
                // above already computed -- descendToNodeOrdinal reuses them to
                // evaluate ESVO's own per-level criterion while walking, so it can
                // stop coarser than brick level exactly like the ordinary gate does.
                int farOctantMask = octantMaskFromDir(rayDirLocal);
                int farDepth = farFieldDescentDepth(octreeConfig.bricksPerAxis);
                uint farNodeOrdinal;
                uint farSampledLevel;
                bool farReachedBrick = descendToNodeOrdinal(cell, farOctantMask, farDepth,
                                                            worldDistToCell, cellWorldSize, farNodeOrdinal, farSampledLevel);
                incrFarFieldCount();
                if (!farReachedBrick) incrFarFieldDescentFail();  // round-13 probe
                g_mipSampleLevel = farSampledLevel;
                bool farMipResolved = farReachedBrick && shadeFromMipSample(farNodeOrdinal, hitColor, hitNormal);
                recordFarFieldMipResolve(farMipResolved);  // round-7 blocker-1 probe
                if (farMipResolved) {
                    recordFarFieldSampledLevel(farSampledLevel);  // batch-32 JOB 1
                    recordFarFieldSampleIntensity(dot(hitColor, vec3(0.2126, 0.7152, 0.0722)));  // batch-33 JOB 2
                }
                if (!farMipResolved) {
                    hitColor  = vec3(0.5);
                    hitNormal = -farDirN;
                }
                hitT              = tBias + tCellEnter;
                hitBrickIndex     = uint(g_brickArrayBase) + localBrickIdx;
                hitVoxelLinearIdx = 0u;
                debugInfo.hitFlag  = 1u;
                debugInfo.exitCode = DEBUG_EXIT_HIT;
                recordTraceStep(TRACE_STEP_FAR_FIELD_CUTOFF, farNodeOrdinal, 0,
                                 0u, vec3(cell), tCellEnter, hitT, uvec2(0u, 0u));
                g_lastHitWasFarField = true;  // round-7 blocker-1 probe #3
                return true;
            }
#endif
            // gridEntry/gridDirN in TRUE geometric grid-voxel space ([0,bpa*8]) --
            // the SAME frame marchBrickSdfCell/sampleHitShadingChannels expect
            // (mirrors handleLeafHitInstancedSdf's ESVO-leaf bridge, but the DDA
            // never mirrors so brick/gridEntry are already the canonical
            // (unmirrored) coordinates -- no octant-unmirror step needed).
            vec3 cellEntryPos = rayOriginLocal + rayDirLocal * tCellEnter;
            vec3 fracInCell = clamp(cellEntryPos * bpaF - vec3(cell), vec3(0.0), vec3(0.999999));
            if (enterAxis >= 0) {
                fracInCell[enterAxis] = stepDir[enterAxis] > 0 ? 0.0 : 0.999999;
            }
            // Pinned to marchBrickSdfCell's hardcoded 8.0 brick extent (it slabs
            // bMin = vec3(brick)*8.0 .. +8.0) — the same constant
            // handleLeafHitInstancedSdf pins as BRICK_SIZE_SDF, NOT the runtime
            // octreeConfig.brickSize (round-3 validator, latent-coupling finding).
            const int brickSize = 8;  // BRICK_SIZE_SDF
            vec3 posInBrick = clamp(fracInCell * float(brickSize), vec3(0.0), vec3(float(brickSize) - 0.001));
            vec3 gridEntry = brickLocalToGrid(posInBrick, cell, brickSize);

            float dirLen = length(rayDirLocal);
            if (dirLen < 1e-12) { debugInfo.exitCode = DEBUG_EXIT_NONE; return false; }
            vec3 gridDirN = rayDirLocal / dirLen;
            // Bias the entry an epsilon INTO the brick along the ray so it never sits
            // exactly on a slab plane: an exact-zero (bMin - gridEntry) numerator at
            // grazing incidence lets compiler-variant FMA contraction decide the slab
            // test's outcome — the source of the boot-bimodal frame hash (round-3
            // Gate B blocker). The bias's share of the march distance is added back
            // into tHit below so reported t stays exact.
            const float kEntryBias = 1e-3;  // grid-voxel units (voxel = 1.0, brick = 8.0)
            gridEntry += gridDirN * kEntryBias;

            vec3  nrm;
            float sHit;
            if (marchBrickSdfCell(g_octreeIdx, cell, gridEntry, gridDirN, nrm, sHit)) {
                float gridScale = float(bpa * brickSize);
                float tHitLocal = tCellEnter + (sHit + kEntryBias) / (dirLen * gridScale);
                hitNormal         = nrm;
                hitT              = tBias + tHitLocal;
                hitBrickIndex     = uint(g_brickArrayBase) + localBrickIdx;
                hitVoxelLinearIdx = 0u;

                vec3 gridHit = gridEntry + gridDirN * sHit;
                sampleHitShadingChannels(gridHit, vec3(1.0), 0.5, hitColor, hitRoughness);

#ifdef VIXEN_BRICKMAP_DEBUG
                // W-BRICKMAP Gate-B bisection: overwrite hitColor with quantized
                // diagnostics for the two divergent silhouette pixels only. hitColor
                // still passes through `* inst.color` (TraceWorld.glsl) and the full
                // lighting/tonemap/PNG-8bit chain before the HUD capture reads it --
                // there is no direct-to-outputImage debug path in this pipeline (see
                // this block's own header note) -- so values are quantized into a
                // handful of WIDELY separated buckets (multiples of 1/8) rather than
                // packed bit-for-bit; exact decode contract lives in
                // decode_brickmap_debug.py alongside the analysis script.
                {
                    ivec2 dbgPixel = ivec2(gl_GlobalInvocationID.xy);
                    bool inMainRect = dbgPixel.x >= 318 && dbgPixel.x <= 330 &&
                                       dbgPixel.y >= 288 && dbgPixel.y <= 296;
                    // Second row, directly below the main rect: iter count + brick idx.
                    bool inAuxRect  = dbgPixel.x >= 318 && dbgPixel.x <= 330 &&
                                       dbgPixel.y >= 297 && dbgPixel.y <= 305;
                    if (inMainRect) {
                        float cellCode = float(cell.x * 64 + cell.y * 8 + cell.z); // 0..511
                        hitColor = vec3(
                            clamp(cellCode / 511.0, 0.0, 1.0),   // R: final hit cell (DDA-int -> [0,1])
                            clamp(sHit / 8.0, 0.0, 1.0),         // G: sHit in [0, brickSize] grid units
                            clamp(tCellEnter, 0.0, 1.0));        // B: tCellEnter (ray-space [0,1])
                    } else if (inAuxRect) {
                        hitColor = vec3(
                            clamp(float(iter) / float(maxIters), 0.0, 1.0), // R: DDA iter count
                            clamp(float(localBrickIdx) / 64.0, 0.0, 1.0),   // G: localBrickIdx
                            0.0);
                    }
                }
#endif

                debugInfo.hitFlag = 1u;
                return true;
            }
            // No crossing in this brick -- continue the DDA into the next cell,
            // matching the binary twin's own "miss falls through to POP/ADVANCE" semantics.
        }

        int axis = (tMax.x < tMax.y) ? ((tMax.x < tMax.z) ? 0 : 2) : ((tMax.y < tMax.z) ? 1 : 2);
        tCellEnter = tMax[axis];
        cell[axis] += stepDir[axis];
        tMax[axis] += tDelta[axis];
        enterAxis = axis;
    }

    debugInfo.exitCode = DEBUG_EXIT_NONE;
    return false;
}

// Any-hit twin of traverseCoarseGridInstancedSdf: same DDA, no shading payload,
// early-out the instant an occupied cell's marchBrickSdfCellAnyHit confirms a
// crossing.
bool traverseCoarseGridInstancedSdfAnyHit(vec3 rayOrigin, vec3 rayDir,
                                           vec3 rayOriginLocal, vec3 rayDirLocal, vec2 gridT,
                                           float tmin, float tmax) {
    if (gridT.y < 0.0) return false;

    int bpa = octreeConfig.bricksPerAxis;
    if (bpa <= 0) return false;
    float bpaF = float(bpa);

    bool rayStartsInside = (gridT.x < 0.0);
    float tEnter = rayStartsInside ? 0.0 : (gridT.x + EPSILON);
    vec3 pos = rayOriginLocal + rayDirLocal * tEnter;
    ivec3 cell = clamp(ivec3(floor(pos * bpaF)), ivec3(0), ivec3(bpa - 1));

    ivec3 stepDir; vec3 tDelta; vec3 tMax;
    for (int axis = 0; axis < 3; ++axis) {
        float d = rayDirLocal[axis];
        if (abs(d) < DIR_EPSILON) {
            stepDir[axis] = 0; tDelta[axis] = 1e30; tMax[axis] = 1e30;
        } else {
            stepDir[axis] = d > 0.0 ? 1 : -1;
            tDelta[axis] = (1.0 / bpaF) / abs(d);
            float nextBoundary = (float(cell[axis]) + (d > 0.0 ? 1.0 : 0.0)) / bpaF;
            tMax[axis] = (nextBoundary - rayOriginLocal[axis]) / d;
        }
    }

    vec3 rayStartWorld;
    if (rayStartsInside) { rayStartWorld = rayOrigin; }
    else { rayStartWorld = (octreeConfig.localToWorld * vec4(pos, 1.0)).xyz; }
    float tBias = length(rayStartWorld - rayOrigin);

    uint lookupBase = octreeConfig.brickLookupBase;
    float tCellEnter = tEnter;
    int enterAxis = -1;
    int maxIters = 3 * bpa;
    for (int iter = 0; iter < maxIters; ++iter) {
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(bpa)))) break;
        if (tBias + tCellEnter > tmax) break;

        uint flatIdx = uint(cell.z * bpa * bpa + cell.y * bpa + cell.x);
        uint localBrickIdx = brickLookup[lookupBase + flatIdx];
        if (localBrickIdx != 0xFFFFFFFFu) {
            vec3 cellEntryPos = rayOriginLocal + rayDirLocal * tCellEnter;
            vec3 fracInCell = clamp(cellEntryPos * bpaF - vec3(cell), vec3(0.0), vec3(0.999999));
            if (enterAxis >= 0) {
                fracInCell[enterAxis] = stepDir[enterAxis] > 0 ? 0.0 : 0.999999;
            }
            const int brickSize = 8;  // BRICK_SIZE_SDF — see closest-hit twin's pin comment
            vec3 posInBrick = clamp(fracInCell * float(brickSize), vec3(0.0), vec3(float(brickSize) - 0.001));
            vec3 gridEntry = brickLocalToGrid(posInBrick, cell, brickSize);

            float dirLen = length(rayDirLocal);
            if (dirLen < 1e-12) return false;
            vec3 gridDirN = rayDirLocal / dirLen;
            float gridScale = float(bpa * brickSize);
            // Entry-bias: see closest-hit twin. The march budget below shrinks by the
            // same bias so the [tmin,tmax] window stays exact.
            const float kEntryBias = 1e-3;
            gridEntry += gridDirN * kEntryBias;

            float sMaxLimit = (tmax > 0.0)
                ? max((tmax - tBias - tCellEnter) * dirLen * gridScale - kEntryBias, 0.0)
                : 1e6;
            if (tmax > 0.0 && sMaxLimit <= 0.0) { /* no room in this cell -- try next */ }
            else {
                float sHit;
                if (marchBrickSdfCellAnyHit(g_octreeIdx, cell, gridEntry, gridDirN, sMaxLimit, sHit)) {
                    float tHitLocal = tCellEnter + (sHit + kEntryBias) / (dirLen * gridScale);
                    float hitT = tBias + tHitLocal;
                    if (hitT >= tmin && hitT <= tmax) return true;
                }
            }
        }

        int axis = (tMax.x < tMax.y) ? ((tMax.x < tMax.z) ? 0 : 2) : ((tMax.y < tMax.z) ? 1 : 2);
        tCellEnter = tMax[axis];
        cell[axis] += stepDir[axis];
        tMax[axis] += tDelta[axis];
        enterAxis = axis;
    }

    return false;
}

#include "TraceWorld.glsl"   // Sampled Lighting Inc1 M1: single traversal seam (pure extraction)
