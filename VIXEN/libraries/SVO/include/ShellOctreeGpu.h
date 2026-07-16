#pragma once

// ShellOctreeGpu — SP2 Task 5a.
//
// Convert one or more ShellOctrees (the sparse surface-shell ESVOs built by
// BuildShellOctree, ShellOctree.h) into PLAIN CPU byte buffers in the EXACT
// format the ray-march shader consumes, ready for a later Vulkan upload node
// (Task 5b). There is NO Vulkan here — pure CPU serialization, headless-testable.
//
// ===========================================================================
// THE BYTE FORMAT (reproduced from the canonical serializer + shader)
// ===========================================================================
// This mirrors VoxelSceneCacher::BuildOctree()
// (libraries/CashSystem/src/VoxelSceneCacher.cpp) and is consumed by
// shaders/VoxelRayMarch.comp. Citations are file:line in this engine tree.
//
//   nodes    : the octree's root->childDescriptors copied verbatim.
//              stride = sizeof(ChildDescriptor) == 8 bytes (SVOTypes.h:117).
//              VoxelSceneCacher.cpp:557-560.
//
//   bricks   : 512 * uint32_t per brick == 2048 bytes/brick. One brick per
//              root->brickViews entry, in brickViews order. Per brick, voxels
//              are emitted in linear order z*64 + y*8 + x (x innermost) — the
//              cacher's nested bz/by/bx push order (VoxelSceneCacher.cpp:575-593)
//              is exactly the index the shader reads:
//                  brickData[brickIndex*512 + (z*64 + y*8 + x)]
//              (VoxelRayMarch.comp:193-194). Each word is the voxel's Material
//              component value, or 0 for an empty voxel slot.
//
//              NOTE — re-deriving brick bytes WITHOUT a dense grid:
//              the cacher re-queries its cached VoxelGrid by world position. A
//              ShellOctree has NO grid; its voxels live in `world` (a
//              GaiaVoxelWorld). We therefore query the world by world position:
//                  for each brick: gridOrigin = view.getLocalGridOrigin();
//                  for z,y,x: e = world.getEntityByWorldSpace(gridOrigin+offset);
//                             word = e ? world.getComponentValue<Material>(e) : 0
//              The shell is built with worldMin = (0,0,0) (ShellOctree.h:92), so
//              the brick LOCAL grid origin equals the WORLD grid coord, exactly
//              as in the cacher (which also builds at worldMin = 0). Morton keys
//              floor onto the integer grid (GaiaVoxelWorld.cpp:389), and shell
//              voxels are created at integer positions (ShellOctree.h:77), so the
//              integer-coord lookup is exact.
//
//   materials: GPUMaterial palette, stride 32 bytes (VoxelSceneCacher.h:88).
//              We reproduce the cacher's default 64-entry palette
//              (VoxelSceneCacher.cpp:414-451). materialsSize on the GPU is
//              materials.size()*sizeof(GPUMaterial) (VoxelSceneCacher.cpp:740).
//
//   config   : OctreeConfig, the 256-byte std140 UBO (VoxelSceneCacher.h:100-133,
//              shader VoxelRayMarch.comp:46-62). Fields filled per shell from the
//              octree's maxLevels / brickDepth / bounds
//              (VoxelSceneCacher.cpp:601-636).
//
// ===========================================================================
// PER-OCTREE BASE OFFSETS — THE CONTRACT (unbounded-count concatenation)
// ===========================================================================
// Concatenate() packs N octrees into ONE `nodes` buffer and ONE `bricks`
// buffer (verbatim append, in input order) and records, for octree k:
//   - configs[k].nodeArrayBase  = element offset of octree k's first node
//   - configs[k].brickArrayBase = element offset of octree k's first brick
// so the shader, when it selects octree k for an instance, indexes
//   childDescriptors[nodeArrayBase + localNodeIdx]
//   brickData[(brickArrayBase + localBrickIdx)*512 + voxelIdx].
// These two int32 bases live INSIDE OctreeConfig, in the named fields
// nodeArrayBase / brickArrayBase that were added to the struct tail. The tail
// is uploaded but was unused by the shader, so the struct stays byte-identical
// (exactly 432 B) and the existing shader UBO layout is unchanged.

