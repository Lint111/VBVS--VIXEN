/**
 * @file test_rayquery_feasibility.cpp
 * @brief W-RT Slice 1 feasibility spike — standalone, production-code-untouched proof that
 *        VK_KHR_ray_query works end-to-end on this engine's toolchain: capability gate, device
 *        creation with the ray_query + acceleration_structure feature chain, a procedural
 *        voxel-sphere BLAS/TLAS build, a rayQueryEXT compute dispatch against it, CPU-brute-force
 *        parity check, and a report-only microbench (RT_QUERY vs a software brute-force floor).
 *
 * Scene, extensions, and pass structure follow the fixture pattern in
 * test_recipe_nested_invocation.cpp (VixenSelectWslGpuIcd, raw vkCreateInstance @ 1.3,
 * GTEST_SKIP on missing Vulkan / no discrete-or-integrated GPU / missing RT features — this
 * fixture must never FAIL on non-RT hardware or WSL/Dozen, only SKIP).
 *
 * Deliberately standalone: no engine octree/SVO dependency for the scene — this slice only
 * proves the raw Vulkan/GLSL mechanism works on this stack, not integration.
 *
 * HANDOFF: from a Windows shell, after a fresh MSVC build, run the gtest binary DIRECTLY (per
 * KI-014, never via ctest):
 *
 *     build\libraries\SVO\tests\Debug\test_rayquery_feasibility.exe
 */

#include <gtest/gtest.h>

#include "ShaderCompiler.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Deterministic RNG (SplitMix64) — no std::random_device, no time seeds.
// ---------------------------------------------------------------------------
struct SplitMix64 {
    uint64_t state;
    explicit SplitMix64(uint64_t seed) : state(seed) {}
    uint64_t Next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // [0,1)
    double NextDouble() { return (Next() >> 11) * (1.0 / 9007199254740992.0); }
    float NextFloat(float lo, float hi) { return lo + static_cast<float>(NextDouble()) * (hi - lo); }
};

struct Vec3 { float x, y, z; };
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Normalize(Vec3 v) { float l = Length(v); return l > 1e-8f ? v * (1.0f / l) : Vec3{0, 0, 1}; }

// AABB stored as 6 floats (min.xyz, max.xyz), matching VkAabbPositionsKHR layout.
struct Aabb { float minX, minY, minZ, maxX, maxY, maxZ; };

// ---------------------------------------------------------------------------
// Scene: voxelize a sphere (center 0.5,0.5,0.5, radius 0.35) into a 64^3 grid over local
// space [0,1]^3 (same convention the engine octrees use), partitioned into 8x8x8 bricks
// (8 bricks/side). Emit one AABB per non-empty brick.
// ---------------------------------------------------------------------------
std::vector<Aabb> BuildSphereBrickAabbs() {
    constexpr int kGrid = 64;
    constexpr int kBricksPerSide = 8;
    constexpr int kVoxelsPerBrick = kGrid / kBricksPerSide;  // 8
    constexpr float kCenter = 0.5f, kRadius = 0.35f;

    bool brickOccupied[kBricksPerSide][kBricksPerSide][kBricksPerSide] = {};

    for (int vx = 0; vx < kGrid; ++vx) {
        for (int vy = 0; vy < kGrid; ++vy) {
            for (int vz = 0; vz < kGrid; ++vz) {
                float px = (vx + 0.5f) / kGrid;
                float py = (vy + 0.5f) / kGrid;
                float pz = (vz + 0.5f) / kGrid;
                float dx = px - kCenter, dy = py - kCenter, dz = pz - kCenter;
                if (dx * dx + dy * dy + dz * dz <= kRadius * kRadius) {
                    brickOccupied[vx / kVoxelsPerBrick][vy / kVoxelsPerBrick][vz / kVoxelsPerBrick] = true;
                }
            }
        }
    }

    std::vector<Aabb> bricks;
    for (int bx = 0; bx < kBricksPerSide; ++bx)
        for (int by = 0; by < kBricksPerSide; ++by)
            for (int bz = 0; bz < kBricksPerSide; ++bz)
                if (brickOccupied[bx][by][bz]) {
                    float minX = bx / static_cast<float>(kBricksPerSide);
                    float minY = by / static_cast<float>(kBricksPerSide);
                    float minZ = bz / static_cast<float>(kBricksPerSide);
                    bricks.push_back(Aabb{
                        minX, minY, minZ,
                        minX + 1.0f / kBricksPerSide,
                        minY + 1.0f / kBricksPerSide,
                        minZ + 1.0f / kBricksPerSide});
                }
    return bricks;
}

// Coarse 8^3 occupancy grid over the brick list: grid[(bz*8+by)*8+bx] = brick index, or
// 0xFFFFFFFF if empty. Cell derived from the AABB min corner (matches BuildSphereBrickAabbs'
// bx/by/bz -> minX/minY/minZ mapping). Same indexing must be used in the GLSL DDA shader.
std::vector<uint32_t> BuildBrickGrid(const std::vector<Aabb>& bricks) {
    std::vector<uint32_t> grid(512, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < bricks.size(); ++i) {
        int bx = static_cast<int>(std::round(bricks[i].minX * 8.0f));
        int by = static_cast<int>(std::round(bricks[i].minY * 8.0f));
        int bz = static_cast<int>(std::round(bricks[i].minZ * 8.0f));
        grid[(bz * 8 + by) * 8 + bx] = i;
    }
    return grid;
}

struct Ray { Vec3 origin; Vec3 dir; };

// Rays start OUTSIDE the [0,1]^3 cube (z=-0.5 plane, jittered x/y) aimed at jittered targets
// inside the cube, so no ray originates inside an AABB (avoids the t<0 inside-origin
// ambiguity). Includes a guaranteed-miss band (targets well outside the cube).
std::vector<Ray> GenerateRays(uint32_t count, uint64_t seed) {
    SplitMix64 rng(seed);
    std::vector<Ray> rays;
    rays.reserve(count);
    const uint32_t missBandStart = count * 9 / 10;  // last 10% guaranteed misses
    for (uint32_t i = 0; i < count; ++i) {
        Vec3 origin{rng.NextFloat(-0.25f, 1.25f), rng.NextFloat(-0.25f, 1.25f), -0.5f};
        Vec3 target;
        if (i >= missBandStart) {
            // Aim well outside the cube on the far side -> guaranteed miss.
            target = Vec3{rng.NextFloat(2.0f, 3.0f), rng.NextFloat(2.0f, 3.0f), rng.NextFloat(2.0f, 3.0f)};
        } else {
            target = Vec3{rng.NextFloat(0.0f, 1.0f), rng.NextFloat(0.0f, 1.0f), rng.NextFloat(0.0f, 1.0f)};
        }
        rays.push_back(Ray{origin, Normalize(target - origin)});
    }
    return rays;
}

// ---------------------------------------------------------------------------
// CPU brute-force reference: slab test over ALL brick AABBs, nearest t wins. Identical float
// math to the GLSL slab test below (same operation order) to keep parity tolerances tight.
// ---------------------------------------------------------------------------
struct CpuHit { float t; uint32_t prim; };

bool SlabTest(const Aabb& b, Vec3 origin, Vec3 dir, float& outT) {
    float invX = 1.0f / dir.x, invY = 1.0f / dir.y, invZ = 1.0f / dir.z;
    float tx1 = (b.minX - origin.x) * invX, tx2 = (b.maxX - origin.x) * invX;
    float ty1 = (b.minY - origin.y) * invY, ty2 = (b.maxY - origin.y) * invY;
    float tz1 = (b.minZ - origin.z) * invZ, tz2 = (b.maxZ - origin.z) * invZ;
    float tmin = std::max({std::min(tx1, tx2), std::min(ty1, ty2), std::min(tz1, tz2), 0.0f});
    float tmax = std::min({std::max(tx1, tx2), std::max(ty1, ty2), std::max(tz1, tz2)});
    if (tmax < tmin) return false;
    outT = tmin;
    return true;
}

