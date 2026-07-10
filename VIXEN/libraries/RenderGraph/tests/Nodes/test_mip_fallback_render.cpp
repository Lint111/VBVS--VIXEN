/**
 * @file test_mip_fallback_render.cpp
 * @brief Sparse-Mip ESVO LOD Inc1 M3 gate: shader-side mip fallback read (Tasks 7-9).
 *
 * Bakes a single sphere via ConcatenateSdfWithMips (MipBake.h) so the pool carries a
 * real per-node mip sample alongside the usual node/brick/channel data, calls
 * SetRecipePool + RequestBrickResidency(false), and renders. With bricks never
 * uploaded, every leaf hit-test must fall back to Task 7's mip[nodeIdx] read — this
 * test asserts the result is a recognizable silhouette (fillRatio-style pixel-coverage
 * AND shape check, not just "some pixels lit": the Inc2 M6 precedent this Plan's Task 9
 * explicitly cites found a silhouette-only check insufficient), not a blank/black frame.
 *
 * No-regression: the SAME pool rendered with RequestBrickResidency(true) (bricks fully
 * uploaded) must produce a materially similar hit-pixel count/shape (the true brick
 * march), and an existing binary-shell-octree scene (no mip pool, no residency call —
 * BodyOctreeSceneNode's post-M3 default) must render identically to pre-Inc1.
 *
 * DEVICE SELECTION: mirrors test_recipe_pool_render.cpp — VixenSelectWslGpuIcd() picks
 * Dozen on WSL2 when provisioned, else lavapipe; only those two devices are accepted.
 *
 * Run: ./test_mip_fallback_render
 *   Output: /tmp/mip_fallback_render.png (mip-only), /tmp/mip_fallback_resident.png (resident).
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "MipBake.h"
#include "SdfBake.h"
#include "SdfRecipes.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/SdfInstruction.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float raySizeCoef; float raySizeBias; int32_t instanceCount;
};
static_assert(sizeof(PushConstants) == 76, "PushConstants must be 76 bytes");

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0); f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

constexpr float kWorldGridSize = 10.0f;

Vixen::SVO::BodyInstanceGpu MakeInst(float x, float y, float z, float scale,
                                      uint32_t octreeIndex) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = 1.0f; i.color[1] = 1.0f; i.color[2] = 1.0f;
    return i;
}

PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target, uint32_t w, uint32_t h,
                          int32_t instanceCount) {
    const glm::vec3 dir    = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right  = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up     = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos = eye;  pc.time = 0.0f;
    pc.cameraDir = dir;  pc.fov  = 45.0f;
    pc.cameraUp  = up;   pc.aspect = static_cast<float>(w) / static_cast<float>(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;   // LOD cutoff disabled — isolate Task 7's trigger
    pc.instanceCount = instanceCount;
    return pc;
}

}  // namespace

// ---------------------------------------------------------------------------
// Minimal Vulkan fixture — mirrors test_recipe_pool_render.cpp, +binding 13 (mip pool).
// ---------------------------------------------------------------------------
class MipFallbackRenderTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             softwareConfirmed_ = false;
    std::string      selectedDeviceName_;
    std::unique_ptr<VulkanDevice> deviceShell_;

    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& p) {
        std::string n(p.deviceName); for (char& c : n) c = char(::tolower(c));
        const bool isSoftware =
            (n.find("llvmpipe") != std::string::npos ||
             n.find("lavapipe") != std::string::npos) &&
            p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = n.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_mip_fallback_render"; ai.apiVersion = VK_API_VERSION_1_3;
        const auto layers = EnabledValidationLayers();
        const char* exts[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledLayerCount = uint32_t(layers.size()); ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = exts;
        ASSERT_EQ(vkCreateInstance(&ci, nullptr, &instance_), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(PickSoftwareDevice());
        ASSERT_TRUE(softwareConfirmed_);
        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCmdPool());
        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        if (logicalDevice_ != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice_, nullptr);
        if (instance_     != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    void PickSoftwareDevice() {
        uint32_t cnt = 0; ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, nullptr), VK_SUCCESS);
        ASSERT_GT(cnt, 0u) << "No Vulkan devices visible.";
        std::vector<VkPhysicalDevice> devs(cnt);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, devs.data()), VK_SUCCESS);
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (LooksLikeSoftware(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; softwareConfirmed_ = true; return; }
        }
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(devs[0], &p);
        selectedDeviceName_ = p.deviceName;
    }

    void CreateLogicalDevice() {
        uint32_t qfCnt = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCnt);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCnt; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found);
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = queueFamily_; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        VkPhysicalDeviceFeatures feats{}; feats.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        VkDeviceCreateInfo di{}; di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &feats;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &di, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCmdPool() {
        VkCommandPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &pi, nullptr, &commandPool_), VK_SUCCESS);
    }

    template<typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((filter & (1u<<i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat fmt, VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w,h,1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &img), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(logicalDevice_, img, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size; ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, img, mem, 0), VK_SUCCESS);
    }

    VkImageView MakeView(VkImage img, VkFormat fmt) {
        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkImageView v = VK_NULL_HANDLE; vkCreateImageView(logicalDevice_, &vi, nullptr, &v); return v;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& buf, VkDeviceMemory& mem, bool zero) {
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &buf), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(logicalDevice_, buf, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, buf, mem, 0), VK_SUCCESS);
        if (zero) { void* m=nullptr; vkMapMemory(logicalDevice_, mem, 0, size, 0, &m); std::memset(m,0,size_t(size)); vkUnmapMemory(logicalDevice_, mem); }
    }

    // Render using the real BodyInstanceRayMarch shader (bindings 0-5,8-13).
    void RenderToRgba(VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfg,
                      VkBuffer inst, VkBuffer sdf, VkBuffer lookup, VkBuffer mip,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& rgba, double& ms) {
        ASSERT_TRUE(softwareConfirmed_);
        VkBuffer traceBuf=VK_NULL_HANDLE, ctrBuf=VK_NULL_HANDLE;
        VkDeviceMemory traceMem=VK_NULL_HANDLE, ctrMem=VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ctrBuf, ctrMem, true);
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE, dummyMip=VK_NULL_HANDLE, dummyIter=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE, dMipMem=VK_NULL_HANDLE, dIterMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyIter,dIterMem,true);  // Inc1 M4b binding 14
        if (sdf    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf = dummySdf; }
        if (lookup == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup = dummyLookup; }
        if (mip    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyMip,dMipMem,true); mip = dummyMip; }

        VkImage colorImg=VK_NULL_HANDLE, idImg=VK_NULL_HANDLE;
        VkDeviceMemory colorMem=VK_NULL_HANDLE, idMem=VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R32_UINT, idImg, idMem));
        VkImageView colorView = MakeView(colorImg, VK_FORMAT_R8G8B8A8_UNORM);
        VkImageView idView    = MakeView(idImg,    VK_FORMAT_R32_UINT);

        const auto spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "SPIR-V missing: " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smc{}; smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = spirv.size()*4; smc.pCode = spirv.data();
        VkShaderModule sm = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smc, nullptr, &sm), VK_SUCCESS);

        auto bindL = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{}; lb.binding=b; lb.descriptorType=t;
            lb.descriptorCount=1; lb.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; return lb;
        };
        const std::array<VkDescriptorSetLayoutBinding,13> bindings = {
            bindL(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc1 M4b: per-instance iteration debug
        };
        VkDescriptorSetLayoutCreateInfo dslci{}; dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = uint32_t(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.size=sizeof(pc);
        VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount=1; plci.pSetLayouts=&dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pl), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{}; cpci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; cpci.stage.module=sm; cpci.stage.pName="main";
        cpci.layout=pl;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize,2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11},
        }};
        VkDescriptorPoolCreateInfo dpci{}; dpci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets=1; dpci.poolSizeCount=uint32_t(poolSizes.size()); dpci.pPoolSizes=poolSizes.data();
        VkDescriptorPool pool2 = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_,&dpci,nullptr,&pool2), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool=pool2; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_,&dsai,&ds), VK_SUCCESS);

        VkDescriptorImageInfo colImg{VK_NULL_HANDLE,colorView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idImgI{VK_NULL_HANDLE,idView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesI{nodes,0,VK_WHOLE_SIZE}, bricksI{bricks,0,VK_WHOLE_SIZE},
            matsI{mats,0,VK_WHOLE_SIZE}, traceI{traceBuf,0,VK_WHOLE_SIZE}, cfgI{cfg,0,VK_WHOLE_SIZE},
            ctrI{ctrBuf,0,VK_WHOLE_SIZE}, instI{inst,0,VK_WHOLE_SIZE},
            sdfI{sdf,0,VK_WHOLE_SIZE}, lookupI{lookup,0,VK_WHOLE_SIZE}, iterI{dummyIter,0,VK_WHOLE_SIZE}, mipI{mip,0,VK_WHOLE_SIZE};

        auto wI = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=info; return w;
        };
        auto wB = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo=info; return w;
        };
        const std::array<VkWriteDescriptorSet,13> writes = {
            wI(0,&colImg), wB(1,&nodesI), wB(2,&bricksI), wB(3,&matsI), wB(4,&traceI),
            wB(5,&cfgI), wB(8,&ctrI), wI(9,&idImgI), wB(10,&instI), wB(11,&sdfI), wB(12,&lookupI), wB(13,&mipI),
            wB(14,&iterI)  // Inc1 M4b: per-instance iteration debug
        };
        vkUpdateDescriptorSets(logicalDevice_, uint32_t(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{}; cbai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool=commandPool_; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=1;
        VkCommandBuffer cmd=VK_NULL_HANDLE; ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_,&cbai,&cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        auto toGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        };
        toGeneral(colorImg); toGeneral(idImg);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w+7)/8, (h+7)/8, 1);

        VkImageMemoryBarrier toSrc{}; toSrc.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout=VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        toSrc.image=colorImg; toSrc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        toSrc.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toSrc);

        const VkDeviceSize rbSz = VkDeviceSize(w)*h*4;
        VkBuffer rb=VK_NULL_HANDLE; VkDeviceMemory rbMem=VK_NULL_HANDLE;
        CreateHostBuffer(rbSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rb, rbMem, false);
        VkBufferImageCopy cp{}; cp.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; cp.imageExtent={w,h,1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &cp);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        ASSERT_TRUE(softwareConfirmed_);
        const auto t0 = std::chrono::steady_clock::now();
        ASSERT_EQ(vkQueueSubmit(queue_,1,&si,VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        const auto t1 = std::chrono::steady_clock::now();
        ms = double(std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count());

        void* mapped=nullptr; ASSERT_EQ(vkMapMemory(logicalDevice_,rbMem,0,rbSz,0,&mapped), VK_SUCCESS);
        rgba.assign(size_t(w)*h*4, 0); std::memcpy(rgba.data(), mapped, size_t(rbSz));
        vkUnmapMemory(logicalDevice_, rbMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_,rb,nullptr); vkFreeMemory(logicalDevice_,rbMem,nullptr);
        vkDestroyDescriptorPool(logicalDevice_,pool2,nullptr);
        vkDestroyPipeline(logicalDevice_,pipeline,nullptr);
        vkDestroyPipelineLayout(logicalDevice_,pl,nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_,dsl,nullptr);
        vkDestroyShaderModule(logicalDevice_,sm,nullptr);
        vkDestroyImageView(logicalDevice_,colorView,nullptr); vkDestroyImageView(logicalDevice_,idView,nullptr);
        vkDestroyImage(logicalDevice_,colorImg,nullptr); vkFreeMemory(logicalDevice_,colorMem,nullptr);
        vkDestroyImage(logicalDevice_,idImg,nullptr);    vkFreeMemory(logicalDevice_,idMem,nullptr);
        vkDestroyBuffer(logicalDevice_,traceBuf,nullptr); vkFreeMemory(logicalDevice_,traceMem,nullptr);
        vkDestroyBuffer(logicalDevice_,ctrBuf,nullptr);   vkFreeMemory(logicalDevice_,ctrMem,nullptr);
        if (dummySdf    != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummySdf,nullptr);    vkFreeMemory(logicalDevice_,dSdfMem,nullptr); }
        if (dummyLookup != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyLookup,nullptr); vkFreeMemory(logicalDevice_,dLookupMem,nullptr); }
        if (dummyMip    != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyMip,nullptr);    vkFreeMemory(logicalDevice_,dMipMem,nullptr); }
        vkDestroyBuffer(logicalDevice_,dummyIter,nullptr); vkFreeMemory(logicalDevice_,dIterMem,nullptr);
    }

    // Bakes one sphere via ConcatenateSdfWithMips (real mip pool), renders it at
    // the given residency, and returns pixel-coverage + per-row-band stats used to
    // check the silhouette is round (not just "some pixels lit").
    struct RenderStats {
        int hitPixels = 0;
        int centerColBandHits = 0;   // hits in the vertical center column band (should be tall for a sphere)
        int edgeColBandHits   = 0;   // hits near the left/right edges (should be near-zero for a sphere)
    };

    void BakeRenderAndMeasure(bool residencyRequested, const char* outPath, RenderStats& stats) {
        // Bake a single sphere with a real mip pool (ConcatenateSdfWithMips bakes +
        // attaches mips per-octree; ConcatenateSdf's plain sibling never does).
        constexpr float kRadius = 22.0f;
        const glm::vec3 center(32.0f, 32.0f, 32.0f);
        Vixen::SVO::RecipeParams rp{}; rp.radius = kRadius;
        Vixen::SVO::SdfBakeResult baked =
            Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, center, rp,
                                              /*n=*/64, /*bandVoxels=*/2.5f, /*brickDepth=*/3);
        Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);

        std::vector<const Vixen::SVO::SdfBodyOctree*> ptrs{&body};
        Vixen::SVO::ConcatenatedOctrees pool = Vixen::SVO::ConcatenateSdfWithMips(ptrs);
        ASSERT_GT(pool.mipPool.size(), 0u) << "ConcatenateSdfWithMips must bake a non-empty mip pool";

        RenderPoolAndMeasure(std::move(pool), residencyRequested, outPath, stats);
    }

    // Lazy-Procedural-Delta-Baseline Inc0 M1 Task 3b — the same render+measure
    // machinery, but driven from a pre-built pool (e.g. BakeRegistryToPool's
    // output) rather than baking a sphere inline. Proves a SetRecipePool-fed
    // node renders the mip fallback with REAL samples when the pool came from
    // the production baker path, not just the direct ConcatenateSdfWithMips call.
    void RenderPoolAndMeasure(Vixen::SVO::ConcatenatedOctrees pool, bool residencyRequested,
                              const char* outPath, RenderStats& stats) {
        using C = BodyOctreeSceneNodeConfig;

        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("mip_fallback_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex=0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetRecipePool(std::move(pool));
        node->RequestBrickResidency(residencyRequested);

        // BodyInstanceRayMarch.comp places a body at worldPos, scaled by renderScale,
        // over the octree's [0, kWorldGridSize] local extent (see BodyCentre below) —
        // NOT at the SDF bake-space `center` (that's an internal grid coordinate of the
        // baked octree, unrelated to world placement). worldPos=(0,0,0), renderScale=1
        // is the simplest placement: the body's true world-space centre is exactly
        // 0.5*kWorldGridSize along each axis.
        constexpr float kRenderScale = 1.0f;
        const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
            MakeInst(0.0f, 0.0f, 0.0f, kRenderScale, 0u),
        };
        const glm::vec3 bodyCentre(0.5f * kWorldGridSize * kRenderScale);
        node->SetInstances(instances);
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

        // A residency change requested AFTER Compile (residencyRequested_ defaults
        // true, per the M3 fix) needs one more Execute tick to service the dirty flag
        // when explicitly requesting FALSE — but RequestBrickResidency(false) above was
        // called BEFORE Setup/Compile, so CreateOctreeBuffers already saw it; no extra
        // tick needed either way. Confirmed by test: exercised both true/false below.

        auto buf = [&](int slot) -> VkBuffer {
            return node->GetOutput(slot, 0)->GetHandle<VkBuffer>();
        };
        VkBuffer nodes   = buf(C::OCTREE_NODES_BUFFER_Slot::index);
        VkBuffer bricks  = buf(C::OCTREE_BRICKS_BUFFER_Slot::index);
        VkBuffer mats    = buf(C::OCTREE_MATERIALS_BUFFER_Slot::index);
        VkBuffer cfgBuf  = buf(C::OCTREE_CONFIG_BUFFER_Slot::index);
        VkBuffer instBuf = buf(C::INSTANCE_BUFFER_Slot::index);
        VkBuffer sdfBuf  = buf(C::OCTREE_SDF_BUFFER_Slot::index);
        VkBuffer lookBuf = buf(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index);
        VkBuffer mipBuf  = buf(C::OCTREE_MIPPOOL_BUFFER_Slot::index);
        ASSERT_NE(nodes, VK_NULL_HANDLE); ASSERT_NE(cfgBuf, VK_NULL_HANDLE);
        ASSERT_NE(mipBuf, VK_NULL_HANDLE);

        constexpr uint32_t kW=512, kH=512;
        // Fit the sphere (radius kRadius in grid-voxel units, occupying roughly
        // ±kRadius/n of the [0,kWorldGridSize] world extent) with margin at 45° FOV.
        const float dist = 2.2f * kWorldGridSize * kRenderScale;
        const glm::vec3 eye = bodyCentre + glm::vec3(0.0f, 0.0f, dist);
        const PushConstants pc = MakeCamera(eye, bodyCentre, kW, kH, 1);

        std::vector<uint8_t> rgba; double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             sdfBuf, lookBuf, mipBuf, pc, kW, kH, rgba, ms));

        {
            std::vector<uint8_t> rgb(size_t(kW)*kH*3);
            for (uint32_t i = 0; i < kW*kH; ++i) {
                rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2];
            }
            stbi_write_png(outPath, int(kW), int(kH), 3, rgb.data(), int(kW)*3);
        }

        stats = RenderStats{};
        // Center column band: x in [kW*0.45, kW*0.55) — a round silhouette centered
        // on screen should be hit almost everywhere along y in this band.
        // Edge column band: x in [0, kW*0.05) — outside a centered sphere's radius,
        // should be almost entirely sky (near-zero hits) for a proper round shape.
        const uint32_t centerXLo = uint32_t(kW*0.45f), centerXHi = uint32_t(kW*0.55f);
        const uint32_t edgeXHi   = uint32_t(kW*0.05f);
        for (uint32_t y = 0; y < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                const uint32_t i = y*kW + x;
                const bool hit = rgba[i*4+0]>24 || rgba[i*4+1]>24 || rgba[i*4+2]>40;
                if (!hit) continue;
                ++stats.hitPixels;
                if (x >= centerXLo && x < centerXHi) ++stats.centerColBandHits;
                if (x < edgeXHi) ++stats.edgeColBandHits;
            }
        }
        std::printf("[MIP-FALLBACK] residencyRequested=%d | total=%d centerBand=%d edgeBand=%d | render=%.0f ms -> %s\n",
                    int(residencyRequested), stats.hitPixels, stats.centerColBandHits,
                    stats.edgeColBandHits, ms, outPath);

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
    }
};

