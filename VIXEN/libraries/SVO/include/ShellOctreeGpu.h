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
// PER-OCTREE BASE OFFSETS — THE CONTRACT (for <=3-octree concatenation)
// ===========================================================================
// Concatenate() packs <=3 octrees into ONE `nodes` buffer and ONE `bricks`
// buffer (verbatim append, in input order) and records, for octree k:
//   - configs[k].nodeArrayBase  = element offset of octree k's first node
//   - configs[k].brickArrayBase = element offset of octree k's first brick
// so the shader, when it selects octree k for an instance, indexes
//   childDescriptors[nodeArrayBase + localNodeIdx]
//   brickData[(brickArrayBase + localBrickIdx)*512 + voxelIdx].
// These two int32 bases live INSIDE OctreeConfig, in the first two slots of the
// formerly-`_padding4[16]` std140 tail. The tail is uploaded but was unused by
// the shader, so the struct stays byte-identical (exactly 256 B) and the existing
// shader UBO layout is unchanged.

#include "ShellOctree.h"      // ShellOctree, BuildShellOctree
#include "SdfBake.h"          // SdfBodyOctree, BakeRecipeToSdfWorld, BuildSdfBodyOctree
#include "SVOTypes.h"         // ChildDescriptor
#include "SVOBuilder.h"       // Octree / OctreeBlock
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"  // Material, Density

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
 * Octree configuration UBO. MUST match the shader's `OctreeConfig configs[3]`
 * std140 layout (BodyInstanceRayMarch.comp:91-113). `sizeof(OctreeConfig)` is the
 * ARRAY STRIDE the GPU reads `configs[i]` with — it MUST equal the shader's std140
 * element stride or configs[1]/configs[2] read garbage (see static_assert + the
 * stride trap below).
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
struct OctreeConfig {
    int32_t esvoMaxScale;       // Always 22 (ESVO normalized space)
    int32_t userMaxLevels;      // octree maxLevels (== depth + brickDepthLevels for a shell)
    int32_t brickDepthLevels;   // 3 for 8^3 bricks
    int32_t brickSize;          // 1 << brickDepthLevels == 8

    int32_t minESVOScale;       // esvoMaxScale - userMaxLevels + 1
    int32_t brickESVOScale;     // scale at which nodes are brick parents
    int32_t bricksPerAxis;      // octree->bricksPerAxis
    int32_t _padding1;          // pad to 16-byte alignment

    float gridMinX, gridMinY, gridMinZ;
    float _padding2;            // pad vec3 -> vec4

    float gridMaxX, gridMaxY, gridMaxZ;
    float _padding3;            // pad vec3 -> vec4

    glm::mat4 localToWorld;     // 64 bytes (offset 64)
    glm::mat4 worldToLocal;     // 64 bytes (offset 128)

    int32_t nodeArrayBase;      // offset 192: element offset of this octree's first node
    int32_t brickArrayBase;     // offset 196: brick offset of this octree's first brick

    // Pad the C++ element to the shader's std140 UBO-array stride of 432 bytes (the
    // shader's `float _padding4_tail[14]` is 14*16 B under std140, NOT 14*4 — see the
    // trap note above). 432 - 200 = 232 bytes = 58 floats. Never read by the shader.
    float _padding4[58];        // bytes 200..431, kept zero (completes the element to 432 B)
};
static_assert(sizeof(OctreeConfig) == 432,
              "OctreeConfig array stride must equal the shader's 432-byte std140 UBO element "
              "stride (its trailing float[14] is std140-padded to 14*16 B; the compiled SPIR-V "
              "decorates the configs[3] array with ArrayStride 432). BodyOctreeSceneNode uploads "
              "std::array<OctreeConfig,3> at this struct's sizeof stride — a tighter sizeof "
              "misaligns configs[1]/configs[2] and octreeIndex>0 bodies render NOTHING.");
static_assert(offsetof(OctreeConfig, nodeArrayBase) == 192,
              "nodeArrayBase must stay at offset 192 (a field the shader reads)");
static_assert(offsetof(OctreeConfig, brickArrayBase) == 196,
              "brickArrayBase must stay at offset 196 (a field the shader reads)");
static_assert(offsetof(OctreeConfig, localToWorld) == 64 && offsetof(OctreeConfig, worldToLocal) == 128,
              "the two mat4s the shader reads must stay at offsets 64 / 128");

