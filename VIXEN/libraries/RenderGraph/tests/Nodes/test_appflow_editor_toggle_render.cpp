/**
 * @file test_appflow_editor_toggle_render.cpp
 * @brief AppFlow Inc-2 M4 — the authoritative GPU render-gate: a ToggleLayer dispatched through
 * AppFlowRuntime re-flattens + re-bakes + re-renders the golden document, and Undo() restores
 * the render byte-for-byte.
 *
 * Reuses test_editor_document_render.cpp's Vulkan bring-up (VixenSelectWslGpuIcd + software
 * device pick), flatten->bake->render path (FlattenAndBake/RenderPool), and the golden
 * sample_tri_layer.vxd asset + camera framing the cylinder bore. The only new thing here is
 * that the enabled mask driving the flatten comes from AppFlowRuntime::Layers(), and the
 * toggle/undo are dispatched through AppFlowRuntime::ToggleLayer()/Undo() rather than a raw
 * override vector — proving the whole Inc-2 seam (LayerController -> ActionStack ->
 * EditorDocumentModel's mask param -> FlattenVoxelDocument -> render) end to end on GPU.
 *
 * Output: /tmp/appflow_toggle_{initial,toggled,undone}.png (512x512 RGB8).
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/RecipeSimd.g.hpp"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include "AppFlowRuntime.h"

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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

#ifndef VXD_GOLDEN_PATH
#error "VXD_GOLDEN_PATH (path to sample_tri_layer.vxd) must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;
using Vixen::AppFlow::AppFlowRuntime;

namespace {

// The "cut" layer (Cylinder Subtract) is index 2 in the golden doc — same index
// test_editor_document_render.cpp's ablation test disables to punch the bore-column
// pixel difference.
constexpr uint32_t kCutLayerIndex = 2;

// Baked-perf-pipeline M2 / KI-034: SceneBindings.glsl's real PushConstants struct is
// 96 bytes (debugTargetPixel + accumFrameCount added by bad30727, well before this M2's
// own work; std430 rounds the push-constant block up to a 16-byte multiple, so SPIR-V
// reflection reports 96, not 92). This is the 8th of KI-034's 8 named files.
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float raySizeCoef; float raySizeBias; int32_t instanceCount;
    int32_t _pad0;  // std430 forces ivec2 to 8-byte alignment (real gap at offset [76,80))
    glm::ivec2 debugTargetPixel = glm::ivec2(-1, -1);  // Inc1 M4b (bytes 80-87); (-1,-1) disables
    uint32_t   accumFrameCount = 1u;                    // Sampled Lighting Inc2 M2 (bytes 88-91)
    uint32_t   _pad1 = 0u;  // std430 push-constant block rounds up to a 16-byte multiple
};
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes");

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0); f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.seekg(0); f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
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
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = instanceCount;
    return pc;
}

// Flattens the golden document with a per-layer enabled override built from `mask` (bit i ==
// layer i enabled) into a baked single-recipe ConcatenatedOctrees pool. Mirrors
// EditorDocumentModel::FlattenToRecipeEntry's mask-driven seam (Inc-2 Task 5).
Vixen::SVO::RecipeBakeResult FlattenAndBake(const Yeroket::Sdf::Generated::VoxelDocumentView& view,
                                             uint32_t mask,
                                             std::vector<uint8_t>& outBlob) {
    std::vector<uint8_t> enabledOverride(view.header.layerCount);
    for (uint32_t i = 0; i < view.header.layerCount; ++i) {
        enabledOverride[i] = static_cast<uint8_t>((mask >> i) & 1u);
    }

    std::string err;
    const bool flattenOk = Vixen::SVO::FlattenVoxelDocument(view, &enabledOverride, outBlob, err);
    EXPECT_TRUE(flattenOk) << err;

    Yeroket::Sdf::Generated::RecipeContainerView rv{};
    const bool readOk = Yeroket::Sdf::Generated::ReadRecipeContainer(outBlob.data(), outBlob.size(), rv);
    EXPECT_TRUE(readOk);

    Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
    entry.bytecode.assign(rv.instructions, rv.instructions + rv.header.instructionCount);
    entry.bakeResolution = rv.header.bakeResolution;
    entry.bandVoxels     = rv.header.bandVoxels;
    entry.brickDepth      = rv.header.brickDepth;

    Vixen::SVO::RecipeRegistry reg;
    const auto regResult = reg.Register(1u, entry);
    EXPECT_EQ(regResult, Vixen::SVO::RecipeRegistry::RegisterResult::Ok);

    Vixen::SVO::RecipeBakeConfig bakeCfg{};  // defaults: n=64, band=2.5, depth=3
    return Vixen::SVO::BakeRegistryToPool(reg, bakeCfg);
}

}  // namespace

// ---------------------------------------------------------------------------
// Minimal Vulkan fixture — identical structure to test_editor_document_render.cpp.
// ---------------------------------------------------------------------------
class AppFlowEditorToggleRenderTest : public ::testing::Test {
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

    // Real discrete/integrated GPUs are preferred; software/Dozen is only a fallback
    // when no real GPU is visible (the software-only gate was a lavapipe-era artifact).
    static bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
        return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

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
        // 0ew: this render-smoke suite intentionally supports lavapipe; make that software path
        // explicit. The parity precedent remains real-GPU-only via IsRealGpu in
        // test_recipe_glsl_numerical_parity.cpp.
#if defined(__linux__)
        ::setenv("VIXEN_ALLOW_SOFTWARE_VULKAN", "1", /*overwrite=*/1);
