// ============================================================================
// TraceWorld.glsl - The single traversal seam every lighting ray uses
// ============================================================================
// Sampled Lighting Inc1 M1: pure extraction of BodyInstanceRayMarch.comp's
// per-pixel instance loop (procedural SDF sphere-trace + ESVO tier-crossing
// traversal, nearest-hit accumulation across all body instances) into a
// single reusable function, TraceWorld(). VERBATIM move — same math, same
// LOD / tier-crossing / mip-fallback / stored-SDF paths, same instance-loop
// ordering and early-outs. main() now calls TraceWorld() then shades exactly
// as before with the existing computeLighting.
//
// Must be #included AFTER BodyInstanceRayMarch.comp's own includes (SVOTypes,
// Materials, VoxelChannelFormat, Generated/LightingConfig, Generated/OctreeConfig,
// ShaderCounters, CoordinateTransforms, RayGeneration, ESVOCoefficients,
// TraceRecording, ESVOTraversal, Lighting, SdfRecipes, StoredSdf, MipFallback)
// and after the BodyInstance struct / bodyInstances SSBO / instanceIterCount
// SSBO / octreeConfig macro / g_octreeIdx,g_brickArrayBase globals /
// traverseOctreeInstanced / traceProceduralBody / PROVIDER_* defines are all
// declared — exactly the point BodyInstanceRayMarch.comp already includes it
// from (immediately before main()), so no reordering of any shared include.
// ============================================================================

#ifndef TRACEWORLD_GLSL
#define TRACEWORLD_GLSL

// ============================================================================
// isCloserHit - deterministic seam tie-break (M6b Task 6b.0, 2026-07-17)
// ============================================================================
// Root cause: two Cornell wall bodies are separate baked ESVO instances whose
// slabs ABUT along a shared plane (e.g. ceiling/leftWall/backWall corner,
// floor/rightWall corner). A ray landing exactly on that seam gets near-equal
// hitT from both instances; raw `candidateT < bestT` float comparison then
// flips winner per-pixel on sub-ULP noise (different traversal path length,
// FMA fusion, etc.), reading as a checkerboard. This is the SAME junction as
// the parity golden's documented row-21 floor/rightWall near-tie flip
// (tools/bench/parity_thresholds.json's same_path _comment).
//
// Fix: within a RELATIVE epsilon band of the current best, prefer the LOWER
// instance index deterministically instead of trusting raw float ordering.
// Lower-instIdx-wins is an arbitrary but STABLE rule -- every pixel along a
// seam resolves the same way regardless of float noise, so the seam becomes
// a coherent boundary instead of a checkerboard. Epsilon is relative
// (scaled by max(|t|,1.0)) so it stays tight at both near and far distances
// without swallowing genuinely different depths -- 1e-4 relative is far
// tighter than any real depth gap between non-abutting geometry in this
// scene (Cornell box spans ~O(10) world units) but wide enough to cover the
// float noise a seam actually produces (observed noise is sub-1e-5 relative).
#define SEAM_TIE_EPS_REL 1e-4

bool isCloserHit(float candidateT, uint candidateInstIdx, float bestT, uint bestInstIdx) {
    float tieBand = SEAM_TIE_EPS_REL * max(abs(bestT), 1.0);
    if (abs(candidateT - bestT) <= tieBand) {
        // Near-tie: stable tiebreaker, not raw float ordering. Lower instIdx
        // wins. bestInstIdx == 0xFFFFFFFFu means "no winner yet" -- any
        // candidate takes it.
        return candidateInstIdx < bestInstIdx;
    }
    return candidateT < bestT;
}

// ============================================================================
// WorldHit - what the march produces at a hit
// ============================================================================
// Carries everything the shade path (computeLighting) reads: tinted color,
// normal, hit distance, roughness, plus the brick/voxel identity used by the
// pick/ID output image. Mirrors main()'s pre-extraction "best*" accumulator
// locals exactly.
struct WorldHit {
    vec3  color;
    vec3  normal;
    float t;
    float roughness;
    uint  brickIndex;
    uint  voxelIdx;
    // M3 round 3 (2026-07-14): which body instance produced the winning nearest-hit --
    // a Cornell-demo diagnostic aid (localizing a stray out-of-bounds hit found by a CPU
    // HitRecord readback) mirroring M1 round 4's own now-removed g_cornellDiagWinnerInstIdx
    // technique, but carried as a real WorldHit field this time instead of a separate
    // shader global, and left permanently available (cheap, always-written) rather than
    // scaffolded in/out per round. 0xFFFFFFFFu when anyHit is false (no winner).
    uint  instIdx;
    // M11.2 (2026-07-18): winning instance's emission intensity, carried straight off
    // BodyInstance.recipeParams[3] (see TraceWorld's instance loop below for why that slot)
    // -- 0.0 for every non-emissive body. Lets the primary-hit shade pass add a
    // self-lit term for the Cornell ceiling light without a new SEM_* channel or any
    // change to the light-tree/GI path, which already samples emission independently.
    float emission;
    // Round 9: does the pixel's FINAL winning hit come off the far-field mip-resolve
    // path? NOT the same as "some far-field candidate won its own per-instance
    // isCloserHit compare" (the round-8-located [FarFieldWon] conflation) -- this
    // rides bestInstIdx's actual selection, so a later non-far-field instance
    // overwriting the winner also overwrites/clears this. false when anyHit is false.
    bool wasFarField;
    // Regime-3 compositing (VIXEN_REGIME3_COMPOSITE), part 2: residual transmittance
    // left over from a regime-3 accumulation-walk winner (g_lastRegime3ResidualT's
    // header comment, SceneBindings.glsl) -- 1.0 (no coverage contributed, blend is a
    // no-op) whenever the winner did NOT come from that walk, INCLUDING every flag-off
    // boot (the global is never written away from 1.0 there). behindColor is the
    // SAME-PASS second-nearest candidate's color (see the instance loop's secondBest
    // bookkeeping below) -- vec3(0.0) when no second candidate existed this frame.
    float residualT;
    vec3  behindColor;
    // E1-T1 probe-only terminal classification; not serialized into HitRecord.
    uint footprintRegime;
    uint sourceMask;
};

// ============================================================================
// getOctreeTraceBounds - conservative allocated-brick AABB, in local [0,1]^3 space
// ============================================================================
// Baked-Perf M5 Task 5.1/5.2: configs[octreeIdx].traceBoundsMin/Max (stamped by
// SerializeSdf, see ShellOctreeGpu.h) is the tight AABB over every brick this
// octree actually allocated, in the SAME normalized [0,1]^3 local grid space
// localRayOrigin/localRayDir already occupy after worldToLocal below — no extra
// transform needed at the call site. Old cached configs and the plain binary
// Serialize() path (no per-brick lookup loop to derive a tighter bound from)
// leave both fields at their memset(0) default (min==max==(0,0,0)); treat that
// as "no tighter bound than the full root" so the schema extension is
// backward-safe. Returns true when the bound is genuinely tighter than [0,1]^3
// (i.e. actually worth using in place of the full-cube cull).
bool getOctreeTraceBounds(uint octreeIdx, out vec3 boundsMin, out vec3 boundsMax) {
    boundsMin = configs[octreeIdx].traceBoundsMin;
    boundsMax = configs[octreeIdx].traceBoundsMax;
    bool valid = all(greaterThan(boundsMax, boundsMin)) &&
                 all(greaterThanEqual(boundsMin, vec3(0.0))) &&
                 all(lessThanEqual(boundsMax, vec3(1.0)));
    if (!valid) {
        boundsMin = vec3(0.0);
        boundsMax = vec3(1.0);
        return false;
    }
    return any(greaterThan(boundsMin, vec3(0.0))) ||
           any(lessThan(boundsMax, vec3(1.0)));
}

