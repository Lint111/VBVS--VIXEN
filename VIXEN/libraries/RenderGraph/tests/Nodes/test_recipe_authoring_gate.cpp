/**
 * @file test_recipe_authoring_gate.cpp
 * @brief I4.2 — Live lavapipe CSG recipe render gate.
 *
 * Two tests:
 *   1. CsgSubtractRendersNonTrivial — bakes a Subtract(Box, Sphere) recipe where the
 *      sphere protrudes through the +z face of the box → verifies the CSG notch is
 *      visible (hitPixels > 3000) and saves /tmp/recipe_gate.png.
 *   2. DefaultSceneRegression — renders the standard 3-shell-octree scene (no recipe
 *      pool) to confirm the M2 SSBO binding-5 change didn't break the base path;
 *      saves /tmp/recipe_default_scene.png.
 *
 * Box SDF notes (SdfRecipeEval.cpp):
 *   sdBox(pos, halfExtents) — pos is raw voxel coords in [0, n].
 *   Box(36,36,36) occupies [0,36]^3, which INCLUDES the grid centre (32,32,32).
 *   Sphere(32,32,48, r=20) protrudes into the box from the +z face at z=36:
 *     sphere extends from z=28 to z=68 → 8 voxels inside the box below z=36.
 *     At z=36 the sphere cross-section has radius sqrt(20²−12²)=16 voxels.
 *
 * SAFETY — LAVAPIPE ONLY: identical contract to test_body_instance_raymarch_render.cpp.
 *
 * Run:
 *   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
 *   ./test_recipe_authoring_gate
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "TestVkValidation.h"

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

Vixen::SVO::BodyInstanceGpu MakeInst(float x, float y, float z, float scale, uint32_t oi,
                                      float r = 1.f, float g = 1.f, float b = 1.f) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0]=x; i.worldPos[1]=y; i.worldPos[2]=z;
    i.renderScale = scale; i.octreeIndex = oi;
    i.color[0]=r; i.color[1]=g; i.color[2]=b;
    return i;
}

glm::vec3 BodyCentre(const Vixen::SVO::BodyInstanceGpu& inst) {
    return glm::vec3(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]) +
           glm::vec3(0.5f * kWorldGridSize * inst.renderScale);
}

PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target,
                          uint32_t w, uint32_t h, int32_t instanceCount) {
    const glm::vec3 dir   = glm::normalize(target - eye);
    const glm::vec3 up0(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(dir, up0));
    const glm::vec3 up    = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos = eye;   pc.time = 0.0f;
    pc.cameraDir = dir;   pc.fov  = 45.0f;
    pc.cameraUp  = up;    pc.aspect = float(w) / float(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = instanceCount;
    return pc;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lavapipe render fixture (mirrors test_body_instance_raymarch_render.cpp).
// ---------------------------------------------------------------------------
class RecipeAuthoringGateTest : public ::testing::Test {
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

    static bool IsSoftware(const VkPhysicalDeviceProperties& p) {
        std::string n(p.deviceName); for (char& c : n) c = char(::tolower(c));
        return (n.find("llvmpipe") != std::string::npos ||
                n.find("lavapipe") != std::string::npos) &&
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    }

    void SetUp() override {
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_recipe_authoring_gate"; ai.apiVersion = VK_API_VERSION_1_3;
        const auto layers = EnabledValidationLayers();
        const char* exts[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledLayerCount   = uint32_t(layers.size());
        ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = exts;
        ASSERT_EQ(vkCreateInstance(&ci, nullptr, &instance_), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(PickSoftwareDevice());
        ASSERT_TRUE(softwareConfirmed_)
            << "Refusing: '" << selectedDeviceName_ << "' is not the software rasterizer.";
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
        ASSERT_GT(cnt, 0u) << "No Vulkan devices. Set VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json";
        std::vector<VkPhysicalDevice> devs(cnt);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, devs.data()), VK_SUCCESS);
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (IsSoftware(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; softwareConfirmed_ = true; return; }
        }
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(devs[0], &p);
        selectedDeviceName_ = p.deviceName;
    }

    void CreateLogicalDevice() {
        uint32_t qfCnt = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCnt);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCnt; ++i)
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
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
        ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
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

    void RenderToRgba(VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfg,
                      VkBuffer inst, VkBuffer sdf, VkBuffer lookup,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& rgba, double& ms) {
        ASSERT_TRUE(softwareConfirmed_);
        VkBuffer traceBuf=VK_NULL_HANDLE, ctrBuf=VK_NULL_HANDLE;
        VkDeviceMemory traceMem=VK_NULL_HANDLE, ctrMem=VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ctrBuf,   ctrMem,   true);
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE;
        if (sdf    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf=dummySdf; }
        if (lookup == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup=dummyLookup; }

        VkImage cImg=VK_NULL_HANDLE, iImg=VK_NULL_HANDLE;
        VkDeviceMemory cMem=VK_NULL_HANDLE, iMem=VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM,cImg,cMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R32_UINT,iImg,iMem));
        VkImageView cv = MakeView(cImg, VK_FORMAT_R8G8B8A8_UNORM);
        VkImageView iv = MakeView(iImg, VK_FORMAT_R32_UINT);

        const auto spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "SPIR-V missing: " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smc{}; smc.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize=spirv.size()*4; smc.pCode=spirv.data();
        VkShaderModule sm=VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_,&smc,nullptr,&sm), VK_SUCCESS);

        auto bindL = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{}; lb.binding=b; lb.descriptorType=t;
            lb.descriptorCount=1; lb.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; return lb;
        };
        const std::array<VkDescriptorSetLayoutBinding,11> bindings = {
            bindL(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // I3.2: SSBO
            bindL(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };
        VkDescriptorSetLayoutCreateInfo dslci{}; dslci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount=uint32_t(bindings.size()); dslci.pBindings=bindings.data();
        VkDescriptorSetLayout dsl=VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_,&dslci,nullptr,&dsl), VK_SUCCESS);

        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.size=sizeof(pc);
        VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount=1; plci.pSetLayouts=&dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
        VkPipelineLayout pl=VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_,&plci,nullptr,&pl), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{}; cpci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; cpci.stage.module=sm; cpci.stage.pName="main";
        cpci.layout=pl;
        VkPipeline pipeline=VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize,2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9},
        }};
        VkDescriptorPoolCreateInfo dpci{}; dpci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets=1; dpci.poolSizeCount=uint32_t(poolSizes.size()); dpci.pPoolSizes=poolSizes.data();
        VkDescriptorPool descPool=VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_,&dpci,nullptr,&descPool), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool=descPool; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsl;
        VkDescriptorSet ds=VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_,&dsai,&ds), VK_SUCCESS);

        VkDescriptorImageInfo colI{VK_NULL_HANDLE,cv,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idI{VK_NULL_HANDLE,iv,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nI{nodes,0,VK_WHOLE_SIZE}, brI{bricks,0,VK_WHOLE_SIZE},
            mI{mats,0,VK_WHOLE_SIZE}, trI{traceBuf,0,VK_WHOLE_SIZE}, cI{cfg,0,VK_WHOLE_SIZE},
            ctI{ctrBuf,0,VK_WHOLE_SIZE}, inI{inst,0,VK_WHOLE_SIZE},
            sdI{sdf,0,VK_WHOLE_SIZE}, lkI{lookup,0,VK_WHOLE_SIZE};

        auto wI2 = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=info; return w;
        };
        auto wB2 = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo=info; return w;
        };
        const std::array<VkWriteDescriptorSet,11> writes = {
            wI2(0,&colI), wB2(1,&nI), wB2(2,&brI), wB2(3,&mI), wB2(4,&trI),
            wB2(5,&cI), wB2(8,&ctI), wI2(9,&idI), wB2(10,&inI), wB2(11,&sdI), wB2(12,&lkI)
        };
        vkUpdateDescriptorSets(logicalDevice_, uint32_t(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{}; cbai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool=commandPool_; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=1;
        VkCommandBuffer cmd=VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_,&cbai,&cmd), VK_SUCCESS);
        VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        auto toGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b2{}; b2.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b2.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b2.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            b2.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b2.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b2.image=img; b2.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b2.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b2);
        };
        toGeneral(cImg); toGeneral(iImg);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w+7)/8, (h+7)/8, 1);

        VkImageMemoryBarrier toSrc{}; toSrc.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout=VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        toSrc.image=cImg; toSrc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        toSrc.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toSrc);

        const VkDeviceSize rbSz = VkDeviceSize(w)*h*4;
        VkBuffer rb=VK_NULL_HANDLE; VkDeviceMemory rbMem=VK_NULL_HANDLE;
        CreateHostBuffer(rbSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rb, rbMem, false);
        VkBufferImageCopy cp{}; cp.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; cp.imageExtent={w,h,1};
        vkCmdCopyImageToBuffer(cmd, cImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &cp);
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
        vkDestroyBuffer(logicalDevice_,rb,nullptr);       vkFreeMemory(logicalDevice_,rbMem,nullptr);
        vkDestroyDescriptorPool(logicalDevice_,descPool,nullptr);
        vkDestroyPipeline(logicalDevice_,pipeline,nullptr);
        vkDestroyPipelineLayout(logicalDevice_,pl,nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_,dsl,nullptr);
        vkDestroyShaderModule(logicalDevice_,sm,nullptr);
        vkDestroyImageView(logicalDevice_,cv,nullptr); vkDestroyImageView(logicalDevice_,iv,nullptr);
        vkDestroyImage(logicalDevice_,cImg,nullptr);    vkFreeMemory(logicalDevice_,cMem,nullptr);
        vkDestroyImage(logicalDevice_,iImg,nullptr);    vkFreeMemory(logicalDevice_,iMem,nullptr);
        vkDestroyBuffer(logicalDevice_,traceBuf,nullptr); vkFreeMemory(logicalDevice_,traceMem,nullptr);
        vkDestroyBuffer(logicalDevice_,ctrBuf,nullptr);   vkFreeMemory(logicalDevice_,ctrMem,nullptr);
        if (dummySdf    != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummySdf,nullptr);    vkFreeMemory(logicalDevice_,dSdfMem,nullptr); }
        if (dummyLookup != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyLookup,nullptr); vkFreeMemory(logicalDevice_,dLookupMem,nullptr); }
    }

    // Run a node lifecycle (Setup → Compile → Execute → read outputs → call fn → Cleanup).
    template<typename Fn>
    void RunNode(BodyOctreeSceneNode* node, std::unique_ptr<NodeInstance>& nodeBase,
                 const std::vector<Vixen::SVO::BodyInstanceGpu>& instances, Fn&& fn) {
        using C = BodyOctreeSceneNodeConfig;
        Resource devRes; SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;  uint32_t fi = 0; SetHandleVal<uint32_t>(frRes, fi);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);
        node->SetInstances(instances);
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        fi = 0; SetHandleVal<uint32_t>(frRes, fi);
        ASSERT_NO_THROW(node->Execute());

        VkBuffer nodes   = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index,   0)->GetHandle<VkBuffer>();
        VkBuffer bricks  = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index,  0)->GetHandle<VkBuffer>();
        VkBuffer mats    = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index,0)->GetHandle<VkBuffer>();
        VkBuffer cfgBuf  = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index,  0)->GetHandle<VkBuffer>();
        VkBuffer instBuf = node->GetOutput(C::INSTANCE_BUFFER_Slot::index,       0)->GetHandle<VkBuffer>();
        VkBuffer sdfBuf  = node->GetOutput(C::OCTREE_SDF_BUFFER_Slot::index,     0)->GetHandle<VkBuffer>();
        VkBuffer lookBuf = node->GetOutput(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index,0)->GetHandle<VkBuffer>();

        fn(nodes, bricks, mats, cfgBuf, instBuf, sdfBuf, lookBuf);

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
        nodeBase.reset();
    }
};

// ---------------------------------------------------------------------------
// I4.2a — CSG Subtract(Box, Sphere) renders a non-trivial solid.
//
// The sphere protrudes through the +z face of the box, cutting a circular notch.
// The test asserts the result has MORE than 3000 lit pixels (far more than zero).
// The threshold is deliberately conservative to be CPU/lavapipe stable.
// ---------------------------------------------------------------------------
TEST_F(RecipeAuthoringGateTest, CsgSubtractRendersNonTrivial) {
    std::printf("[ lavapipe ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    using SdfI  = Vixen::SVO::Recipe::SdfInstruction;
    using SdfOp = Vixen::SVO::Recipe::SdfOpCode;

    // Box(halfExtents=36,36,36): occupies [0,36]^3 in the 64^3 grid.
    // Grid centre (32,32,32) is inside the box (32<36 on all axes).
    SdfI boxInst{}; boxInst.opCode = uint8_t(SdfOp::Box);
    boxInst.data[0] = 36.0f; boxInst.data[1] = 36.0f; boxInst.data[2] = 36.0f;

    // Sphere(centre=(32,32,48), r=20): protrudes into the box from the +z face at z=36.
    //   sphere extends from z=28 to z=68 → 8 voxels of overlap below z=36.
    //   at z=36 the intersection circle has radius sqrt(400-144)=16 voxels.
    SdfI sphInst{}; sphInst.opCode = uint8_t(SdfOp::Sphere);
    sphInst.data[0] = 32.0f; sphInst.data[1] = 32.0f; sphInst.data[2] = 48.0f;
    sphInst.data[3] = 20.0f;

    // Subtract pops 2 (box bottom, sphere top), pushes 1 (box − sphere).
    SdfI subInst{}; subInst.opCode = uint8_t(SdfOp::Subtract);

    Vixen::SVO::RecipeRegistry reg;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { boxInst, sphInst, subInst };
    ASSERT_EQ(reg.Register(100u, entry), Vixen::SVO::RecipeRegistry::RegisterResult::Ok)
        << "Recipe registration failed";

    Vixen::SVO::RecipeBakeConfig bakeCfg{};
    auto bakeResult = Vixen::SVO::BakeRegistryToPool(reg, bakeCfg);
    ASSERT_TRUE(bakeResult.ok) << bakeResult.err;
    ASSERT_EQ(bakeResult.pool.count, 1u);

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("csg_gate");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);
    node->SetRecipePool(std::move(bakeResult.pool));

    // Single body at the world origin, renderScale 0.20 (radius 1.0 world unit).
    constexpr float kRS = 0.20f;
    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInst(0.0f, 0.0f, 0.0f, kRS, 0u),
    };

    constexpr uint32_t kW = 512, kH = 512;

    ASSERT_NO_FATAL_FAILURE(RunNode(node, nodeBase, instances,
        [&](VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfgBuf,
            VkBuffer instBuf, VkBuffer sdfBuf, VkBuffer lookBuf) {

        ASSERT_NE(nodes,   VK_NULL_HANDLE);
        ASSERT_NE(cfgBuf,  VK_NULL_HANDLE);
        ASSERT_NE(instBuf, VK_NULL_HANDLE);

        const glm::vec3 centre = BodyCentre(instances[0]);  // (1,1,1)
        const float     R      = 0.5f * kWorldGridSize * kRS;  // 1.0
        // Camera from +z side, slight tilt so depth shading visible.
        const glm::vec3 eye = centre + glm::normalize(glm::vec3(0.1f, 0.2f, 1.0f)) * (R * 6.0f);
        const PushConstants pc = MakeCamera(eye, centre, kW, kH, int32_t(instances.size()));

        std::vector<uint8_t> rgba; double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             sdfBuf, lookBuf, pc, kW, kH, rgba, ms));

        // Write PNG.
        const char* outPath = "/tmp/recipe_gate.png";
        {
            std::vector<uint8_t> rgb(size_t(kW)*kH*3);
            for (uint32_t i = 0; i < kW*kH; ++i) {
                rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2];
            }
            stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW*3);
        }

        int hitPixels = 0;
        for (uint32_t i = 0; i < kW*kH; ++i)
            if (rgba[i*4+0]>24 || rgba[i*4+1]>24 || rgba[i*4+2]>40) ++hitPixels;

        std::printf("[CSG] Subtract(Box,Sphere) | hitPixels=%d | render=%.0f ms | -> %s\n",
                    hitPixels, ms, outPath);

        EXPECT_GT(hitPixels, 3000)
            << "Expected box-minus-sphere body to fill >3000 pixels. PNG: " << outPath;
    }));
}

// ---------------------------------------------------------------------------
// I4.2b — Default 3-shell scene regression (no recipe pool).
//
// Confirms that the M2 SSBO change (binding 5 = STORAGE_BUFFER) did NOT break
// the standard BodyOctreeSceneNode path (hardcoded shell octrees).
// Saves /tmp/recipe_default_scene.png.
// ---------------------------------------------------------------------------
TEST_F(RecipeAuthoringGateTest, DefaultSceneRegression) {
    std::printf("[ lavapipe ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("default_scene");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);
    // No SetRecipePool — uses the standard 3-shell-octree path.

    constexpr float kRS = 0.10f;
    const float R   = 0.5f * kWorldGridSize * kRS;
    const float sep = R * 3.0f;
    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInst(-sep, 0.0f, 0.0f, kRS, 0u, 1.0f,1.0f,1.0f),
        MakeInst( 0.0f, 0.0f, 0.0f, kRS, 1u, 1.0f,1.0f,1.0f),
        MakeInst( sep, 0.0f, 0.0f, kRS, 2u, 1.0f,1.0f,1.0f),
    };

    constexpr uint32_t kW = 768, kH = 256;

    ASSERT_NO_FATAL_FAILURE(RunNode(node, nodeBase, instances,
        [&](VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfgBuf,
            VkBuffer instBuf, VkBuffer sdfBuf, VkBuffer lookBuf) {

        // OctreeConfig must still be 432 bytes (shader's std140 ArrayStride).
        ASSERT_EQ(sizeof(Vixen::SVO::OctreeConfig), 432u)
            << "OctreeConfig size mismatch (std140 regression)";

        const glm::vec3 c0 = BodyCentre(instances[0]);
        const glm::vec3 c2 = BodyCentre(instances[2]);
        const glm::vec3 centre = 0.5f * (c0 + c2);
        const float spanX = std::abs(c2.x - c0.x) + 2.0f * R;
        const float dist  = (0.5f * spanX) / (std::tan(glm::radians(22.5f)) * float(kW)/float(kH)) * 1.35f;
        const glm::vec3 eye = centre + glm::vec3(0.0f, 0.15f, 1.0f) * dist;
        const PushConstants pc = MakeCamera(eye, centre, kW, kH, int32_t(instances.size()));

        std::vector<uint8_t> rgba; double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             VK_NULL_HANDLE, VK_NULL_HANDLE,
                                             pc, kW, kH, rgba, ms));

        const char* outPath = "/tmp/recipe_default_scene.png";
        {
            std::vector<uint8_t> rgb(size_t(kW)*kH*3);
            for (uint32_t i = 0; i < kW*kH; ++i) {
                rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2];
            }
            stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW*3);
        }

        int hitPixels = 0;
        for (uint32_t i = 0; i < kW*kH; ++i)
            if (rgba[i*4+0]>24 || rgba[i*4+1]>24 || rgba[i*4+2]>40) ++hitPixels;

        std::printf("[DEFAULT] 3-body default scene | hitPixels=%d | render=%.0f ms | -> %s\n",
                    hitPixels, ms, outPath);

        EXPECT_GT(hitPixels, 500)
            << "Default 3-shell scene produced no hits. SSBO binding-5 regression? PNG: " << outPath;
    }));
}