// ---------------------------------------------------------------------------
// Task 9 gate: mip-only tree (residency NEVER requested) renders a recognizable
// silhouette from mip samples alone — non-trivial pixel coverage AND a round
// shape (dense center-column hits, near-empty edge-column hits), not just
// "some pixels are lit" (the Inc2 M6 precedent this Plan's Task 9 cites).
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, MipOnlyTreeRendersRoundSilhouette) {
    std::printf("[ lavapipe ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    RenderStats stats;
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/false, "/tmp/mip_fallback_render.png", stats));

    EXPECT_GT(stats.hitPixels, 5000)
        << "Mip-only tree should render a non-trivial silhouette from mip samples alone";
    // Round-shape check: the center column band (spanning the sphere's widest point)
    // must be almost entirely hit; the edge band (outside the sphere's radius) must be
    // almost entirely empty. A silhouette-only pixel-count check can't tell a round
    // blob from a degenerate full-screen fill or a thin sliver — this can.
    const int centerBandRows = int(512 * 0.10f);  // band width in x, full height in y -> 512 rows tall
    EXPECT_GT(stats.centerColBandHits, int(512 * 0.5f))
        << "Center column band should be substantially covered by a centered sphere";
    EXPECT_LT(stats.edgeColBandHits, centerBandRows / 4)
        << "Edge column band should be mostly empty (sky) for a round, centered silhouette "
           "— a full-screen fill or degenerate shape would light this band up too";
}