// ============================================================================
// Stored-SDF layout descriptor (Inc2 M2)
// ============================================================================
// Stored as three uint32s at the START of OctreeConfig._padding4 (bytes 200-211).
// The shader never reads _padding4, so these are safe to repurpose.
// M4's GLSL must replicate these exact byte offsets in the OctreeConfig struct.
//
//   byte 200 = _padding4[0] reinterpreted as uint32: formatId
//              0 = FORMAT_BINARY (existing ESVO path, default / zero-init)
//              1 = FORMAT_STORED_SDF (Inc2 trilinear iso-surface path)
//   byte 204 = _padding4[1] reinterpreted as uint32: bricksPerAxis (grid side)
//   byte 208 = _padding4[2] reinterpreted as uint32: sdfBrickArrayBase
//              element offset (in floats) of this octree's first SDF brick in
//              the concatenated sdfBricks buffer (same pattern as brickArrayBase).
//
// sizeof(OctreeConfig) == 432 is UNCHANGED (no new fields; we alias existing pad).

/// Format IDs written into OctreeConfig._padding4[0] (byte 200).
static constexpr uint32_t FORMAT_BINARY     = 0u;  ///< binary ESVO path (default)
static constexpr uint32_t STORED_SDF        = 1u;  ///< Inc2 trilinear iso-surface path

/// Read the formatId from the OctreeConfig tail (byte 200, uint32 aliased in _padding4[0]).
inline uint32_t formatIdOf(const OctreeConfig& c) {
    uint32_t v;
    std::memcpy(&v, &c._padding4[0], sizeof(uint32_t));
    return v;
}
/// Write formatId into the OctreeConfig tail.
inline void setFormatId(OctreeConfig& c, uint32_t id) {
    std::memcpy(&c._padding4[0], &id, sizeof(uint32_t));
}
/// Read the sdfBrickArrayBase from the OctreeConfig tail (byte 208, uint32 aliased in _padding4[2]).
inline uint32_t sdfBrickArrayBaseOf(const OctreeConfig& c) {
    uint32_t v;
    std::memcpy(&v, &c._padding4[2], sizeof(uint32_t));
    return v;
}
/// Write sdfBrickArrayBase into the OctreeConfig tail.
inline void setSdfBrickArrayBase(OctreeConfig& c, uint32_t base) {
    std::memcpy(&c._padding4[2], &base, sizeof(uint32_t));
}
/// Write bricksPerAxis into the OctreeConfig descriptor tail (byte 204, _padding4[1]).
inline void setDescriptorBricksPerAxis(OctreeConfig& c, uint32_t bpa) {
    std::memcpy(&c._padding4[1], &bpa, sizeof(uint32_t));
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

    // SoA SDF brick stride: one float per voxel, 512 voxels => 2048 bytes/brick.
    // Same z*64+y*8+x voxel order as the material brick loop above.
    // Only populated when emitSdf=true (Stored-SDF bodies). Empty for binary ESVO.
    static constexpr uint32_t kSdfBrickStrideBytes = 512u * sizeof(float);  // 2048

    std::vector<uint8_t> nodes;           // ChildDescriptor array (stride 8)
    std::vector<uint8_t> bricks;          // 512*uint32 per brick (stride 2048)
    std::vector<uint8_t> materials;       // GPUMaterial palette (stride 32)
    OctreeConfig config{};

    // Inc2 M2 — SoA-SDF extension (emitSdf=true only):
    std::vector<uint8_t> sdfBricks;       // 512*float per brick (stride 2048)
    std::vector<uint8_t> brickGridLookup; // uint32[bricksPerAxis^3]: grid-coord→brickIndex,
                                          // 0xFFFFFFFF = unallocated brick.

    uint32_t nodeCount = 0;   // == nodes.size() / sizeof(ChildDescriptor)
    uint32_t brickCount = 0;  // == bricks.size() / kBrickStrideBytes
};

/**
 * <=3 ShellOctrees concatenated into shared node/brick buffers, plus per-octree
 * configs (carrying nodeArrayBase / brickArrayBase) and count tables.
 */
struct ConcatenatedOctrees {
    static constexpr size_t kMaxOctrees = 3;

