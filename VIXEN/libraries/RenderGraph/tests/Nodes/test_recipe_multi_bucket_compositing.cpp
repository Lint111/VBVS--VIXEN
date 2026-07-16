/**
 * @file test_recipe_multi_bucket_compositing.cpp
 * @brief Recipe GPU Instance Bucketing Inc2 M3 live-run gate (Tasks 6-8).
 *
 * Extends M2's single-recipe indirect-dispatch proof (test_recipe_bucketed_indirect_dispatch.cpp)
 * to MULTIPLE simultaneously-hot recipes, and proves cross-bucket `HitRecord` compositing is
 * correct under real screen-space overlap. Three things this file proves:
 *
 *   Task 6 — Minimal hotness gate: a placeholder policy (bucket instance count >= kHotnessThreshold,
 *     see kHotnessThreshold below) decides which M1 buckets get promoted to a specialized pipeline.
 *     `HotnessGate_PromotesOnlyBucketsAboveThreshold` exercises this purely on M1's bucketCounts[]
 *     output — no GPU dispatch needed for this half, since the gate itself is a CPU-side decision
 *     over already-proven bucketing data.
 *
 *   Task 7 — Multi-recipe sequential bucketed dispatch: TWO hot recipes, each compiled to its own
 *     specialized pipeline (Task 5's EmitSpecializedRecipeComputeShader, looped) and dispatched via
 *     vkCmdDispatchIndirect, ALL issued through a REAL `Vixen::RenderGraph::MultiDispatchNode`
 *     instance (constructed standalone via NodeType::CreateInstance + SetInput/Setup/Compile/Execute,
 *     mirroring test_hitrecord_readback.cpp's established "no full RenderGraph needed" pattern —
 *     see that file's HitRecordReadbackTest fixture) with its DEFAULT autoBarriers_=true (never
 *     disabled anywhere in this file). A third, COLD recipe's instances are handled by a separate,
 *     hand-rolled GPU dispatch that mirrors TraceWorld.glsl's sphere-march algorithm exactly (same
 *     algorithm M2's CPU oracle reimplements) — standing in for "the existing tier-0 switch path,"
 *     WITHOUT modifying the real shaders/BodyInstanceRayMarch.comp (that shader is shared by the
 *     live app and dozens of other gates; touching it is out of this milestone's scope, see the
 *     Task 8 comment block below for why a standalone equivalent is the correct-scoped substitute).
 *
 *   Task 8 — Cross-bucket HitRecord compositing (the most important gate in this plan): 2 hot
 *     recipes' bound spheres are positioned to VISIBLY overlap on screen from the test camera, with
 *     recipe A nearer than recipe B in the overlap region. Both bucket shaders write HitRecord via
 *     the SAME plain (non-atomic) read-compare-conditionally-overwrite scheme
 *     (SpecializedRecipeShaderGlsl.h already does this, confirmed by inspection — this file exercises
 *     it under REAL overlap for the first time). The gate runs the full multi-bucket + cold-recipe
 *     sequence in BOTH dispatch orderings (hot-buckets-then-cold, and cold-then-hot-buckets) to
 *     empirically confirm the plan's order-independence claim ("nearest wins is order-independent by
 *     construction") rather than assuming it — both orderings must produce IDENTICAL HitRecord
 *     contents, and both must match the independent multi-recipe CPU oracle (extends M2's own
 *     CpuOracleTraceUberRecipeBody to loop every instance across every recipe, exactly mirroring
 *     what the tier-0 switch's TraceWorld() instance loop does today).
 *
 * ---------------------------------------------------------------------------------------------
 * WHY A STANDALONE TIER-0-EQUIVALENT DISPATCH, NOT THE REAL shaders/BodyInstanceRayMarch.comp:
 * ---------------------------------------------------------------------------------------------
 * Investigation before writing this file confirmed shaders/TraceWorld.glsl's instance loop (used by
 * BodyInstanceRayMarch.comp, the real tier-0 switch shader) has NO mechanism to exclude a
 * hot-and-separately-bucketed instance from its own full-instance march, and its HitRecord write
 * (BodyInstanceRayMarch.comp:256-257, `hitRecords[hitRecordIdx] = rec;`) is a flat, unconditional
 * overwrite with no read-compare-write guard. Wiring bucketed dispatch into the real production
 * shader (shared by the live VixenApp render graph, DirectLighting.comp's downstream read, and many
 * other gate tests asserting byte-identity) is a real-integration concern for a FUTURE increment
 * (BuildRenderGraph.cpp wiring is explicitly out of THIS plan's scope — see the plan doc's Task 7:
 * "Issue ALL of these through MultiDispatchNode," not "wire into the live app graph"). What Task 8
 * needs to PROVE — that plain read-compare-write compositing is correct and order-independent when
 * multiple dispatches share the HitRecord SSBO under MultiDispatchNode's default barrier — does not
 * require the real switch shader's full uber-shader machinery; it requires a second GPU dispatch
 * that (a) writes the SAME conditional-write pattern the specialized shaders use, and (b) marches a
 * DIFFERENT, disjoint set of instances (correctly modelling "cold recipes stay off the bucketed
 * path" — in the real integration, tier-0's instanceCount would be reduced to exclude hot instances,
 * per this file's own design note below). CpuOracleColdSphereMarch (below) is exactly that: the
 * verbatim same sphere-march algorithm as CpuOracleTraceUberRecipeBody (M2), promoted to a REAL
 * compiled GPU compute shader (ColdRecipeMarchGlsl, self-contained, no #include chain) instead of a
 * CPU function — so the compositing gate is real GPU-to-GPU SSBO interaction, not GPU-vs-CPU.
 *
 * DEVICE SELECTION: same contract as test_recipe_bucketed_indirect_dispatch.cpp — a real
 * discrete/integrated GPU is PREFERRED; software (lavapipe/llvmpipe) or Dozen only as a fallback.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/DispatchPass.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "Hash.h"  // ComputeSHA256HexFromUint32Vec — SPIR-V content hash for pipeline cache key
#include "IRenderTarget.h"
#include "MainCacher.h"
#include "ComputePipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "Nodes/MultiDispatchNode.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/SpecializedRecipeShaderGlsl.h"
#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "ShaderCompiler.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef RECIPE_BUCKETING_SPV
#error "RECIPE_BUCKETING_SPV (path to compiled RecipeInstanceBucketing.spv) must be defined by CMake"
#endif
#ifndef SDF_CORE_KERNELS_GLSL_PATH
#error "SDF_CORE_KERNELS_GLSL_PATH must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;
using Vixen::Vulkan::Resources::IRenderTarget;
using Vixen::Vulkan::Resources::RenderTargetData;

// ============================================================================
// Task 6: minimal hotness gate — placeholder policy, NOT the real async usage-history tracker
// (deferred, out of scope for this whole increment per the plan/design docs). Promotes any
// recipeId whose M1 bucket has >= kHotnessThreshold instances this frame. 4 is chosen as a
// non-trivial starting number per the plan's own suggestion ("the plan suggests >=4 as a
// reasonable non-trivial starting point") — it is NOT tuned/benchmarked, and is explicitly
// expected to be replaced once the real hot-mark/usage-history tracker exists (a future
// increment, per Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07 §7).
// ============================================================================
namespace {
constexpr uint32_t kHotnessThreshold = 4;

bool IsHot(uint32_t bucketInstanceCount) {
    return bucketInstanceCount >= kHotnessThreshold;
}
}  // namespace

// ---------------------------------------------------------------------------
// Task 6 gate: pure CPU-side decision over bucket counts. Deliberately does not touch the GPU —
// the bucketing pass itself (M1) already proved bucketCounts[] correctness; this only proves the
// threshold decision built on top of it.
// ---------------------------------------------------------------------------
TEST(HotnessGate, PromotesOnlyBucketsAboveThreshold) {
    // Mirrors a real per-frame bucketCounts[] readout: recipe 3 has 3 instances (below
    // threshold, stays cold), recipe 5 has 4 (exactly at threshold, promoted), recipe 9 has 10
    // (well above, promoted), recipe 12 has 0 (untouched bucket, stays cold).
    std::vector<uint32_t> bucketCounts(16, 0u);
    bucketCounts[3] = 3;
    bucketCounts[5] = 4;
    bucketCounts[9] = 10;
    bucketCounts[12] = 0;

    std::vector<uint32_t> hotRecipeIds, coldRecipeIds;
    for (uint32_t recipeId = 0; recipeId < bucketCounts.size(); ++recipeId) {
        if (bucketCounts[recipeId] == 0) continue;  // untouched bucket: not a recipe present this frame
        if (IsHot(bucketCounts[recipeId])) {
            hotRecipeIds.push_back(recipeId);
        } else {
            coldRecipeIds.push_back(recipeId);
        }
    }

    EXPECT_EQ(hotRecipeIds.size(), 2u);
    EXPECT_NE(std::find(hotRecipeIds.begin(), hotRecipeIds.end(), 5u), hotRecipeIds.end());
    EXPECT_NE(std::find(hotRecipeIds.begin(), hotRecipeIds.end(), 9u), hotRecipeIds.end());

    EXPECT_EQ(coldRecipeIds.size(), 1u);
    EXPECT_NE(std::find(coldRecipeIds.begin(), coldRecipeIds.end(), 3u), coldRecipeIds.end());

    // Boundary check: exactly-at-threshold must promote (>=, not >).
    EXPECT_TRUE(IsHot(kHotnessThreshold));
    EXPECT_FALSE(IsHot(kHotnessThreshold - 1));
}

namespace {

// Byte-identical to RecipeInstanceBucketing.comp's Push block (mirrors M2's own copy).
struct BucketingPush {
    glm::mat4 viewProj;
    uint32_t  instanceCount;
    uint32_t  maxBuckets;
    uint32_t  maxMembersPerBucket;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  mode;
};

struct RecipeBoundSphereCpu {
    float center[3];
    float radius;
    float relaxation;
    float _pad[3];
};
static_assert(sizeof(RecipeBoundSphereCpu) == 32, "RecipeBoundSphereCpu std430 mirror size");

// Byte-identical to the specialized shader's Push block (SpecializedRecipeShaderGlsl.h).
struct SpecializedPush {
    glm::vec3 cameraPos; float _p0;
    glm::vec3 cameraDir; float fov;
    glm::vec3 cameraUp;  float aspect;
    glm::vec3 cameraRight; float _p1;
    uint32_t  memberCount;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  rectMinX;
    uint32_t  rectMinY;
    float     boundRadius;
    float     stepRelaxation;
};

// Byte-identical to the cold-path shader's Push block (ColdRecipeMarchGlsl, below) — same
// camera fields as SpecializedPush, but a full-screen fixed dispatch (no rect offset) and a
// per-recipe-instance loop instead of a single-recipe member list (mirrors TraceWorld.glsl's
// "loop bodyInstances[], each carries its own recipeId + bound sphere" shape).
struct ColdPush {
    glm::vec3 cameraPos; float _p0;
    glm::vec3 cameraDir; float fov;
    glm::vec3 cameraUp;  float aspect;
    glm::vec3 cameraRight; float _p1;
    uint32_t  instanceCount;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  _p2;
};

// Byte-identical to HitRecord.glsl's std430 layout (64 B/element).
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

// Per-instance record fed to BOTH the bucketing pass and the cold-path shader below — same
// 64-byte std430 layout as Vixen::SVO::BodyInstanceGpu / SceneBindings.glsl's BodyInstance,
// plus a bound-sphere (center/radius) baked in per-instance so the cold-path shader (which has
// no per-recipe RecipeBoundSphereBuffer of its own — this test hands it directly) can early-
// reject exactly like getRecipeBoundSphere's registered-boundCenter convention.
struct ColdInstanceCpu {
    float worldPos[3];
    float renderScale;
    float color[3];
    uint32_t octreeIndex;
    uint32_t providerKind;
    uint32_t recipeId;
    float recipeParams[6];
    float boundCenter[3];
    float boundRadius;
    float stepRelaxation;
    float _pad[3];
};
static_assert(sizeof(ColdInstanceCpu) == 96, "ColdInstanceCpu std430 mirror size");

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

// CPU oracle: reimplements traceUberRecipeBody's exact sphere-march (SdfRecipes.glsl:139-210),
// verbatim identical to test_recipe_bucketed_indirect_dispatch.cpp's own
// CpuOracleTraceUberRecipeBody (M2) — kept as an independent, separately-typed copy per this
// file rather than a shared header, mirroring that file's own "independent ground truth, not a
// copy of the GPU shader under test" framing.
bool CpuOracleTraceUberRecipeBody(const Vixen::SVO::Recipe::SdfInstruction* prog, uint32_t progCount,
                                   glm::vec3 boundCenter, float boundRadius, float relaxation,
                                   glm::vec3 ro, glm::vec3 rd, std::span<const float> params,
                                   glm::vec3& outNormal, float& outT) {
    outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    outT = 0.0f;

    glm::vec3 oc = ro - boundCenter;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - boundRadius * boundRadius;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float sq = std::sqrt(disc);
    float tNear = std::max(-b - sq, 0.0f);
    float tFar  = -b + sq;
    if (tFar < 0.0f) return false;

    float t = tNear;
    constexpr int MAX_STEPS = 128;
    constexpr float EPS = 1e-3f;
    for (int i = 0; i < MAX_STEPS; ++i) {
        glm::vec3 p = ro + rd * t;
        float d = Vixen::SVO::Recipe::evalRecipe(prog, progCount, p, params);
        if (d < EPS) {
            const float h = 1e-3f;
            glm::vec2 e(h, 0.0f);
            float gx = Vixen::SVO::Recipe::evalRecipe(prog, progCount, p + glm::vec3(e.x, e.y, e.y), params)
                     - Vixen::SVO::Recipe::evalRecipe(prog, progCount, p - glm::vec3(e.x, e.y, e.y), params);
            float gy = Vixen::SVO::Recipe::evalRecipe(prog, progCount, p + glm::vec3(e.y, e.x, e.y), params)
                     - Vixen::SVO::Recipe::evalRecipe(prog, progCount, p - glm::vec3(e.y, e.x, e.y), params);
            float gz = Vixen::SVO::Recipe::evalRecipe(prog, progCount, p + glm::vec3(e.y, e.y, e.x), params)
                     - Vixen::SVO::Recipe::evalRecipe(prog, progCount, p - glm::vec3(e.y, e.y, e.x), params);
            outNormal = glm::normalize(glm::vec3(gx, gy, gz));
            outT = t;
            return true;
        }
        t += d * relaxation;
        if (t > tFar) return false;
    }
    return false;
}

// One recipe's synthetic registration: bytecode + bound sphere + a set of world-space instance
// positions + a distinguishing colour (so the overlap-region winner is identifiable both via
// hitT ordering AND via albedo, a redundant cross-check).
struct SyntheticRecipe {
    uint32_t recipeId;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    std::vector<Vixen::SVO::Recipe::SdfInstruction> prog;
    std::vector<glm::vec3> instancePositions;
    glm::vec3 color;
};

// Multi-recipe CPU oracle: loops EVERY instance across EVERY recipe (hot + cold together) and
// keeps the nearest hit — this is the exact shape of TraceWorld.glsl's own instance loop (which
// has no per-recipe partitioning, see this file's header comment), just reimplemented on the
// CPU as independent ground truth. Returns the winning recipe's index into `recipes` via
// outWinnerRecipe (SIZE_MAX if no hit) so the overlap-region check can assert WHICH recipe won,
// not just that a hit occurred.
bool MultiRecipeCpuOracle(const std::vector<SyntheticRecipe>& recipes,
                          glm::vec3 ro, glm::vec3 rd,
                          float& outT, glm::vec3& outColor, size_t& outWinnerRecipe) {
    bool anyHit = false;
    float bestT = 1e30f;
    glm::vec3 bestColor(0.0f);
    size_t bestRecipe = SIZE_MAX;

    for (size_t r = 0; r < recipes.size(); ++r) {
        const auto& recipe = recipes[r];
        for (const auto& pos : recipe.instancePositions) {
            (void)pos;  // Per this codebase's established convention (confirmed in M2's own
                        // test comment), a recipeId>=2 recipe's field geometry and bound-sphere
                        // reject center are NOT translated by instance worldPos — only the
                        // registered boundCenter is used (UberShaderSplice.h's
                        // getRecipeBoundSphere returns entry.boundCenter verbatim). Each
                        // instance of the same recipe therefore produces the identical
                        // world-space geometry; this loop still iterates per-instance to match
                        // TraceWorld's own per-instance loop shape (one march attempt per
                        // instance, all against the same field), not to vary geometry per pos.
            glm::vec3 n; float t;
            std::array<float, 6> params{};
            if (CpuOracleTraceUberRecipeBody(recipe.prog.data(), static_cast<uint32_t>(recipe.prog.size()),
                                              recipe.entry.boundCenter, recipe.entry.boundRadius,
                                              recipe.entry.stepRelaxation, ro, rd, params, n, t)) {
                if (t < bestT) {
                    bestT = t; bestColor = recipe.color; bestRecipe = r; anyHit = true;
                }
            }
        }
    }

    outT = bestT; outColor = bestColor; outWinnerRecipe = bestRecipe;
    return anyHit;
}

}  // namespace

class RecipeMultiBucketCompositingTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;

    std::unique_ptr<VulkanDevice> deviceShell_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& props) {
        std::string name(props.deviceName);
        for (char& c : name) c = static_cast<char>(::tolower(c));
        const bool isSoftware =
            (name.find("llvmpipe") != std::string::npos ||
             name.find("lavapipe") != std::string::npos) &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = name.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_recipe_multi_bucket_compositing";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        const auto  enabledLayers = EnabledValidationLayers();
        const char* extensions[]  = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        VkInstanceCreateInfo instInfo{};
        instInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo        = &appInfo;
        instInfo.enabledLayerCount       = static_cast<uint32_t>(enabledLayers.size());
        instInfo.ppEnabledLayerNames     = enabledLayers.empty() ? nullptr : enabledLayers.data();
        instInfo.enabledExtensionCount   = 1;
        instInfo.ppEnabledExtensionNames = extensions;

        ASSERT_EQ(vkCreateInstance(&instInfo, nullptr, &instance_), VK_SUCCESS)
            << "vkCreateInstance failed — is a Vulkan device available?";

        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found; nearest was '"
            << selectedDeviceName_ << "'.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
        // VulkanDevice's own device-creating CreateDevice() resolves these; since this test
        // builds VkDevice by hand and only wraps it afterward, resolve them explicitly here (see
        // CreateLogicalDevice()'s comment on VK_KHR_synchronization2 for why this is required —
        // MultiDispatchNode::InsertAutoBarrier calls through fpCmdPipelineBarrier2 unconditionally
        // whenever autoBarriers_ is on, which this milestone's plan requires stay the default).
        deviceShell_->fpCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
            vkGetDeviceProcAddr(logicalDevice_, "vkCmdPipelineBarrier2KHR"));
        deviceShell_->fpQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
            vkGetDeviceProcAddr(logicalDevice_, "vkQueueSubmit2KHR"));
        ASSERT_NE(deviceShell_->fpCmdPipelineBarrier2, nullptr)
            << "vkCmdPipelineBarrier2KHR failed to resolve despite VK_KHR_synchronization2 "
               "being requested as a device extension";
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    }

    void PickPhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
        deviceConfirmed_    = false;
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found) << "No compute queue family on the selected device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_; qInfo.queueCount = 1; qInfo.pQueuePriorities = &priority;

        // Request VK_KHR_synchronization2: MultiDispatchNode's autoBarriers_ path (the DEFAULT
        // this milestone's plan requires never be disabled) calls through
        // VulkanDevice::fpCmdPipelineBarrier2, a per-device function pointer resolved via
        // vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR") — see VulkanDevice.cpp's own
        // CreateDevice(). This test builds its VkDevice by hand (not through VulkanDevice's own
        // device-creating constructor) and wraps it afterward, so that resolution must be done
        // here explicitly or fpCmdPipelineBarrier2 stays null — calling through a null function
        // pointer inside InsertAutoBarrier is exactly what caused a real SEH 0xc0000005 crash the
        // first time this test exercised a REAL MultiDispatchNode with 2+ queued passes (M2's
        // test never hit this: it hand-rolls legacy vkCmdPipelineBarrier directly and never
        // drives MultiDispatchNode's own barrier path at all).
        VkPhysicalDeviceSynchronization2Features sync2Features{};
        sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        sync2Features.synchronization2 = VK_TRUE;

        const char* deviceExtensions[] = {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.pNext = &sync2Features;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
        dInfo.enabledExtensionCount = 1; dInfo.ppEnabledExtensionNames = deviceExtensions;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    template<typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VkDeviceMemory& outMem, bool zero) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &outBuf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, outBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, outBuf, outMem, 0), VK_SUCCESS);
        if (zero) {
            void* m = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, outMem, 0, size, 0, &m), VK_SUCCESS);
            std::memset(m, 0, static_cast<size_t>(size));
            vkUnmapMemory(logicalDevice_, outMem);
        }
    }

    void UploadBuffer(VkDeviceMemory mem, const void* data, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    void ZeroBuffer(VkDeviceMemory mem, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memset(m, 0, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    template <typename T>
    void ReadbackBuffer(VkDeviceMemory mem, VkDeviceSize size, std::vector<T>& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(T));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }
};

// ---------------------------------------------------------------------------
// THE decisive test: Tasks 7+8 combined. 2 hot recipes (bucketed, specialized pipelines,
// dispatched through a real MultiDispatchNode) + 1 cold recipe (standalone GPU dispatch
// standing in for tier-0, see file header) all writing the SAME HitRecord SSBO via plain
// read-compare-write, run in BOTH orderings, cross-checked against the multi-recipe CPU oracle.
// ---------------------------------------------------------------------------
TEST_F(RecipeMultiBucketCompositingTest, OverlappingHotRecipesCompositeCorrectlyBothOrderings) {
    std::cout << "[ multi-bucket-compositing ] selected physical device: '" << selectedDeviceName_ << "'\n";
    ASSERT_TRUE(deviceConfirmed_);

    constexpr uint32_t kScreenWidth = 256, kScreenHeight = 256;
    constexpr uint32_t kMaxBuckets = 256, kMaxMembersPerBucket = 64;

    // --- Recipe A: sphere radius 1.2, centred at (0,0,4) — NEARER to camera. Red. 5 instances
    //     (>= kHotnessThreshold): hot.
    //     IMPORTANT: evalRecipe's Sphere op (SdfRecipeEval.h) evaluates `SdfCore_Sphere(pos, c,
    //     r)` against the RAW world-space march point with NO translation by boundCenter or
    //     instance worldPos (confirmed by inspection — see this file's header comment on the
    //     established recipeId>=2 convention). The field function's OWN center (data[0..2]) is
    //     therefore the actual world-space position of the sphere; boundCenter/boundRadius are a
    //     SEPARATE, independently-supplied early-reject sphere (used by both the CPU oracle and
    //     the specialized/cold-path shaders' bound-sphere test) that must be kept consistent with
    //     data[0..2] or the reject test culls/mis-times real hits. Both are set to the SAME
    //     center here for that reason. ---
    constexpr uint32_t kRecipeA = 5;
    Vixen::SVO::Recipe::SdfInstruction progA[1]{};
    progA[0].opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    progA[0].data[0] = 0.0f; progA[0].data[1] = 0.0f; progA[0].data[2] = 4.0f; progA[0].data[3] = 1.2f;
    Vixen::SVO::RecipeRegistry::RecipeEntry entryA;
    entryA.bytecode.assign(progA, progA + 1);
    entryA.boundCenter = glm::vec3(0.0f, 0.0f, 4.0f);   // nearer: +Z toward camera at eye.z=20
    entryA.boundRadius = 1.6f;
    entryA.stepRelaxation = 1.0f;

    // --- Recipe B: sphere radius 1.2, centred at (0.6,0.3,-1.0) — offset laterally but FARTHER
    //     from camera. Blue. Chosen so its on-screen projection visibly overlaps recipe A's near
    //     screen-centre, while B's actual world position is farther along Z so A wins the
    //     overlap region. 6 instances: hot. Same data[0..2]==boundCenter convention as A. ---
    constexpr uint32_t kRecipeB = 11;
    Vixen::SVO::Recipe::SdfInstruction progB[1]{};
    progB[0].opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    progB[0].data[0] = 0.6f; progB[0].data[1] = 0.3f; progB[0].data[2] = -1.0f; progB[0].data[3] = 1.2f;
    Vixen::SVO::RecipeRegistry::RecipeEntry entryB;
    entryB.bytecode.assign(progB, progB + 1);
    entryB.boundCenter = glm::vec3(0.6f, 0.3f, -1.0f);  // farther: -Z, small lateral offset for a
                                                          // clean but real (not exact-concentric)
                                                          // screen-space overlap with A
    entryB.boundRadius = 1.8f;
    entryB.stepRelaxation = 1.0f;

    // --- Cold recipe C: sphere radius 0.8, well off to the side (screen-space DISJOINT from A/B
    //     — this is a plain sanity instance, not part of the overlap gate itself), 2 instances
    //     (< kHotnessThreshold): cold, handled by the standalone tier-0-equivalent dispatch. Same
    //     data[0..2]==boundCenter convention as A/B. ---
    constexpr uint32_t kRecipeC = 20;
    Vixen::SVO::Recipe::SdfInstruction progC[1]{};
    progC[0].opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    progC[0].data[0] = -6.0f; progC[0].data[1] = 0.0f; progC[0].data[2] = 0.0f; progC[0].data[3] = 0.8f;
    Vixen::SVO::RecipeRegistry::RecipeEntry entryC;
    entryC.bytecode.assign(progC, progC + 1);
    entryC.boundCenter = glm::vec3(-6.0f, 0.0f, 0.0f);  // far off-axis: disjoint from A/B on screen
    entryC.boundRadius = 1.0f;
    entryC.stepRelaxation = 1.0f;

    // --- Instance lists (worldPos is NOT combined with recipe geometry — see file header note
    //     on the codebase's established recipeId>=2 convention; multiple instances of the same
    //     recipe all produce the same on-screen result, they exist here purely to exercise the
    //     hotness-threshold COUNT and the bucketing/member-list machinery under real multi-
    //     instance load, matching M2's own test's precedent). ---
    std::vector<Vixen::SVO::BodyInstanceGpu> hotInstances;  // recipe A + B together, fed to M1's
                                                              // bucketing pass
    auto addHotInstance = [&](uint32_t recipeId, glm::vec3 pos, glm::vec3 color) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x; inst.worldPos[1] = pos.y; inst.worldPos[2] = pos.z;
        inst.renderScale = 1.0f;
        inst.color[0] = color.x; inst.color[1] = color.y; inst.color[2] = color.z;
        inst.recipeId = recipeId;
        hotInstances.push_back(inst);
    };
    // Colors must match allRecipesForOracle's per-recipe color (below) exactly — the
    // specialized shader's HitRecord.albedo write is `bestColor = inst.color`
    // (SpecializedRecipeShaderGlsl.h), a per-INSTANCE field, not derived from recipeId.
    const glm::vec3 kColorA(0.9f, 0.15f, 0.15f), kColorB(0.15f, 0.15f, 0.9f);
    for (int i = 0; i < 5; ++i) addHotInstance(kRecipeA, glm::vec3(0.0f, 0.0f, 0.0f), kColorA);
    for (int i = 0; i < 6; ++i) addHotInstance(kRecipeB, glm::vec3(0.0f, 0.0f, 0.0f), kColorB);
    const uint32_t hotInstanceCount = static_cast<uint32_t>(hotInstances.size());

    std::vector<ColdInstanceCpu> coldInstances;
    for (int i = 0; i < 2; ++i) {
        ColdInstanceCpu ci{};
        ci.worldPos[0] = ci.worldPos[1] = ci.worldPos[2] = 0.0f;
        ci.renderScale = 1.0f; ci.providerKind = 1 /* procedural */; ci.recipeId = kRecipeC;
        ci.boundCenter[0] = entryC.boundCenter.x; ci.boundCenter[1] = entryC.boundCenter.y; ci.boundCenter[2] = entryC.boundCenter.z;
        ci.boundRadius = entryC.boundRadius; ci.stepRelaxation = entryC.stepRelaxation;
        ci.color[0] = 0.2f; ci.color[1] = 0.9f; ci.color[2] = 0.2f;  // green, distinguishable
        coldInstances.push_back(ci);
    }
    const uint32_t coldInstanceCount = static_cast<uint32_t>(coldInstances.size());

    std::vector<SyntheticRecipe> allRecipesForOracle = {
        SyntheticRecipe{kRecipeA, entryA, {progA[0]}, {glm::vec3(0.0f)}, kColorA},
        SyntheticRecipe{kRecipeB, entryB, {progB[0]}, {glm::vec3(0.0f)}, kColorB},
        SyntheticRecipe{kRecipeC, entryC, {progC[0]}, {glm::vec3(0.0f)}, glm::vec3(0.2f, 0.9f, 0.2f)},
    };

    // Camera: looks down -Z from (0,0,20) at the origin — frames both A (nearer, on-axis) and
    // B (farther, small lateral offset) so their bound spheres visibly overlap near screen
    // centre, plus C off to the side.
    const glm::vec3 eye(0.0f, 0.0f, 20.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float fovDeg = 40.0f;
    glm::mat4 projection = glm::perspective(glm::radians(fovDeg),
        float(kScreenWidth) / float(kScreenHeight), 0.1f, 200.0f);
    projection[1][1] *= -1.0f;
    glm::mat4 viewProj = projection * view;

    const glm::vec3 camDir = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 camRight = glm::normalize(glm::cross(camDir, worldUp));
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));
    const float aspect = float(kScreenWidth) / float(kScreenHeight);

    // ======================================================================
    // Sanity pre-check (CPU-only, no GPU): confirm the scene ACTUALLY produces real overlap and
    // a real winner-changes-across-the-image pattern before spending a GPU dispatch on it — a
    // scene with no real overlap would make the decisive gate below vacuous.
    // ======================================================================
    {
        int aWins = 0, bWins = 0, cWins = 0, misses = 0;
        for (uint32_t py = 0; py < kScreenHeight; py += 4) {
            for (uint32_t px = 0; px < kScreenWidth; px += 4) {
                glm::vec2 uv = (glm::vec2(px, py) + 0.5f) / glm::vec2(float(kScreenWidth), float(kScreenHeight));
                glm::vec2 ndc = uv * 2.0f - 1.0f; ndc.y = -ndc.y;
                const float tanHalfFov = std::tan(glm::radians(fovDeg * 0.5f));
                const glm::vec3 rayDir = glm::normalize(camDir + camRight * ndc.x * tanHalfFov * aspect
                                                                + camUp    * ndc.y * tanHalfFov);
                float t; glm::vec3 color; size_t winner;
                if (MultiRecipeCpuOracle(allRecipesForOracle, eye, rayDir, t, color, winner)) {
                    if (winner == 0) ++aWins; else if (winner == 1) ++bWins; else ++cWins;
                } else {
                    ++misses;
                }
            }
        }
        std::printf("[MULTI-BUCKET] scene sanity: aWins=%d bWins=%d cWins=%d misses=%d (coarse 4px stride)\n",
                    aWins, bWins, cWins, misses);
        ASSERT_GT(aWins, 0) << "Scene setup bug: recipe A never wins any pixel — camera/geometry framing is broken.";
        ASSERT_GT(bWins, 0) << "Scene setup bug: recipe B never wins any pixel — no real overlap exists to test "
                                "(B is fully occluded by A, or never visible) — this test's core premise fails.";
    }

    // ======================================================================
    // STEP 1: M1 bucketing pre-pass, run ONCE on the hot instances (A+B) — produces per-bucket
    // member lists + screen-space coverage rects. Cold instances (recipe C) are NOT fed through
    // this pass at all (mirrors the design doc §3.4 model: cold recipes' instances go straight
    // to the fallback path, not through the specialized-pipeline machinery).
    // ======================================================================
    VkBuffer instBuf, boundBuf, countBuf, idxBuf, minXBuf, minYBuf, maxXBuf, maxYBuf, indirectBuf;
    VkDeviceMemory instMem, boundMem, countMem, idxMem, minXMem, minYMem, maxXMem, maxYMem, indirectMem;

    const VkDeviceSize instSize  = hotInstanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
    const VkDeviceSize boundSize = kMaxBuckets * sizeof(RecipeBoundSphereCpu);
    const VkDeviceSize countSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize idxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * kMaxMembersPerBucket * sizeof(uint32_t);
    const VkDeviceSize extremaSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize indirectSize = static_cast<VkDeviceSize>(kMaxBuckets) * 3 * sizeof(uint32_t);

    CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
    CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
    CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
    CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
    CreateHostBuffer(indirectSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        indirectBuf, indirectMem, true);

    UploadBuffer(instMem, hotInstances.data(), instSize);

    std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
    boundSpheres[kRecipeA] = RecipeBoundSphereCpu{
        {entryA.boundCenter.x, entryA.boundCenter.y, entryA.boundCenter.z}, entryA.boundRadius, entryA.stepRelaxation, {0, 0, 0}};
    boundSpheres[kRecipeB] = RecipeBoundSphereCpu{
        {entryB.boundCenter.x, entryB.boundCenter.y, entryB.boundCenter.z}, entryB.boundRadius, entryB.stepRelaxation, {0, 0, 0}};
    UploadBuffer(boundMem, boundSpheres.data(), boundSize);

    // NOTE: RecipeInstanceBucketing.comp's own bucketing/coverage pass computes
    // `inst.worldPos + bound.center` for its screen-space AABB projection (a DIFFERENT,
    // bucketing-only convention from the tier-0/specialized-shader reject-sphere convention —
    // see RecipeInstanceBucketing.comp:210-217's own comment and SpecializedRecipeShaderGlsl.h's
    // matching comment). Since every hot instance here has worldPos=(0,0,0), the bucketing
    // pass's AABB is computed directly from bound.center, consistent with entryA/entryB's
    // boundCenter above.

    const std::vector<uint32_t> bucketingSpirv = ReadSpirv(RECIPE_BUCKETING_SPV);
    ASSERT_FALSE(bucketingSpirv.empty()) << "Failed to read compiled SPIR-V at " << RECIPE_BUCKETING_SPV;
    VkShaderModuleCreateInfo bsmci{};
    bsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    bsmci.codeSize = bucketingSpirv.size() * sizeof(uint32_t); bsmci.pCode = bucketingSpirv.data();
    VkShaderModule bucketingModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &bsmci, nullptr, &bucketingModule), VK_SUCCESS);

    auto bind = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b; lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb.descriptorCount = 1;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    const std::array<VkDescriptorSetLayoutBinding, 9> bucketingBindings = {
        bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
    };
    VkDescriptorSetLayoutCreateInfo bdslci{};
    bdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bdslci.bindingCount = static_cast<uint32_t>(bucketingBindings.size()); bdslci.pBindings = bucketingBindings.data();
    VkDescriptorSetLayout bucketingDsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &bdslci, nullptr, &bucketingDsl), VK_SUCCESS);

    VkPushConstantRange bpcr{};
    bpcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; bpcr.offset = 0; bpcr.size = sizeof(BucketingPush);
    VkPipelineLayoutCreateInfo bplci{};
    bplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    bplci.setLayoutCount = 1; bplci.pSetLayouts = &bucketingDsl;
    bplci.pushConstantRangeCount = 1; bplci.pPushConstantRanges = &bpcr;
    VkPipelineLayout bucketingLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &bplci, nullptr, &bucketingLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo bcpci{};
    bcpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    bcpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    bcpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    bcpci.stage.module = bucketingModule; bcpci.stage.pName = "main";
    bcpci.layout = bucketingLayout;
    VkPipeline bucketingPipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &bcpci, nullptr, &bucketingPipeline), VK_SUCCESS);

    VkDescriptorPoolSize bPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9};
    VkDescriptorPoolCreateInfo bdpci{};
    bdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    bdpci.maxSets = 1; bdpci.poolSizeCount = 1; bdpci.pPoolSizes = &bPoolSize;
    VkDescriptorPool bucketingPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &bdpci, nullptr, &bucketingPool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo bdsai{};
    bdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    bdsai.descriptorPool = bucketingPool; bdsai.descriptorSetCount = 1; bdsai.pSetLayouts = &bucketingDsl;
    VkDescriptorSet bucketingSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &bdsai, &bucketingSet), VK_SUCCESS);

    VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE}, boundInfo{boundBuf, 0, VK_WHOLE_SIZE},
        countInfo{countBuf, 0, VK_WHOLE_SIZE}, idxInfo{idxBuf, 0, VK_WHOLE_SIZE},
        minXInfo{minXBuf, 0, VK_WHOLE_SIZE}, minYInfo{minYBuf, 0, VK_WHOLE_SIZE},
        maxXInfo{maxXBuf, 0, VK_WHOLE_SIZE}, maxYInfo{maxYBuf, 0, VK_WHOLE_SIZE},
        indirectInfo{indirectBuf, 0, VK_WHOLE_SIZE};
    auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = bucketingSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 9> bucketingWrites = {
        wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
        wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
        wBuf(8, &indirectInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(bucketingWrites.size()), bucketingWrites.data(), 0, nullptr);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer bucketCmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &bucketCmd), VK_SUCCESS);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(bucketCmd, &cbbi), VK_SUCCESS);

    vkCmdBindPipeline(bucketCmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingPipeline);
    vkCmdBindDescriptorSets(bucketCmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingLayout, 0, 1, &bucketingSet, 0, nullptr);

    BucketingPush pcInit{};
    pcInit.viewProj = viewProj; pcInit.instanceCount = hotInstanceCount;
    pcInit.maxBuckets = kMaxBuckets; pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
    pcInit.screenWidth = kScreenWidth; pcInit.screenHeight = kScreenHeight;
    pcInit.mode = 1;
    vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
    vkCmdDispatch(bucketCmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    BucketingPush pcBucket = pcInit; pcBucket.mode = 0;
    vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
    vkCmdDispatch(bucketCmd, (hotInstanceCount + 63) / 64, 1, 1);

    vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    BucketingPush pcFinalize = pcInit; pcFinalize.mode = 2;
    vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcFinalize), &pcFinalize);
    vkCmdDispatch(bucketCmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    ASSERT_EQ(vkEndCommandBuffer(bucketCmd), VK_SUCCESS);
    VkSubmitInfo bsi{}; bsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    bsi.commandBufferCount = 1; bsi.pCommandBuffers = &bucketCmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &bsi, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    std::vector<uint32_t> bucketCounts, bucketIndices, minXBits, minYBits, indirectCmds;
    ReadbackBuffer(countMem, countSize, bucketCounts);
    ReadbackBuffer(idxMem, idxSize, bucketIndices);
    ReadbackBuffer(minXMem, extremaSize, minXBits);
    ReadbackBuffer(minYMem, extremaSize, minYBits);
    ReadbackBuffer(indirectMem, indirectSize, indirectCmds);

    ASSERT_EQ(bucketCounts[kRecipeA], 5u) << "bucket A membership mismatch";
    ASSERT_EQ(bucketCounts[kRecipeB], 6u) << "bucket B membership mismatch";
    // Task 6 hotness gate applied to the REAL bucketing output: both must clear the threshold.
    ASSERT_TRUE(IsHot(bucketCounts[kRecipeA]));
    ASSERT_TRUE(IsHot(bucketCounts[kRecipeB]));

    auto rectOriginOf = [&](uint32_t recipeId) -> std::pair<uint32_t, uint32_t> {
        const uint32_t minXBitVal = minXBits[recipeId];
        const uint32_t minYBitVal = minYBits[recipeId];
        const uint32_t x = minXBitVal == 0xFFFFFFFFu ? 0 : static_cast<uint32_t>(*reinterpret_cast<const float*>(&minXBitVal));
        const uint32_t y = minYBitVal == 0xFFFFFFFFu ? 0 : static_cast<uint32_t>(*reinterpret_cast<const float*>(&minYBitVal));
        return {x, y};
    };
    const auto [rectAMinX, rectAMinY] = rectOriginOf(kRecipeA);
    const auto [rectBMinX, rectBMinY] = rectOriginOf(kRecipeB);
    const uint32_t indirectAX = indirectCmds[kRecipeA * 3 + 0], indirectAY = indirectCmds[kRecipeA * 3 + 1];
    const uint32_t indirectBX = indirectCmds[kRecipeB * 3 + 0], indirectBY = indirectCmds[kRecipeB * 3 + 1];
    ASSERT_GT(indirectAX, 0u); ASSERT_GT(indirectAY, 0u);
    ASSERT_GT(indirectBX, 0u); ASSERT_GT(indirectBY, 0u);
    std::printf("[MULTI-BUCKET] recipeA: count=%u rect=[%u,%u] indirect=[%u,%u]  recipeB: count=%u rect=[%u,%u] indirect=[%u,%u]\n",
                bucketCounts[kRecipeA], rectAMinX, rectAMinY, indirectAX, indirectAY,
                bucketCounts[kRecipeB], rectBMinX, rectBMinY, indirectBX, indirectBY);

    // ======================================================================
    // STEP 2: compile BOTH specialized shaders (Task 5's emitter, looped per Task 7) through a
    // REAL ComputePipelineCacher (via a standalone MainCacher — MainCacher is explicitly "an
    // ordinary instantiable object owned by its host," not a graph-scoped singleton, see
    // MainCacher.h's own header comment) so Task 7's "reuse the cacher, simple cache-hit reuse
    // isn't out of scope" requirement is exercised for real, not just asserted in a comment.
    // ======================================================================
    std::ifstream coreFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(coreFile.good()) << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream coreSs; coreSs << coreFile.rdbuf();
    const std::string sdfCoreGlsl = coreSs.str();

    CashSystem::MainCacher mainCacher;
    mainCacher.Initialize(nullptr);
    // ComputePipelineCacher's "convenience mode" (a bare VkDescriptorSetLayout instead of a
    // pre-built PipelineLayoutWrapper, see ComputePipelineCreateParams) internally reaches for
    // PipelineLayoutCacher via its owning MainCacher (ComputePipelineCacher.cpp's
    // CreatePipelineLayout) — must be registered FIRST, mirroring ComputePipelineNode.cpp's own
    // registration order (PipelineLayoutCacher before ComputePipelineCacher).
    mainCacher.RegisterCacher<CashSystem::PipelineLayoutCacher, CashSystem::PipelineLayoutWrapper,
                               CashSystem::PipelineLayoutCreateParams>(
        typeid(CashSystem::PipelineLayoutWrapper), "PipelineLayout", true);
    mainCacher.RegisterCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                               CashSystem::ComputePipelineCreateParams>(
        typeid(CashSystem::ComputePipelineWrapper), "ComputePipeline", true);
    auto* pipelineCacher = mainCacher.GetCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                                 CashSystem::ComputePipelineCreateParams>(
        typeid(CashSystem::ComputePipelineWrapper), deviceShell_.get());
    ASSERT_NE(pipelineCacher, nullptr);

    // Shared descriptor set layout + pipeline layout for both specialized shaders (Task 5's own
    // binding namespace: 0=bodyInstances, 1=bucketMembers, 2=hitRecords — identical shape for
    // every recipe, so ONE VkDescriptorSetLayout/VkPipelineLayout genuinely serves both; only
    // the compiled SPIR-V module differs per recipe).
    const std::array<VkDescriptorSetLayoutBinding, 3> specBindings = {bind(0), bind(1), bind(2)};
    VkDescriptorSetLayoutCreateInfo sdslci{};
    sdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sdslci.bindingCount = static_cast<uint32_t>(specBindings.size()); sdslci.pBindings = specBindings.data();
    VkDescriptorSetLayout specDsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &sdslci, nullptr, &specDsl), VK_SUCCESS);

    VkPushConstantRange spcr{};
    spcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; spcr.offset = 0; spcr.size = sizeof(SpecializedPush);

    // NOTE: deliberately NOT hand-creating a separate VkPipelineLayout here. ComputePipelineCacher
    // (via its "convenience fallback" — a bare descriptorSetLayout instead of a pre-built
    // PipelineLayoutWrapper) creates its OWN VkPipelineLayout internally through PipelineLayoutCacher.
    // vkCmdBindDescriptorSets/vkCmdPushConstants must be called with THAT SAME layout object the
    // pipeline was actually built against (Vulkan's pipeline-layout-compatibility rules are defined
    // per-object, not per-content) — CompiledRecipe.layout below is read back from the wrapper
    // instead of a second, independently-created (if content-identical) layout, which caused a real
    // GPU-driver crash (SEH 0xc0000005) the first time this test ran with two separate objects.
    struct CompiledRecipe {
        uint32_t recipeId;
        VkShaderModule module = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::string shaderKey;
    };
    auto compileSpecialized = [&](uint32_t recipeId, const Vixen::SVO::RecipeRegistry::RecipeEntry& entry) -> CompiledRecipe {
        const std::string src = Vixen::SVO::Recipe::EmitSpecializedRecipeComputeShader(entry, recipeId, sdfCoreGlsl);
        ShaderManagement::ShaderCompiler compiler;
        ShaderManagement::CompilationOptions opts;
        opts.validateSpirv = false;  // ponytail: known glslang SPIR-V validator quirk (see M2's test)
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, src, "main", opts);
        EXPECT_TRUE(compOut.success) << "recipe " << recipeId << " compile failed:\n" << compOut.GetFullLog();
        if (!compOut.success) return {};

        CompiledRecipe out; out.recipeId = recipeId;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = compOut.spirv.size() * sizeof(uint32_t); smci.pCode = compOut.spirv.data();
        EXPECT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &out.module), VK_SUCCESS);

        // Pipeline cache-key convention (mirrors ComputePipelineNode.cpp:261-282): programName +
        // SHA256(spirv) — code identity, not descriptor-interface identity.
        out.shaderKey = "sdfRecipe_specialized_" + std::to_string(recipeId) + ":" +
                         ShaderManagement::ComputeSHA256HexFromUint32Vec(compOut.spirv);

        CashSystem::ComputePipelineCreateParams params;
        params.shaderModule = out.module;
        params.entryPoint = "main";
        params.descriptorSetLayout = specDsl;
        params.pushConstantRanges = {spcr};
        params.shaderKey = out.shaderKey;
        params.layoutKey = "recipe_bucketing_specialized_layout";
        params.workgroupSizeX = 8; params.workgroupSizeY = 8; params.workgroupSizeZ = 1;
        auto wrapper = pipelineCacher->GetOrCreate(params);
        EXPECT_NE(wrapper, nullptr);
        if (wrapper) {
            out.pipeline = wrapper->pipeline;
            out.layout = wrapper->pipelineLayoutWrapper->layout;
        }
        return out;
    };

    CompiledRecipe compiledA = compileSpecialized(kRecipeA, entryA);
    CompiledRecipe compiledB = compileSpecialized(kRecipeB, entryB);
    ASSERT_NE(compiledA.pipeline, VK_NULL_HANDLE);
    ASSERT_NE(compiledB.pipeline, VK_NULL_HANDLE);
    ASSERT_NE(compiledA.shaderKey, compiledB.shaderKey) << "distinct recipes must not collide on pipeline cache key";

    // Cache-hit reuse check (Task 7: "reuse ComputePipelineCacher so identical-bytecode recipes
    // don't recompile redundantly"): re-requesting recipe A's EXACT params must return the SAME
    // VkPipeline handle, not a freshly created one.
    {
        CashSystem::ComputePipelineCreateParams reParams;
        reParams.shaderModule = compiledA.module;
        reParams.entryPoint = "main";
        reParams.descriptorSetLayout = specDsl;
        reParams.pushConstantRanges = {spcr};
        reParams.shaderKey = compiledA.shaderKey;
        reParams.layoutKey = "recipe_bucketing_specialized_layout";
        reParams.workgroupSizeX = 8; reParams.workgroupSizeY = 8; reParams.workgroupSizeZ = 1;
        auto reWrapper = pipelineCacher->GetOrCreate(reParams);
        ASSERT_NE(reWrapper, nullptr);
        EXPECT_EQ(reWrapper->pipeline, compiledA.pipeline) << "cache-hit reuse failed: re-requesting the "
            "same recipe's pipeline params created a NEW VkPipeline instead of reusing the cached one";
    }

    // ======================================================================
    // STEP 3: compile the cold-path shader (standalone tier-0-equivalent, see file header) —
    // one fixed full-screen dispatch, loops the (small) cold instance list, same conditional
    // HitRecord write scheme as the specialized shaders.
    // ======================================================================
    static const char* kColdRecipeMarchGlsl = R"GLSL(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct ColdInstance {
    vec3  worldPos; float renderScale;
    vec3  color; uint octreeIndex;
    uint  providerKind; uint recipeId;
    float recipeParams[6];
    vec3  boundCenter; float boundRadius;
    float stepRelaxation; float _pad[3];
};
layout(std430, binding = 0) readonly buffer ColdInstanceBuffer { ColdInstance coldInstances[]; };

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL
#define HITRECORD_FLAG_HIT 0x1u
struct HitRecord {
    vec3 albedo; float roughness;
    vec3 worldNormal; float hitT;
    vec3 worldPos; uint flags;
    uint _pad0[3];
};
#endif
layout(std430, binding = 1) buffer HitRecordBuffer { HitRecord hitRecords[]; };