// ============================================================================
// TraceWorld - nearest-hit across all body instances (procedural + ESVO)
// ============================================================================
// origin/dir: world-space ray. tmin/tmax: currently unused by the moved body
// (the pre-extraction code had no incoming t-span parameter — every instance
// re-derives its own AABB entry via rayAABBIntersection internally, and the
// nearest-hit accumulator itself starts at bestT = 1e30) but are accepted for
// the seam's general signature per the Inc1 plan; VERBATIM behavior means
// they are not threaded into the moved logic in M1.
// Returns true if any instance was hit (nearer than tmax is NOT enforced here,
// matching pre-extraction behavior exactly — tmax is unused).
bool TraceWorld(vec3 origin, vec3 dir, float tmin, float tmax, out WorldHit hit) {
    vec3 rayOrigin = origin;
    vec3 rayDir    = dir;

    bool  anyHit          = false;
    float bestT           = 1e30;
    vec3  bestColor       = vec3(0.0);
    vec3  bestNormal      = vec3(0.0, 1.0, 0.0);
    float bestRoughness   = 0.5;   // Inc3 M3: default for binary/procedural paths
    uint  bestBrickIndex  = 0u;
    uint  bestVoxelIdx    = 0u;
#ifdef VIXEN_REGIME3_COMPOSITE
    // Compositing slice part 2: same-pass second-nearest candidate, tracked
    // ALONGSIDE best* (never a re-trace) -- every candidate that loses
    // isCloserHit against the CURRENT best but beats the current second
    // becomes the new second. Only meaningful when the eventual winner is a
    // regime-3 partial-coverage hit (WorldHit.residualT < 1.0); unused
    // otherwise. #ifdef'd out entirely on a flag-off build -- zero cost there.
    float secondT     = 1e30;
    vec3  secondColor = vec3(0.0);
    float bestResidualT = 1.0;  // 1.0 == no-op; overwritten only if the winner is a regime-3 hit
#ifdef VIXEN_COMPOSITION_COUNTERS
    // E2-T1: a ray may admit more than one behind candidate, but the census
    // classifies rays, not extra instance traversals. Record at most once.
    bool compositionRelaxedRayRecorded = false;
#endif
#endif
    uint  bestInstIdx     = 0xFFFFFFFFu;  // M3 round 3: winning instance, see WorldHit.instIdx
    float bestEmission    = 0.0;          // M11.2: winning instance's emission intensity
    bool  bestWasFarField = false;        // round 9: see WorldHit.wasFarField
    uint  bestFootprintRegime = 1u;
    uint  sourceMask = 0u;

#ifdef VIXEN_RTQUERY_TRAVERSAL
    // W-RTQUERY Slice A round 3 -- THE HOIST REDESIGN: search every
    // FORMAT_STORED_SDF instance's per-brick-AABB TLAS in ONE rayQuery, with
    // the TRUE WORLD ray, before the per-instance loop below runs (see
    // traverseRayQueryWorld's header in RayQueryTraversal.glsl for the full
    // t-space contract -- the returned hitT is already world-space, no
    // *renderScale correction needed, unlike the ESVO/DDA per-instance calls
    // inside the loop). Seed bestT/bestInstIdx/best* from its result via the
    // SAME isCloserHit tie-break the instance loop itself uses, so a TLAS hit
    // competes fairly with a non-SDF instance's own march. The loop below then
    // SKIPS any instance this search already covered (FORMAT_STORED_SDF) --
    // non-SDF instances (FORMAT_BINARY / procedural) still run their normal
    // per-instance branch untouched.
    {
        vec3  rqColor, rqNormal; float rqT, rqRoughness, rqEmission;
        uint  rqBrick, rqVoxel, rqInstIdx;
        DebugRaySample rqDbg;
        rqDbg.pixel         = uvec2(ivec2(gl_GlobalInvocationID.xy));
        rqDbg.rayDir        = rayDir;
        rqDbg.octantMask    = 0u;
        rqDbg.hitFlag       = 0u;
        rqDbg.exitCode      = DEBUG_EXIT_NONE;
        rqDbg.lastStepMask  = 0u;
        rqDbg.iterationCount = 0u;
        rqDbg.scale         = 0;
        rqDbg.stateIdx      = 0u;
        rqDbg.tMin          = 0.0;
        rqDbg.tMax          = 0.0;
        rqDbg.scaleExp2     = 0.0;
        rqDbg.posMirrored   = vec3(0.0);
        rqDbg.localNorm     = vec3(0.0);
        g_lastHitWasFarField = false;  // round-7 blocker-1 probe #3: reset before the call
        g_lastFootprintRegime = 1u;
        bool rqHit = traverseRayQueryWorld(rayOrigin, normalize(rayDir),
                                            rqColor, rqNormal, rqT, rqRoughness, rqEmission,
                                            rqBrick, rqVoxel, rqInstIdx, rqDbg);
        bool rqWasFarField = g_lastHitWasFarField;
        uint rqFootprintRegime = g_lastFootprintRegime;
        if (rqHit && isCloserHit(rqT, rqInstIdx, bestT, bestInstIdx)) {
            if (rqWasFarField) { incrFarFieldWon(); }  // round-7 blocker-1 probe #3
#ifdef VIXEN_REGIME3_COMPOSITE
            // The instance we're about to displace becomes the new second-nearest
            // candidate (same-pass bookkeeping, not a re-trace -- see secondT's
            // header comment). Only takes effect the first time a winner exists.
            if (anyHit) { secondT = bestT; secondColor = bestColor; }
#endif
            BodyInstance rqInst = bodyInstances[rqInstIdx];
            bestT          = rqT;
            bestColor      = rqColor * rqInst.color;
            bestNormal     = rqNormal;
            bestRoughness  = rqRoughness;
            bestBrickIndex = rqBrick;
            bestVoxelIdx   = rqVoxel;
            bestInstIdx    = rqInstIdx;
            // E15-T1: this RTQuery search only ever serves FORMAT_STORED_SDF instances
            // (see this block's header comment) -- recipeParams[3] is never populated for
            // those (M11.2's convention is procedural/legacy-only), so the winning voxel's
            // own SEM_EMISSION reading (near march via sampleHitShadingChannels, or the
            // mip representative via shadeFromMipSample on the far-field arm) is the
            // correct emission source here, mirroring the ESVO stored-branch fix below.
            bestEmission   = rqEmission;
            bestWasFarField = rqWasFarField;  // round 9: rides the winner, not a sticky global
            bestFootprintRegime = rqFootprintRegime;
            sourceMask |= 2u;
            anyHit         = true;
        }
    }
#endif

    // -----------------------------------------------------------------------
    // INSTANCE LOOP
    // -----------------------------------------------------------------------
    int numInstances = clamp(pc.instanceCount, 0, 3 * 64); // safety cap
    for (int instIdx = 0; instIdx < numInstances; ++instIdx) {

        // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: skip a caller-specified instance
        // subset (see InstanceSkipMaskBuffer's comment in SceneBindings.glsl). A no-op
        // (isInstanceSkipped always false) whenever the skip mask is empty/unbound —
        // the default, every-existing-scene case.
        if (isInstanceSkipped(instIdx)) {
#ifdef VIXEN_GPU_TRACE_HOOKS
            instanceIterCount[instIdx] = 0u;
#endif
            continue;
        }

        BodyInstance inst = bodyInstances[instIdx];

        // --- Procedural provider: analytic SDF sphere-trace (no octree) ---
        // Lazy-Procedural-Delta-Baseline Inc0 M5/M6 Task 11/12/13: recipeId < 2 stays on
        // the hand-written legacy analytic path (byte-identical formula); recipeId >= 2 is
        // registry-driven (uber-shader splice) with a bound-sphere front-to-back early-reject
        // (Task 12) and a far-subpixel flat-shade early-out (Task 13) before paying for the
        // full traceUberRecipeBody march. See BodyInstanceRayMarch.comp's pre-extraction
        // main() (git history) for the original inline version this was ported from when
        // TraceWorld.glsl (Sampled Lighting Inc1 M1) was merged with this branch's Inc0 M5/M6.
        if (inst.providerKind == PROVIDER_PROCEDURAL) {
            vec3 pNormal;
            float pT;
            bool pHit = false;

            // recipeId 0/1 stay on the hand-written legacy analytic path (RECIPE_SPHERE /
            // RECIPE_DISPLACED_SPHERE) — byte-identical formula, unchanged by the M5 splice.
            // recipeId >= 2 is registry-driven: only reachable when at least one recipe was
            // registered (the ONLY way a BodyInstance can carry such an id), which is also
            // the only condition under which evalRecipeField/getRecipeBoundSphere exist in
            // this translation unit (see BodyInstanceRayMarch.comp's splice-marker comment) —
            // so this branch never references an undeclared identifier in an unspliced build.
            if (inst.recipeId < 2u) {
                vec3 pCenter = inst.worldPos;
                vec3 pParams = vec3(inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2]);
                pHit = traceProceduralBody(inst.recipeId, pCenter, pParams, rayOrigin, rayDir,
                                           pNormal, pT);
            }
#ifdef VIXEN_UBER_RECIPE_SPLICED
            else {
                vec3  boundCenter; float boundRadius; float relaxation;
                getRecipeBoundSphere(inst.recipeId, boundCenter, boundRadius, relaxation);

                // Front-to-back early-reject (Task 12): mirrors the ESVO branch's own
                // entryTWorld > bestT discipline below — skip the march entirely when this
                // instance's bound-sphere entry point is already farther than a hit some
                // OTHER instance already recorded this pixel. instanceIterCount stays 0u for
                // a rejected instance (same "zero traversal iterations, not just a discarded
                // result" proof the ESVO branch relies on for its own gate test).
                vec3  oc   = rayOrigin - boundCenter;
                float b    = dot(oc, rayDir);
                float c    = dot(oc, oc) - boundRadius * boundRadius;
                float disc = b * b - c;
                if (disc < 0.0) {
#ifdef VIXEN_GPU_TRACE_HOOKS
                    instanceIterCount[instIdx] = 0u;
#endif
                    continue;  // ray misses this instance's bound sphere entirely
                }
                float entryT = max(-b - sqrt(disc), 0.0);
                // M6b Task 6b.0: same relative tie-band as isCloserHit / the ESVO branch's
                // entryTWorld reject above -- do not eliminate a candidate within the seam
                // tie-band before it can be considered by the deterministic tiebreaker.
                float uberEntryTieBand = SEAM_TIE_EPS_REL * max(abs(bestT), 1.0);
                if (entryT > bestT + uberEntryTieBand) {
#ifdef VIXEN_GPU_TRACE_HOOKS
                    instanceIterCount[instIdx] = 0u;
#endif
                    continue;  // nearest possible hit is already farther than bestT
                }

                // Task 13 far early-out: once the WHOLE bound sphere's projected footprint
                // at entryT has sub-resolved to under one screen pixel (same cone-spread
                // formula the ESVO branch's screen-space LOD cutoff uses — raySizeCoef is
                // 2*tan(halfFovPerPixel)), further marching can't resolve any more surface
                // detail than a flat shade already gives. Deliberately coarse: color/normal
                // fidelity at this distance is out of scope for Task 13 (plan's own note),
                // this only saves the march's per-step cost once it can no longer matter.
                // Gated on raySizeCoef>0.0 so LOD-disabled builds/tests (raySizeCoef==0,
                // e.g. the M4/M5 offscreen harnesses) never take this path.
                bool farSubPixel = false;
                if (pc.raySizeCoef > 0.0) {
                    float footprint = entryT * pc.raySizeCoef + pc.raySizeBias;
                    farSubPixel = footprint >= 2.0 * boundRadius;
                }

                if (farSubPixel) {
#ifdef VIXEN_GPU_TRACE_HOOKS
                    instanceIterCount[instIdx] = 1u;  // flat-shaded, not a full march — nonzero proves it wasn't rejected
#endif
                    pHit    = true;
                    pT      = entryT;
                    pNormal = normalize(-rayDir);  // face the camera — cheapest plausible normal for a sub-pixel blob
                } else {
                    uint pSteps;
                    // Recipe-Parameterization M2 Task 6: read inst.recipeParams[] out of the
                    // SSBO once here and pass it down as a plain argument — same convention
                    // the legacy recipeId<2 branch above already uses for pParams, not a
                    // re-index inside the emitted field-function bodies.
                    float uberParams[6] = float[6](
                        inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2],
                        inst.recipeParams[3], inst.recipeParams[4], inst.recipeParams[5]);
                    pHit = traceUberRecipeBody(inst.recipeId, boundCenter, boundRadius, relaxation,
                                               rayOrigin, rayDir, uberParams, pNormal, pT, pSteps);
                    // Task 12 evidence (c): a non-rejected instance always writes its real march
                    // step count (>=1, even on a miss that exhausted MAX_STEPS or exited tFar) —
                    // only the two continue-above paths leave this 0u, so "0 here" means "the
                    // early-reject fired," matching the ESVO branch's own convention exactly.
#ifdef VIXEN_GPU_TRACE_HOOKS
                    instanceIterCount[instIdx] = pSteps;
#endif
                }
            }
#endif

            if (pHit) sourceMask |= 1u;
            if (pHit && isCloserHit(pT, uint(instIdx), bestT, bestInstIdx)) {
#ifdef VIXEN_REGIME3_COMPOSITE
                if (anyHit) { secondT = bestT; secondColor = bestColor; }
#endif
                bestT          = pT;
                bestColor      = inst.color;   // procedural base colour = instance tint
                bestNormal     = pNormal;      // smooth SDF-gradient normal
                bestBrickIndex = 0u;
                bestVoxelIdx   = 0u;
                bestInstIdx    = uint(instIdx);
                // M11.2: recipeParams[3..5] are unused by every registered Cornell recipe
                // (recipeId>=2 bodies sample world p directly, see BuildRenderGraph.cpp's
                // VIXEN_DDGI_CORNELL_VIRTUAL_DEMO seeding comment) and recipeId<2's legacy
                // analytic path only reads recipeParams[0..2] -- recipeParams[3] is genuinely
                // spare for both, so it carries the light body's emission intensity (0.0 for
                // every non-emissive body) without a new BodyInstance field.
                bestEmission   = inst.recipeParams[3];
                bestFootprintRegime = 1u;
                anyHit         = true;
            }
            continue;  // procedural body fully handled; skip the ESVO path
        }

        uint oi = inst.octreeIndex;  // index into runtime-sized configs[] SSBO (I3.2)

        // Inc2 M6: Stored-SDF bodies are NO LONGER special-cased here. They flow
        // through the SAME ESVO path as binary bodies below; the leaf hit-test inside
        // traverseOctreeInstanced dispatches to the trilinear SDF march by formatId.
        // (The old standalone marchStoredSdf flat sphere-trace has been retired.)

        // Set globals used by fetchESVONode (via g_esvoNodeBase in ESVOTraversal.glsl)
        // and by marchBrickInstanced (via g_brickArrayBase below).
        g_octreeIdx      = int(oi);
        g_mipSampleLevel = 0u;
        g_esvoNodeBase   = configs[oi].nodeArrayBase;   // declared in ESVOTraversal.glsl
        g_brickArrayBase = configs[oi].brickArrayBase;

        // ------------------------------------------------------------------
        // World → instance-local ray transform (single consistent frame)
        //
        // The instance places the body at inst.worldPos, scaled by
        // inst.renderScale.  Serialize emits the BASE (un-instanced) octree
        // mapping in configs[oi]: worldToLocal maps the base octree's world
        // frame → its [0,1]^3 grid (it already folds in the gridMin/gridMax
        // centre — we must NOT re-apply a centre offset here).
        //
        // To render an INSTANCE we first de-instance the world ray into the
        // base octree's world frame by undoing the placement, then let
        // traverseOctreeInstanced apply configs[oi].worldToLocal exactly once:
        //
        //   p_base = (p_world - worldPos) / renderScale            (point)
        //   d_base =  normalize(d_world)  = d_world (already unit)  (direction)
        //
        // ORIGIN is de-instanced (÷renderScale) so the traversal runs in the
        // base octree's own shrunk frame; DIRECTION is kept UNIT-LENGTH. With a
        // unit direction and the de-instanced origin, the ESVO traversal's
        // returned hitT is a EUCLIDEAN DISTANCE in that shrunk frame — the SAME
        // kind of quantity as tEntryWorld (initRayCoefficients/rayStartWorld's
        // own length()-based entry distance, SceneBindings.glsl). hitT *=
        // renderScale (below) then converts that shrunk-frame distance to true
        // world distance for the cross-instance nearest-hit test.
        //
        // WHY UNIT, not rayDir/renderScale (M5b root-cause fix, 2026-07-17): the
        // ESVO leaf composes hitT = tBias + state.t_min + sHit/(dirLen*gridScale)
        // (SceneBindings.glsl handleLeafHitInstancedSdf). tBias (=tEntryWorld) is
        // a length()-distance, but state.t_min/sHit are t-PARAMETERS along the
        // direction fed to initRayCoefficients. Those two are only the same unit
        // when that direction is UNIT-length. Passing a non-unit rayDir/renderScale
        // made state.t_min come out renderScale× larger than tBias, so their sum
        // (then *renderScale) placed the hit renderScale× too far along the ray —
        // the backWall z≈−27 far-hit, seen ONLY at renderScale≠1 (every prior
        // scene used renderScale=1, where /renderScale is a no-op and the bug is
        // invisible). Verified byte-exact against the CPU mirror
        // (test_stored_sdf_march_mirror.cpp, which always normalizes): non-unit
        // dir reproduces z=−27; unit dir gives the correct z=6 surface hit.
        // Normalizing is a strict no-op for renderScale==1 (instDir already unit).
        // ------------------------------------------------------------------
        vec3  instOrigin = (rayOrigin - inst.worldPos) / inst.renderScale;
        vec3  instDir    = normalize(rayDir);   // UNIT — see the M5b note above

        // Quick AABB cull in the base octree's [0,1]^3 grid (same transform the
        // traversal uses internally), before paying for full ESVO descent.
        //
        // Baked-Perf M5 Task 5.2 (CORRECTED, see Task 5.6 round 2 finding): the cull
        // TEST uses the TIGHT allocated-brick bounds (configs[oi].traceBoundsMin/Max,
        // Task 5.1) — the prior [0,1]^3 cull was structurally dead (every octree's own
        // root spans the whole grid, so the test could never reject anything a
        // genuinely tighter bound wouldn't also pass). BUT the gridT actually PASSED
        // into traverseOctreeInstanced below is still the FULL [0,1]^3 span, NOT the
        // tight one — an earlier version of this fix passed the tight gridT straight
        // through and broke bodies whose tight bounds are genuinely interior to the
        // root cube (verified live: backWall/sphereObj vanished 100% in the Cornell
        // repro) -- the tight span substituted a WRONG entry face into the ESVO
        // t_min/t_max setup, which can invert/degenerate and silently miss the
        // instance. (M5b note, 2026-07-17: that live "vanished when tight-gridT was
        // substituted" observation stands, but do NOT read it as the cause of the
        // backWall z≈−27 FAR-HIT -- that was a SEPARATE bug, the non-unit instDir
        // fed into the ESVO ray setup at renderScale≠1, now fixed at instDir above;
        // the M5-era attribution of the far-hit to this interior-entry frame issue
        // was disproven by the CPU mirror, which reproduces the far-hit purely from
        // the direction magnitude.) The tight bounds are therefore used ONLY as a
        // strictly-conservative fast-reject (miss
        // the instance entirely if the tight AABB itself isn't hit -- correct, since
        // "outside every allocated brick's tight AABB" always implies "outside the
        // populated geometry" regardless of which cube's boundary the descent enters
        // at) and are NOT substituted for the full-cube gridT/entry-point math the
        // traversal actually needs. This still gets the fast-reject win (skip full ESVO
        // descent for rays that miss the tight box) without touching where the
        // traversal itself begins.
        vec3 localRayOrigin = (configs[oi].worldToLocal * vec4(instOrigin, 1.0)).xyz;
        vec3 localRayDir    = mat3(configs[oi].worldToLocal) * instDir;
        vec3 traceBoundsMin, traceBoundsMax;
        getOctreeTraceBounds(oi, traceBoundsMin, traceBoundsMax);
        vec2 tightRejectT = rayAABBIntersection(localRayOrigin, localRayDir, traceBoundsMin, traceBoundsMax);
        // Full slab-intersection validity: tNear>tFar (tightRejectT.x>tightRejectT.y) is
        // also a miss, not just tFar<0 -- a thin tight box can be missed this way from
        // many angles even while tFar>=0, unlike the full [0,1]^3 root which a converging
        // camera ray enters with tNear<=tFar almost trivially.
        if (tightRejectT.y < 0.0 || tightRejectT.x > tightRejectT.y) {
#ifdef VIXEN_GPU_TRACE_HOOKS
            instanceIterCount[instIdx] = 0u;  // proves zero traversal iterations (Inc1 M4b test)
#endif
            continue;  // ray misses this instance's allocated-brick AABB entirely
        }
        vec2 gridT = rayAABBIntersection(localRayOrigin, localRayDir, vec3(0.0), vec3(1.0));

        if (gridT.y < 0.0) {
#ifdef VIXEN_GPU_TRACE_HOOKS
            instanceIterCount[instIdx] = 0u;  // proves zero traversal iterations (Inc1 M4b test)
#endif
            continue;  // ray misses this instance's AABB
        }

        g_lastFootprintRegime = 1u;
