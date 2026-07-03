/**
 * @file test_body_instance_raymarch_render.cpp
 * @brief Render the REAL GPU ray-march shader (BodyInstanceRayMarch.comp) to a PNG
 *        on lavapipe (software Vulkan, CPU). The decisive crack test.
 *
 * The CPU reference renderer (cpu_body_render_main.cpp) draws the SP2 body scene by
 * the SVO library's CPU castRay path and shows dark "+" brick-boundary cracks at the
 * close-up. This test runs the ACTUAL shipped compute shader against the SAME octree
 * GPU buffers + the SAME synthetic scene + the SAME NEAR camera, so the two PNGs are
 * directly comparable: does the shipped renderer crack the same way, or render clean?
 *
 * ===========================================================================
 *  SAFETY — LAVAPIPE ONLY (identical contract to test_body_octree_lifetime.cpp)
 * ===========================================================================
 * lavapipe (llvmpipe) is a pure-CPU LLVM rasterizer that NEVER touches the
 * WSL2/Mesa-Dozen (Vulkan-over-D3D12) path. The harness forces lavapipe two ways:
 *   1. The runner sets VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json.
 *   2. PickSoftwarePhysicalDevice() selects ONLY a device whose deviceName contains
 *      "llvmpipe"/"lavapipe" AND deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU, and the
 *      fixture HARD-ASSERTS softwareConfirmed_ before ANY vkQueueSubmit. If the chosen
 *      device is not the software rasterizer the test FAILS and never submits.
 *
 * Run:
 *   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
 *   VK_LAYER_PATH=<...>/.vulkan-sdk/1.4.350.1/x86_64/share/vulkan/explicit_layer.d \
 *   ./test_body_instance_raymarch_render
 *
 * Output: /tmp/glsl_shader_near.png  (512x512 RGBA8, the shipped shader's NEAR view).
 *
 * The SPIR-V is compiled at BUILD time by glslc (CMake custom command) and its path is
 * passed in via the GLSL_RAYMARCH_SPV compile definition.
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

#include <vulkan/vulkan.h>

// PNG writer (own TU impl — mirrors cpu_body_render_main.cpp; stb is an INTERFACE
// header-only dep so we instantiate the write impl here).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>   // setenv / unsetenv (VIXEN_STORED_SDF_DEMO bake gate)
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

// ---------------------------------------------------------------------------
// Push-constant block — byte-identical to BodyInstanceRayMarch.comp PushConstants.
// Layout (GLSL std430 push-constant rules: each vec3 is 16-byte aligned but 12-byte
// sized, so the trailing scalar fills the 4-byte pad after each vec3):
//   0  cameraPos(12)  + time(4)        -> 16
//   16 cameraDir(12)  + fov(4)         -> 32
//   32 cameraUp(12)   + aspect(4)      -> 48
//   48 cameraRight(12)+ debugMode(4)   -> 64
//   64 raySizeCoef(4) raySizeBias(4) instanceCount(4) -> 76
// glm::vec3 is 12 bytes (4-byte aligned), so the C++ struct lays out identically (76 B).
// The shader header's "60 B" note undercounts the per-vec3 16-byte alignment padding.
// NOTE: the shader's getRayDir() applies radians(pc.fov*0.5), so pc.fov is DEGREES.
// ---------------------------------------------------------------------------
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;       // DEGREES
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float   raySizeCoef;
    float   raySizeBias;
    int32_t instanceCount;
};
static_assert(sizeof(PushConstants) == 76, "PushConstants must be 76 bytes (matches shader std430 push block)");

// ---------------------------------------------------------------------------
// Read a compiled SPIR-V file into a uint32 vector.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Lavapipe render fixture — reuses the device/instance/pool/node bring-up from
// test_body_octree_lifetime.cpp verbatim, then adds a compute render.
// ---------------------------------------------------------------------------
class BodyInstanceRayMarchRenderTest : public ::testing::Test {
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

    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& props) {
        std::string name(props.deviceName);
        for (char& c : name) c = static_cast<char>(::tolower(c));
        const bool nameSays =
            name.find("llvmpipe") != std::string::npos ||
            name.find("lavapipe") != std::string::npos;
        const bool typeSays = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        return nameSays && typeSays;
    }

    void SetUp() override {
        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_body_instance_raymarch_render";
        // The shader is compiled with --target-env=vulkan1.3 (SPIR-V 1.6), so the
        // instance/device must advertise Vulkan 1.3 or the shader module is rejected
        // ("Invalid SPIR-V binary version 1.6 for target environment SPIR-V 1.5").
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        // ponytail: validation is a debug aid — only enabled when the SDK layer is installed
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
            << "vkCreateInstance failed — is lavapipe on VK_ICD_FILENAMES?";

        ASSERT_NO_FATAL_FAILURE(PickSoftwarePhysicalDevice());
        ASSERT_TRUE(softwareConfirmed_)
            << "Refusing to run: selected device '" << selectedDeviceName_
            << "' is NOT the software rasterizer. Aborting before any vkQueueSubmit.";

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
        if (logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyDevice(logicalDevice_, nullptr);
            logicalDevice_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    void PickSoftwarePhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible. Is lavapipe forced via "
                                "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json?";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                softwareConfirmed_  = true;
                return;
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
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        // The shader writes to image2D/uimage2D WITHOUT a format qualifier
        // (SPIR-V capability StorageImageWriteWithoutFormat), which requires this
        // feature. lavapipe supports it; enable it so the shader module validates
        // and the pipeline executes correctly.
        VkPhysicalDeviceFeatures features{};
        features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos    = &qInfo;
        dInfo.pEnabledFeatures     = &features;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
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

    // Create an image (storage-usable + transfer-src) in the given format.
    void CreateImage(uint32_t w, uint32_t h, VkFormat format,
                     VkImage& outImage, VkDeviceMemory& outMem) {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = format;
        ci.extent        = {w, h, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &outImage), VK_SUCCESS);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(logicalDevice_, outImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, outImage, outMem, 0), VK_SUCCESS);
    }

    VkImageView CreateView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo vi{};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateImageView(logicalDevice_, &vi, nullptr, &view), VK_SUCCESS);
        return view;
    }

    // A small host-visible buffer (for the dummy trace/counter SSBOs the shader declares).
    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VkDeviceMemory& outMem, bool zero) {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &outBuf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, outBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
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

    // -----------------------------------------------------------------------
    // Run the REAL shader against the node's 5 octree/instance buffers with the
    // given push constants, at w*h, and return the rendered RGBA8 bytes (+ render
    // time). Stands up the compute pipeline + descriptor set for every binding the
    // shader uses (0,1,2,3,4,5,8,9,10), dispatches, and copies the colour image back.
    // The software (lavapipe) device MUST already be confirmed — asserted before submit.
    void RenderToRgba(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
                      VkBuffer configBuf, VkBuffer instanceBuf,
                      VkBuffer sdfBuf, VkBuffer brickLookupBuf,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& outRgba /*w*h*4*/, double& outRenderMs) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";

        // Dummy SSBOs for the trace (4) + counter (8) bindings the shader declares.
        // traceCapacity stays 0 (zeroed) so beginRayTrace() is a no-op.
        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, counterBuf, counterMem, true);

        // SDF (11) + brick-lookup (12) bindings the shader statically declares. For binary
        // bodies the shader never reads them at runtime (formatId==BINARY), but they MUST be
        // bound to satisfy the pipeline layout. Callers without SDF data pass VK_NULL_HANDLE;
        // create a 256-byte dummy here so the binding is always valid.
        VkBuffer dummySdf = VK_NULL_HANDLE, dummyLookup = VK_NULL_HANDLE;
        VkDeviceMemory dummySdfMem = VK_NULL_HANDLE, dummyLookupMem = VK_NULL_HANDLE;
        if (sdfBuf == VK_NULL_HANDLE) {
            CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySdf, dummySdfMem, true);
            sdfBuf = dummySdf;
        }
        if (brickLookupBuf == VK_NULL_HANDLE) {
            CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyLookup, dummyLookupMem, true);
            brickLookupBuf = dummyLookup;
        }

        // Offscreen output images: rgba8 colour (0) + r32ui id (9).
        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, idImg, idMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        VkImageView idView    = CreateView(idImg, kIdFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE);
        ASSERT_NE(idView, VK_NULL_HANDLE);

        // SPIR-V -> shader module.
        const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        // Descriptor set layout (bindings 0..10 the shader uses).
        auto bind = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = t; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };
        const std::array<VkDescriptorSetLayoutBinding, 11> bindings = {
            bind(0,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // I3.2: configs UBO→SSBO (runtime-sized)
            bind(8,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(9,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc2: SoA-SDF brick data
            bind(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc2: brick-grid lookup
        };
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings    = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0; pcr.size = sizeof(PushConstants);
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
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
                  VK_SUCCESS);

        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9},   // 1(nodes)+1(bricks)+1(mats)+1(trace)+1(config)+1(counter)+1(inst)+1(sdf)+1(lookup)
        }};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        dpci.pPoolSizes = poolSizes.data();
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
        VkDescriptorBufferInfo sdfInfo{sdfBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lookupInfo{brickLookupBuf, 0, VK_WHOLE_SIZE};

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
        const std::array<VkWriteDescriptorSet, 11> writes = {
            wImg(0, &colorInfo),
            wBuf(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),  // I3.2: SSBO
            wBuf(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &counterInfo),
            wImg(9, &idInfo),
            wBuf(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        // Record + submit.
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
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

        const VkDeviceSize rbSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer rbBuf = VK_NULL_HANDLE; VkDeviceMemory rbMem = VK_NULL_HANDLE;
        CreateHostBuffer(rbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rbBuf, rbMem, false);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &copy);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

        // FINAL SAFETY GATE before the only vkQueueSubmit.
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: software device not confirmed; refusing vkQueueSubmit.";
        const auto t0 = std::chrono::steady_clock::now();
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        const auto t1 = std::chrono::steady_clock::now();
        outRenderMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rbMem, 0, rbSize, 0, &mapped), VK_SUCCESS);
        outRgba.assign(static_cast<size_t>(w) * h * 4, 0);
        std::memcpy(outRgba.data(), mapped, static_cast<size_t>(rbSize));
        vkUnmapMemory(logicalDevice_, rbMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rbBuf, nullptr);    vkFreeMemory(logicalDevice_, rbMem, nullptr);
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
        if (dummySdf != VK_NULL_HANDLE)    { vkDestroyBuffer(logicalDevice_, dummySdf, nullptr);    vkFreeMemory(logicalDevice_, dummySdfMem, nullptr); }
        if (dummyLookup != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_, dummyLookup, nullptr); vkFreeMemory(logicalDevice_, dummyLookupMem, nullptr); }
    }
};

