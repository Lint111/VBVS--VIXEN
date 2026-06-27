/**
 * @file test_procedural_recipe_render.cpp
 * @brief P2.2 M2 — Live procedural compute render via the compile realization.
 *
 * Emits an all-HLSL compute shader from a sphere∪sphere SdfInstruction program,
 * compiles it via ShaderCompiler (HLSL→SPIR-V), dispatches it on lavapipe with
 * a minimal 1-binding descriptor layout (storage image) + push constants (camera).
 * Asserts bodyPixels > 20000 and writes /tmp/glsl_recipe_procedural.png.
 *
 * Binding contract (must match kTraceMain in SdfRecipeCodegen.h):
 *   binding 0 = RWTexture2D (storage image, R8G8B8A8_UNORM)
 *   push constant range 0..76 = cbuffer PC (76 bytes)
 *
 * SAFETY — LAVAPIPE ONLY (identical contract to test_body_instance_raymarch_render.cpp)
 * lavapipe (llvmpipe) is a pure-CPU rasterizer that never touches WSL2/Mesa-Dozen.
 * The fixture hard-asserts softwareConfirmed_ before any vkQueueSubmit.
 *
 * Critical lavapipe gotcha: storage-image writes silently no-op unless the device is
 * created with Vulkan 1.3 instance API + shaderStorageImageWriteWithoutFormat enabled.
 *
 * Run:
 *   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
 *   VK_LAYER_PATH=<sdk>/x86_64/share/vulkan/explicit_layer.d \
 *   ./test_procedural_recipe_render
 *
 * Output: /tmp/glsl_recipe_procedural.png (512x512 RGBA8, procedural peanut sphere-trace).
 */

#include <gtest/gtest.h>

#include "Recipe/SdfRecipeCodegen.h"
#include "Recipe/SdfInstruction.h"
#include "ShaderCompiler.h"
#include "TestVkValidation.h"

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDF_CORE_KERNELS_HLSL_PATH
#  error "SDF_CORE_KERNELS_HLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

// ---------------------------------------------------------------------------
// Push-constant block — byte-identical to the cbuffer PC in kTraceMain:
//   cbuffer PC : register(b0) {
//       float3 camPos;   float _p0;    // offset  0, size 16
//       float3 camDir;   float fov;    // offset 16, size 16
//       float3 camUp;    float aspect; // offset 32, size 16
//       float3 camRight; int _p1;      // offset 48, size 16
//   };                                 // total = 64 bytes
// Note: std140/HLSL cbuffer pads float3 to 16 bytes each → 4×16 = 64 bytes.
// The plan describes "76-byte camera block" inherited from the octree PushConstants,
// but the emitter's kTraceMain has exactly 4 float3+scalar fields → 64 bytes.
// We declare BOTH possibilities and pick the one the shader actually expects.
// The emitter's cbuffer PC has exactly: camPos(12)+_p0(4)+camDir(12)+fov(4)+
//   camUp(12)+aspect(4)+camRight(12)+_p1(4) = 64 bytes.
// ---------------------------------------------------------------------------
struct RecipePushConstants {
    glm::vec3 camPos;   float _p0;      // 16 bytes
    glm::vec3 camDir;   float fov;      // 16 bytes — fov in DEGREES (shader applies radians())
    glm::vec3 camUp;    float aspect;   // 16 bytes
    glm::vec3 camRight; int32_t _p1;   // 16 bytes
};
static_assert(sizeof(RecipePushConstants) == 64,
    "RecipePushConstants must be 64 bytes (matches cbuffer PC in kTraceMain)");

RecipePushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target,
                                uint32_t w, uint32_t h) {
    const glm::vec3 dir     = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right   = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up      = glm::normalize(glm::cross(right, dir));
    RecipePushConstants pc{};
    pc.camPos   = eye;
    pc._p0      = 0.0f;
    pc.camDir   = dir;
    pc.fov      = 45.0f;   // DEGREES
    pc.camUp    = up;
    pc.aspect   = static_cast<float>(w) / static_cast<float>(h);
    pc.camRight = right;
    pc._p1      = 0;
    return pc;
}

