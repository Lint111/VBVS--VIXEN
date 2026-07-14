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
};

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

    // -----------------------------------------------------------------------
    // INSTANCE LOOP
    // -----------------------------------------------------------------------
    int numInstances = clamp(pc.instanceCount, 0, 3 * 64); // safety cap
    for (int instIdx = 0; instIdx < numInstances; ++instIdx) {

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
                    instanceIterCount[instIdx] = 0u;
                    continue;  // ray misses this instance's bound sphere entirely
                }
                float entryT = max(-b - sqrt(disc), 0.0);
                if (entryT > bestT) {
                    instanceIterCount[instIdx] = 0u;
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
                    instanceIterCount[instIdx] = 1u;  // flat-shaded, not a full march — nonzero proves it wasn't rejected
                    pHit    = true;
                    pT      = entryT;
                    pNormal = normalize(-rayDir);  // face the camera — cheapest plausible normal for a sub-pixel blob
                } else {
                    uint pSteps;
                    pHit = traceUberRecipeBody(inst.recipeId, boundCenter, boundRadius, relaxation,
                                               rayOrigin, rayDir, pNormal, pT, pSteps);
                    // Task 12 evidence (c): a non-rejected instance always writes its real march
                    // step count (>=1, even on a miss that exhausted MAX_STEPS or exited tFar) —
                    // only the two continue-above paths leave this 0u, so "0 here" means "the
                    // early-reject fired," matching the ESVO branch's own convention exactly.
                    instanceIterCount[instIdx] = pSteps;
                }
            }