// ---------------------------------------------------------------------------
// Shared scene helpers
// ---------------------------------------------------------------------------
namespace {

constexpr float kBaseRadiusAu  = 0.05f;   // scene_instances.h kBaseRenderRadiusAu
constexpr float kWorldGridSize = 10.0f;   // ShellOctreeGpu::Serialize localToWorld scale

Vixen::SVO::BodyInstanceGpu MakeInstance(float x, float y, float z, float scale,
                                         uint32_t octreeIndex, float r, float g, float b) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = r; i.color[1] = g; i.color[2] = b;
    return i;
}

// The SHIPPED shader does NOT centre the body at worldPos. Its OctreeConfig
// (ShellOctreeGpu::Serialize) hardcodes localToWorld = scale(kWorldGridSize=10) with NO
// centring translate, and de-instances the ray as instOrigin=(rayOrigin-worldPos)/renderScale.
// So the shell sphere (which fills the octree's [0,1]^3 grid: radius 0.5 at centre 0.5) maps
// to an ACTUAL-WORLD ball spanning [worldPos, worldPos + kWorldGridSize*renderScale]^3:
//   centre = worldPos + 0.5*kWorldGridSize*renderScale,  radius = 0.5*kWorldGridSize*renderScale.
glm::vec3 ShaderBodyCentre(const Vixen::SVO::BodyInstanceGpu& inst) {
    const glm::vec3 wp(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
    return wp + glm::vec3(0.5f * kWorldGridSize * inst.renderScale);
}
float ShaderBodyRadius(const Vixen::SVO::BodyInstanceGpu& inst) {
    return 0.5f * kWorldGridSize * inst.renderScale;
}

// Build a look-at camera into the push-constant block (fov in DEGREES; the shader applies
// radians() itself). raySizeCoef=0 => full detail (no LOD), best for judging cracks.
PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target,
                         uint32_t w, uint32_t h, int32_t instanceCount) {
    const glm::vec3 dir   = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right  = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up     = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos   = eye;   pc.time   = 0.0f;
    pc.cameraDir   = dir;   pc.fov    = 45.0f;
    pc.cameraUp    = up;    pc.aspect = static_cast<float>(w) / static_cast<float>(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f;  pc.raySizeBias = 0.0f;
    pc.instanceCount = instanceCount;
    return pc;
}

// Build the node, push `instances`, Compile+Execute, and return its 5 published buffers.
struct NodeBuffers {
    VkBuffer nodes = VK_NULL_HANDLE, bricks = VK_NULL_HANDLE, materials = VK_NULL_HANDLE;
    VkBuffer config = VK_NULL_HANDLE, instance = VK_NULL_HANDLE;
    VkBuffer sdf = VK_NULL_HANDLE, brickLookup = VK_NULL_HANDLE;   // Inc2 bindings 11/12
};

}  // namespace

