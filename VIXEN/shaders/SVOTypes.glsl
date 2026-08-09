// ============================================================================
// SVOTypes.glsl - Shared SVO/ESVO Data Structures
// ============================================================================
// This file defines data structures that match the C++ SVOTypes.h layout.
// Include this file in any shader that needs to access SVO data.
//
// IMPORTANT: Keep this file in sync with libraries/SVO/include/SVOTypes.h
// ============================================================================

#ifndef SVO_TYPES_GLSL
#define SVO_TYPES_GLSL

// ============================================================================
// CHILD DESCRIPTOR (64 bits = uvec2)
// ============================================================================
// Matches C++ ChildDescriptor struct layout exactly.
//
// First 32 bits (descriptor.x):
//   bits 0-14:  childPointer (15 bits) - Offset to first child descriptor
//   bit 15:     farBit (1 bit) - 1 = childPointer is indirect reference
//   bits 16-23: validMask (8 bits) - Bit per child: 1 = slot contains voxel
//   bits 24-31: leafMask (8 bits) - Bit per child: 1 = voxel is leaf
//
// Second 32 bits (descriptor.y):
//   For INTERNAL nodes (contour mode):
//     bits 0-23:  contourPointer (24 bits) - Offset to contour data
//     bits 24-31: contourMask (8 bits) - Bit per child: 1 = has contour
//
//   For LEAF nodes at brick level (brick mode):
//     bits 0-23:  brickIndex (24 bits) - Index into sparse brick array
//     bits 24-31: flags (8 bits) - Reserved for future use
//
// The interpretation of the second 32 bits depends on context:
// - Contours are used for mesh surface approximation
// - Bricks are used for native voxel data storage
// ============================================================================

// Sentinel value for "no brick" or "no contour"
const uint SVO_INVALID_INDEX = 0xFFFFFFu;  // 24-bit max

// ============================================================================
// DESCRIPTOR FIELD EXTRACTION
// ============================================================================

// First 32 bits (hierarchy)
uint getChildPointer(uvec2 descriptor) {
    return descriptor.x & 0x7FFFu;  // bits 0-14
}

bool getFarBit(uvec2 descriptor) {
    return (descriptor.x & 0x8000u) != 0u;  // bit 15
}

uint getValidMask(uvec2 descriptor) {
    return (descriptor.x >> 16) & 0xFFu;  // bits 16-23
}

uint getLeafMask(uvec2 descriptor) {
    return (descriptor.x >> 24) & 0xFFu;  // bits 24-31
}

// Second 32 bits - Contour interpretation
uint getContourPointer(uvec2 descriptor) {
    return descriptor.y & 0xFFFFFFu;  // bits 0-23
}

uint getContourMask(uvec2 descriptor) {
    return (descriptor.y >> 24) & 0xFFu;  // bits 24-31
}

// Second 32 bits - Brick interpretation (for leaf nodes)
uint getBrickIndex(uvec2 descriptor) {
    return descriptor.y & 0xFFFFFFu;  // bits 0-23 (same field as contourPointer)
}

uint getBrickFlags(uvec2 descriptor) {
    return (descriptor.y >> 24) & 0xFFu;  // bits 24-31
}

// ============================================================================
// TIER-CROSSING LEAF INTERPRETATION (Tiered-ESVO Inc2 M2/M3)
// ============================================================================
// A THIRD interpretation of the same descriptor.y field pair, selected by
// farBit (SVOTypes.h ChildDescriptor::isTierCrossing/getTierRefIndex/
// getChildRootScaleHint — mirror those exactly). farBit==0 means "brick mode"
// (unchanged, see above); farBit==1 on a LEAF means "tier-crossing": bits 0-23
// of descriptor.y are an index into this tree's own slice of the concatenated
// TierRefTable (offset by OctreeConfig.tierRefTableBase), and bits 24-31 carry
// the child tree's own root ESVO scale hint (0-22). Callers MUST check
// getFarBit(descriptor) before choosing this interpretation vs. the brick-mode
// one above — same bits, mutually exclusive readings.
uint getTierRefIndex(uvec2 descriptor) {
    return descriptor.y & 0xFFFFFFu;  // bits 0-23 (same field as contourPointer/brickIndex)
}