CpuHit BruteForceCpu(const std::vector<Aabb>& bricks, Vec3 origin, Vec3 dir) {
    CpuHit best{-1.0f, 0xFFFFFFFFu};
    for (uint32_t i = 0; i < bricks.size(); ++i) {
        float t;
        if (SlabTest(bricks[i], origin, dir, t)) {
            if (best.prim == 0xFFFFFFFFu || t < best.t) { best.t = t; best.prim = i; }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// GLSL sources (embedded strings).
// ---------------------------------------------------------------------------
const char* kRayQueryShaderSrc = R"GLSL(
#version 460
#extension GL_EXT_ray_query : require

layout(local_size_x = 64) in;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, std430) readonly buffer Rays { vec4 originsAndDirs[]; }; // 2 vec4 per ray: origin, dir
// t, prim (as float bits), AABB-candidate count, generate-call count (debug instrumentation)
layout(set = 0, binding = 2, std430) writeonly buffer Out { vec4 hits[]; };
// Binding 3 is the SAME tightly-packed 24-byte-per-AABB buffer bound as the BLAS geometry
// input (C++ `struct Aabb { float minX,minY,minZ,maxX,maxY,maxZ; }`, stride = sizeof(Aabb)).
// std430 packs a bare float array with no padding, so index prim*6 + [0..5] reproduces that
// layout exactly (do NOT declare this as vec4[] — that assumes 32-byte stride and misaligns
// every brick past index 0).
layout(set = 0, binding = 3, std430) readonly buffer Bricks { float aabbData[]; };
// Debug: first 8 candidates per ray as (prim, tmin | -999 if slab-rejected). Length-guarded so a
// tiny dummy buffer disables recording (microbench path).
layout(set = 0, binding = 4, std430) buffer CandDebug { vec2 candDebug[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint rayCount = originsAndDirs.length() / 2u;
    if (idx >= rayCount) return;

    vec3 origin = originsAndDirs[idx * 2u + 0u].xyz;
    vec3 dir    = originsAndDirs[idx * 2u + 1u].xyz;

    rayQueryEXT q;
    rayQueryInitializeEXT(q, topLevelAS, gl_RayFlagsNoneEXT, 0xFF, origin, 0.0, dir, 1e30);

    uint candCount = 0u;
    uint genCount  = 0u;
    // SPEC PRECONDITION (Slice-1 finding): rayQueryGenerateIntersectionEXT's tHit must lie
    // within the CURRENT ray interval [tMin, committed t] — generating beyond the committed
    // hit is app UB, and NVIDIA implements generate as an unconditional replace (last one
    // wins). The candidate loop must therefore track its own running minimum and only
    // generate on improvement. Without this, ~2% of rays commit the wrong (later) brick.
    float bestT = 1e30;
    while (rayQueryProceedEXT(q)) {
        if (rayQueryGetIntersectionTypeEXT(q, false) == gl_RayQueryCandidateIntersectionAABBEXT) {
            candCount++;
            uint prim = rayQueryGetIntersectionPrimitiveIndexEXT(q, false);
            vec3 bmin = vec3(aabbData[prim * 6u + 0u], aabbData[prim * 6u + 1u], aabbData[prim * 6u + 2u]);
            vec3 bmax = vec3(aabbData[prim * 6u + 3u], aabbData[prim * 6u + 4u], aabbData[prim * 6u + 5u]);
            vec3 invD = 1.0 / dir;
            vec3 t1 = (bmin - origin) * invD;
            vec3 t2 = (bmax - origin) * invD;
            vec3 tlo = min(t1, t2);
            vec3 thi = max(t1, t2);
            float tmin = max(max(tlo.x, tlo.y), max(tlo.z, 0.0));
            float tmax = min(min(thi.x, thi.y), thi.z);
            bool pass = (tmax >= tmin) && (tmin < bestT);
            uint slot = idx * 8u + min(candCount - 1u, 7u);
            if (slot < candDebug.length()) {
                candDebug[slot] = vec2(float(prim), pass ? tmin : -999.0);
            }
            if (pass) {
                bestT = tmin;
                genCount++;
                rayQueryGenerateIntersectionEXT(q, tmin);
            }
        }
    }

    if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionGeneratedEXT) {
        float t = rayQueryGetIntersectionTEXT(q, true);
        uint prim = rayQueryGetIntersectionPrimitiveIndexEXT(q, true);
        hits[idx] = vec4(t, uintBitsToFloat(prim), float(candCount), float(genCount));
    } else {
        hits[idx] = vec4(-1.0, uintBitsToFloat(0xFFFFFFFFu), float(candCount), float(genCount));
    }
}
)GLSL";

// Software brute-force floor: identical slab-test math, no rayQuery, loops all bricks.
const char* kBruteForceShaderSrc = R"GLSL(
#version 460
layout(local_size_x = 64) in;

layout(set = 0, binding = 1, std430) readonly buffer Rays { vec4 originsAndDirs[]; };
layout(set = 0, binding = 2, std430) writeonly buffer Out { vec4 hits[]; }; // t, prim, passCount, passCount
// Same 24-byte-per-AABB layout as the RT_QUERY shader above — see comment there.
layout(set = 0, binding = 3, std430) readonly buffer Bricks { float aabbData[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint rayCount = originsAndDirs.length() / 2u;
    if (idx >= rayCount) return;

    vec3 origin = originsAndDirs[idx * 2u + 0u].xyz;
    vec3 dir    = originsAndDirs[idx * 2u + 1u].xyz;
    vec3 invD   = 1.0 / dir;

    uint brickCount = aabbData.length() / 6u;
    float bestT = -1.0;
    uint bestPrim = 0xFFFFFFFFu;
    uint passCount = 0u;
    for (uint i = 0u; i < brickCount; ++i) {
        vec3 bmin = vec3(aabbData[i * 6u + 0u], aabbData[i * 6u + 1u], aabbData[i * 6u + 2u]);
        vec3 bmax = vec3(aabbData[i * 6u + 3u], aabbData[i * 6u + 4u], aabbData[i * 6u + 5u]);
        vec3 t1 = (bmin - origin) * invD;
        vec3 t2 = (bmax - origin) * invD;
        vec3 tlo = min(t1, t2);
        vec3 thi = max(t1, t2);
        float tmin = max(max(tlo.x, tlo.y), max(tlo.z, 0.0));
        float tmax = min(min(thi.x, thi.y), thi.z);
        if (tmax >= tmin) {
            passCount++;
            if (bestPrim == 0xFFFFFFFFu || tmin < bestT) { bestT = tmin; bestPrim = i; }
        }
    }
    hits[idx] = vec4(bestT, uintBitsToFloat(bestPrim), float(passCount), float(passCount));
}
)GLSL";

// Stackless coarse-grid DDA: slab-test the unit cube, then Amanatides-Woo march an 8^3
// occupancy grid, no ray-query extension (pure compute). Cell-entry t matches the CPU slab
// tmin of that cell's AABB (parity contract with BruteForceCpu).
const char* kBrickmapDdaShaderSrc = R"GLSL(
#version 460
layout(local_size_x = 64) in;

layout(set = 0, binding = 1, std430) readonly buffer Rays { vec4 originsAndDirs[]; };
layout(set = 0, binding = 2, std430) writeonly buffer Out { vec4 hits[]; }; // t, prim, stepCount, 0
layout(set = 0, binding = 3, std430) readonly buffer Bricks { float aabbData[]; };
layout(set = 0, binding = 5, std430) readonly buffer Grid { uint brickGrid[]; };

const float kGridN = 8.0;
const float kInf = 1e30;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint rayCount = originsAndDirs.length() / 2u;
    if (idx >= rayCount) return;

    vec3 origin = originsAndDirs[idx * 2u + 0u].xyz;
    vec3 dir    = originsAndDirs[idx * 2u + 1u].xyz;

    // --- slab test vs the unit cube [0,1]^3 ---
    vec3 invD = 1.0 / dir;
    vec3 t1 = (vec3(0.0) - origin) * invD;
    vec3 t2 = (vec3(1.0) - origin) * invD;
    vec3 tlo = min(t1, t2);
    vec3 thi = max(t1, t2);
    float tEnter = max(max(tlo.x, tlo.y), max(tlo.z, 0.0));
    float tExit  = min(min(thi.x, thi.y), thi.z);

    uint stepCount = 0u;
    if (tExit < tEnter) {
        hits[idx] = vec4(-1.0, uintBitsToFloat(0xFFFFFFFFu), float(stepCount), 0.0);
        return;
    }

    // Nudge slightly into the cube so the entry point resolves to the cell we're actually
    // entering, not the boundary cell behind it (fp robustness at the cube surface).
    // ponytail: the 1e-5 nudge << the 1e-4 parity tolerance and << the 0.125 cell size, so a
    // boundary-graze that resolves one cell forward is absorbed by the tie-recompute fallback
    // in the parity test. Keep that ordering if any of the three constants ever changes.
    vec3 pos = origin + dir * (tEnter + 1e-5);
    ivec3 cell = clamp(ivec3(floor(pos * kGridN)), ivec3(0), ivec3(7));

    ivec3 stepDir;
    vec3 tDelta;
    vec3 tMax;
    for (int axis = 0; axis < 3; ++axis) {
        float d = dir[axis];
        if (abs(d) < 1e-8) {
            stepDir[axis] = 0;
            tDelta[axis] = kInf;
            tMax[axis] = kInf;
        } else {
            stepDir[axis] = d > 0.0 ? 1 : -1;
            tDelta[axis] = (1.0 / kGridN) / abs(d);
            // Next cell boundary along this axis, derived from the cell index (not pos) to
            // avoid fp drift accumulating across steps.
            float nextBoundary = (float(cell[axis]) + (d > 0.0 ? 1.0 : 0.0)) / kGridN;
            tMax[axis] = (nextBoundary - origin[axis]) / d;
        }
    }

    float tCellEnter = tEnter;
    for (int iter = 0; iter < 24; ++iter) {
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(8)))) break;

        // PARITY PRECONDITION: bricks tile grid cells 1:1 (BuildSphereBrickAabbs emits exactly
        // one AABB per occupied cell), so first-occupied-CELL == nearest-BRICK and cell-entry t
        // == that brick's slab tmin. If brick granularity ever diverges from the grid, the
        // comparison against BruteForceCpu stops being like-for-like.
        uint g = brickGrid[(cell.z * 8 + cell.y) * 8 + cell.x];
        if (g != 0xFFFFFFFFu) {
            hits[idx] = vec4(tCellEnter, uintBitsToFloat(g), float(stepCount), 0.0);
            return;
        }

        // Advance via the smallest tMax axis.
        int axis = (tMax.x < tMax.y) ? ((tMax.x < tMax.z) ? 0 : 2) : ((tMax.y < tMax.z) ? 1 : 2);
        tCellEnter = tMax[axis];
        cell[axis] += stepDir[axis];
        tMax[axis] += tDelta[axis];
        stepCount++;
    }

    hits[idx] = vec4(-1.0, uintBitsToFloat(0xFFFFFFFFu), float(stepCount), 0.0);
}
)GLSL";

}  // namespace