// ---------------------------------------------------------------------------
// Single-kind (octree 0) crack render — the decisive CPU-vs-GPU comparison image.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RenderRealShaderNearViewToPng) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // One body on octree 0 (star shell) at the origin. The star shell is the same hollow-
    // sphere shell geometry as every kind, so cracks surface the same way as the CPU castRay.
    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInstance(0.0f, 0.0f, 0.0f, kBaseRadiusAu * 2.0f, 0, 1.00f, 0.95f, 0.60f),
    };
    node->SetInstances(instances);
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    NodeBuffers b;
    b.nodes     = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.bricks    = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.materials = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.config    = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.instance  = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(b.nodes, VK_NULL_HANDLE);    ASSERT_NE(b.bricks, VK_NULL_HANDLE);
    ASSERT_NE(b.materials, VK_NULL_HANDLE); ASSERT_NE(b.config, VK_NULL_HANDLE);
    ASSERT_NE(b.instance, VK_NULL_HANDLE);

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(instances[0]);             // (0.5,0.5,0.5)
    const float     R     = ShaderBodyRadius(instances[0]);            // 0.50 AU
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, static_cast<int32_t>(instances.size()));

    std::vector<uint8_t> rgba; double renderMs = 0.0;
    ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, b.instance,
                                         VK_NULL_HANDLE, VK_NULL_HANDLE,  // binary: dummy SDF/lookup
                                         pc, kW, kH, rgba, renderMs));

    std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
    int hitPixels = 0;
    for (uint32_t i = 0; i < kW * kH; ++i) {
        const uint8_t r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], bb = rgba[i * 4 + 2];
        rgb[i * 3 + 0] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = bb;
        if (r > 24 || g > 24 || bb > 40) ++hitPixels;   // brighter than darkest sky
    }
    const char* outPath = "/tmp/glsl_shader_near.png";
    EXPECT_NE(stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW * 3), 0)
        << "stbi_write_png failed for " << outPath;
    std::printf("[NEAR] %ux%u | device='%s' | render=%.0f ms | body pixels=%d / %u | -> %s\n",
                kW, kH, selectedDeviceName_.c_str(), renderMs, hitPixels, kW * kH, outPath);
    EXPECT_GT(hitPixels, 0) << "Shader produced an all-sky image — the body was not hit.";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// MULTI-KIND render — the decisive proof of the OctreeConfig stride fix.