#include "ShellOctree.h"      // ShellOctree, BuildShellOctree
#include "SdfBake.h"          // SdfBodyOctree, BakeRecipeToSdfWorld, BuildSdfBodyOctree
#include "SVOTypes.h"         // ChildDescriptor
#include "SVOBuilder.h"       // Octree / OctreeBlock
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"  // Material, Density
#include "VoxelChannelFormat.h"  // ChannelDesc, kMaxChannels, SemanticId, FieldKind (Inc3 M1)
#include "TierRef.h"          // TierRef (Tiered-ESVO Inc2 M1 Task 1)

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>  // glm::scale / glm::translate

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Vixen::SVO {

// ===========================================================================
// GPU structs (re-declared to match the cacher / shader byte layout exactly)
// ===========================================================================

/**
 * GPU material palette entry. MUST match CashSystem::GPUMaterial and the
 * shader's Material struct: 32 bytes (VoxelSceneCacher.h:81-88).
 */
struct GPUMaterial {
    float albedo[3];   // 12 bytes
    float roughness;   // 4 bytes
    float metallic;    // 4 bytes
    float emissive;    // 4 bytes
    float padding[2];  // 8 bytes
};
static_assert(sizeof(GPUMaterial) == 32, "GPUMaterial must be 32 bytes (matches the shader palette stride)");

/**
 * Octree configuration record (one per octree in the runtime-sized SSBO at binding 5).
 * `sizeof(OctreeConfig)` is the ARRAY STRIDE the GPU reads `configs[i]` with — it MUST
 * equal 432 bytes or indices >0 read garbage (see static_assert + the stride trap below).
 *
 * ===================== std140 UBO-ARRAY-STRIDE TRAP (fixed 2026-06-20) ============
 * The shader's element is laid out by std140, where an ARRAY OF SCALARS rounds each
 * element up to 16 bytes (vec4 alignment). The trailing `float _padding4_tail[14]`
 * therefore occupies 14*16 = 224 bytes (NOT 14*4 = 56), so the shader's whole
 * OctreeConfig element strides at **432 bytes** — verified from the compiled SPIR-V:
 *   OpDecorate %_arr_OctreeConfig_uint_3 ArrayStride 432
 *   OpDecorate %_arr_float_uint_14       ArrayStride 16
 * (The shader source's "14 * 4 = 56 B" comment is wrong — it ignores std140 scalar-
 * array padding.) BodyOctreeSceneNode uploads `std::array<OctreeConfig,3>` at THIS
 * struct's sizeof stride, so the C++ struct MUST also be 432 bytes: then configs[0]
 * (byte 0), configs[1] (byte 432), configs[2] (byte 864) all align with the shader.
 * A tighter sizeof (the old 256/260) put configs[1]/[2] before the shader's read
 * point → garbage localToWorld/worldToLocal → the AABB cull failed → every body with
 * octreeIndex>0 silently drew NOTHING (only octree 0, at base offset 0, rendered).
 *
 * The shader also reads the generated descriptor/tail fields at bytes 200..395:
 * format/channel layout, pool prefixes, residency, and conservative trace bounds.
 * Bytes 396..431 remain reserved. The `worldGridSize` convenience field that used
 * to trail this struct was removed — it was write-only here; the live readers use
 * the SEPARATE CashSystem::OctreeConfig. Do NOT change a generated offset without
 * regenerating both artifacts and extending the SDI parity test; keep the struct
 * exactly 432 bytes so the array stride matches the shader.
 */
// OctreeConfig is generated from the canonical [GpuStruct] (Phase C). Its 432 B
// std430 layout + full sizeof/offsetof static_assert battery live in the generated
// header (included transitively via VoxelChannelFormat.h). ChannelDesc is aliased
// there too. All consumers keep the Vixen::SVO::OctreeConfig spelling.
using Vixen::Gpu::OctreeConfig;

// ============================================================================
// Inc2 Stored-SDF descriptor helpers (thin shims over named fields, Inc3 M1)
// ============================================================================
// These preserve the Inc2 public API so existing call sites in ConcatenateSdf,
// SerializeSdf, and test_soa_sdf_serialize.cpp continue to compile unchanged.

/// Format IDs for OctreeConfig::formatId (byte 200).
static constexpr uint32_t FORMAT_BINARY = 0u;  ///< binary ESVO path (default)
static constexpr uint32_t STORED_SDF    = 1u;  ///< Inc2 trilinear iso-surface path

// ============================================================================
// brickGridLookup sentinel encoding
// ============================================================================
// A cell of the dense grid→brick lookup table (brickGridLookup) holds either an
// allocated brickView index (0 .. brickCount-1) OR the single "no brick" sentinel
// kBrickUnalloc (0xFFFFFFFF). The GPU sampler returns a large positive distance for
// such a cell — legitimate defensive handling for genuinely out-of-grid / far-exterior
// samples. (Brick selection is OCCUPANCY-based — every active brick, interior or shell,
// is allocated — so a surface stencil never reaches into an unallocated interior brick;
// there is no sign-aware interior sentinel to distinguish.)
static constexpr uint32_t kBrickUnalloc = 0xFFFFFFFFu;  ///< no allocated brick
/// True if a brickGridLookup value is the "no brick" sentinel.
inline bool isBrickUnallocated(uint32_t v) {
    return v == kBrickUnalloc;
}

/// Read the formatId from the OctreeConfig tail (byte 200, now a named field).
inline uint32_t formatIdOf(const OctreeConfig& c) {
    return c.formatId;
}
/// Write formatId into the OctreeConfig tail.
inline void setFormatId(OctreeConfig& c, uint32_t id) {
    c.formatId = id;
}
/// Read the poolBrickBase (formerly sdfBrickArrayBase) from OctreeConfig (byte 208).
inline uint32_t sdfBrickArrayBaseOf(const OctreeConfig& c) {
    return c.poolBrickBase;
}
/// Write poolBrickBase (formerly sdfBrickArrayBase) into the OctreeConfig tail.
inline void setSdfBrickArrayBase(OctreeConfig& c, uint32_t base) {
    c.poolBrickBase = base;
}
/// Write bricksPerAxisSdf into the OctreeConfig tail (byte 204).
inline void setDescriptorBricksPerAxis(OctreeConfig& c, uint32_t bpa) {
    c.bricksPerAxisSdf = bpa;
}
/// Read the mipPoolBase (Sparse-Mip ESVO LOD Inc1 M1 Task 3) from OctreeConfig (byte 352).
inline uint32_t mipPoolBaseOf(const OctreeConfig& c) {
    return c.mipPoolBase;
}
/// Write mipPoolBase into the OctreeConfig tail.
inline void setMipPoolBase(OctreeConfig& c, uint32_t base) {
    c.mipPoolBase = base;
}
/// Read brickResident (Sparse-Mip ESVO LOD Inc1 M3 Task 7) from OctreeConfig (byte 356).
inline bool brickResidentOf(const OctreeConfig& c) {
    return c.brickResident != 0u;
}
/// Write brickResident into the OctreeConfig tail — per-tree binary residency
/// (§0 scope): 0 = bricksBuffer_ region allocated but not populated (mip-only),
/// 1 = fully uploaded. The shader's leaf-hit existence check reads this field
/// directly rather than hasBrick()/contourPointer, which stays valid regardless
/// of residency (M2 Task 4) and so cannot itself signal "not yet uploaded."
inline void setBrickResident(OctreeConfig& c, bool resident) {
    c.brickResident = resident ? 1u : 0u;
}
/// Read the tierRefTableBase (Tiered-ESVO Inc2 M1 Task 3) from OctreeConfig.
/// Element offset (in TierRef units) of this octree's own slice of the
/// concatenated ConcatenatedOctrees::tierRefTable — mirrors mipPoolBase's
/// convention exactly (0 == "no tier-ref entries before this offset", not
/// necessarily "this tree has no tier-ref entries" — the per-octree COUNT,
/// see ConcatenatedOctrees::tierRefCounts, is what a future traversal-restart
/// milestone would need to bound the slice, analogous to how mipPoolBase
/// alone does not bound a slice either — both rely on the shader knowing
/// where the NEXT tree's base starts, or a stored per-tree count).
inline uint32_t tierRefTableBaseOf(const OctreeConfig& c) {
    return c.tierRefTableBase;
}
/// Write tierRefTableBase into the OctreeConfig tail.
inline void setTierRefTableBase(OctreeConfig& c, uint32_t base) {
    c.tierRefTableBase = base;
}
/// Read the uint32-element offset of this octree's dense brick lookup table.
inline uint32_t brickLookupBaseOf(const OctreeConfig& c) {
    return c.brickLookupBase;
}
/// Write the uint32-element offset of this octree's dense brick lookup table.
inline void setBrickLookupBase(OctreeConfig& c, uint32_t base) {
    c.brickLookupBase = base;
}

// ===========================================================================
// Serialized output
// ===========================================================================

/**
 * One ShellOctree serialized to CPU byte buffers, ready to upload.
 */
struct SerializedOctree {
    // Voxel order within a brick is z*64 + y*8 + x; one word (uint32 material id)
    // per voxel; 512 voxels per 8^3 brick => 2048 bytes/brick.
    static constexpr uint32_t kBrickStrideBytes = 512u * sizeof(uint32_t);  // 2048
    static constexpr uint32_t kVoxelsPerBrick = 512u;

    std::vector<uint8_t> nodes;           // ChildDescriptor array (stride 8)
    std::vector<uint8_t> bricks;          // 512*uint32 per brick (stride 2048)
    std::vector<uint8_t> materials;       // GPUMaterial palette (stride 32)
    OctreeConfig config{};

    // Inc3 M2 — Generic multi-channel SoA pool (emitSdf=true only):
    // Layout: channelPool[brick * brickStrideFloats + channelBaseFloats[c] + comp*512 + voxel]
    // Channels in canonical order: SDF(1 float), Color(3 floats), Roughness(1 float), ...
    std::vector<uint8_t> channelPool;     // brickCount * brickStrideFloats * sizeof(float)
    uint32_t channelCount     = 0;        // number of live channels
    uint32_t brickStrideFloats = 0;       // total floats per brick (sum over all channels)
    ChannelDesc channels[kMaxChannels]{};  // per-channel descriptors (channelBaseFloats packed inside)

    std::vector<uint8_t> brickGridLookup; // uint32[bricksPerAxis^3]: grid-coord→brickIndex,
                                          // 0xFFFFFFFF = unallocated brick.

    // Sparse-Mip ESVO LOD Inc1 M1 Task 3 — per-node, per-channel filtered mip
    // pool (bottom-up bake fill, MipBake.h::BakeMipPool). SoA layout mirrors
    // channelPool's read-by-semantic convention but at NODE granularity:
    //   mipPool[nodeIdx * channelCount + channelIdx] -> one packed MipSample
    //   (2 floats: value, coverage), stride sizeof(MipSample) bytes.
    // Empty (zero-size) unless the caller populates it via SetMipPool below —
    // M1's bake/serialize is opt-in per Task's own scope (existing SerializeSdf
    // callers are unaffected until they choose to call it).
    std::vector<uint8_t> mipPool;

    // Tiered-ESVO Inc2 M1 Task 2 — this octree's own tier-crossing edges
    // (one TierRef per registered cross-tier leaf; §3.2). Unlike mipPool/
    // channelPool, this is NOT baked from the tree's existing geometry — it
    // is explicitly registered by a caller (M2's construction path adds
    // entries alongside marking a leaf's ChildDescriptor::farBit=1; this
    // milestone only provides the storage + concatenation bookkeeping).
    // Empty for every tree that has no tier-crossing leaves (today: all of
    // them — M2 has not shipped yet).
    std::vector<TierRef> tierRefs;

    uint32_t nodeCount = 0;   // == nodes.size() / sizeof(ChildDescriptor)
    uint32_t brickCount = 0;  // == bricks.size() / kBrickStrideBytes

    // Returns the channelBaseFloats for the given semantic (scans channels[]).
    // Returns 0xFFFFFFFFu if the semantic is not present.
    uint32_t channelBaseFloats(SemanticId sem) const {
        for (uint32_t i = 0; i < channelCount; ++i) {
            if (channels[i].semanticId == static_cast<uint32_t>(sem))
                return channels[i].channelBaseFloats;
        }
        return 0xFFFFFFFFu;
    }

    // Read a single float from the pool:
    //   brick  — brick index (0..brickCount-1)
    //   voxel  — voxel slot within brick (0..511, z*64+y*8+x order)
    //   comp   — component index within the channel's elemCount (0 for scalars)
    // Returns 0.0f if the semantic is not present.
    float readPoolVoxel(SemanticId sem, uint32_t brick, uint32_t voxel, uint32_t comp) const {
        const uint32_t base = channelBaseFloats(sem);
        if (base == 0xFFFFFFFFu) return 0.0f;
        const uint32_t floatIdx = brick * brickStrideFloats + base + comp * kVoxelsPerBrick + voxel;
        const size_t   byteOff  = static_cast<size_t>(floatIdx) * sizeof(float);
        if (byteOff + sizeof(float) > channelPool.size()) return 0.0f;
        float val = 0.0f;
        std::memcpy(&val, channelPool.data() + byteOff, sizeof(float));
        return val;
    }
};

/**
 * N ShellOctrees concatenated into shared node/brick buffers, plus per-octree
 * configs (carrying nodeArrayBase / brickArrayBase) and count tables.
 * Count is unbounded — the only limit is the memory budget applied by the baker.
 */
struct ConcatenatedOctrees {
    std::vector<uint8_t> nodes;      // octree0 nodes ++ octree1 nodes ++ ...
    std::vector<uint8_t> bricks;     // octree0 bricks ++ octree1 bricks ++ ...
    std::vector<uint8_t> materials;  // shared palette (identical across shells)

    // Inc3 M2 — Generic multi-channel SoA pool (populated by ConcatenateSdf; empty otherwise):
    std::vector<uint8_t> channelPool;     // octree0 pool ++ octree1 pool ++ ...
    std::vector<uint8_t> brickGridLookup; // octrees concatenated; each sub-table is
                                          // uint32[bpa^3] where bpa = bricksPerAxis.
                                          // Per-octree size varies; M3 uploads them separately.

    // Sparse-Mip ESVO LOD Inc1 M1 Task 3 — concatenated per-octree mip pools
    // (empty unless the caller populated each SerializedOctree::mipPool before
    // concatenation — see ConcatenateSdf).
    std::vector<uint8_t> mipPool;

    // Tiered-ESVO Inc2 M1 Task 2 — concatenated per-octree tier-crossing
    // edges: octree0's tierRefs ++ octree1's tierRefs ++ ... , in the exact
    // order Concatenate/ConcatenateSdf iterate octrees, matching every other
    // pool's append convention. A tier-crossing leaf's ChildDescriptor::
    // contourPointer (§3.1, not touched by this milestone) indexes into this
    // tree's OWN slice, offset by OctreeConfig::tierRefTableBase (Task 3) —
    // exactly the poolBrickBase/mipPoolBase pattern.
    std::vector<TierRef> tierRefTable;

    std::vector<OctreeConfig> configs;    // per-octree config (size == count)
    std::vector<uint32_t>     nodeCounts; // per-octree node count
    std::vector<uint32_t>     brickCounts;// per-octree brick count
    std::vector<uint32_t>     tierRefCounts; // per-octree tier-ref-table entry count
    uint32_t count = 0;  // number of octrees packed (== configs.size())
};

/**
 * Per-instance GPU record (std430-friendly, 64 bytes). The host-side BodyInstance
 * lives in the outer repo (vixen/render/scene_instances.h) and is intentionally
 * NOT included here — Task 5b / main bridges the two. octreeIndex selects which
 * concatenated octree (and thus which OctreeConfig) this instance draws.
 */
struct BodyInstanceGpu {
    float worldPos[3];       // 0   : body centre (world space)
    float renderScale;       // 12  : Stored: grid scale; Procedural: unused
    float color[3];          // 16  : per-instance tint
    uint32_t octreeIndex;    // 28  : Stored: index into configs[]; Procedural: unused
    uint32_t providerKind;   // 32  : 0 = Stored/ESVO, 1 = Procedural
    uint32_t recipeId;       // 36  : Procedural recipe id (0 = sphere, 1 = displaced sphere)
    float recipeParams[6];   // 40..63 : params.xyz = (radius, displaceAmp, displaceFreq); 3 spare
};
// 64-byte std430 record. recipeParams[6] is valid because binding 10 is a std430
// SSBO (float[] stride = 4). providerKind defaults to 0 (Stored) under value-init,
// so a zeroed/legacy record renders via the unchanged ESVO path.
static_assert(sizeof(BodyInstanceGpu) == 64, "BodyInstanceGpu must be 64 bytes (std430 record)");
static_assert(offsetof(BodyInstanceGpu, providerKind) == 32, "providerKind @32");
static_assert(offsetof(BodyInstanceGpu, recipeId)     == 36, "recipeId @36");
static_assert(offsetof(BodyInstanceGpu, recipeParams) == 40, "recipeParams @40");

// ===========================================================================
// Implementation helpers
// ===========================================================================

namespace detail {

// Reproduce the cacher's default 64-entry material palette
// (VoxelSceneCacher.cpp:414-451). We only need indices 0..20 to be meaningful;
// the rest default to mid-grey, exactly as the cacher fills them.
inline std::vector<GPUMaterial> BuildDefaultMaterialPalette() {
    std::vector<GPUMaterial> m(64, GPUMaterial{{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}});
    m[0]  = {{0.8f, 0.8f, 0.8f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};  // default white diffuse
    m[1]  = {{0.75f, 0.1f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // red
    m[2]  = {{0.1f, 0.75f, 0.1f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // green
    m[3]  = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};  // white
    m[4]  = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[5]  = {{0.9f, 0.9f, 0.9f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[6]  = {{0.7f, 0.7f, 0.7f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[7]  = {{0.3f, 0.3f, 0.3f}, 0.9f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[8]  = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[9]  = {{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    m[10] = {{0.8f, 0.7f, 0.5f}, 0.8f, 0.0f, 0.0f, {0.0f, 0.0f}};  // beige
    m[11] = {{0.4f, 0.6f, 0.8f}, 0.7f, 0.0f, 0.0f, {0.0f, 0.0f}};  // light blue
    m[19] = {{1.0f, 0.0f, 1.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}};  // debug magenta
    m[20] = {{1.0f, 1.0f, 0.9f}, 0.0f, 0.0f, 5.0f, {0.0f, 0.0f}};  // emissive light
    return m;
}

}  // namespace detail

// ===========================================================================
// Single-octree serialization
// ===========================================================================

/**
 * Serialize one ShellOctree into CPU byte buffers in the shader's exact format.
 */
inline SerializedOctree Serialize(const ShellOctree& shell) {
    if (!shell.octree || !shell.world) {
        throw std::runtime_error("ShellOctreeGpu::Serialize: shell missing octree or world");
    }
    const Octree* oct = shell.octree->getOctree();
    if (!oct || !oct->root) {
        throw std::runtime_error("ShellOctreeGpu::Serialize: octree has no root (call rebuild)");
    }

    SerializedOctree out;
    Vixen::GaiaVoxel::GaiaVoxelWorld& world = *shell.world;

    // --- nodes: raw ChildDescriptor array (VoxelSceneCacher.cpp:557-560).
    const std::vector<ChildDescriptor>& descriptors = oct->root->childDescriptors;
    out.nodeCount = static_cast<uint32_t>(descriptors.size());
    out.nodes.resize(descriptors.size() * sizeof(ChildDescriptor));
    if (!descriptors.empty()) {
        std::memcpy(out.nodes.data(), descriptors.data(), out.nodes.size());
    }

    // --- bricks: 512 uint32 per brick, voxel order z*64+y*8+x, queried by world
    //     position (VoxelSceneCacher.cpp:570-594; shader index L193-194).
    const std::vector<Vixen::GaiaVoxel::EntityBrickView>& brickViews = oct->root->brickViews;
    const int brickSide = oct->brickSideLength;  // 8
    out.brickCount = static_cast<uint32_t>(brickViews.size());

    std::vector<uint32_t> brickWords;
    brickWords.reserve(brickViews.size() * SerializedOctree::kVoxelsPerBrick);
    for (const Vixen::GaiaVoxel::EntityBrickView& view : brickViews) {
        const glm::ivec3 gridOrigin = view.getLocalGridOrigin();
        for (int bz = 0; bz < brickSide; ++bz) {
            for (int by = 0; by < brickSide; ++by) {
                for (int bx = 0; bx < brickSide; ++bx) {
                    const glm::vec3 worldPos(
                        static_cast<float>(gridOrigin.x + bx),
                        static_cast<float>(gridOrigin.y + by),
                        static_cast<float>(gridOrigin.z + bz));
                    uint32_t materialId = 0u;
                    const auto entity = world.getEntityByWorldSpace(worldPos);
                    if (world.exists(entity)) {
                        const auto mat = world.getComponentValue<Vixen::GaiaVoxel::Material>(entity);
                        materialId = mat.has_value() ? mat.value() : 0u;
                    }
                    brickWords.push_back(materialId);
                }
            }
        }
    }
    out.bricks.resize(brickWords.size() * sizeof(uint32_t));
    if (!brickWords.empty()) {
        std::memcpy(out.bricks.data(), brickWords.data(), out.bricks.size());
    }

    // --- materials: the default palette (VoxelSceneCacher.cpp:414-451).
    const std::vector<GPUMaterial> palette = detail::BuildDefaultMaterialPalette();
    out.materials.resize(palette.size() * sizeof(GPUMaterial));
    std::memcpy(out.materials.data(), palette.data(), out.materials.size());

    // --- config: OctreeConfig per shell (VoxelSceneCacher.cpp:601-636).
    OctreeConfig& c = out.config;
    std::memset(&c, 0, sizeof(OctreeConfig));  // zero padding (Release-safe, cacher L603)

    const int maxLevels = oct->maxLevels;
    const int brickDepth = brickSide > 0
        ? static_cast<int>(std::lround(std::log2(static_cast<double>(brickSide))))
        : 3;

    c.esvoMaxScale = LaineKarrasOctree::ESVO_MAX_SCALE;
    c.userMaxLevels = maxLevels;
    c.brickDepthLevels = brickDepth;
    c.brickSize = 1 << brickDepth;

    c.minESVOScale = c.esvoMaxScale - c.userMaxLevels + 1;
    const int brickUserScale = c.userMaxLevels - c.brickDepthLevels;
    c.brickESVOScale = c.esvoMaxScale - (c.userMaxLevels - 1 - brickUserScale);
    c.bricksPerAxis = oct->bricksPerAxis;

    // World-grid extent the base octree's localToWorld scales by (matches cacher
    // WORLD_GRID_SIZE). Used below for the localToWorld/worldToLocal matrices; it is
    // NOT stored on OctreeConfig (the former trailing worldGridSize field was removed
    // to keep sizeof(OctreeConfig)==256 — see the struct's stride-trap note).
    constexpr float kWorldGridSize = 10.0f;

    c.gridMinX = oct->worldMin.x;
    c.gridMinY = oct->worldMin.y;
    c.gridMinZ = oct->worldMin.z;
    c.gridMaxX = oct->worldMax.x;
    c.gridMaxY = oct->worldMax.y;
    c.gridMaxZ = oct->worldMax.z;

    const glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(kWorldGridSize));
    const glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
    c.localToWorld = translateMat * scaleMat;
    c.worldToLocal = glm::inverse(c.localToWorld);

    // Single-octree: bases are zero (it is the only octree in its own buffers).
    c.nodeArrayBase = 0;
    c.brickArrayBase = 0;

    return out;
}

// ===========================================================================
// Tier-crossing leaf construction (Tiered-ESVO Inc2 M2 Task 4)
// ===========================================================================
//
// MarkLeafAsTierCrossing is the ONE, explicit opt-in a caller uses to turn an
// already-serialized leaf into a tier-crossing reference (farBit=1) instead of
// an ordinary brick leaf. This is deliberately a SEPARATE, additive path next
// to Serialize()/SerializeSdf() — every existing call path is completely
// unaffected (it still emits farBit=0 leaves exactly as before,
// SVORebuild.cpp:439,512); nothing here changes default tree-building
// behavior for anyone who does not call this function.
//
// Shape mirrors this file's own established "opt a tree into a special
// behavior via one small explicit call" convention (setBodyOctree,
// setSignedDistanceField, LaineKarrasOctree.h) — applied post-serialization,
// at leaf granularity, since a tier-crossing edge is a property of one
// specific leaf, not the whole tree.
//
// Usage: call Serialize()/SerializeSdf() as normal to get a SerializedOctree,
// then call this once per leaf you want to mark, identifying the leaf by the
// (parent descriptor index, octant) pair — the same addressing scheme
// existing tests already use to locate a specific leaf
// (test_mip_sample_bake.cpp's `leafToBrickView` key convention:
// `(parentDescriptorIndex << 3) | octant`). Registers a new TierRef entry in
// out.tierRefs and points the leaf's ChildDescriptor at it.
//
// childRootScaleHint (0-22, §3.1) is the child tree's own root ESVO scale —
// an intrinsic property of the CHILD octree's construction (its
// maxLevels/userToESVOScale mapping, LaineKarrasOctree.h:409), not something
// derivable from TierRef::childScale (a parent-local linear scale factor,
// a different quantity entirely — see TierRef.h §3.3). The caller must
// supply it explicitly, exactly as the plan's own Task 4 text specifies
// (`setTierCrossing(tierRefIndex, childRootScale)`).

// Tiered-ESVO Inc3 M5 Task 9: the marked leaf's OWN cell center, in the
// PARENT's local [1,2) frame, for a leaf that is a DIRECT CHILD OF THE ROOT
// (parentDescriptorIndex==0 — every tier-crossing demo fixture to date marks
// only root-level leaves). TierRef.h's own header requires childOriginLocal
// to be "the point that maps to the child's [1,2) frame center" (the SumTail
// composition's inverse, remapRayIntoChildFrame); for a root-level leaf that
// point is the LEAF'S OWN CENTER, at (1.25 or 1.75) per axis depending on
// the octant's bit (bit0=x,bit1=y,bit2=z; SVOTypes.h's mirroredToLocalOctant
// convention) — NEVER the constant (1.5,1.5,1.5), which is the ROOT CUBE'S
// shared CORNER, common to all 8 octants, not any one octant's center. Using
// that corner as childOriginLocal was the M5 root-caused defect: it makes
// the octant corner nearest (1.5,1.5,1.5) a SCALE-INVARIANT FIXED POINT (the
// remap maps it to 1.5 regardless of childScale) while the leaf's opposite
// corner races away proportionally to 1/childScale — a corner-anchored,
// non-concentric "wedge" collapse instead of a shrink centered on the leaf,
// with the far side immediately clipped by the child tree's own [1,2)
// domain boundary (hence the observed near-zero magnification and the
// silhouette saturating almost immediately below childScale==1.0).
inline glm::vec3 RootLeafOctantCenterLocal(int octant) {
    return glm::vec3(
        (octant & 1) ? 1.75f : 1.25f,
        (octant & 2) ? 1.75f : 1.25f,
        (octant & 4) ? 1.75f : 1.25f);
}

inline void MarkLeafAsTierCrossing(SerializedOctree& out,
                                   uint32_t parentDescriptorIndex,
                                   int octant,
                                   const TierRef& tierRef,
                                   uint8_t childRootScaleHint) {
    if (octant < 0 || octant > 7) {
        throw std::runtime_error("ShellOctreeGpu::MarkLeafAsTierCrossing: octant must be 0..7");
    }
    if (childRootScaleHint > 22) {
        throw std::runtime_error("ShellOctreeGpu::MarkLeafAsTierCrossing: childRootScaleHint must be 0..22");
    }
    const size_t descByteOffset = static_cast<size_t>(parentDescriptorIndex) * sizeof(ChildDescriptor);
    if (descByteOffset + sizeof(ChildDescriptor) > out.nodes.size()) {
        throw std::runtime_error("ShellOctreeGpu::MarkLeafAsTierCrossing: parentDescriptorIndex out of range");
    }

    ChildDescriptor parentDesc;
    std::memcpy(&parentDesc, out.nodes.data() + descByteOffset, sizeof(ChildDescriptor));
    if (!parentDesc.hasChild(octant) || !parentDesc.isLeaf(octant)) {
        throw std::runtime_error(
            "ShellOctreeGpu::MarkLeafAsTierCrossing: (parentDescriptorIndex, octant) is not an existing leaf child");
    }

    // Locate the CHILD descriptor this leaf occupies — the same addressing
    // SVORebuild.cpp's/SVOTraversal.cpp's leaf-hit path uses (leafDescIdx =
    // childPointer + totalInternalChildren [across the WHOLE valid set] +
    // leafChildrenBeforeThisOctant).
    const uint32_t totalInternal = static_cast<uint32_t>(std::popcount(
        static_cast<uint8_t>(parentDesc.validMask & ~parentDesc.leafMask)));
    uint32_t leafBefore = 0;
    for (int i = 0; i < octant; ++i) {
        if (parentDesc.hasChild(i) && parentDesc.isLeaf(i)) ++leafBefore;
    }
    const uint32_t leafDescIdx = parentDesc.childPointer + totalInternal + leafBefore;

    const size_t leafByteOffset = static_cast<size_t>(leafDescIdx) * sizeof(ChildDescriptor);
    if (leafByteOffset + sizeof(ChildDescriptor) > out.nodes.size()) {
        throw std::runtime_error("ShellOctreeGpu::MarkLeafAsTierCrossing: resolved leaf descriptor index out of range");
    }

    ChildDescriptor leafDesc;
    std::memcpy(&leafDesc, out.nodes.data() + leafByteOffset, sizeof(ChildDescriptor));

    const uint32_t tierRefIndex = static_cast<uint32_t>(out.tierRefs.size());
    leafDesc.setTierCrossing(tierRefIndex, childRootScaleHint);

    std::memcpy(out.nodes.data() + leafByteOffset, &leafDesc, sizeof(ChildDescriptor));
    out.tierRefs.push_back(tierRef);
}

// ===========================================================================
// SoA-SDF Serialize (Inc2 M2) — SdfBodyOctree → SerializedOctree with
//   channelPool + brickGridLookup + layout descriptor in OctreeConfig named fields.
// ===========================================================================

/**
 * Serialize one SdfBodyOctree into CPU byte buffers (material bricks + generic
 * multi-channel SoA pool + dense grid-lookup table + OctreeConfig with
 * Stored-SDF descriptor).
 *
 * Voxel order in the channelPool: identical to the material bricks loop —
 *   channelPool[brick * brickStrideFloats + channelBase + voxel] where voxel
 *   = z*64+y*8+x. Same z-outer, y-middle, x-inner order as the binary Serialize above.
 *
 * Dense grid→brick lookup (brickGridLookup):
 *   A flat uint32[bpa^3] table (bpa = oct->bricksPerAxis, read from Octree).
 *   GPU flat index:  brickX + brickY*bpa + brickZ*bpa^2
 *   Value: brickView index (0..brickCount-1), or 0xFFFFFFFF = unallocated.
 *   Built by inverting brickGridToBrickView (SVOBuilder.h:59-61):
 *     packed key = brickX | (brickY<<10) | (brickZ<<20)
 *
 * Layout descriptor (OctreeConfig named tail fields, sizeof = 432):
 *   byte 200 (formatId,       uint32): STORED_SDF (1u)
 *   byte 204 (bricksPerAxis,  uint32): bricks per axis of the octree
 *   byte 208 (poolBrickBase,  uint32): element offset (floats) of this octree's
 *                                      first brick in the shared channelPool
 *                                      (0 for single-octree; ConcatenateSdf
 *                                      updates per-octree)
 * M4's GLSL OctreeConfig must match these exact byte offsets.
 */
inline SerializedOctree SerializeSdf(const SdfBodyOctree& body) {
    if (!body.octree || !body.world) {
        throw std::runtime_error("ShellOctreeGpu::SerializeSdf: body missing octree or world");
    }
    const Octree* oct = body.octree->getOctree();
    if (!oct || !oct->root) {
        throw std::runtime_error("ShellOctreeGpu::SerializeSdf: octree has no root (call rebuild)");
    }

    SerializedOctree out;
    Vixen::GaiaVoxel::GaiaVoxelWorld& world = *body.world;

    // --- nodes: raw ChildDescriptor array (same as binary path)
    const std::vector<ChildDescriptor>& descriptors = oct->root->childDescriptors;
    out.nodeCount = static_cast<uint32_t>(descriptors.size());
    out.nodes.resize(descriptors.size() * sizeof(ChildDescriptor));
    if (!descriptors.empty()) {
        std::memcpy(out.nodes.data(), descriptors.data(), out.nodes.size());
    }

    // --- Channel table: canonical order SDF(1) → Color(3) → Roughness(1) → Emission(1).
    //     This order is the SINGLE SOURCE OF TRUTH for the pool layout; the test pins it.
    //     Emission (Sampled Lighting Inc3 M3) is SCALAR intensity, mean-filtered up the
    //     mip pyramid like Color/Roughness (FK_NONE) — not the SDF's min-magnitude rule.
    struct ChannelSpec {
        SemanticId  sem;
        uint32_t    elemCount;
        FieldKind   fieldKind;
    };
    const ChannelSpec kChannelSpecs[] = {
        { SEM_SDF,       1u, FK_DISTANCE },
        { SEM_COLOR,     3u, FK_NONE     },
        { SEM_ROUGHNESS, 1u, FK_NONE     },
        { SEM_EMISSION,  1u, FK_NONE     },
    };
    constexpr uint32_t kNumChannels =
        static_cast<uint32_t>(sizeof(kChannelSpecs) / sizeof(kChannelSpecs[0]));

    // Build out.channels[] and compute brickStrideFloats.
    uint32_t runningBase = 0u;
    for (uint32_t ci = 0; ci < kNumChannels; ++ci) {
        out.channels[ci].semanticId        = static_cast<uint32_t>(kChannelSpecs[ci].sem);
        out.channels[ci].elemCount         = kChannelSpecs[ci].elemCount;
        out.channels[ci].channelBaseFloats = runningBase;
        out.channels[ci].fieldKind         = static_cast<uint32_t>(kChannelSpecs[ci].fieldKind);
        runningBase += kChannelSpecs[ci].elemCount * SerializedOctree::kVoxelsPerBrick;
    }
    out.channelCount      = kNumChannels;
    out.brickStrideFloats = runningBase;  // total floats per brick

    // --- bricks (material) + generic SoA pool: iterate brickViews in order.
    const std::vector<Vixen::GaiaVoxel::EntityBrickView>& brickViews = oct->root->brickViews;
    const int brickSide = oct->brickSideLength;  // 8
    out.brickCount = static_cast<uint32_t>(brickViews.size());

    std::vector<uint32_t> brickWords;
    brickWords.reserve(out.brickCount * SerializedOctree::kVoxelsPerBrick);

    // channelPool: brickCount * brickStrideFloats floats, zero-initialised.
    const size_t poolFloats = static_cast<size_t>(out.brickCount) * out.brickStrideFloats;
    std::vector<float> pool(poolFloats, 0.0f);

    for (uint32_t bi = 0; bi < out.brickCount; ++bi) {
        const Vixen::GaiaVoxel::EntityBrickView& view = brickViews[bi];
        const glm::ivec3 gridOrigin = view.getLocalGridOrigin();
        uint32_t voxelSlot = 0u;
        for (int bz = 0; bz < brickSide; ++bz) {
            for (int by = 0; by < brickSide; ++by) {
                for (int bx = 0; bx < brickSide; ++bx, ++voxelSlot) {
                    const glm::vec3 worldPos(
                        static_cast<float>(gridOrigin.x + bx),
                        static_cast<float>(gridOrigin.y + by),
                        static_cast<float>(gridOrigin.z + bz));
                    const auto entity = world.getEntityByWorldSpace(worldPos);
                    const bool alive  = world.exists(entity);

                    // material brick (unchanged)
                    uint32_t materialId = 0u;
                    if (alive) {
                        const auto mat = world.getComponentValue<Vixen::GaiaVoxel::Material>(entity);
                        materialId = mat.has_value() ? mat.value() : 0u;
                    }
                    brickWords.push_back(materialId);

                    // Generic channel pool — write each channel in canonical order.
                    for (uint32_t ci = 0; ci < kNumChannels; ++ci) {
                        const uint32_t base = out.channels[ci].channelBaseFloats;
                        const uint32_t ec   = out.channels[ci].elemCount;
                        SemanticId     sem  = kChannelSpecs[ci].sem;

                        if (!alive) {
                            // Leave zero (default) — no voxel here.
                            continue;
                        }

                        if (sem == SEM_SDF) {
                            const auto den = world.getComponentValue<Vixen::GaiaVoxel::Density>(entity);
                            pool[bi * out.brickStrideFloats + base + voxelSlot] =
                                den.has_value() ? den.value() : 0.0f;
                        } else if (sem == SEM_COLOR) {
                            const auto col = world.getComponentValue<Vixen::GaiaVoxel::Color>(entity);
                            glm::vec3 cv(1.0f);
                            if (col.has_value()) cv = col.value();
                            // Color: 3 components, each occupying a full 512-voxel lane.
                            // comp*512 offsets: comp=0 → base+0..511, comp=1 → base+512..1023, etc.
                            for (uint32_t comp = 0; comp < ec; ++comp) {
                                pool[bi * out.brickStrideFloats + base + comp * kVoxelsPerBrick + voxelSlot] = cv[comp];
                            }
                        } else if (sem == SEM_ROUGHNESS) {
                            const auto rgh = world.getComponentValue<Vixen::GaiaVoxel::Roughness>(entity);
                            pool[bi * out.brickStrideFloats + base + voxelSlot] =
                                rgh.has_value() ? rgh.value() : 0.5f;
                        } else if (sem == SEM_EMISSION) {
                            // Sampled Lighting Inc3 M3: scalar emissive intensity.
                            // Missing default 0.0 (non-emissive) — the byte-identity
                            // escape hatch for scenes that never bake emission.
                            const auto emi = world.getComponentValue<Vixen::GaiaVoxel::EmissionIntensity>(entity);
                            pool[bi * out.brickStrideFloats + base + voxelSlot] =
                                emi.has_value() ? emi.value() : 0.0f;
                        }
                        (void)ec;  // suppress warning for single-elem channels
                    }
                }
            }
        }
    }

    out.bricks.resize(brickWords.size() * sizeof(uint32_t));
    if (!brickWords.empty()) {
        std::memcpy(out.bricks.data(), brickWords.data(), out.bricks.size());
    }
    out.channelPool.resize(poolFloats * sizeof(float));
    if (!pool.empty()) {
        std::memcpy(out.channelPool.data(), pool.data(), out.channelPool.size());
    }

    // --- materials: default palette
    const std::vector<GPUMaterial> palette = detail::BuildDefaultMaterialPalette();
    out.materials.resize(palette.size() * sizeof(GPUMaterial));
    std::memcpy(out.materials.data(), palette.data(), out.materials.size());

    // --- Dense grid→brick lookup: uint32[bpa^3], plus its conservative AABB.
    glm::ivec3 traceBrickMin(oct->bricksPerAxis);
    glm::ivec3 traceBrickMaxExclusive(0);
    bool hasTraceBricks = false;
    {
        const int bpa = oct->bricksPerAxis;
        const uint32_t tableSize = static_cast<uint32_t>(bpa) *
                                   static_cast<uint32_t>(bpa) *
                                   static_cast<uint32_t>(bpa);
        std::vector<uint32_t> lookupTable(tableSize, 0xFFFFFFFFu);
        for (const auto& kv : oct->root->brickGridToBrickView) {
            const uint32_t key = kv.first;
            const uint32_t brickViewIdx = kv.second;
            const uint32_t gx = (key)       & 0x3FFu;
            const uint32_t gy = (key >> 10) & 0x3FFu;
            const uint32_t gz = (key >> 20) & 0x3FFu;
            const uint32_t flatIdx = gx
                                   + gy * static_cast<uint32_t>(bpa)
                                   + gz * static_cast<uint32_t>(bpa) * static_cast<uint32_t>(bpa);
            if (flatIdx < tableSize) {
                lookupTable[flatIdx] = brickViewIdx;
                hasTraceBricks = true;
                const glm::ivec3 brickCoord(static_cast<int>(gx), static_cast<int>(gy), static_cast<int>(gz));
                traceBrickMin = glm::min(traceBrickMin, brickCoord);
                traceBrickMaxExclusive = glm::max(traceBrickMaxExclusive, brickCoord + glm::ivec3(1));
            }
        }

        out.brickGridLookup.resize(tableSize * sizeof(uint32_t));
        if (!lookupTable.empty()) {
            std::memcpy(out.brickGridLookup.data(), lookupTable.data(), out.brickGridLookup.size());
        }
    }

    // --- OctreeConfig (same header fields as binary Serialize)
    OctreeConfig& c = out.config;
    std::memset(&c, 0, sizeof(OctreeConfig));

    const int maxLevels = oct->maxLevels;
    const int brickDepth = brickSide > 0
        ? static_cast<int>(std::lround(std::log2(static_cast<double>(brickSide))))
        : 3;
    c.esvoMaxScale    = LaineKarrasOctree::ESVO_MAX_SCALE;
    c.userMaxLevels   = maxLevels;
    c.brickDepthLevels= brickDepth;
    c.brickSize       = 1 << brickDepth;
    c.minESVOScale    = c.esvoMaxScale - c.userMaxLevels + 1;
    const int brickUserScale = c.userMaxLevels - c.brickDepthLevels;
    c.brickESVOScale  = c.esvoMaxScale - (c.userMaxLevels - 1 - brickUserScale);
    c.bricksPerAxis   = oct->bricksPerAxis;

    // NOT FIXED IN M1 -- see M1 Progress Log entry. localToWorld's hardcoded uniform
    // kWorldGridSize=10.0 scale (ignoring the octree's own worldMin/worldMax, which ARE
    // already written correctly into gridMinX/Y/Z below) is a genuine bug in isolation,
    // but a dedicated investigation found it is currently a DE FACTO load-bearing
    // convention: application/main/source/graph/BuildRenderGraph.cpp independently
    // redeclares the identical `constexpr float kWorldGridSize = 10.0f;` in 4 places
    // (search that file for the symbol) and uses it to convert every baked body's
    // grid-space world position/extent (including light-tree cuts) into world units via
    // BodyInstanceGpu's separate worldPos/renderScale instancing layer -- NOT via this
    // localToWorld matrix, which the instanced shader path (TraceWorld.glsl) treats as a
    // fixed body-LOCAL [0,1]^3->[0,10]^3 frame, already de-instanced before worldToLocal
    // is ever applied. Changing this scale to the octree's real worldMax-worldMin here
    // without ALSO updating every one of those BuildRenderGraph.cpp call sites in lockstep
    // silently mis-sizes/mis-places every demo body (Cornell walls, DDGI leak-gate,
    // tier-crossing spheres) currently rendered via the instanced path -- a regression far
    // outside M1's zero-behavior-change gate. Left as pre-existing behavior; flagged back
    // to the plan owner as a separate, larger cross-cutting fix (touches every scene
    // authoring call site, not just SDF bake) rather than silently expanding this plan.
    constexpr float kWorldGridSize = 10.0f;
    c.gridMinX = oct->worldMin.x; c.gridMinY = oct->worldMin.y; c.gridMinZ = oct->worldMin.z;
    c.gridMaxX = oct->worldMax.x; c.gridMaxY = oct->worldMax.y; c.gridMaxZ = oct->worldMax.z;
    const glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(kWorldGridSize));
    const glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
    c.localToWorld = translateMat * scaleMat;
    c.worldToLocal = glm::inverse(c.localToWorld);

    c.nodeArrayBase  = 0;
    c.brickArrayBase = 0;

    // Stored-SDF layout descriptor: formatId, bricksPerAxis, poolBrickBase (=0 single-octree),
    // channelCount, brickStrideFloats, channels[].
    setFormatId(c, STORED_SDF);
    setDescriptorBricksPerAxis(c, static_cast<uint32_t>(oct->bricksPerAxis));
    setSdfBrickArrayBase(c, 0u);  // poolBrickBase; ConcatenateSdf sets per-octree value
    setBrickLookupBase(c, 0u);    // single-octree lookup begins at entry zero
    c.channelCount      = out.channelCount;
    c.brickStrideFloats = out.brickStrideFloats;

    // Conservative reject bound in the SAME normalized [0,1] frame used by the
    // shader's root AABB. It encloses complete allocated bricks, not just surface
    // samples, so a ray rejected by this box cannot reach an ESVO leaf. Traversal
    // still starts from the unchanged root cube after this cheap pre-test.
    if (hasTraceBricks && oct->bricksPerAxis > 0) {
        const float invBpa = 1.0f / static_cast<float>(oct->bricksPerAxis);
        const glm::vec3 traceMin = glm::vec3(traceBrickMin) * invBpa;
        const glm::vec3 traceMax = glm::vec3(traceBrickMaxExclusive) * invBpa;
        c.traceBoundsMinX = traceMin.x;
        c.traceBoundsMinY = traceMin.y;
        c.traceBoundsMinZ = traceMin.z;
        c.traceBoundsMaxX = traceMax.x;
        c.traceBoundsMaxY = traceMax.y;
        c.traceBoundsMaxZ = traceMax.z;
    }
    for (uint32_t ci = 0; ci < out.channelCount && ci < kMaxChannels; ++ci) {
        c.channels[ci] = out.channels[ci];
    }

    return out;
}

// ===========================================================================
// Multi-octree concatenation (unbounded count)
// ===========================================================================

/**
 * Concatenate N ShellOctrees into shared node/brick buffers, recording each
 * octree's nodeArrayBase / brickArrayBase in its config (THE contract).
 * Count is unbounded.
 */
inline ConcatenatedOctrees Concatenate(const std::vector<const ShellOctree*>& octrees) {
    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());
    cat.configs.resize(octrees.size());
    cat.nodeCounts.resize(octrees.size());
    cat.brickCounts.resize(octrees.size());
    cat.tierRefCounts.resize(octrees.size());

    uint32_t nodeBase = 0;   // running element offset into the node buffer
    uint32_t brickBase = 0;  // running brick offset into the brick buffer
    uint32_t tierRefBase = 0; // running element offset into the tier-ref table (Inc2 M1 Task 2)

    for (size_t k = 0; k < octrees.size(); ++k) {
        if (octrees[k] == nullptr) {
            throw std::invalid_argument("ShellOctreeGpu::Concatenate: null octree pointer");
        }
        SerializedOctree s = Serialize(*octrees[k]);

        // Record bases BEFORE appending, then stamp them into this octree's config.
        s.config.nodeArrayBase = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        // tierRefTableBase (Task 3): s.tierRefs is always empty via this plain
        // Serialize() path (no tier-crossing construction path exists yet, M2),
        // so this simply stamps the correct (currently-unused) base for every
        // tree, matching mipPoolBase's "0 == no pool" convention.
        setTierRefTableBase(s.config, tierRefBase);

        cat.configs[k] = s.config;
        cat.nodeCounts[k] = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;
        cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

        cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
        cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());

        // The palette is identical across shells; keep one shared copy.
        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase += s.nodeCount;
        brickBase += s.brickCount;
        tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
    }

    return cat;
}

/**
 * Concatenate N SdfBodyOctrees into shared node/brick/channelPool buffers.
 * Records per-octree nodeArrayBase, brickArrayBase, and poolBrickBase
 * (OctreeConfig.poolBrickBase@208) for each octree. Count is unbounded.
 *
 * poolBrickBase is the ELEMENT offset (in float units) of each octree's
 * first brick in the concatenated channelPool buffer — mirrors the
 * brickArrayBase convention (VoxelSceneCacher.cpp:740 pattern).
 *
 * brickGridLookup: the per-octree lookup tables are appended in order.
 * Each sub-table is uint32[bpa^3] for that octree. brickLookupBase records
 * the prefix sum explicitly, so adjacent octrees may use different bpa values.
 */
inline ConcatenatedOctrees ConcatenateSdf(const std::vector<const SdfBodyOctree*>& octrees) {
    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());
    cat.configs.resize(octrees.size());
    cat.nodeCounts.resize(octrees.size());
    cat.brickCounts.resize(octrees.size());
    cat.tierRefCounts.resize(octrees.size());

    uint32_t nodeBase    = 0;   // running node element offset
    uint32_t brickBase   = 0;   // running brick offset
    uint32_t poolBase    = 0;   // running pool element offset (in floats)
    uint32_t tierRefBase = 0;   // running tier-ref-table element offset (Inc2 M1 Task 2)
    uint32_t lookupBase  = 0;   // running brick-lookup offset (in uint32 entries)

    for (size_t k = 0; k < octrees.size(); ++k) {
        if (octrees[k] == nullptr) {
            throw std::invalid_argument("ShellOctreeGpu::ConcatenateSdf: null octree pointer");
        }
        SerializedOctree s = SerializeSdf(*octrees[k]);

        // Stamp bases into this octree's config BEFORE appending.
        // poolBase is the float-element offset of this octree's first brick in the pool.
        s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        setSdfBrickArrayBase(s.config, poolBase);  // poolBrickBase = poolBase
        setBrickLookupBase(s.config, lookupBase);
        // mipPoolBase is intentionally left at its default (0): SerializeSdf
        // never populates mip pools (opt-in, see MipBake.h), and this plain
        // ConcatenateSdf does not bake them either — ConcatenateSdfWithMips
        // (MipBake.h) is the mip-aware sibling that stamps mipPoolBase and
        // appends cat.mipPool. Leaving it untouched here (rather than
        // advancing it against empty data) keeps mipPoolBase==0 meaningfully
        // "no mip pool" for every octree, matching cat.mipPool staying empty.
        // tierRefTableBase (Task 3): s.tierRefs is populated only by a caller
        // that explicitly registered cross-tier edges before calling
        // ConcatenateSdf (M2's construction path, not built yet) — stamping
        // the base here regardless keeps the field meaningful (0 == "no
        // tier-ref entries before this offset") the same way poolBrickBase/
        // mipPoolBase are always stamped even when their pools are empty.
        setTierRefTableBase(s.config, tierRefBase);

        cat.configs[k]     = s.config;
        cat.nodeCounts[k]  = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;
        cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

        cat.nodes.insert(cat.nodes.end(),   s.nodes.begin(),   s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(),  s.bricks.end());
        cat.channelPool.insert(cat.channelPool.end(),
                               s.channelPool.begin(), s.channelPool.end());
        cat.brickGridLookup.insert(cat.brickGridLookup.end(),
                                   s.brickGridLookup.begin(), s.brickGridLookup.end());
        cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());

        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase  += s.nodeCount;
        brickBase += s.brickCount;
        // poolBase advances by brickCount * brickStrideFloats (total floats per brick)
        poolBase  += s.brickCount * s.brickStrideFloats;
        tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
        lookupBase +=
            static_cast<uint32_t>(s.brickGridLookup.size() / sizeof(uint32_t));
    }

    return cat;
}

// ===========================================================================
// Instance packing
// ===========================================================================

/**
 * Pack a list of BodyInstanceGpu records into a tight byte buffer (in order).
 */
inline std::vector<uint8_t> PackInstances(const std::vector<BodyInstanceGpu>& instances) {
    std::vector<uint8_t> bytes(instances.size() * sizeof(BodyInstanceGpu));
    if (!instances.empty()) {
        std::memcpy(bytes.data(), instances.data(), bytes.size());
    }
    return bytes;
}

}  // namespace Vixen::SVO
