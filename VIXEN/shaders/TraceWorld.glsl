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
        if (inst.providerKind == PROVIDER_PROCEDURAL) {
            vec3 pCenter = inst.worldPos;
            vec3 pParams = vec3(inst.recipeParams[0], inst.recipeParams[1], inst.recipeParams[2]);
            vec3 pNormal;
            float pT;
            if (traceProceduralBody(inst.recipeId, pCenter, pParams, rayOrigin, rayDir,
                                    pNormal, pT)) {
                if (pT < bestT) {
                    bestT          = pT;
                    bestColor      = inst.color;   // procedural base colour = instance tint
                    bestNormal     = pNormal;      // smooth SDF-gradient normal
                    bestBrickIndex = 0u;
                    bestVoxelIdx   = 0u;
                    anyHit         = true;
                }
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
        if (gridT.x >= 0.0) {
            // Mirror traverseOctreeInstanced's OWN internal rayStartWorld/tEntryWorld
            // computation exactly (lines ~524-526 above): entryPointWorldInstSpace is
            // already in the de-instanced (instOrigin-relative) frame at REAL world
            // scale (configs[oi].localToWorld bakes in kWorldGridSize) — measuring its
            // distance from instOrigin (NOT re-applying renderScale/worldPos a second
            // time) gives the same world-consistent t the traversal itself uses for
            // hitT, by the identical scale-ratio-preserving argument documented above
            // (instDir = rayDir/renderScale is not unit-length, but t computed this way
            // is still in true world-distance units — see the invScale comment block).
            vec3 entryPointLocal = localRayOrigin + localRayDir * (gridT.x + EPSILON);
            vec3 entryPointWorldInstSpace =
                (configs[oi].localToWorld * vec4(entryPointLocal, 1.0)).xyz;
            float entryTWorld = length(entryPointWorldInstSpace - instOrigin);
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
        // 1/renderScale).  Because rayDir is unit-length, the returned hitT
        // equals the TRUE world distance along the original ray
        // (instOrigin + t*instDir maps back to rayOrigin + t*rayDir), so the
        // cross-instance nearest-hit test below compares like-for-like.
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

#endif // TRACEWORLD_GLSL