//
// Renders THREE bodies, one per octreeIndex (0=star, 1=planet, 2=moon), side by side.
// BEFORE the fix (sizeof(OctreeConfig)==260): only octreeIndex 0 drew; configs[1]/[2] read
// a 4/8-byte-misaligned transform → octreeIndex 1/2 bodies were blank. AFTER (sizeof==256):
// all three draw as correct solid shells. Per-kind hit counts are classified by each shell's
// material colour (BuildShellOctree(depth, materialId=kind+1) → palette idx1=red, idx2=green,
// idx3=white), with each instance tinted WHITE so the material colour shows pure. A non-zero
// kind-1 AND kind-2 count is the fix proof. Saves /tmp/glsl_shader_multikind.png.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RenderMultiKindBodiesProvesStrideFix) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_multikind");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Three equal-size bodies (renderScale 0.10 → shader radius 0.50 AU each), spread along X
    // by ~3 radii so they sit side by side. WHITE instance tint so each shell's MATERIAL colour
    // (red/green/white for kind 0/1/2) shows pure for unambiguous per-kind classification.
    const float scale = kBaseRadiusAu * 2.0f;                         // 0.10
    const float Rb    = 0.5f * kWorldGridSize * scale;               // 0.50 AU
    const float sep   = Rb * 3.0f;                                    // body-to-body world spacing
    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInstance(-sep, 0.0f, 0.0f, scale, 0, 1.0f, 1.0f, 1.0f),  // kind 0 star  shell (red mat)
        MakeInstance( 0.0f, 0.0f, 0.0f, scale, 1, 1.0f, 1.0f, 1.0f),  // kind 1 planet shell (green mat)
        MakeInstance( sep, 0.0f, 0.0f, scale, 2, 1.0f, 1.0f, 1.0f),  // kind 2 moon  shell (white mat)
    };
    node->SetInstances(instances);
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    NodeBuffers b;
    b.nodes     = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.bricks    = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.materials = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.config    = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.instance  = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(b.config, VK_NULL_HANDLE);  ASSERT_NE(b.instance, VK_NULL_HANDLE);

    // The C++ OctreeConfig stride MUST equal the shader's std140 UBO-array stride (432 B) so
    // the node's std::array<OctreeConfig,3> upload aligns configs[1]/configs[2] with the shader.
    ASSERT_EQ(sizeof(Vixen::SVO::OctreeConfig), 432u)
        << "OctreeConfig must match the shader's 432-byte std140 configs[] ArrayStride";

    // Frame all three side by side. Aim at the MIDPOINT of the three shader-body centres
    // (not worldPos — the shader offsets the body to worldPos+0.5*kWorldGridSize*renderScale),
    // looking straight down -Z so the row sits across the wide frame, backed off just enough
    // to fit the span with margin.
    constexpr uint32_t kW = 768, kH = 256;   // wide framing for a side-by-side row
    const glm::vec3 c0 = ShaderBodyCentre(instances[0]);
    const glm::vec3 c2 = ShaderBodyCentre(instances[2]);
    const glm::vec3 centre = 0.5f * (c0 + c2);                        // midpoint of the row
    const float spanX = std::abs(c2.x - c0.x) + 2.0f * Rb;           // full row width
    // Distance to fit spanX horizontally at fov 45° with this aspect (+ margin), looking -Z.
    const float halfFov = glm::radians(45.0f) * 0.5f;
    const float aspect  = static_cast<float>(kW) / static_cast<float>(kH);
    const float dist    = (0.5f * spanX) / (std::tan(halfFov) * aspect) * 1.35f;
    const glm::vec3 eye = centre + glm::vec3(0.0f, 0.15f, 1.0f) * dist;  // slight tilt for shading
    const PushConstants pc = MakeCamera(eye, centre, kW, kH, static_cast<int32_t>(instances.size()));
    std::printf("[MULTIKIND] centres x=[%.2f, %.2f, %.2f] aim=(%.2f,%.2f,%.2f) eye=(%.2f,%.2f,%.2f) "
                "dist=%.2f spanX=%.2f\n",
                c0.x, ShaderBodyCentre(instances[1]).x, c2.x, centre.x, centre.y, centre.z,
                eye.x, eye.y, eye.z, dist, spanX);

    std::vector<uint8_t> rgba; double renderMs = 0.0;
    ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, b.instance,
                                         VK_NULL_HANDLE, VK_NULL_HANDLE,  // binary: dummy SDF/lookup
                                         pc, kW, kH, rgba, renderMs));

    // Per-kind hit classification by material hue (robust to Lambert dimming):
    //   kind 0 = RED-dominant   (palette idx1 {0.75,0.1,0.1}) → r >> g,b
    //   kind 1 = GREEN-dominant (palette idx2 {0.1,0.75,0.1}) → g >> r,b
    //   kind 2 = GRAY/white     (palette idx3 {0.9,0.9,0.9})  → balanced channels (neither r- nor
    //            g-dominant). Classifying gray as "channels balanced" rather than "all > 90" makes
    //            it survive the lit-side shading that the strict-white test was missing.
    std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
    int redPixels = 0, greenPixels = 0, grayPixels = 0, anyBody = 0;
    for (uint32_t i = 0; i < kW * kH; ++i) {
        const int r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], bl = rgba[i * 4 + 2];
        rgb[i * 3 + 0] = static_cast<uint8_t>(r);
        rgb[i * 3 + 1] = static_cast<uint8_t>(g);
        rgb[i * 3 + 2] = static_cast<uint8_t>(bl);
        const bool body = (r > 24 || g > 24 || bl > 40);   // brighter than darkest sky
        if (!body) continue;
        ++anyBody;
        if      (r > g + 25 && r > bl + 25) ++redPixels;       // kind 0 (star, red mat)
        else if (g > r + 25 && g > bl + 25) ++greenPixels;     // kind 1 (planet, green mat)
        else                                ++grayPixels;      // kind 2 (moon, gray/white mat)
    }

    const char* outPath = "/tmp/glsl_shader_multikind.png";
    EXPECT_NE(stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW * 3), 0)
        << "stbi_write_png failed for " << outPath;

    std::printf("[MULTIKIND] %ux%u | device='%s' | render=%.0f ms | body px total=%d | "
                "kind0(red/star octree0)=%d  kind1(green/planet octree1)=%d  "
                "kind2(gray/moon octree2)=%d | -> %s\n",
                kW, kH, selectedDeviceName_.c_str(), renderMs, anyBody,
                redPixels, greenPixels, grayPixels, outPath);

    // The fix proof: octreeIndex 1 AND 2 must now render real bodies (non-zero), not blank.
    // (Before the 432-byte UBO-array-stride fix, kind1/kind2 were exactly 0.)
    EXPECT_GT(redPixels,   500) << "kind 0 (octreeIndex 0) body did not render";
    EXPECT_GT(greenPixels, 500) << "kind 1 (octreeIndex 1) body did not render — stride fix regressed";
    EXPECT_GT(grayPixels,  500) << "kind 2 (octreeIndex 2) body did not render — stride fix regressed";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// STORED-SDF render (Inc2 M6) — decisive proof of the ESVO-leaf-hit redesign.