#endif
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_appflow_editor_toggle_render"; ai.apiVersion = VK_API_VERSION_1_3;
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
            if (IsRealGpu(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; softwareConfirmed_ = true; return; }
        }
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

    // Render using the real BodyInstanceRayMarch shader (binding 5 = SSBO, I3.2).
    void RenderToRgba(VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfg,
                      VkBuffer inst, VkBuffer sdf, VkBuffer lookup,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& rgba, double& ms) {
        ASSERT_TRUE(softwareConfirmed_);
        // Baked-perf-pipeline M2: RayTraceBuffer (binding 4) is real, non-placeholder -- see
        // test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation.
        constexpr VkDeviceSize kRayTraceBufferSize = 16 /*header*/ + 256 /*slots*/ * (16 + 64 * 48) /*TRACE_RAY_SIZE*/;
        VkBuffer traceBuf=VK_NULL_HANDLE, ctrBuf=VK_NULL_HANDLE;
        VkDeviceMemory traceMem=VK_NULL_HANDLE, ctrMem=VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ctrBuf, ctrMem, true);
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE, dummyMip=VK_NULL_HANDLE, dummyIter=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE, dMipMem=VK_NULL_HANDLE, dIterMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyIter,dIterMem,true);  // Inc1 M4b binding 14
        if (sdf    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf = dummySdf; }
        if (lookup == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup = dummyLookup; }
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyMip,dMipMem,true);
        // Baked-perf-pipeline M2: bindings 15/18 (TierRefTableBuffer/HitRecordBuffer) placeholders
        // -- see test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation.
        VkBuffer dummyTierRef=VK_NULL_HANDLE, dummyHitRecord=VK_NULL_HANDLE;
        VkDeviceMemory dTierRefMem=VK_NULL_HANDLE, dHitRecordMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyTierRef,dTierRefMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyHitRecord,dHitRecordMem,true);
        // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer (binding 35) placeholder.
        VkBuffer dummySkipMask=VK_NULL_HANDLE;
        VkDeviceMemory dSkipMaskMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySkipMask,dSkipMaskMem,true);

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
        const std::array<VkDescriptorSetLayoutBinding,16> bindings = {
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
            bindL(15,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // TierRefTableBuffer (placeholder)
            bindL(18,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // HitRecordBuffer (placeholder)
            bindL(35,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer
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
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 14},
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
            sdfI{sdf,0,VK_WHOLE_SIZE}, lookupI{lookup,0,VK_WHOLE_SIZE}, iterI{dummyIter,0,VK_WHOLE_SIZE}, mipI{dummyMip,0,VK_WHOLE_SIZE},
            tierRefI{dummyTierRef,0,VK_WHOLE_SIZE}, hitRecordI{dummyHitRecord,0,VK_WHOLE_SIZE},
            skipMaskI{dummySkipMask,0,VK_WHOLE_SIZE};

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
        const std::array<VkWriteDescriptorSet,16> writes = {
            wI(0,&colImg), wB(1,&nodesI), wB(2,&bricksI), wB(3,&matsI), wB(4,&traceI),
            wB(5,&cfgI), wB(8,&ctrI), wI(9,&idImgI), wB(10,&instI), wB(11,&sdfI), wB(12,&lookupI), wB(13,&mipI),
            wB(14,&iterI),  // Inc1 M4b: per-instance iteration debug
            wB(15,&tierRefI), wB(18,&hitRecordI),
            wB(35,&skipMaskI),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1
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
        vkDestroyBuffer(logicalDevice_,dummyMip,nullptr); vkFreeMemory(logicalDevice_,dMipMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyIter,nullptr); vkFreeMemory(logicalDevice_,dIterMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyTierRef,nullptr);   vkFreeMemory(logicalDevice_,dTierRefMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyHitRecord,nullptr); vkFreeMemory(logicalDevice_,dHitRecordMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummySkipMask,nullptr);  vkFreeMemory(logicalDevice_,dSkipMaskMem,nullptr);
    }

    // Bakes `pool` into a BodyOctreeSceneNode, renders one instance (octreeIndex=0,
    // renderScale=5.0, worldPos=(0,0,0)) with the given camera, and returns the RGBA readback.
    void RenderPool(Vixen::SVO::ConcatenatedOctrees pool, const PushConstants& pc,
                     uint32_t w, uint32_t h, std::vector<uint8_t>& outRgba) {
        using C = BodyOctreeSceneNodeConfig;
        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("appflow_toggle_render_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex=0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetRecipePool(std::move(pool));
        // Lazy-Procedural-Delta-Baseline Inc0 M1: pin eager residency so this test's
        // pixel gates keep exercising the full brick march once mip-capable pools
        // default to lazy (a later milestone) — currently a no-op (default is eager).
        node->RequestBrickResidency(true);

        // renderScale=5.0 — see test_editor_document_render.cpp's RenderPool comment: the golden
        // document's object-centered geometry is baked at grid-center (32,32,32) (Inc2a fix);
        // grid-to-world = (kWorldGridSize/n)*renderScale = (10/64)*5 = 0.78125.
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;
        inst.renderScale = 5.0f;
        inst.color[0] = 1.0f; inst.color[1] = 1.0f; inst.color[2] = 1.0f;
        inst.octreeIndex = 0u;
        node->SetInstances({inst});
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

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
        ASSERT_NE(nodes, VK_NULL_HANDLE); ASSERT_NE(cfgBuf, VK_NULL_HANDLE);

        double ms = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             sdfBuf, lookBuf, pc, w, h, outRgba, ms));

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
        nodeBase.reset();
    }
};

// ---------------------------------------------------------------------------
// AppFlow Inc-2 M4 — toggle -> re-flatten -> render -> undo, all dispatched through
// AppFlowRuntime. The authoritative live proof that the whole seam (LayerController ->
// ActionStack -> mask -> FlattenVoxelDocument -> bake -> render) works end to end.
// ---------------------------------------------------------------------------
TEST_F(AppFlowEditorToggleRenderTest, ToggleThenUndoRestoresRender) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    const auto raw = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(raw.empty()) << "golden asset missing: " << VXD_GOLDEN_PATH;
    Yeroket::Sdf::Generated::VoxelDocumentView view{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(raw.data(), raw.size(), view));
    ASSERT_EQ(view.header.layerCount, 3u);
    ASSERT_LT(kCutLayerIndex, view.header.layerCount);

    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), Vixen::AppFlow::LoadResult::Ok);
    rt.Layers().SetLayerCount(view.header.layerCount);

    // Same camera as test_editor_document_render.cpp's ablation test: looks straight down the
    // cylinder bore so the cut toggle produces a real, non-silhouette-blind pixel difference.
    constexpr uint32_t kW = 512, kH = 512;
    constexpr float kGridToWorld = 0.15625f * 5.0f;  // (kWorldGridSize/n) * renderScale
    const glm::vec3 target((32.0f + 0.1f) * kGridToWorld, (32.0f + 0.98f) * kGridToWorld, (32.0f + 0.1f) * kGridToWorld);
    const glm::vec3 eye = target + glm::vec3(0.35f, 1.3f, 0.35f);
    const PushConstants pc = MakeCamera(eye, target, kW, kH, 1);

    auto renderMask = [&](uint32_t mask) -> std::vector<uint8_t> {
        std::vector<uint8_t> blob;
        auto bakeResult = FlattenAndBake(view, mask, blob);
        EXPECT_TRUE(bakeResult.ok) << bakeResult.err;
        std::vector<uint8_t> rgba;
        RenderPool(std::move(bakeResult.pool), pc, kW, kH, rgba);
        return rgba;
    };

    // 1. All layers enabled (the runtime's initial mask).
    const std::vector<uint8_t> pixelsInitial = renderMask(rt.Layers().Mask());
    ASSERT_EQ(pixelsInitial.size(), size_t(kW) * kH * 4);

    // 2. Toggle the cut layer off through AppFlowRuntime, re-render with the resulting mask.
    // ToggleLayer is a registered handler (design §4.3), not a framework verb: the framework
    // knows nothing about layers or self-inverse toggling, only that an id routes to a fn.
    int changed = 0;
    rt.RegisterHandler(Vixen::AppFlow::Generated::FlowActionId::ToggleLayer,
        [&](const Vixen::AppFlow::AppFlowRuntime::Params&){
            rt.Stack().Dispatch(Vixen::AppFlow::Generated::FlowActionId::ToggleLayer,
                [&](bool /*forward*/){ rt.Layers().Toggle(kCutLayerIndex); ++changed; });
        });
    ASSERT_EQ(rt.DispatchById(Vixen::AppFlow::Generated::FlowActionId::ToggleLayer),
              Vixen::AppFlow::DispatchResult::Ok);
    EXPECT_FALSE(rt.Layers().IsEnabled(kCutLayerIndex));
    EXPECT_EQ(changed, 1);
    const std::vector<uint8_t> pixelsToggled = renderMask(rt.Layers().Mask());

    // 3. Undo through AppFlowRuntime, re-render again.
    ASSERT_EQ(rt.Stack().Undo(), Vixen::AppFlow::DispatchResult::Ok);
    EXPECT_TRUE(rt.Layers().IsEnabled(kCutLayerIndex));
    EXPECT_EQ(changed, 2);
    const std::vector<uint8_t> pixelsUndone = renderMask(rt.Layers().Mask());

    auto writePng = [&](const char* path, const std::vector<uint8_t>& rgba) {
        std::vector<uint8_t> rgb(size_t(kW)*kH*3);
        for (uint32_t i = 0; i < kW*kH; ++i) {
            rgb[i*3+0]=rgba[i*4+0]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2];
        }
        stbi_write_png(path, int(kW), int(kH), 3, rgb.data(), int(kW)*3);
    };
    writePng("/tmp/appflow_toggle_initial.png", pixelsInitial);
    writePng("/tmp/appflow_toggle_toggled.png", pixelsToggled);
    writePng("/tmp/appflow_toggle_undone.png",  pixelsUndone);

    // Bore-column region: same centre-screen sample the template's ablation test uses (the
    // camera looks straight down through the cylinder bore at screen-centre).
    int boreDiffPixels = 0;
    constexpr uint32_t kRegionHalf = 40;
    for (uint32_t y = kH/2 - kRegionHalf; y < kH/2 + kRegionHalf; ++y) {
        for (uint32_t x = kW/2 - kRegionHalf; x < kW/2 + kRegionHalf; ++x) {
            const uint32_t i = y*kW + x;
            const int dr = int(pixelsInitial[i*4+0]) - int(pixelsToggled[i*4+0]);
            const int dg = int(pixelsInitial[i*4+1]) - int(pixelsToggled[i*4+1]);
            const int db = int(pixelsInitial[i*4+2]) - int(pixelsToggled[i*4+2]);
            if (std::abs(dr) > 16 || std::abs(dg) > 16 || std::abs(db) > 16) ++boreDiffPixels;
        }
    }
    std::printf("[APPFLOW/toggle] boreDiffPixels=%d (region=%ux%u)\n",
                boreDiffPixels, kRegionHalf*2, kRegionHalf*2);

    // The toggle must have visibly changed the render at the bore (mirrors the template's
    // ablation-gate threshold — this file uses the same camera and same cut layer).
    EXPECT_GT(boreDiffPixels, 3000)
        << "ToggleLayer through AppFlowRuntime did not change the rendered geometry at the bore";

    // Undo must restore the render byte-for-byte — not just "close", exact.
    ASSERT_EQ(pixelsUndone.size(), pixelsInitial.size());
    EXPECT_EQ(std::memcmp(pixelsUndone.data(), pixelsInitial.data(), pixelsInitial.size()), 0)
        << "Undo through AppFlowRuntime did not restore the render byte-for-byte";
}