#endif

            if (pHit && pT < bestT) {
                bestT          = pT;
                bestColor      = inst.color;   // procedural base colour = instance tint
                bestNormal     = pNormal;      // smooth SDF-gradient normal
                bestBrickIndex = 0u;
                bestVoxelIdx   = 0u;
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
        //   p_base = (p_world      - worldPos) / renderScale     (point)
        //   d_base =  d_world                  / renderScale     (direction)
        //
        // CRITICAL: the SAME /renderScale must scale BOTH origin and
        // direction, or the t-parametrisation is warped and hitT (used for the
        // cross-instance nearest-hit test and the LOD coarse-shade distance)
        // ends up in inconsistent units when renderScale != 1.  The translation
        // term drops out of the direction because a direction is a difference
        // of points; the scale does not.
        //
        // We deliberately do NOT renormalise d_base: traverseOctreeInstanced
        // and initRayCoefficients are scale-tolerant (they derive t-spans from
        // ratios), and keeping d_base = rayDir/renderScale means hitT is in the
        // SAME world-distance units across every instance, so the nearest-hit
        // comparison is correct.  (rayDir itself is already unit-length.)
        // ------------------------------------------------------------------
        float invScale = 1.0 / inst.renderScale;
        vec3  instOrigin = (rayOrigin - inst.worldPos) * invScale;
        vec3  instDir    = rayDir * invScale;

        // Quick AABB cull in the base octree's [0,1]^3 grid (same transform the
        // traversal uses internally), before paying for full ESVO descent.
        vec3 localRayOrigin = (configs[oi].worldToLocal * vec4(instOrigin, 1.0)).xyz;
        vec3 localRayDir    = mat3(configs[oi].worldToLocal) * instDir;
        vec2 gridT = rayAABBIntersection(localRayOrigin, localRayDir, vec3(0.0), vec3(1.0));

        if (gridT.y < 0.0) {
            instanceIterCount[instIdx] = 0u;  // proves zero traversal iterations (Inc1 M4b test)
            continue;  // ray misses this instance's AABB
        }

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
        // ROOT-CAUSE FIX (Cornell demo M1 round 4): entryPointWorldInstSpace is
        // measured in the (instOrigin-relative, i.e. ALREADY /renderScale) frame
        // -- length(entryPointWorldInstSpace - instOrigin) is therefore a t
        // PARAMETER along instDir, not a true world distance, UNLESS renderScale
        // == 1 (every scene before this one happened to use renderScale=1, which
        // made this bug invisible -- see instDir's own /invScale derivation
        // above: instDir is rayDir/renderScale, NOT unit-length, contrary to the
        // old comment here). Multiplying by inst.renderScale converts the
        // parameter back to true world-distance units, matching bestT (which
        // accumulates real hitT values, now also renderScale-corrected below).
        // Verified numerically against a live GPU readback (HitRecord.worldPos):
        // without this factor, a renderScale=3.2 body reported hitT=5.625 for a
        // ray whose true entry distance was 18.0 (5.625*3.2==18.0 exactly).
        if (gridT.x >= 0.0) {
            vec3 entryPointLocal = localRayOrigin + localRayDir * (gridT.x + EPSILON);
            vec3 entryPointWorldInstSpace =
                (configs[oi].localToWorld * vec4(entryPointLocal, 1.0)).xyz;
            float entryTWorld = length(entryPointWorldInstSpace - instOrigin) * inst.renderScale;
            if (entryTWorld > bestT) {
                // This instance's nearest possible entry is already farther than
                // something already hit this ray — its full ESVO traversal
                // (below) cannot possibly produce the nearest hit. Skip it
                // entirely: zero traversal iterations, not just a discarded
                // result.
                instanceIterCount[instIdx] = 0u;  // proves zero traversal iterations (Inc1 M4b test)
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
        uint  hitBrick;
        uint  hitVoxel;

        // Pass the de-instanced ray (origin AND direction both scaled by
        // 1/renderScale). ROOT-CAUSE FIX (Cornell demo M1 round 4): instDir is
        // NOT unit-length when renderScale != 1 (it is rayDir/renderScale), so
        // traverseOctreeInstanced's returned hitT is a parameter along instDir,
        // not a true world distance -- multiply by inst.renderScale below to
        // recover true world-distance units before it's used for the
        // cross-instance nearest-hit test or HitRecord.worldPos reconstruction
        // (BodyInstanceRayMarch.comp's rayOrigin + rayDir*hitT). Every scene
        // before this one used renderScale=1, which made the missing factor
        // invisible (multiplying by 1 is a no-op) -- see this file's other
        // renderScale-correction (the entryTWorld reject above) for the same fix
        // applied to the front-to-back early-reject's own copy of this math.
        //
        // Pass localRayOrigin/localRayDir/gridT (already computed above for the AABB cull)
        // straight through instead of letting traverseOctreeInstanced recompute the same
        // ray-vs-AABB math a second time — a single "beam test" pre-pass result shared by
        // both the cull check and the traversal, so they can never disagree at the AABB
        // silhouette (see traverseOctreeInstanced's own comment for why that mattered).
        bool instHit = traverseOctreeInstanced(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness,
                                           hitBrick, hitVoxel, dbg);
        hitT *= inst.renderScale;  // parameter-along-instDir -> true world distance (see comment above)
        instanceIterCount[instIdx] = dbg.iterationCount;  // Inc1 M4b occlusion-reject test hook

        if (instHit && hitT < bestT) {
            bestT           = hitT;
            // Tint by instance colour (multiply LOD-grey or material colour)
            bestColor       = hitColor * inst.color;
            bestNormal      = hitNormal;
            bestRoughness   = hitRoughness;   // Inc3 M3: per-voxel roughness
            bestBrickIndex  = hitBrick;
            bestVoxelIdx    = hitVoxel;
            anyHit          = true;
        }
    }

    hit.color      = bestColor;
    hit.normal     = bestNormal;
    hit.t          = bestT;
    hit.roughness  = bestRoughness;
    hit.brickIndex = bestBrickIndex;
    hit.voxelIdx   = bestVoxelIdx;
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

    int numInstances = clamp(pc.instanceCount, 0, 3 * 64); // safety cap, matches TraceWorld
    for (int instIdx = 0; instIdx < numInstances; ++instIdx) {

        BodyInstance inst = bodyInstances[instIdx];

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
                pHit = traceUberRecipeBody(inst.recipeId, boundCenter, boundRadius, relaxation,
                                           rayOrigin, rayDir, pNormal, pT, pSteps);
            }
#endif

            if (pHit && pT >= tmin && pT <= tmax) {
                return true;  // any-hit: no need to keep looking
            }
            continue;  // procedural body fully handled; skip the ESVO path
        }

        uint oi = inst.octreeIndex;  // index into runtime-sized configs[] SSBO (I3.2)

        // Set globals used by fetchESVONode (via g_esvoNodeBase in ESVOTraversal.glsl)
        // and by marchBrickInstanced (via g_brickArrayBase below). Same contract as
        // TraceWorld -- see that function's identical block for the full rationale.
        g_octreeIdx      = int(oi);
        g_esvoNodeBase   = configs[oi].nodeArrayBase;
        g_brickArrayBase = configs[oi].brickArrayBase;

        // World -> instance-local ray transform (identical to TraceWorld; see
        // that function's block comment for the full derivation of why the
        // SAME invScale must divide both origin and direction).
        float invScale = 1.0 / inst.renderScale;
        vec3  instOrigin = (rayOrigin - inst.worldPos) * invScale;
        vec3  instDir    = rayDir * invScale;

        // Quick AABB cull in the base octree's [0,1]^3 grid, before paying for
        // full ESVO descent.
        vec3 localRayOrigin = (configs[oi].worldToLocal * vec4(instOrigin, 1.0)).xyz;
        vec3 localRayDir    = mat3(configs[oi].worldToLocal) * instDir;
        vec2 gridT = rayAABBIntersection(localRayOrigin, localRayDir, vec3(0.0), vec3(1.0));
        if (gridT.y < 0.0) {
            continue;  // ray misses this instance's AABB entirely
        }

        // NOTE: unlike TraceWorld, there is no "farther than bestT already
        // found" reject here -- any-hit semantics mean the FIRST occluder
        // found (in instance-loop order, not necessarily nearest) is enough
        // to prove occlusion. Skipping the reject also means we don't need
        // TraceWorld's world-space entry-distance reprojection math for
        // instances that are behind an already-found occluder; we simply
        // never get there because we've already returned true.

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
        uint  hitBrick;
        uint  hitVoxel;

        bool instHit = traverseOctreeInstanced(instOrigin, instDir,
                                           localRayOrigin, localRayDir, gridT,
                                           hitColor, hitNormal, hitT,
                                           hitRoughness,
                                           hitBrick, hitVoxel, dbg);
        hitT *= inst.renderScale;  // parameter-along-instDir -> true world distance (see TraceWorld's identical fix)

        if (instHit && hitT >= tmin && hitT <= tmax) {
            return true;  // any-hit: confirmed occluder, stop immediately
        }
    }

    return false;  // no occluder found in [tmin, tmax] across any instance -- lit
}

#endif // TRACEWORLD_GLSL