#ifdef VIXEN_COMPOSITION_COUNTERS
        // E6-T1: routed through the shared classifyCellFootprintRegime (SceneBindings.glsl)
        // instead of the former inline duplicate. Same formula, same inputs.
        float compositionDirLen = length(localRayDir);
        int compositionBpa = configs[oi].bricksPerAxis;
        if (compositionDirLen >= 1e-12 && compositionBpa > 0) {
            float compositionWorldDist =
                0.5 * (max(gridT.x, 0.0) + gridT.y) * inst.renderScale;
            float compositionCellWorldSize =
                ((1.0 / float(compositionBpa)) / compositionDirLen) * inst.renderScale;
            g_lastFootprintRegime = classifyCellFootprintRegime(
                compositionWorldDist, compositionCellWorldSize, pc.raySizeCoef, pc.raySizeBias, pc.cosmicK);
        }
#endif

#ifdef VIXEN_RTQUERY_TRAVERSAL
        // Round-3 hoist redesign: this instance's FORMAT_STORED_SDF geometry was
        // already searched (or not) by the single traverseRayQueryWorld call above
        // the loop -- its TLAS covers every FORMAT_STORED_SDF instance in one pass.
        // Re-running the ESVO/DDA path here for the same instance would be
        // redundant work at best and a double-counted candidate at worst. Non-SDF
        // instances (FORMAT_BINARY) are NOT in the TLAS (BodyOctreeSceneNode::
        // EnsureRtQueryTlasBuilt only builds BLASes from FORMAT_STORED_SDF's
        // brickGridLookup) and fall through to the normal branch below.
        if (configs[oi].formatId == FORMAT_STORED_SDF) {
#ifdef VIXEN_GPU_TRACE_HOOKS
            instanceIterCount[instIdx] = 0u;  // covered by the hoisted TLAS search, not this loop
#endif
            continue;
        }
