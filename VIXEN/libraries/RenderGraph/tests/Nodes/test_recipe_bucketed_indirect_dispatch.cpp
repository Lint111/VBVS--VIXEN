/**
 * @file test_recipe_bucketed_indirect_dispatch.cpp
 * @brief Recipe GPU Instance Bucketing Inc2 M2 live-run gate (Tasks 4-5).
 *
 * Proves the indirect-dispatch + specialized-pipeline mechanism end-to-end for ONE hot recipe:
 *   1. Dispatches shaders/RecipeInstanceBucketing.comp (M1, unmodified bucketing logic + new
 *      M2 mode==2 finalize pass) against a synthetic scene of N instances of ONE recipe, to
 *      produce a real per-bucket VkDispatchIndirectCommand.
 *   2. Compiles a specialized single-recipe shader (EmitSpecializedRecipeComputeShader, Task 5)
 *      SYNCHRONOUSLY via ShaderManagement::ShaderCompiler (mirrors
 *      ComputePipelineNode.cpp:261-282's hash-the-SPIR-V cache-key convention for the pipeline
 *      key, though this standalone test does not go through the full RenderGraph
 *      ComputePipelineCacher — it validates the COMPILE + DISPATCH + CORRECTNESS mechanism the
 *      cacher would wrap).
 *   3. Dispatches the specialized shader via vkCmdDispatchIndirect (Task 4's new DispatchPass
 *      path — this test drives vkCmdDispatchIndirect directly against a hand-built command
 *      buffer, the same "no RenderGraph node involvement" pattern M1's own gate test used) using
 *      the indirect command from step 1.
 *   4. Reads back HitRecord and compares against an INDEPENDENT CPU oracle that reimplements the
 *      exact same sphere-march algorithm as traceUberRecipeBody (SdfRecipes.glsl) — the same
 *      algorithm the tier-0 switch path would run for this recipe+instances — using
 *      Vixen::SVO::Recipe::evalRecipe (the CPU SDF evaluator) as the field-function oracle. This
 *      is the "proves the specialized-pipeline path produces the SAME hits as the switch path
 *      would" gate the M2 plan requires.
 *
 * A SEPARATE control section (Task 4's own additivity requirement) proves the indirect-buffer
 * branch is byte-identical/inert when unset: DispatchPass::IsValid()'s fixed-workgroup path is
 * exercised via a direct construction + assertion, and MultiDispatchNode's own CPU-side test
 * suite (test_multidispatch_integration.cpp, unmodified by this milestone) is the authoritative
 * "existing behavior unaffected" proof — this file adds a targeted DispatchPass-level check
 * alongside it rather than duplicating that whole suite.
 *
 * DEVICE SELECTION: same contract as test_recipe_instance_bucketing.cpp — a real discrete/
 * integrated GPU is PREFERRED; software (lavapipe/llvmpipe) or Dozen only as a fallback.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "Data/DispatchPass.h"
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

using Vixen::Vulkan::Resources::VulkanDevice;

// ---------------------------------------------------------------------------
// Task 4 additivity check: a DispatchPass with no indirectBuffer set is validated and behaves
// exactly as before M2 — pure CPU, no device needed. Companion to
// test_multidispatch_integration.cpp staying green (that suite is the authoritative "existing
// MultiDispatchNode behavior unaffected" proof; this is a focused DispatchPass-level check).
// ---------------------------------------------------------------------------
TEST(DispatchPassIndirect, FixedWorkgroupPathUnaffectedWhenIndirectBufferUnset) {
    Vixen::RenderGraph::DispatchPass pass;
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1);
    pass.layout   = reinterpret_cast<VkPipelineLayout>(0x1);
    pass.workGroupCount = {4, 4, 1};
    EXPECT_TRUE(pass.IsValid());
    EXPECT_FALSE(pass.indirectBuffer.has_value());
    EXPECT_EQ(pass.TotalWorkGroups(), 16u);
}

TEST(DispatchPassIndirect, IndirectBufferPathValidatesOnBufferPresenceNotWorkgroupCount) {
    Vixen::RenderGraph::DispatchPass pass;
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1);
    pass.layout   = reinterpret_cast<VkPipelineLayout>(0x1);
    // Deliberately zeroed — must NOT matter once indirectBuffer is set (pre-M2, this would fail
    // IsValid(); post-M2 it must pass, since the real dispatch dimensions come from the GPU
    // buffer, not this CPU-known field).
    pass.workGroupCount = {0, 0, 0};
    pass.indirectBuffer = reinterpret_cast<VkBuffer>(0x2);
    EXPECT_TRUE(pass.IsValid());
}

TEST(DispatchPassIndirect, InvalidWhenIndirectBufferIsExplicitNullHandle) {
    Vixen::RenderGraph::DispatchPass pass;
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1);
    pass.layout   = reinterpret_cast<VkPipelineLayout>(0x1);
    pass.indirectBuffer = VK_NULL_HANDLE;
    EXPECT_FALSE(pass.IsValid());
}

namespace {

// Byte-identical to RecipeInstanceBucketing.comp's Push block (M2: mode now also accepts 2).
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

// Byte-identical to HitRecord.glsl's std430 layout (64 B/element — see that file's own layout
// comment). A plain C++ float[3] has only 4-byte alignment (unlike GLSL's vec3, which is 16-byte
// aligned even though its SIZE is 12 B) — so this mirror cannot rely on struct tail-padding to
// reach 64 B the way the GLSL side does; _pad0 is sized 4 (not 3) to make the byte count exact
// without depending on any implicit alignment padding the compiler may or may not add.
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
// minus the occupancy-grid skip optimization (M2's specialized shader doesn't use it either —
// out of scope, see SpecializedRecipeShaderGlsl.h), using evalRecipe as the field-function
// oracle. This is independent ground truth for "what would the tier-0 switch path have produced
// for this recipe+instance+ray," not a copy of the GPU shader under test.
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

}  // namespace

class RecipeBucketedIndirectDispatchTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;

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
        appInfo.pApplicationName = "test_recipe_bucketed_indirect_dispatch";
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
    }

    void TearDown() override {
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

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
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
// THE decisive test: bucket -> indirect command -> specialized pipeline dispatch -> HitRecord,
// compared against the CPU oracle (independent reimplementation of the tier-0 switch's own
// sphere-march algorithm).
// ---------------------------------------------------------------------------
TEST_F(RecipeBucketedIndirectDispatchTest, SpecializedPipelineMatchesTier0SphereMarchOracle) {
    std::cout << "[ bucketed-indirect ] selected physical device: '" << selectedDeviceName_ << "'\n";
    ASSERT_TRUE(deviceConfirmed_);

    // --- Recipe: single sphere, radius 1.5, at the recipe's own local origin. ---
    constexpr uint32_t kHotRecipeId = 5;
    Vixen::SVO::Recipe::SdfInstruction prog[1]{};
    prog[0].opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    prog[0].data[0] = 0.0f; prog[0].data[1] = 0.0f; prog[0].data[2] = 0.0f; prog[0].data[3] = 1.5f;

    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    entry.bytecode.assign(prog, prog + 1);
    entry.boundRadius = 2.0f;      // conservative: sphere radius 1.5 + margin
    entry.stepRelaxation = 1.0f;

    // --- Synthetic scene: 4 instances of the SAME hot recipe, spaced along X. NOTE: per this
    // codebase's own existing convention (confirmed by inspection of TraceWorld.glsl's
    // recipeId>=2 branch — evalRecipeField/traceUberRecipeBody take the raw world-space march
    // point with NO per-instance worldPos offset, unlike the legacy recipeId<2 analytic path),
    // a recipeId>=2 recipe's actual FIELD GEOMETRY is not translated by instance worldPos.
    // The bound-sphere early-reject in TraceWorld.glsl/tier-0 ALSO does not add worldPos: it
    // calls getRecipeBoundSphere(recipeId, ...), which (UberShaderSplice.h) returns the
    // recipe's REGISTERED boundCenter verbatim as a compile-time constant — worldPos is never
    // combined with it at that call site. (RecipeInstanceBucketing.comp's OWN bucketing/
    // coverage pass separately computes `inst.worldPos + bound.center` for its screen-space
    // AABB projection — that is a different, bucketing-only convention and must not be
    // conflated with the tier-0 reject-sphere center used here.) So these 4 instances all
    // produce the SAME sphere at world origin, but exercise 4 separate bound-sphere-reject +
    // member-list-loop iterations — this test's job is proving the specialized pipeline
    // reproduces the tier-0 switch's march EXACTLY (same surprising behavior included), not
    // proving instances render at visually distinct positions. ---
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    auto addInstance = [&](glm::vec3 pos) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x; inst.worldPos[1] = pos.y; inst.worldPos[2] = pos.z;
        inst.renderScale = 1.0f;
        inst.recipeId = kHotRecipeId;
        instances.push_back(inst);
    };
    addInstance(glm::vec3(-6.0f, 0.0f, 0.0f));
    addInstance(glm::vec3(-2.0f, 0.0f, 0.0f));
    addInstance(glm::vec3( 2.0f, 0.0f, 0.0f));
    addInstance(glm::vec3( 6.0f, 0.0f, 0.0f));
    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());

    constexpr uint32_t kMaxBuckets = 256, kMaxMembersPerBucket = 64;
    constexpr uint32_t kScreenWidth = 256, kScreenHeight = 256;

    std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
    boundSpheres[kHotRecipeId] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, entry.boundRadius, entry.stepRelaxation, {0, 0, 0}};

    // Camera: looks down -Z at the whole row of spheres from a distance, framing all 4.
    const glm::vec3 eye(0.0f, 3.0f, 20.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float fovDeg = 45.0f;
    glm::mat4 projection = glm::perspective(glm::radians(fovDeg),
        float(kScreenWidth) / float(kScreenHeight), 0.1f, 200.0f);
    projection[1][1] *= -1.0f;
    glm::mat4 viewProj = projection * view;

    // ======================================================================
    // STEP 1: bucketing pre-pass (M1 shader, unmodified bucketing logic) + M2's new mode==2
    // finalize pass emitting a real VkDispatchIndirectCommand.
    // ======================================================================
    VkBuffer instBuf, boundBuf, countBuf, idxBuf, minXBuf, minYBuf, maxXBuf, maxYBuf, indirectBuf;
    VkDeviceMemory instMem, boundMem, countMem, idxMem, minXMem, minYMem, maxXMem, maxYMem, indirectMem;

    const VkDeviceSize instSize  = instanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
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
    // Indirect buffer: must carry VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT for vkCmdDispatchIndirect
    // to legally read it, IN ADDITION to STORAGE_BUFFER_BIT (the bucketing shader writes it as
    // an ordinary SSBO).
    CreateHostBuffer(indirectSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        indirectBuf, indirectMem, true);

    UploadBuffer(instMem,  instances.data(),    instSize);
    UploadBuffer(boundMem, boundSpheres.data(), boundSize);

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
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingLayout, 0, 1, &bucketingSet, 0, nullptr);

    BucketingPush pcInit{};
    pcInit.viewProj = viewProj; pcInit.instanceCount = instanceCount;
    pcInit.maxBuckets = kMaxBuckets; pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
    pcInit.screenWidth = kScreenWidth; pcInit.screenHeight = kScreenHeight;
    pcInit.mode = 1;  // init extrema
    vkCmdPushConstants(cmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
    vkCmdDispatch(cmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    BucketingPush pcBucket = pcInit; pcBucket.mode = 0;  // bucket + coverage
    vkCmdPushConstants(cmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
    vkCmdDispatch(cmd, (instanceCount + 63) / 64, 1, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    BucketingPush pcFinalize = pcInit; pcFinalize.mode = 2;  // Task 4: emit indirect command
    vkCmdPushConstants(cmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcFinalize), &pcFinalize);
    vkCmdDispatch(cmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    std::vector<uint32_t> bucketCounts, bucketIndices, minXBits, minYBits, maxXBits, maxYBits, indirectCmds;
    ReadbackBuffer(countMem, countSize, bucketCounts);
    ReadbackBuffer(idxMem, idxSize, bucketIndices);
    ReadbackBuffer(minXMem, extremaSize, minXBits);
    ReadbackBuffer(minYMem, extremaSize, minYBits);
    ReadbackBuffer(maxXMem, extremaSize, maxXBits);
    ReadbackBuffer(maxYMem, extremaSize, maxYBits);
    ReadbackBuffer(indirectMem, indirectSize, indirectCmds);

    ASSERT_EQ(bucketCounts[kHotRecipeId], instanceCount) << "bucketing pre-pass membership mismatch";
    const uint32_t rectMinX = minXBits[kHotRecipeId] == 0xFFFFFFFFu ? 0
        : static_cast<uint32_t>(*reinterpret_cast<const float*>(&minXBits[kHotRecipeId]));
    const uint32_t rectMinY = static_cast<uint32_t>(*reinterpret_cast<const float*>(&minYBits[kHotRecipeId]));
    const uint32_t indirectX = indirectCmds[kHotRecipeId * 3 + 0];
    const uint32_t indirectY = indirectCmds[kHotRecipeId * 3 + 1];
    const uint32_t indirectZ = indirectCmds[kHotRecipeId * 3 + 2];
    ASSERT_GT(indirectX, 0u) << "indirect command has zero X workgroups — bucket coverage was empty/degenerate";
    ASSERT_GT(indirectY, 0u) << "indirect command has zero Y workgroups";
    ASSERT_EQ(indirectZ, 1u);
    std::printf("[BUCKETED-INDIRECT] hot recipe %u: count=%u rect origin=[%u,%u] indirect=[%u,%u,%u]\n",
                kHotRecipeId, bucketCounts[kHotRecipeId], rectMinX, rectMinY, indirectX, indirectY, indirectZ);

    // ======================================================================
    // STEP 2: compile the specialized single-recipe shader (Task 5) synchronously.
    // ======================================================================
    std::ifstream coreFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(coreFile.good()) << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream coreSs; coreSs << coreFile.rdbuf();
    const std::string sdfCoreGlsl = coreSs.str();

    const std::string specializedSrc =
        Vixen::SVO::Recipe::EmitSpecializedRecipeComputeShader(entry, kHotRecipeId, sdfCoreGlsl);

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.validateSpirv = false;  // ponytail: known glslang SPIR-V validator quirk (see test_procedural_recipe_render.cpp)
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, specializedSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "Specialized shader GLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << specializedSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Pipeline cache-key convention (mirrors ComputePipelineNode.cpp:261-282): programName +
    // SHA256(spirv). This standalone test does not go through ComputePipelineCacher (no
    // RenderGraph context here), but records the key computation to prove the convention is
    // followable from this shader's own compiled output.
    {
        std::ostringstream keyLog;
        keyLog << "sdfRecipe_" << kHotRecipeId << "_specialized:<sha256 of "
               << compOut.spirv.size() << " words>";
        std::printf("[BUCKETED-INDIRECT] pipeline cache key convention: %s\n", keyLog.str().c_str());
    }

    // ======================================================================
    // STEP 3: dispatch the specialized shader via vkCmdDispatchIndirect against the real
    // indirect command produced in Step 1.
    // ======================================================================
    VkBuffer specInstBuf, specMembersBuf, hitRecordBuf;
    VkDeviceMemory specInstMem, specMembersMem, hitRecordMem;
    CreateHostBuffer(instSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, specInstBuf, specInstMem, false);
    UploadBuffer(specInstMem, instances.data(), instSize);

    // This bucket's compacted member list, sliced out of bucketIndices[] at row kHotRecipeId.
    std::vector<uint32_t> members(bucketIndices.begin() + kHotRecipeId * kMaxMembersPerBucket,
                                   bucketIndices.begin() + kHotRecipeId * kMaxMembersPerBucket + instanceCount);
    const VkDeviceSize membersSize = instanceCount * sizeof(uint32_t);
    CreateHostBuffer(membersSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, specMembersBuf, specMembersMem, false);
    UploadBuffer(specMembersMem, members.data(), membersSize);

    const VkDeviceSize hitRecordSize = static_cast<VkDeviceSize>(kScreenWidth) * kScreenHeight * sizeof(HitRecordCpu);
    CreateHostBuffer(hitRecordSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);

    VkShaderModuleCreateInfo ssmci{};
    ssmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ssmci.codeSize = compOut.spirv.size() * sizeof(uint32_t); ssmci.pCode = compOut.spirv.data();
    VkShaderModule specModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &ssmci, nullptr, &specModule), VK_SUCCESS);

    const std::array<VkDescriptorSetLayoutBinding, 3> specBindings = {bind(0), bind(1), bind(2)};
    VkDescriptorSetLayoutCreateInfo sdslci{};
    sdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sdslci.bindingCount = static_cast<uint32_t>(specBindings.size()); sdslci.pBindings = specBindings.data();
    VkDescriptorSetLayout specDsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &sdslci, nullptr, &specDsl), VK_SUCCESS);

    VkPushConstantRange spcr{};
    spcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; spcr.offset = 0; spcr.size = sizeof(SpecializedPush);
    VkPipelineLayoutCreateInfo splci{};
    splci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    splci.setLayoutCount = 1; splci.pSetLayouts = &specDsl;
    splci.pushConstantRangeCount = 1; splci.pPushConstantRanges = &spcr;
    VkPipelineLayout specLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &splci, nullptr, &specLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo scpci{};
    scpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    scpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    scpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    scpci.stage.module = specModule; scpci.stage.pName = "main";
    scpci.layout = specLayout;
    VkPipeline specPipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &scpci, nullptr, &specPipeline), VK_SUCCESS);

    VkDescriptorPoolSize sPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo sdpci{};
    sdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    sdpci.maxSets = 1; sdpci.poolSizeCount = 1; sdpci.pPoolSizes = &sPoolSize;
    VkDescriptorPool specPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &sdpci, nullptr, &specPool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo sdsai{};
    sdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sdsai.descriptorPool = specPool; sdsai.descriptorSetCount = 1; sdsai.pSetLayouts = &specDsl;
    VkDescriptorSet specSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &sdsai, &specSet), VK_SUCCESS);

    VkDescriptorBufferInfo specInstInfo{specInstBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo specMembersInfo{specMembersBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo hitRecordInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
    auto wSpecBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = specSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 3> specWrites = {
        wSpecBuf(0, &specInstInfo), wSpecBuf(1, &specMembersInfo), wSpecBuf(2, &hitRecordInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(specWrites.size()), specWrites.data(), 0, nullptr);

    const glm::vec3 camDir = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 camRight = glm::normalize(glm::cross(camDir, worldUp));
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));

    SpecializedPush specPc{};
    specPc.cameraPos = eye; specPc.cameraDir = camDir; specPc.fov = fovDeg;
    specPc.cameraUp = camUp; specPc.aspect = float(kScreenWidth) / float(kScreenHeight);
    specPc.cameraRight = camRight;
    specPc.memberCount = instanceCount;
    specPc.screenWidth = kScreenWidth; specPc.screenHeight = kScreenHeight;
    specPc.rectMinX = rectMinX; specPc.rectMinY = rectMinY;
    specPc.boundRadius = entry.boundRadius; specPc.stepRelaxation = entry.stepRelaxation;

    VkCommandBuffer cmd2 = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd2), VK_SUCCESS);
    ASSERT_EQ(vkBeginCommandBuffer(cmd2, &bi), VK_SUCCESS);

    vkCmdBindPipeline(cmd2, VK_PIPELINE_BIND_POINT_COMPUTE, specPipeline);
    vkCmdBindDescriptorSets(cmd2, VK_PIPELINE_BIND_POINT_COMPUTE, specLayout, 0, 1, &specSet, 0, nullptr);
    vkCmdPushConstants(cmd2, specLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(specPc), &specPc);
    // Task 4's mechanism under real test: vkCmdDispatchIndirect against the buffer M2's
    // finalize pass wrote, at this bucket's own byte offset (kHotRecipeId * 3 uint32s).
    vkCmdDispatchIndirect(cmd2, indirectBuf, kHotRecipeId * 3 * sizeof(uint32_t));

    VkMemoryBarrier hostBarrier2{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier2.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd2, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &hostBarrier2, 0, nullptr, 0, nullptr);
    ASSERT_EQ(vkEndCommandBuffer(cmd2), VK_SUCCESS);

    VkSubmitInfo si2{}; si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si2.commandBufferCount = 1; si2.pCommandBuffers = &cmd2;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si2, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    std::vector<HitRecordCpu> hitRecords;
    ReadbackBuffer(hitRecordMem, hitRecordSize, hitRecords);

    // ======================================================================
    // STEP 4: correctness gate — compare GPU HitRecord against the CPU oracle for every
    // covered pixel. This is the milestone's decisive assertion.
    // ======================================================================
    // Scans the FULL screen, not just the dispatched rect — a pixel outside the rect must read
    // back as a miss (never written by the specialized dispatch) AND the oracle must also
    // report a miss there, or the coverage rect under-covered (a real correctness bug, not just
    // an efficiency concern) and this loop is what catches it.
    int gpuHits = 0, oracleHits = 0, matchedHits = 0;
    float maxHitTDelta = 0.0f;

    for (uint32_t py = 0; py < kScreenHeight; ++py) {
        for (uint32_t px = 0; px < kScreenWidth; ++px) {
            const uint32_t hitIdx = py * kScreenWidth + px;
            const HitRecordCpu& gpuRec = hitRecords[hitIdx];
            const bool gpuHit = (gpuRec.flags & 0x1u) != 0u;

            glm::vec2 uv = (glm::vec2(px, py) + 0.5f) / glm::vec2(float(kScreenWidth), float(kScreenHeight));
            glm::vec2 ndc = uv * 2.0f - 1.0f; ndc.y = -ndc.y;
            const float tanHalfFov = std::tan(glm::radians(fovDeg * 0.5f));
            const glm::vec3 rayDir = glm::normalize(camDir + camRight * ndc.x * tanHalfFov * specPc.aspect
                                                            + camUp    * ndc.y * tanHalfFov);

            bool oracleAnyHit = false; float oracleBestT = 1e30f; glm::vec3 oracleNormal(0.0f, 1.0f, 0.0f);
            for (const auto& inst : instances) {
                // Tier-0's getRecipeBoundSphere returns the recipe's REGISTERED boundCenter
                // verbatim (UberShaderSplice.h emits it as a compile-time constant baked from
                // RecipeEntry::boundCenter) — worldPos is never added to it at the
                // TraceWorld.glsl call site. Must match entry.boundCenter, not inst.worldPos.
                glm::vec3 boundCenter = entry.boundCenter;
                glm::vec3 n; float t;
                std::array<float, 6> params{};
                std::copy(std::begin(inst.recipeParams), std::end(inst.recipeParams), params.begin());
                if (CpuOracleTraceUberRecipeBody(prog, 1, boundCenter, entry.boundRadius, entry.stepRelaxation,
                                                  eye, rayDir, params, n, t)) {
                    if (t < oracleBestT) { oracleBestT = t; oracleNormal = n; oracleAnyHit = true; }
                }
            }

            if (gpuHit) ++gpuHits;
            if (oracleAnyHit) ++oracleHits;

            ASSERT_EQ(gpuHit, oracleAnyHit)
                << "hit/miss mismatch at pixel (" << px << "," << py << "): GPU=" << gpuHit
                << " oracle=" << oracleAnyHit;
            if (gpuHit && oracleAnyHit) {
                ++matchedHits;
                const float delta = std::abs(gpuRec.hitT - oracleBestT);
                maxHitTDelta = std::max(maxHitTDelta, delta);
                EXPECT_NEAR(gpuRec.hitT, oracleBestT, 0.05f)
                    << "hitT mismatch at pixel (" << px << "," << py << ")";
            }
        }
    }

    std::printf("[BUCKETED-INDIRECT] gpuHits=%d oracleHits=%d matchedHits=%d maxHitTDelta=%.5f\n",
                gpuHits, oracleHits, matchedHits, maxHitTDelta);
    ASSERT_GT(matchedHits, 0) << "no pixels hit at all — camera framing or march logic is broken (test bug, not a milestone finding)";

    // --- Cleanup ---
    vkDeviceWaitIdle(logicalDevice_);
    vkDestroyDescriptorPool(logicalDevice_, specPool, nullptr);
    vkDestroyPipeline(logicalDevice_, specPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, specLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, specDsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, specModule, nullptr);
    vkDestroyBuffer(logicalDevice_, specInstBuf, nullptr); vkFreeMemory(logicalDevice_, specInstMem, nullptr);
    vkDestroyBuffer(logicalDevice_, specMembersBuf, nullptr); vkFreeMemory(logicalDevice_, specMembersMem, nullptr);
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