    std::vector<uint8_t> nodes;      // octree0 nodes ++ octree1 nodes ++ ...
    std::vector<uint8_t> bricks;     // octree0 bricks ++ octree1 bricks ++ ...
    std::vector<uint8_t> materials;  // shared palette (identical across shells)

    // Inc2 M2 — SoA-SDF extension (populated by ConcatenateSdf; empty otherwise):
    std::vector<uint8_t> sdfBricks;       // octree0 sdfBricks ++ octree1 sdfBricks ++ ...
    std::vector<uint8_t> brickGridLookup; // octrees concatenated; each sub-table is
                                          // uint32[bpa^3] where bpa = bricksPerAxis.
                                          // Per-octree size varies; M3 uploads them separately.

    std::array<OctreeConfig, kMaxOctrees> configs{};
    std::array<uint32_t, kMaxOctrees> nodeCounts{};
    std::array<uint32_t, kMaxOctrees> brickCounts{};
    uint32_t count = 0;  // number of octrees actually packed (<= kMaxOctrees)
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
//   sdfBricks + brickGridLookup + layout descriptor in OctreeConfig._padding4.
// ===========================================================================

/**
 * Serialize one SdfBodyOctree into CPU byte buffers (material bricks + SoA-SDF
 * bricks + dense grid-lookup table + OctreeConfig with Stored-SDF descriptor).
 *
 * Voxel order in sdfBricks: identical to the material bricks loop —
 *   sdfBricks[i * 512 + z*64+y*8+x] == Density at (gridOrigin+{bx,by,bz}).
 *   Same z-outer, y-middle, x-inner order as the binary Serialize above.
 *
 * Dense grid→brick lookup (brickGridLookup):
 *   A flat uint32[bpa^3] table (bpa = oct->bricksPerAxis, read from Octree).
 *   GPU flat index:  brickX + brickY*bpa + brickZ*bpa^2
 *   Value: brickView index (0..brickCount-1), or 0xFFFFFFFF = unallocated.
 *   Built by inverting brickGridToBrickView (SVOBuilder.h:59-61):
 *     packed key = brickX | (brickY<<10) | (brickZ<<20)
 *
 * Layout descriptor (OctreeConfig._padding4, sizeof unchanged = 432):
 *   byte 200 (_padding4[0], uint32 alias): formatId = STORED_SDF (1u)
 *   byte 204 (_padding4[1], uint32 alias): bricksPerAxis (uint32)
 *   byte 208 (_padding4[2], uint32 alias): sdfBrickArrayBase = 0 single-octree
 *                                          (ConcatenateSdf updates per-octree)
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

    // --- bricks (material) + sdfBricks: iterate brickViews in order,
    //     same z*64+y*8+x inner loop as the binary Serialize above.
    const std::vector<Vixen::GaiaVoxel::EntityBrickView>& brickViews = oct->root->brickViews;
    const int brickSide = oct->brickSideLength;  // 8
    out.brickCount = static_cast<uint32_t>(brickViews.size());

    std::vector<uint32_t> brickWords;
    std::vector<float>    sdfWords;
    brickWords.reserve(brickViews.size() * SerializedOctree::kVoxelsPerBrick);
    sdfWords.reserve(brickViews.size()   * SerializedOctree::kVoxelsPerBrick);

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
                    float    density    = 0.0f;
                    const auto entity = world.getEntityByWorldSpace(worldPos);
                    if (world.exists(entity)) {
                        const auto mat = world.getComponentValue<Vixen::GaiaVoxel::Material>(entity);
                        materialId = mat.has_value() ? mat.value() : 0u;
                        const auto den = world.getComponentValue<Vixen::GaiaVoxel::Density>(entity);
                        density = den.has_value() ? den.value() : 0.0f;
                    }
                    brickWords.push_back(materialId);
                    sdfWords.push_back(density);
                }
            }
        }
    }
    out.bricks.resize(brickWords.size() * sizeof(uint32_t));
    if (!brickWords.empty()) {
        std::memcpy(out.bricks.data(), brickWords.data(), out.bricks.size());
    }
    out.sdfBricks.resize(sdfWords.size() * sizeof(float));
    if (!sdfWords.empty()) {
        std::memcpy(out.sdfBricks.data(), sdfWords.data(), out.sdfBricks.size());
    }

    // --- materials: default palette
    const std::vector<GPUMaterial> palette = detail::BuildDefaultMaterialPalette();
    out.materials.resize(palette.size() * sizeof(GPUMaterial));
    std::memcpy(out.materials.data(), palette.data(), out.materials.size());

    // --- Dense grid→brick lookup: uint32[bpa^3].
    //     brickGridToBrickView key encoding (SVOBuilder.h:108-110):
    //       key = brickX | (brickY<<10) | (brickZ<<20)
    //     GPU flat index: brickX + brickY*bpa + brickZ*bpa*bpa
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

    // Stored-SDF layout descriptor in _padding4 tail (bytes 200-211):
    //   byte 200 (_padding4[0]): formatId = STORED_SDF (1u)
    //   byte 204 (_padding4[1]): bricksPerAxis (uint32)
    //   byte 208 (_padding4[2]): sdfBrickArrayBase (0 for single-octree)
    setFormatId(c, STORED_SDF);
    setDescriptorBricksPerAxis(c, static_cast<uint32_t>(oct->bricksPerAxis));
    setSdfBrickArrayBase(c, 0u);

    return out;
}

// ===========================================================================
// Multi-octree concatenation (<= 3)
// ===========================================================================

/**
 * Concatenate <=3 ShellOctrees into shared node/brick buffers, recording each
 * octree's nodeArrayBase / brickArrayBase in its config (THE contract). Throws
 * std::length_error if given more than 3 octrees.
 */
inline ConcatenatedOctrees Concatenate(const std::vector<const ShellOctree*>& octrees) {
    if (octrees.size() > ConcatenatedOctrees::kMaxOctrees) {
        throw std::length_error("ShellOctreeGpu::Concatenate: at most 3 octrees supported");
    }

    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());

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
 * Concatenate <=3 SdfBodyOctrees into shared node/brick/sdfBricks buffers.
 * Records per-octree nodeArrayBase, brickArrayBase, and sdfBrickArrayBase
 * (in OctreeConfig._padding4[2]) for each octree. Throws std::length_error
 * if given more than 3 octrees.
 *
 * sdfBrickArrayBase is the ELEMENT offset (in float units) of each octree's
 * first SDF brick in the concatenated sdfBricks buffer — mirrors the
 * brickArrayBase convention (VoxelSceneCacher.cpp:740 pattern).
 *
 * brickGridLookup: the per-octree lookup tables are appended in order.
 * Each sub-table is uint32[bpa^3] for that octree (bpa may differ if octrees
 * have different bricksPerAxis, though in practice they match). M3 uploads
 * them together; the sdfBrickArrayBase offset in the descriptor is sufficient
 * for the shader to index the right sub-table if sizes are equal.
 */
inline ConcatenatedOctrees ConcatenateSdf(const std::vector<const SdfBodyOctree*>& octrees) {
    if (octrees.size() > ConcatenatedOctrees::kMaxOctrees) {
        throw std::length_error("ShellOctreeGpu::ConcatenateSdf: at most 3 octrees supported");
    }

    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());