// ---------------------------------------------------------------------------
// Build SdfInstruction helpers (mirror test_recipe_codegen.cpp)
// ---------------------------------------------------------------------------
static Vixen::SVO::Recipe::SdfInstruction makeSphere(float cx, float cy, float cz, float r) {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    in.data[0] = cx; in.data[1] = cy; in.data[2] = cz; in.data[3] = r;
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeUnion() {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeBox(float bx, float by, float bz) {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Box);
    in.data[0] = bx; in.data[1] = by; in.data[2] = bz;
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeSmoothUnion(float k) {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::SmoothUnion);
    in.data[2] = k;  // k = Data0.z
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeMirrorX() {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::MirrorX);
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeRestorePos() {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::RestorePos);
    return in;
}
static Vixen::SVO::Recipe::SdfInstruction makeSubtract() {
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Subtract);
    return in;
}

// ---------------------------------------------------------------------------
// Lavapipe render fixture — minimal: 1 binding (storage image) + push constants.
// Device/instance/pool bring-up mirrors test_body_instance_raymarch_render.cpp.
// ---------------------------------------------------------------------------
class ProceduralRecipeRenderTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             softwareConfirmed_ = false;
    std::string      selectedDeviceName_;

    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& props) {
        std::string name(props.deviceName);
        for (char& c : name) c = static_cast<char>(::tolower(c));
        return (name.find("llvmpipe") != std::string::npos ||
                name.find("lavapipe") != std::string::npos) &&
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    }

    void SetUp() override {
        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_procedural_recipe_render";
        // Vulkan 1.3 instance — required so the SPIR-V 1.6 module is accepted.
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
            << "Refusing to run: device '" << selectedDeviceName_
            << "' is NOT lavapipe. Aborting before vkQueueSubmit.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());
    }

    void TearDown() override {
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
        ASSERT_GT(count, 0u)
            << "No Vulkan physical devices — is VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json set?";
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

        // CRITICAL: shaderStorageImageWriteWithoutFormat MUST be enabled or storage-image
        // writes are silently dropped by lavapipe (the shader writes nothing → black image).
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

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        }
        return UINT32_MAX;
    }

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

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VkDeviceMemory& outMem) {
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
    }

    // Dispatch the compiled shader, readback float pixels.
    // outRgba32f is width*height*4 floats (RGBA32F, one float per channel).
    void RenderProcedural(const std::vector<uint32_t>& spirv,
                          const RecipePushConstants& pc,
                          uint32_t w, uint32_t h,
                          std::vector<float>& outRgba32f) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not lavapipe; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());

        // Use RGBA32F to match glslang's default Rgba32f storage image format for RWTexture2D.
        // Using R8G8B8A8_UNORM would cause a format mismatch (StorageImage format validation).
        const VkFormat kColorFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
        VkImage colorImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE);

        // Shader module from compiled SPIR-V.
        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        // Minimal descriptor set layout: binding 0 = storage image.
        VkDescriptorSetLayoutBinding imgBinding{};
        imgBinding.binding         = 0;
        imgBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imgBinding.descriptorCount = 1;
        imgBinding.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 1;
        dslci.pBindings    = &imgBinding;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        // Push constant range: 64 bytes for RecipePushConstants (cbuffer PC).
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset     = 0;
        pcr.size       = sizeof(RecipePushConstants);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &dsl;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcr;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module       = shaderModule;
        cpci.stage.pName        = "main";
        cpci.layout             = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
                  VK_SUCCESS);

        // Descriptor pool + set.
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes    = &poolSize;
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool     = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

        VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet imgWrite{};
        imgWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imgWrite.dstSet          = descSet;
        imgWrite.dstBinding      = 0;
        imgWrite.descriptorCount = 1;
        imgWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imgWrite.pImageInfo      = &colorInfo;
        vkUpdateDescriptorSets(logicalDevice_, 1, &imgWrite, 0, nullptr);

        // Allocate + record command buffer.
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = commandPool_;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        // UNDEFINED → GENERAL before the compute shader writes.
        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image               = colorImg;
        toGeneral.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toGeneral.srcAccessMask       = 0;
        toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        // GENERAL → TRANSFER_SRC before readback.
        VkImageMemoryBarrier toSrc{};
        toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image               = colorImg;
        toSrc.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSrc);

        // RGBA32F = 4 floats per pixel = 16 bytes per pixel.
        const VkDeviceSize rbSize = static_cast<VkDeviceSize>(w) * h * 4 * sizeof(float);
        VkBuffer rbBuf = VK_NULL_HANDLE;
        VkDeviceMemory rbMem = VK_NULL_HANDLE;
        CreateHostBuffer(rbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rbBuf, rbMem);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent      = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &copy);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;

        // Final safety gate — abort before submit if not software device.
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not lavapipe; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rbMem, 0, rbSize, 0, &mapped), VK_SUCCESS);
        outRgba32f.resize(static_cast<size_t>(w) * h * 4);
        std::memcpy(outRgba32f.data(), mapped, static_cast<size_t>(rbSize));
        vkUnmapMemory(logicalDevice_, rbMem);

        // Cleanup.
        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rbBuf, nullptr);
        vkFreeMemory(logicalDevice_, rbMem, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr);
        vkFreeMemory(logicalDevice_, colorMem, nullptr);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// THE TEST
