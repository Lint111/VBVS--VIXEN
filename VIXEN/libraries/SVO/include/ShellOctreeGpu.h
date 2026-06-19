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
// the shader, so the struct stays byte-identical (still 256 B; worldGridSize
// still at offset 256) and the existing shader UBO layout is unchanged.

#include "ShellOctree.h"      // ShellOctree, BuildShellOctree
#include "SVOTypes.h"         // ChildDescriptor
#include "SVOBuilder.h"       // Octree / OctreeBlock
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"  // Material

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
 * Octree configuration UBO. MUST match CashSystem::OctreeConfig and the shader's
 * OctreeConfigUBO std140 layout (VoxelSceneCacher.h:100-133,
 * VoxelRayMarch.comp:46-62). The 256-byte UBO portion is what gets uploaded;
 * `worldGridSize` sits just past it (offset 256) and is NOT uploaded.
 *
 * SP2 addition: `nodeArrayBase` / `brickArrayBase` reuse the first two slots of
 * the std140 tail (formerly `_padding4[16]`); the shader did not read that tail,
 * so the layout is byte-identical and still 256 bytes.
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

    glm::mat4 localToWorld;     // 64 bytes
    glm::mat4 worldToLocal;     // 64 bytes

    // std140 tail (formerly float _padding4[16] == 64 bytes). First two int32
    // slots now carry the per-octree base offsets (the concat contract).
    int32_t nodeArrayBase;      // element offset of this octree's first node
    int32_t brickArrayBase;     // brick offset of this octree's first brick
    float   _padding4[14];      // remaining std140 tail, kept zero

    // Non-UBO field (not uploaded) — convenience only.
    float worldGridSize;
};
static_assert(offsetof(OctreeConfig, worldGridSize) == 256,
              "OctreeConfig UBO portion must be exactly 256 bytes (matches the shader UBO)");
static_assert(offsetof(OctreeConfig, nodeArrayBase) == 192,
              "nodeArrayBase must land at the start of the std140 tail (offset 192)");

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

    std::vector<uint8_t> nodes;       // ChildDescriptor array (stride 8)
    std::vector<uint8_t> bricks;      // 512*uint32 per brick (stride 2048)
    std::vector<uint8_t> materials;   // GPUMaterial palette (stride 32)
    OctreeConfig config{};

    uint32_t nodeCount = 0;   // == nodes.size() / sizeof(ChildDescriptor)
    uint32_t brickCount = 0;  // == bricks.size() / kBrickStrideBytes
};

/**
 * <=3 ShellOctrees concatenated into shared node/brick buffers, plus per-octree
 * configs (carrying nodeArrayBase / brickArrayBase) and count tables.
 */
struct ConcatenatedOctrees {
    static constexpr size_t kMaxOctrees = 3;

    std::vector<uint8_t> nodes;   // octree0 nodes ++ octree1 nodes ++ ...
    std::vector<uint8_t> bricks;  // octree0 bricks ++ octree1 bricks ++ ...
    std::vector<uint8_t> materials;  // shared palette (identical across shells)

    std::array<OctreeConfig, kMaxOctrees> configs{};
    std::array<uint32_t, kMaxOctrees> nodeCounts{};
    std::array<uint32_t, kMaxOctrees> brickCounts{};
    uint32_t count = 0;  // number of octrees actually packed (<= kMaxOctrees)
};

/**
 * Per-instance GPU record (std430-friendly, 32 bytes). The host-side BodyInstance
 * lives in the outer repo (vixen/render/scene_instances.h) and is intentionally
 * NOT included here — Task 5b / main bridges the two. octreeIndex selects which
 * concatenated octree (and thus which OctreeConfig) this instance draws.
 */
struct BodyInstanceGpu {
    float worldPos[3];      // 12 bytes
    float renderScale;      // 4 bytes
    float color[3];         // 12 bytes
    uint32_t octreeIndex;   // 4 bytes
};
static_assert(sizeof(BodyInstanceGpu) == 32, "BodyInstanceGpu must be 32 bytes (std430 record)");

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

    constexpr float kWorldGridSize = 10.0f;  // matches cacher WORLD_GRID_SIZE
    c.worldGridSize = kWorldGridSize;

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