#endif

        // Inc1 M4b: instances are sorted front-to-back CPU-side (see
        // BodyOctreeSceneNode::SortInstancesFrontToBack), so bestT here already
        // reflects every CLOSER instance's hit. gridT.x is a LOCAL grid-space t
        // (after worldToLocal), not directly comparable to bestT (a world-space
        // distance) — convert the AABB entry point back to world space the SAME
        // way traverseOctreeInstanced does internally (its rayStartWorld/
        // tEntryWorld computation) before comparing, or this reject silently
        // compares mismatched units. Skipped when the ray already starts inside
        // this instance's AABB (gridT.x < 0.0): there is no meaningful "entry
        // point" to compare in that case, and the instance may still be the
        // nearest hit.
        //
        // RENDERSCALE CORRECTION (Cornell demo M1 round 4; instDir clarified by
        // the M5b fix above): entryPointWorldInstSpace and instOrigin both live
        // in the de-instanced (÷renderScale) frame, so length(entryPointWorldInstSpace
        // - instOrigin) is a Euclidean distance in that shrunk frame — the SAME
        // shrunk-frame-distance quantity the ESVO traversal's hitT now is (instDir
        // is UNIT, see above). Multiplying by inst.renderScale converts it to true
        // world distance, matching bestT (which accumulates real hitT values, also
        // *renderScale below). Verified numerically against a live GPU readback
        // (HitRecord.worldPos): without this factor, a renderScale=3.2 body reported
        // hitT=5.625 for a ray whose true entry distance was 18.0 (5.625*3.2==18.0).
        if (gridT.x >= 0.0) {
            vec3 entryPointLocal = localRayOrigin + localRayDir * (gridT.x + EPSILON);
            vec3 entryPointWorldInstSpace =
                (configs[oi].localToWorld * vec4(entryPointLocal, 1.0)).xyz;
            float entryTWorld = length(entryPointWorldInstSpace - instOrigin) * inst.renderScale;
            // M6b Task 6b.0: use the SAME relative tie-band as isCloserHit's winner
            // compare, not a raw `>`. Without this, an instance whose entry point sits
            // within the tie-band of bestT (a seam neighbor) could be entry-rejected here
            // before its full traversal ever runs, silently disagreeing with the
            // deterministic lower-instIdx tiebreaker isCloserHit would otherwise apply --
            // this reject must never eliminate a candidate the winner compare would have
            // preferred.
            float entryTieBand = SEAM_TIE_EPS_REL * max(abs(bestT), 1.0);
            bool entryBehindCurrentBest = entryTWorld > bestT + entryTieBand;
#ifdef VIXEN_REGIME3_COMPOSITE
            // E2-T1: the nearest-hit cull is valid for opaque winners, but a
            // partial-coverage cosmic winner consumes one farther layer. Match
            // BodyInstanceRayMarch.comp's blend interval exactly so candidates
            // that cannot contribute retain the original cull.
            bool relaxForComposite =
                bestFootprintRegime == 3u &&
                bestResidualT > 1e-6 &&
                bestResidualT < 0.999999;
#ifdef VIXEN_COMPOSITION_COUNTERS
            if (entryBehindCurrentBest && relaxForComposite && !compositionRelaxedRayRecorded) {
                recordCompositionRelaxedRay();
                compositionRelaxedRayRecorded = true;
            }
#endif
            if (!relaxForComposite && entryBehindCurrentBest) {
#else
            if (entryBehindCurrentBest) {
#endif
                // This instance's nearest possible entry is already farther than
                // something already hit this ray — its full ESVO traversal
                // (below) cannot possibly produce the nearest hit. Skip it
                // entirely: zero traversal iterations, not just a discarded
                // result.
#ifdef VIXEN_GPU_TRACE_HOOKS
                instanceIterCount[instIdx] = 0u;  // proves zero traversal iterations (Inc1 M4b test)
#endif
                continue;
            }
        }

        // ------------------------------------------------------------------
        // Run ESVO traversal against this instance's octree
        // ------------------------------------------------------------------
        DebugRaySample dbg;
        dbg.pixel         = uvec2(ivec2(gl_GlobalInvocationID.xy));
        dbg.rayDir        = rayDir;
        dbg.octantMask    = 0u;
        dbg.hitFlag       = 0u;
        dbg.exitCode      = DEBUG_EXIT_NONE;
        dbg.lastStepMask  = 0u;
        dbg.iterationCount = 0u;
        dbg.scale         = configs[oi].esvoMaxScale;
        dbg.stateIdx      = 0u;
        dbg.tMin          = 0.0;
        dbg.tMax          = 0.0;
        dbg.scaleExp2     = 0.0;
        dbg.posMirrored   = vec3(0.0);
        dbg.localNorm     = vec3(0.0);

        vec3  hitColor;
        vec3  hitNormal;
        float hitT;
        float hitRoughness;
        float hitEmission;
        uint  hitBrick;
        uint  hitVoxel;

        // Pass the de-instanced ray: origin ÷renderScale, direction UNIT (M5b
        // fix — see the instOrigin/instDir block above). traverseOctreeInstanced
        // returns hitT as a Euclidean distance in the de-instanced (shrunk) frame;
        // multiply by inst.renderScale below to recover true world-distance units
        // before it's used for the cross-instance nearest-hit test or
        // HitRecord.worldPos reconstruction (BodyInstanceRayMarch.comp's
        // rayOrigin + rayDir*hitT). Every scene before backWall used renderScale=1,
        // which made the *renderScale a no-op and the non-unit-direction unit-mix
        // bug invisible -- see this file's other renderScale-correction (the
        // entryTWorld reject above) for the same factor on the early-reject math.
        //
        // Pass localRayOrigin/localRayDir/gridT (already computed above for the AABB cull)
        // straight through instead of letting traverseOctreeInstanced recompute the same
        // ray-vs-AABB math a second time — a single "beam test" pre-pass result shared by
        // both the cull check and the traversal, so they can never disagree at the AABB
        // silhouette (see traverseOctreeInstanced's own comment for why that mattered).
#ifdef VIXEN_RTQUERY_TRAVERSAL
        // W-RTQUERY Slice A round 3 -- THE HOIST REDESIGN: FORMAT_STORED_SDF
        // instances never reach this call site anymore (skipped above, covered by
        // the hoisted traverseRayQueryWorld search before the loop) -- only
        // FORMAT_BINARY instances land here, same as the flag-off ESVO path.
        bool instHit = traverseOctreeInstanced(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness, hitEmission,
                                           hitBrick, hitVoxel, dbg);
#elif defined(VIXEN_BRICKMAP_TRAVERSAL)
        // W-BRICKMAP Slice 2 round 3: coarse-grid DDA backend, FORMAT_STORED_SDF
        // only (RETARGETED -- round 2 had this inverted; brickGridLookup is only
        // populated for STORED_SDF octrees, see traverseCoarseGridInstanced's
        // header). FORMAT_BINARY falls back to the ESVO path at runtime per-
        // instance -- its brickLookup binding is a 1-byte placeholder.
        bool instHit = (configs[oi].formatId == FORMAT_STORED_SDF)
            ? traverseCoarseGridInstancedSdf(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           inst.renderScale,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness, hitEmission,
                                           hitBrick, hitVoxel, dbg)
            : traverseOctreeInstanced(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness, hitEmission,
                                           hitBrick, hitVoxel, dbg);
#else
        bool instHit = traverseOctreeInstanced(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness, hitEmission,
                                           hitBrick, hitVoxel, dbg);
#endif
        bool instWasFarField = g_lastHitWasFarField;  // round-7 blocker-1 probe #3
        uint instFootprintRegime = g_lastFootprintRegime;
        g_lastHitWasFarField = false;  // consume before the next instance's call
#ifdef VIXEN_REGIME3_COMPOSITE
        // Compositing slice part 2: consume+reset the SAME call-boundary-global
        // convention as g_lastHitWasFarField above (residualT's header comment,
        // SceneBindings.glsl). 1.0 (no-op) unless this instance's call was a
        // regime-3 accumulation-walk winner.
        float instResidualT = g_lastRegime3ResidualT;
        g_lastRegime3ResidualT = 1.0;
#endif
        hitT *= inst.renderScale;  // shrunk-frame distance -> true world distance (unit instDir; see comment above)
        if (instHit) sourceMask |= 2u;
#ifdef VIXEN_GPU_TRACE_HOOKS
        instanceIterCount[instIdx] = dbg.iterationCount;  // Inc1 M4b occlusion-reject test hook
#endif

        if (instHit && isCloserHit(hitT, uint(instIdx), bestT, bestInstIdx)) {
            if (instWasFarField) { incrFarFieldWon(); }  // round-7 blocker-1 probe #3
#ifdef VIXEN_REGIME3_COMPOSITE
            if (anyHit) { secondT = bestT; secondColor = bestColor; }
            bestResidualT = instResidualT;
#endif
            bestT           = hitT;
            // Tint by instance colour (multiply LOD-grey or material colour)
            bestColor       = hitColor * inst.color;
            bestNormal      = hitNormal;
            bestRoughness   = hitRoughness;   // Inc3 M3: per-voxel roughness
            bestBrickIndex  = hitBrick;
            bestVoxelIdx    = hitVoxel;
            bestInstIdx     = uint(instIdx);
            // E15-T1 (supersedes the old M11.2 recipeParams[3] read): the per-voxel
            // SEM_EMISSION channel now reaches primary shading -- hitEmission carries the winning
            // brick/voxel's own emission (near march: sampleHitShadingChannels reading
            // SEM_EMISSION at the hit grid position; far-field: shadeFromMipSample's
            // mip representative of the same channel), 0.0 for every non-emissive
            // stored body and for FORMAT_BINARY instances (no SEM_EMISSION channel;
            // the dispatch above sets hitEmission = 0.0 explicitly on that branch) --
            // byte-identical to the old recipeParams[3] read (always 0.0 for STORED,
            // since providerKind==PROVIDER_STORED never touches recipeParams).
            bestEmission    = hitEmission;
            bestWasFarField = instWasFarField;  // round 9: rides the winner, not a sticky global
            bestFootprintRegime = instFootprintRegime;
            anyHit          = true;
        }
#ifdef VIXEN_REGIME3_COMPOSITE
        else if (instHit) {
            // Lost isCloserHit against the current best, but still a real hit
            // this frame -- if it's nearer than the current second, it becomes
            // the new second (same-pass bookkeeping, not a re-trace). Not
            // gated on THIS candidate's own residualT: the composite blend
            // only ever consults secondColor when the WINNER (bestResidualT)
            // was a regime-3 partial-coverage hit, so any nearer loser is a
            // valid "what's behind the winner" candidate regardless of its
            // own kind.
            if (hitT < secondT) { secondT = hitT; secondColor = hitColor * inst.color; }
        }
#endif
    }

    hit.color      = bestColor;
    hit.normal     = bestNormal;
    hit.t          = bestT;
    hit.roughness  = bestRoughness;
    hit.brickIndex = bestBrickIndex;
    hit.voxelIdx   = bestVoxelIdx;
    hit.instIdx    = bestInstIdx;
    hit.emission   = bestEmission;
    hit.wasFarField = anyHit && bestWasFarField;
    hit.footprintRegime = bestFootprintRegime;
    hit.sourceMask = sourceMask;