// ---------------------------------------------------------------------------
TEST_F(ProceduralRecipeRenderTest, RenderProceduralRecipe) {
    // Step 1: read the vendored SdfCoreKernels HLSL.
    std::ifstream kernelFile(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << kernelFile.rdbuf();
    const std::string sdfCoreHlsl = ss.str();

    // Step 2: build peanut recipe — two overlapping spheres at x=±2, r=2.5.
    // The union gives a peanut/figure-8 shape with bumps at ±2 on X.
    Vixen::SVO::Recipe::SdfInstruction prog[] = {
        makeSphere(-2.0f, 0.0f, 0.0f, 2.5f),
        makeSphere( 2.0f, 0.0f, 0.0f, 2.5f),
        makeUnion()
    };
    const std::string shaderSrc =
        Vixen::SVO::Recipe::EmitProceduralComputeShader(prog, 3, sdfCoreHlsl);

    // Step 3: compile HLSL → SPIR-V via ShaderCompiler.
    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL;
    // ponytail: validateSpirv=false (known glslang SPIR-V validator quirk, compile-gate uses it in M1)
    opts.validateSpirv  = false;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "HLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Step 4: set up camera looking at the peanut from the side, framing both lobes.
    // Peanut centre = (0,0,0), extent ≈ ±4.5 on X, ±2.5 on Y/Z.
    // Eye at (0, 0, 12) looking toward origin — shows both lobes side-by-side.
    constexpr uint32_t W = 512, H = 512;
    const RecipePushConstants pc = MakeCamera(
        glm::vec3(0.0f, 0.0f, 12.0f),   // eye
        glm::vec3(0.0f, 0.0f,  0.0f),   // target
        W, H
    );

    // Step 5: dispatch on lavapipe, readback float pixels (RGBA32F).
    std::vector<float> rgba32f;
    ASSERT_NO_FATAL_FAILURE(RenderProcedural(compOut.spirv, pc, W, H, rgba32f));
    ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

    // Step 6: count body pixels (non-background) and convert to RGBA8 for PNG.
    // Background = float3(0.02, 0.02, 0.05). Body is lit green: R>0.08 OR G>0.08 threshold.
    int bodyPixels = 0;
    std::vector<uint8_t> rgba8(W * H * 4);
    for (uint32_t i = 0; i < W * H; ++i) {
        const float fr = rgba32f[i * 4 + 0];
        const float fg = rgba32f[i * 4 + 1];
        const float fb = rgba32f[i * 4 + 2];
        const float fa = rgba32f[i * 4 + 3];
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::min(fr * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::min(fg * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::min(fb * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 3] = static_cast<uint8_t>(std::min(fa * 255.0f + 0.5f, 255.0f));
        if (fr > 0.08f || fg > 0.08f) ++bodyPixels;
    }

    // Step 7: write PNG (controller reads it to confirm smooth two-lobe peanut).
    const char* pngPath = "/tmp/glsl_recipe_procedural.png";
    const int pngOk = stbi_write_png(pngPath,
        static_cast<int>(W), static_cast<int>(H), 4,
        rgba8.data(), static_cast<int>(W) * 4);

    // Diagnostics.
    printf("[RenderProceduralRecipe] bodyPixels=%d  PNG written=%s  path=%s\n",
           bodyPixels, pngOk ? "YES" : "NO", pngPath);
    fflush(stdout);

    EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << pngPath;
    ASSERT_GT(bodyPixels, 20000)
        << "bodyPixels=" << bodyPixels << " <= 20000 — image is likely all-black. "
           "Check: (1) shaderStorageImageWriteWithoutFormat enabled, (2) Vulkan 1.3 instance, "
           "(3) camera framing — are the spheres in view? "
           "Sample floats [pixel 0]: R=" << rgba32f[0] << " G=" << rgba32f[1]
           << " B=" << rgba32f[2] << " A=" << rgba32f[3]
           << "\nEmitted shader source (first 2000 chars):\n"
        << shaderSrc.substr(0, 2000);
}

// ---------------------------------------------------------------------------
// P2.4 M2b — Live lavapipe render gate for MirrorX(SmoothUnion(Box, Sphere)).
//
// Recipe: [MirrorX, Box(halfExtents), Sphere(center, r), SmoothUnion(k), RestorePos]
// Box (halfExtents 0.6,0.5,0.5) is centred at origin in mirrored space — a slightly
// wide symmetric block. Sphere at (1.8,0,0) appears at BOTH x=+1.8 AND x=−1.8 after
// the mirror fold, giving two protrusions blended into the box via smooth-union.
// Result from the front: bilaterally symmetric dumbbell — box centre + two rounded
// blobs on each side. Mirror symmetry + smooth-union are visually distinct from a
// plain sphere∪sphere peanut.
//
// Writes /tmp/glsl_sdf_m2_mirror.png (512×512 RGBA8).
// Controller/validator reads the PNG to confirm symmetry and smooth blending.
// ---------------------------------------------------------------------------
TEST_F(ProceduralRecipeRenderTest, RenderMirrorCsgRecipe) {
    // Step 1: read the vendored SdfCoreKernels HLSL.
    std::ifstream kernelFile(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << kernelFile.rdbuf();
    const std::string sdfCoreHlsl = ss.str();

    // Step 2: build mirror-CSG recipe.
    // In mirrored space (pos.x = |p.x|, pos.y/z unchanged):
    //   Box centred at origin, halfExtents (0.6, 0.5, 0.5)
    //   Sphere at (1.8, 0, 0), radius 0.9
    //   SmoothUnion k=0.5 → wide smooth blend
    // After RestorePos, world pos is restored. The body is symmetric in x.
    Vixen::SVO::Recipe::SdfInstruction prog[] = {
        makeMirrorX(),
        makeBox(0.6f, 0.5f, 0.5f),
        makeSphere(1.8f, 0.0f, 0.0f, 0.9f),
        makeSmoothUnion(0.5f),
        makeRestorePos()
    };
    const std::string shaderSrc =
        Vixen::SVO::Recipe::EmitProceduralComputeShader(prog, 5, sdfCoreHlsl);

    // Step 3: compile HLSL → SPIR-V.
    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (see P2.2 M1)
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "HLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Step 4: camera looking head-on at the symmetric body from z+.
    // Body extent: ±(1.8+0.9)=±2.7 on x, ±(0.5+~0.5bleed)≈±1.2 on y/z.
    // Eye at (0,0,10), target (0,0,0), FOV 45° gives comfortable framing.
    constexpr uint32_t W = 512, H = 512;
    const RecipePushConstants pc = MakeCamera(
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f,  0.0f),
        W, H
    );

    // Step 5: dispatch on lavapipe, readback float pixels.
    std::vector<float> rgba32f;
    ASSERT_NO_FATAL_FAILURE(RenderProcedural(compOut.spirv, pc, W, H, rgba32f));
    ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

    // Step 6: count body pixels + convert to RGBA8 for PNG.
    int bodyPixels = 0;
    std::vector<uint8_t> rgba8(W * H * 4);
    for (uint32_t i = 0; i < W * H; ++i) {
        const float fr = rgba32f[i * 4 + 0];
        const float fg = rgba32f[i * 4 + 1];
        const float fb = rgba32f[i * 4 + 2];
        const float fa = rgba32f[i * 4 + 3];
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::min(fr * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::min(fg * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::min(fb * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 3] = static_cast<uint8_t>(std::min(fa * 255.0f + 0.5f, 255.0f));
        if (fr > 0.08f || fg > 0.08f) ++bodyPixels;
    }

    // Step 7: write PNG — controller reads it to verify mirror symmetry + smooth-union.
    const char* pngPath = "/tmp/glsl_sdf_m2_mirror.png";
    const int pngOk = stbi_write_png(pngPath,
        static_cast<int>(W), static_cast<int>(H), 4,
        rgba8.data(), static_cast<int>(W) * 4);

    printf("[RenderMirrorCsgRecipe] bodyPixels=%d  PNG written=%s  path=%s\n",
           bodyPixels, pngOk ? "YES" : "NO", pngPath);
    fflush(stdout);

    EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << pngPath;
    ASSERT_GT(bodyPixels, 20000)
        << "bodyPixels=" << bodyPixels << " <= 20000 — image is likely all-black. "
           "Check: (1) shaderStorageImageWriteWithoutFormat, (2) Vulkan 1.3, "
           "(3) camera framing. "
           "Sample floats [pixel 0]: R=" << rgba32f[0] << " G=" << rgba32f[1]
           << " B=" << rgba32f[2] << " A=" << rgba32f[3]
           << "\nEmitted shader (first 2000 chars):\n"
        << shaderSrc.substr(0, 2000);
}

// ---------------------------------------------------------------------------
// P2.4 M3a — Live lavapipe render gate for Subtract(Box, Sphere).
//
// Recipe: [Box(halfExtents 0.7,0.7,0.7), Sphere(center=(0,0,0.7), r=0.55), Subtract]
// Sphere centered on the +z box face protrudes through it; Subtract carves a visible
// concave spherical bite into the front face (bowl-like depression when seen from z+).
// NOTE: sphere at origin would be fully enclosed (r=0.55 < halfExtent=0.7) →
// interior void, invisible from outside; center must sit on/near the face.
// This is the authoritative GPU-matches-CPU proof for the non-commutative Subtract
// opcode (A=box=base, B=sphere=cutter).
//
// Writes /tmp/glsl_sdf_m3a_subtract.png (512×512 RGBA8). ICD-only (no validation).
// Validator reads the PNG to confirm the box-with-spherical-cavity appearance.
// ---------------------------------------------------------------------------
TEST_F(ProceduralRecipeRenderTest, RenderSubtractBoxSphere) {
    // Step 1: read vendored SdfCoreKernels HLSL (now includes all M3a kernels).
    std::ifstream kernelFile(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << kernelFile.rdbuf();
    const std::string sdfCoreHlsl = ss.str();

    // Step 2: build Subtract(Box, Sphere) recipe.
    // Box halfExtents (0.7,0.7,0.7) = A (base, deeper on stack).
    // Sphere centered on the +z face (0,0,0.7) r=0.55 = B (cutter, top of stack).
    // The sphere protrudes through the +z face → Subtract carves a VISIBLE concave
    // spherical bite visible from the front (camera at z+). A sphere at the origin
    // would be fully enclosed (r=0.55 < halfExtent=0.7) → interior void, invisible.
    Vixen::SVO::Recipe::SdfInstruction prog[] = {
        makeBox(0.7f, 0.7f, 0.7f),
        makeSphere(0.0f, 0.0f, 0.7f, 0.55f),
        makeSubtract()
    };
    const std::string shaderSrc =
        Vixen::SVO::Recipe::EmitProceduralComputeShader(prog, 3, sdfCoreHlsl);

    // Step 3: compile HLSL → SPIR-V.
    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk (see P2.2 M1)
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "HLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Step 4: camera looking from z+ at the carved box.
    // Box extent ±0.7 on all axes; eye at (0,0,6) gives comfortable framing.
    // Slight upward tilt reveals the cavity on the front face.
    constexpr uint32_t W = 512, H = 512;
    const RecipePushConstants pc = MakeCamera(
        glm::vec3(0.0f, 0.3f, 6.0f),   // eye — slight up tilt shows the carved face
        glm::vec3(0.0f, 0.0f, 0.0f),   // target — box centre
        W, H
    );

    // Step 5: dispatch on lavapipe, readback float pixels.
    std::vector<float> rgba32f;
    ASSERT_NO_FATAL_FAILURE(RenderProcedural(compOut.spirv, pc, W, H, rgba32f));
    ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

    // Step 6: count body pixels + convert to RGBA8 for PNG.
    int bodyPixels = 0;
    std::vector<uint8_t> rgba8(W * H * 4);
    for (uint32_t i = 0; i < W * H; ++i) {
        const float fr = rgba32f[i * 4 + 0];
        const float fg = rgba32f[i * 4 + 1];
        const float fb = rgba32f[i * 4 + 2];
        const float fa = rgba32f[i * 4 + 3];
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::min(fr * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::min(fg * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::min(fb * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 3] = static_cast<uint8_t>(std::min(fa * 255.0f + 0.5f, 255.0f));
        if (fr > 0.08f || fg > 0.08f) ++bodyPixels;
    }

    // Step 7: write PNG — validator reads it to verify box-with-spherical-cavity.
    const char* pngPath = "/tmp/glsl_sdf_m3a_subtract.png";
    const int pngOk = stbi_write_png(pngPath,
        static_cast<int>(W), static_cast<int>(H), 4,
        rgba8.data(), static_cast<int>(W) * 4);

    printf("[RenderSubtractBoxSphere] bodyPixels=%d  PNG written=%s  path=%s\n",
           bodyPixels, pngOk ? "YES" : "NO", pngPath);
    fflush(stdout);

    EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << pngPath;
    ASSERT_GT(bodyPixels, 5000)
        << "bodyPixels=" << bodyPixels << " <= 5000 — image is likely all-black. "
           "Check: (1) shaderStorageImageWriteWithoutFormat, (2) Vulkan 1.3, "
           "(3) Subtract opcode case landed in EmitProceduralComputeShader. "
           "Sample floats [pixel 0]: R=" << rgba32f[0] << " G=" << rgba32f[1]
           << " B=" << rgba32f[2] << " A=" << rgba32f[3]
           << "\nEmitted shader (first 2000 chars):\n"
        << shaderSrc.substr(0, 2000);
}

// ---------------------------------------------------------------------------
// P2.4 M3b-1 — Live lavapipe render gate for Torus.
//
// Recipe: [Torus(majorRadius=0.6, minorRadius=0.2)]
// Torus ring in the XZ plane, centred at origin. Camera looking from Z+
// down slightly (elevated) shows the classic ring/donut cross-section with
// a hole through the centre.
//
// Writes /tmp/glsl_sdf_m3b_torus.png (512×512 RGBA8). ICD-only (no validation).
// Validator reads the PNG to confirm a ring/donut shape.
// ---------------------------------------------------------------------------
TEST_F(ProceduralRecipeRenderTest, RenderTorus) {
    // Step 1: read the vendored SdfCoreKernels HLSL (now includes Torus).
    std::ifstream kernelFile(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << kernelFile.rdbuf();
    const std::string sdfCoreHlsl = ss.str();

    // Step 2: Torus recipe. majorR=0.6, minorR=0.2 → ring in XZ plane.
    // data[0]=majorRadius, data[1]=minorRadius
    Vixen::SVO::Recipe::SdfInstruction torus{};
    torus.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Torus);
    torus.data[0] = 0.6f;  // majorRadius
    torus.data[1] = 0.2f;  // minorRadius

    Vixen::SVO::Recipe::SdfInstruction prog[] = { torus };
    const std::string shaderSrc =
        Vixen::SVO::Recipe::EmitProceduralComputeShader(prog, 1, sdfCoreHlsl);

    // Step 3: compile HLSL → SPIR-V.
    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "HLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Step 4: camera elevated above the XZ plane looking down at a slight angle.
    // Eye at (0, 2.5, 4.0) looking toward origin — shows ring from slightly above,
    // revealing the hole through the centre (classic donut view).
    // Torus extent: |XZ| ≤ majorR+minorR = 0.8, Y height ≤ minorR = 0.2.
    constexpr uint32_t W = 512, H = 512;
    const RecipePushConstants pc = MakeCamera(
        glm::vec3(0.0f, 2.5f, 4.0f),   // eye — slightly above and back
        glm::vec3(0.0f, 0.0f, 0.0f),   // target — torus centre
        W, H
    );

    // Step 5: dispatch on lavapipe, readback float pixels (RGBA32F).
    std::vector<float> rgba32f;
    ASSERT_NO_FATAL_FAILURE(RenderProcedural(compOut.spirv, pc, W, H, rgba32f));
    ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

    // Step 6: count body pixels + convert to RGBA8 for PNG.
    int bodyPixels = 0;
    std::vector<uint8_t> rgba8(W * H * 4);
    for (uint32_t i = 0; i < W * H; ++i) {
        const float fr = rgba32f[i * 4 + 0];
        const float fg = rgba32f[i * 4 + 1];
        const float fb = rgba32f[i * 4 + 2];
        const float fa = rgba32f[i * 4 + 3];
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::min(fr * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::min(fg * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::min(fb * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 3] = static_cast<uint8_t>(std::min(fa * 255.0f + 0.5f, 255.0f));
        if (fr > 0.08f || fg > 0.08f) ++bodyPixels;
    }

    // Step 7: write PNG — validator reads it to confirm ring/donut shape with hole.
    const char* pngPath = "/tmp/glsl_sdf_m3b_torus.png";
    const int pngOk = stbi_write_png(pngPath,
        static_cast<int>(W), static_cast<int>(H), 4,
        rgba8.data(), static_cast<int>(W) * 4);

    printf("[RenderTorus] bodyPixels=%d  PNG written=%s  path=%s\n",
           bodyPixels, pngOk ? "YES" : "NO", pngPath);
    fflush(stdout);

    EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << pngPath;
    ASSERT_GT(bodyPixels, 5000)
        << "bodyPixels=" << bodyPixels << " <= 5000 — image is likely all-black. "
           "Check: (1) shaderStorageImageWriteWithoutFormat, (2) Vulkan 1.3, "
           "(3) Torus opcode case in EmitProceduralComputeShader. "
           "Sample floats [pixel 0]: R=" << rgba32f[0] << " G=" << rgba32f[1]
           << " B=" << rgba32f[2] << " A=" << rgba32f[3]
           << "\nEmitted shader (first 2000 chars):\n"
        << shaderSrc.substr(0, 2000);
}

// ---------------------------------------------------------------------------
// P2.4 M3b-2 — Live lavapipe render gate for Cone (positioned op).
//
// Recipe: [Cone(half-angle=30°, height=1.0, offset=(0,0,0))]
//   sin(30°)=0.5, cos(30°)=0.866. Apex at origin, base circle at y=-1.0,
//   base radius ≈ height * sin/cos ≈ 0.577. Camera slightly elevated from z+
//   looking toward the cone's mid-point — shows a classic pointed triangle
//   silhouette confirming the Cone kernel + position-offset path works on GPU.
//   Position offset is (0,0,0) so the emit path still generates "p - float3(0,0,0)".
//
// Writes /tmp/glsl_sdf_m3b_cone.png (512×512 RGBA8). ICD-only (no validation).
// Validator reads the PNG to confirm a cone/triangle shape.
// ---------------------------------------------------------------------------
TEST_F(ProceduralRecipeRenderTest, RenderCone) {
    // Step 1: read the vendored SdfCoreKernels HLSL (now includes Cone).
    std::ifstream kernelFile(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss;
    ss << kernelFile.rdbuf();
    const std::string sdfCoreHlsl = ss.str();

    // Step 2: Cone recipe. half-angle=30° (sin=0.5, cos=0.866), height=1.0, no offset.
    // data[0]=sinAngle, data[1]=cosAngle, data[2]=height, data[4..6]=position offset (all 0).
    Vixen::SVO::Recipe::SdfInstruction cone{};
    cone.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Cone);
    cone.data[0] = 0.5f;       // sin(30°)
    cone.data[1] = 0.866025f;  // cos(30°)
    cone.data[2] = 1.0f;       // height
    // data[4..6] remain 0.0f (default) — position offset at origin

    Vixen::SVO::Recipe::SdfInstruction prog[] = { cone };
    const std::string shaderSrc =
        Vixen::SVO::Recipe::EmitProceduralComputeShader(prog, 1, sdfCoreHlsl);

    // Step 3: compile HLSL → SPIR-V.
    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL;
    opts.validateSpirv  = false;  // ponytail: glslang SPIR-V validator quirk
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "HLSL compile failed:\n" << compOut.GetFullLog()
        << "\n--- emitted source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    // Step 4: camera looking from z+ slightly elevated.
    // Cone: apex at (0,0,0), base at y=-1.0 (radius ≈ 0.577).
    // Eye at (0, 0.5, 4.0) target (0, -0.5, 0) — shows pointed triangle silhouette.
    constexpr uint32_t W = 512, H = 512;
    const RecipePushConstants pc = MakeCamera(
        glm::vec3(0.0f, 0.5f, 4.0f),   // eye — slightly above and back
        glm::vec3(0.0f, -0.5f, 0.0f),  // target — cone midpoint
        W, H
    );

    // Step 5: dispatch on lavapipe, readback float pixels (RGBA32F).
    std::vector<float> rgba32f;
    ASSERT_NO_FATAL_FAILURE(RenderProcedural(compOut.spirv, pc, W, H, rgba32f));
    ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

    // Step 6: count body pixels + convert to RGBA8 for PNG.
    int bodyPixels = 0;
    std::vector<uint8_t> rgba8(W * H * 4);
    for (uint32_t i = 0; i < W * H; ++i) {
        const float fr = rgba32f[i * 4 + 0];
        const float fg = rgba32f[i * 4 + 1];
        const float fb = rgba32f[i * 4 + 2];
        const float fa = rgba32f[i * 4 + 3];
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::min(fr * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::min(fg * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::min(fb * 255.0f + 0.5f, 255.0f));
        rgba8[i * 4 + 3] = static_cast<uint8_t>(std::min(fa * 255.0f + 0.5f, 255.0f));
        if (fr > 0.08f || fg > 0.08f) ++bodyPixels;
    }

    // Step 7: write PNG — validator reads it to confirm cone/triangle shape.
    const char* pngPath = "/tmp/glsl_sdf_m3b_cone.png";
    const int pngOk = stbi_write_png(pngPath,
        static_cast<int>(W), static_cast<int>(H), 4,
        rgba8.data(), static_cast<int>(W) * 4);

    printf("[RenderCone] bodyPixels=%d  PNG written=%s  path=%s\n",
           bodyPixels, pngOk ? "YES" : "NO", pngPath);
    fflush(stdout);

    EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << pngPath;
    ASSERT_GT(bodyPixels, 2000)
        << "bodyPixels=" << bodyPixels << " <= 2000 — image is likely all-black. "
           "Check: (1) shaderStorageImageWriteWithoutFormat, (2) Vulkan 1.3, "
           "(3) Cone opcode case in EmitProceduralComputeShader. "
           "Sample floats [pixel 0]: R=" << rgba32f[0] << " G=" << rgba32f[1]
           << " B=" << rgba32f[2] << " A=" << rgba32f[3]
           << "\nEmitted shader (first 2000 chars):\n"
        << shaderSrc.substr(0, 2000);
}
