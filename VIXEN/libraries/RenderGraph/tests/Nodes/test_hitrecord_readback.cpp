/**
 * @file test_hitrecord_readback.cpp
 * @brief Proves the HitRecord SSBO round-trip (Sampled Lighting Inc1 M3): dispatches the
 *        REAL BodyInstanceRayMarch.comp shader, reads back the HitRecordBuffer (binding 17)
 *        it wrote per-pixel, and asserts the record's fields match the shader's own colour/
 *        ID output — the SAME cross-check the shader's own main() performs internally
 *        (write hitRecords[idx], then read it back before shading), just verified from the
 *        CPU side against a real host-visible buffer instead of trusting the GPU-side round
 *        trip alone.
 *
 * Reuses the real-shader dispatch pattern from test_body_instance_raymarch_render.cpp /
 * test_body_instance_occlusion_reject.cpp, extended with a host-visible readback buffer at
 * binding 17.
 *
 * Run: ./test_hitrecord_readback
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

namespace {

// Byte-identical to BodyInstanceRayMarch.comp's PushConstants block (see
// test_body_instance_raymarch_render.cpp's own copy for the layout derivation).
// Baked-perf-pipeline M2: SceneBindings.glsl's real PushConstants struct is 96 bytes
// (debugTargetPixel + accumFrameCount added by 47eccd64, well before this M2's own
// work; std430 rounds the whole push-constant block up to a 16-byte multiple, so
// SPIR-V reflection reports 96, not 92 -- see test_body_instance_raymarch_render.cpp's
// PushConstants for the established fix pattern this mirrors).
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;       // DEGREES
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float   raySizeCoef;
    float   raySizeBias;
    int32_t instanceCount;
    int32_t _pad0;  // std430 forces ivec2 to 8-byte alignment (real gap at offset [76,80))
    glm::ivec2 debugTargetPixel = glm::ivec2(-1, -1);  // Inc1 M4b (bytes 80-87); (-1,-1) disables
    uint32_t   accumFrameCount = 1u;                    // Sampled Lighting Inc2 M2 (bytes 88-91)
    uint32_t   _pad1 = 0u;  // std430 push-constant block rounds up to a 16-byte multiple
};
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes (std430 push block, 16-byte rounded)");

// Host-side mirror of shaders/HitRecord.glsl's std430 layout — see
// test_hitrecord_sdi_parity.cpp for the SPIR-V-reflection proof this matches the shader.
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    // See test_hitrecord_sdi_parity.cpp's identical mirror struct for why _pad0 is 4 elements
    // (16 B), not GLSL's named 3 (12 B): std430's array-stride rule adds a further 4 B of
    // hidden tail padding after the last named field to round the whole struct up to its
    // largest member's alignment (16 B) — a plain C++ struct needs that made explicit.
    uint32_t _pad0[4];
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

constexpr uint32_t kHitRecordFlagHit = 0x1u;

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

class HitRecordReadbackTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             softwareConfirmed_ = false;

    std::unique_ptr<VulkanDevice> deviceShell_;

    // Real discrete/integrated GPUs are now PREFERRED; software/Dozen is only a
    // fallback when no real GPU is visible.
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
        appInfo.pApplicationName = "test_hitrecord_readback";
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

        ASSERT_NO_FATAL_FAILURE(PickSoftwarePhysicalDevice());
        ASSERT_TRUE(softwareConfirmed_)
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

    void PickSoftwarePhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);
        // Prefer a real discrete/integrated GPU; fall back to software/Dozen only
        // when no real GPU is visible.
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                softwareConfirmed_ = true; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                softwareConfirmed_ = true; return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
        softwareConfirmed_  = false;
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
        ASSERT_TRUE(found) << "No compute queue family on the software device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_; qInfo.queueCount = 1; qInfo.pQueuePriorities = &priority;

        VkPhysicalDeviceFeatures features{};
        features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
        dInfo.pEnabledFeatures = &features;
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

    void CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImage& outImage, VkDeviceMemory& outMem) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {w, h, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &outImage), VK_SUCCESS);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(logicalDevice_, outImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, outImage, outMem, 0), VK_SUCCESS);
    }

    VkImageView CreateView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateImageView(logicalDevice_, &vi, nullptr, &view), VK_SUCCESS);
        return view;
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

    // Dispatches the real shader at w*h and reads back BOTH the colour/ID images and the
    // per-pixel HitRecordBuffer (binding 17, Inc1 M3).
    void RenderAndReadHitRecords(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
                                 VkBuffer configBuf, VkBuffer instanceBuf,
                                 const PushConstants& pc, uint32_t w, uint32_t h,
                                 std::vector<uint8_t>& outRgba,
                                 std::vector<uint32_t>& outIds,
                                 std::vector<HitRecordCpu>& outHitRecords) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";

        // Baked-perf-pipeline M2: RayTraceBuffer (binding 4) is real, non-placeholder -- see
        // test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation of
        // why a 256-byte placeholder is UB once this SPV compiles with VIXEN_GPU_TRACE_HOOKS.
        constexpr VkDeviceSize kRayTraceBufferSize = 16 /*header*/ + 256 /*slots*/ * (16 + 64 * 48) /*TRACE_RAY_SIZE*/;
        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, counterBuf, counterMem, true);

        VkBuffer dummySdf = VK_NULL_HANDLE, dummyLookup = VK_NULL_HANDLE, dummyMip = VK_NULL_HANDLE,
                 dummyIter = VK_NULL_HANDLE;
        VkDeviceMemory dummySdfMem = VK_NULL_HANDLE, dummyLookupMem = VK_NULL_HANDLE,
                       dummyMipMem = VK_NULL_HANDLE, dummyIterMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySdf, dummySdfMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyLookup, dummyLookupMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyMip, dummyMipMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyIter, dummyIterMem, true);

        // Baked-perf-pipeline M2: binding 15 (TierRefTableBuffer) placeholder -- see this
        // file's bindings-array comment above for the fuller citation.
        VkBuffer dummyTierRef = VK_NULL_HANDLE;
        VkDeviceMemory dummyTierRefMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyTierRef, dummyTierRefMem, true);

        // Sampled Lighting Inc1 M3: the HitRecordBuffer under test (binding 17), sized w*h,
        // zero-initialised so an untouched slot is trivially distinguishable from a real write.
        const VkDeviceSize hitRecordBufSize = static_cast<VkDeviceSize>(w) * h * sizeof(HitRecordCpu);
        VkBuffer hitRecordBuf = VK_NULL_HANDLE; VkDeviceMemory hitRecordMem = VK_NULL_HANDLE;
        CreateHostBuffer(hitRecordBufSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         hitRecordBuf, hitRecordMem, /*zero=*/true);

        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, idImg, idMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        VkImageView idView    = CreateView(idImg, kIdFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE); ASSERT_NE(idView, VK_NULL_HANDLE);

        const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        auto bind = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = t; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };
        // Bindings 0-14 mirror test_body_instance_raymarch_render.cpp's layout.
        // Baked-perf-pipeline M2: binding 15 (TierRefTableBuffer) is a real SSBO the shader has
        // declared since before this M2's own work -- this test's layout never picked it up,
        // exposed by a from-scratch rebuild (see test_body_instance_occlusion_reject.cpp's
        // identical fix for the fuller citation). NOTE: this test's own comment previously
        // labeled binding 17 as "HitRecordBuffer" -- that's WRONG, binding 17 is
        // LightingConfigSSBO; the real HitRecordBuffer is at 18 (verified against
        // BodyInstanceRayMarch.comp's own layout declarations directly).
        const std::array<VkDescriptorSetLayoutBinding, 15> bindings = {
            bind(0,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(8,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(9,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // TierRefTableBuffer (placeholder)
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc1 M3: HitRecordBuffer (real, under test)
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

        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 13},
        }};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); dpci.pPoolSizes = poolSizes.data();
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

        VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idInfo{VK_NULL_HANDLE, idView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesInfo{nodesBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bricksInfo{bricksBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo matsInfo{materialsBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo traceInfo{traceBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo configInfo{configBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo counterInfo{counterBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instInfo{instanceBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo sdfInfo{dummySdf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lookupInfo{dummyLookup, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo mipInfo{dummyMip, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo iterInfo{dummyIter, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo hitRecordInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tierRefInfo{dummyTierRef, 0, VK_WHOLE_SIZE};

        auto wImg = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = descSet; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w2.pImageInfo = info;
            return w2;
        };
        auto wBuf = [&](uint32_t b, VkDescriptorType t, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = descSet; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = t; w2.pBufferInfo = info;
            return w2;
        };
        const std::array<VkWriteDescriptorSet, 15> writes = {
            wImg(0, &colorInfo),
            wBuf(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &counterInfo),
            wImg(9, &idInfo),
            wBuf(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &iterInfo),
            wBuf(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),  // TierRefTableBuffer (placeholder)
            wBuf(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),  // HitRecordBuffer (real, under test; was wrongly 17)
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

        auto barrierToGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrierToGeneral(colorImg);
        barrierToGeneral(idImg);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = colorImg; toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

        VkImageMemoryBarrier idToSrc = toSrc;
        idToSrc.image = idImg;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &idToSrc);

        const VkDeviceSize rgbaSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer rgbaBuf = VK_NULL_HANDLE; VkDeviceMemory rgbaMem = VK_NULL_HANDLE;
        CreateHostBuffer(rgbaSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rgbaBuf, rgbaMem, false);
        VkBufferImageCopy colorCopy{};
        colorCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        colorCopy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rgbaBuf, 1, &colorCopy);

        const VkDeviceSize idSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer idBuf = VK_NULL_HANDLE; VkDeviceMemory idBufMem = VK_NULL_HANDLE;
        CreateHostBuffer(idSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, idBuf, idBufMem, false);
        VkBufferImageCopy idCopy{};
        idCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        idCopy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, idImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, idBuf, 1, &idCopy);

        // Barrier the HitRecord SSBO (shader write -> host read); it's already
        // HOST_VISIBLE|HOST_COHERENT so no copy-to-buffer stage is needed, just a pipeline
        // barrier ordering the shader write ahead of the host map/read below.
        VkBufferMemoryBarrier hitRecordBarrier{};
        hitRecordBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hitRecordBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hitRecordBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hitRecordBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.buffer = hitRecordBuf; hitRecordBarrier.offset = 0; hitRecordBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &hitRecordBarrier, 0, nullptr);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

        ASSERT_TRUE(softwareConfirmed_) << "ABORT: software device not confirmed; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        void* mappedRgba = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rgbaMem, 0, rgbaSize, 0, &mappedRgba), VK_SUCCESS);
        outRgba.assign(static_cast<size_t>(w) * h * 4, 0);
        std::memcpy(outRgba.data(), mappedRgba, static_cast<size_t>(rgbaSize));
        vkUnmapMemory(logicalDevice_, rgbaMem);

        void* mappedIds = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, idBufMem, 0, idSize, 0, &mappedIds), VK_SUCCESS);
        outIds.assign(static_cast<size_t>(w) * h, 0);
        std::memcpy(outIds.data(), mappedIds, static_cast<size_t>(idSize));
        vkUnmapMemory(logicalDevice_, idBufMem);

        void* mappedHitRecords = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, hitRecordMem, 0, hitRecordBufSize, 0, &mappedHitRecords), VK_SUCCESS);
        outHitRecords.assign(static_cast<size_t>(w) * h, HitRecordCpu{});
        std::memcpy(outHitRecords.data(), mappedHitRecords, static_cast<size_t>(hitRecordBufSize));
        vkUnmapMemory(logicalDevice_, hitRecordMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rgbaBuf, nullptr); vkFreeMemory(logicalDevice_, rgbaMem, nullptr);
        vkDestroyBuffer(logicalDevice_, idBuf, nullptr);   vkFreeMemory(logicalDevice_, idBufMem, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImageView(logicalDevice_, idView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr); vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyImage(logicalDevice_, idImg, nullptr);    vkFreeMemory(logicalDevice_, idMem, nullptr);
        vkDestroyBuffer(logicalDevice_, traceBuf, nullptr);   vkFreeMemory(logicalDevice_, traceMem, nullptr);
        vkDestroyBuffer(logicalDevice_, counterBuf, nullptr); vkFreeMemory(logicalDevice_, counterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummySdf, nullptr);    vkFreeMemory(logicalDevice_, dummySdfMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyLookup, nullptr); vkFreeMemory(logicalDevice_, dummyLookupMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyMip, nullptr);    vkFreeMemory(logicalDevice_, dummyMipMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyIter, nullptr);   vkFreeMemory(logicalDevice_, dummyIterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, hitRecordBuf, nullptr); vkFreeMemory(logicalDevice_, hitRecordMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyTierRef, nullptr); vkFreeMemory(logicalDevice_, dummyTierRefMem, nullptr);
    }
};

namespace {

constexpr float kBaseRadiusAu  = 0.05f;
constexpr float kWorldGridSize = 10.0f;

Vixen::SVO::BodyInstanceGpu MakeInstance(float x, float y, float z, float scale,
                                         uint32_t octreeIndex, float r, float g, float b) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = r; i.color[1] = g; i.color[2] = b;
    return i;
}

glm::vec3 ShaderBodyCentre(const Vixen::SVO::BodyInstanceGpu& inst) {
    const glm::vec3 wp(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
    return wp + glm::vec3(0.5f * kWorldGridSize * inst.renderScale);
}
float ShaderBodyRadius(const Vixen::SVO::BodyInstanceGpu& inst) {
    return 0.5f * kWorldGridSize * inst.renderScale;
}

PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target,
                         uint32_t w, uint32_t h, int32_t instanceCount) {
    const glm::vec3 dir = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up    = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos = eye;   pc.time = 0.0f;
    pc.cameraDir = dir;   pc.fov = 45.0f;
    pc.cameraUp = up;     pc.aspect = static_cast<float>(w) / static_cast<float>(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = instanceCount;
    return pc;
}

}  // namespace

// ---------------------------------------------------------------------------
// The decisive test: a real render of one body, comparing the HitRecordBuffer's
// per-pixel content against the shader's OWN ID output — which was itself
// derived by reading the SAME buffer back inside the shader (see
// BodyInstanceRayMarch.comp main()'s HitRecord round-trip comment). If the SSBO
// pack/write/read/unpack were lossy, the ID image (driven by the shader's
// in-shader readback) and this test's own CPU-side readback of the identical
// buffer would disagree.
//
// M2c fix: this test used to ALSO cross-check HitRecord.flags against the
// colour image (`imgLooksHit`) — that check went permanently red when commit
// 784adff7 (Sampled Lighting Inc3 M1, KI-018) split shading out of
// BodyInstanceRayMarch.comp into DirectLighting.comp/SpatialReuseShade.comp;
// this shader stopped writing colorImg (binding 0) entirely, so `imgLooksHit`
// was always false regardless of the real hit data (confirmed: this run showed
// hit=1350/miss=2746 real hits in HitRecord, all failing the colour cross-check).
// The colour-image cross-check is removed — it now tests a contract this shader
// no longer implements. The ID-buffer cross-check below is untouched and still
// valid: idOutputImage IS still written by this shader (main()'s pickID write).
// ---------------------------------------------------------------------------
TEST_F(HitRecordReadbackTest, HitRecordMatchesShaderColorAndIdOutput) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_hitrecord_readback");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInstance(0.0f, 0.0f, 0.0f, kBaseRadiusAu * 2.0f, 0, 1.00f, 0.95f, 0.60f),
    };
    node->SetInstances(instances);
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    VkBuffer nodesBuf     = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer bricksBuf    = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer materialsBuf = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer configBuf    = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer instanceBuf  = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(nodesBuf, VK_NULL_HANDLE);    ASSERT_NE(bricksBuf, VK_NULL_HANDLE);
    ASSERT_NE(materialsBuf, VK_NULL_HANDLE); ASSERT_NE(configBuf, VK_NULL_HANDLE);
    ASSERT_NE(instanceBuf, VK_NULL_HANDLE);

    constexpr uint32_t kW = 64, kH = 64;   // small deliberately: readback + per-pixel check is O(w*h)
    const glm::vec3 focus = ShaderBodyCentre(instances[0]);
    const float     R     = ShaderBodyRadius(instances[0]);
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, static_cast<int32_t>(instances.size()));

    std::vector<uint8_t> rgba;
    std::vector<uint32_t> ids;
    std::vector<HitRecordCpu> hitRecords;
    ASSERT_NO_FATAL_FAILURE(RenderAndReadHitRecords(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
        pc, kW, kH, rgba, ids, hitRecords));

    ASSERT_EQ(hitRecords.size(), static_cast<size_t>(kW) * kH);

    int checkedHit = 0, checkedMiss = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const uint32_t idx = y * kW + x;
            const HitRecordCpu& rec = hitRecords[idx];
            const bool recHit = (rec.flags & kHitRecordFlagHit) != 0u;

            if (recHit) {
                ++checkedHit;
                // roughness/normal must be finite and normal roughly unit-length — a lossy
                // pack/unpack (e.g. a wrong offset silently reading padding) would typically
                // produce NaN/garbage here, not a plausible-looking value.
                const float nLenSq = rec.worldNormal[0] * rec.worldNormal[0] +
                                     rec.worldNormal[1] * rec.worldNormal[1] +
                                     rec.worldNormal[2] * rec.worldNormal[2];
                EXPECT_NEAR(nLenSq, 1.0f, 0.05f)
                    << "pixel (" << x << "," << y << "): HitRecord.worldNormal not unit-length "
                       "(lenSq=" << nLenSq << ") — suspect a pack/unpack offset bug";
                EXPECT_GE(rec.roughness, 0.0f);
                EXPECT_LE(rec.roughness, 1.0f);
                EXPECT_GT(rec.hitT, 0.0f) << "pixel (" << x << "," << y << "): hit but hitT<=0";

                // ID buffer cross-check: a hit pixel's idOutputImage entry must NOT be the
                // 0xFFFFFFFF miss sentinel (both are driven off the SAME anyHitRT this
                // milestone introduced — see main()'s pickID computation).
                EXPECT_NE(ids[idx], 0xFFFFFFFFu)
                    << "pixel (" << x << "," << y << "): HitRecord says hit but idOutputImage "
                       "has the miss sentinel";
            } else {
                ++checkedMiss;
                EXPECT_EQ(ids[idx], 0xFFFFFFFFu)
                    << "pixel (" << x << "," << y << "): HitRecord says miss but idOutputImage "
                       "is not the miss sentinel";
            }
        }
    }
    std::printf("[HITRECORD] %ux%u | hit=%d miss=%d\n", kW, kH, checkedHit, checkedMiss);
    EXPECT_GT(checkedHit, 0) << "Shader produced an all-miss image — the body was not hit "
                                "(HitRecord round-trip can't be meaningfully checked).";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}