// ---------------------------------------------------------------------------
// Fixture — same SetUp()-GTEST_SKIP shape as RecipeNestedInvocationGpuParityTest.
// ---------------------------------------------------------------------------
class RayQueryFeasibilityTest : public ::testing::Test {
protected:
    VkInstance       instance_        = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_  = VK_NULL_HANDLE;
    VkDevice         device_          = VK_NULL_HANDLE;
    VkQueue          queue_           = VK_NULL_HANDLE;
    VkCommandPool    commandPool_     = VK_NULL_HANDLE;
    uint32_t         queueFamily_     = 0;
    bool             realGpuConfirmed_ = false;
    VkDeviceSize     scratchAlignment_ = 1;  // minAccelerationStructureScratchOffsetAlignment

    // Extension function pointers (loaded via vkGetDeviceProcAddr).
    PFN_vkGetAccelerationStructureBuildSizesKHR    pvkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCreateAccelerationStructureKHR           pvkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          pvkDestroyAccelerationStructureKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        pvkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR pvkGetAccelerationStructureDeviceAddressKHR_ = nullptr;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_rayquery_feasibility";
        appInfo.apiVersion       = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;
        if (vkCreateInstance(&instInfo, nullptr, &instance_) != VK_SUCCESS) {
            GTEST_SKIP() << "vkCreateInstance failed — no Vulkan available on this machine.";
        }

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) { GTEST_SKIP() << "No Vulkan physical devices visible."; }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) { physicalDevice_ = dev; realGpuConfirmed_ = true; break; }
        }
        if (!realGpuConfirmed_) {
            GTEST_SKIP() << "No REAL (discrete/integrated) GPU found — skipping (WSL/Dozen is CPU-type).";
        }

        // --- Capability gate: required device extensions ---
        static const char* kRequiredExtensions[] = {
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
            VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        };
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availableExts(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, availableExts.data());

        std::vector<const char*> missing;
        for (const char* required : kRequiredExtensions) {
            bool found = false;
            for (const auto& ext : availableExts) {
                if (std::strcmp(ext.extensionName, required) == 0) { found = true; break; }
            }
            if (!found) missing.push_back(required);
        }
        if (!missing.empty()) {
            std::string msg = "Missing required extensions:";
            for (const char* m : missing) { msg += " "; msg += m; }
            GTEST_SKIP() << msg;
        }

        // --- Capability gate: required features ---
        VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{};
        rqFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeatures.pNext = &rqFeatures;
        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12Features.pNext = &asFeatures;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk12Features;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);

        std::vector<std::string> missingFeatures;
        if (!asFeatures.accelerationStructure) missingFeatures.push_back("accelerationStructure");
        if (!rqFeatures.rayQuery) missingFeatures.push_back("rayQuery");
        if (!vk12Features.bufferDeviceAddress) missingFeatures.push_back("bufferDeviceAddress");
        if (!missingFeatures.empty()) {
            std::string msg = "Missing required features:";
            for (auto& m : missingFeatures) { msg += " "; msg += m; }
            GTEST_SKIP() << msg;
        }

        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(physicalDevice_, &props2);
        scratchAlignment_ = std::max<VkDeviceSize>(asProps.minAccelerationStructureScratchOffsetAlignment, 1);

        CreateLogicalDevice(kRequiredExtensions, 6);
        LoadExtensionFunctions();
        CreateCommandPool();
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    void CreateLogicalDevice(const char* const* extensions, uint32_t extensionCount) {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found);

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{};
        rqFeatures.sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rqFeatures.rayQuery = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeatures.accelerationStructure = VK_TRUE;
        asFeatures.pNext                 = &rqFeatures;

        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12Features.bufferDeviceAddress  = VK_TRUE;
        vk12Features.pNext                = &asFeatures;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk12Features;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.pNext                   = &features2;
        dInfo.queueCreateInfoCount    = 1;
        dInfo.pQueueCreateInfos       = &qInfo;
        dInfo.enabledExtensionCount   = extensionCount;
        dInfo.ppEnabledExtensionNames = extensions;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &device_), VK_SUCCESS);
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    }

    void LoadExtensionFunctions() {
        pvkGetAccelerationStructureBuildSizesKHR_ = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
            vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureBuildSizesKHR"));
        pvkCreateAccelerationStructureKHR_ = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device_, "vkCreateAccelerationStructureKHR"));
        pvkDestroyAccelerationStructureKHR_ = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device_, "vkDestroyAccelerationStructureKHR"));
        pvkCmdBuildAccelerationStructuresKHR_ = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            vkGetDeviceProcAddr(device_, "vkCmdBuildAccelerationStructuresKHR"));
        pvkGetAccelerationStructureDeviceAddressKHR_ = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureDeviceAddressKHR"));
        ASSERT_NE(pvkGetAccelerationStructureBuildSizesKHR_, nullptr);
        ASSERT_NE(pvkCreateAccelerationStructureKHR_, nullptr);
        ASSERT_NE(pvkDestroyAccelerationStructureKHR_, nullptr);
        ASSERT_NE(pvkCmdBuildAccelerationStructuresKHR_, nullptr);
        ASSERT_NE(pvkGetAccelerationStructureDeviceAddressKHR_, nullptr);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        return UINT32_MAX;
    }

