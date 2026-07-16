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

// Byte-identical to RecipeInstanceBucketing.comp's Push block.
struct PushConstants {
    glm::mat4 viewProj;
    uint32_t  instanceCount;
    uint32_t  maxBuckets;
    uint32_t  maxMembersPerBucket;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  mode;
};

// Byte-identical to the shader's RecipeBoundSphere struct.
struct RecipeBoundSphereCpu {
    float center[3];
    float radius;
    float relaxation;
    float _pad[3];
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
    boundSpheres[kRecipeA] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, {0, 0, 0}};
    boundSpheres[kRecipeB] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 1.5f, 1.0f, {0, 0, 0}};
    boundSpheres[kRecipeC] = RecipeBoundSphereCpu{{0.0f, 0.0f, 0.0f}, 0.75f, 1.0f, {0, 0, 0}};

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
             maxXBuf = VK_NULL_HANDLE, maxYBuf = VK_NULL_HANDLE, indirectBuf = VK_NULL_HANDLE;
    VkDeviceMemory instMem = VK_NULL_HANDLE, boundMem = VK_NULL_HANDLE, countMem = VK_NULL_HANDLE,
                   idxMem = VK_NULL_HANDLE, minXMem = VK_NULL_HANDLE, minYMem = VK_NULL_HANDLE,
                   maxXMem = VK_NULL_HANDLE, maxYMem = VK_NULL_HANDLE, indirectMem = VK_NULL_HANDLE;

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

    CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
    CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
    CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
    CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
    CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
    CreateHostBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, indirectBuf, indirectMem, true);

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
    const std::array<VkDescriptorSetLayoutBinding, 9> bindings = {
        bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
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

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9};
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

    auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSet; w.dstBinding = b; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 9> writes = {
        wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
        wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
        wBuf(8, &indirectInfo),
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
}