#ifdef VIXEN_REGIME3_COMPOSITE
    hit.residualT   = bestResidualT;
    hit.behindColor = secondColor;
#else
    hit.residualT   = 1.0;
    hit.behindColor = vec3(0.0);
#endif
    return anyHit;
}

// ============================================================================
// TraceWorldShadow - any-hit occlusion test across all body instances
// ============================================================================
// Sampled Lighting Inc1 M2: a cheap "is anything between A and B" trace for
// shadow rays. Reuses TraceWorld's instance loop / AABB-cull / world<->local
// transform skeleton VERBATIM, but stops the moment ANY instance reports an
// occluder within [tmin, tmax] -- no nearest-hit bookkeeping, no normal/color/
// roughness/brick-id extraction, no cross-instance comparison. This is
// strictly cheaper per invocation than TraceWorld: the early-out means an
// occluded shadow ray can terminate after the FIRST instance/traversal that
// proves occlusion, rather than visiting every instance to find the nearest.
//
// "Occluder" uses the exact same notion of solid/hit as TraceWorld: a
// procedural body's traceProceduralBody() sphere-trace hit, or an ESVO
// traverseOctreeInstanced() leaf hit (binary voxel OR stored-SDF leaf,
// whichever traverseOctreeInstanced's internal dispatch selects) -- i.e.
// "occluder" here is exactly "whatever TraceWorld would have accepted as a
// candidate hit," just without keeping the nearest one.
//
// SELF-INTERSECTION BIAS (shadow acne): a shadow ray traced from a surface
// point toward a light will, without care, immediately re-intersect the
// surface it just left (the entry face of the very voxel/triangle it's
// leaving), producing false self-shadowing ("shadow acne"). This function
// does NOT apply any bias itself -- tmin is taken as given and t=0 is a
// valid occluder distance like any other. The CALLER is responsible for
// one of:
//   (a) starting the ray off the surface: origin + normal * eps, with
//       tmin left at (or near) 0, or
//   (b) leaving origin exactly on the surface and instead choosing tmin
//       to skip the first epsilon of the ray (tmin = eps rather than 0).
// Either is sufficient; do not do both (double-biasing wastes tmin budget
// and can clip real nearby occluders). The DirectLighting consumer (M4)
// picks one of these two conventions when it becomes the actual caller.
//
// Returns true the moment a confirmed occluder is found within [tmin, tmax];
// false if every instance was checked and none occluded (ray is lit).
bool TraceWorldShadow(vec3 origin, vec3 dir, float tmin, float tmax) {
    vec3 rayOrigin = origin;
    vec3 rayDir    = dir;
    g_lastShadowCompositionRegime = 1u;
    g_lastShadowCompositionSourceMask = 0u;

#ifdef VIXEN_RTQUERY_TRAVERSAL
    // W-RTQUERY Slice A round 3 -- THE HOIST REDESIGN: same hoist as TraceWorld's
    // own closest-hit search above -- ONE rayQuery against the TRUE WORLD ray and
    // [tmin,tmax] covers every FORMAT_STORED_SDF instance's occlusion test before
    // the per-instance loop below runs. See traverseRayQueryWorldAnyHit's header
    // (RayQueryTraversal.glsl) for the t-space contract; tmin/tmax are passed
    // straight through, WORLD-space, no per-instance renderScale division (unlike
    // the loop's own instTmin/instTmax below, which feed the ESVO/DDA de-instanced
    // frame).
    if (traverseRayQueryWorldAnyHit(rayOrigin, normalize(rayDir), tmin, tmax)) {
        return true;
    }
#endif

    int numInstances = clamp(pc.instanceCount, 0, 3 * 64); // safety cap, matches TraceWorld
    for (int instIdx = 0; instIdx < numInstances; ++instIdx) {

        // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: same skip mechanism as TraceWorld's
        // instance loop above — see that loop's identical block and InstanceSkipMaskBuffer's
        // comment in SceneBindings.glsl for the full rationale. No-op when the skip mask is
        // empty/unbound.
        if (isInstanceSkipped(instIdx)) {
            continue;
        }

        BodyInstance inst = bodyInstances[instIdx];

#ifdef VIXEN_SHADOW_DBG
        if (g_shadowDbgArm != 0) g_shadowDbgCurInst = instIdx;
#endif

        // --- Procedural provider: analytic SDF sphere-trace (no octree) ---
        // Mirrors TraceWorld's recipeId dispatch (Task 11/12/13): recipeId<2 stays on the
        // legacy analytic path; recipeId>=2 is registry-driven via the uber-shader splice.
        // Any-hit semantics mean no bound-sphere/far-subpixel early-outs are needed here —
        // this is already cheaper than TraceWorld's nearest-hit accumulation.
        if (inst.providerKind == PROVIDER_PROCEDURAL) {
            vec3 pNormal;
            float pT;
            bool pHit = false;

            if (inst.recipeId < 2u) {
                vec3 pCenter = inst.worldPos;
                vec3 pParams = vec3(inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2]);
                pHit = traceProceduralBody(inst.recipeId, pCenter, pParams, rayOrigin, rayDir,
                                           pNormal, pT);
            }
#ifdef VIXEN_UBER_RECIPE_SPLICED
            else {
                vec3  boundCenter; float boundRadius; float relaxation;
                getRecipeBoundSphere(inst.recipeId, boundCenter, boundRadius, relaxation);
                uint pSteps;
                // Recipe-Parameterization M2 Task 6: same inst.recipeParams[] threading as
                // TraceWorld's primary call site above.
                float uberParams[6] = float[6](
                    inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2],
                    inst.recipeParams[3], inst.recipeParams[4], inst.recipeParams[5]);
                pHit = traceUberRecipeBody(inst.recipeId, boundCenter, boundRadius, relaxation,
                                           rayOrigin, rayDir, uberParams, pNormal, pT, pSteps);
            }
#endif

            if (pHit && pT >= tmin && pT <= tmax) {
#ifdef VIXEN_COMPOSITION_COUNTERS
                g_lastShadowCompositionSourceMask |= 1u;
#endif
#ifdef VIXEN_SHADOW_DBG
                if (g_shadowDbgArm != 0) { g_shadowDbgInst = instIdx; g_shadowDbgSHitGrid = pT; g_shadowDbgLeafKind = 2; }
#endif
                return true;  // any-hit: no need to keep looking
            }
            continue;  // procedural body fully handled; skip the ESVO path
        }

        uint oi = inst.octreeIndex;  // index into runtime-sized configs[] SSBO (I3.2)

        // Set globals used by fetchESVONode (via g_esvoNodeBase in ESVOTraversal.glsl)
        // and by marchBrickInstanced (via g_brickArrayBase below). Same contract as
        // TraceWorld -- see that function's identical block for the full rationale.
        g_octreeIdx      = int(oi);
        g_mipSampleLevel = 0u;
        g_esvoNodeBase   = configs[oi].nodeArrayBase;
        g_brickArrayBase = configs[oi].brickArrayBase;

        // World -> instance-local ray transform (identical to TraceWorld; see
        // that function's block comment for the full derivation). ORIGIN is
        // de-instanced (÷renderScale); DIRECTION is UNIT (M5b root-cause fix,
        // 2026-07-17 — a non-unit rayDir/renderScale makes the ESVO state.t_min
        // renderScale× larger than tEntryWorld, mixing units in the hitT sum and
        // placing the occluder renderScale× too far; occlusion at renderScale≠1
        // was consequently wrong). hitT *= renderScale below converts the
        // shrunk-frame distance to true world. No-op for renderScale==1.
        vec3  instOrigin = (rayOrigin - inst.worldPos) / inst.renderScale;
        vec3  instDir    = normalize(rayDir);   // UNIT — see the M5b note in TraceWorld

        // Quick AABB cull in the base octree's [0,1]^3 grid, before paying for
        // full ESVO descent. Tight-bounds fast-reject, same as TraceWorld's identical
        // block (see there for the full derivation).
        vec3 localRayOrigin = (configs[oi].worldToLocal * vec4(instOrigin, 1.0)).xyz;
        vec3 localRayDir    = mat3(configs[oi].worldToLocal) * instDir;
        vec3 traceBoundsMin, traceBoundsMax;
        getOctreeTraceBounds(oi, traceBoundsMin, traceBoundsMax);
        vec2 tightRejectT = rayAABBIntersection(localRayOrigin, localRayDir, traceBoundsMin, traceBoundsMax);
        if (tightRejectT.y < 0.0 || tightRejectT.x > tightRejectT.y) {
            continue;  // ray misses this instance's allocated-brick AABB entirely
        }
        vec2 gridT = rayAABBIntersection(localRayOrigin, localRayDir, vec3(0.0), vec3(1.0));
        if (gridT.y < 0.0) {
            continue;  // ray misses this instance's AABB entirely
        }