// GTEST_SKIP()/ASSERT_* inside a void helper only return from THAT helper — the caller keeps
// executing with null handles unless it checks. Guard every helper call site with this
// (ASSERT_NO_FATAL_FAILURE alone does not stop the caller on a skip).
#define RQ_GUARD() \
    do { if (::testing::Test::HasFatalFailure() || ::testing::Test::IsSkipped()) return; } while (0)

    // Every allocation backing a device-address buffer needs
    // VkMemoryAllocateFlagsInfo{VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT} chained.
    // `memProps` picks the memory type: HOST_VISIBLE|HOST_COHERENT for buffers this fixture
    // maps (rays/AABBs/output/instance), DEVICE_LOCAL for AS-storage/scratch (never mapped —
    // on non-ReBAR discrete GPUs the host-visible heap can be a small BAR window that doesn't
    // intersect the AS-storage/device-address memoryTypeBits at all). No suitable memory type
    // is hardware variation, not a bug: SKIP rather than FAIL.
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool deviceAddress,
                       VkMemoryPropertyFlags memProps,
                       VkBuffer& outBuf, VkDeviceMemory& outMem) {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(device_, &bi, nullptr, &outBuf), VK_SUCCESS);

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, outBuf, &req);

        VkMemoryAllocateFlagsInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, memProps);
        if (ai.memoryTypeIndex == UINT32_MAX) {
            GTEST_SKIP() << "No memory type satisfies requirements 0x" << std::hex
                         << req.memoryTypeBits << std::dec << " with properties 0x"
                         << std::hex << memProps << std::dec
                         << " — hardware memory-topology variation, not a ray-query defect.";
            return;
        }
        if (deviceAddress) ai.pNext = &flagsInfo;

        ASSERT_EQ(vkAllocateMemory(device_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(device_, outBuf, outMem, 0), VK_SUCCESS);
    }

    static constexpr VkMemoryPropertyFlags kHostVisible =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    static constexpr VkMemoryPropertyFlags kDeviceLocal =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // vkCmdBuildAccelerationStructuresKHR requires scratchData.deviceAddress aligned to
    // minAccelerationStructureScratchOffsetAlignment (commonly 128 on NVIDIA). The scratch
    // buffer is over-allocated by that alignment so a rounded-up address always stays in-bounds.
    VkDeviceAddress AlignedScratchAddress(VkBuffer scratchBuf) {
        VkDeviceAddress addr = GetBufferDeviceAddress(scratchBuf);
        return (addr + scratchAlignment_ - 1) & ~(scratchAlignment_ - 1);
    }

    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buf) {
        VkBufferDeviceAddressInfo info{};
        info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = buf;
        return vkGetBufferDeviceAddress(device_, &info);
    }

    VkCommandBuffer BeginOneShotCommands() {
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = commandPool_;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &cbai, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void EndAndSubmit(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    }

    // Builds a BLAS over `aabbBuf` (bricks.size() AABBs) then a TLAS with one identity
    // instance referencing it. Returns handles + backing buffers/memory via out-params (the
    // caller owns destruction).
    struct AsResult {
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
        VkBuffer blasBuf = VK_NULL_HANDLE, tlasBuf = VK_NULL_HANDLE;
        VkDeviceMemory blasMem = VK_NULL_HANDLE, tlasMem = VK_NULL_HANDLE;
        VkBuffer instanceBuf = VK_NULL_HANDLE;
        VkDeviceMemory instanceMem = VK_NULL_HANDLE;
        VkBuffer scratchBuf = VK_NULL_HANDLE;
        VkDeviceMemory scratchMem = VK_NULL_HANDLE;
    };

    void BuildAccelerationStructures(const std::vector<Aabb>& bricks, VkBuffer aabbBuf, AsResult& out) {
        const uint32_t brickCount = static_cast<uint32_t>(bricks.size());

        // --- BLAS geometry over AABBs ---
        VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
        aabbsData.sType   = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        aabbsData.data.deviceAddress = GetBufferDeviceAddress(aabbBuf);
        aabbsData.stride  = sizeof(Aabb);  // 24 bytes, matches VkAabbPositionsKHR

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs = aabbsData;
        geometry.flags         = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo{};
        blasBuildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        blasBuildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        blasBuildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        blasBuildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        blasBuildInfo.geometryCount = 1;
        blasBuildInfo.pGeometries   = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR blasSizes{};
        blasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        pvkGetAccelerationStructureBuildSizesKHR_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &blasBuildInfo, &brickCount, &blasSizes);

        CreateBuffer(blasSizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true, kDeviceLocal, out.blasBuf, out.blasMem);
        RQ_GUARD();

        VkAccelerationStructureCreateInfoKHR blasCreate{};
        blasCreate.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        blasCreate.buffer = out.blasBuf;
        blasCreate.size   = blasSizes.accelerationStructureSize;
        blasCreate.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ASSERT_EQ(pvkCreateAccelerationStructureKHR_(device_, &blasCreate, nullptr, &out.blas), VK_SUCCESS);

        VkDeviceSize scratchSize =
            std::max(blasSizes.buildScratchSize, static_cast<VkDeviceSize>(1)) + scratchAlignment_;
        CreateBuffer(scratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true, kDeviceLocal, out.scratchBuf, out.scratchMem);
        RQ_GUARD();

        blasBuildInfo.dstAccelerationStructure  = out.blas;
        blasBuildInfo.scratchData.deviceAddress = AlignedScratchAddress(out.scratchBuf);

        VkAccelerationStructureBuildRangeInfoKHR blasRange{};
        blasRange.primitiveCount = brickCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pBlasRange = &blasRange;

        VkCommandBuffer cmd = BeginOneShotCommands();
        pvkCmdBuildAccelerationStructuresKHR_(cmd, 1, &blasBuildInfo, &pBlasRange);

        // NOTE: the BLAS and TLAS builds go through separate one-shot command buffers and
        // EndAndSubmit's vkQueueWaitIdle already fully orders them — this barrier is
        // belt-and-braces, not the ordering mechanism.
        VkMemoryBarrier blasBarrier{};
        blasBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        blasBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        blasBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &blasBarrier, 0, nullptr, 0, nullptr);
        EndAndSubmit(cmd);

        VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
        blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        blasAddrInfo.accelerationStructure = out.blas;
        VkDeviceAddress blasAddress = pvkGetAccelerationStructureDeviceAddressKHR_(device_, &blasAddrInfo);

        // --- TLAS: one identity instance referencing the BLAS ---
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform.matrix[0][0] = 1.0f;
        instance.transform.matrix[1][1] = 1.0f;
        instance.transform.matrix[2][2] = 1.0f;
        instance.instanceCustomIndex  = 0;
        instance.mask                 = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags                 = 0;  // AABB (procedural) BLAS instance — triangle-facing cull is N/A
        instance.accelerationStructureReference = blasAddress;

        CreateBuffer(sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true, kHostVisible, out.instanceBuf, out.instanceMem);
        RQ_GUARD();
        {
            void* mapped = nullptr;
            vkMapMemory(device_, out.instanceMem, 0, sizeof(instance), 0, &mapped);
            std::memcpy(mapped, &instance, sizeof(instance));
            vkUnmapMemory(device_, out.instanceMem);
        }

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.data.deviceAddress = GetBufferDeviceAddress(out.instanceBuf);

        VkAccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tlasGeometry.geometry.instances = instancesData;

        VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
        tlasBuildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        tlasBuildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tlasBuildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        tlasBuildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tlasBuildInfo.geometryCount = 1;
        tlasBuildInfo.pGeometries   = &tlasGeometry;

        const uint32_t oneInstance = 1;
        VkAccelerationStructureBuildSizesInfoKHR tlasSizes{};
        tlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        pvkGetAccelerationStructureBuildSizesKHR_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &tlasBuildInfo, &oneInstance, &tlasSizes);

        CreateBuffer(tlasSizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true, kDeviceLocal, out.tlasBuf, out.tlasMem);
        RQ_GUARD();

        VkAccelerationStructureCreateInfoKHR tlasCreate{};
        tlasCreate.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        tlasCreate.buffer = out.tlasBuf;
        tlasCreate.size   = tlasSizes.accelerationStructureSize;
        tlasCreate.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        ASSERT_EQ(pvkCreateAccelerationStructureKHR_(device_, &tlasCreate, nullptr, &out.tlas), VK_SUCCESS);

        // Reuse the scratch buffer if big enough (accounting for alignment padding), otherwise
        // allocate a bigger one (also padded by scratchAlignment_ for AlignedScratchAddress).
        if (tlasSizes.buildScratchSize + scratchAlignment_ > scratchSize) {
            vkDestroyBuffer(device_, out.scratchBuf, nullptr);
            vkFreeMemory(device_, out.scratchMem, nullptr);
            CreateBuffer(tlasSizes.buildScratchSize + scratchAlignment_,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                true, kDeviceLocal, out.scratchBuf, out.scratchMem);
            RQ_GUARD();
        }
        tlasBuildInfo.dstAccelerationStructure  = out.tlas;
        tlasBuildInfo.scratchData.deviceAddress = AlignedScratchAddress(out.scratchBuf);

        VkAccelerationStructureBuildRangeInfoKHR tlasRange{};
        tlasRange.primitiveCount = 1;
        const VkAccelerationStructureBuildRangeInfoKHR* pTlasRange = &tlasRange;

        VkCommandBuffer cmd2 = BeginOneShotCommands();
        // Barrier before dispatch consumers will also need ACCELERATION_STRUCTURE_READ for
        // COMPUTE stage; that one is issued by the caller right before the compute dispatch.
        pvkCmdBuildAccelerationStructuresKHR_(cmd2, 1, &tlasBuildInfo, &pTlasRange);
        EndAndSubmit(cmd2);
    }

    void DestroyAs(AsResult& as) {
        if (as.tlas != VK_NULL_HANDLE) pvkDestroyAccelerationStructureKHR_(device_, as.tlas, nullptr);
        if (as.blas != VK_NULL_HANDLE) pvkDestroyAccelerationStructureKHR_(device_, as.blas, nullptr);
        if (as.tlasBuf != VK_NULL_HANDLE) vkDestroyBuffer(device_, as.tlasBuf, nullptr);
        if (as.tlasMem != VK_NULL_HANDLE) vkFreeMemory(device_, as.tlasMem, nullptr);
        if (as.blasBuf != VK_NULL_HANDLE) vkDestroyBuffer(device_, as.blasBuf, nullptr);
        if (as.blasMem != VK_NULL_HANDLE) vkFreeMemory(device_, as.blasMem, nullptr);
        if (as.instanceBuf != VK_NULL_HANDLE) vkDestroyBuffer(device_, as.instanceBuf, nullptr);
        if (as.instanceMem != VK_NULL_HANDLE) vkFreeMemory(device_, as.instanceMem, nullptr);
        if (as.scratchBuf != VK_NULL_HANDLE) vkDestroyBuffer(device_, as.scratchBuf, nullptr);
        if (as.scratchMem != VK_NULL_HANDLE) vkFreeMemory(device_, as.scratchMem, nullptr);
    }

    // Compiles + dispatches `shaderSrc` against `tlas` for `rays`, returns per-ray (t, prim).
    void DispatchRays(const char* shaderSrc, VkAccelerationStructureKHR tlas,
                       VkBuffer rayBuf, VkDeviceSize rayBufSize,
                       VkBuffer aabbBuf, VkDeviceSize aabbBufSize,
                       uint32_t rayCount,
                       std::vector<float>& outT, std::vector<uint32_t>& outPrim,
                       std::vector<double>* outDurationsNs = nullptr, int repeats = 1,
                       std::vector<uint32_t>* outCandCount = nullptr,
                       std::vector<uint32_t>* outGenCount = nullptr,
                       std::vector<float>* outCandDebug = nullptr,
                       VkBuffer gridBuf = VK_NULL_HANDLE, VkDeviceSize gridBufSize = 0) {
        ShaderManagement::ShaderCompiler compiler;
        ShaderManagement::CompilationOptions opts;
        opts.sourceLanguage    = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
        opts.targetSpirvVersion = 150;  // SPIR-V 1.5 >= the 1.4 floor ray query needs
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
        ASSERT_TRUE(compOut.success) << compOut.GetFullLog() << "\n--- source ---\n" << shaderSrc;

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = compOut.spirv.size() * sizeof(uint32_t);
        smci.pCode    = compOut.spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(device_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDeviceSize outBufSize = static_cast<VkDeviceSize>(rayCount) * sizeof(float) * 4;
        VkBuffer outBuf = VK_NULL_HANDLE; VkDeviceMemory outMem = VK_NULL_HANDLE;
        CreateBuffer(outBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, outBuf, outMem);
        RQ_GUARD();

        // Debug candidate-trace buffer: 8 vec2 per ray when requested, else a tiny dummy whose
        // length makes the shader's guarded writes no-ops (microbench path pays nothing).
        VkDeviceSize dbgBufSize = outCandDebug
            ? static_cast<VkDeviceSize>(rayCount) * 8 * sizeof(float) * 2
            : sizeof(float) * 2;
        VkBuffer dbgBuf = VK_NULL_HANDLE; VkDeviceMemory dbgMem = VK_NULL_HANDLE;
        CreateBuffer(dbgBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, dbgBuf, dbgMem);
        RQ_GUARD();
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(device_, dbgMem, 0, dbgBufSize, 0, &mapped), VK_SUCCESS);
            std::memset(mapped, 0, static_cast<size_t>(dbgBufSize));
            vkUnmapMemory(device_, dbgMem);
        }

        // Binding 5 (brickmap grid) is always present in the layout even for shaders that don't
        // declare it — bind a tiny dummy buffer when the caller has no real grid, same trick as
        // the debug buffer above.
        bool hasGrid = (gridBuf != VK_NULL_HANDLE);
        VkBuffer dummyGridBuf = VK_NULL_HANDLE; VkDeviceMemory dummyGridMem = VK_NULL_HANDLE;
        if (!hasGrid) {
            CreateBuffer(sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible,
                         dummyGridBuf, dummyGridMem);
            RQ_GUARD();
        }

        bool usesTlas = (tlas != VK_NULL_HANDLE);
        VkDescriptorSetLayoutBinding bindings[6]{};
        uint32_t bindingCount = 0;
        if (usesTlas) {
            bindings[bindingCount].binding = 0;
            bindings[bindingCount].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            bindings[bindingCount].descriptorCount = 1;
            bindings[bindingCount].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindingCount++;
        }
        bindings[bindingCount] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindingCount++;
        bindings[bindingCount] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindingCount++;
        bindings[bindingCount] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindingCount++;
        bindings[bindingCount] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindingCount++;
        bindings[bindingCount] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindingCount++;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = bindingCount;
        dslci.pBindings    = bindings;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts    = &dsl;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, usesTlas ? 1u : 0u},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = usesTlas ? 2 : 1;
        dpci.pPoolSizes    = usesTlas ? poolSizes : &poolSizes[1];
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool     = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(device_, &dsai, &descSet), VK_SUCCESS);

        std::vector<VkWriteDescriptorSet> writes;
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
        if (usesTlas) {
            asWrite.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures    = &tlas;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.pNext = &asWrite;
            w.dstSet = descSet; w.dstBinding = 0; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes.push_back(w);
        }
        VkDescriptorBufferInfo rayInfo{rayBuf, 0, rayBufSize};
        VkDescriptorBufferInfo outInfo{outBuf, 0, outBufSize};
        VkDescriptorBufferInfo aabbInfo{aabbBuf, 0, aabbBufSize};
        VkWriteDescriptorSet w1{}; w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = descSet; w1.dstBinding = 1; w1.descriptorCount = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w1.pBufferInfo = &rayInfo;
        writes.push_back(w1);
        VkWriteDescriptorSet w2{}; w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = descSet; w2.dstBinding = 2; w2.descriptorCount = 1;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w2.pBufferInfo = &outInfo;
        writes.push_back(w2);
        VkWriteDescriptorSet w3{}; w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = descSet; w3.dstBinding = 3; w3.descriptorCount = 1;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w3.pBufferInfo = &aabbInfo;
        writes.push_back(w3);
        VkDescriptorBufferInfo dbgInfo{dbgBuf, 0, dbgBufSize};
        VkWriteDescriptorSet w4{}; w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w4.dstSet = descSet; w4.dstBinding = 4; w4.descriptorCount = 1;
        w4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w4.pBufferInfo = &dbgInfo;
        writes.push_back(w4);
        VkDescriptorBufferInfo gridInfo{hasGrid ? gridBuf : dummyGridBuf, 0, hasGrid ? gridBufSize : sizeof(uint32_t)};
        VkWriteDescriptorSet w5{}; w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w5.dstSet = descSet; w5.dstBinding = 5; w5.descriptorCount = 1;
        w5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w5.pBufferInfo = &gridInfo;
        writes.push_back(w5);
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        const uint32_t groups = (rayCount + 63) / 64;
        std::vector<double> durationsNs;
        durationsNs.reserve(repeats);
        for (int rep = 0; rep < repeats; ++rep) {
            VkCommandBuffer cmd = BeginOneShotCommands();
            if (usesTlas) {
                VkMemoryBarrier asBarrier{};
                asBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                asBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                asBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &asBarrier, 0, nullptr, 0, nullptr);
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

            vkCmdDispatch(cmd, groups, 1, 1);

            VkBufferMemoryBarrier toHost{};
            toHost.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            toHost.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
            toHost.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
            toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHost.buffer              = outBuf;
            toHost.offset              = 0;
            toHost.size                = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                  0, 0, nullptr, 1, &toHost, 0, nullptr);
            vkEndCommandBuffer(cmd);

            VkSubmitInfo si{};
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd;
            // Timed interval is exactly submit -> queue idle (recording excluded).
            auto t0 = std::chrono::high_resolution_clock::now();
            vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue_);
            auto t1 = std::chrono::high_resolution_clock::now();
            vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

            double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
            durationsNs.push_back(ns);
        }
        if (outDurationsNs) *outDurationsNs = durationsNs;

        outT.resize(rayCount);
        outPrim.resize(rayCount);
        if (outCandCount) outCandCount->resize(rayCount);
        if (outGenCount) outGenCount->resize(rayCount);
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, outMem, 0, outBufSize, 0, &mapped), VK_SUCCESS);
        auto* raw = static_cast<float*>(mapped);
        for (uint32_t i = 0; i < rayCount; ++i) {
            outT[i]    = raw[i * 4 + 0];
            uint32_t bits; std::memcpy(&bits, &raw[i * 4 + 1], sizeof(bits));
            outPrim[i] = bits;
            if (outCandCount) (*outCandCount)[i] = static_cast<uint32_t>(raw[i * 4 + 2]);
            if (outGenCount) (*outGenCount)[i]  = static_cast<uint32_t>(raw[i * 4 + 3]);
        }
        vkUnmapMemory(device_, outMem);
        if (outCandDebug) {
            outCandDebug->resize(static_cast<size_t>(rayCount) * 16);
            void* dbgMapped = nullptr;
            ASSERT_EQ(vkMapMemory(device_, dbgMem, 0, dbgBufSize, 0, &dbgMapped), VK_SUCCESS);
            std::memcpy(outCandDebug->data(), dbgMapped, static_cast<size_t>(dbgBufSize));
            vkUnmapMemory(device_, dbgMem);
        }
        vkDeviceWaitIdle(device_);

        vkDestroyDescriptorPool(device_, descPool, nullptr);
        vkDestroyPipeline(device_, pipeline, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        vkDestroyBuffer(device_, outBuf, nullptr);
        vkFreeMemory(device_, outMem, nullptr);
        vkDestroyBuffer(device_, dbgBuf, nullptr);
        vkFreeMemory(device_, dbgMem, nullptr);
        if (!hasGrid) {
            vkDestroyBuffer(device_, dummyGridBuf, nullptr);
            vkFreeMemory(device_, dummyGridMem, nullptr);
        }
    }
};