//
// Bakes the 3 Stored-SDF body kinds (VIXEN_STORED_SDF_DEMO → the node's SDF path:
// BakeRecipeToSdfWorld → BuildSdfBodyOctree → ConcatenateSdf; formatId=STORED_SDF;
// bindings 11/12 populated) and renders them through the SHIPPED
// BodyInstanceRayMarch.comp — which now drives the Stored-SDF iso-surface from the
// ESVO octree traversal (handleLeafHitInstancedSdf + marchBrickSdf), NOT the retired
// flat marchStoredSdf.
//
// Pre-redesign bug: POV/angle-dependent brick-aligned HOLES in the spheres.
// Oracle: a SMOOTH sphere's silhouette must be SOLID — within each scanline's body
// span there must be (almost) no interior background pixels. fillRatio =
// bodyPixels/spanPixels ≈ 1.0 for a clean render; interior holes drop it well below.
// Saves /tmp/glsl_sdf_smooth_near.png + /tmp/glsl_sdf_displaced_near.png for inspection.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RenderStoredSdfBodiesNoHoles) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_sdf");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Body world transform: SerializeSdf sets localToWorld = scale(kWorldGridSize=10) (NOT the grid
    // size), so the octree's normalized [0,1]^3 maps to a 10*renderScale world cube. The SDF sphere's
    // iso-surface sits at grid radius 26 of 64 (normalized 0.40625). Hence (matching the proven binary
    // helpers): world centre = worldPos + 0.5*10*renderScale = ShaderBodyCentre;
    //           shell-extent  = 0.5*10*renderScale = ShaderBodyRadius (the SDF iso is ~0.81 of this).
    // Place worldPos at origin; frame on the body's actual centre/extent.
    constexpr float kRS = 2.0f;   // → body ~8 world units across, fills the 512^2 frame
    const Vixen::SVO::BodyInstanceGpu frameInst = MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0, 1.0f, 1.0f, 1.0f);

    // Seed with one body so Compile's ring allocation is valid; bake as Stored-SDF.
    node->SetInstances({ frameInst });
    node->Setup();
    ::setenv("VIXEN_STORED_SDF_DEMO", "1", /*overwrite=*/1);   // node bakes SDF in EnsureOctreesBuilt
    ASSERT_NO_THROW(node->Compile());
    ::unsetenv("VIXEN_STORED_SDF_DEMO");                       // clean for any later (binary) test

    NodeBuffers b;
    b.nodes       = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.bricks      = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.materials   = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.config      = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.sdf         = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.brickLookup = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(b.config, VK_NULL_HANDLE);
    ASSERT_NE(b.sdf, VK_NULL_HANDLE);
    ASSERT_NE(b.brickLookup, VK_NULL_HANDLE);

    ASSERT_EQ(sizeof(Vixen::SVO::OctreeConfig), 432u)
        << "OctreeConfig must match the shader's 432-byte std140 configs[] ArrayStride";

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(frameInst);                       // worldPos + 5*renderScale
    const float     R     = ShaderBodyRadius(frameInst);                       // 5*renderScale (shell extent)
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, 1);

    // Render ONE Stored-SDF body (octreeIdx) alone, save a PNG, return its RGBA.
    auto renderBody = [&](uint32_t octreeIdx, const char* pngPath,
                          std::vector<uint8_t>& outRgba) {
        node->SetInstances({ MakeInstance(0.0f, 0.0f, 0.0f, kRS, octreeIdx, 1.0f, 1.0f, 1.0f) });
        frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
        node->Execute();
        VkBuffer inst = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        ASSERT_NE(inst, VK_NULL_HANDLE);
        double ms = 0.0;
        RenderToRgba(b.nodes, b.bricks, b.materials, b.config, inst,
                     b.sdf, b.brickLookup, pc, kW, kH, outRgba, ms);
        std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
        int bodyPx = 0;
        for (uint32_t i = 0; i < kW * kH; ++i) {
            const uint8_t r = outRgba[i*4+0], g = outRgba[i*4+1], bl = outRgba[i*4+2];
            rgb[i*3+0]=r; rgb[i*3+1]=g; rgb[i*3+2]=bl;
            if (r > 24 || g > 24 || bl > 40) ++bodyPx;
        }
        EXPECT_NE(stbi_write_png(pngPath, kW, kH, 3, rgb.data(), kW * 3), 0)
            << "stbi_write_png failed for " << pngPath;
        std::printf("[SDF] octree %u | render=%.0f ms | body px=%d / %u | -> %s\n",
                    octreeIdx, ms, bodyPx, kW * kH, pngPath);
    };

    // --- Smooth sphere (octree 0): STRICT solid-silhouette (no-holes) oracle ---
    std::vector<uint8_t> smooth;
    ASSERT_NO_FATAL_FAILURE(renderBody(0u, "/tmp/glsl_sdf_smooth_near.png", smooth));

    uint64_t totalBody = 0, totalSpan = 0;
    int bodyRows = 0, bodyPixels = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        int first = -1, last = -1, cnt = 0;
        for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &smooth[(static_cast<size_t>(y) * kW + x) * 4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) { if (first < 0) first = int(x); last = int(x); ++cnt; }
        }
        if (first >= 0) { totalBody += cnt; totalSpan += (last - first + 1); ++bodyRows; bodyPixels += cnt; }
    }
    const double fillRatio = (totalSpan > 0) ? double(totalBody) / double(totalSpan) : 0.0;
    std::printf("[SDF] smooth sphere: bodyPx=%d rows=%d fillRatio=%.4f (1.0 = solid, holes drop it)\n",
                bodyPixels, bodyRows, fillRatio);

    EXPECT_GT(bodyPixels, 20000) << "Stored-SDF smooth sphere barely rendered — body not hit.";
    EXPECT_GT(fillRatio, 0.97)
        << "Stored-SDF smooth sphere has interior HOLES (fillRatio " << fillRatio
        << " < 0.97) — the ESVO-leaf-hit redesign did not close the brick-aligned gaps.";

    // --- Displaced sphere (octree 1): lenient coverage + PNG for inspection ---
    std::vector<uint8_t> displaced;
    ASSERT_NO_FATAL_FAILURE(renderBody(1u, "/tmp/glsl_sdf_displaced_near.png", displaced));
    int dispBody = 0;
    for (uint32_t i = 0; i < kW * kH; ++i) {
        const uint8_t r = displaced[i*4+0], g = displaced[i*4+1], bl = displaced[i*4+2];
        if (r > 24 || g > 24 || bl > 40) ++dispBody;
    }
    EXPECT_GT(dispBody, 20000) << "Stored-SDF displaced sphere barely rendered — body not hit.";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// MULTI-CHANNEL render gate (Inc3 M4) — decisive proof that per-voxel color