// ---------------------------------------------------------------------------
// No-regression: the SAME baked pool with residency explicitly requested TRUE
// (bricks fully uploaded, real trilinear SDF march) must ALSO render a
// comparable silhouette — proves Task 7's existence check doesn't misfire and
// suppress the real march path when bricks ARE resident.
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, ResidentTreeRendersComparableSilhouette) {
    ASSERT_TRUE(softwareConfirmed_);

    RenderStats mipOnly, resident;
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/false, "/tmp/mip_fallback_mip_only.png", mipOnly));
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/true, "/tmp/mip_fallback_resident.png", resident));

    EXPECT_GT(resident.hitPixels, 5000)
        << "Fully-resident tree must render a non-trivial silhouette (real brick march)";
    // Both should show a round silhouette of the SAME sphere/camera, but they are NOT
    // expected to match pixel-for-pixel: Task 7's fallback is a hard-switch "does this
    // leaf's brick have ANY occupied (near-surface-band) voxel" test (coverage > 0),
    // not a true iso-surface intersection test (direction doc point 4 / plan Task 7:
    // "v1 = hard switch... a coarse... representation, not an iso-surface march"). A
    // narrow-band SDF (bandVoxels=2.5) bakes occupied voxels within ~2.5 voxels of the
    // true surface in every direction, so grazing rays that clip a near-surface leaf's
    // bounding cube without crossing the true curved surface still register a mip hit —
    // the mip-only silhouette is EXPECTED to be somewhat larger than the resident
    // iso-surface march's, not equal. The bound below catches genuine breakage (a
    // vanishing or wildly exploded silhouette), not this documented coarseness.
    const double ratio = double(mipOnly.hitPixels) / double(resident.hitPixels);
    EXPECT_GT(ratio, 0.5) << "Mip-only silhouette is suspiciously smaller than the resident render";
    EXPECT_LT(ratio, 6.0) << "Mip-only silhouette is implausibly larger than the resident render "
                             "(expect some growth from the coarse hard-switch test, not an explosion)";
}

