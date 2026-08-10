// ============================================================================
// RayQueryTraversal.glsl -- VK_KHR_ray_query per-brick-AABB TLAS backend
// (W-RTQUERY Slice A: third search backend alongside ESVO / the coarse-grid
// DDA, isolating the SEARCH phase while sharing the IDENTICAL sampling tail
// -- marchBrickSdfCell/marchBrickSdfCellAnyHit, StoredSdf.glsl -- the DDA
// backend uses (traverseCoarseGridInstancedSdf, SceneBindings.glsl ~1881).
// FORMAT_STORED_SDF only: the TLAS is built CPU-side from the SAME
// brickGridLookup/brickLookupBase source the DDA backend walks
// (BodyOctreeSceneNode::EnsureRtQueryTlasBuilt), so a FORMAT_BINARY octree
// (no lookup table populated for it) has no AABBs and is never instanced.
// ============================================================================
// Only compiled in when VIXEN_RTQUERY_TRAVERSAL is set (TraceWorld.glsl's
// #ifdef call sites); the TLAS binding itself is gated the same way -- an
// unbound descriptor at this binding would break the flag-off app's
// descriptor set (same reasoning as depthDistanceImage's VIXEN_B1_OCCLUSION_
// CULL gate, BodyInstanceRayMarch.comp).
// ============================================================================
//
// ROUND 3 -- THE HOIST REDESIGN (supersedes the per-instance-loop wiring):
// the prior version was called INSIDE TraceWorld's per-instance loop with the
// DE-INSTANCED ray (instOrigin/instDir), while the TLAS is built in TRUE
// WORLD space (translate(worldPos)*scale(renderScale)*localToWorld PER
// INSTANCE, BodyOctreeSceneNode.cpp). That mixed three inconsistent frames on
// one path. This version is called ONCE, before the instance loop, with the
// TRUE WORLD ray -- the TLAS's own per-instance transform does all the
// world<->object work, so there is no manual world->local inversion here at
// all. See traverseRayQueryWorld's header for the full t-space contract.
// ============================================================================

#ifdef VIXEN_RTQUERY_TRAVERSAL

// #extension GL_EXT_ray_query moved to SceneBindings.glsl's file head (fix 5):
// this file is #included ~400 lines into SceneBindings.glsl, well after other
// includes/declarations have already emitted tokens into the translation unit
// -- an #extension directive that late is mid-translation-unit, which the SDI
// merge-variants tool (and some driver GLSL front-ends) reject/mishandle. The
// directive itself lives at SceneBindings.glsl's top, guarded by the same
// VIXEN_RTQUERY_TRAVERSAL #ifdef so a flag-off compile is byte-identical.

// Binding 40: first free slot past the march shader's highest bound index (36).
// instanceCustomIndex on each TLAS instance is the INSTANCE index -- the same
// index TraceWorld's own loop uses into bodyInstances[] (BodyOctreeSceneNode::
// EnsureRtQueryTlasBuilt, round-3 hoist redesign) -- NOT the octree index. The
// octree is derived from it exactly like TraceWorld's loop does:
// oi = bodyInstances[ci].octreeIndex.
layout(set = 0, binding = 40) uniform accelerationStructureEXT rtQueryTlas;

