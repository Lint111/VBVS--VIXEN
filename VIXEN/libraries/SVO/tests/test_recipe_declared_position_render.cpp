/**
 * @file test_recipe_declared_position_render.cpp
 * @brief Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype's
 *        live-render/visual gate. Renders the hand-authored prototype recipe
 *        [ReadParamFloat3(idx=0), DeclarePosition, Sphere(center=0,r=0.5)] as a flat 2D
 *        distance-field slice (Z=0 plane, no ray marching needed since the emitted GLSL
 *        function IS the distance field) at 3 different ReadParam-supplied declared
 *        positions, writing one PNG per position for visual confirmation that changing the
 *        declared position actually moves where the shape renders — not a full 3D ray-traced
 *        render (that infrastructure doesn't exist for the field-function-only GLSL emitter;
 *        EmitProceduralComputeShader's HLSL trace-main is a different, unrelated emitter path
 *        per SdfRecipeCodegen.h), but a direct, cheap, real-GPU visual proof of the same claim
 *        test_recipe_glsl_numerical_parity.cpp's DeclaredPositionMatchesAcrossCpuAndGpu already
 *        proves numerically.
 *
 * Device selection/bring-up mirrors test_recipe_glsl_numerical_parity.cpp's
 * RecipeGlslNumericalParityTest fixture (real discrete/integrated GPU required; SKIPs
 * otherwise — no lavapipe/Dozen for this task).
 *
 * Run directly (per KI-014, not via ctest):
 *   build\ninja\libraries\SVO\tests\Debug\test_recipe_declared_position_render.exe
 *
 * Output: /tmp/declared_pos_{0,1,2}.png (256x256, white=inside sphere / black=outside,
 *   sampling the XY plane at Z=0, world extent [-4,4]^2).
 */

#include <gtest/gtest.h>

#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "Recipe/SdfRecipeEval.h"
#include "ShaderCompiler.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDF_CORE_KERNELS_GLSL_PATH
#  error "SDF_CORE_KERNELS_GLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

using namespace Vixen::SVO::Recipe;

// Compose a compute shader that samples the emitted field function across a WxH grid over
// the world-space XY plane at Z=0, writing sign(field) (1.0 = inside/on-surface, 0.0 =
// outside) into an RGBA32F storage image — a flat "which pixels are inside the shape" slice,
// not a ray-traced render. worldHalfExtent controls the sampled world-space square
// [-worldHalfExtent, +worldHalfExtent]^2.
std::string ComposeSliceShader(const std::string& sdfCoreGlsl, const std::string& emittedFieldFn,
                                float worldHalfExtent) {
    std::ostringstream ss;
    ss << "#version 450\n";
    ss << sdfCoreGlsl << "\n";
    ss << emittedFieldFn << "\n";
    ss << "layout(local_size_x = 8, local_size_y = 8) in;\n";
    ss << "layout(set = 0, binding = 0, rgba32f) uniform image2D outImage;\n";
    ss << "layout(set = 0, binding = 1, std430) readonly buffer InParams { float params[6]; };\n";
    ss << "void main() {\n";
    ss << "  ivec2 sz = imageSize(outImage);\n";
    ss << "  if (gl_GlobalInvocationID.x >= uint(sz.x) || gl_GlobalInvocationID.y >= uint(sz.y)) return;\n";
    ss << "  float u = (float(gl_GlobalInvocationID.x) + 0.5) / float(sz.x);\n";
    ss << "  float v = (float(gl_GlobalInvocationID.y) + 0.5) / float(sz.y);\n";
    ss << "  float he = " << worldHalfExtent << ";\n";
    ss << "  vec3 p = vec3((u * 2.0 - 1.0) * he, (v * 2.0 - 1.0) * he, 0.0);\n";
    ss << "  float pr[6] = float[6](params[0], params[1], params[2], params[3], params[4], params[5]);\n";
    ss << "  vec3 declaredPos;\n";
    ss << "  float d = sdfRecipe_0(p, pr, declaredPos);\n";
    ss << "  float inside = d <= 0.0 ? 1.0 : 0.0;\n";
    ss << "  imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), vec4(inside, inside, inside, 1.0));\n";
    ss << "}\n";
    return ss.str();
}

class DeclaredPositionRenderTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             realGpuConfirmed_ = false;
    std::string      selectedDeviceName_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_recipe_declared_position_render";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;

        if (vkCreateInstance(&instInfo, nullptr, &instance_) != VK_SUCCESS) {
            GTEST_SKIP() << "vkCreateInstance failed — no Vulkan available on this machine.";
        }

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) GTEST_SKIP() << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                realGpuConfirmed_   = true;
                break;
            }
        }
        if (!realGpuConfirmed_) {
            GTEST_SKIP() << "No REAL (discrete/integrated) GPU found — skipping live-render gate.";
        }

        CreateLogicalDevice();
        CreateCommandPool();
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
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
        ASSERT_TRUE(found) << "No compute queue family on the selected GPU";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        // shaderStorageImageWriteWithoutFormat: rgba32f imageStore needs this on some drivers
        // (same gotcha as test_procedural_recipe_render.cpp).
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
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImage& outImage, VkDeviceMemory& outMem) {
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

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuf, VkDeviceMemory& outMem) {
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

    // Dispatch the slice shader once, readback RGBA32F pixels.
    void RenderSlice(const std::vector<uint32_t>& spirv, const std::array<float, 6>& params,
                      uint32_t w, uint32_t h, std::vector<float>& outRgba32f) {
        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());

        const VkFormat kColorFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
        VkImage colorImg = VK_NULL_HANDLE; VkDeviceMemory colorMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE);

        const VkDeviceSize paramsSize = params.size() * sizeof(float);
        VkBuffer paramsBuf = VK_NULL_HANDLE; VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(paramsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, paramsBuf, paramsMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, paramsMem, 0, paramsSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, params.data(), static_cast<size_t>(paramsSize));
            vkUnmapMemory(logicalDevice_, paramsMem);
        }

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 2;
        dslci.pBindings    = bindings;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts    = &dsl;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes    = poolSizes;
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
        VkDescriptorBufferInfo paramsInfo{paramsBuf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSet; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &colorInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSet; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &paramsInfo;
        vkUpdateDescriptorSets(logicalDevice_, 2, writes, 0, nullptr);

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
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

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
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSrc);

        const VkDeviceSize rbSize = static_cast<VkDeviceSize>(w) * h * 4 * sizeof(float);
        VkBuffer rbBuf = VK_NULL_HANDLE; VkDeviceMemory rbMem = VK_NULL_HANDLE;
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

        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rbMem, 0, rbSize, 0, &mapped), VK_SUCCESS);
        outRgba32f.resize(static_cast<size_t>(w) * h * 4);
        std::memcpy(outRgba32f.data(), mapped, static_cast<size_t>(rbSize));
        vkUnmapMemory(logicalDevice_, rbMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rbBuf, nullptr); vkFreeMemory(logicalDevice_, rbMem, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr); vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyBuffer(logicalDevice_, paramsBuf, nullptr); vkFreeMemory(logicalDevice_, paramsMem, nullptr);
    }
};

} // namespace