#ifdef VIXEN_COMPOSITION_COUNTERS
        uint instCompositionRegime = 1u;
        // E6-T1: routed through the shared classifyCellFootprintRegime (SceneBindings.glsl)
        // instead of the former inline duplicate. Same formula, same inputs.
        float compositionDirLen = length(localRayDir);
        int compositionBpa = configs[oi].bricksPerAxis;
        if (compositionDirLen >= 1e-12 && compositionBpa > 0) {
            float compositionWorldDist =
                0.5 * (max(gridT.x, 0.0) + gridT.y) * inst.renderScale;
            float compositionCellWorldSize =
                ((1.0 / float(compositionBpa)) / compositionDirLen) * inst.renderScale;
            instCompositionRegime = classifyCellFootprintRegime(
                compositionWorldDist, compositionCellWorldSize, pc.raySizeCoef, pc.raySizeBias, pc.cosmicK);
        }
        // Slice 0 counts destination wave ENTRIES, not only occluders. Reaching
        // this AABB means the materialized evaluator participated in the entry;
        // preserve the most accumulative destination regime across candidates.
        g_lastShadowCompositionRegime =
            max(g_lastShadowCompositionRegime, instCompositionRegime);
        g_lastShadowCompositionSourceMask |= 2u;
#endif

#ifdef VIXEN_RTQUERY_TRAVERSAL
        // Round-3 hoist redesign: this instance's FORMAT_STORED_SDF geometry was
        // already occlusion-tested by the single traverseRayQueryWorldAnyHit call
        // above the loop -- see TraceWorld's identical skip for the full rationale.
        if (configs[oi].formatId == FORMAT_STORED_SDF) {
            continue;
        }