// ----------------------------------------------------------------------------
// traverseRayQueryWorld -- closest-hit search over the per-brick-AABB TLAS,
// hoisted OUT of the per-instance loop: called ONCE with the true world ray,
// before TraceWorld's instance loop runs, because the TLAS already carries
// each instance's own local->world transform -- one rayQuery visits every
// FORMAT_STORED_SDF instance's occupied bricks in a single pass.
//
// T-SPACE CONTRACT (read before touching this function):
//   worldOrigin/worldDirUnit are TRUE WORLD space, worldDirUnit UNIT length.
//   rayQueryInitializeEXT is given this ray directly with [0, 1e30] --
//   the whole rayQuery interval (and thus every t rayQueryGenerateIntersectionEXT
//   commits) is measured in WORLD units.
//
//   Per AABB candidate, rayQueryGetIntersectionObjectRayOriginEXT/
//   ObjectRayDirectionEXT return the ray transformed by the CANDIDATE
//   INSTANCE's inverse world->object matrix M^-1 (the TLAS instance transform
//   is localToWorld = translate(worldPos)*scale(renderScale)*configs[oi].
//   localToWorld, so object space here IS configs[oi]'s own [0,1]^3 local
//   grid space -- the SAME frame localRayOrigin/localRayDir occupy in the DDA/
//   ESVO backends): objOrigin = M^-1 * worldOrigin (point, translation
//   included), objDir = M^-1 * worldDirUnit (VECTOR, no translation --
//   linear only). Because a ray point is affine in its parameter,
//   M^-1(worldOrigin + t*worldDirUnit) = objOrigin + t*objDir for ANY t --
//   i.e. the SAME scalar t that parameterizes the world-space ray also
//   parameterizes the object-space ray against the UNNORMALIZED objDir. So
//   slab-testing the brick AABB as objOrigin + s*objDir and solving for s
//   yields s == tWorld DIRECTLY: no division by length(objDir) is needed to
//   go from the slab solve to a world-space t. (objDir is NOT unit -- its
//   length encodes the world->object scale, e.g. 1/renderScale times
//   configs[oi]'s own grid-to-world scale -- but that length only matters
//   for converting a GRID-SPACE march distance back to this same s/tWorld
//   parameter, which the code below does via dirLen = length(objDir), exactly
//   mirroring how the DDA/ESVO backends convert their own local-frame march
//   distance back to world units.)
//
//   The returned hitT is this same tWorld quantity throughout -- callers need
//   NO *renderScale correction (unlike TraceWorld's ESVO/DDA call sites, whose
//   traversal runs in a shrunk de-instanced frame and must rescale after the
//   fact). The TLAS transform already did that scaling before this function
//   ever saw the ray.
// ----------------------------------------------------------------------------
bool traverseRayQueryWorld(vec3 worldOrigin, vec3 worldDirUnit,
                            out vec3 hitColor, out vec3 hitNormal, out float hitT,
                            out float hitRoughness, out float hitEmission,
                            out uint hitBrickIndex, out uint hitVoxelLinearIdx,
                            out uint hitInstanceIdx,
                            inout DebugRaySample debugInfo) {
    hitColor = vec3(0.0); hitNormal = vec3(0.0); hitT = 0.0; hitRoughness = 0.5;
    hitEmission = 0.0;
    hitBrickIndex = 0u; hitVoxelLinearIdx = 0u; hitInstanceIdx = 0xFFFFFFFFu;
    g_lastFootprintRegime = 1u;
    debugInfo.hitFlag = 0u;
    debugInfo.exitCode = DEBUG_EXIT_NONE;

    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rtQueryTlas, gl_RayFlagsNoneEXT, 0xFF,
                           worldOrigin, 0.0, worldDirUnit, 1e30);

    // GENERATE-MIN-TRACKING RULE (mandatory -- see test_rayquery_feasibility.cpp's
    // Slice-1 finding): rayQueryGenerateIntersectionEXT's tHit must lie within the
    // CURRENT ray interval [tMin, committed t]; generating beyond the committed
    // hit is app UB and NVIDIA implements it as an unconditional last-write-wins
    // replace. Track a running bestT (WORLD space, matching the interval this
    // rayQuery was initialized with) and generate ONLY on improvement.
    float bestT = 1e30;
    int   bestInstIdx = -1;
    int   bestOctreeIdx = -1;
    ivec3 bestCell = ivec3(0);

    // Same g_octreeIdx/g_brickArrayBase save/restore discipline as the prior
    // per-instance-loop version (fix 1): marchBrickSdfCell's internal
    // channelBaseFloats()/octreeConfig reads go through these globals, not
    // through an explicit parameter, so they must track the CANDIDATE's octree
    // before any sampling call.
    const int savedOctreeIdx      = g_octreeIdx;
    const int savedBrickArrayBase = g_brickArrayBase;

    while (rayQueryProceedEXT(rq)) {
        // ROUND-6 blocker-2 localization probe: unconditional counter as the
        // FIRST statement in the loop, before ANY continue -- distinguishes
        // "the TLAS yields zero AABB candidates at all" (loopEntries stays 0)
        // from "candidates exist but are consumed by one of the continues
        // below" (loopEntries fires, but farFieldCandidates/farFieldCount
        // downstream stay 0 -- bisect by moving this call past each continue
        // in turn). Deliberately a SEPARATE counter from farFieldCandidates
        // (round-5's gate-reaching tally, called later at :248) so the two
        // numbers aren't conflated.
        incrRtLoopEntries();
        if (rayQueryGetIntersectionTypeEXT(rq, false) != gl_RayQueryCandidateIntersectionAABBEXT) {
            continue;
        }
        // instanceCustomIndex is the INSTANCE index (BodyOctreeSceneNode::
        // EnsureRtQueryTlasBuilt, round-3 hoist redesign) -- derive the octree
        // exactly like TraceWorld's own instance loop does.
        const int  ci       = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
        const uint brickIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
        const int  oi       = int(bodyInstances[ci].octreeIndex);
        // ROUND-17 probe: octree-3-only loop-entry tally, taken as early as
        // possible (right after oi is known, before any of THIS candidate's
        // own continues below) -- see rtLoopEntriesOct3's field comment.
        if (oi == 3) incrRtLoopEntriesOct3();

        g_octreeIdx      = oi;
        g_mipSampleLevel = 0u;
        g_brickArrayBase = configs[oi].brickArrayBase;
        const int bpa = configs[oi].bricksPerAxis;
        if (bpa <= 0) continue;
        // Recover the brick's grid coord from its flat lookup-table index -- the
        // SAME flat = gz*bpa*bpa + gy*bpa + gx convention the DDA backend and the
        // CPU-side AABB build (BodyOctreeSceneNode::EnsureRtQueryTlasBuilt) both use.
        const int gx = int(brickIdx) % bpa;
        const int gy = (int(brickIdx) / bpa) % bpa;
        const int gz = int(brickIdx) / (bpa * bpa);
        const ivec3 cell = ivec3(gx, gy, gz);

        // Candidate ray in OBJECT (octree-local [0,1]^3) space, straight from the
        // API -- no manual world->local inversion. See this function's header for
        // the full t-space derivation: objDir is NOT unit (its length encodes the
        // world->object scale), and the slab-solve coefficient `s` below equals
        // tWorld directly because the ray's parameterization is affine.
        const vec3 objOrigin = rayQueryGetIntersectionObjectRayOriginEXT(rq, false);
        const vec3 objDir    = rayQueryGetIntersectionObjectRayDirectionEXT(rq, false);
        const float dirLen = length(objDir);
        if (dirLen < 1e-12) continue;
        const vec3 gridDirN = objDir / dirLen;  // unit direction, object/grid frame

        // Slab-test the brick's own AABB (octree-local [0,1]^3 space, same box the
        // BLAS itself holds) against objOrigin + s*objDir to find this candidate's
        // entry coefficient s -- which, per the header derivation, IS tWorld.
        const float resolution = float(bpa) * 8.0;  // BRICK_SIZE_SDF, matches marchBrickSdfCell's pin
        const vec3 bMinLocal = vec3(cell) / resolution;
        const vec3 bMaxLocal = bMinLocal + vec3(1.0 / resolution);
        const vec3 invD = vec3(
            abs(objDir.x) > 1e-8 ? 1.0 / objDir.x : 0.0,
            abs(objDir.y) > 1e-8 ? 1.0 / objDir.y : 0.0,
            abs(objDir.z) > 1e-8 ? 1.0 / objDir.z : 0.0);
        const vec3 t0 = (bMinLocal - objOrigin) * invD;
        const vec3 t1 = (bMaxLocal - objOrigin) * invD;
        const vec3 tlo = min(t0, t1);
        const vec3 thi = max(t0, t1);
        // Winning entry axis -- the axis whose slab bound produced tCellEnter --
        // for the entry-face snap below (DDA-twin parity, SceneBindings.glsl
        // ~1955-1958): argmax(tlo), ties broken by axis order x<y<z (matches the
        // ternary-chain convention the DDA's own axis-pick uses at its step site,
        // SceneBindings.glsl ~2030).
        const float tCellEnter = max(max(tlo.x, tlo.y), max(tlo.z, 0.0));
        if (tCellEnter >= bestT) {
            // ROUND-17 probe: octree-3-only tally of THIS specific reject site.
            if (oi == 3) incrFarFieldGateRejectOct3();
            continue;  // cannot possibly beat the current best
        }
        int enterAxis = (tlo.x >= tlo.y) ? ((tlo.x >= tlo.z) ? 0 : 2) : ((tlo.y >= tlo.z) ? 1 : 2);

        // gridEntry/gridDirN in TRUE geometric grid-voxel space ([0,bpa*8]) -- the
        // SAME frame marchBrickSdfCell expects (mirrors the DDA backend's identical
        // construction, SceneBindings.glsl's traverseCoarseGridInstancedSdf).
        const int brickSize = 8;  // BRICK_SIZE_SDF
        const float bpaF = float(bpa);
        vec3 cellEntryPos = objOrigin + objDir * tCellEnter;
        vec3 fracInCell = clamp(cellEntryPos * bpaF - vec3(cell), vec3(0.0), vec3(0.999999));
        // Entry-face snap parity with the DDA twin (SceneBindings.glsl ~1955-1958):
        // pin the entry axis's fractional coordinate to the exact slab plane the
        // ray entered through (0.0 or 0.999999 depending on step direction), rather
        // than trust the raw clamp -- at grazing incidence the raw fracInCell can
        // land a hair off the true entry face from float noise, which is exactly
        // the divergence class that produced the DDA backend's boot-bimodal frame
        // hash (see traverseCoarseGridInstancedSdf's own comment on this). Unlike
        // the DDA's DDA-stepped enterAxis (only known after at least one step),
        // every RT candidate is an independent slab test, so the winning axis is
        // simply the axis that produced tCellEnter (computed above) -- always
        // well-defined here, no "-1 = no snap" case needed.
        fracInCell[enterAxis] = (objDir[enterAxis] > 0.0) ? 0.0 : 0.999999;
        vec3 posInBrick = clamp(fracInCell * float(brickSize), vec3(0.0), vec3(float(brickSize) - 0.001));
        vec3 gridEntry = brickLocalToGrid(posInBrick, cell, brickSize);
        const float kEntryBias = 1e-3;  // grid-voxel units, matches the DDA backend's bias
        gridEntry += gridDirN * kEntryBias;

// ROUND-5 FIX: widened from bare VIXEN_COMPOSED_TRAVERSAL, mirroring the DDA
// twin's identical fix (SceneBindings.glsl) -- the gate's math has no
// composed-identity dependency; it was dead code on any standalone
// VIXEN_RTQUERY_TRAVERSAL boot (no VIXEN_COMPOSED_TRAVERSAL) since only the
// composed-resolution branch ever pushed that define (BuildRenderGraph.cpp:
// 1623-1631).
#if defined(VIXEN_COMPOSED_TRAVERSAL) || defined(VIXEN_RTQUERY_TRAVERSAL)
        // W-COMPOSED far-field tier: same criterion as the DDA twin's own far-field
        // block (SceneBindings.glsl, traverseCoarseGridInstancedSdf) -- mirrors the
        // ESVO screen-space LOD cutoff (tv_max * raySizeCoef + raySizeBias >=
        // scale_exp2) at the candidate-cell level, expressed in WORLD units (the
        // SAME frame the DDA twin's worldDistToCell/cellWorldSize use, and the
        // canonical world-frame form the ordinary ESVO gate itself uses elsewhere,
        // e.g. SceneBindings.glsl:1052 worldDistToChild*coef+bias>=childWorldSize).
        //
        // ROUND-3 FIX ITEM 4 (raySizeBias frame unification): tCellEnter, per this
        // function's header, is tWORLD directly (no rescale needed -- objDir isn't
        // unit, but the rayQuery interval and every s it solves are already WORLD-
        // space). The prior version instead converted the LHS DOWN to the local/
        // object frame (tCellEnter*dirLen) and compared against a local-frame RHS
        // (1.0/bpaF) -- correct for the coef*distance term (both sides were then
        // local-scale) but wrong for raySizeBias, which the ESVO gate's own
        // convention always adds UNSCALED in whatever frame the comparison runs
        // (see the citations above): a local-frame bias term is not the same
        // physical bias the DDA twin's world-frame comparison adds. Converting the
        // RHS UP to world instead (worldCellSize = (1.0/bpaF)/dirLen -- dirLen is
        // the world->object scale, so dividing the local cell size by it yields its
        // true-world size, exactly the DDA twin's cellWorldSize formula) keeps LHS
        // and RHS both in world units and makes raySizeBias directly comparable
        // between the two twins and the ESVO gate, matching the brief's ask
        // (currently inert either way since no writer sets raySizeBias yet -- see
        // RaySizeCoefNode.cpp, which writes only RAY_SIZE_COEF -- but the frame now
        // agrees so a future writer's value means the same thing on both backends).
        float worldCellSize = (1.0 / bpaF) / dirLen;
        incrFarFieldCandidates();  // round-5: counts candidates reaching the gate test, before it runs
        incrFarGenRectCellEntry();  // batch-24 FARGEN: rect-scoped cell-entry funnel
        if (oi == 3) incrFarFieldCandidatesOct3();  // round-17 probe: octree-3-only tally
        // ROUND-15 investigation (batch-15), comment corrected ROUND-16: a
        // raw-probe A/B against the DDA twin's worldDistToCell proved tCellEnter
        // here was ALREADY the correct world-frame quantity -- the ~2x GateLhs gap
        // was DDA's round-5 formula double-counting tBias+tCellEnter as if they
        // were additive (see SceneBindings.glsl's matching round-15/16 comment for
        // the full derivation). No change needed here; RT's math was correct all
        // along. CORRECTION (round-15 agent's own comment here was false, per the
        // round-15 CORRECTIONS ledger entry): tCellEnter here is NOT "equal to
        // tBias to the ULP" -- tCellEnter is computed via RT's TLAS-affine ray
        // parameterization and is a WORLD-frame `t` directly (this function's
        // header), whereas DDA's tBias is an INST-frame quantity reprojected
        // through octreeConfig.localToWorld. They are equal only insofar as both
        // correctly measure the same physical camera-to-cell-entry distance in
        // their OWN respective frames -- they are not the same variable in the
        // same frame, and no ULP-exact identity between them holds in general.
        recordFarFieldGateOperands(tCellEnter * pc.raySizeCoef + pc.raySizeBias, worldCellSize);  // round-6 probe
        recordFarGenRectLhsHistogram(tCellEnter * pc.raySizeCoef + pc.raySizeBias);  // batch-25 JOB 2
        // Regime-1/regime-2 transition test (deep-field mip-accessor policy
        // design doc §regimes: regime-1 is "footprint < voxel size", NOT
        // "footprint < cell/brick size"). BATCH-33 FIX -- same rationale as
        // the DDA twin (SceneBindings.glsl, see its comment for the full
        // derivation): admit under VIXEN_MIP_POLICY whenever the footprint
        // covers the FINEST ladder rung (one voxel, cellWorldSize/8) instead
        // of the whole brick cell, so descendToNodeOrdinal (already ladder-
        // aware, batch-29/30) gets a chance to resolve levels finer than
        // brick. Regime-1 boundary unchanged (voxel size, not cell size) --
        // flag-off keeps the original single-rung test byte-identical.
#ifdef VIXEN_MIP_POLICY
        const float kFarFieldBrickSize = 8.0;  // BRICK_SIZE_SDF
        float policyAdmitFootprint = worldCellSize / kFarFieldBrickSize;
        bool policyAdmits = pc.raySizeCoef > 0.0 &&
            tCellEnter * pc.raySizeCoef + pc.raySizeBias >= policyAdmitFootprint;
        // Batch-35: RT's candidate gate IS already the entry-point decision
        // (no march precedes it -- marchBrickSdfCell below only runs when
        // this branch is NOT taken), so no dispatch inversion is needed here,
        // unlike the DDA twin (SceneBindings.glsl). Counted for symmetry with
        // [PolicyEntryDispatch] so both backends' entry/march split is
        // visible from the same instrument family.
        recordPolicyEntryDispatch(policyAdmits);
        if (policyAdmits) {
#else
        if (pc.raySizeCoef > 0.0 &&
            tCellEnter * pc.raySizeCoef + pc.raySizeBias >= worldCellSize) {
#endif
            incrFarGenRectGateCross();  // batch-24 FARGEN: rect-scoped gate-cross funnel
            if (tCellEnter < bestT) {
                bestT = tCellEnter;
                bestInstIdx = ci;
                bestOctreeIdx = oi;
                bestCell = cell;
                hitNormal = -gridDirN;
#ifdef VIXEN_COMPOSITION_COUNTERS
                // Inline exact FootprintRegime formula (the production helper
                // specified by the residency doc does not exist yet).
                float compositionFootprint =
                    tCellEnter * pc.raySizeCoef + pc.raySizeBias;
                g_lastFootprintRegime =
                    (pc.raySizeCoef <= 0.0 || compositionFootprint < worldCellSize / 8.0)
                        ? 1u
                        : (compositionFootprint < pc.cosmicK * worldCellSize ? 2u : 3u);
#endif

                // Coordinate-bit descent to the REAL ESVO mip sample (replaces the
                // degenerate sampleHitShadingChannels entry-point resolution -- see
                // descendToNodeOrdinal's header, ESVOTraversal.glsl, for the full
                // derivation incl. the round-3 level-selection/farBit/depth fixes.
                // `cell` is the canonical/unmirrored brick-grid coordinate (recovered
                // from brickIdx above, same frame the DDA twin's `cell` uses); objDir
                // gives the octant mask directly (octantMaskFromDir, SVOTypes.glsl).
                // depth is the brick level's distance from root (farFieldDescentDepth,
                // SVOTypes.glsl -- NOT the old findMSB(bpa)). tCellEnter/worldCellSize
                // are the SAME world-frame quantities the footprint test just above
                // computed (item 4's fix), reused here so the descent can evaluate
                // ESVO's own per-level criterion and stop coarser than brick level.
                g_esvoNodeBase = configs[oi].nodeArrayBase;
                int farOctantMask = octantMaskFromDir(objDir);
                int farDepth = farFieldDescentDepth(configs[oi].bricksPerAxis);
                uint farNodeOrdinal;
                uint farSampledLevel;
                bool farReachedBrick = descendToNodeOrdinal(cell, farOctantMask, farDepth,
                                                            tCellEnter, worldCellSize, farNodeOrdinal, farSampledLevel);
                incrFarFieldCount();
                if (!farReachedBrick) incrFarFieldDescentFail();  // round-13 probe
                g_mipSampleLevel = farSampledLevel;
                bool farMipResolved = farReachedBrick && shadeFromMipSample(farNodeOrdinal, hitColor, hitNormal, hitEmission);
                recordFarFieldMipResolve(farMipResolved);  // round-7 blocker-1 probe
                if (farMipResolved) {
                    recordFarFieldSampledLevel(farSampledLevel);  // batch-32 JOB 1
                    recordFarFieldSampleIntensity(dot(hitColor, vec3(0.2126, 0.7152, 0.0722)));  // batch-33 JOB 2
                }
                if (!farMipResolved) {
                    hitColor  = vec3(0.5);
                    hitNormal = -gridDirN;
                }
                rayQueryGenerateIntersectionEXT(rq, tCellEnter);
                // Mirror the DDA twin's step recording (SceneBindings.glsl ~1977) so
                // stepType==8 (TRACE_STEP_FAR_FIELD_CUTOFF) is observable from the RT
                // path too, not just the DDA backend. nodeIndex uses the resolved mip
                // node ordinal (diagnostic-only field).
                recordTraceStep(TRACE_STEP_FAR_FIELD_CUTOFF, farNodeOrdinal, 0,
                                 0u, vec3(cell), tCellEnter, tCellEnter, uvec2(0u, 0u));
                g_lastHitWasFarField = true;  // round-7 blocker-1 probe #3 (may be
                                               // overwritten false by a later, closer
                                               // non-far-field candidate in this SAME
                                               // rayQuery loop -- see the plain
                                               // marchBrickSdfCell branch below)
            }
            continue;  // skip the full brick march for this candidate
        }
#endif

        vec3  nrm;
        float sHit;
        if (marchBrickSdfCell(oi, cell, gridEntry, gridDirN, nrm, sHit)) {
            const float gridScale = resolution;
            // tWorld = tCellEnter + (grid-space march distance converted back to the
            // s/tWorld parameter). The march runs in grid-voxel space at gridScale
            // voxels-per-object-unit along gridDirN (unit); objDir's own magnitude
            // (dirLen) is the object-per-world scale, so dividing by (dirLen*gridScale)
            // converts a grid-voxel distance to a tWorld delta -- the exact mirror of
            // traverseCoarseGridInstancedSdf's tHitLocal formula, just parameterized
            // by tWorld instead of a local-frame t (this backend never had a separate
            // local-frame t: s IS tWorld throughout, see header).
            const float tWorld = tCellEnter + (sHit + kEntryBias) / (dirLen * gridScale);
            if (tWorld < bestT) {
                bestT = tWorld;
                bestInstIdx = ci;
                bestOctreeIdx = oi;
                bestCell = cell;
                hitNormal = nrm;
                g_lastHitWasFarField = false;  // round-7 blocker-1 probe #3: a closer
                                                // ordinary brick march beat the far-field
                                                // candidate within this SAME rayQuery loop
                g_lastFootprintRegime = 1u;
                // Fix 4 (prior version): populate hitColor/hitRoughness via the SAME
                // channel-sample helper the DDA twin calls.
                sampleHitShadingChannels(gridEntry + gridDirN * sHit, vec3(1.0), 0.5, hitColor, hitRoughness, hitEmission);
                // GENERATE-MIN-TRACKING: the rayQuery's own interval is WORLD-space
                // (see the initialize call above), so generate this same tWorld --
                // the quantity bestT just tracked -- as the precondition requires.
                rayQueryGenerateIntersectionEXT(rq, tWorld);
            }
        }
    }

    if (bestOctreeIdx < 0) {
        g_octreeIdx      = savedOctreeIdx;
        g_brickArrayBase = savedBrickArrayBase;
        debugInfo.exitCode = DEBUG_EXIT_NONE;
        return false;
    }

    hitT              = bestT;
    hitInstanceIdx    = uint(bestInstIdx);
    // FIX 2: match the DDA twin's hitBrickIndex convention exactly (SceneBindings.glsl
    // ~1986/572) -- brickArrayBase + the LOOKED-UP local brick index, not the flat grid
    // coordinate directly (that was a third, incompatible convention).
    {
        const int bestBpa = configs[bestOctreeIdx].bricksPerAxis;
        const uint bestFlatIdx = uint(bestCell.z * bestBpa * bestBpa + bestCell.y * bestBpa + bestCell.x);
        const uint localBrickIdx = brickLookup[configs[bestOctreeIdx].brickLookupBase + bestFlatIdx];
        // Guard: should be impossible for a hit candidate -- fix 1's degenerate boxes
        // (min>max) never intersect, so an unallocated cell can't reach here. Skip
        // defensively rather than emit a bogus index into the brick pool.
        if (localBrickIdx == 0xFFFFFFFFu) {
            g_octreeIdx      = savedOctreeIdx;
            g_brickArrayBase = savedBrickArrayBase;
            debugInfo.exitCode = DEBUG_EXIT_NONE;
            return false;
        }
        hitBrickIndex = uint(configs[bestOctreeIdx].brickArrayBase) + localBrickIdx;
    }
    hitVoxelLinearIdx = 0u;
    debugInfo.hitFlag = 1u;
    // Leave g_octreeIdx/g_brickArrayBase pointed at the WINNING octree, matching
    // the prior version's convention -- TraceWorld's instance loop re-sets both
    // before it processes any per-instance (non-SDF) body anyway.
    return true;
}