// ---------------------------------------------------------------------------
// (E/F) Correctness: rayQueryEXT dispatch vs CPU brute-force slab reference over 10000 rays.
// ---------------------------------------------------------------------------
TEST_F(RayQueryFeasibilityTest, GpuRayQueryMatchesCpuBruteForce) {
    std::vector<Aabb> bricks = BuildSphereBrickAabbs();
    ASSERT_GT(bricks.size(), 0u) << "Sphere voxelization produced zero occupied bricks.";

    constexpr uint32_t kRayCount = 10000;
    std::vector<Ray> rays = GenerateRays(kRayCount, /*seed=*/0xC0FFEEULL);

    // --- Upload AABB buffer (also used as BLAS geometry input) ---
    VkDeviceSize aabbBufSize = static_cast<VkDeviceSize>(bricks.size()) * sizeof(Aabb);
    VkBuffer aabbBuf = VK_NULL_HANDLE; VkDeviceMemory aabbMem = VK_NULL_HANDLE;
    CreateBuffer(aabbBufSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        true, kHostVisible, aabbBuf, aabbMem);
    RQ_GUARD();
    {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, aabbMem, 0, aabbBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, bricks.data(), static_cast<size_t>(aabbBufSize));
        vkUnmapMemory(device_, aabbMem);
    }

    AsResult as;
    ASSERT_NO_FATAL_FAILURE(BuildAccelerationStructures(bricks, aabbBuf, as));
    RQ_GUARD();

    // --- Upload rays (origin, dir) as vec4 pairs ---
    VkDeviceSize rayBufSize = static_cast<VkDeviceSize>(kRayCount) * sizeof(float) * 8;  // 2 vec4/ray
    VkBuffer rayBuf = VK_NULL_HANDLE; VkDeviceMemory rayMem = VK_NULL_HANDLE;
    CreateBuffer(rayBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, rayBuf, rayMem);
    RQ_GUARD();
    {
        std::vector<float> packed(kRayCount * 8);
        for (uint32_t i = 0; i < kRayCount; ++i) {
            packed[i * 8 + 0] = rays[i].origin.x; packed[i * 8 + 1] = rays[i].origin.y;
            packed[i * 8 + 2] = rays[i].origin.z; packed[i * 8 + 3] = 0.0f;
            packed[i * 8 + 4] = rays[i].dir.x;    packed[i * 8 + 5] = rays[i].dir.y;
            packed[i * 8 + 6] = rays[i].dir.z;    packed[i * 8 + 7] = 0.0f;
        }
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, rayMem, 0, rayBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, packed.data(), static_cast<size_t>(rayBufSize));
        vkUnmapMemory(device_, rayMem);
    }

    std::vector<float> gpuT;
    std::vector<uint32_t> gpuPrim;
    std::vector<uint32_t> gpuCand, gpuGen;
    std::vector<float> candDebug;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kRayQueryShaderSrc, as.tlas, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, gpuT, gpuPrim,
                                         nullptr, 1, &gpuCand, &gpuGen, &candDebug));
    RQ_GUARD();

    // --- Independent data-path reference: software brute-force shader over the SAME buffers.
    // If this matches the CPU exactly, buffer layout/upload/readback are proven good and any
    // RT_QUERY mismatch is isolated to the BLAS/TLAS/rayQuery path.
    std::vector<float> bruteT;
    std::vector<uint32_t> brutePrim;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kBruteForceShaderSrc, VK_NULL_HANDLE, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, bruteT, brutePrim));
    RQ_GUARD();

    // --- CPU reference + parity check ---
    constexpr float kTolerance = 1e-4f;
    int mismatchCount = 0;
    int bruteMismatchCount = 0;
    struct Mismatch { uint32_t idx; Vec3 origin, dir; float tRt, tCpu; uint32_t primRt, primCpu;
                      uint32_t cand, gen, cpuPass; };
    std::vector<Mismatch> mismatches;

    for (uint32_t i = 0; i < kRayCount; ++i) {
        CpuHit ref = BruteForceCpu(bricks, rays[i].origin, rays[i].dir);
        bool bruteAgrees = (brutePrim[i] == ref.prim) &&
                           (ref.prim == 0xFFFFFFFFu || std::fabs(bruteT[i] - ref.t) <= kTolerance);
        if (!bruteAgrees) bruteMismatchCount++;
        bool bothMiss = (gpuPrim[i] == 0xFFFFFFFFu && ref.prim == 0xFFFFFFFFu);
        bool tClose = std::fabs(gpuT[i] - ref.t) <= kTolerance;
        bool primMatch = (gpuPrim[i] == ref.prim);
        bool tieOk = false;
        if (!primMatch && !bothMiss && gpuPrim[i] != 0xFFFFFFFFu) {
            // Adjacent bricks can share a face: recompute CPU entry-t for the RT-reported prim.
            float tRecompute;
            if (gpuPrim[i] < bricks.size() &&
                SlabTest(bricks[gpuPrim[i]], rays[i].origin, rays[i].dir, tRecompute)) {
                tieOk = std::fabs(tRecompute - ref.t) <= kTolerance;
            }
        }
        bool pass = bothMiss || (tClose && (primMatch || tieOk));
        if (!pass) {
            mismatchCount++;
            if (mismatches.size() < 10) {
                uint32_t cpuPass = 0;
                for (uint32_t b = 0; b < bricks.size(); ++b) {
                    float tTmp;
                    if (SlabTest(bricks[b], rays[i].origin, rays[i].dir, tTmp)) cpuPass++;
                }
                mismatches.push_back({i, rays[i].origin, rays[i].dir, gpuT[i], ref.t,
                                      gpuPrim[i], ref.prim, gpuCand[i], gpuGen[i], cpuPass});
            }
        }
    }

    if (!mismatches.empty()) {
        for (auto& m : mismatches) {
            std::fprintf(stderr,
                "MISMATCH ray[%u] origin=(%.4f,%.4f,%.4f) dir=(%.4f,%.4f,%.4f) "
                "t_rt=%.6f t_cpu=%.6f prim_rt=%u prim_cpu=%u rtCand=%u rtGen=%u cpuSlabPass=%u\n",
                m.idx, m.origin.x, m.origin.y, m.origin.z, m.dir.x, m.dir.y, m.dir.z,
                m.tRt, m.tCpu, m.primRt, m.primCpu, m.cand, m.gen, m.cpuPass);
            std::fprintf(stderr, "  rt candidates seen:");
            for (uint32_t s = 0; s < 8 && s < m.cand; ++s) {
                float prim = candDebug[(static_cast<size_t>(m.idx) * 8 + s) * 2 + 0];
                float t    = candDebug[(static_cast<size_t>(m.idx) * 8 + s) * 2 + 1];
                std::fprintf(stderr, "  [%u]=(prim %.0f, t %.6f)", s, prim, t);
            }
            std::fprintf(stderr, "\n");
        }
    }
    EXPECT_EQ(bruteMismatchCount, 0)
        << bruteMismatchCount << " / " << kRayCount
        << " rays mismatched between the CPU reference and the software BRUTE_AABB shader — "
           "data path (buffers/layout/upload) is suspect, not the rayQuery path.";
    EXPECT_EQ(mismatchCount, 0) << mismatchCount << " / " << kRayCount << " rays mismatched.";

    DestroyAs(as);
    vkDestroyBuffer(device_, rayBuf, nullptr);
    vkFreeMemory(device_, rayMem, nullptr);
    vkDestroyBuffer(device_, aabbBuf, nullptr);
    vkFreeMemory(device_, aabbMem, nullptr);
}