#endif

        // Baked-Perf M4 Task 4.2 (audit C1/C2 / Top #7): instance reject at entry-t > tmax --
        // this instance's AABB entry point cannot possibly hold an occluder within [tmin,tmax]
        // if the entry itself is already farther than tmax (the light). Same world-space
        // entry-distance reprojection TraceWorld's own entryTWorld reject uses (that function's
        // identical block has the full derivation); skipped when the ray already starts inside
        // this instance's AABB (gridT.x < 0.0), matching TraceWorld's own skip condition there.
        if (gridT.x >= 0.0) {
            vec3 entryPointLocal = localRayOrigin + localRayDir * (gridT.x + EPSILON);
            vec3 entryPointWorldInstSpace =
                (configs[oi].localToWorld * vec4(entryPointLocal, 1.0)).xyz;
            float entryTWorld = length(entryPointWorldInstSpace - instOrigin) * inst.renderScale;
            if (entryTWorld > tmax) {
                continue;  // no occluder in this instance can be nearer than the light itself
            }
        }

        // Any-hit occlusion march (Task 4.2): no gradient/color/roughness payload, no
        // DebugRaySample threading (TraceWorldShadow has no pixel/dbg concept to snapshot --
        // see traverseOctreeInstancedAnyHit's own header). tmin/tmax are converted into the
        // SAME de-instanced (÷renderScale) shrunk frame traverseOctreeInstanced's hitT lives
        // in before its own `*= inst.renderScale` conversion below -- i.e. divide, not
        // multiply, the world-space [tmin,tmax] by renderScale to match.
        float instTmin = tmin / inst.renderScale;
        float instTmax = tmax / inst.renderScale;