uint getChildRootScaleHint(uvec2 descriptor) {
    return (descriptor.y >> 24) & 0xFFu;  // bits 24-31 (same field as contourMask/brickFlags)
}

// ============================================================================
// CHILD VALIDITY HELPERS
// ============================================================================

bool childExists(uint validMask, int childIndex) {
    return ((validMask >> childIndex) & 1u) != 0u;
}

bool childIsLeaf(uint leafMask, int childIndex) {
    return ((leafMask >> childIndex) & 1u) != 0u;
}

bool childHasContour(uint contourMask, int childIndex) {
    return ((contourMask >> childIndex) & 1u) != 0u;
}

// ============================================================================
// CHILD COUNTING (for packed array indexing)
// ============================================================================

// Count INTERNAL (non-leaf) children before childIndex in packed array
// Only counts children that EXIST (validMask) AND are NOT leaves (leafMask clear)
uint countInternalChildrenBefore(uint validMask, uint leafMask, int childIndex) {
    if (childIndex <= 0) return 0u;
    uint mask = (1u << childIndex) - 1u;
    uint internalMask = validMask & ~leafMask;
    return bitCount(internalMask & mask);
}

// Count LEAF children before childIndex (for brick/leaf lookup)
uint countLeavesBefore(uint validMask, uint leafMask, int childIndex) {
    if (childIndex <= 0) return 0u;
    uint mask = (1u << childIndex) - 1u;
    uint leafChildren = validMask & leafMask;
    return bitCount(leafChildren & mask);
}

// Resolve a leaf child's own final node-array index (its slot in esvoNodes[],
// the same ordinal MipBake.h's leaf-zero-sample fix and the traversal's
// mip[level][ordinal] fallback both address). Non-leaf children are stored
// contiguously first, then leaf children (SVORebuild.cpp PHASE 3) — mirrors
// handleLeafHitInstanced's own leafDescriptorIndex computation exactly, moved
// here so the traversal loop can resolve it too (Sparse-Mip ESVO LOD Inc1 M3
// Task 7 needs it BEFORE deciding whether to call the leaf-hit handler at all).
uint resolveLeafDescriptorIndex(uvec2 parentDescriptor, uint validMask, uint leafMask,
                                int localChildIdx) {
    uint childPointer          = getChildPointer(parentDescriptor);
    uint totalInternalChildren = bitCount(validMask & ~leafMask);
    uint leafChildrenBeforeMe  = countLeavesBefore(validMask, leafMask, localChildIdx);
    return childPointer + totalInternalChildren + leafChildrenBeforeMe;
}

// ============================================================================
// OCTANT MIRRORING (ESVO ray-direction space)
// ============================================================================
// The octant_mask encodes which axes are mirrored based on ray direction:
//   - Starts at 7
//   - XOR each bit for positive ray direction component
//   - Result: bit=0 means axis IS mirrored, bit=1 means NOT mirrored
//
// Conversion between mirrored-space (traversal) and local-space (storage):
//   localIdx = mirroredIdx ^ (~octant_mask & 7)
//   mirroredIdx = localIdx ^ (~octant_mask & 7)  // XOR is its own inverse

int mirroredToLocalOctant(int mirroredIdx, int octant_mask) {
    return mirroredIdx ^ ((~octant_mask) & 7);
}

int localToMirroredOctant(int localIdx, int octant_mask) {
    return localIdx ^ ((~octant_mask) & 7);
}

// Legacy alias for compatibility
int mirroredToWorldOctant(int mirroredIdx, int octant_mask) {
    return mirroredIdx ^ octant_mask;
}

// ============================================================================
// BRICK CONSTANTS
// ============================================================================

// Standard brick dimensions (8x8x8 voxels)
const int BRICK_SIZE = 8;
const int BRICK_VOXEL_COUNT = 512;  // 8 * 8 * 8