// ---------------------------------------------------------------------------
// (D) Correctness: BRICKMAP_DDA (stackless coarse-grid march) vs CPU brute-force reference,
// same 10000-ray seed as the RT parity test above.
// ---------------------------------------------------------------------------
TEST_F(RayQueryFeasibilityTest, BrickmapDdaMatchesCpuBruteForce) {
    std::vector<Aabb> bricks = BuildSphereBrickAabbs();
    ASSERT_GT(bricks.size(), 0u);
    std::vector<uint32_t> grid = BuildBrickGrid(bricks);

    constexpr uint32_t kRayCount = 10000;
    std::vector<Ray> rays = GenerateRays(kRayCount, /*seed=*/0xC0FFEEULL);

    VkDeviceSize aabbBufSize = static_cast<VkDeviceSize>(bricks.size()) * sizeof(Aabb);
    VkBuffer aabbBuf = VK_NULL_HANDLE; VkDeviceMemory aabbMem = VK_NULL_HANDLE;
    CreateBuffer(aabbBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, aabbBuf, aabbMem);
    RQ_GUARD();
    {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, aabbMem, 0, aabbBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, bricks.data(), static_cast<size_t>(aabbBufSize));
        vkUnmapMemory(device_, aabbMem);
    }

    VkDeviceSize gridBufSize = static_cast<VkDeviceSize>(grid.size()) * sizeof(uint32_t);
    VkBuffer gridBuf = VK_NULL_HANDLE; VkDeviceMemory gridMem = VK_NULL_HANDLE;
    CreateBuffer(gridBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, gridBuf, gridMem);
    RQ_GUARD();
    {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, gridMem, 0, gridBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, grid.data(), static_cast<size_t>(gridBufSize));
        vkUnmapMemory(device_, gridMem);
    }

    VkDeviceSize rayBufSize = static_cast<VkDeviceSize>(kRayCount) * sizeof(float) * 8;
    VkBuffer rayBuf = VK_NULL_HANDLE; VkDeviceMemory rayMem = VK_NULL_HANDLE;
    CreateBuffer(rayBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, rayBuf, rayMem);
    RQ_GUARD();
    {
        std::vector<float> packed(kRayCount * 8);
        for (uint32_t i = 0; i < kRayCount; ++i) {
            packed[i * 8 + 0] = rays[i].origin.x; packed[i * 8 + 1] = rays[i].origin.y;
            packed[i * 8 + 2] = rays[i].origin.z; packed[i * 8 + 3] = 0.0f;
            packed[i * 8 + 4] = rays[i].dir.x;    packed[i * 8 + 5] = rays[i].dir.y;
            packed[i * 8 + 6] = rays[i].dir.z;    packed[i * 8 + 7] = 0.0f;
        }
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, rayMem, 0, rayBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, packed.data(), static_cast<size_t>(rayBufSize));
        vkUnmapMemory(device_, rayMem);
    }

    std::vector<float> ddaT;
    std::vector<uint32_t> ddaPrim;
    std::vector<uint32_t> ddaStepCount;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kBrickmapDdaShaderSrc, VK_NULL_HANDLE, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, ddaT, ddaPrim,
                                         nullptr, 1, &ddaStepCount, nullptr, nullptr,
                                         gridBuf, gridBufSize));
    RQ_GUARD();

    constexpr float kTolerance = 1e-4f;
    int mismatchCount = 0;
    struct Mismatch { uint32_t idx; Vec3 origin, dir; float tDda, tCpu; uint32_t primDda, primCpu, steps; };
    std::vector<Mismatch> mismatches;

    for (uint32_t i = 0; i < kRayCount; ++i) {
        CpuHit ref = BruteForceCpu(bricks, rays[i].origin, rays[i].dir);
        bool bothMiss = (ddaPrim[i] == 0xFFFFFFFFu && ref.prim == 0xFFFFFFFFu);
        bool tClose = std::fabs(ddaT[i] - ref.t) <= kTolerance;
        bool primMatch = (ddaPrim[i] == ref.prim);
        bool tieOk = false;
        if (!primMatch && !bothMiss && ddaPrim[i] != 0xFFFFFFFFu) {
            // Boundary-graze ties between adjacent bricks: recompute CPU entry-t for the
            // DDA-reported prim.
            float tRecompute;
            if (ddaPrim[i] < bricks.size() &&
                SlabTest(bricks[ddaPrim[i]], rays[i].origin, rays[i].dir, tRecompute)) {
                tieOk = std::fabs(tRecompute - ref.t) <= kTolerance;
            }
        }
        bool pass = bothMiss || (tClose && (primMatch || tieOk));
        if (!pass) {
            mismatchCount++;
            if (mismatches.size() < 10) {
                mismatches.push_back({i, rays[i].origin, rays[i].dir, ddaT[i], ref.t,
                                      ddaPrim[i], ref.prim, ddaStepCount[i]});
            }
        }
    }

    for (auto& m : mismatches) {
        std::fprintf(stderr,
            "MISMATCH ray[%u] origin=(%.4f,%.4f,%.4f) dir=(%.4f,%.4f,%.4f) "
            "t_dda=%.6f t_cpu=%.6f prim_dda=%u prim_cpu=%u steps=%u\n",
            m.idx, m.origin.x, m.origin.y, m.origin.z, m.dir.x, m.dir.y, m.dir.z,
            m.tDda, m.tCpu, m.primDda, m.primCpu, m.steps);
    }
    EXPECT_EQ(mismatchCount, 0) << mismatchCount << " / " << kRayCount << " rays mismatched.";

    vkDestroyBuffer(device_, rayBuf, nullptr);
    vkFreeMemory(device_, rayMem, nullptr);
    vkDestroyBuffer(device_, aabbBuf, nullptr);
    vkFreeMemory(device_, aabbMem, nullptr);
    vkDestroyBuffer(device_, gridBuf, nullptr);
    vkFreeMemory(device_, gridMem, nullptr);
}