// and roughness channels reach the GPU shader and produce visible variation.
//
// Renders the smooth Stored-SDF sphere (octree 0) which is baked with:
//   color = 0.5 + 0.5*cos(p*0.12 + {0, 2.094, 4.188})  (smooth RGB bands)
//   roughness = clamp(0.2 + 0.6*fract(p.y*0.0625), 0, 1) (Y-stripe)
//
// Assertions:
//   (a) SDF still SOLID -- fillRatio > 0.97 (no-regression from RenderStoredSdfBodiesNoHoles)
//   (b) Per-voxel COLOR varies -- sample a horizontal row of body pixels; the
//       per-channel range (max-min) must exceed 0.10 on at least one of R/G/B.
//       A flat tint (color not routed to shader) would FAIL this.
//   (c) PNG written to /tmp/glsl_sdf_multichannel.png for visual inspection.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RenderStoredSdfMultiChannel) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_multichannel");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    constexpr float kRS = 2.0f;
    const Vixen::SVO::BodyInstanceGpu frameInst = MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0, 1.0f, 1.0f, 1.0f);

    node->SetInstances({ frameInst });
    node->Setup();
    ::setenv("VIXEN_STORED_SDF_DEMO", "1", /*overwrite=*/1);
    ASSERT_NO_THROW(node->Compile());
    ::unsetenv("VIXEN_STORED_SDF_DEMO");

    NodeBuffers b;
    b.nodes       = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.bricks      = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.materials   = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.config      = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.sdf         = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.brickLookup = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(b.config, VK_NULL_HANDLE);
    ASSERT_NE(b.sdf, VK_NULL_HANDLE);
    ASSERT_NE(b.brickLookup, VK_NULL_HANDLE);

    ASSERT_EQ(sizeof(Vixen::SVO::OctreeConfig), 432u)
        << "OctreeConfig must match the shader's 432-byte std140 configs[] ArrayStride";

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(frameInst);
    const float     R     = ShaderBodyRadius(frameInst);
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, 1);

    // Render the Stored-SDF sphere (octree 0) into RGBA.
    node->SetInstances({ MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0u, 1.0f, 1.0f, 1.0f) });
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->Execute();
    VkBuffer inst = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(inst, VK_NULL_HANDLE);

    std::vector<uint8_t> rgba; double renderMs = 0.0;
    ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, inst,
                                          b.sdf, b.brickLookup, pc, kW, kH, rgba, renderMs));

    // ------------------------------------------------------------------
    // (a) SDF solid: fillRatio > 0.97 (no-regression oracle)
    // ------------------------------------------------------------------
    uint64_t totalBody = 0, totalSpan = 0;
    int bodyRows = 0, bodyPixels = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        int first = -1, last = -1, cnt = 0;
        for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &rgba[(static_cast<size_t>(y) * kW + x) * 4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) {
                if (first < 0) first = int(x); last = int(x); ++cnt;
            }
        }
        if (first >= 0) { totalBody += cnt; totalSpan += (last - first + 1); ++bodyRows; bodyPixels += cnt; }
    }
    const double fillRatio = (totalSpan > 0) ? double(totalBody) / double(totalSpan) : 0.0;
    std::printf("[MULTICHANNEL] smooth sphere: bodyPx=%d rows=%d fillRatio=%.4f\n",
                bodyPixels, bodyRows, fillRatio);

    EXPECT_GT(bodyPixels, 20000) << "Multi-channel Stored-SDF sphere barely rendered.";
    EXPECT_GT(fillRatio, 0.97)
        << "Multi-channel Stored-SDF sphere has HOLES (fillRatio=" << fillRatio << " < 0.97)";

    // ------------------------------------------------------------------
    // (b) Per-voxel color varies: scan a horizontal row near the centre of the
    //     sphere disk, collect body pixels, and assert that the range of at
    //     least one channel (R/G/B) exceeds 0.10 (flat tint would FAIL).
    // ------------------------------------------------------------------
    // Pick the row in the vertical-centre half with the most body pixels.
    uint32_t bestRow = kH / 2;
    int bestCnt = 0;
    for (uint32_t y = kH / 4; y < 3 * kH / 4; ++y) {
        int cnt = 0;
        for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &rgba[(static_cast<size_t>(y) * kW + x) * 4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) ++cnt;
        }
        if (cnt > bestCnt) { bestCnt = cnt; bestRow = y; }
    }

    float rMin = 1.0f, rMax = 0.0f;
    float gMin = 1.0f, gMax = 0.0f;
    float blMin = 1.0f, blMax = 0.0f;
    int rowBodyPx = 0;
    for (uint32_t x = 0; x < kW; ++x) {
        const uint8_t* px = &rgba[(static_cast<size_t>(bestRow) * kW + x) * 4];
        if (!(px[0] > 24 || px[1] > 24 || px[2] > 40)) continue;
        ++rowBodyPx;
        const float rf  = px[0] / 255.0f;
        const float gf  = px[1] / 255.0f;
        const float bff = px[2] / 255.0f;
        if (rf  < rMin)  rMin  = rf;   if (rf  > rMax)  rMax  = rf;
        if (gf  < gMin)  gMin  = gf;   if (gf  > gMax)  gMax  = gf;
        if (bff < blMin) blMin = bff;  if (bff > blMax) blMax = bff;
    }
    const float rRange  = rMax  - rMin;
    const float gRange  = gMax  - gMin;
    const float blRange = blMax - blMin;
    std::printf("[MULTICHANNEL] color range row=%u bodyPx=%d | R=[%.3f,%.3f] range=%.3f | "
                "G=[%.3f,%.3f] range=%.3f | B=[%.3f,%.3f] range=%.3f\n",
                bestRow, rowBodyPx, rMin, rMax, rRange, gMin, gMax, gRange, blMin, blMax, blRange);

    const float maxRange = (rRange > gRange) ? (rRange > blRange ? rRange : blRange)
                                              : (gRange > blRange ? gRange : blRange);
    EXPECT_GT(maxRange, 0.10f)
        << "Per-voxel color shows no variation (max channel range=" << maxRange
        << ") -- color channel is NOT reaching the shader (flat tint / color not wired).";

    // ------------------------------------------------------------------
    // (c) Write PNG for visual inspection
    // ------------------------------------------------------------------
    std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
    for (uint32_t i = 0; i < kW * kH; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    const char* outPath = "/tmp/glsl_sdf_multichannel.png";
    EXPECT_NE(stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW * 3), 0)
        << "stbi_write_png failed for " << outPath;
    std::printf("[MULTICHANNEL] %ux%u | render=%.0f ms | -> %s\n", kW, kH, renderMs, outPath);

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// P2.1 M2 — Recipe-baked body render gate.
//
// Injects a sphere∪sphere peanut recipe into octree 0 via SetBakeRecipe before
// Compile. The resulting body cannot be produced by any hardcoded recipe, which
// proves the materialization path (recipe → BakeRecipeInstructionsToSdfWorld →
// Stored octree → shipped BodyInstanceRayMarch.comp shader) end-to-end.
//
// Oracle: peanut silhouette is SOLID — fillRatio > 0.97, bodyPixels > 20000.
// PNG: /tmp/glsl_sdf_recipe_peanut.png (controller reads it).
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RenderRecipeBakedBody) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_recipe");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    constexpr float kRS = 2.0f;
    const Vixen::SVO::BodyInstanceGpu frameInst = MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0, 1.0f, 1.0f, 1.0f);
    node->SetInstances({ frameInst });

    // Build a sphere∪sphere peanut recipe, OBJECT-CENTERED (Inc2a re-derivation): the node's
    // VIXEN_STORED_SDF_DEMO path bakes bakeRecipe_ via BakeRecipeInstructionsToSdfWorld with
    // center=(32,32,32) (BodyOctreeSceneNode.cpp's kSdfCenter), which now applies `p - center`
    // before evalRecipe -- and Sphere's data[0..2] is its OWN local center offset, added on top
    // of that already-centered point. So a sphere must be authored RELATIVE to local origin, not
    // at the old raw-grid-absolute (26/38, 32, 32) (which pre-fix coincided with grid-absolute
    // since center was ignored; post-fix that same value would double-offset by center, landing
    // near grid (-6,0,0)/(6,0,0) as an absolute coordinate rather than the intended local one).
    // Two overlapping spheres at local x=-6 and x=+6, radius=16 each → forms a peanut shape the
    // hardcoded recipes cannot produce; center=(32,32,32) places the pair back at grid (26,32,32)
    // / (38,32,32) -- the same effective grid position as before the fix.
    auto makeSph = [](glm::vec3 c, float r) {
        Vixen::SVO::Recipe::SdfInstruction in{};
        in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
        in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
        return in;
    };
    Vixen::SVO::Recipe::SdfInstruction uni{};
    uni.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);

    node->SetBakeRecipe({
        makeSph({-6.0f, 0.0f, 0.0f}, 16.0f),  // left lobe (local; grid-absolute 26,32,32)
        makeSph({ 6.0f, 0.0f, 0.0f}, 16.0f),  // right lobe (local; grid-absolute 38,32,32)
        uni                                     // union → peanut
    });

    node->Setup();
    ::setenv("VIXEN_STORED_SDF_DEMO", "1", /*overwrite=*/1);
    ASSERT_NO_THROW(node->Compile());
    ::unsetenv("VIXEN_STORED_SDF_DEMO");

    NodeBuffers b;
    b.nodes       = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.bricks      = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.materials   = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.config      = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.sdf         = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    b.brickLookup = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(b.config,      VK_NULL_HANDLE);
    ASSERT_NE(b.sdf,         VK_NULL_HANDLE);
    ASSERT_NE(b.brickLookup, VK_NULL_HANDLE);

    ASSERT_EQ(sizeof(Vixen::SVO::OctreeConfig), 432u)
        << "OctreeConfig must match the shader's 432-byte std140 configs[] ArrayStride";

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(frameInst);
    const float     R     = ShaderBodyRadius(frameInst);
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, 1);

    // Render octree 0 (recipe-baked peanut).
    node->SetInstances({ MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0u, 1.0f, 1.0f, 1.0f) });
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->Execute();
    VkBuffer inst = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(inst, VK_NULL_HANDLE);

    std::vector<uint8_t> rgba; double ms = 0.0;
    ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, inst,
                                         b.sdf, b.brickLookup, pc, kW, kH, rgba, ms));

    // Write PNG (controller reads this to verify visually).
    const char* outPath = "/tmp/glsl_sdf_recipe_peanut.png";
    {
        std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
        for (uint32_t i = 0; i < kW * kH; ++i) {
            rgb[i*3+0] = rgba[i*4+0]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2];
        }
        EXPECT_NE(stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW * 3), 0)
            << "stbi_write_png failed for " << outPath;
    }

    // Solid-silhouette oracle (same as RenderStoredSdfBodiesNoHoles smooth sphere check).
    uint64_t totalBody = 0, totalSpan = 0;
    int bodyRows = 0, bodyPixels = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        int first = -1, last = -1, cnt = 0;
        for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &rgba[(static_cast<size_t>(y) * kW + x) * 4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) {
                if (first < 0) first = int(x); last = int(x); ++cnt;
            }
        }
        if (first >= 0) { totalBody += cnt; totalSpan += (last - first + 1); ++bodyRows; bodyPixels += cnt; }
    }
    const double fillRatio = (totalSpan > 0) ? double(totalBody) / double(totalSpan) : 0.0;
    std::printf("[RECIPE] peanut: bodyPx=%d rows=%d fillRatio=%.4f | render=%.0f ms | -> %s\n",
                bodyPixels, bodyRows, fillRatio, ms, outPath);

    EXPECT_GT(bodyPixels, 20000) << "recipe-baked body barely rendered";
    EXPECT_GT(fillRatio, 0.97)   << "recipe-baked body has interior holes (fillRatio=" << fillRatio << ")";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// P2.3 M2 — Live edit → re-materialize → re-render gate.
