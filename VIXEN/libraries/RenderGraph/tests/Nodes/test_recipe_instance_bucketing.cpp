/**
 * @file test_recipe_instance_bucketing.cpp
 * @brief Recipe GPU Instance Bucketing Inc2 M1 live-run gate (Tasks 2-3).
 *
 * Dispatches the SHIPPED shaders/RecipeInstanceBucketing.comp directly (no RenderGraph node
 * involvement — this milestone's gate is proving the bucketing DATA is correct via SSBO
 * readback, isolated from dispatch/compositing risk per the M1 plan) against a KNOWN synthetic
 * scene: 3 distinct recipeIds, N instances each at known world positions, a known view-proj
 * matrix. Reads back bucketCounts[]/bucketIndices[]/bucketCoverage{MinX,MinY,MaxX,MaxY}[] and
 * asserts:
 *   - bucket membership: each recipeId's bucket contains EXACTLY its own instances' indices
 *     (order-independent set comparison — atomic-append order is not guaranteed).
 *   - screen-space coverage: the bucket's [minX,minY]-[maxX,maxY] pixel rect matches a
 *     hand-computed expected projection of the recipe's world-space bound sphere (translated to
 *     each member instance's worldPos, unioned across members) through the SAME view-proj matrix
 *     the shader used, via ProjectToPixel's identical math re-derived on the CPU side.
 *
 * DEVICE SELECTION: same contract as test_shell_revalidate_node.cpp / test_hitrecord_readback.cpp
 * — a real discrete/integrated GPU is PREFERRED; software (lavapipe/llvmpipe) or Dozen only as a
 * fallback when no real GPU is visible. Some usable device is hard-asserted before any submit.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef RECIPE_BUCKETING_SPV
#error "RECIPE_BUCKETING_SPV (path to compiled RecipeInstanceBucketing.spv) must be defined by CMake"
#endif

using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// Byte-identical to RecipeInstanceBucketing.comp's Push block. Load-Tier Contract M1 added
// raySizeCoef/raySizeBias/cameraPos (KI-034 is the exact bug class this staleness risks --
// every hand-mirrored PushConstants struct for this shader MUST be updated in lockstep with
// the shader's own push_constant block, or vkCreateComputePipelines fails
// VUID-VkComputePipelineCreateInfo-layout-10069). Value-initialization (`PushConstants pc{};`)
// below zero-inits the 3 new fields, which is exactly "gating disabled" -- existing tests that
// never set them keep their pre-M1 behavior unchanged.
struct PushConstants {
    glm::mat4 viewProj;
    uint32_t  instanceCount;
    uint32_t  maxBuckets;
    uint32_t  maxMembersPerBucket;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  mode;
    float     raySizeCoef;
    float     raySizeBias;
    glm::vec3 cameraPos;
};

// Byte-identical to the shader's RecipeBoundSphere struct. Load-Tier Contract M1 added
// gateFootprintThreshold, M2 added precisionFootprintThreshold (both 0.0 = not opted in, same
// convention RecipeEntry uses).
struct RecipeBoundSphereCpu {
    float center[3];
    float radius;
    float relaxation;
    float gateFootprintThreshold;
    float precisionFootprintThreshold;
    float _pad;
};
static_assert(sizeof(RecipeBoundSphereCpu) == 32, "RecipeBoundSphereCpu std430 mirror size");

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

}  // namespace

class RecipeInstanceBucketingTest : public ::testing::Test {
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
        appInfo.pApplicationName = "test_recipe_instance_bucketing";
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
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
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
};

namespace {

constexpr uint32_t kMaxBuckets           = 256;
constexpr uint32_t kMaxMembersPerBucket  = 64;
constexpr uint32_t kScreenWidth          = 256;
constexpr uint32_t kScreenHeight         = 256;

// CPU re-derivation of RecipeInstanceBucketing.comp's ProjectToPixel, for computing the
// expected coverage rect independently of the shader under test.
bool ProjectToPixelCpu(const glm::mat4& viewProj, glm::vec3 worldPos, glm::vec2& outPixel) {
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outPixel = glm::vec2((ndc.x * 0.5f + 0.5f) * float(kScreenWidth),
                          (ndc.y * 0.5f + 0.5f) * float(kScreenHeight));
    return true;
}

// Mirrors the shader's 7-point conservative footprint + clamp, for one instance.
void ExpectedCoverageForInstance(const glm::mat4& viewProj, glm::vec3 worldPos,
                                  glm::vec3 boundCenter, float radius,
                                  bool& anyOut, float& minX, float& minY, float& maxX, float& maxY) {
    glm::vec3 sphereCenter = worldPos + boundCenter;
    const glm::vec3 offsets[7] = {
        glm::vec3(0.0f),
        glm::vec3(radius, 0, 0), glm::vec3(-radius, 0, 0),
        glm::vec3(0, radius, 0), glm::vec3(0, -radius, 0),
        glm::vec3(0, 0, radius), glm::vec3(0, 0, -radius),
    };
    bool any = false;
    float lminX = 0, lminY = 0, lmaxX = 0, lmaxY = 0;
    for (const auto& off : offsets) {
        glm::vec2 pixel;
        if (ProjectToPixelCpu(viewProj, sphereCenter + off, pixel)) {
            if (!any) { lminX = lmaxX = pixel.x; lminY = lmaxY = pixel.y; any = true; }
            else {
                lminX = std::min(lminX, pixel.x); lmaxX = std::max(lmaxX, pixel.x);
                lminY = std::min(lminY, pixel.y); lmaxY = std::max(lmaxY, pixel.y);
            }
        }
    }
    if (!any) { anyOut = false; return; }
    lminX = std::clamp(std::floor(lminX), 0.0f, float(kScreenWidth));
    lminY = std::clamp(std::floor(lminY), 0.0f, float(kScreenHeight));
    lmaxX = std::clamp(std::ceil(lmaxX),  0.0f, float(kScreenWidth));
    lmaxY = std::clamp(std::ceil(lmaxY),  0.0f, float(kScreenHeight));
    if (!anyOut) { minX = lminX; minY = lminY; maxX = lmaxX; maxY = lmaxY; }
    else {
        minX = std::min(minX, lminX); maxX = std::max(maxX, lmaxX);
        minY = std::min(minY, lminY); maxY = std::max(maxY, lmaxY);
    }
    anyOut = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// The decisive test: 3 distinct recipeIds, N instances each at known positions, a known
// camera. Dispatches the real shader (init pass mode=1, then bucket+coverage pass mode=0),
// reads back every output SSBO, and cross-checks against hand-computed expectations.
// ---------------------------------------------------------------------------
TEST_F(RecipeInstanceBucketingTest, BucketsAndCoverageMatchKnownSyntheticScene) {
    std::cout << "[ bucketing ] selected physical device: '" << selectedDeviceName_
              << "'\n";
    ASSERT_TRUE(deviceConfirmed_);

    // --- Synthetic scene: 3 recipeIds, distinct instance counts each, known positions. ---
    constexpr uint32_t kRecipeA = 3, kRecipeB = 7, kRecipeC = 41;
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    auto addInstance = [&](uint32_t recipeId, glm::vec3 pos) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x; inst.worldPos[1] = pos.y; inst.worldPos[2] = pos.z;
        inst.renderScale = 1.0f;
        inst.recipeId = recipeId;
        instances.push_back(inst);
    };
    // Recipe A: 5 instances along X.
    for (int i = 0; i < 5; ++i) addInstance(kRecipeA, glm::vec3(float(i) * 2.0f, 0.0f, 0.0f));
    // Recipe B: 3 instances along Y, offset from A.
    for (int i = 0; i < 3; ++i) addInstance(kRecipeB, glm::vec3(10.0f, float(i) * 2.0f, 0.0f));
    // Recipe C: 8 instances along Z, offset again.
    for (int i = 0; i < 8; ++i) addInstance(kRecipeC, glm::vec3(-10.0f, 0.0f, float(i) * 1.5f));

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    ASSERT_EQ(instanceCount, 16u);

    // Per-recipe bound spheres (dense array indexed by recipeId, up to kMaxBuckets).
    std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
    boundSpheres[kRecipeA] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    boundSpheres[kRecipeB] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 1.5f, 1.0f, 0.0f, 0.0f, 0.0f};
    boundSpheres[kRecipeC] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 0.75f, 1.0f, 0.0f, 0.0f, 0.0f};

    // Known camera: looks down -Z-ish at the whole scene from a distance, framing all 3 clusters.
    const glm::vec3 eye(0.0f, 15.0f, 40.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f),
        float(kScreenWidth) / float(kScreenHeight), 0.1f, 200.0f);
    projection[1][1] *= -1.0f;  // Vulkan Y-flip, mirrors CameraNode.cpp
    glm::mat4 viewProj = projection * view;

    // --- Expected results, computed independently on the CPU. ---
    std::map<uint32_t, std::vector<uint32_t>> expectedMembers;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        expectedMembers[instances[i].recipeId].push_back(i);
    }
    struct ExpectedCoverage { bool any = false; float minX, minY, maxX, maxY; };
    std::map<uint32_t, ExpectedCoverage> expectedCoverage;
    for (const auto& [recipeId, members] : expectedMembers) {
        ExpectedCoverage cov{};
        const auto& bs = boundSpheres[recipeId];
        glm::vec3 center(bs.center[0], bs.center[1], bs.center[2]);
        for (uint32_t idx : members) {
            glm::vec3 pos(instances[idx].worldPos[0], instances[idx].worldPos[1], instances[idx].worldPos[2]);
            ExpectedCoverageForInstance(viewProj, pos, center, bs.radius,
                                        cov.any, cov.minX, cov.minY, cov.maxX, cov.maxY);
        }
        expectedCoverage[recipeId] = cov;
    }

    // --- GPU buffers. ---
    VkBuffer instBuf = VK_NULL_HANDLE, boundBuf = VK_NULL_HANDLE, countBuf = VK_NULL_HANDLE,
             idxBuf = VK_NULL_HANDLE, minXBuf = VK_NULL_HANDLE, minYBuf = VK_NULL_HANDLE,
             maxXBuf = VK_NULL_HANDLE, maxYBuf = VK_NULL_HANDLE, indirectBuf = VK_NULL_HANDLE,
             precCountBuf = VK_NULL_HANDLE, precIdxBuf = VK_NULL_HANDLE;
    VkDeviceMemory instMem = VK_NULL_HANDLE, boundMem = VK_NULL_HANDLE, countMem = VK_NULL_HANDLE,
                   idxMem = VK_NULL_HANDLE, minXMem = VK_NULL_HANDLE, minYMem = VK_NULL_HANDLE,
                   maxXMem = VK_NULL_HANDLE, maxYMem = VK_NULL_HANDLE, indirectMem = VK_NULL_HANDLE,
                   precCountMem = VK_NULL_HANDLE, precIdxMem = VK_NULL_HANDLE;

    const VkDeviceSize instSize  = instanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
    const VkDeviceSize boundSize = kMaxBuckets * sizeof(RecipeBoundSphereCpu);
    const VkDeviceSize countSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize idxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * kMaxMembersPerBucket * sizeof(uint32_t);
    const VkDeviceSize extremaSize = kMaxBuckets * sizeof(uint32_t);
    // Inc2 M2 Task 4: RecipeInstanceBucketing.comp gained a 9th binding (BucketIndirectCommandBuffer,
    // binding 8) for the new mode==2 finalize pass — this test doesn't exercise mode==2, but the
    // descriptor set layout must still declare every binding the SPIR-V module references, or
    // vkCreateComputePipelines fails VUID-VkComputePipelineCreateInfo-layout-07988.
    const VkDeviceSize indirectSize = static_cast<VkDeviceSize>(kMaxBuckets) * 3 * sizeof(uint32_t);
    // Load-Tier Contract M2: bindings 9-10 (precision sub-bucket pair) — same "must declare every
    // binding the SPIR-V references" reasoning as the indirect-command buffer above; this test
    // doesn't exercise precision tiering, but the pipeline layout must still be complete.
    const VkDeviceSize precCountSize = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * sizeof(uint32_t);
    const VkDeviceSize precIdxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * kMaxMembersPerBucket * sizeof(uint32_t);

    CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
    CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
    CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
    CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
    CreateHostBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indirectBuf, indirectMem, true);
    CreateHostBuffer(precCountSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precCountBuf, precCountMem, true);
    CreateHostBuffer(precIdxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precIdxBuf,   precIdxMem,   true);

    UploadBuffer(instMem,  instances.data(),     instSize);
    UploadBuffer(boundMem, boundSpheres.data(),  boundSize);

    // --- Pipeline. ---
    const std::vector<uint32_t> spirv = ReadSpirv(RECIPE_BUCKETING_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << RECIPE_BUCKETING_SPV;
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

    auto bind = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b; lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb.descriptorCount = 1;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    const std::array<VkDescriptorSetLayoutBinding, 11> bindings = {
        bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
        bind(9), bind(10),
    };
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shaderModule; cpci.stage.pName = "main";
    cpci.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

    VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo boundInfo{boundBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo countInfo{countBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo idxInfo{idxBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minXInfo{minXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minYInfo{minYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxXInfo{maxXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxYInfo{maxYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo indirectInfo{indirectBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precCountInfo{precCountBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precIdxInfo{precIdxBuf, 0, VK_WHOLE_SIZE};

    auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 11> writes = {
        wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
        wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
        wBuf(8, &indirectInfo), wBuf(9, &precCountInfo), wBuf(10, &precIdxInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

    PushConstants pcInit{};
    pcInit.viewProj = viewProj;
    pcInit.instanceCount = instanceCount;
    pcInit.maxBuckets = kMaxBuckets;
    pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
    pcInit.screenWidth = kScreenWidth;
    pcInit.screenHeight = kScreenHeight;
    pcInit.mode = 1;  // init pass
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
    vkCmdDispatch(cmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier initBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    initBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &initBarrier, 0, nullptr, 0, nullptr);

    PushConstants pcBucket = pcInit;
    pcBucket.mode = 0;  // bucket + coverage pass
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
    vkCmdDispatch(cmd, (instanceCount + 63) / 64, 1, 1);

    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    // --- Readback. ---
    auto readback = [&](VkDeviceMemory mem, VkDeviceSize size, auto& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(typename std::decay_t<decltype(out)>::value_type));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    };

    std::vector<uint32_t> bucketCounts, bucketIndices, minXBits, minYBits, maxXBits, maxYBits;
    readback(countMem, countSize, bucketCounts);
    readback(idxMem, idxSize, bucketIndices);
    readback(minXMem, extremaSize, minXBits);
    readback(minYMem, extremaSize, minYBits);
    readback(maxXMem, extremaSize, maxXBits);
    readback(maxYMem, extremaSize, maxYBits);

    // --- Assertions: bucket membership. ---
    for (const auto& [recipeId, expectedIdx] : expectedMembers) {
        ASSERT_EQ(bucketCounts[recipeId], expectedIdx.size())
            << "recipeId " << recipeId << ": bucket count mismatch";
        std::set<uint32_t> actualSet, expectedSet(expectedIdx.begin(), expectedIdx.end());
        for (uint32_t slot = 0; slot < bucketCounts[recipeId]; ++slot) {
            actualSet.insert(bucketIndices[recipeId * kMaxMembersPerBucket + slot]);
        }
        EXPECT_EQ(actualSet, expectedSet) << "recipeId " << recipeId << ": bucket membership mismatch";
    }
    // Every OTHER bucket (not one of the 3 recipes) must be empty.
    for (uint32_t id = 0; id < kMaxBuckets; ++id) {
        if (expectedMembers.count(id)) continue;
        EXPECT_EQ(bucketCounts[id], 0u) << "recipeId " << id << " unexpectedly non-empty";
    }

    // --- Assertions: screen-space coverage. ---
    constexpr float kPixelTolerance = 1.5f;  // float rounding slack between GPU/CPU projection
    for (const auto& [recipeId, expected] : expectedCoverage) {
        ASSERT_TRUE(expected.any) << "recipeId " << recipeId << ": expected coverage was empty (test bug)";
        const float actualMinX = *reinterpret_cast<const float*>(&minXBits[recipeId]);
        const float actualMinY = *reinterpret_cast<const float*>(&minYBits[recipeId]);
        const float actualMaxX = *reinterpret_cast<const float*>(&maxXBits[recipeId]);
        const float actualMaxY = *reinterpret_cast<const float*>(&maxYBits[recipeId]);

        EXPECT_NEAR(actualMinX, expected.minX, kPixelTolerance) << "recipeId " << recipeId << " minX";
        EXPECT_NEAR(actualMinY, expected.minY, kPixelTolerance) << "recipeId " << recipeId << " minY";
        EXPECT_NEAR(actualMaxX, expected.maxX, kPixelTolerance) << "recipeId " << recipeId << " maxX";
        EXPECT_NEAR(actualMaxY, expected.maxY, kPixelTolerance) << "recipeId " << recipeId << " maxY";

        std::printf("[BUCKETING] recipe %u: count=%u coverage=[%.1f,%.1f]-[%.1f,%.1f] (expected [%.1f,%.1f]-[%.1f,%.1f])\n",
                    recipeId, bucketCounts[recipeId], actualMinX, actualMinY, actualMaxX, actualMaxY,
                    expected.minX, expected.minY, expected.maxX, expected.maxY);
    }

    vkDeviceWaitIdle(logicalDevice_);
    vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
    vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
    vkDestroyBuffer(logicalDevice_, instBuf, nullptr);  vkFreeMemory(logicalDevice_, instMem, nullptr);
    vkDestroyBuffer(logicalDevice_, boundBuf, nullptr); vkFreeMemory(logicalDevice_, boundMem, nullptr);
    vkDestroyBuffer(logicalDevice_, countBuf, nullptr); vkFreeMemory(logicalDevice_, countMem, nullptr);
    vkDestroyBuffer(logicalDevice_, idxBuf, nullptr);   vkFreeMemory(logicalDevice_, idxMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minXBuf, nullptr);  vkFreeMemory(logicalDevice_, minXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minYBuf, nullptr);  vkFreeMemory(logicalDevice_, minYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxXBuf, nullptr);  vkFreeMemory(logicalDevice_, maxXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxYBuf, nullptr);  vkFreeMemory(logicalDevice_, maxYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, indirectBuf, nullptr); vkFreeMemory(logicalDevice_, indirectMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precCountBuf, nullptr); vkFreeMemory(logicalDevice_, precCountMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precIdxBuf, nullptr);   vkFreeMemory(logicalDevice_, precIdxMem, nullptr);
}

// ---------------------------------------------------------------------------
// Recipe Load-Tier Contract M1 (gating tier): a recipe that opts in
// (gateFootprintThreshold > 0) must have its far/small-footprint instances excluded from
// bucketing/promotion entirely (no bucketIndices entry, no coverage contribution), while its
// near/large-footprint instances bucket normally. A recipe that does NOT opt in
// (gateFootprintThreshold == 0, the default) must be completely unaffected regardless of
// distance -- this is the n=0 regression check the M1 prompt requires.
// ---------------------------------------------------------------------------
TEST_F(RecipeInstanceBucketingTest, GatingTierExcludesFarInstanceKeepsNearInstanceAndNonParticipant) {
    std::cout << "[ bucketing-gate ] selected physical device: '" << selectedDeviceName_
              << "'\n";
    ASSERT_TRUE(deviceConfirmed_);

    // recipeGated opts in (gateFootprintThreshold set below); recipeControl does not, and is
    // placed at the SAME far distance as recipeGated's far instance to prove non-participation
    // is unaffected by distance.
    constexpr uint32_t kRecipeGated = 5, kRecipeControl = 9;
    constexpr float kBoundRadius = 1.0f;

    // Camera: pinhole, looking down -Z from the origin. raySizeCoef mirrors
    // RaySizeCoefNode's real formula (2*tan(fov/screenHeight/2)) at a representative fov/height
    // so the gate math below is realistic, not a contrived huge/tiny value.
    const float fovYRadians = glm::radians(45.0f);
    const float raySizeCoef = 2.0f * std::tan((fovYRadians / float(kScreenHeight)) * 0.5f);
    const float raySizeBias = 0.0f;
    const glm::vec3 cameraPos(0.0f, 0.0f, 0.0f);

    // footprint(distance) = distance * raySizeCoef + raySizeBias. Pick a threshold, then a
    // NEAR distance whose footprint is comfortably ABOVE it and a FAR distance whose footprint
    // is comfortably BELOW it.
    constexpr float kGateThreshold = 0.05f;
    const float nearDistance = (kGateThreshold * 4.0f) / raySizeCoef;   // footprint ~4x threshold
    const float farDistance  = (kGateThreshold * 0.25f) / raySizeCoef;  // footprint ~0.25x threshold
    ASSERT_GT(nearDistance, farDistance) << "test construction sanity check";

    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    auto addInstance = [&](uint32_t recipeId, glm::vec3 pos) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x; inst.worldPos[1] = pos.y; inst.worldPos[2] = pos.z;
        inst.renderScale = 1.0f;
        inst.recipeId = recipeId;
        instances.push_back(inst);
    };
    // recipeGated: one near instance (should bucket normally) and one far instance (should be
    // gated out entirely -- absent from bucketCounts/bucketIndices/coverage).
    constexpr uint32_t kNearIdx = 0, kFarGatedIdx = 1, kFarControlIdx = 2;
    addInstance(kRecipeGated, glm::vec3(0.0f, 0.0f, -nearDistance));
    addInstance(kRecipeGated, glm::vec3(0.0f, 0.0f, -farDistance));
    // recipeControl: same far distance as recipeGated's gated-out instance, but NOT opted in --
    // must bucket normally (proves the gate doesn't leak onto non-participating recipes).
    addInstance(kRecipeControl, glm::vec3(0.0f, 0.0f, -farDistance));
    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    ASSERT_EQ(instanceCount, 3u);

    std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
    boundSpheres[kRecipeGated]   = RecipeBoundSphereCpu{{0, 0, 0}, kBoundRadius, 1.0f, kGateThreshold, 0.0f, 0.0f};
    boundSpheres[kRecipeControl] = RecipeBoundSphereCpu{{0, 0, 0}, kBoundRadius, 1.0f, 0.0f, 0.0f, 0.0f};

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(fovYRadians,
        float(kScreenWidth) / float(kScreenHeight), 0.01f, 10000.0f);
    projection[1][1] *= -1.0f;  // Vulkan Y-flip, mirrors CameraNode.cpp
    glm::mat4 viewProj = projection * view;

    // --- GPU buffers (same shape as the decisive test above). ---
    VkBuffer instBuf = VK_NULL_HANDLE, boundBuf = VK_NULL_HANDLE, countBuf = VK_NULL_HANDLE,
             idxBuf = VK_NULL_HANDLE, minXBuf = VK_NULL_HANDLE, minYBuf = VK_NULL_HANDLE,
             maxXBuf = VK_NULL_HANDLE, maxYBuf = VK_NULL_HANDLE, indirectBuf = VK_NULL_HANDLE,
             precCountBuf = VK_NULL_HANDLE, precIdxBuf = VK_NULL_HANDLE;
    VkDeviceMemory instMem = VK_NULL_HANDLE, boundMem = VK_NULL_HANDLE, countMem = VK_NULL_HANDLE,
                   idxMem = VK_NULL_HANDLE, minXMem = VK_NULL_HANDLE, minYMem = VK_NULL_HANDLE,
                   maxXMem = VK_NULL_HANDLE, maxYMem = VK_NULL_HANDLE, indirectMem = VK_NULL_HANDLE,
                   precCountMem = VK_NULL_HANDLE, precIdxMem = VK_NULL_HANDLE;

    const VkDeviceSize instSize  = instanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
    const VkDeviceSize boundSize = kMaxBuckets * sizeof(RecipeBoundSphereCpu);
    const VkDeviceSize countSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize idxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * kMaxMembersPerBucket * sizeof(uint32_t);
    const VkDeviceSize extremaSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize indirectSize = static_cast<VkDeviceSize>(kMaxBuckets) * 3 * sizeof(uint32_t);
    // Load-Tier Contract M2: bindings 9-10 (precision sub-bucket pair) — this test doesn't
    // exercise precision tiering, but the pipeline layout must still declare every binding the
    // SPIR-V module references (same reasoning as the indirect-command buffer's own comment).
    const VkDeviceSize precCountSize = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * sizeof(uint32_t);
    const VkDeviceSize precIdxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * kMaxMembersPerBucket * sizeof(uint32_t);

    CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
    CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
    CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
    CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
    CreateHostBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indirectBuf, indirectMem, true);
    CreateHostBuffer(precCountSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precCountBuf, precCountMem, true);
    CreateHostBuffer(precIdxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precIdxBuf,   precIdxMem,   true);

    UploadBuffer(instMem,  instances.data(),    instSize);
    UploadBuffer(boundMem, boundSpheres.data(), boundSize);

    const std::vector<uint32_t> spirv = ReadSpirv(RECIPE_BUCKETING_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << RECIPE_BUCKETING_SPV;
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

    auto bind = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b; lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb.descriptorCount = 1;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    const std::array<VkDescriptorSetLayoutBinding, 11> bindings = {
        bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
        bind(9), bind(10),
    };
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shaderModule; cpci.stage.pName = "main";
    cpci.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

    VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo boundInfo{boundBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo countInfo{countBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo idxInfo{idxBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minXInfo{minXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minYInfo{minYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxXInfo{maxXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxYInfo{maxYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo indirectInfo{indirectBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precCountInfo{precCountBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precIdxInfo{precIdxBuf, 0, VK_WHOLE_SIZE};

    auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 11> writes = {
        wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
        wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
        wBuf(8, &indirectInfo), wBuf(9, &precCountInfo), wBuf(10, &precIdxInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

    PushConstants pcInit{};
    pcInit.viewProj = viewProj;
    pcInit.instanceCount = instanceCount;
    pcInit.maxBuckets = kMaxBuckets;
    pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
    pcInit.screenWidth = kScreenWidth;
    pcInit.screenHeight = kScreenHeight;
    pcInit.mode = 1;  // init pass
    pcInit.raySizeCoef = raySizeCoef;
    pcInit.raySizeBias = raySizeBias;
    pcInit.cameraPos = cameraPos;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
    vkCmdDispatch(cmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier initBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    initBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &initBarrier, 0, nullptr, 0, nullptr);

    PushConstants pcBucket = pcInit;
    pcBucket.mode = 0;  // bucket + coverage pass
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
    vkCmdDispatch(cmd, (instanceCount + 63) / 64, 1, 1);

    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    auto readback = [&](VkDeviceMemory mem, VkDeviceSize size, auto& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(typename std::decay_t<decltype(out)>::value_type));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    };

    std::vector<uint32_t> bucketCounts, bucketIndices;
    readback(countMem, countSize, bucketCounts);
    readback(idxMem, idxSize, bucketIndices);

    // --- The decisive assertions ---
    // recipeGated's bucket must contain EXACTLY the near instance (kNearIdx), NOT the far one
    // (kFarGatedIdx) -- gated out entirely, not merely excluded from coverage.
    ASSERT_EQ(bucketCounts[kRecipeGated], 1u)
        << "recipeGated: expected exactly 1 bucketed instance (the near one), far instance "
           "should have been gated out";
    EXPECT_EQ(bucketIndices[kRecipeGated * kMaxMembersPerBucket + 0], kNearIdx)
        << "recipeGated: the bucketed instance should be the NEAR one, not the far (gated) one";

    // recipeControl (not opted in) must bucket its far instance normally -- proves the gate
    // does not leak onto non-participating recipes regardless of distance.
    ASSERT_EQ(bucketCounts[kRecipeControl], 1u)
        << "recipeControl: non-participating recipe must bucket its instance normally "
           "even though it is at the SAME far distance as recipeGated's gated-out instance";
    EXPECT_EQ(bucketIndices[kRecipeControl * kMaxMembersPerBucket + 0], kFarControlIdx);

    std::printf("[BUCKETING-GATE] recipeGated count=%u (expected 1, near only) "
                "recipeControl count=%u (expected 1, far but non-participating)\n",
                bucketCounts[kRecipeGated], bucketCounts[kRecipeControl]);

    vkDeviceWaitIdle(logicalDevice_);
    vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
    vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
    vkDestroyBuffer(logicalDevice_, instBuf, nullptr);  vkFreeMemory(logicalDevice_, instMem, nullptr);
    vkDestroyBuffer(logicalDevice_, boundBuf, nullptr); vkFreeMemory(logicalDevice_, boundMem, nullptr);
    vkDestroyBuffer(logicalDevice_, countBuf, nullptr); vkFreeMemory(logicalDevice_, countMem, nullptr);
    vkDestroyBuffer(logicalDevice_, idxBuf, nullptr);   vkFreeMemory(logicalDevice_, idxMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minXBuf, nullptr);  vkFreeMemory(logicalDevice_, minXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minYBuf, nullptr);  vkFreeMemory(logicalDevice_, minYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxXBuf, nullptr);  vkFreeMemory(logicalDevice_, maxXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxYBuf, nullptr);  vkFreeMemory(logicalDevice_, maxYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, indirectBuf, nullptr); vkFreeMemory(logicalDevice_, indirectMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precCountBuf, nullptr); vkFreeMemory(logicalDevice_, precCountMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precIdxBuf, nullptr);   vkFreeMemory(logicalDevice_, precIdxMem, nullptr);
}

// ---------------------------------------------------------------------------
// Recipe Load-Tier Contract M2 (precision tier): a recipe that opts in
// (precisionFootprintThreshold > 0) must route its far/small-footprint instances into the
// TIER-1 (half-precision) precision sub-bucket and its near/large-footprint instances into
// TIER-0 (full-precision), while a non-participating recipe (precisionFootprintThreshold == 0,
// the default) must always resolve tier 0 regardless of distance -- the n=0 regression case
// this milestone's own prompt requires. This is ADDITIVE: every instance still lands in its
// plain per-recipe bucket (bucketCounts/bucketIndices, unaffected -- proven by re-running the
// existing tests in this file/binary) AND, only for opted-in recipes, in one of the new
// precision sub-buckets (precisionBucketCounts/precisionBucketIndices, bindings 9-10).
// ---------------------------------------------------------------------------
TEST_F(RecipeInstanceBucketingTest, PrecisionTierRoutesFarInstanceToHalfNearInstanceToFullNonParticipantAlwaysFull) {
    std::cout << "[ bucketing-precision ] selected physical device: '" << selectedDeviceName_
              << "'\n";
    ASSERT_TRUE(deviceConfirmed_);

    // recipePrecision opts in (precisionFootprintThreshold set below); recipeControl does not,
    // and is placed at the SAME far distance as recipePrecision's tier-1 instance to prove
    // non-participation is unaffected by distance (mirrors the M1 gating test's control shape).
    constexpr uint32_t kRecipePrecision = 6, kRecipeControl = 10;
    constexpr float kBoundRadius = 1.0f;

    const float fovYRadians = glm::radians(45.0f);
    const float raySizeCoef = 2.0f * std::tan((fovYRadians / float(kScreenHeight)) * 0.5f);
    const float raySizeBias = 0.0f;
    const glm::vec3 cameraPos(0.0f, 0.0f, 0.0f);

    // footprint(distance) = distance * raySizeCoef + raySizeBias. Pick a threshold, then a NEAR
    // distance whose footprint is comfortably ABOVE it (tier 0) and a FAR distance whose
    // footprint is comfortably BELOW it (tier 1) -- same construction as the M1 gating test.
    constexpr float kPrecisionThreshold = 0.05f;
    const float nearDistance = (kPrecisionThreshold * 4.0f) / raySizeCoef;
    const float farDistance  = (kPrecisionThreshold * 0.25f) / raySizeCoef;
    ASSERT_GT(nearDistance, farDistance) << "test construction sanity check";

    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    auto addInstance = [&](uint32_t recipeId, glm::vec3 pos) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x; inst.worldPos[1] = pos.y; inst.worldPos[2] = pos.z;
        inst.renderScale = 1.0f;
        inst.recipeId = recipeId;
        instances.push_back(inst);
    };
    constexpr uint32_t kNearIdx = 0, kFarPrecisionIdx = 1, kFarControlIdx = 2;
    addInstance(kRecipePrecision, glm::vec3(0.0f, 0.0f, -nearDistance));   // expect tier 0
    addInstance(kRecipePrecision, glm::vec3(0.0f, 0.0f, -farDistance));    // expect tier 1
    addInstance(kRecipeControl,   glm::vec3(0.0f, 0.0f, -farDistance));    // non-opted-in: tier 0
    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    ASSERT_EQ(instanceCount, 3u);

    std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
    boundSpheres[kRecipePrecision] = RecipeBoundSphereCpu{{0, 0, 0}, kBoundRadius, 1.0f, 0.0f, kPrecisionThreshold, 0.0f};
    boundSpheres[kRecipeControl]   = RecipeBoundSphereCpu{{0, 0, 0}, kBoundRadius, 1.0f, 0.0f, 0.0f, 0.0f};

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(fovYRadians,
        float(kScreenWidth) / float(kScreenHeight), 0.01f, 10000.0f);
    projection[1][1] *= -1.0f;
    glm::mat4 viewProj = projection * view;

    // --- GPU buffers: the same 9 (bindings 0-8) plus the 2 new precision sub-bucket buffers
    // (bindings 9-10, compound-keyed recipeId*2+tier, sized kMaxBuckets*2). ---
    VkBuffer instBuf = VK_NULL_HANDLE, boundBuf = VK_NULL_HANDLE, countBuf = VK_NULL_HANDLE,
             idxBuf = VK_NULL_HANDLE, minXBuf = VK_NULL_HANDLE, minYBuf = VK_NULL_HANDLE,
             maxXBuf = VK_NULL_HANDLE, maxYBuf = VK_NULL_HANDLE, indirectBuf = VK_NULL_HANDLE,
             precCountBuf = VK_NULL_HANDLE, precIdxBuf = VK_NULL_HANDLE;
    VkDeviceMemory instMem = VK_NULL_HANDLE, boundMem = VK_NULL_HANDLE, countMem = VK_NULL_HANDLE,
                   idxMem = VK_NULL_HANDLE, minXMem = VK_NULL_HANDLE, minYMem = VK_NULL_HANDLE,
                   maxXMem = VK_NULL_HANDLE, maxYMem = VK_NULL_HANDLE, indirectMem = VK_NULL_HANDLE,
                   precCountMem = VK_NULL_HANDLE, precIdxMem = VK_NULL_HANDLE;

    const VkDeviceSize instSize  = instanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
    const VkDeviceSize boundSize = kMaxBuckets * sizeof(RecipeBoundSphereCpu);
    const VkDeviceSize countSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize idxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * kMaxMembersPerBucket * sizeof(uint32_t);
    const VkDeviceSize extremaSize = kMaxBuckets * sizeof(uint32_t);
    const VkDeviceSize indirectSize = static_cast<VkDeviceSize>(kMaxBuckets) * 3 * sizeof(uint32_t);
    const VkDeviceSize precCountSize = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * sizeof(uint32_t);
    const VkDeviceSize precIdxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * 2 * kMaxMembersPerBucket * sizeof(uint32_t);

    CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
    CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
    CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
    CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
    CreateHostBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indirectBuf, indirectMem, true);
    CreateHostBuffer(precCountSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precCountBuf, precCountMem, true);
    CreateHostBuffer(precIdxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, precIdxBuf,   precIdxMem,   true);

    UploadBuffer(instMem,  instances.data(),    instSize);
    UploadBuffer(boundMem, boundSpheres.data(), boundSize);

    const std::vector<uint32_t> spirv = ReadSpirv(RECIPE_BUCKETING_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << RECIPE_BUCKETING_SPV;
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

    auto bind = [](uint32_t b) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b; lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb.descriptorCount = 1;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return lb;
    };
    const std::array<VkDescriptorSetLayoutBinding, 11> bindings = {
        bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
        bind(9), bind(10),
    };
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shaderModule; cpci.stage.pName = "main";
    cpci.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

    VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo boundInfo{boundBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo countInfo{countBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo idxInfo{idxBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minXInfo{minXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo minYInfo{minYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxXInfo{maxXBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo maxYInfo{maxYBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo indirectInfo{indirectBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precCountInfo{precCountBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo precIdxInfo{precIdxBuf, 0, VK_WHOLE_SIZE};

    auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 11> writes = {
        wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
        wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
        wBuf(8, &indirectInfo), wBuf(9, &precCountInfo), wBuf(10, &precIdxInfo),
    };
    vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

    PushConstants pcInit{};
    pcInit.viewProj = viewProj;
    pcInit.instanceCount = instanceCount;
    pcInit.maxBuckets = kMaxBuckets;
    pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
    pcInit.screenWidth = kScreenWidth;
    pcInit.screenHeight = kScreenHeight;
    pcInit.mode = 1;  // init pass
    pcInit.raySizeCoef = raySizeCoef;
    pcInit.raySizeBias = raySizeBias;
    pcInit.cameraPos = cameraPos;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
    vkCmdDispatch(cmd, (kMaxBuckets + 63) / 64, 1, 1);

    VkMemoryBarrier initBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    initBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &initBarrier, 0, nullptr, 0, nullptr);

    PushConstants pcBucket = pcInit;
    pcBucket.mode = 0;  // bucket + coverage pass
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
    vkCmdDispatch(cmd, (instanceCount + 63) / 64, 1, 1);

    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    auto readback = [&](VkDeviceMemory mem, VkDeviceSize size, auto& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(typename std::decay_t<decltype(out)>::value_type));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    };

    // Plain per-recipe bucket -- must be UNAFFECTED (additive mechanism): all 3 instances land
    // in their recipe's plain bucket regardless of precision tier.
    std::vector<uint32_t> bucketCounts, bucketIndices;
    readback(countMem, countSize, bucketCounts);
    readback(idxMem, idxSize, bucketIndices);
    ASSERT_EQ(bucketCounts[kRecipePrecision], 2u)
        << "recipePrecision: both instances (tier 0 and tier 1) must still land in the plain "
           "per-recipe bucket -- precision tiering is additive, not a replacement";
    ASSERT_EQ(bucketCounts[kRecipeControl], 1u);

    // Precision sub-buckets -- compound key recipeId*2+tier.
    std::vector<uint32_t> precCounts, precIndices;
    readback(precCountMem, precCountSize, precCounts);
    readback(precIdxMem, precIdxSize, precIndices);

    const uint32_t precisionTier0 = kRecipePrecision * 2u + 0u;
    const uint32_t precisionTier1 = kRecipePrecision * 2u + 1u;
    const uint32_t controlTier0   = kRecipeControl * 2u + 0u;
    const uint32_t controlTier1   = kRecipeControl * 2u + 1u;

    ASSERT_EQ(precCounts[precisionTier0], 1u)
        << "recipePrecision tier 0 (full precision): expected exactly the NEAR instance";
    EXPECT_EQ(precIndices[precisionTier0 * kMaxMembersPerBucket + 0], kNearIdx);

    ASSERT_EQ(precCounts[precisionTier1], 1u)
        << "recipePrecision tier 1 (half precision): expected exactly the FAR instance";
    EXPECT_EQ(precIndices[precisionTier1 * kMaxMembersPerBucket + 0], kFarPrecisionIdx);

    // Non-participating recipe: ALWAYS tier 0, regardless of distance -- proves the precision
    // gate does not leak onto non-opted-in recipes (same discipline as the M1 gating control).
    ASSERT_EQ(precCounts[controlTier0], 1u)
        << "recipeControl: non-participating recipe must resolve tier 0 (full precision) even "
           "though it is at the SAME far distance as recipePrecision's tier-1 instance";
    EXPECT_EQ(precIndices[controlTier0 * kMaxMembersPerBucket + 0], kFarControlIdx);
    EXPECT_EQ(precCounts[controlTier1], 0u)
        << "recipeControl: tier 1 sub-bucket must stay empty -- non-opted-in recipes never "
           "resolve to half precision";

    std::printf("[BUCKETING-PRECISION] recipePrecision tier0(near)=%u tier1(far)=%u "
                "recipeControl tier0=%u tier1=%u (expected 1/1/1/0)\n",
                precCounts[precisionTier0], precCounts[precisionTier1],
                precCounts[controlTier0], precCounts[controlTier1]);

    vkDeviceWaitIdle(logicalDevice_);
    vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
    vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
    vkDestroyBuffer(logicalDevice_, instBuf, nullptr);  vkFreeMemory(logicalDevice_, instMem, nullptr);
    vkDestroyBuffer(logicalDevice_, boundBuf, nullptr); vkFreeMemory(logicalDevice_, boundMem, nullptr);
    vkDestroyBuffer(logicalDevice_, countBuf, nullptr); vkFreeMemory(logicalDevice_, countMem, nullptr);
    vkDestroyBuffer(logicalDevice_, idxBuf, nullptr);   vkFreeMemory(logicalDevice_, idxMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minXBuf, nullptr);  vkFreeMemory(logicalDevice_, minXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, minYBuf, nullptr);  vkFreeMemory(logicalDevice_, minYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxXBuf, nullptr);  vkFreeMemory(logicalDevice_, maxXMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maxYBuf, nullptr);  vkFreeMemory(logicalDevice_, maxYMem, nullptr);
    vkDestroyBuffer(logicalDevice_, indirectBuf, nullptr); vkFreeMemory(logicalDevice_, indirectMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precCountBuf, nullptr); vkFreeMemory(logicalDevice_, precCountMem, nullptr);
    vkDestroyBuffer(logicalDevice_, precIdxBuf, nullptr);   vkFreeMemory(logicalDevice_, precIdxMem, nullptr);
}