    uint32_t nodeBase    = 0;   // running node element offset
    uint32_t brickBase   = 0;   // running brick offset
    uint32_t sdfBase     = 0;   // running SDF element offset (in floats = voxels)

    for (size_t k = 0; k < octrees.size(); ++k) {
        if (octrees[k] == nullptr) {
            throw std::invalid_argument("ShellOctreeGpu::ConcatenateSdf: null octree pointer");
        }
        SerializedOctree s = SerializeSdf(*octrees[k]);

        // Stamp bases into this octree's config BEFORE appending.
        s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        setSdfBrickArrayBase(s.config, sdfBase);

        cat.configs[k]     = s.config;
        cat.nodeCounts[k]  = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;

        cat.nodes.insert(cat.nodes.end(),   s.nodes.begin(),   s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(),  s.bricks.end());
        cat.sdfBricks.insert(cat.sdfBricks.end(), s.sdfBricks.begin(), s.sdfBricks.end());
        cat.brickGridLookup.insert(cat.brickGridLookup.end(),
                                   s.brickGridLookup.begin(), s.brickGridLookup.end());

        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase  += s.nodeCount;
        brickBase += s.brickCount;
        // sdfBase advances by brickCount * kVoxelsPerBrick (float elements per SDF brick)
        sdfBase   += s.brickCount * SerializedOctree::kVoxelsPerBrick;
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