TEST_F(DeclaredPositionRenderTest, ThreeDeclaredPositionsMoveTheRenderedShape) {
    constexpr uint32_t W = 256, H = 256;
    constexpr float kWorldHalfExtent = 4.0f; // world extent [-4,4]^2

    // Same prototype recipe as test_recipe_eval_parity.cpp /
    // test_recipe_glsl_numerical_parity.cpp's dedicated test.
    const SdfInstruction prog[] = {
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::ReadParamFloat3; in.paramMask = 1; in.data[0] = 0.0f; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::DeclarePosition; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere; in.data[3] = 0.5f; return in; }(),
    };
    constexpr uint32_t kCount = 3;

    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good()) << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    const std::string fieldFn = EmitProceduralFieldFunctionGlsl(
        prog, kCount, /*recipeId=*/0, /*emitDeclaredPositionOutParam=*/true);
    const std::string shaderSrc = ComposeSliceShader(sdfCoreGlsl, fieldFn, kWorldHalfExtent);

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "GLSL compile failed:\n" << compOut.GetFullLog() << "\n--- source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    struct Case { const char* path; glm::vec3 declared; };
    const std::array<Case, 3> cases = {{
        {"/tmp/declared_pos_0.png", glm::vec3(0.0f, 0.0f, 0.0f)},   // sphere at world origin
        {"/tmp/declared_pos_1.png", glm::vec3(2.0f, 1.5f, 0.0f)},   // moved up-right
        {"/tmp/declared_pos_2.png", glm::vec3(-2.5f, -1.0f, 0.0f)}, // moved down-left
    }};

    for (const auto& c : cases) {
        const std::array<float, 6> params = {c.declared.x, c.declared.y, c.declared.z, 0.0f, 0.0f, 0.0f};
        std::vector<float> rgba32f;
        ASSERT_NO_FATAL_FAILURE(RenderSlice(compOut.spirv, params, W, H, rgba32f));
        ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

        int insidePixels = 0;
        std::vector<uint8_t> rgba8(W * H * 4);
        for (uint32_t i = 0; i < W * H; ++i) {
            const float v = rgba32f[i * 4 + 0]; // R==G==B (grayscale inside mask)
            const uint8_t b = static_cast<uint8_t>(v > 0.5f ? 255 : 0);
            rgba8[i * 4 + 0] = b; rgba8[i * 4 + 1] = b; rgba8[i * 4 + 2] = b; rgba8[i * 4 + 3] = 255;
            if (v > 0.5f) ++insidePixels;
        }
        const int pngOk = stbi_write_png(c.path, static_cast<int>(W), static_cast<int>(H), 4,
                                          rgba8.data(), static_cast<int>(W) * 4);
        printf("[DeclaredPositionRender] declared=(%.2f,%.2f,%.2f) insidePixels=%d PNG=%s path=%s\n",
               c.declared.x, c.declared.y, c.declared.z, insidePixels, pngOk ? "YES" : "NO", c.path);
        fflush(stdout);
        EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << c.path;

        // Sanity: some pixels must be inside (the sphere is within the sampled world extent
        // for all 3 declared positions above) and it must not be the WHOLE image (i.e. the
        // shape is a bounded disc, not degenerate).
        EXPECT_GT(insidePixels, 20) << "declared=(" << c.declared.x << "," << c.declared.y
                                     << "," << c.declared.z << ") — sphere disc too small/absent";
        EXPECT_LT(insidePixels, static_cast<int>(W * H) / 2)
            << "declared=(" << c.declared.x << "," << c.declared.y << "," << c.declared.z
            << ") — unexpectedly large inside region";

        // Compute the pixel-space centroid of the inside mask and confirm it lands near the
        // expected screen position for the declared world position (world->pixel: same
        // mapping as ComposeSliceShader's u/v -> world formula, inverted).
        double sumX = 0.0, sumY = 0.0;
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                if (rgba32f[(y * W + x) * 4 + 0] > 0.5f) { sumX += x; sumY += y; }
            }
        }
        ASSERT_GT(insidePixels, 0);
        const double centroidPxX = sumX / insidePixels;
        const double centroidPxY = sumY / insidePixels;
        const double expectedU = (c.declared.x / kWorldHalfExtent + 1.0) * 0.5;
        const double expectedV = (c.declared.y / kWorldHalfExtent + 1.0) * 0.5;
        const double expectedPxX = expectedU * W;
        const double expectedPxY = expectedV * H;
        EXPECT_NEAR(centroidPxX, expectedPxX, 4.0)
            << "declared=(" << c.declared.x << "," << c.declared.y << ") centroid X mismatch — "
               "declared position did not move the rendered shape as expected";
        EXPECT_NEAR(centroidPxY, expectedPxY, 4.0)
            << "declared=(" << c.declared.x << "," << c.declared.y << ") centroid Y mismatch — "
               "declared position did not move the rendered shape as expected";
    }
}
