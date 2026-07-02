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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>  // glm::scale / glm::translate

#include <array>
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
 * The shader reads fields only at byte offsets 0..199 (the header ints, gridMin/Max,
 * the two mat4s, and nodeArrayBase@192 / brickArrayBase@196); bytes 200..431 are
 * pure pad it never touches, so the ONLY constraint on the tail is that it makes the
 * struct exactly 432 bytes. (The `worldGridSize` convenience field that used to trail
 * this struct was removed — it was write-only here; the live readers use the SEPARATE
 * CashSystem::OctreeConfig.) Do NOT change the offset of any field at <200; keep the
 * struct exactly 432 bytes so the array stride matches the shader.
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

    std::vector<OctreeConfig> configs;    // per-octree config (size == count)
    std::vector<uint32_t>     nodeCounts; // per-octree node count
    std::vector<uint32_t>     brickCounts;// per-octree brick count
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

    c.esvoMaxScale = 22;
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

    // --- Channel table: canonical order SDF(1) → Color(3) → Roughness(1).
    //     This order is the SINGLE SOURCE OF TRUTH for the pool layout; the test pins it.
    struct ChannelSpec {
        SemanticId  sem;
        uint32_t    elemCount;
        FieldKind   fieldKind;
    };
    const ChannelSpec kChannelSpecs[] = {
        { SEM_SDF,       1u, FK_DISTANCE },
        { SEM_COLOR,     3u, FK_NONE     },
        { SEM_ROUGHNESS, 1u, FK_NONE     },
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

    // --- Dense grid→brick lookup: uint32[bpa^3].
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
    c.esvoMaxScale    = 22;
    c.userMaxLevels   = maxLevels;
    c.brickDepthLevels= brickDepth;
    c.brickSize       = 1 << brickDepth;
    c.minESVOScale    = c.esvoMaxScale - c.userMaxLevels + 1;
    const int brickUserScale = c.userMaxLevels - c.brickDepthLevels;
    c.brickESVOScale  = c.esvoMaxScale - (c.userMaxLevels - 1 - brickUserScale);
    c.bricksPerAxis   = oct->bricksPerAxis;

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
    c.channelCount      = out.channelCount;
    c.brickStrideFloats = out.brickStrideFloats;
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

    uint32_t nodeBase = 0;   // running element offset into the node buffer
    uint32_t brickBase = 0;  // running brick offset into the brick buffer

    for (size_t k = 0; k < octrees.size(); ++k) {
        if (octrees[k] == nullptr) {
            throw std::invalid_argument("ShellOctreeGpu::Concatenate: null octree pointer");
        }
        SerializedOctree s = Serialize(*octrees[k]);

        // Record bases BEFORE appending, then stamp them into this octree's config.
        s.config.nodeArrayBase = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);

        cat.configs[k] = s.config;
        cat.nodeCounts[k] = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;

        cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());

        // The palette is identical across shells; keep one shared copy.
        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase += s.nodeCount;
        brickBase += s.brickCount;
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
 * Each sub-table is uint32[bpa^3] for that octree (bpa may differ if octrees
 * have different bricksPerAxis, though in practice they match). M3 uploads
 * them together; the poolBrickBase offset in the descriptor is sufficient
 * for the shader to index the right sub-table if sizes are equal.
 */
inline ConcatenatedOctrees ConcatenateSdf(const std::vector<const SdfBodyOctree*>& octrees) {
    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());
    cat.configs.resize(octrees.size());
    cat.nodeCounts.resize(octrees.size());
    cat.brickCounts.resize(octrees.size());

    uint32_t nodeBase    = 0;   // running node element offset
    uint32_t brickBase   = 0;   // running brick offset
    uint32_t poolBase    = 0;   // running pool element offset (in floats)

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

        cat.configs[k]     = s.config;
        cat.nodeCounts[k]  = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;

        cat.nodes.insert(cat.nodes.end(),   s.nodes.begin(),   s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(),  s.bricks.end());
        cat.channelPool.insert(cat.channelPool.end(),
                               s.channelPool.begin(), s.channelPool.end());
        cat.brickGridLookup.insert(cat.brickGridLookup.end(),
                                   s.brickGridLookup.begin(), s.brickGridLookup.end());

        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase  += s.nodeCount;
        brickBase += s.brickCount;
        // poolBase advances by brickCount * brickStrideFloats (total floats per brick)
        poolBase  += s.brickCount * s.brickStrideFloats;
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