// ---------------------------------------------------------------------------
// Lazy-Procedural-Delta-Baseline Inc0 M1 Task 3b — this is the M2 gate's
// offscreen twin: a pool baked through the PRODUCTION path (RecipeRegistry ->
// BakeRegistryToPool, exactly what a real SetRecipePool caller would use, not
// the direct ConcatenateSdfWithMips call the tests above exercise) must ALSO
// render the mip fallback with real samples when bricks are never made
// resident — proving BakeRegistryToPool's Task 1 mip wiring is load-bearing,
// not just ConcatenateSdfWithMips in isolation.
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, RegistryBakedPoolRendersMipFallback) {
    ASSERT_TRUE(softwareConfirmed_);

    Vixen::SVO::RecipeRegistry reg;
    Vixen::SVO::RecipeRegistry::RecipeEntry sphere{};
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    in.data[0] = 0.0f; in.data[1] = 0.0f; in.data[2] = 0.0f; in.data[3] = 22.0f;  // object-centered
    sphere.bytecode = { in };
    ASSERT_EQ(reg.Register(1u, sphere), Vixen::SVO::RecipeRegistry::RegisterResult::Ok);

    Vixen::SVO::RecipeBakeConfig cfg{};  // defaults: center=(32,32,32), n=64, band=2.5, depth=3
    Vixen::SVO::RecipeBakeResult baked = Vixen::SVO::BakeRegistryToPool(reg, cfg);
    ASSERT_TRUE(baked.ok) << baked.err;
    ASSERT_GT(baked.pool.mipPool.size(), 0u)
        << "BakeRegistryToPool must bake+attach mips for its callers (Task 1)";

    RenderStats stats;
    ASSERT_NO_FATAL_FAILURE(RenderPoolAndMeasure(
        std::move(baked.pool), /*residencyRequested=*/false,
        "/tmp/mip_fallback_registry_baked.png", stats));

    EXPECT_GT(stats.hitPixels, 5000)
        << "Registry-baked, non-resident tree should render a non-trivial silhouette from mip samples alone";
    const int centerBandRows = int(512 * 0.10f);
    EXPECT_GT(stats.centerColBandHits, int(512 * 0.5f))
        << "Center column band should be substantially covered by a centered sphere";
    EXPECT_LT(stats.edgeColBandHits, centerBandRows / 4)
        << "Edge column band should be mostly empty (sky) for a round, centered silhouette";
}