// ---------------------------------------------------------------------------
// (G) Report-only microbench: RT_QUERY vs BRUTE_AABB software floor, 1M rays, 10 dispatches,
// min/median ns-per-ray printed to stdout. No perf assertions.
// ---------------------------------------------------------------------------
TEST_F(RayQueryFeasibilityTest, Microbench_RtQueryVsBruteForce) {
    std::vector<Aabb> bricks = BuildSphereBrickAabbs();
    ASSERT_GT(bricks.size(), 0u);
    std::vector<uint32_t> grid = BuildBrickGrid(bricks);

    constexpr uint32_t kRayCount = 1000000;
    std::vector<Ray> rays = GenerateRays(kRayCount, /*seed=*/0xB16B00B5ULL);

    VkDeviceSize aabbBufSize = static_cast<VkDeviceSize>(bricks.size()) * sizeof(Aabb);
    VkBuffer aabbBuf = VK_NULL_HANDLE; VkDeviceMemory aabbMem = VK_NULL_HANDLE;
    CreateBuffer(aabbBufSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        true, kHostVisible, aabbBuf, aabbMem);
    RQ_GUARD();
    {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, aabbMem, 0, aabbBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, bricks.data(), static_cast<size_t>(aabbBufSize));
        vkUnmapMemory(device_, aabbMem);
    }

    AsResult as;
    ASSERT_NO_FATAL_FAILURE(BuildAccelerationStructures(bricks, aabbBuf, as));
    RQ_GUARD();

    VkDeviceSize rayBufSize = static_cast<VkDeviceSize>(kRayCount) * sizeof(float) * 8;
    VkBuffer rayBuf = VK_NULL_HANDLE; VkDeviceMemory rayMem = VK_NULL_HANDLE;
    CreateBuffer(rayBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, rayBuf, rayMem);
    RQ_GUARD();
    {
        std::vector<float> packed(kRayCount * 8);
        for (uint32_t i = 0; i < kRayCount; ++i) {
            packed[i * 8 + 0] = rays[i].origin.x; packed[i * 8 + 1] = rays[i].origin.y;
            packed[i * 8 + 2] = rays[i].origin.z; packed[i * 8 + 3] = 0.0f;
            packed[i * 8 + 4] = rays[i].dir.x;    packed[i * 8 + 5] = rays[i].dir.y;
            packed[i * 8 + 6] = rays[i].dir.z;    packed[i * 8 + 7] = 0.0f;
        }
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, rayMem, 0, rayBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, packed.data(), static_cast<size_t>(rayBufSize));
        vkUnmapMemory(device_, rayMem);
    }

    VkDeviceSize gridBufSize = static_cast<VkDeviceSize>(grid.size()) * sizeof(uint32_t);
    VkBuffer gridBuf = VK_NULL_HANDLE; VkDeviceMemory gridMem = VK_NULL_HANDLE;
    CreateBuffer(gridBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, kHostVisible, gridBuf, gridMem);
    RQ_GUARD();
    {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(device_, gridMem, 0, gridBufSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, grid.data(), static_cast<size_t>(gridBufSize));
        vkUnmapMemory(device_, gridMem);
    }

    constexpr int kRepeats = 10;
    std::vector<float> t1; std::vector<uint32_t> p1;
    std::vector<double> rtQueryDurationsNs;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kRayQueryShaderSrc, as.tlas, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, t1, p1,
                                         &rtQueryDurationsNs, kRepeats));
    RQ_GUARD();

    std::vector<float> t2; std::vector<uint32_t> p2;
    std::vector<double> bruteDurationsNs;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kBruteForceShaderSrc, VK_NULL_HANDLE, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, t2, p2,
                                         &bruteDurationsNs, kRepeats));
    RQ_GUARD();

    std::vector<float> t3; std::vector<uint32_t> p3;
    std::vector<double> ddaDurationsNs;
    ASSERT_NO_FATAL_FAILURE(DispatchRays(kBrickmapDdaShaderSrc, VK_NULL_HANDLE, rayBuf, rayBufSize,
                                         aabbBuf, aabbBufSize, kRayCount, t3, p3,
                                         &ddaDurationsNs, kRepeats, nullptr, nullptr, nullptr,
                                         gridBuf, gridBufSize));
    RQ_GUARD();

    auto minMedian = [](std::vector<double> v) -> std::pair<double, double> {
        std::sort(v.begin(), v.end());
        return {v.front(), v[v.size() / 2]};
    };
    auto [rtMin, rtMedian] = minMedian(rtQueryDurationsNs);
    auto [bruteMin, bruteMedian] = minMedian(bruteDurationsNs);
    auto [ddaMin, ddaMedian] = minMedian(ddaDurationsNs);

    std::printf("[RT_QUERY]   min ns/dispatch=%.0f  median ns/dispatch=%.0f  "
                "min ns/ray=%.2f  median ns/ray=%.2f  (rays=%u, dispatches=%d)\n",
                rtMin, rtMedian, rtMin / kRayCount, rtMedian / kRayCount, kRayCount, kRepeats);
    std::printf("[BRUTE_AABB] min ns/dispatch=%.0f  median ns/dispatch=%.0f  "
                "min ns/ray=%.2f  median ns/ray=%.2f  (rays=%u, dispatches=%d)\n",
                bruteMin, bruteMedian, bruteMin / kRayCount, bruteMedian / kRayCount, kRayCount, kRepeats);
    std::printf("[BRICKMAP_DDA] min ns/dispatch=%.0f  median ns/dispatch=%.0f  "
                "min ns/ray=%.2f  median ns/ray=%.2f  (rays=%u, dispatches=%d)\n",
                ddaMin, ddaMedian, ddaMin / kRayCount, ddaMedian / kRayCount, kRayCount, kRepeats);

    DestroyAs(as);
    vkDestroyBuffer(device_, rayBuf, nullptr);
    vkFreeMemory(device_, rayMem, nullptr);
    vkDestroyBuffer(device_, aabbBuf, nullptr);
    vkFreeMemory(device_, aabbMem, nullptr);
    vkDestroyBuffer(device_, gridBuf, nullptr);
    vkFreeMemory(device_, gridMem, nullptr);
}