// ----------------------------------------------------------------------------
// traverseRayQueryWorldAnyHit -- any-hit occlusion twin, hoisted the same way
// as traverseRayQueryWorld: called ONCE with the true world ray and [tmin,
// tmax] before TraceWorldShadow's instance loop runs. Same t-space contract
// as the closest-hit twin above (see its header) -- tmin/tmax are WORLD-space
// and used directly as the rayQuery interval, no per-instance rescale.
// ----------------------------------------------------------------------------
bool traverseRayQueryWorldAnyHit(vec3 worldOrigin, vec3 worldDirUnit,
                                  float tmin, float tmax) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rtQueryTlas, gl_RayFlagsNoneEXT, 0xFF,
                           worldOrigin, max(tmin, 0.0), worldDirUnit, tmax);

    const int savedOctreeIdx      = g_octreeIdx;
    const int savedBrickArrayBase = g_brickArrayBase;

    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) != gl_RayQueryCandidateIntersectionAABBEXT) {
            continue;
        }
        const int  ci       = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
        const uint brickIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
        const int  oi       = int(bodyInstances[ci].octreeIndex);

        g_octreeIdx      = oi;
        g_mipSampleLevel = 0u;
        g_brickArrayBase = configs[oi].brickArrayBase;
        const int bpa = configs[oi].bricksPerAxis;
        if (bpa <= 0) continue;
        const int gx = int(brickIdx) % bpa;
        const int gy = (int(brickIdx) / bpa) % bpa;
        const int gz = int(brickIdx) / (bpa * bpa);
        const ivec3 cell = ivec3(gx, gy, gz);

        const vec3 objOrigin = rayQueryGetIntersectionObjectRayOriginEXT(rq, false);
        const vec3 objDir    = rayQueryGetIntersectionObjectRayDirectionEXT(rq, false);
        const float dirLen = length(objDir);
        if (dirLen < 1e-12) continue;
        const vec3 gridDirN = objDir / dirLen;

        const float resolution = float(bpa) * 8.0;
        const vec3 bMinLocal = vec3(cell) / resolution;
        const vec3 bMaxLocal = bMinLocal + vec3(1.0 / resolution);
        const vec3 invD = vec3(
            abs(objDir.x) > 1e-8 ? 1.0 / objDir.x : 0.0,
            abs(objDir.y) > 1e-8 ? 1.0 / objDir.y : 0.0,
            abs(objDir.z) > 1e-8 ? 1.0 / objDir.z : 0.0);
        const vec3 t0 = (bMinLocal - objOrigin) * invD;
        const vec3 t1 = (bMaxLocal - objOrigin) * invD;
        const vec3 tlo = min(t0, t1);
        const float tCellEnter = max(max(tlo.x, tlo.y), max(tlo.z, 0.0));
        int enterAxis = (tlo.x >= tlo.y) ? ((tlo.x >= tlo.z) ? 0 : 2) : ((tlo.y >= tlo.z) ? 1 : 2);

