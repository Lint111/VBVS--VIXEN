/**
 * @file test_editor_document_render.cpp
 * @brief Inc1 M4 — vixen_editor's load/flatten/bake/render/toggle path, live-gated on a real device.
 *
 * Loads the golden sample_tri_layer.vxd (base=Box(1,1,1), bulge=Sphere(r=0.6) SmoothUnion
 * k=0.15, cut=Cylinder(halfHeight=1.5,radius=0.35) Subtract), flattens+bakes+renders it through
 * the real BodyOctreeSceneNode -> BodyInstanceRayMarch.comp path (same fixture pattern as
 * test_recipe_pool_render.cpp), then re-does it with the cut layer disabled and asserts a real,
 * non-silhouette-blind pixel difference: the cylinder (halfHeight=1.5) punches all the way
 * through the box's top face (halfExtents=1), so a column of pixels directly above the cylinder
 * bore is background/void with the cut enabled and solid box-top color with it disabled — a
 * verified numeric fact (see the flatten test's GridParityAgainstIndependentComposition and this
 * file's own probe: at grid x in [0,0.3], z=0, the surface is void up past y=1.6 with the cut,
 * solid at y=0.98 without it).
 *
 * NOTE on scale (Inc2a re-derivation): BakeRecipeInstructionsToSdfWorld now applies `center`
 * (`p - center` at eval, see SdfBake.h) exactly like the analytic bake path, so the golden
 * document's object-centered geometry (authored near local origin, ~[-1.5,1.5] extent) is baked
 * AT RecipeBakeConfig::center's default grid position (32,32,32) -- no longer clipped to the
 * positive-octant corner as it was pre-fix. Grid-to-world: (kWorldGridSize/n)*renderScale =
 * (10/64)*5 = 0.78125, so grid-center (32,32,32) -> world (25,25,25); this is the camera target
 * below (previously the corner-workaround target of world ~(0.39,0.39,0.39), which pre-fix was
 * the only place the geometry actually rendered -- post-fix that point is empty space, which is
 * exactly the fresh 0-hit-pixel failure this task's re-derivation fixes).
 *
 * DEVICE SELECTION: identical contract to test_recipe_pool_render.cpp — prefers
 * Mesa-Dozen (the real GPU) via VixenSelectWslGpuIcd(), falls back to lavapipe.
 *
 * Run: ./test_editor_document_render
 *   (set VK_ICD_FILENAMES explicitly to force a specific ICD, e.g. for comparison.)
 *
 * Output: /tmp/editor_document_render_{with,without}_cut.png (512x512 RGBA8).
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
#include "Recipe/generated/VoxelDocument.g.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/VoxelDocumentFlattener.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <algorithm>
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

#ifndef VXD_GOLDEN_PATH
#error "VXD_GOLDEN_PATH (path to sample_tri_layer.vxd) must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// Baked-perf-pipeline M2: SceneBindings.glsl's real PushConstants struct is 92 bytes
// (debugTargetPixel + accumFrameCount added by 47eccd64, well before this M2's own
// work -- see test_body_instance_occlusion_reject.cpp's identical fix for the fuller
// citation of why a from-scratch shader rebuild surfaces this mirror's staleness).
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float raySizeCoef; float raySizeBias; int32_t instanceCount;
    int32_t _pad0;  // GLSL std430 aligns ivec2 to 8 bytes (offset 80); a plain C++ struct
                    // packs debugTargetPixel at offset 76 without this explicit filler.
    glm::ivec2 debugTargetPixel;
    uint32_t   accumFrameCount;
};
static_assert(sizeof(PushConstants) == 92, "PushConstants must be 92 bytes");

// ---------------------------------------------------------------------------
// M2c fix: this file's colorImg (binding 0) readback went permanently dark when
// commit 784adff7 (Sampled Lighting Inc3 M1, KI-018) split shading out of
// BodyInstanceRayMarch.comp into DirectLighting.comp/SpatialReuseShade.comp —
// this shader now writes ONLY HitRecordBuffer (binding 18) and idOutputImage
// (binding 9), never outputImage. Hit/color checks now read HitRecord instead —
// same mirror struct test_hitrecord_readback.cpp/test_body_instance_raymarch_
// render.cpp already established.
// ---------------------------------------------------------------------------
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];  // std430 tail padding — see test_hitrecord_readback.cpp's identical mirror
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

constexpr uint32_t kHitRecordFlagHit = 0x1u;

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
    // Baked-perf-pipeline M2: pc{} zero-inits debugTargetPixel to (0,0), which
    // shouldCaptureDebug (TraceRecording.glsl, compiled when VIXEN_GPU_TRACE_HOOKS is
    // defined -- see body_instance_raymarch_spv's glslc -D flag) reads as a real
    // click-target match at pixel (0,0), routing that ray into the reserved
    // DEBUG_CLICK_TARGET_SLOT (255). traceBuf is now sized for this (see
    // kRayTraceBufferSize in RenderToRgba), but there's no reason to exercise this path
    // at all here. (-1,-1) is the documented disable value (SceneBindings.glsl's
    // debugTargetPixel comment).
    pc.debugTargetPixel = glm::ivec2(-1, -1);
    return pc;
}

// Flattens the golden document (with the given enabledOverride, or all-enabled if null) into a
// baked single-recipe ConcatenatedOctrees pool. Mirrors EditorApplication::ApplyDocumentToScene.
Vixen::SVO::RecipeBakeResult FlattenAndBake(const Yeroket::Sdf::Generated::VoxelDocumentView& view,
                                             const std::vector<uint8_t>* enabledOverride,
                                             std::vector<uint8_t>& outBlob) {
    std::string err;
    const bool flattenOk = Vixen::SVO::FlattenVoxelDocument(view, enabledOverride, outBlob, err);
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
// Minimal Vulkan fixture — identical structure to test_recipe_pool_render.cpp.
// ---------------------------------------------------------------------------
class EditorDocumentRenderTest : public ::testing::Test {
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
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_editor_document_render"; ai.apiVersion = VK_API_VERSION_1_3;
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
                      std::vector<uint8_t>& rgba, double& ms,
                      std::vector<HitRecordCpu>* outHitRecords = nullptr) {
        ASSERT_TRUE(softwareConfirmed_);
        // Baked-perf-pipeline M2: RayTraceBuffer (binding 4) is real, non-placeholder -- see
        // test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation of
        // why a 256-byte placeholder is UB once this SPV compiles with VIXEN_GPU_TRACE_HOOKS
        // (grid-capture fires at every 64th pixel of this test's 512x512 dispatch). This test
        // doesn't read RayTraceBuffer back, but shares the SPV with tests that DO need
        // VIXEN_GPU_TRACE_HOOKS for instanceIterCount[], so it pays the same sizing requirement.
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
        // Baked-perf-pipeline M2: bindings 15 (TierRefTableBuffer) and 18 (HitRecordBuffer) are
        // real SSBOs the shader has declared since before this M2's own work (Tiered-ESVO Inc2
        // M3 / M-wire Task 8) -- this test's descriptor layout/pool/writes never picked them up,
        // which only became visible once a from-scratch rebuild of BodyInstanceRayMarch.comp
        // (forced by M2's CMake change) made vkCreateComputePipelines validate against the
        // shader's REAL current reflected interface instead of a stale cached .spv. Same
        // 256-byte placeholder pattern as dummySdf/dummyLookup/dummyMip above.
        VkBuffer dummyTierRef=VK_NULL_HANDLE, dummyHitRecord=VK_NULL_HANDLE;
        VkDeviceMemory dTierRefMem=VK_NULL_HANDLE, dHitRecordMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyTierRef,dTierRefMem,true);
        // M2c fix: sized for real w*h*64 (not a 256-byte placeholder) and read back below —
        // this is now the buffer the hit/color checks read (see this file's HitRecordCpu
        // comment; colorImg/binding 0 is never written post-KI-018).
        const VkDeviceSize hitRecordBufSize = VkDeviceSize(w) * VkDeviceSize(h) * 64;
        CreateHostBuffer(hitRecordBufSize,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyHitRecord,dHitRecordMem,true);
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
            wB(15,&tierRefI),   // TierRefTableBuffer (placeholder)
            wB(18,&hitRecordI),  // HitRecordBuffer (placeholder)
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

        // M2c fix: barrier the HitRecord SSBO (shader write -> host read) before the host
        // reads it below — same pattern test_body_instance_occlusion_reject.cpp's iteration
        // debug buffer already uses.
        VkBufferMemoryBarrier hitRecordBarrier{}; hitRecordBarrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hitRecordBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; hitRecordBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        hitRecordBarrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; hitRecordBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.buffer=dummyHitRecord; hitRecordBarrier.offset=0; hitRecordBarrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0,0,nullptr,1,&hitRecordBarrier,0,nullptr);

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

        if (outHitRecords != nullptr) {
            void* hrMapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, dHitRecordMem, 0, hitRecordBufSize, 0, &hrMapped), VK_SUCCESS);
            outHitRecords->assign(size_t(w) * h, HitRecordCpu{});
            std::memcpy(outHitRecords->data(), hrMapped, size_t(hitRecordBufSize));
            vkUnmapMemory(logicalDevice_, dHitRecordMem);
        }

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
    // renderScale=1.0, worldPos=(0,0,0) so grid-space maps 1:1 to world-space) with the given
    // camera, and returns the RGBA readback + hit-pixel count (threshold matches
    // test_recipe_pool_render.cpp's non-background heuristic).
    void RenderPool(Vixen::SVO::ConcatenatedOctrees pool, const PushConstants& pc,
                     uint32_t w, uint32_t h, std::vector<uint8_t>& outRgba, int& outHitPixels,
                     std::vector<HitRecordCpu>* outHitRecords = nullptr) {
        using C = BodyOctreeSceneNodeConfig;
        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("editor_doc_render_test");
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

        // renderScale=5.0 — see EditorApplication::ApplyDocumentToScene's comment: the golden
        // document's object-centered geometry (~[-1.5,1.5] extent) is now baked AT
        // RecipeBakeConfig::center's default grid position (32,32,32) (Inc2a fix), and the
        // shader's base-octree world frame is a fixed [0,10] span (kWorldGridSize=10), so
        // grid-to-world = (10/64)*5 = 0.78125; grid-center (32,32,32) -> world (25,25,25).
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

        // M2c fix: outHitPixels reads HitRecordBuffer (still written post-784adff7/KI-018)
        // instead of the dead colorImg — DO NOT revert to a colorImg readback; see this file's
        // HitRecordCpu comment. outRgba is still returned (some callers write it to PNG for
        // inspection) but no longer drives the hit count.
        double ms = 0.0;
        std::vector<HitRecordCpu> localHitRecords;
        std::vector<HitRecordCpu>& hitRecords = outHitRecords ? *outHitRecords : localHitRecords;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             sdfBuf, lookBuf, pc, w, h, outRgba, ms, &hitRecords));

        outHitPixels = 0;
        for (uint32_t i = 0; i < w*h; ++i) {
            if ((hitRecords[i].flags & kHitRecordFlagHit) != 0u) ++outHitPixels;
        }

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
        nodeBase.reset();
    }
};

// ---------------------------------------------------------------------------
// M4 — load -> flatten -> bake -> render (all layers enabled): asserts a visible body.
// ---------------------------------------------------------------------------
TEST_F(EditorDocumentRenderTest, GoldenDocumentAllLayersRendersVisibleBody) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(softwareConfirmed_);

    const auto raw = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(raw.empty()) << "golden asset missing: " << VXD_GOLDEN_PATH;
    Yeroket::Sdf::Generated::VoxelDocumentView view{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(raw.data(), raw.size(), view));
    ASSERT_EQ(view.header.layerCount, 3u);

    std::vector<uint8_t> blob;
    auto bakeResult = FlattenAndBake(view, nullptr, blob);
    ASSERT_TRUE(bakeResult.ok) << bakeResult.err;
    ASSERT_EQ(bakeResult.pool.count, 1u);

    // Camera: frame the golden's whole geometry, now correctly centered at grid (32,32,32) —
    // Inc2a's bake-center fix (BakeRecipeInstructionsToSdfWorld applies `center`) means the
    // object-centered document no longer clips to the positive-octant corner (pre-fix behavior).
    // worldPos=(0,0,0), renderScale=5.0, grid-to-world=(10/64)*5=0.78125 -- see RenderPool's
    // renderScale comment. grid-center (32,32,32) -> world (25,25,25).
    constexpr uint32_t kW = 512, kH = 512;
    constexpr float kGridToWorld = 0.15625f * 5.0f;  // (kWorldGridSize/n) * renderScale
    const glm::vec3 target(32.0f * kGridToWorld, 32.0f * kGridToWorld, 32.0f * kGridToWorld);
    const glm::vec3 eye = target + glm::vec3(1.6f, 1.3f, 1.6f);
    const PushConstants pc = MakeCamera(eye, target, kW, kH, 1);

    std::vector<uint8_t> rgba; int hitPixels = 0;
    std::vector<HitRecordCpu> hitRecords;
    ASSERT_NO_FATAL_FAILURE(RenderPool(std::move(bakeResult.pool), pc, kW, kH, rgba, hitPixels, &hitRecords));

    {
        // M2c fix: PNG rendered from HitRecord.albedo (still written post-784adff7/KI-018),
        // not the dead colorImg — DO NOT revert this to a colorImg readback — so it stays
        // visually meaningful for inspection.
        std::vector<uint8_t> rgb(size_t(kW)*kH*3);
        for (uint32_t i = 0; i < kW*kH; ++i) {
            const HitRecordCpu& rec = hitRecords[i];
            const bool hit = (rec.flags & kHitRecordFlagHit) != 0u;
            rgb[i*3+0] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[0], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+1] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[1], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+2] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[2], 0.0f, 1.0f) * 255.0f) : 0;
        }
        stbi_write_png("/tmp/editor_document_render_with_cut.png", int(kW), int(kH), 3, rgb.data(), int(kW)*3);
    }

    std::printf("[EDITOR] all-layers render | hitPixels=%d -> /tmp/editor_document_render_with_cut.png\n", hitPixels);
    // Inc2a re-derivation: post-fix, the whole object-centered body is framed (not clipped to a
    // corner), so a fresh lavapipe run measures hitPixels=221250 (of 512*512=262144, ~84% fill) --
    // far above the old corner-workaround's weak >500 bound. 50000 keeps wide margin below the
    // measured value while still meaningfully gating "is the body visible at all".
    EXPECT_GT(hitPixels, 50000) << "Golden document produced too few hit pixels -- body may not be visible";
}

// ---------------------------------------------------------------------------
// M4 — ablation gate: vary ONLY the "cut" layer's enabled bit, assert a real pixel-level
// difference at the cylinder bore's top-face location (NOT just an aggregate count, and NOT
// silhouette-blind -- the cylinder (halfHeight=1.5) punches all the way through the box
// (halfExtents=1)'s top face, so this is a genuine outline/hole difference, verified numerically
// via evalRecipe before writing this test: at grid (x in [0,0.3], z=0) the surface is void up to
// y>1.6 with the cut enabled, solid at y=0.98 with it disabled).
// ---------------------------------------------------------------------------
TEST_F(EditorDocumentRenderTest, DisablingCutLayerChangesTopFaceSilhouette) {
    ASSERT_TRUE(softwareConfirmed_);

    const auto raw = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(raw.empty());
    Yeroket::Sdf::Generated::VoxelDocumentView view{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(raw.data(), raw.size(), view));

    constexpr uint32_t kW = 512, kH = 512;
    // Look down at the cylinder bore's footprint (x,z near the object's LOCAL origin, inside the
    // 0.35-radius bore) from a steep angle so a disabled cut shows solid box-top colour and an
    // enabled cut shows void/background through the hole -- an outline change directly under the
    // camera's centre pixel, not an interior-only depression a silhouette test would miss.
    // Inc2a: the object-centered document is now baked AT grid-center (32,32,32) (Inc2a's
    // bake-center fix), so a local-space point (lx,ly,lz) lands at grid (32+lx, 32+ly, 32+lz).
    // Grid->world: (kWorldGridSize/n)*renderScale = 0.15625*5 = 0.78125 (see RenderPool's
    // comment); local bore footprint (0.1,0.1) and local box-top y=0.98 (both verified
    // numerically beforehand, unchanged by the centering fix -- it's a local-space fact).
    constexpr float kGridToWorld = 0.15625f * 5.0f;
    const glm::vec3 target((32.0f + 0.1f) * kGridToWorld, (32.0f + 0.98f) * kGridToWorld, (32.0f + 0.1f) * kGridToWorld);
    const glm::vec3 eye = target + glm::vec3(0.35f, 1.3f, 0.35f);  // steep but non-degenerate angle
    const PushConstants pc = MakeCamera(eye, target, kW, kH, 1);

    std::vector<uint8_t> blobWithCut, blobNoCut;
    auto bakeWithCut = FlattenAndBake(view, nullptr, blobWithCut);
    ASSERT_TRUE(bakeWithCut.ok) << bakeWithCut.err;

    std::vector<uint8_t> enabledOverride = {1, 1, 0};  // base, bulge enabled; cut DISABLED
    auto bakeNoCut = FlattenAndBake(view, &enabledOverride, blobNoCut);
    ASSERT_TRUE(bakeNoCut.ok) << bakeNoCut.err;

    // M2c fix: hitWithCut/hitNoCut and the centre-diff both read HitRecordBuffer (still
    // written post-784adff7/KI-018) instead of the dead colorImg — DO NOT revert to a
    // colorImg readback; see this file's HitRecordCpu comment. PNGs rendered from albedo
    // so they stay visually meaningful.
    std::vector<uint8_t> rgbaWithCut, rgbaNoCut;
    std::vector<HitRecordCpu> hitRecordsWithCut, hitRecordsNoCut;
    int hitWithCut = 0, hitNoCut = 0;
    ASSERT_NO_FATAL_FAILURE(RenderPool(std::move(bakeWithCut.pool), pc, kW, kH, rgbaWithCut, hitWithCut, &hitRecordsWithCut));
    ASSERT_NO_FATAL_FAILURE(RenderPool(std::move(bakeNoCut.pool),  pc, kW, kH, rgbaNoCut,  hitNoCut, &hitRecordsNoCut));

    auto writePng = [&](const char* path, const std::vector<HitRecordCpu>& recs) {
        std::vector<uint8_t> rgb(size_t(kW)*kH*3);
        for (uint32_t i = 0; i < kW*kH; ++i) {
            const HitRecordCpu& rec = recs[i];
            const bool hit = (rec.flags & kHitRecordFlagHit) != 0u;
            rgb[i*3+0] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[0], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+1] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[1], 0.0f, 1.0f) * 255.0f) : 0;
            rgb[i*3+2] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[2], 0.0f, 1.0f) * 255.0f) : 0;
        }
        stbi_write_png(path, int(kW), int(kH), 3, rgb.data(), int(kW)*3);
    };
    writePng("/tmp/editor_document_render_with_cut.png", hitRecordsWithCut);
    writePng("/tmp/editor_document_render_without_cut.png", hitRecordsNoCut);

    // Sample a small region around screen-centre (where the camera looks straight down through
    // the bore) and count differing pixels between the two renders. A hit/miss flip counts as
    // a difference even before comparing albedo (with-cut=void/miss, no-cut=solid/hit is
    // exactly this case — the simplest possible "differs").
    int centreDiffPixels = 0;
    constexpr uint32_t kRegionHalf = 40;
    for (uint32_t y = kH/2 - kRegionHalf; y < kH/2 + kRegionHalf; ++y) {
        for (uint32_t x = kW/2 - kRegionHalf; x < kW/2 + kRegionHalf; ++x) {
            const uint32_t i = y*kW + x;
            const HitRecordCpu& recCut = hitRecordsWithCut[i];
            const HitRecordCpu& recNoCut = hitRecordsNoCut[i];
            const bool hitCut = (recCut.flags & kHitRecordFlagHit) != 0u;
            const bool hitNoCutPx = (recNoCut.flags & kHitRecordFlagHit) != 0u;
            if (hitCut != hitNoCutPx) { ++centreDiffPixels; continue; }
            if (!hitCut) continue;  // both miss: no difference to measure
            const float dr = std::abs(recCut.albedo[0] - recNoCut.albedo[0]);
            const float dg = std::abs(recCut.albedo[1] - recNoCut.albedo[1]);
            const float db = std::abs(recCut.albedo[2] - recNoCut.albedo[2]);
            if (dr > 16.0f/255.0f || dg > 16.0f/255.0f || db > 16.0f/255.0f) ++centreDiffPixels;
        }
    }

    std::printf("[EDITOR/ablation] hitWithCut=%d hitNoCut=%d centreDiffPixels=%d (region=%ux%u)\n",
                hitWithCut, hitNoCut, centreDiffPixels, kRegionHalf*2, kRegionHalf*2);

    // The disabled-cut render must show MORE solid pixels overall (the hole gets filled in).
    EXPECT_GT(hitNoCut, hitWithCut)
        << "Disabling the cut layer should fill in the punched-through hole, increasing hit count";
    // And the top-face region directly over the bore must differ at the pixel level -- proves
    // the toggle actually changed the rendered geometry there, not just an aggregate elsewhere.
    // Inc2a re-derivation: the corrected centering means the camera looks dead-center at the
    // bore (not an off-corner view), so a fresh lavapipe run measures centreDiffPixels=6400 --
    // literally the ENTIRE 80x80=6400 sampled region differs (with-cut=void, no-cut=solid box
    // top). 3000 keeps wide margin below the measured value while still meaningfully gating
    // "did the toggle change geometry here" (old weak bound was >50, out of the same 6400).
    EXPECT_GT(centreDiffPixels, 3000)
        << "Expected a real pixel-level difference under the cylinder bore; toggle may not be wired";
}

// ---------------------------------------------------------------------------
// M4 — determinism: flattening the same document+override twice must be byte-identical.
// ---------------------------------------------------------------------------
TEST_F(EditorDocumentRenderTest, FlattenIsDeterministic) {
    const auto raw = ReadFile(VXD_GOLDEN_PATH);
    ASSERT_FALSE(raw.empty());
    Yeroket::Sdf::Generated::VoxelDocumentView view{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadVoxelDocument(raw.data(), raw.size(), view));

    std::vector<uint8_t> blobA, blobB;
    std::string errA, errB;
    ASSERT_TRUE(Vixen::SVO::FlattenVoxelDocument(view, nullptr, blobA, errA)) << errA;
    ASSERT_TRUE(Vixen::SVO::FlattenVoxelDocument(view, nullptr, blobB, errB)) << errB;

    ASSERT_EQ(blobA.size(), blobB.size());
    EXPECT_EQ(std::memcmp(blobA.data(), blobB.data(), blobA.size()), 0)
        << "Flattening the same document twice produced different bytes";

    // Also verify determinism with a non-trivial override applied.
    std::vector<uint8_t> ov = {1, 0, 1};
    std::vector<uint8_t> blobC, blobD;
    std::string errC, errD;
    ASSERT_TRUE(Vixen::SVO::FlattenVoxelDocument(view, &ov, blobC, errC)) << errC;
    ASSERT_TRUE(Vixen::SVO::FlattenVoxelDocument(view, &ov, blobD, errD)) << errD;
    ASSERT_EQ(blobC.size(), blobD.size());
    EXPECT_EQ(std::memcmp(blobC.data(), blobD.data(), blobC.size()), 0);
}
