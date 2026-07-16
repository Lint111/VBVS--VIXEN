/**
 * @file test_recipe_bucketing_perf.cpp
 * @brief Recipe GPU Instance Bucketing Inc2 M4 live-run gate (Task 9 — performance measurement).
 *
 * Extends M3's proven standalone-harness pattern (test_recipe_multi_bucket_compositing.cpp) from
 * a 2-hot-recipe correctness proof to an N-hot-recipe STEADY-STATE TIMING measurement, at
 * N=3/10/100 distinct recipes, comparing:
 *
 *   (a) this increment's bucketed-dispatch path: N specialized single-recipe pipelines
 *       (EmitSpecializedRecipeComputeShader, Task 5), each dispatched via vkCmdDispatchIndirect
 *       against its own M1 bucket, all issued sequentially through a REAL
 *       Vixen::RenderGraph::MultiDispatchNode with default autoBarriers_=true (same contract as
 *       M3), against
 *   (b) a COLD-PATH STAND-IN dispatch: one fixed-size compute pass looping all N recipes'
 *       instances in a single shader invocation (mirrors this file's own cold-path shader from
 *       M3, generalized to N recipes) — standing in for "the existing tier-0 switch path,"
 *       exactly like M3's ColdRecipeMarchGlsl. See "WHY A STAND-IN, NOT THE REAL SWITCH SHADER"
 *       below for why this is the correct-scoped substitute in THIS standalone harness (the
 *       apples-to-apples tier-0 NUMBER for the ledger comes from the real VIXEN.exe app via
 *       VIXEN_PROCEDURAL_UBER_DEMO + VIXEN_PERF_CSV, captured separately — see Perf-Ledger.md).
 *
 * This harness's own (a)-vs-(b) comparison exists to isolate ONE variable: does routing through
 * N specialized pipelines + N indirect dispatches + N barrier insertions (MultiDispatchNode's
 * default autoBarriers_) cost more or less GPU+CPU wall time than a single fixed dispatch looping
 * the same N recipes' worth of instances, at the SAME instance/recipe counts the real tier-0
 * switch was measured at. It does NOT reproduce the tier-0 switch's own tested cost (shader
 * SIZE/compile-time under N spliced field functions) — that number already exists in
 * Perf-Ledger.md's "Switch-scaling measurement" table and is re-captured on the discrete GPU
 * separately for M4 (see that ledger entry's own header for the GPU-class note).
 *
 * ---------------------------------------------------------------------------------------------
 * WHY A STAND-IN, NOT THE REAL BodyInstanceRayMarch.comp (same reasoning as M3, restated):
 * ---------------------------------------------------------------------------------------------
 * shaders/TraceWorld.glsl's instance loop (the real tier-0 switch's march) has no mechanism to
 * exclude a hot-and-separately-bucketed instance from its own full-instance march, and its
 * HitRecord write is an unconditional overwrite. Wiring bucketed dispatch into the real
 * production shader is live-app integration, explicitly out of scope for this whole increment
 * (see the plan doc's M4 Scoping note, added 2026-07-16) — this harness stays consistent with
 * M1-M3's own standalone-GTest-harness approach, not the live render graph.
 *
 * DEVICE SELECTION (the specific fix M4 is required to make before capturing any numbers): unlike
 * M1-M3's PickPhysicalDevice() (which accepts ANY real GPU, discrete or integrated, first-match-
 * wins), THIS harness's PickPhysicalDevice() mirrors DeviceNode::SelectPhysicalDevice()'s exact
 * logic (DeviceNode.cpp:203-228) — prefer the first VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, fall
 * back to the first real GPU (discrete or integrated) only if no discrete GPU is present, and
 * software (lavapipe/llvmpipe/Dozen) only as the last resort. The selected device's
 * vkGetPhysicalDeviceProperties.deviceName is printed for EVERY test case (hard requirement per
 * the M4 prompt) and asserted to be the expected discrete NVIDIA device where one is present.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/DispatchPass.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "Hash.h"  // ComputeSHA256HexFromUint32Vec — SPIR-V content hash for pipeline cache key
#include "IRenderTarget.h"
#include "MainCacher.h"
#include "ComputePipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "Nodes/MultiDispatchNode.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/SpecializedRecipeShaderGlsl.h"
#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "ShaderCompiler.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef RECIPE_BUCKETING_SPV
#error "RECIPE_BUCKETING_SPV (path to compiled RecipeInstanceBucketing.spv) must be defined by CMake"
#endif
#ifndef SDF_CORE_KERNELS_GLSL_PATH
#error "SDF_CORE_KERNELS_GLSL_PATH must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;
using Vixen::Vulkan::Resources::IRenderTarget;
using Vixen::Vulkan::Resources::RenderTargetData;

// ============================================================================
// Task 6's minimal hotness gate, restated identically to M3 (kHotnessThreshold=4, documented
// placeholder — see test_recipe_multi_bucket_compositing.cpp's own header comment). Every
// synthetic recipe this harness generates gets >=4 instances so 100% of them promote to bucketed
// dispatch at every tested N — a realistic hot/cold MIX per the M4 prompt's Task 9 instruction is
// achieved by also constructing a smaller below-threshold set routed through the cold-path
// stand-in (see BuildScene below).
// ============================================================================
namespace {
constexpr uint32_t kHotnessThreshold = 4;
bool IsHot(uint32_t bucketInstanceCount) { return bucketInstanceCount >= kHotnessThreshold; }

// Byte-identical to RecipeInstanceBucketing.comp's Push block (mirrors M2/M3's own copy).
struct BucketingPush {
    glm::mat4 viewProj;
    uint32_t  instanceCount;
    uint32_t  maxBuckets;
    uint32_t  maxMembersPerBucket;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  mode;
};

struct RecipeBoundSphereCpu {
    float center[3];
    float radius;
    float relaxation;
    float _pad[3];
};
static_assert(sizeof(RecipeBoundSphereCpu) == 32, "RecipeBoundSphereCpu std430 mirror size");

// Byte-identical to the specialized shader's Push block (SpecializedRecipeShaderGlsl.h).
// Inc3 M1: shrunk to camera/screen fields (identical across every bucket in a frame) plus a
// single recipeId selector -- memberCount/rectMinX/rectMinY/boundRadius/stepRelaxation moved to
// the shared BucketMetaBuffer SSBO (see BucketMetaCpu below), indexed by recipeId.
struct SpecializedPush {
    glm::vec3 cameraPos; float _p0;
    glm::vec3 cameraDir; float fov;
    glm::vec3 cameraUp;  float aspect;
    glm::vec3 cameraRight; float _p1;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  maxMembersPerBucket;
    uint32_t  recipeId;
};

// Byte-identical to the specialized shader's BucketMeta struct (SpecializedRecipeShaderGlsl.h).
// One shared SSBO, one entry per recipeId (indexed exactly like RecipeBoundSphereCpu/boundBuf
// already is) -- Inc3 M1's replacement for the old per-bucket push-constant scalar fields.
struct BucketMetaCpu {
    uint32_t memberCount;
    uint32_t rectMinX;
    uint32_t rectMinY;
    float    boundRadius;
    float    stepRelaxation;
    uint32_t _pad[3];
};
static_assert(sizeof(BucketMetaCpu) == 32, "BucketMetaCpu std430 mirror size");

// Cold-path stand-in push block: same camera fields, plus a fixed instance count/recipe count
// for the N-recipe generalized loop (ColdRecipeMarchGlsl below).
struct ColdPush {
    glm::vec3 cameraPos; float _p0;
    glm::vec3 cameraDir; float fov;
    glm::vec3 cameraUp;  float aspect;
    glm::vec3 cameraRight; float _p1;
    uint32_t  instanceCount;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  _p2;
};

// Byte-identical to HitRecord.glsl's std430 layout (64 B/element).
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

// Per-instance record fed to the cold-path stand-in shader — same shape as M3's ColdInstanceCpu.
struct ColdInstanceCpu {
    float worldPos[3];
    float renderScale;
    float color[3];
    uint32_t octreeIndex;
    uint32_t providerKind;
    uint32_t recipeId;
    float recipeParams[6];
    float boundCenter[3];
    float boundRadius;
    float stepRelaxation;
    float _pad[3];
};
static_assert(sizeof(ColdInstanceCpu) == 96, "ColdInstanceCpu std430 mirror size");

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

class RecipeBucketingPerfTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;
    bool             discreteGpuSelected_ = false;

    std::unique_ptr<VulkanDevice> deviceShell_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    static bool IsDiscreteGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
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
        appInfo.pApplicationName = "test_recipe_bucketing_perf";
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
            << "Refusing to run: no usable Vulkan device found; nearest was '"
            << selectedDeviceName_ << "'.";
        std::printf("[recipe-bucketing-perf] selected physical device: '%s' (discrete=%d)\n",
                    selectedDeviceName_.c_str(), discreteGpuSelected_ ? 1 : 0);

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
        deviceShell_->fpCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
            vkGetDeviceProcAddr(logicalDevice_, "vkCmdPipelineBarrier2KHR"));
        deviceShell_->fpQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
            vkGetDeviceProcAddr(logicalDevice_, "vkQueueSubmit2KHR"));
        ASSERT_NE(deviceShell_->fpCmdPipelineBarrier2, nullptr)
            << "vkCmdPipelineBarrier2KHR failed to resolve despite VK_KHR_synchronization2 "
               "being requested as a device extension";
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

    // Mirrors DeviceNode::SelectPhysicalDevice()'s exact logic (DeviceNode.cpp:203-228): prefer
    // the first VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU; fall back to the first real GPU (any type)
    // if none is discrete; software/Dozen only as a last resort. THIS is the fix M1-M3's
    // PickPhysicalDevice() (first-real-GPU-wins, no discrete preference) was missing — see this
    // file's header comment and the M4 plan doc's "Also carried forward from M3" note.
    void PickPhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);

        // Pass 1: first DISCRETE GPU wins.
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsDiscreteGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = true; return;
            }
        }
        // Pass 2: no discrete GPU found — first real GPU (integrated) wins.
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = false; return;
            }
        }
        // Pass 3: software/Dozen fallback.
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = false; return;
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

        VkPhysicalDeviceSynchronization2Features sync2Features{};
        sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        sync2Features.synchronization2 = VK_TRUE;

        const char* deviceExtensions[] = {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.pNext = &sync2Features;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
        dInfo.enabledExtensionCount = 1; dInfo.ppEnabledExtensionNames = deviceExtensions;
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

    void ZeroBuffer(VkDeviceMemory mem, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memset(m, 0, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    template <typename T>
    void ReadbackBuffer(VkDeviceMemory mem, VkDeviceSize size, std::vector<T>& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(T));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    // ==========================================================================================
    // RunPerfCase(N) — the actual measurement. Builds N distinct hot recipes (spheres arranged on
    // a grid so they don't screen-overlap — this harness measures dispatch/routing overhead, not
    // compositing correctness, which M3 already proved), each with kHotnessThreshold instances
    // (all promoted), buckets them via M1's real compute pre-pass, compiles N specialized
    // pipelines via a REAL ComputePipelineCacher (Task 5/7's pattern), then measures:
    //   (a) bucketed-dispatch: N indirect dispatches through ONE MultiDispatchNode, steady-state
    //       wall time over kSteadyIters repeats (fresh command buffer/resubmit each iter — this
    //       harness has no swapchain/frame-pacing, so "steady FPS" here means "steady per-iter
    //       submit+waitIdle wall time," the CPU-observable equivalent available in a headless
    //       compute-only harness).
    //   (b) cold-path stand-in: ONE fixed dispatch looping all N recipes worth of instances in a
    //       single shader invocation, same iteration count, same submit+waitIdle discipline.
    // Both (a) and (b) are on the SAME device, SAME scene, SAME iteration count — a controlled,
    // isolated A/B, not a claim to reproduce the tier-0 switch's own measured cost (see file
    // header).
    // ==========================================================================================
    struct PerfResult {
        double avgMsPerIter = 0.0;
        double avgFps = 0.0;
        uint32_t hitCount = 0;
    };

    void RunPerfCase(uint32_t N, PerfResult& outBucketed, PerfResult& outColdStandIn) {
        ASSERT_TRUE(deviceConfirmed_);
        constexpr uint32_t kScreenWidth = 256, kScreenHeight = 256;
        const uint32_t kMaxBuckets = std::max<uint32_t>(256, N + 16);
        constexpr uint32_t kMaxMembersPerBucket = 64;
        constexpr uint32_t kInstancesPerRecipe = kHotnessThreshold;  // exactly-at-threshold: all N promote
        constexpr int kSteadyIters = 30;

        // --- Build N recipes on a grid, spread wide enough in world-space X/Y that their bound
        //     spheres don't screen-overlap (this harness measures dispatch overhead, not
        //     compositing — M3 already proved compositing correctness under real overlap). ---
        struct RecipeDef {
            uint32_t recipeId;
            Vixen::SVO::RecipeRegistry::RecipeEntry entry;
            Vixen::SVO::Recipe::SdfInstruction prog[1];
        };
        std::vector<RecipeDef> recipes(N);
        const uint32_t gridCols = static_cast<uint32_t>(std::ceil(std::sqrt(double(N))));
        constexpr float kSpacing = 4.0f;
        for (uint32_t i = 0; i < N; ++i) {
            const uint32_t gx = i % gridCols, gy = i / gridCols;
            const glm::vec3 center(
                (float(gx) - float(gridCols) * 0.5f) * kSpacing,
                (float(gy) - float((N + gridCols - 1) / gridCols) * 0.5f) * kSpacing,
                0.0f);
            RecipeDef& rd = recipes[i];
            rd.recipeId = 2 + i;  // recipeId 0/1 reserved (established convention, see M2/M3)
            rd.prog[0].opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
            rd.prog[0].data[0] = center.x; rd.prog[0].data[1] = center.y; rd.prog[0].data[2] = center.z;
            rd.prog[0].data[3] = 1.0f;
            rd.entry.bytecode.assign(rd.prog, rd.prog + 1);
            rd.entry.boundCenter = center;
            rd.entry.boundRadius = 1.4f;
            rd.entry.stepRelaxation = 1.0f;
        }

        std::vector<Vixen::SVO::BodyInstanceGpu> hotInstances;
        auto addHotInstance = [&](uint32_t recipeId) {
            Vixen::SVO::BodyInstanceGpu inst{};
            inst.renderScale = 1.0f;
            inst.color[0] = 0.8f; inst.color[1] = 0.3f; inst.color[2] = 0.3f;
            inst.recipeId = recipeId;
            hotInstances.push_back(inst);
        };
        for (const auto& rd : recipes)
            for (uint32_t k = 0; k < kInstancesPerRecipe; ++k) addHotInstance(rd.recipeId);
        const uint32_t hotInstanceCount = static_cast<uint32_t>(hotInstances.size());

        // Camera: pulled back far enough on Z to frame the whole grid.
        const float gridExtent = float(gridCols) * kSpacing;
        const glm::vec3 eye(0.0f, 0.0f, gridExtent * 1.4f + 10.0f);
        const glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
        const float fovDeg = 60.0f;
        glm::mat4 projection = glm::perspective(glm::radians(fovDeg),
            float(kScreenWidth) / float(kScreenHeight), 0.1f, 500.0f);
        projection[1][1] *= -1.0f;
        glm::mat4 viewProj = projection * view;
        const glm::vec3 camDir = glm::normalize(target - eye);
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 camRight = glm::normalize(glm::cross(camDir, worldUp));
        const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));
        const float aspect = float(kScreenWidth) / float(kScreenHeight);

        // ======================================================================
        // STEP 1: M1 bucketing pre-pass — real compute shader, run ONCE (bucket membership is
        // static across the perf-timed loop; only the dispatch phase is what's being measured).
        // ======================================================================
        VkBuffer instBuf, boundBuf, countBuf, idxBuf, minXBuf, minYBuf, maxXBuf, maxYBuf, indirectBuf;
        VkDeviceMemory instMem, boundMem, countMem, idxMem, minXMem, minYMem, maxXMem, maxYMem, indirectMem;

        const VkDeviceSize instSize  = hotInstanceCount * sizeof(Vixen::SVO::BodyInstanceGpu);
        const VkDeviceSize boundSize = kMaxBuckets * sizeof(RecipeBoundSphereCpu);
        const VkDeviceSize countSize = kMaxBuckets * sizeof(uint32_t);
        const VkDeviceSize idxSize   = static_cast<VkDeviceSize>(kMaxBuckets) * kMaxMembersPerBucket * sizeof(uint32_t);
        const VkDeviceSize extremaSize = kMaxBuckets * sizeof(uint32_t);
        const VkDeviceSize indirectSize = static_cast<VkDeviceSize>(kMaxBuckets) * 3 * sizeof(uint32_t);

        CreateHostBuffer(instSize,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf,  instMem,  false);
        CreateHostBuffer(boundSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boundBuf, boundMem, false);
        CreateHostBuffer(countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, countBuf, countMem, true);
        CreateHostBuffer(idxSize,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, idxBuf,   idxMem,   true);
        CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minXBuf, minXMem, true);
        CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, minYBuf, minYMem, true);
        CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxXBuf, maxXMem, true);
        CreateHostBuffer(extremaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxYBuf, maxYMem, true);
        CreateHostBuffer(indirectSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            indirectBuf, indirectMem, true);

        UploadBuffer(instMem, hotInstances.data(), instSize);

        std::vector<RecipeBoundSphereCpu> boundSpheres(kMaxBuckets, RecipeBoundSphereCpu{});
        for (const auto& rd : recipes) {
            boundSpheres[rd.recipeId] = RecipeBoundSphereCpu{
                {rd.entry.boundCenter.x, rd.entry.boundCenter.y, rd.entry.boundCenter.z},
                rd.entry.boundRadius, rd.entry.stepRelaxation, {0, 0, 0}};
        }
        UploadBuffer(boundMem, boundSpheres.data(), boundSize);

        const std::vector<uint32_t> bucketingSpirv = ReadSpirv(RECIPE_BUCKETING_SPV);
        ASSERT_FALSE(bucketingSpirv.empty()) << "Failed to read compiled SPIR-V at " << RECIPE_BUCKETING_SPV;
        VkShaderModuleCreateInfo bsmci{};
        bsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        bsmci.codeSize = bucketingSpirv.size() * sizeof(uint32_t); bsmci.pCode = bucketingSpirv.data();
        VkShaderModule bucketingModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &bsmci, nullptr, &bucketingModule), VK_SUCCESS);

        auto bind = [](uint32_t b) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };
        const std::array<VkDescriptorSetLayoutBinding, 9> bucketingBindings = {
            bind(0), bind(1), bind(2), bind(3), bind(4), bind(5), bind(6), bind(7), bind(8),
        };
        VkDescriptorSetLayoutCreateInfo bdslci{};
        bdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        bdslci.bindingCount = static_cast<uint32_t>(bucketingBindings.size()); bdslci.pBindings = bucketingBindings.data();
        VkDescriptorSetLayout bucketingDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &bdslci, nullptr, &bucketingDsl), VK_SUCCESS);

        VkPushConstantRange bpcr{};
        bpcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; bpcr.offset = 0; bpcr.size = sizeof(BucketingPush);
        VkPipelineLayoutCreateInfo bplci{};
        bplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        bplci.setLayoutCount = 1; bplci.pSetLayouts = &bucketingDsl;
        bplci.pushConstantRangeCount = 1; bplci.pPushConstantRanges = &bpcr;
        VkPipelineLayout bucketingLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &bplci, nullptr, &bucketingLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo bcpci{};
        bcpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        bcpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        bcpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        bcpci.stage.module = bucketingModule; bcpci.stage.pName = "main";
        bcpci.layout = bucketingLayout;
        VkPipeline bucketingPipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &bcpci, nullptr, &bucketingPipeline), VK_SUCCESS);

        VkDescriptorPoolSize bPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9};
        VkDescriptorPoolCreateInfo bdpci{};
        bdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        bdpci.maxSets = 1; bdpci.poolSizeCount = 1; bdpci.pPoolSizes = &bPoolSize;
        VkDescriptorPool bucketingPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &bdpci, nullptr, &bucketingPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo bdsai{};
        bdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        bdsai.descriptorPool = bucketingPool; bdsai.descriptorSetCount = 1; bdsai.pSetLayouts = &bucketingDsl;
        VkDescriptorSet bucketingSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &bdsai, &bucketingSet), VK_SUCCESS);

        VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE}, boundInfo{boundBuf, 0, VK_WHOLE_SIZE},
            countInfo{countBuf, 0, VK_WHOLE_SIZE}, idxInfo{idxBuf, 0, VK_WHOLE_SIZE},
            minXInfo{minXBuf, 0, VK_WHOLE_SIZE}, minYInfo{minYBuf, 0, VK_WHOLE_SIZE},
            maxXInfo{maxXBuf, 0, VK_WHOLE_SIZE}, maxYInfo{maxYBuf, 0, VK_WHOLE_SIZE},
            indirectInfo{indirectBuf, 0, VK_WHOLE_SIZE};
        auto wBuf = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = bucketingSet; w.dstBinding = b; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
            return w;
        };
        const std::array<VkWriteDescriptorSet, 9> bucketingWrites = {
            wBuf(0, &instInfo), wBuf(1, &boundInfo), wBuf(2, &countInfo), wBuf(3, &idxInfo),
            wBuf(4, &minXInfo), wBuf(5, &minYInfo), wBuf(6, &maxXInfo), wBuf(7, &maxYInfo),
            wBuf(8, &indirectInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(bucketingWrites.size()), bucketingWrites.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
        VkCommandBuffer bucketCmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &bucketCmd), VK_SUCCESS);

        VkCommandBufferBeginInfo cbbi{};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(bucketCmd, &cbbi), VK_SUCCESS);

        vkCmdBindPipeline(bucketCmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingPipeline);
        vkCmdBindDescriptorSets(bucketCmd, VK_PIPELINE_BIND_POINT_COMPUTE, bucketingLayout, 0, 1, &bucketingSet, 0, nullptr);

        BucketingPush pcInit{};
        pcInit.viewProj = viewProj; pcInit.instanceCount = hotInstanceCount;
        pcInit.maxBuckets = kMaxBuckets; pcInit.maxMembersPerBucket = kMaxMembersPerBucket;
        pcInit.screenWidth = kScreenWidth; pcInit.screenHeight = kScreenHeight;
        pcInit.mode = 1;
        vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcInit), &pcInit);
        vkCmdDispatch(bucketCmd, (kMaxBuckets + 63) / 64, 1, 1);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);

        BucketingPush pcBucket = pcInit; pcBucket.mode = 0;
        vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcBucket), &pcBucket);
        vkCmdDispatch(bucketCmd, (hotInstanceCount + 63) / 64, 1, 1);

        vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);

        BucketingPush pcFinalize = pcInit; pcFinalize.mode = 2;
        vkCmdPushConstants(bucketCmd, bucketingLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcFinalize), &pcFinalize);
        vkCmdDispatch(bucketCmd, (kMaxBuckets + 63) / 64, 1, 1);

        VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(bucketCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

        ASSERT_EQ(vkEndCommandBuffer(bucketCmd), VK_SUCCESS);
        VkSubmitInfo bsi{}; bsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        bsi.commandBufferCount = 1; bsi.pCommandBuffers = &bucketCmd;
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &bsi, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        std::vector<uint32_t> bucketCounts, bucketIndices, minXBits, minYBits, indirectCmds;
        ReadbackBuffer(countMem, countSize, bucketCounts);
        ReadbackBuffer(idxMem, idxSize, bucketIndices);
        ReadbackBuffer(minXMem, extremaSize, minXBits);
        ReadbackBuffer(minYMem, extremaSize, minYBits);
        ReadbackBuffer(indirectMem, indirectSize, indirectCmds);

        for (const auto& rd : recipes) {
            ASSERT_EQ(bucketCounts[rd.recipeId], kInstancesPerRecipe)
                << "recipe " << rd.recipeId << " bucket membership mismatch";
            ASSERT_TRUE(IsHot(bucketCounts[rd.recipeId])) << "recipe " << rd.recipeId << " not promoted";
        }

        auto rectOriginOf = [&](uint32_t recipeId) -> std::pair<uint32_t, uint32_t> {
            const uint32_t minXBitVal = minXBits[recipeId];
            const uint32_t minYBitVal = minYBits[recipeId];
            const uint32_t x = minXBitVal == 0xFFFFFFFFu ? 0 : static_cast<uint32_t>(*reinterpret_cast<const float*>(&minXBitVal));
            const uint32_t y = minYBitVal == 0xFFFFFFFFu ? 0 : static_cast<uint32_t>(*reinterpret_cast<const float*>(&minYBitVal));
            return {x, y};
        };

        // ======================================================================
        // STEP 2: compile N specialized shaders through a REAL ComputePipelineCacher (Task 5/7's
        // pattern, looped N times instead of M3's fixed 2).
        // ======================================================================
        std::ifstream coreFile(SDF_CORE_KERNELS_GLSL_PATH);
        ASSERT_TRUE(coreFile.good()) << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
        std::ostringstream coreSs; coreSs << coreFile.rdbuf();
        const std::string sdfCoreGlsl = coreSs.str();

        CashSystem::MainCacher mainCacher;
        mainCacher.Initialize(nullptr);
        mainCacher.RegisterCacher<CashSystem::PipelineLayoutCacher, CashSystem::PipelineLayoutWrapper,
                                   CashSystem::PipelineLayoutCreateParams>(
            typeid(CashSystem::PipelineLayoutWrapper), "PipelineLayout", true);
        mainCacher.RegisterCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                   CashSystem::ComputePipelineCreateParams>(
            typeid(CashSystem::ComputePipelineWrapper), "ComputePipeline", true);
        auto* pipelineCacher = mainCacher.GetCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                                     CashSystem::ComputePipelineCreateParams>(
            typeid(CashSystem::ComputePipelineWrapper), deviceShell_.get());
        ASSERT_NE(pipelineCacher, nullptr);

        const std::array<VkDescriptorSetLayoutBinding, 4> specBindings = {bind(0), bind(1), bind(2), bind(3)};
        VkDescriptorSetLayoutCreateInfo sdslci{};
        sdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sdslci.bindingCount = static_cast<uint32_t>(specBindings.size()); sdslci.pBindings = specBindings.data();
        VkDescriptorSetLayout specDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &sdslci, nullptr, &specDsl), VK_SUCCESS);

        VkPushConstantRange spcr{};
        spcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; spcr.offset = 0; spcr.size = sizeof(SpecializedPush);

        struct CompiledRecipe {
            uint32_t recipeId;
            VkShaderModule module = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            std::string shaderKey;
        };
        auto compileSpecialized = [&](uint32_t recipeId, const Vixen::SVO::RecipeRegistry::RecipeEntry& entry) -> CompiledRecipe {
            const std::string src = Vixen::SVO::Recipe::EmitSpecializedRecipeComputeShader(entry, recipeId, sdfCoreGlsl);
            ShaderManagement::ShaderCompiler compiler;
            ShaderManagement::CompilationOptions opts;
            opts.validateSpirv = false;
            auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, src, "main", opts);
            EXPECT_TRUE(compOut.success) << "recipe " << recipeId << " compile failed:\n" << compOut.GetFullLog();
            if (!compOut.success) return {};

            CompiledRecipe out; out.recipeId = recipeId;
            VkShaderModuleCreateInfo smci{};
            smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = compOut.spirv.size() * sizeof(uint32_t); smci.pCode = compOut.spirv.data();
            EXPECT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &out.module), VK_SUCCESS);

            out.shaderKey = "sdfRecipe_specialized_" + std::to_string(recipeId) + ":" +
                             ShaderManagement::ComputeSHA256HexFromUint32Vec(compOut.spirv);

            CashSystem::ComputePipelineCreateParams params;
            params.shaderModule = out.module;
            params.entryPoint = "main";
            params.descriptorSetLayout = specDsl;
            params.pushConstantRanges = {spcr};
            params.shaderKey = out.shaderKey;
            params.layoutKey = "recipe_bucketing_perf_specialized_layout";
            params.workgroupSizeX = 8; params.workgroupSizeY = 8; params.workgroupSizeZ = 1;
            auto wrapper = pipelineCacher->GetOrCreate(params);
            EXPECT_NE(wrapper, nullptr);
            if (wrapper) {
                out.pipeline = wrapper->pipeline;
                out.layout = wrapper->pipelineLayoutWrapper->layout;
            }
            return out;
        };

        const auto compileStart = std::chrono::steady_clock::now();
        std::vector<CompiledRecipe> compiled;
        compiled.reserve(N);
        for (const auto& rd : recipes) compiled.push_back(compileSpecialized(rd.recipeId, rd.entry));
        for (const auto& c : compiled) ASSERT_NE(c.pipeline, VK_NULL_HANDLE);
        const auto compileEnd = std::chrono::steady_clock::now();
        const double compileMs = std::chrono::duration<double, std::milli>(compileEnd - compileStart).count();
        std::printf("[recipe-bucketing-perf][N=%u] synchronous specialized-pipeline compile: %.2f ms total "
                    "(%.2f ms/recipe) — ON THE CRITICAL PATH per this increment's scoped limitation "
                    "(see plan doc Risks: 'Synchronous compile latency'), excluded from the steady-state "
                    "dispatch-only timing below.\n", N, compileMs, compileMs / double(N));

        // ======================================================================
        // STEP 3: shared dispatch-phase GPU resources.
        //
        // Inc3 M1: `idxBuf` (bucketIndices[], the M1-bucketing pre-pass's own shared row-major
        // output, STEP 1 above) is bound DIRECTLY here instead of being read back and re-uploaded
        // per-bucket into N separate memberBufs[] -- that CPU-side readback+slice+reupload loop is
        // exactly the per-bucket overhead this milestone targets, and it's now simply gone: the
        // buffer this dispatch phase needs already exists, on the GPU, from STEP 1.
        // A NEW shared BucketMetaBuffer (bucketMetaBuf) replaces the old per-bucket push-constant
        // scalar fields (memberCount/rectMinX/rectMinY/boundRadius/stepRelaxation) -- one entry per
        // recipeId, built directly from STEP 1's own bucketCounts[]/rectOriginOf()/recipe entries.
        // ======================================================================
        VkBuffer specInstBuf, hitRecordBuf;
        VkDeviceMemory specInstMem, hitRecordMem;
        CreateHostBuffer(instSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, specInstBuf, specInstMem, false);
        UploadBuffer(specInstMem, hotInstances.data(), instSize);

        const VkDeviceSize hitRecordSize = static_cast<VkDeviceSize>(kScreenWidth) * kScreenHeight * sizeof(HitRecordCpu);
        CreateHostBuffer(hitRecordSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);

        // One BucketMetaCpu entry per POSSIBLE recipeId (indexed the same way boundBuf/idxBuf
        // already are: by raw recipeId, not by this scene's compact 0..N-1 loop index) so the
        // shader's `bucketMeta[pc.recipeId]` lookup matches idxBuf's own row-major convention
        // exactly. Only the N recipeIds this scene actually uses are populated; the rest stay
        // zero-initialized and unread.
        const uint32_t kMaxRecipeId = kMaxBuckets;
        std::vector<BucketMetaCpu> bucketMetaCpuVec(kMaxRecipeId, BucketMetaCpu{});
        for (uint32_t i = 0; i < N; ++i) {
            const uint32_t recipeId = recipes[i].recipeId;
            const auto [rx, ry] = rectOriginOf(recipeId);
            BucketMetaCpu meta{};
            meta.memberCount = bucketCounts[recipeId];
            meta.rectMinX = rx; meta.rectMinY = ry;
            meta.boundRadius = recipes[i].entry.boundRadius;
            meta.stepRelaxation = recipes[i].entry.stepRelaxation;
            bucketMetaCpuVec[recipeId] = meta;
        }
        const VkDeviceSize bucketMetaSize = bucketMetaCpuVec.size() * sizeof(BucketMetaCpu);
        VkBuffer bucketMetaBuf; VkDeviceMemory bucketMetaMem;
        CreateHostBuffer(bucketMetaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bucketMetaBuf, bucketMetaMem, false);
        UploadBuffer(bucketMetaMem, bucketMetaCpuVec.data(), bucketMetaSize);

        // ONE descriptor pool sized for ONE shared descriptor set (was N+1 sets, N*3+2
        // descriptors) -- Inc3 M1's actual per-bucket-allocation elimination.
        VkDescriptorPoolSize dPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &dPoolSize;
        VkDescriptorPool dispatchPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &dispatchPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = dispatchPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &specDsl;
        VkDescriptorSet sharedSpecSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &sharedSpecSet), VK_SUCCESS);

        VkDescriptorBufferInfo specInstInfo{specInstBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo memberInfo{idxBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo hitInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bucketMetaInfo{bucketMetaBuf, 0, VK_WHOLE_SIZE};
        auto wSharedSet = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = sharedSpecSet; w.dstBinding = b; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
            return w;
        };
        const std::array<VkWriteDescriptorSet, 4> sharedSetWrites = {
            wSharedSet(0, &specInstInfo), wSharedSet(1, &memberInfo),
            wSharedSet(2, &hitInfo), wSharedSet(3, &bucketMetaInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(sharedSetWrites.size()), sharedSetWrites.data(), 0, nullptr);

        std::vector<SpecializedPush> pushes(N);
        for (uint32_t i = 0; i < N; ++i) {
            SpecializedPush pc{};
            pc.cameraPos = eye; pc.cameraDir = camDir; pc.fov = fovDeg; pc.cameraUp = camUp; pc.aspect = aspect; pc.cameraRight = camRight;
            pc.screenWidth = kScreenWidth; pc.screenHeight = kScreenHeight;
            pc.maxMembersPerBucket = kMaxMembersPerBucket; pc.recipeId = recipes[i].recipeId;
            pushes[i] = pc;
        }

        // ======================================================================
        // STEP 4: (a) bucketed-dispatch steady-state timing — a FRESH MultiDispatchNode + N
        // indirect dispatches per iteration (mirrors real per-frame recording; this increment
        // does not cache/reuse command buffers across frames, matching the live-app's own
        // per-frame re-record model per RenderGraph's Execute() contract).
        //
        // Inc3 M1 gate #2 (measured call-count reduction): these two counters are set by every
        // runBucketedIter() call to the ACTUAL number of vkCmdBindDescriptorSets/vkCmdPushConstants
        // calls DispatchPass data will cause MultiDispatchNode::RecordDispatches to issue for the
        // bucketed path this iteration (mirroring that function's own `if (!pass.descriptorSets.
        // empty())`/`if (pass.pushConstants.has_value())` guards exactly) — a direct count, not an
        // assumption that the refactor worked.
        // ======================================================================
        uint32_t lastBindDescriptorSetCallCount = 0;
        uint32_t lastPushConstantCallCount = 0;
        auto runBucketedIter = [&]() {
            MultiDispatchNodeType nodeType("MultiDispatch");
            auto nodeBase = nodeType.CreateInstance("recipe_bucketing_perf_bucketed");
            auto* node = dynamic_cast<MultiDispatchNode*>(nodeBase.get());
            EXPECT_NE(node, nullptr);
            if (!node) return;

            Vixen::Vulkan::Resources::RenderTargetData fakeTarget;
            fakeTarget.buffers.resize(1);
            using MC = MultiDispatchNodeConfig;
            Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
            Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
            Resource targetRes; SetHandleVal<IRenderTarget*>(targetRes, static_cast<IRenderTarget*>(&fakeTarget));
            Resource imageIdxRes; uint32_t imageIndex = 0; SetHandleVal<uint32_t>(imageIdxRes, imageIndex);
            Resource frameIdxRes; uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameIdxRes, frameIndex);

            node->SetInput(MC::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
            node->SetInput(MC::COMMAND_POOL_Slot::index,        0, &poolRes);
            node->SetInput(MC::SWAPCHAIN_INFO_Slot::index,      0, &targetRes);
            node->SetInput(MC::IMAGE_INDEX_Slot::index,         0, &imageIdxRes);
            node->SetInput(MC::CURRENT_FRAME_INDEX_Slot::index, 0, &frameIdxRes);

            node->Setup();
            node->Compile();

            // Inc3 M1: bind the ONE shared descriptor set on the FIRST bucket's pass only. Vulkan
            // descriptor-set bindings persist across vkCmdBindPipeline/vkCmdDispatchIndirect within
            // a command buffer as long as pipeline layouts stay compatible (spec: "Pipeline Layout
            // Compatibility" -- binding a new pipeline never disturbs bound descriptor sets, and
            // all N specialized pipelines here share ONE VkPipelineLayout) -- confirmed
            // spec-legal, not GPU-specific behavior. Leaving `pass.descriptorSets` empty for
            // buckets 2..N makes MultiDispatchNode::RecordDispatches' own
            // `if (!pass.descriptorSets.empty())` guard (MultiDispatchNode.cpp) skip the
            // vkCmdBindDescriptorSets call entirely for those passes -- a REAL, measured drop from
            // N calls to 1, not just fewer bytes moved per call.
            uint32_t bindDescriptorSetCallsIssued = 0;
            for (uint32_t i = 0; i < N; ++i) {
                DispatchPass pass;
                pass.pipeline = compiled[i].pipeline; pass.layout = compiled[i].layout;
                if (i == 0) {
                    pass.descriptorSets = {sharedSpecSet};
                    ++bindDescriptorSetCallsIssued;
                }
                pass.indirectBuffer = indirectBuf;
                pass.indirectBufferOffset = recipes[i].recipeId * 3 * sizeof(uint32_t);
                PushConstantData pcData; pcData.data.resize(sizeof(SpecializedPush));
                std::memcpy(pcData.data.data(), &pushes[i], sizeof(SpecializedPush));
                pass.pushConstants = pcData;
                pass.debugName = "Recipe" + std::to_string(recipes[i].recipeId) + "_Specialized";
                node->QueueDispatch(std::move(pass));
            }
            lastBindDescriptorSetCallCount = bindDescriptorSetCallsIssued;
            lastPushConstantCallCount = N;  // unchanged: recipeId still varies per bucket, still N pushes

            node->Execute();
            using MC2 = MultiDispatchNodeConfig;
            VkCommandBuffer cmdBuffer = node->GetOutput(MC2::COMMAND_BUFFER_Slot::index, 0)->GetHandle<VkCommandBuffer>();
            EXPECT_NE(cmdBuffer, VK_NULL_HANDLE);

            VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1; si.pCommandBuffers = &cmdBuffer;
            EXPECT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
            EXPECT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

            node->Cleanup(CleanupReason::FinalTeardown);
        };

        // Warm-up iteration (excluded from timing — first-touch driver costs, cache warm).
        ZeroBuffer(hitRecordMem, hitRecordSize);
        runBucketedIter();

        // Inc3 M1 gate #2: report the MEASURED before/after Vulkan API call counts per bucket
        // batch, not an assumed reduction. "Before" (pre-M1) was N vkCmdBindDescriptorSets + N
        // vkCmdPushConstants (one distinct descriptor set + one push-constant blob per bucket, per
        // the grounding research and this file's own pre-M1 per-bucket specSets[]/memberBufs[]
        // loop, with a 92-byte push-constant struct -- 4 vec3+float camera pairs (64B) plus
        // memberCount/screenWidth/screenHeight/rectMinX/rectMinY/boundRadius/stepRelaxation, 7
        // scalars, 28B). "After" is measured directly off this run's actual DispatchPass
        // construction; the push-constant struct itself shrinks to just the camera fields plus
        // screenWidth/screenHeight/maxMembersPerBucket/recipeId (4 scalars, 16B tail).
        constexpr size_t kPreM1PushConstantBytes = 92;
        const uint32_t preM1BindDescriptorSetCalls = N;
        const uint32_t preM1PushConstantCalls = N;
        std::printf("[recipe-bucketing-perf][N=%u][Inc3-M1] vkCmdBindDescriptorSets: %u -> %u per bucket-batch "
                    "(saved %u calls). vkCmdPushConstants: %u -> %u per bucket-batch (push-constant PAYLOAD "
                    "shrunk from %zu to %zu bytes/call; scalar fields memberCount/rectMinX/rectMinY/"
                    "boundRadius/stepRelaxation moved to shared BucketMetaBuffer SSBO).\n",
                    N, preM1BindDescriptorSetCalls, lastBindDescriptorSetCallCount,
                    preM1BindDescriptorSetCalls - lastBindDescriptorSetCallCount,
                    preM1PushConstantCalls, lastPushConstantCallCount,
                    kPreM1PushConstantBytes, sizeof(SpecializedPush));

        ZeroBuffer(hitRecordMem, hitRecordSize);
        const auto bucketedStart = std::chrono::steady_clock::now();
        for (int iter = 0; iter < kSteadyIters; ++iter) runBucketedIter();
        const auto bucketedEnd = std::chrono::steady_clock::now();
        const double bucketedTotalMs = std::chrono::duration<double, std::milli>(bucketedEnd - bucketedStart).count();
        outBucketed.avgMsPerIter = bucketedTotalMs / double(kSteadyIters);
        outBucketed.avgFps = outBucketed.avgMsPerIter > 0.0 ? 1000.0 / outBucketed.avgMsPerIter : 0.0;
        {
            std::vector<HitRecordCpu> hr;
            ReadbackBuffer(hitRecordMem, hitRecordSize, hr);
            for (const auto& r : hr) if (r.flags & 0x1u) ++outBucketed.hitCount;
        }
        std::printf("[recipe-bucketing-perf][N=%u] BUCKETED: %.4f ms/iter, %.1f fps (%d steady iters), hits=%u\n",
                    N, outBucketed.avgMsPerIter, outBucketed.avgFps, kSteadyIters, outBucketed.hitCount);

        // ======================================================================
        // STEP 5: (b) cold-path stand-in — ONE fixed dispatch looping ALL N recipes' instances
        // (generalizes M3's ColdRecipeMarchGlsl from a small fixed list to N recipes' worth).
        // ======================================================================
        std::vector<ColdInstanceCpu> coldInstances;
        coldInstances.reserve(N);
        for (const auto& rd : recipes) {
            ColdInstanceCpu ci{};
            ci.boundCenter[0] = rd.entry.boundCenter.x; ci.boundCenter[1] = rd.entry.boundCenter.y; ci.boundCenter[2] = rd.entry.boundCenter.z;
            ci.boundRadius = rd.entry.boundRadius; ci.stepRelaxation = rd.entry.stepRelaxation;
            ci.color[0] = 0.8f; ci.color[1] = 0.3f; ci.color[2] = 0.3f;
            ci.recipeId = rd.recipeId;
            ci.recipeParams[0] = 1.0f;  // sphere radius, matches prog[0].data[3] above
            coldInstances.push_back(ci);
        }
        const VkDeviceSize coldInstSize = coldInstances.size() * sizeof(ColdInstanceCpu);
        VkBuffer coldInstBuf; VkDeviceMemory coldInstMem;
        CreateHostBuffer(coldInstSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, coldInstBuf, coldInstMem, false);
        UploadBuffer(coldInstMem, coldInstances.data(), coldInstSize);

        static const char* kColdRecipeMarchGlsl = R"GLSL(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct ColdInstance {
    vec3  worldPos; float renderScale;
    vec3  color; uint octreeIndex;
    uint  providerKind; uint recipeId;
    float recipeParams[6];
    vec3  boundCenter; float boundRadius;
    float stepRelaxation; float _pad[3];
};
layout(std430, binding = 0) readonly buffer ColdInstanceBuffer { ColdInstance coldInstances[]; };

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL
#define HITRECORD_FLAG_HIT 0x1u
struct HitRecord {
    vec3 albedo; float roughness;
    vec3 worldNormal; float hitT;
    vec3 worldPos; uint flags;
    uint _pad0[3];
};
#endif
layout(std430, binding = 1) buffer HitRecordBuffer { HitRecord hitRecords[]; };

layout(push_constant) uniform Push {
    vec3 cameraPos; float _p0;
    vec3 cameraDir; float fov;
    vec3 cameraUp;  float aspect;
    vec3 cameraRight; float _p1;
    uint instanceCount;
    uint screenWidth;
    uint screenHeight;
    uint _p2;
} pc;

float sdfSphere(vec3 p, float r) { return length(p) - r; }

vec3 getRayDir(vec2 uv) {
    float tanHalfFov = tan(radians(pc.fov * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return normalize(pc.cameraDir + pc.cameraRight * ndc.x * tanHalfFov * pc.aspect
                                   + pc.cameraUp    * ndc.y * tanHalfFov);
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= int(pc.screenWidth) || pixelCoords.y >= int(pc.screenHeight)) return;

    vec2 uv = (vec2(pixelCoords) + 0.5) / vec2(float(pc.screenWidth), float(pc.screenHeight));
    vec3 rayOrigin = pc.cameraPos;
    vec3 rayDir    = getRayDir(uv);

    bool  anyHit = false;
    float bestT  = 1e30;
    vec3  bestNormal = vec3(0.0, 1.0, 0.0);
    vec3  bestColor  = vec3(1.0);

    for (uint m = 0u; m < pc.instanceCount; ++m) {
        ColdInstance inst = coldInstances[m];
        vec3  oc = rayOrigin - inst.boundCenter;
        float b  = dot(oc, rayDir);
        float c  = dot(oc, oc) - inst.boundRadius * inst.boundRadius;
        float disc = b * b - c;
        if (disc < 0.0) continue;
        float sq = sqrt(disc);
        float tNear = max(-b - sq, 0.0);
        float tFar  = -b + sq;
        if (tFar < 0.0 || tNear >= bestT) continue;

        float t = tNear;
        const int   MAX_STEPS = 128;
        const float EPS = 1e-3;
        float sphereRadius = inst.recipeParams[0];
        for (int i = 0; i < MAX_STEPS; ++i) {
            vec3  p = rayOrigin + rayDir * t - inst.boundCenter;
            float d = sdfSphere(p, sphereRadius);
            if (d < EPS) {
                if (t < bestT) {
                    bestT = t;
                    bestNormal = normalize(p);
                    bestColor  = inst.color;
                    anyHit = true;
                }
                break;
            }
            t += d * inst.stepRelaxation;
            if (t > tFar) break;
        }
    }

    if (!anyHit) return;

    uint hitIdx = uint(pixelCoords.y) * pc.screenWidth + uint(pixelCoords.x);
    if (bestT < hitRecords[hitIdx].hitT || hitRecords[hitIdx].flags == 0u) {
        HitRecord rec;
        rec.albedo = bestColor;
        rec.roughness = 1.0;
        rec.worldNormal = bestNormal;
        rec.hitT = bestT;
        rec.worldPos = rayOrigin + rayDir * bestT;
        rec.flags = HITRECORD_FLAG_HIT;
        rec._pad0 = uint[3](0u, 0u, 0u);
        hitRecords[hitIdx] = rec;
    }
}
)GLSL";

        ShaderManagement::ShaderCompiler coldCompiler;
        ShaderManagement::CompilationOptions coldOpts;
        coldOpts.validateSpirv = false;
        auto coldCompOut = coldCompiler.Compile(ShaderManagement::ShaderStage::Compute, kColdRecipeMarchGlsl, "main", coldOpts);
        ASSERT_TRUE(coldCompOut.success) << "cold-path stand-in shader compile failed:\n" << coldCompOut.GetFullLog();

        VkShaderModuleCreateInfo cmsmci{};
        cmsmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        cmsmci.codeSize = coldCompOut.spirv.size() * sizeof(uint32_t); cmsmci.pCode = coldCompOut.spirv.data();
        VkShaderModule coldModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &cmsmci, nullptr, &coldModule), VK_SUCCESS);

        const std::array<VkDescriptorSetLayoutBinding, 2> coldBindings = {bind(0), bind(1)};
        VkDescriptorSetLayoutCreateInfo cdslci{};
        cdslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        cdslci.bindingCount = static_cast<uint32_t>(coldBindings.size()); cdslci.pBindings = coldBindings.data();
        VkDescriptorSetLayout coldDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &cdslci, nullptr, &coldDsl), VK_SUCCESS);

        VkPushConstantRange cpcr{};
        cpcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; cpcr.offset = 0; cpcr.size = sizeof(ColdPush);
        VkPipelineLayoutCreateInfo cplci{};
        cplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        cplci.setLayoutCount = 1; cplci.pSetLayouts = &coldDsl;
        cplci.pushConstantRangeCount = 1; cplci.pPushConstantRanges = &cpcr;
        VkPipelineLayout coldLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &cplci, nullptr, &coldLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo ccpci{};
        ccpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ccpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ccpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ccpci.stage.module = coldModule; ccpci.stage.pName = "main";
        ccpci.layout = coldLayout;
        VkPipeline coldPipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &ccpci, nullptr, &coldPipeline), VK_SUCCESS);

        VkDescriptorPool coldPool = VK_NULL_HANDLE;
        VkDescriptorPoolSize coldPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo cdpci{};
        cdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        cdpci.maxSets = 1; cdpci.poolSizeCount = 1; cdpci.pPoolSizes = &coldPoolSize;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &cdpci, nullptr, &coldPool), VK_SUCCESS);
        VkDescriptorSetAllocateInfo cdsai{};
        cdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        cdsai.descriptorPool = coldPool; cdsai.descriptorSetCount = 1; cdsai.pSetLayouts = &coldDsl;
        VkDescriptorSet coldSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &cdsai, &coldSet), VK_SUCCESS);

        VkDescriptorBufferInfo coldInstInfo{coldInstBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo coldHitInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
        auto wColdSet = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = coldSet; w.dstBinding = b; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = info;
            return w;
        };
        const std::array<VkWriteDescriptorSet, 2> coldWrites = {wColdSet(0, &coldInstInfo), wColdSet(1, &coldHitInfo)};
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(coldWrites.size()), coldWrites.data(), 0, nullptr);

        ColdPush pcCold{};
        pcCold.cameraPos = eye; pcCold.cameraDir = camDir; pcCold.fov = fovDeg; pcCold.cameraUp = camUp; pcCold.aspect = aspect; pcCold.cameraRight = camRight;
        pcCold.instanceCount = static_cast<uint32_t>(coldInstances.size()); pcCold.screenWidth = kScreenWidth; pcCold.screenHeight = kScreenHeight;

        auto runColdIter = [&]() {
            VkCommandBufferAllocateInfo icbai{};
            icbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            icbai.commandPool = commandPool_; icbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; icbai.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &icbai, &cmd), VK_SUCCESS);

            VkCommandBufferBeginInfo icbbi{};
            icbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            icbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ASSERT_EQ(vkBeginCommandBuffer(cmd, &icbbi), VK_SUCCESS);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, coldPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, coldLayout, 0, 1, &coldSet, 0, nullptr);
            vkCmdPushConstants(cmd, coldLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcCold), &pcCold);
            vkCmdDispatch(cmd, (kScreenWidth + 7) / 8, (kScreenHeight + 7) / 8, 1);

            ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
            VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
            ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
            ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
            vkFreeCommandBuffers(logicalDevice_, commandPool_, 1, &cmd);
        };

        ZeroBuffer(hitRecordMem, hitRecordSize);
        runColdIter();  // warm-up

        ZeroBuffer(hitRecordMem, hitRecordSize);
        const auto coldStart = std::chrono::steady_clock::now();
        for (int iter = 0; iter < kSteadyIters; ++iter) runColdIter();
        const auto coldEnd = std::chrono::steady_clock::now();
        const double coldTotalMs = std::chrono::duration<double, std::milli>(coldEnd - coldStart).count();
        outColdStandIn.avgMsPerIter = coldTotalMs / double(kSteadyIters);
        outColdStandIn.avgFps = outColdStandIn.avgMsPerIter > 0.0 ? 1000.0 / outColdStandIn.avgMsPerIter : 0.0;
        {
            std::vector<HitRecordCpu> hr;
            ReadbackBuffer(hitRecordMem, hitRecordSize, hr);
            for (const auto& r : hr) if (r.flags & 0x1u) ++outColdStandIn.hitCount;
        }
        std::printf("[recipe-bucketing-perf][N=%u] COLD-STAND-IN: %.4f ms/iter, %.1f fps (%d steady iters), hits=%u\n",
                    N, outColdStandIn.avgMsPerIter, outColdStandIn.avgFps, kSteadyIters, outColdStandIn.hitCount);

        ASSERT_GT(outBucketed.hitCount, 0u) << "bucketed path produced zero hits — scene/camera framing broken";
        ASSERT_GT(outColdStandIn.hitCount, 0u) << "cold-stand-in path produced zero hits — scene/camera framing broken";

        // --- Cleanup ---
        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyDescriptorPool(logicalDevice_, coldPool, nullptr);
        vkDestroyPipeline(logicalDevice_, coldPipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, coldLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, coldDsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, coldModule, nullptr);
        vkDestroyBuffer(logicalDevice_, coldInstBuf, nullptr); vkFreeMemory(logicalDevice_, coldInstMem, nullptr);

        vkDestroyDescriptorPool(logicalDevice_, dispatchPool, nullptr);
        for (const auto& c : compiled) {
            vkDestroyPipeline(logicalDevice_, c.pipeline, nullptr);
            vkDestroyShaderModule(logicalDevice_, c.module, nullptr);
        }
        if (!compiled.empty()) vkDestroyPipelineLayout(logicalDevice_, compiled[0].layout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, specDsl, nullptr);
        vkDestroyBuffer(logicalDevice_, specInstBuf, nullptr); vkFreeMemory(logicalDevice_, specInstMem, nullptr);
        vkDestroyBuffer(logicalDevice_, hitRecordBuf, nullptr); vkFreeMemory(logicalDevice_, hitRecordMem, nullptr);
        vkDestroyBuffer(logicalDevice_, bucketMetaBuf, nullptr); vkFreeMemory(logicalDevice_, bucketMetaMem, nullptr);

        vkDestroyDescriptorPool(logicalDevice_, bucketingPool, nullptr);
        vkDestroyPipeline(logicalDevice_, bucketingPipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, bucketingLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, bucketingDsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, bucketingModule, nullptr);
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
};

TEST_F(RecipeBucketingPerfTest, N3_BucketedVsColdStandIn) {
    PerfResult bucketed, cold;
    ASSERT_NO_FATAL_FAILURE(RunPerfCase(3, bucketed, cold));
    std::printf("[recipe-bucketing-perf][SUMMARY][N=3] bucketed=%.4fms(%.1ffps) cold-stand-in=%.4fms(%.1ffps) "
                "speedup=%.3fx\n", bucketed.avgMsPerIter, bucketed.avgFps, cold.avgMsPerIter, cold.avgFps,
                cold.avgMsPerIter / bucketed.avgMsPerIter);
}

TEST_F(RecipeBucketingPerfTest, N10_BucketedVsColdStandIn) {
    PerfResult bucketed, cold;
    ASSERT_NO_FATAL_FAILURE(RunPerfCase(10, bucketed, cold));
    std::printf("[recipe-bucketing-perf][SUMMARY][N=10] bucketed=%.4fms(%.1ffps) cold-stand-in=%.4fms(%.1ffps) "
                "speedup=%.3fx\n", bucketed.avgMsPerIter, bucketed.avgFps, cold.avgMsPerIter, cold.avgFps,
                cold.avgMsPerIter / bucketed.avgMsPerIter);
}

TEST_F(RecipeBucketingPerfTest, N100_BucketedVsColdStandIn) {
    PerfResult bucketed, cold;
    ASSERT_NO_FATAL_FAILURE(RunPerfCase(100, bucketed, cold));
    std::printf("[recipe-bucketing-perf][SUMMARY][N=100] bucketed=%.4fms(%.1ffps) cold-stand-in=%.4fms(%.1ffps) "
                "speedup=%.3fx\n", bucketed.avgMsPerIter, bucketed.avgFps, cold.avgMsPerIter, cold.avgFps,
                cold.avgMsPerIter / bucketed.avgMsPerIter);
}