// Brick voxel indexing: linear index = z*64 + y*8 + x
uint brickVoxelIndex(ivec3 localCoord) {
    return uint(localCoord.z * 64 + localCoord.y * 8 + localCoord.x);
}

ivec3 brickVoxelCoord(uint linearIndex) {
    int z = int(linearIndex / 64u);
    int y = int((linearIndex % 64u) / 8u);
    int x = int(linearIndex % 8u);
    return ivec3(x, y, z);
}

// octantMaskFromDir: reconstruct octant_mask from a LOCAL ray direction --
// the far-field DDA/RT sites never build a full RayCoefficients (that's the
// ESVO backend's own machinery), but the mask is exactly the same 3-bit XOR
// initRayCoefficients computes from d.x/d.y/d.z sign (ESVOCoefficients.glsl
// ~82-85), so a full coefficients build isn't needed just for this bit. Used
// by descendToNodeOrdinal (ESVOTraversal.glsl, defined after fetchESVONode).
int octantMaskFromDir(vec3 localDir) {
    int mask = 7;
    if (localDir.x > 0.0) mask ^= 1;
    if (localDir.y > 0.0) mask ^= 2;
    if (localDir.z > 0.0) mask ^= 4;
    return mask;
}

// farFieldDescentDepth: the brick level's distance from root, for
// descendToNodeOrdinal (ESVOTraversal.glsl). esvoMaxScale - brickESVOScale is
// the EXACT root-to-brick hop count straight from the config's own scale
// fields (ShellOctreeGpu.h: brickESVOScale = esvoMaxScale - (userMaxLevels-1
// - brickUserScale), the same builder-depth derivation that stamps
// bricksPerAxis) -- round-3 fix item 2 (minor) replaces the old call sites'
// plain findMSB(bpa), which FLOORS log2(bpa) and is silently wrong whenever
// bricksPerAxis isn't a power of two (SVORebuild.cpp:261 builds it via a
// ceiling division, voxelsPerAxis+brickSideLength-1)/brickSideLength -- e.g.
// bpa=3 gives findMSB=1 instead of the correct depth 2, stopping the descent
// a level short while still reporting success. Deriving directly from the
// scale fields sidesteps the floor/ceil question (and the bpa-power-of-two
// assumption) entirely.
//
// SHALLOW-ROOT CAVEAT (round-3 fix item 2, blocker -- NOT fully closed here):
// this assumes the octree's node-array root sits at the frame-spanning depth
// (root hop 0 == the true tree root). SVORebuild.cpp:498-571 documents that a
// clustered sparse tree can converge SHALLOWER (Octree::rootDepth <
// frameDepth, see LaineKarrasOctree::rootShortfall()/effectiveLevels()) --
// but that shortfall is a CPU-only quantity today: ShellOctreeGpu.h's
// OctreeConfig builder never reads rootDepth/rootShortfall, so no GPU-visible
// signal exists to correct for it here. Fixing this fully needs a schema
// change (a new OctreeConfig field, kernel-codegen-owned, out of this file's
// scope) -- descendToNodeOrdinal's own per-hop childExists/leaf checks fail
// closed (return false, caller falls back) rather than silently sampling a
// wrong node in the common case where the mismatch causes a missing-child or
// early-leaf hit, but a shallow root that still validly resolves bpa-many
// hops down a WRONG path is not caught. Latent on the shipped (frame-
// spanning) scene; tracked as a follow-up, not fixed by this ceil-div change.
// ROUND-16 FIX (supersedes round-15's "+1" probe, which was test-scene
// special-casing -- see the round-15 CORRECTIONS ledger entry): descent
// needs log2(bricksPerAxis) coordinate-bit hops to uniquely address every
// brick along an axis, NOT esvoMaxScale-brickESVOScale (that's
// brickDepthLevels-1, a different quantity that only coincides with
// log2(bpa) when log2(bpa)==2*brickDepth -- true for this scene's bpa=8/
// brickDepth=3 by coincidence, false in general, e.g. bpa=128 or bpa=32).
// bricksPerAxis is guaranteed a power of two by the octree builder
// (SVORebuild.cpp bricksPerAxis derivation), so findMSB is an EXACT log2
// here (unlike the old bpa-only callers' findMSB(bpa) this function's
// header already documents replacing -- that concern was about a
// non-power-of-two bpa from a ceiling division elsewhere; the config's
// bricksPerAxis field itself is the already-rounded power-of-two value).
int farFieldDescentDepth(int bricksPerAxis) {
    return max(findMSB(bricksPerAxis), 0);
}