#ifdef VIXEN_RTQUERY_TRAVERSAL
        // W-RTQUERY Slice A round 3 -- THE HOIST REDESIGN: FORMAT_STORED_SDF
        // instances never reach this call site anymore (skipped above, covered by
        // the hoisted traverseRayQueryWorldAnyHit search before the loop) -- only
        // FORMAT_BINARY instances land here.
        bool instHit = traverseOctreeInstancedAnyHit(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           instTmin, instTmax);
#elif defined(VIXEN_BRICKMAP_TRAVERSAL)
        // Round 3: retargeted to FORMAT_STORED_SDF -- see the closest-hit call site above.
        bool instHit = (configs[oi].formatId == FORMAT_STORED_SDF)
            ? traverseCoarseGridInstancedSdfAnyHit(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           instTmin, instTmax)
            : traverseOctreeInstancedAnyHit(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           instTmin, instTmax);
#else
        bool instHit = traverseOctreeInstancedAnyHit(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           instTmin, instTmax);
#endif

        if (instHit) {
#ifdef VIXEN_SHADOW_DBG
            if (g_shadowDbgArm != 0) { g_shadowDbgInst = instIdx; }
#endif
            return true;  // any-hit: confirmed occluder, stop immediately (already tmin/tmax-checked inside)
        }
    }

    return false;  // no occluder found in [tmin, tmax] across any instance -- lit
}

#endif // TRACEWORLD_GLSL