#ifdef VIXEN_COMPOSITION_COUNTERS
        // A candidate AABB is a materialized destination evaluated by this
        // wave entry even when its SDF does not ultimately occlude the ray.
        // FootprintRegime is not materialized in code yet, so keep the design
        // formula inline for this probe call site.
        float compositionCellWorldSize = (1.0 / float(bpa)) / dirLen;
        float compositionFootprint = tCellEnter * pc.raySizeCoef + pc.raySizeBias;
        uint compositionRegime =
            (pc.raySizeCoef <= 0.0 || compositionFootprint < compositionCellWorldSize / 8.0)
                ? 1u
                : (compositionFootprint < pc.cosmicK * compositionCellWorldSize ? 2u : 3u);
        g_lastShadowCompositionRegime =
            max(g_lastShadowCompositionRegime, compositionRegime);
        g_lastShadowCompositionSourceMask |= 2u;
#endif

        const int brickSize = 8;
        const float bpaF = float(bpa);
        vec3 cellEntryPos = objOrigin + objDir * tCellEnter;
        vec3 fracInCell = clamp(cellEntryPos * bpaF - vec3(cell), vec3(0.0), vec3(0.999999));
        // Entry-face snap parity -- see the closest-hit twin's identical block.
        fracInCell[enterAxis] = (objDir[enterAxis] > 0.0) ? 0.0 : 0.999999;
        vec3 posInBrick = clamp(fracInCell * float(brickSize), vec3(0.0), vec3(float(brickSize) - 0.001));
        vec3 gridEntry = brickLocalToGrid(posInBrick, cell, brickSize);
        const float kEntryBias = 1e-3;
        gridEntry += gridDirN * kEntryBias;

        // Per-cell march budget derived from tmax, exactly as the DDA any-hit twin
        // does (traverseCoarseGridInstancedSdfAnyHit, SceneBindings.glsl ~2106) --
        // converted to grid units through the SAME t-space contract as the
        // closest-hit twin above (dirLen*gridScale voxels per tWorld unit). This
        // is the round-3 fix for the prior version's sMaxLimit=1e30 (which ignored
        // tmax entirely and produced spurious shadowing past the light).
        const float gridScale = resolution;
        const float sMaxLimit = (tmax > 0.0)
            ? max((tmax - tCellEnter) * dirLen * gridScale - kEntryBias, 0.0)
            : 1e6;
        if (tmax > 0.0 && sMaxLimit <= 0.0) continue;  // no march room in this cell -- try next candidate

        float sHit;
        if (marchBrickSdfCellAnyHit(oi, cell, gridEntry, gridDirN, sMaxLimit, sHit)) {
            const float tWorld = tCellEnter + (sHit + kEntryBias) / (dirLen * gridScale);
            if (tWorld >= tmin && tWorld <= tmax) {
                // Any-hit: no rayQueryGenerateIntersectionEXT needed here -- unlike
                // the closest-hit twin, this function returns on the FIRST confirmed
                // crossing and never inspects the rayQuery's committed intersection
                // again (no rayQueryProceedEXT after this point), so there is nothing
                // downstream that reads a committed t/instance off `rq`. Generating
                // would only matter if the caller kept querying `rq` post-return.
                g_octreeIdx      = savedOctreeIdx;
                g_brickArrayBase = savedBrickArrayBase;
                return true;
            }
        }
    }
    // No occluder found -- restore, mirroring the closest-hit twin's early-return.
    g_octreeIdx      = savedOctreeIdx;
    g_brickArrayBase = savedBrickArrayBase;
    return false;
}

#endif // VIXEN_RTQUERY_TRAVERSAL
