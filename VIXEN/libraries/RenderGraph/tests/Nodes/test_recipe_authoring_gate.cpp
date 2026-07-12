/**
 * @file test_recipe_authoring_gate.cpp
 * @brief I4.2 — Live CSG recipe render gate.
 *
 * Two tests:
 *   1. CsgSubtractRendersNonTrivial — bakes a Subtract(Box, Sphere) recipe where the
 *      sphere (local centre=(0,0,0), r=28) sits at the box's local z-midpoint, spanning
 *      the full [-26,26] box depth → through-tunnel → ablation delta (boxOnly-csg>500).
 *   2. DefaultSceneRegression — renders the standard 3-shell-octree scene (no recipe
 *      pool) to confirm the M2 SSBO binding-5 change didn't break the base path;
 *      saves /tmp/recipe_default_scene.png.
 *
 * Box SDF notes (SdfRecipeEval.cpp / SdfCore_Box):
 *   sdBox(abs(pos), halfExtents) — interior where all |pos.i| < halfExtent. Box has no
 *   position field, so it is always centered at the eval point passed to it.
 *
 * Inc2a re-derivation (BakeRecipeInstructionsToSdfWorld now applies `center`, p - center,
 * before evalRecipe -- matching evalSdf's convention): both primitives are authored
 * OBJECT-CENTERED (local origin) instead of the old raw-grid-absolute convention, and
 * RecipeBakeConfig's default center=(32,32,32) places them in the grid at bake time.
 *   Box(26,26,26): local-centered box spans local [-26,26] -> grid [6,58] once centered,
 *     safely inside the visible [0,64) grid (6-voxel margin to each wall). The old
 *     halfExtents=36 relied on the box sitting UNCENTERED at raw grid [0,36] pre-fix --
 *     post-fix that same value would center the box at grid-32 and push its surface to
 *     grid [-4,68], entirely OUTSIDE [0,64), baking zero voxels.
 *   Sphere(local centre=(0,0,0), r=28): centred at the box's local z-midpoint (0, since the
 *     box is symmetric about local origin); spans local z ∈ [-28,28], which COVERS the full
 *     [-26,26] box z-depth → THROUGH-TUNNEL (not a depression), hole radius at the box's
 *     z=26 face ≈ 10.4 voxels (sqrt(28²-26²)). Rays through the tunnel find no subtract
 *     surface within the baked domain → black pixels.
 *
 * DEVICE SELECTION: identical contract to test_body_instance_raymarch_render.cpp —
 * via VixenSelectWslGpuIcd(), a real discrete/integrated GPU is PREFERRED, with software
 * (lavapipe/llvmpipe) or Dozen used only as a fallback when no real GPU is visible.
 *
 * Run: ./test_recipe_authoring_gate
 *   (set VK_ICD_FILENAMES explicitly to force a specific ICD, e.g. for comparison.)
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
// Render fixture (mirrors test_body_instance_raymarch_render.cpp).
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

    // Real discrete/integrated GPUs are now PREFERRED; software/Dozen is only a
    // fallback when no real GPU is visible.
    static bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
        return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    static bool IsSoftware(const VkPhysicalDeviceProperties& p) {
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
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";
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
        // Prefer a real discrete/integrated GPU; fall back to software/Dozen only
        // when no real GPU is visible.
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (IsRealGpu(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; softwareConfirmed_ = true; return; }
        }
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
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE, dummyMip=VK_NULL_HANDLE, dummyIter=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE, dMipMem=VK_NULL_HANDLE, dIterMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyIter,dIterMem,true);  // Inc1 M4b binding 14
        if (sdf    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf=dummySdf; }
        if (lookup == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup=dummyLookup; }
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyMip,dMipMem,true);

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
        const std::array<VkDescriptorSetLayoutBinding,13> bindings = {
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
            bindL(13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc1 M4b: per-instance iteration debug
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
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11},
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
            sdI{sdf,0,VK_WHOLE_SIZE}, lkI{lookup,0,VK_WHOLE_SIZE}, mpI{dummyMip,0,VK_WHOLE_SIZE},
            itI{dummyIter,0,VK_WHOLE_SIZE};

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
        const std::array<VkWriteDescriptorSet,13> writes = {
            wI2(0,&colI), wB2(1,&nI), wB2(2,&brI), wB2(3,&mI), wB2(4,&trI),
            wB2(5,&cI), wB2(8,&ctI), wI2(9,&idI), wB2(10,&inI), wB2(11,&sdI), wB2(12,&lkI), wB2(13,&mpI),
            wB2(14,&itI)  // Inc1 M4b: per-instance iteration debug
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
        vkDestroyBuffer(logicalDevice_,dummyMip,nullptr); vkFreeMemory(logicalDevice_,dMipMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyIter,nullptr); vkFreeMemory(logicalDevice_,dIterMem,nullptr);
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
// I4.2a — CSG Subtract(Box, Sphere) ablation gate.
//
// Renders the SAME body twice with an identical camera:
//   Pass A — Box only          → boxOnlyPx
//   Pass B — Subtract(Box,Sphere) → csgPx
//
// A no-op Subtract would leave csgPx ≈ boxOnlyPx (delta ≈ 0) and the delta
// assert fails.  The real subtract punches a through-tunnel (hole radius
// ≈12.6 voxels at the z=36 face) that removes a large visible chunk.
// ---------------------------------------------------------------------------
TEST_F(RecipeAuthoringGateTest, CsgSubtractRendersNonTrivial) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    using SdfI  = Vixen::SVO::Recipe::SdfInstruction;
    using SdfOp = Vixen::SVO::Recipe::SdfOpCode;

    // Inc2a re-derivation: BakeRecipeInstructionsToSdfWorld now applies `center`
    // (p - center) before evalRecipe, so both Box and Sphere are authored OBJECT-CENTERED
    // (relative to local origin) instead of the old raw-grid-absolute convention. Box has no
    // position field (SdfCore_Box uses abs(pos)) so it is always centered at local origin;
    // halfExtents=26 keeps its surface ([-26,26] -> grid [6,58] once RecipeBakeConfig's default
    // center=(32,32,32) is applied) safely inside the visible [0,64) grid with a 6-voxel margin
    // (mirrors the kSdfRadius=26 margin convention used elsewhere, e.g. BodyOctreeSceneNode.cpp).
    // (Old halfExtents=36 pre-fix relied on the box sitting UNCENTERED in the positive octant
    // [0,36] -- post-fix that same 36 would center the box at grid-32 and push its surface to
    // grid [-4,68], entirely OUTSIDE [0,64), baking zero voxels -- exactly the fresh boxOnlyPx=0
    // failure this migration fixes.)
    SdfI boxInst{}; boxInst.opCode = uint8_t(SdfOp::Box);
    boxInst.data[0] = 26.0f; boxInst.data[1] = 26.0f; boxInst.data[2] = 26.0f;

    // Sphere(local centre=(0,0,0), r=28): Sphere's data[0..2] is its OWN local center offset,
    // applied on top of the already-centered eval point -- so it must also be authored
    // object-centered (local, relative to the box's local origin), not at the old raw-grid
    // absolute (32,32,18). The box's local z-midpoint is 0 (box spans [-26,26], symmetric about
    // local origin), so a sphere centered at local (0,0,0) already sits at the z-midpoint.
    //   Sphere spans z ∈ [-28,28], which COVERS the full [-26,26] box z-depth → THROUGH-TUNNEL.
    //   Hole radius at the box's z=26 face: sqrt(28²-26²) = sqrt(108) ≈ 10.4 voxels.
    //   Rays through the hole see no surface within the baking domain → black pixels.
    SdfI sphInst{}; sphInst.opCode = uint8_t(SdfOp::Sphere);
    sphInst.data[0] = 0.0f; sphInst.data[1] = 0.0f; sphInst.data[2] = 0.0f;
    sphInst.data[3] = 28.0f;

    SdfI subInst{}; subInst.opCode = uint8_t(SdfOp::Subtract);

    Vixen::SVO::RecipeBakeConfig bakeCfg{};

    // Shared instance + camera parameters.
    constexpr float kRS = 0.20f;
    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInst(0.0f, 0.0f, 0.0f, kRS, 0u),
    };
    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 centre = BodyCentre(instances[0]);  // (1,1,1)
    const float     R      = 0.5f * kWorldGridSize * kRS;  // 1.0
    // Camera from +z, slight tilt — same for BOTH passes so delta is recipe-only.
    const glm::vec3 eye = centre + glm::normalize(glm::vec3(0.1f, 0.2f, 1.0f)) * (R * 6.0f);
    const PushConstants sharedPc = MakeCamera(eye, centre, kW, kH, int32_t(instances.size()));

    // --- Pass A: Box only (ablation baseline) ---
    int boxOnlyPx = 0;
    {
        Vixen::SVO::RecipeRegistry regBox;
        Vixen::SVO::RecipeRegistry::RecipeEntry eBox{};
        eBox.bytecode = { boxInst };
        ASSERT_EQ(regBox.Register(100u, eBox), Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
        auto boxResult = Vixen::SVO::BakeRegistryToPool(regBox, bakeCfg);
        ASSERT_TRUE(boxResult.ok) << boxResult.err;

        BodyOctreeSceneNodeType nt("BodyOctreeScene");
        auto nb = nt.CreateInstance("box_ablation");
        auto* nd = dynamic_cast<BodyOctreeSceneNode*>(nb.get());
        ASSERT_NE(nd, nullptr);
        nd->SetRecipePool(std::move(boxResult.pool));
        // Lazy-Procedural-Delta-Baseline Inc0 M1: pin eager residency so this test's
        // pixel gates (ablation-delta) keep exercising the full brick march once
        // mip-capable pools default to lazy (a later milestone) — currently a
        // no-op (default is eager).
        nd->RequestBrickResidency(true);

        ASSERT_NO_FATAL_FAILURE(RunNode(nd, nb, instances,
            [&](VkBuffer ns, VkBuffer br, VkBuffer mt, VkBuffer cfg,
                VkBuffer inst, VkBuffer sdf, VkBuffer lk) {
            std::vector<uint8_t> rgba; double ms2 = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderToRgba(ns,br,mt,cfg,inst,sdf,lk,sharedPc,kW,kH,rgba,ms2));
            for (uint32_t i = 0; i < kW*kH; ++i)
                if (rgba[i*4+0]>24 || rgba[i*4+1]>24 || rgba[i*4+2]>40) ++boxOnlyPx;
            std::printf("[CSG/ablation] Box only | px=%d | %.0f ms\n", boxOnlyPx, ms2);
        }));
    }
    ASSERT_GT(boxOnlyPx, 1000) << "Box-only baseline rendered too few pixels to be a useful ablation";

    // --- Pass B: Subtract(Box, Sphere) ---
    int csgPx = 0;
    {
        Vixen::SVO::RecipeRegistry regCsg;
        Vixen::SVO::RecipeRegistry::RecipeEntry eCsg{};
        eCsg.bytecode = { boxInst, sphInst, subInst };
        ASSERT_EQ(regCsg.Register(100u, eCsg), Vixen::SVO::RecipeRegistry::RegisterResult::Ok);
        auto csgResult = Vixen::SVO::BakeRegistryToPool(regCsg, bakeCfg);
        ASSERT_TRUE(csgResult.ok) << csgResult.err;
        ASSERT_EQ(csgResult.pool.count, 1u);

        BodyOctreeSceneNodeType nt("BodyOctreeScene");
        auto nb = nt.CreateInstance("csg_gate");
        auto* nd = dynamic_cast<BodyOctreeSceneNode*>(nb.get());
        ASSERT_NE(nd, nullptr);
        nd->SetRecipePool(std::move(csgResult.pool));
        // Lazy-Procedural-Delta-Baseline Inc0 M1: pin eager residency so this test's
        // pixel gates (CSG cut-through) keep exercising the full brick march once
        // mip-capable pools default to lazy (a later milestone) — currently a
        // no-op (default is eager).
        nd->RequestBrickResidency(true);

        ASSERT_NO_FATAL_FAILURE(RunNode(nd, nb, instances,
            [&](VkBuffer ns, VkBuffer br, VkBuffer mt, VkBuffer cfg,
                VkBuffer inst, VkBuffer sdf, VkBuffer lk) {
            ASSERT_NE(ns,   VK_NULL_HANDLE);
            ASSERT_NE(cfg,  VK_NULL_HANDLE);
            ASSERT_NE(inst, VK_NULL_HANDLE);

            std::vector<uint8_t> rgba; double ms = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderToRgba(ns,br,mt,cfg,inst,sdf,lk,sharedPc,kW,kH,rgba,ms));

            const char* outPath = "/tmp/recipe_gate.png";
            {
                std::vector<uint8_t> rgb(size_t(kW)*kH*3);
                for (uint32_t i = 0; i < kW*kH; ++i) {
                    rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2];
                }
                stbi_write_png(outPath, kW, kH, 3, rgb.data(), kW*3);
            }

            for (uint32_t i = 0; i < kW*kH; ++i)
                if (rgba[i*4+0]>24 || rgba[i*4+1]>24 || rgba[i*4+2]>40) ++csgPx;

            std::printf("[CSG] Subtract(Box,Sphere) | px=%d | render=%.0f ms | -> %s\n",
                        csgPx, ms, outPath);
        }));
    }

    std::printf("[CSG/delta] boxOnly=%d  csg=%d  delta=%d\n", boxOnlyPx, csgPx, boxOnlyPx - csgPx);

    EXPECT_GT(csgPx, 3000)
        << "CSG body rendered too few pixels (degenerate solid?)";
    // Ablation: real Subtract punches a through-tunnel (≈12.6-voxel-radius hole).
    // A no-op Subtract → delta ≈ 0 → fails. Target: delta > 500.
    EXPECT_GT(boxOnlyPx - csgPx, 500)
        << "Subtract carved less than 500 pixels from the box — Subtract may be a no-op. "
        << "boxOnly=" << boxOnlyPx << " csg=" << csgPx;
}

// ---------------------------------------------------------------------------
// I4.2b — Default 3-shell scene regression (no recipe pool).
//
// Confirms that the M2 SSBO change (binding 5 = STORAGE_BUFFER) did NOT break
// the standard BodyOctreeSceneNode path (hardcoded shell octrees).
// Saves /tmp/recipe_default_scene.png.
// ---------------------------------------------------------------------------
TEST_F(RecipeAuthoringGateTest, DefaultSceneRegression) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
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