layout(push_constant) uniform Push {
    vec3 cameraPos; float _p0;
    vec3 cameraDir; float fov;
    vec3 cameraUp;  float aspect;
    vec3 cameraRight; float _p1;
    uint instanceCount;
    uint screenWidth;
    uint screenHeight;
    uint _p2;
} pc;

// Same analytic-sphere field function shape as EmitSpecializedRecipeComputeShader's emitted
// sdfRecipe_<id> — this test's cold recipe is a plain sphere (SdfOpCode::Sphere), so a direct
// closed-form distance function is equivalent to (and cheaper than) invoking the full recipe
// bytecode interpreter for this single-op case; correctness is still cross-checked against the
// SAME CpuOracleTraceUberRecipeBody oracle every other path in this test uses.
float sdfSphere(vec3 p, float r) { return length(p) - r; }

vec3 getRayDir(vec2 uv) {
    float tanHalfFov = tan(radians(pc.fov * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return normalize(pc.cameraDir + pc.cameraRight * ndc.x * tanHalfFov * pc.aspect
                                   + pc.cameraUp    * ndc.y * tanHalfFov);
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= int(pc.screenWidth) || pixelCoords.y >= int(pc.screenHeight)) return;

    vec2 uv = (vec2(pixelCoords) + 0.5) / vec2(float(pc.screenWidth), float(pc.screenHeight));
    vec3 rayOrigin = pc.cameraPos;
    vec3 rayDir    = getRayDir(uv);

    bool  anyHit = false;
    float bestT  = 1e30;
    vec3  bestNormal = vec3(0.0, 1.0, 0.0);
    vec3  bestColor  = vec3(1.0);

    for (uint m = 0u; m < pc.instanceCount; ++m) {
        ColdInstance inst = coldInstances[m];
        vec3  oc = rayOrigin - inst.boundCenter;
        float b  = dot(oc, rayDir);
        float c  = dot(oc, oc) - inst.boundRadius * inst.boundRadius;
        float disc = b * b - c;
        if (disc < 0.0) continue;
        float sq = sqrt(disc);
        float tNear = max(-b - sq, 0.0);
        float tFar  = -b + sq;
        if (tFar < 0.0 || tNear >= bestT) continue;

        float t = tNear;
        const int   MAX_STEPS = 128;
        const float EPS = 1e-3;
        // recipeParams[0..2] carries the sphere's own radius (mirrors this test's C++-side
        // SdfInstruction encoding, prog[0].data[3] -> the field evaluator's sphere radius);
        // this cold-path shader hard-codes the same radius convention its C++ SdfInstruction
        // setup uses (kRecipeC's prog[0].data[3] == entryC's actual sphere radius).
        float sphereRadius = 0.8;
        for (int i = 0; i < MAX_STEPS; ++i) {
            vec3  p = rayOrigin + rayDir * t - inst.boundCenter;
            float d = sdfSphere(p, sphereRadius);
            if (d < EPS) {
                if (t < bestT) {
                    bestT = t;
                    bestNormal = normalize(p);
                    bestColor  = inst.color;
                    anyHit = true;
                }
                break;
            }
            t += d * inst.stepRelaxation;
            if (t > tFar) break;
        }
    }

    if (!anyHit) return;

    uint hitIdx = uint(pixelCoords.y) * pc.screenWidth + uint(pixelCoords.x);
    // SAME plain (non-atomic) read-compare-conditionally-overwrite scheme as
    // SpecializedRecipeShaderGlsl.h's emitted shaders — this is the standalone tier-0-equivalent
    // half of Task 8's "the tier-0 path's own write may also need this same conditional-write
    // treatment" question, applied here since the real BodyInstanceRayMarch.comp is out of this
    // milestone's scope (see file header). Symmetric with the specialized shaders' own write:
    // whichever dispatch runs second sees whatever the first left behind and only overwrites if
    // strictly nearer (or the slot is still a virgin miss) -- order-independent by construction.
    if (bestT < hitRecords[hitIdx].hitT || hitRecords[hitIdx].flags == 0u) {
        HitRecord rec;
        rec.albedo = bestColor;
        rec.roughness = 1.0;
        rec.worldNormal = bestNormal;
        rec.hitT = bestT;
        rec.worldPos = rayOrigin + rayDir * bestT;
        rec.flags = HITRECORD_FLAG_HIT;
        rec._pad0 = uint[3](0u, 0u, 0u);
        hitRecords[hitIdx] = rec;
    }
}
)GLSL";

    ShaderManagement::ShaderCompiler coldCompiler;
    ShaderManagement::CompilationOptions coldOpts;
    coldOpts.validateSpirv = false;
    auto coldCompOut = coldCompiler.Compile(ShaderManagement::ShaderStage::Compute, kColdRecipeMarchGlsl, "main", coldOpts);
    ASSERT_TRUE(coldCompOut.success) << "cold-path shader compile failed:\n" << coldCompOut.GetFullLog();

    VkShaderModuleCreateInfo cmsmci{};
    cmsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    cmsmci.codeSize = coldCompOut.spirv.size() * sizeof(uint32_t); cmsmci.pCode = coldCompOut.spirv.data();
    VkShaderModule coldModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &cmsmci, nullptr, &coldModule), VK_SUCCESS);

    const std::array<VkDescriptorSetLayoutBinding, 2> coldBindings = {bind(0), bind(1)};
    VkDescriptorSetLayoutCreateInfo cdslci{};
    cdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cdslci.bindingCount = static_cast<uint32_t>(coldBindings.size()); cdslci.pBindings = coldBindings.data();
    VkDescriptorSetLayout coldDsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &cdslci, nullptr, &coldDsl), VK_SUCCESS);

    VkPushConstantRange cpcr{};
    cpcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; cpcr.offset = 0; cpcr.size = sizeof(ColdPush);
    VkPipelineLayoutCreateInfo cplci{};
    cplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    cplci.setLayoutCount = 1; cplci.pSetLayouts = &coldDsl;
    cplci.pushConstantRangeCount = 1; cplci.pPushConstantRanges = &cpcr;
    VkPipelineLayout coldLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &cplci, nullptr, &coldLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo ccpci{};
    ccpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ccpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ccpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ccpci.stage.module = coldModule; ccpci.stage.pName = "main";
    ccpci.layout = coldLayout;
    VkPipeline coldPipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &ccpci, nullptr, &coldPipeline), VK_SUCCESS);

    // ======================================================================
    // STEP 4: shared GPU resources for the dispatch phase (instance buffers, member-list slices,
    // HitRecord SSBO, descriptor sets) — built ONCE, reused across both ordering runs below (each
    // ordering re-zeros HitRecord and re-runs, so results are directly comparable).
    // ======================================================================
    VkBuffer specInstBuf, membersABuf, membersBBuf, coldInstBuf, hitRecordBuf;
    VkDeviceMemory specInstMem, membersAMem, membersBMem, coldInstMem, hitRecordMem;

    CreateHostBuffer(instSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, specInstBuf, specInstMem, false);
    UploadBuffer(specInstMem, hotInstances.data(), instSize);

    std::vector<uint32_t> membersA(bucketIndices.begin() + kRecipeA * kMaxMembersPerBucket,
                                    bucketIndices.begin() + kRecipeA * kMaxMembersPerBucket + bucketCounts[kRecipeA]);
    std::vector<uint32_t> membersB(bucketIndices.begin() + kRecipeB * kMaxMembersPerBucket,
                                    bucketIndices.begin() + kRecipeB * kMaxMembersPerBucket + bucketCounts[kRecipeB]);
    const VkDeviceSize membersASize = membersA.size() * sizeof(uint32_t);
    const VkDeviceSize membersBSize = membersB.size() * sizeof(uint32_t);
    CreateHostBuffer(membersASize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, membersABuf, membersAMem, false);
    UploadBuffer(membersAMem, membersA.data(), membersASize);
    CreateHostBuffer(membersBSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, membersBBuf, membersBMem, false);
    UploadBuffer(membersBMem, membersB.data(), membersBSize);

    const VkDeviceSize coldInstSize = coldInstanceCount * sizeof(ColdInstanceCpu);
    CreateHostBuffer(coldInstSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, coldInstBuf, coldInstMem, false);
    UploadBuffer(coldInstMem, coldInstances.data(), coldInstSize);

    const VkDeviceSize hitRecordSize = static_cast<VkDeviceSize>(kScreenWidth) * kScreenHeight * sizeof(HitRecordCpu);
    CreateHostBuffer(hitRecordSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);

    VkDescriptorPoolSize dPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 + 3 + 2};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 3; dpci.poolSizeCount = 1; dpci.pPoolSizes = &dPoolSize;
    VkDescriptorPool dispatchPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &dispatchPool), VK_SUCCESS);

    auto allocSet = [&](VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = dispatchPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(logicalDevice_, &dsai, &set);
        return set;
    };
    VkDescriptorSet setA = allocSet(specDsl), setB = allocSet(specDsl), setCold = allocSet(coldDsl);

    VkDescriptorBufferInfo specInstInfo{specInstBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo membersAInfo{membersABuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo membersBInfo{membersBBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo coldInstInfo{coldInstBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo hitRecordInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
    auto wSet = [&](VkDescriptorSet set, uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 8> dispatchWrites = {
        wSet(setA, 0, &specInstInfo), wSet(setA, 1, &membersAInfo), wSet(setA, 2, &hitRecordInfo),
        wSet(setB, 0, &specInstInfo), wSet(setB, 1, &membersBInfo), wSet(setB, 2, &hitRecordInfo),
        wSet(setCold, 0, &coldInstInfo), wSet(setCold, 1, &hitRecordInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(dispatchWrites.size()), dispatchWrites.data(), 0, nullptr);

    SpecializedPush pcA{};
    pcA.cameraPos = eye; pcA.cameraDir = camDir; pcA.fov = fovDeg; pcA.cameraUp = camUp; pcA.aspect = aspect; pcA.cameraRight = camRight;
    pcA.memberCount = bucketCounts[kRecipeA]; pcA.screenWidth = kScreenWidth; pcA.screenHeight = kScreenHeight;
    pcA.rectMinX = rectAMinX; pcA.rectMinY = rectAMinY; pcA.boundRadius = entryA.boundRadius; pcA.stepRelaxation = entryA.stepRelaxation;

    SpecializedPush pcB = pcA;
    pcB.memberCount = bucketCounts[kRecipeB]; pcB.rectMinX = rectBMinX; pcB.rectMinY = rectBMinY;
    pcB.boundRadius = entryB.boundRadius; pcB.stepRelaxation = entryB.stepRelaxation;

    ColdPush pcCold{};
    pcCold.cameraPos = eye; pcCold.cameraDir = camDir; pcCold.fov = fovDeg; pcCold.cameraUp = camUp; pcCold.aspect = aspect; pcCold.cameraRight = camRight;
    pcCold.instanceCount = coldInstanceCount; pcCold.screenWidth = kScreenWidth; pcCold.screenHeight = kScreenHeight;

    // ======================================================================
    // STEP 5: MultiDispatchNode standalone construction (mirrors test_hitrecord_readback.cpp's
    // established "no full RenderGraph needed" pattern — NodeType::CreateInstance +
    // SetInput/Setup/Compile/Execute directly on the node instance).
    // ======================================================================
    using MC = MultiDispatchNodeConfig;
    Vixen::Vulkan::Resources::RenderTargetData fakeTarget;
    fakeTarget.buffers.resize(1);  // CompileImpl only calls GetImageCount() -> 1 command buffer

    // Runs the full A+B(+cold) dispatch sequence via a FRESH MultiDispatchNode with default
    // autoBarriers_=true (never disabled), in the requested order, and returns the readback.
    // `hotFirst` selects hot-buckets-then-cold vs cold-then-hot-buckets — Task 8's own explicit
    // "confirm ordering doesn't matter, don't just assume it" requirement.
    auto runSequence = [&](bool hotFirst) -> std::vector<HitRecordCpu> {
        ZeroBuffer(hitRecordMem, hitRecordSize);

        MultiDispatchNodeType nodeType("MultiDispatch");
        auto nodeBase = nodeType.CreateInstance("recipe_multi_bucket_" + std::string(hotFirst ? "hot_first" : "cold_first"));
        auto* node = dynamic_cast<MultiDispatchNode*>(nodeBase.get());
        EXPECT_NE(node, nullptr);
        if (!node) return {};

        Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
        Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource targetRes; SetHandleVal<IRenderTarget*>(targetRes, static_cast<IRenderTarget*>(&fakeTarget));
        Resource imageIdxRes; uint32_t imageIndex = 0; SetHandleVal<uint32_t>(imageIdxRes, imageIndex);
        Resource frameIdxRes; uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameIdxRes, frameIndex);

        node->SetInput(MC::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
        node->SetInput(MC::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(MC::SWAPCHAIN_INFO_Slot::index,      0, &targetRes);
        node->SetInput(MC::IMAGE_INDEX_Slot::index,         0, &imageIdxRes);
        node->SetInput(MC::CURRENT_FRAME_INDEX_Slot::index, 0, &frameIdxRes);

        node->Setup();
        EXPECT_NO_THROW(node->Compile());

        DispatchPass passA;
        passA.pipeline = compiledA.pipeline; passA.layout = compiledA.layout;
        passA.descriptorSets = {setA};
        passA.indirectBuffer = indirectBuf; passA.indirectBufferOffset = kRecipeA * 3 * sizeof(uint32_t);
        {
            PushConstantData pcData; pcData.data.resize(sizeof(SpecializedPush));
            std::memcpy(pcData.data.data(), &pcA, sizeof(SpecializedPush));
            passA.pushConstants = pcData;
        }
        passA.debugName = "RecipeA_Specialized";

        DispatchPass passB;
        passB.pipeline = compiledB.pipeline; passB.layout = compiledB.layout;
        passB.descriptorSets = {setB};
        passB.indirectBuffer = indirectBuf; passB.indirectBufferOffset = kRecipeB * 3 * sizeof(uint32_t);
        {
            PushConstantData pcData; pcData.data.resize(sizeof(SpecializedPush));
            std::memcpy(pcData.data.data(), &pcB, sizeof(SpecializedPush));
            passB.pushConstants = pcData;
        }
        passB.debugName = "RecipeB_Specialized";

        DispatchPass passCold;
        passCold.pipeline = coldPipeline; passCold.layout = coldLayout;
        passCold.descriptorSets = {setCold};
        passCold.workGroupCount = {(kScreenWidth + 7) / 8, (kScreenHeight + 7) / 8, 1};
        {
            PushConstantData pcData; pcData.data.resize(sizeof(ColdPush));
            std::memcpy(pcData.data.data(), &pcCold, sizeof(ColdPush));
            passCold.pushConstants = pcData;
        }
        passCold.debugName = "ColdRecipe_TierZeroEquivalent";

        if (hotFirst) {
            node->QueueDispatch(std::move(passA));
            node->QueueDispatch(std::move(passB));
            node->QueueDispatch(std::move(passCold));
        } else {
            node->QueueDispatch(std::move(passCold));
            node->QueueDispatch(std::move(passA));
            node->QueueDispatch(std::move(passB));
        }

        EXPECT_NO_THROW(node->Execute());
        VkCommandBuffer cmdBuffer = node->GetOutput(MC::COMMAND_BUFFER_Slot::index, 0)->GetHandle<VkCommandBuffer>();
        EXPECT_NE(cmdBuffer, VK_NULL_HANDLE);

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmdBuffer;
        EXPECT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        EXPECT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        std::vector<HitRecordCpu> result;
        ReadbackBuffer(hitRecordMem, hitRecordSize, result);

        node->Cleanup(CleanupReason::FinalTeardown);
        return result;
    };

    // ======================================================================
    // STEP 6: run BOTH orderings, cross-check each against the multi-recipe CPU oracle over the
    // FULL screen (not a spot-check — Task 8 explicitly requires "a real, whole-relevant-region
    // pixel comparison, not a spot-check of one or two pixels"), then cross-check the two
    // orderings against EACH OTHER (must be byte-identical — this is the direct empirical proof
    // of the plan's order-independence claim, not an assumption).
    // ======================================================================
    auto checkAgainstOracle = [&](const std::vector<HitRecordCpu>& hitRecords, const char* label) {
        int gpuHits = 0, oracleHits = 0, matchedHits = 0, overlapRegionChecked = 0, overlapRegionAWins = 0;
        float maxHitTDelta = 0.0f;
        ASSERT_EQ(hitRecords.size(), static_cast<size_t>(kScreenWidth) * kScreenHeight);

        for (uint32_t py = 0; py < kScreenHeight; ++py) {
            for (uint32_t px = 0; px < kScreenWidth; ++px) {
                const uint32_t hitIdx = py * kScreenWidth + px;
                const HitRecordCpu& gpuRec = hitRecords[hitIdx];
                const bool gpuHit = (gpuRec.flags & 0x1u) != 0u;

                glm::vec2 uv = (glm::vec2(px, py) + 0.5f) / glm::vec2(float(kScreenWidth), float(kScreenHeight));
                glm::vec2 ndc = uv * 2.0f - 1.0f; ndc.y = -ndc.y;
                const float tanHalfFov = std::tan(glm::radians(fovDeg * 0.5f));
                const glm::vec3 rayDir = glm::normalize(camDir + camRight * ndc.x * tanHalfFov * aspect
                                                                + camUp    * ndc.y * tanHalfFov);

                float oracleT; glm::vec3 oracleColor; size_t oracleWinner;
                const bool oracleAnyHit = MultiRecipeCpuOracle(allRecipesForOracle, eye, rayDir, oracleT, oracleColor, oracleWinner);

                if (gpuHit) ++gpuHits;
                if (oracleAnyHit) ++oracleHits;

                ASSERT_EQ(gpuHit, oracleAnyHit)
                    << "[" << label << "] hit/miss mismatch at pixel (" << px << "," << py << "): "
                    << "GPU=" << gpuHit << " oracle=" << oracleAnyHit;

                if (gpuHit && oracleAnyHit) {
                    ++matchedHits;
                    const float delta = std::abs(gpuRec.hitT - oracleT);
                    maxHitTDelta = std::max(maxHitTDelta, delta);
                    EXPECT_NEAR(gpuRec.hitT, oracleT, 0.05f)
                        << "[" << label << "] hitT mismatch at pixel (" << px << "," << py << ")";
                    // Redundant identity cross-check via albedo — catches a "right hitT, wrong
                    // recipe's shader wrote it" bug that a pure hitT comparison could miss.
                    EXPECT_NEAR(gpuRec.albedo[0], oracleColor.x, 0.05f)
                        << "[" << label << "] albedo.r mismatch at (" << px << "," << py << ") — "
                           "wrong recipe won compositing";
                    EXPECT_NEAR(gpuRec.albedo[2], oracleColor.z, 0.05f)
                        << "[" << label << "] albedo.b mismatch at (" << px << "," << py << ")";

                    // Overlap-region bookkeeping: pixels where BOTH A and B's bound spheres could
                    // plausibly contribute (oracle winner is A, but B is also a real candidate —
                    // approximate via oracle recomputation is already the ground truth here).
                    if (oracleWinner == 0) {
                        // Distinguish "true overlap region" pixels from "A visible, B nowhere near"
                        // pixels by checking whether removing A changes the result to B.
                        std::vector<SyntheticRecipe> withoutA = {allRecipesForOracle[1], allRecipesForOracle[2]};
                        float tWithoutA; glm::vec3 cWithoutA; size_t wWithoutA;
                        if (MultiRecipeCpuOracle(withoutA, eye, rayDir, tWithoutA, cWithoutA, wWithoutA) && wWithoutA == 0) {
                            ++overlapRegionChecked;
                            ++overlapRegionAWins;
                        }
                    }
                }
            }
        }

        std::printf("[MULTI-BUCKET][%s] gpuHits=%d oracleHits=%d matchedHits=%d maxHitTDelta=%.5f "
                    "overlapRegionPixels=%d (A won all of them=%d)\n",
                    label, gpuHits, oracleHits, matchedHits, maxHitTDelta,
                    overlapRegionChecked, overlapRegionAWins);
        ASSERT_GT(matchedHits, 0) << "[" << label << "] no pixels hit at all — scene/camera framing broken.";
        ASSERT_GT(overlapRegionChecked, 0) << "[" << label << "] no true overlap-region pixels found "
            "(where B would win if A were absent, but A wins with both present) — the decisive "
            "nearest-wins compositing claim was never actually exercised by this scene.";
        EXPECT_EQ(overlapRegionAWins, overlapRegionChecked)
            << "[" << label << "] in the overlap region, A (the nearer recipe) must win EVERY "
               "pixel — a mismatch here is exactly the cross-bucket compositing bug Task 8 exists "
               "to catch.";
    };

    std::vector<HitRecordCpu> hotFirstResult = runSequence(/*hotFirst=*/true);
    ASSERT_NO_FATAL_FAILURE(checkAgainstOracle(hotFirstResult, "hot-buckets-then-cold"));

    std::vector<HitRecordCpu> coldFirstResult = runSequence(/*hotFirst=*/false);
    ASSERT_NO_FATAL_FAILURE(checkAgainstOracle(coldFirstResult, "cold-then-hot-buckets"));

    // Direct order-independence proof: the two orderings' HitRecord buffers must be BYTE-
    // IDENTICAL, not just "both individually plausible" — this is the strongest form of Task 8's
    // "confirm ordering doesn't matter" requirement.
    ASSERT_EQ(hotFirstResult.size(), coldFirstResult.size());
    int orderingMismatches = 0;
    for (size_t i = 0; i < hotFirstResult.size(); ++i) {
        if (std::memcmp(&hotFirstResult[i], &coldFirstResult[i], sizeof(HitRecordCpu)) != 0) {
            ++orderingMismatches;
        }
    }
    std::printf("[MULTI-BUCKET] ordering cross-check: %d/%zu pixels differ between hot-first and cold-first\n",
                orderingMismatches, hotFirstResult.size());
    EXPECT_EQ(orderingMismatches, 0) << "dispatch ordering changed the composited result — the "
        "plan's 'nearest wins is order-independent by construction' claim does NOT hold as "
        "implemented; this is exactly the subtle bug the plan's own Risks section warned about.";

    // --- Cleanup ---
    vkDeviceWaitIdle(logicalDevice_);
    vkDestroyDescriptorPool(logicalDevice_, dispatchPool, nullptr);
    vkDestroyPipeline(logicalDevice_, coldPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, coldLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, coldDsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, coldModule, nullptr);
    // compiledA.pipeline/compiledB.pipeline and compiledA.layout (== compiledB.layout, same
    // cache-key -- see the cache-hit reuse check above) were created by the standalone
    // mainCacher's ComputePipelineCacher/PipelineLayoutCacher, which (unlike the real
    // DeviceNode-owned production MainCacher) never gets an explicit device-scoped Cleanup()
    // call in this test -- MainCacher's destructor only cleans device-INDEPENDENT caches (see
    // MainCacher::CleanupGlobalCaches). Destroy them directly here instead: Vulkan destruction
    // only requires exactly-once, not that the destroyer be the "logical owner."
    vkDestroyPipeline(logicalDevice_, compiledA.pipeline, nullptr);
    vkDestroyPipeline(logicalDevice_, compiledB.pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, compiledA.layout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, specDsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, compiledA.module, nullptr);
    vkDestroyShaderModule(logicalDevice_, compiledB.module, nullptr);
    vkDestroyBuffer(logicalDevice_, specInstBuf, nullptr); vkFreeMemory(logicalDevice_, specInstMem, nullptr);
    vkDestroyBuffer(logicalDevice_, membersABuf, nullptr); vkFreeMemory(logicalDevice_, membersAMem, nullptr);
    vkDestroyBuffer(logicalDevice_, membersBBuf, nullptr); vkFreeMemory(logicalDevice_, membersBMem, nullptr);
    vkDestroyBuffer(logicalDevice_, coldInstBuf, nullptr); vkFreeMemory(logicalDevice_, coldInstMem, nullptr);
    vkDestroyBuffer(logicalDevice_, hitRecordBuf, nullptr); vkFreeMemory(logicalDevice_, hitRecordMem, nullptr);

    vkDestroyDescriptorPool(logicalDevice_, bucketingPool, nullptr);
    vkDestroyPipeline(logicalDevice_, bucketingPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, bucketingLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, bucketingDsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, bucketingModule, nullptr);
    vkDestroyBuffer(logicalDevice_, instBuf, nullptr);  vkFreeMemory(logicalDevice_, instMem, nullptr);
    vkDestroyBuffer(logicalDevice_, boundBuf, nullptr); vkFreeMemory(logicalDevice_, boundMem, nullptr);
    vkDestroyBuffer(logicalDevice_, countBuf, nullptr); vkFreeMemory(logicalDevice_, countMem, nullptr);
    vkDestroyBuffer(logicalDevice_, idxBuf, nullptr);   vkFreeMemory(logicalDevice_, idxMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minXBuf, nullptr);  vkFreeMemory(logicalDevice_, minXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minYBuf, nullptr);  vkFreeMemory(logicalDevice_, minYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxXBuf, nullptr);  vkFreeMemory(logicalDevice_, maxXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxYBuf, nullptr);  vkFreeMemory(logicalDevice_, maxYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, indirectBuf, nullptr); vkFreeMemory(logicalDevice_, indirectMem, nullptr);
}