//
// Renders octree 0 as a single sphere (A), edits the recipe to a sphere∪sphere
// peanut (B) via SetBakeRecipe AFTER Compile, drives one Execute (which
// re-materializes), re-reads the (new) handles, and re-renders. B must be a
// WIDER body than A — proving the GPU saw the edit with no graph rebuild.
//
// PNGs: /tmp/glsl_sdf_remat_A.png (sphere), /tmp/glsl_sdf_remat_B.png (peanut).
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceRayMarchRenderTest, RematerializeEditLoop) {
    ASSERT_TRUE(softwareConfirmed_);
    using C = BodyOctreeSceneNodeConfig;

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_render_remat");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    constexpr float kRS = 2.0f;
    const Vixen::SVO::BodyInstanceGpu frameInst = MakeInstance(0.0f, 0.0f, 0.0f, kRS, 0, 1.0f, 1.0f, 1.0f);
    node->SetInstances({ frameInst });

    auto makeSph = [](glm::vec3 c, float r) {
        Vixen::SVO::Recipe::SdfInstruction in{};
        in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
        in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
        return in;
    };
    Vixen::SVO::Recipe::SdfInstruction uni{};
    uni.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);

    // Recipe A: a single centred sphere (one round lobe). Inc2a: object-centered (local
    // origin) -- center=(32,32,32) (kSdfCenter) places it at grid (32,32,32), same effective
    // position as the old grid-absolute (32,32,32) (this one was already at grid-center, so
    // migrating to local (0,0,0) is a no-op in effective grid position).
    node->SetBakeRecipe({ makeSph({0.0f, 0.0f, 0.0f}, 18.0f) });

    node->Setup();
    // Keep STORED_SDF_DEMO set across BOTH executes — the edit Execute's Rematerialize()
    // re-runs EnsureOctreesBuilt(), which gates on this env var.
    ::setenv("VIXEN_STORED_SDF_DEMO", "1", /*overwrite=*/1);
    ASSERT_NO_THROW(node->Compile());      // bakes recipe A; clears recipeDirty_
    ASSERT_NO_THROW(node->Execute());      // uploads instance; no re-materialize (not dirty)

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = ShaderBodyCentre(frameInst);
    const float     R     = ShaderBodyRadius(frameInst);
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const PushConstants pc = MakeCamera(eye, focus, kW, kH, 1);

    // Re-reads handles each call so a re-materialized (recreated) buffer is picked up.
    auto renderAndMeasure = [&](const char* png, int& outWidth, int& outBodyPx) {
        NodeBuffers b;
        b.nodes       = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.bricks      = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.materials   = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.config      = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.sdf         = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        b.brickLookup = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        VkBuffer inst = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        ASSERT_NE(b.sdf, VK_NULL_HANDLE);
        ASSERT_NE(inst,  VK_NULL_HANDLE);

        std::vector<uint8_t> rgba; double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(b.nodes, b.bricks, b.materials, b.config, inst,
                                             b.sdf, b.brickLookup, pc, kW, kH, rgba, ms));
        {
            std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
            for (uint32_t i = 0; i < kW * kH; ++i) {
                rgb[i*3+0] = rgba[i*4+0]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2];
            }
            EXPECT_NE(stbi_write_png(png, kW, kH, 3, rgb.data(), kW*3), 0)
                << "stbi_write_png failed for " << png;
        }
        int minX = int(kW), maxX = -1, bodyPx = 0;
        for (uint32_t y = 0; y < kH; ++y) for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t* px = &rgba[(static_cast<size_t>(y)*kW + x)*4];
            if (px[0] > 24 || px[1] > 24 || px[2] > 40) {
                if (int(x) < minX) minX = int(x);
                if (int(x) > maxX) maxX = int(x);
                ++bodyPx;
            }
        }
        outWidth  = (maxX < minX) ? 0 : (maxX - minX + 1);
        outBodyPx = bodyPx;
    };

    // --- render A (single sphere) ---
    int widthA = 0, pxA = 0;
    renderAndMeasure("/tmp/glsl_sdf_remat_A.png", widthA, pxA);

    // --- EDIT at runtime: sphere∪sphere peanut (two offset lobes, wider than A) ---
    // Inc2a: object-centered (local); center=(32,32,32) places the pair back at the same
    // effective grid position as the old grid-absolute (24,32,32)/(40,32,32).
    node->SetBakeRecipe({
        makeSph({-8.0f, 0.0f, 0.0f}, 16.0f),
        makeSph({ 8.0f, 0.0f, 0.0f}, 16.0f),
        uni
    });
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());       // recipeDirty_ -> Rematerialize + re-emit octree slots
    ::unsetenv("VIXEN_STORED_SDF_DEMO");

    // --- render B (re-reads the NEW handles post-rematerialize) ---
    int widthB = 0, pxB = 0;
    renderAndMeasure("/tmp/glsl_sdf_remat_B.png", widthB, pxB);

    std::printf("[REMAT] A: width=%d px=%d  B: width=%d px=%d  (B = wider peanut)\n",
                widthA, pxA, widthB, pxB);
    EXPECT_GT(widthA, 0) << "shape A (sphere) did not render";
    EXPECT_GT(pxB, 20000) << "shape B barely rendered";
    EXPECT_GT(widthB, static_cast<int>(widthA * 1.15f))
        << "edit did NOT re-materialize: B not wider than A (widthA=" << widthA
        << " widthB=" << widthB << ")";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

}  // namespace