// ============================================================================
// DEEP-FIELD MIP POLICY — the footprint->level function (deep-field-mip-
// policy design doc, "the governing statement": ONE deterministic function
// maps a ray's footprint to a level of the recursive hierarchy; every
// backend consults mip data by this one function).
// ============================================================================
// mipPolicyLevel: regime-2 (MIP HIT) level selection, generalized from the
// brick-rung special case ESVOTraversal.glsl's descendToNodeOrdinal shipped
// with (D=612 parity gate) — SAME arithmetic, now the canonical named
// function instead of an inline per-caller loop body, so DDA/RT/ESVO all
// consult ONE definition (no drift between twins).
//
// SPACE/UNITS (documented once, here, per the design doc's ask): both
// operands are WORLD-space distances. `footprint` is the ray's projected
// cone width at the sample point (worldDist*raySizeCoef+raySizeBias — the
// SAME quantity the ordinary ESVO screen-space LOD gate uses,
// SceneBindings.glsl's "tv_max*raySizeCoef+raySizeBias >= scale_exp2").
// `leafWorldSize` is the WORLD size of the finest level in the ladder this
// call is walking (a brick for the octree-node ladder above brick level; a
// sub-brick voxel for the ladder BELOW brick level, once that tier is
// walked by a caller — this function doesn't care which, it only compares
// sizes). Level L's world size is leafWorldSize * 2^L (standard octree
// scale-doubling, one hop per level, level 0 == the leaf itself). The
// policy is "finest level the footprint still covers": return the LARGEST L
// whose node size (leafWorldSize*2^L) is still <= footprint — the node the
// ray's cone genuinely spans. This is ESVO's own semantics: its gate fires
// at the first node the footprint COVERS (footprint >= scale_exp2).
//
// ⚠ BATCH-32 CORRECTION (design ruling). The original returned the first
// level STRICTLY LARGER than the footprint (footprint < size(L)) — ONE LEVEL
// COARSER whenever the footprint falls between rungs. Measured divergence
// from ESVO's rule: 68% of 3200 pairs (batch-31 validator) / 74% of 4000
// (controller), and NEVER finer. Sampling a blurrier mip than the reference
// caps recoverable coverage — the prime suspect for batch-31's census moving
// only 1-2%. The old header also claimed "the same criterion, not an analog";
// that was false in ~70% of cases. Rule and claim both corrected here.
// raySizeCoef<=0 disables LOD entirely (full-detail marching) — returns 0,
// same convention every other pc.raySizeCoef>0.0 gate in this codebase uses.
// maxLevel caps the search (callers pass their own hierarchy depth so this
// never walks past the root).
int mipPolicyLevel(float footprint, float leafWorldSize, int maxLevel) {
    if (footprint <= 0.0 || leafWorldSize <= 0.0) return 0;
    // Walk coarse->fine; the first level the footprint still COVERS is the
    // finest one the cone genuinely spans. Falls through to 0 (the leaf) when
    // the footprint is smaller than a leaf — regime-1 territory, LOD off.
    for (int level = maxLevel; level > 0; --level) {
        if (footprint >= leafWorldSize * float(1u << uint(level))) return level;
    }
    return 0;
}

// ============================================================================
// MATERIAL DATA (matches C++ Material struct)
// ============================================================================

struct Material {
    vec3 albedo;
    float metalness;
    float roughness;
    float emissive;
    vec2 padding;
};

#endif // SVO_TYPES_GLSL
