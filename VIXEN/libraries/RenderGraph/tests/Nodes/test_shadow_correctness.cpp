/**
 * @file test_shadow_correctness.cpp
 * @brief Sampled Lighting Inc1 M4 gate: exercises the REAL TraceWorldShadow via a real
 *        dispatch of the BodyInstanceRayMarch.comp -> ShadowVisibilityWave.comp ->
 *        SpatialReuseShade.comp chain against a known scene + known directional light, and
 *        asserts a pixel's occlusion state (as read from the shaded colour output) matches
 *        an independent CPU-traced reference shadow ray. This closes
 *        M2's deferred "GLSL traversal agrees with reference" gap: M2 only proved
 *        TraceWorldShadow returns SOME bool via a CPU mirror function; this test proves
 *        the GLSL shader itself (compiled, dispatched, real GPU/lavapipe) produces the
 *        correct SHADOWED/LIT classification for real geometry.
 *
 * Scene (all Procedural-provider spheres — traceProceduralBody, no octree needed):
 *   - occluder: a sphere placed directly between the "target" surface point and the light.
 *   - target:   a flat-ish sphere whose near-side surface point (facing the camera) is
 *               occluded from the light by `occluder`.
 *   - litControl: a second target-like sphere with NO occluder between it and the light —
 *                 must render LIT even with shadows enabled (proves the test isn't just
 *                 measuring "shadows make everything darker").
 *
 * Reference: an independent CPU ray-vs-sphere occlusion test (NOT calling into any VIXEN
 * traversal code — a from-scratch analytic sphere intersection), mirroring the
 * gpu-shader-debug convention (test_traceworld_mirror.cpp's own CPU mirror) but checked
 * against the REAL shader's pixel output this time, not another CPU mirror.
 *
 * Reuses the real-shader dispatch pattern from test_body_instance_occlusion_reject.cpp /
 * test_hitrecord_readback.cpp, extended with the production visibility-wave and shading
 * bindings for LightingConfig, HitRecord, and ShadowConfig.
 *
 * Run: ./test_shadow_correctness
 */

#include <gtest/gtest.h>

#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "ShaderBundleBuilder.h"  // shading and visibility-wave shaders compiled at test runtime

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

#ifndef VIXEN_SHADER_SOURCE_DIR
#error "VIXEN_SHADER_SOURCE_DIR (shaders/ tree, for shading/wave #includes) must be defined by CMake"
#endif

// ---------------------------------------------------------------------------
// ROOT CAUSE (2026-09-01): Sampled Lighting Inc3 M1 (KI-018, 784adff7) moved shading out of
// BodyInstanceRayMarch.comp -- "no image writes (imageSize() call only)" per that shader's own
// binding-0 comment -- and Inc3 M5 (747e156c) split the shading shader into DirectLighting.comp
// (pure reservoir-buffer producer, no image writes) and SpatialReuseShade.comp (the actual
// outputImage writer). W1b (6a8500a2) then made ShadowVisibilityWave.comp the producer of the
// analytic visibility bits consumed by SpatialReuseShade.comp. The test had been updated for
// the M1/M5 output path but still dispatched only march -> shade, so march's zero-initialized
// HitRecordBuffer._pad0[2] made every shadow-enabled light appear occluded: both target and
// litControl became ambient-only (luma 11), while shadows-disabled bypassed the mask (luma 75).
// Fix: dispatch the complete current production chain -- march -> ShadowVisibilityWave ->
// SpatialReuseShade -- with barriers around the read/write HitRecordBuffer transitions.
// reservoirEnabled/probeGridEnabled stay 0 (both shaders' own byte-identity escape hatches),
// so SpatialReuseShade.comp's shading reduces to computeLightingWithShadows(...), the same
// analytic term this test was always meant to exercise, unwrapped from ReSTIR/DDGI.
// ---------------------------------------------------------------------------

namespace {

// Mirrors test_body_instance_raymarch_render.cpp's own PushConstants mirror exactly
// (omits the shader's trailing ivec2 debugTargetPixel — TEMP DEBUG field, unused by
// every hand-built-descriptor-layout test in this file family; the push constant
// RANGE below still covers bytes 0-75, which is all any of these tests' shader paths
// statically read).
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
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes (matches shader std430 push block)");

// Host-side mirror of Generated/LightingConfig.g.h (Sampled Lighting Inc0 M1) — a single
// directional light, matching the field layout test_lightingconfig_sdi_parity.cpp proves.
struct LightCpu {
    float directionX, directionY, directionZ;
    uint32_t kind;
    float radianceX, radianceY, radianceZ;
    float range;
};
static_assert(sizeof(LightCpu) == 32, "LightCpu std430 mirror size");

struct LightingConfigCpu {
    uint32_t lightCount;
    float ambientIntensity;
    uint8_t _pad0[8];
    LightCpu lights[4];
};
static_assert(sizeof(LightingConfigCpu) == 144, "LightingConfigCpu std430 mirror size");

// Host-side mirror of Generated/ShadowConfig.g.h (Sampled Lighting Inc1 M4).
struct ShadowConfigCpu {
    uint32_t enabled;
    uint32_t raysPerLight;
    float maxShadowDistance;
    float biasEpsilon;
};
static_assert(sizeof(ShadowConfigCpu) == 16, "ShadowConfigCpu std430 mirror size");

// Host-side mirrors of the additional Generated/*.g.h structs SpatialReuseShade.comp binds
// (bindings 19/20/22/23/24/25/26/27/31/34, see BuildRenderGraph.cpp's spatialReuseGatherer
// wiring) that BodyInstanceRayMarch.comp's own dispatch never needed. All disabled/zeroed --
// reservoirEnabled==0 and probeGridEnabled==0 are each shader's own byte-identity escape hatch
// (see DirectLighting.comp / SpatialReuseShade.comp main()), so this test exercises exactly
// computeLightingWithShadows(...), nothing ReSTIR/DDGI adds. Declared here, alongside
// PushConstants/LightingConfigCpu/ShadowConfigCpu above, because ShadowCorrectnessTest::
// RenderSceneShaded is a MEMBER FUNCTION using these types, and a class body compiles before
// anything declared textually after it -- these must precede `class ShadowCorrectnessTest`.
struct AccumulationConfigCpu {
    uint32_t enabled;
    float alpha;
    uint32_t maxFrames;
    uint32_t resetOnMotion;
    uint32_t reprojectionEnabled;
};
static_assert(sizeof(AccumulationConfigCpu) == 20, "AccumulationConfigCpu std430 mirror size");

struct PrevCameraConfigCpu {
    float prevViewProj[16];
};
static_assert(sizeof(PrevCameraConfigCpu) == 64, "PrevCameraConfigCpu std430 mirror size");

struct ReservoirConfigCpu {
    uint32_t reservoirEnabled;
    uint32_t candidateCount;
    float spatialRadius;
    uint32_t spatialCount;
    uint32_t temporalCap;
    uint32_t biasedModeEnabled;
    float lightTreeCutThreshold;
    uint32_t frameParity;
};
static_assert(sizeof(ReservoirConfigCpu) == 32, "ReservoirConfigCpu std430 mirror size");

struct LightTreeGpuNodeCpu {
    float worldPosX, worldPosY, worldPosZ;
    float worldExtent;
    float intensity;
    float coverage;
    float _reserved0, _reserved1;
};
static_assert(sizeof(LightTreeGpuNodeCpu) == 32, "LightTreeGpuNodeCpu std430 mirror size");

struct LightTreeBufferCpu {
    uint32_t nodeCount;
    uint8_t _pad0[12];
    LightTreeGpuNodeCpu nodes[64];
};
static_assert(sizeof(LightTreeBufferCpu) == 2064, "LightTreeBufferCpu std430 mirror size");

struct ReservoirRecordCpu {
    uint32_t y;
    float weightSum;
    uint32_t sampleCount;
    float targetPdf;
};
static_assert(sizeof(ReservoirRecordCpu) == 16, "ReservoirRecordCpu std430 mirror size");

struct ProbeGridConfigCpu {
    uint32_t probeGridEnabled;
    float originX, originY, originZ;
    float spacingX, spacingY, spacingZ;
    uint32_t countX, countY, countZ;
    uint32_t raysPerProbe;
    float hysteresisRate;
    uint32_t amortizationFactor;
    uint32_t frameCounter;
};
static_assert(sizeof(ProbeGridConfigCpu) == 56, "ProbeGridConfigCpu std430 mirror size");

// Mirrors SpatialReuseShade.comp's DDGILeakGateDebugShade struct (binding 31) -- unread by this
// test (ddgiLeakGateEnabled stays 0, gating every write inside the shader to it), but must still
// be sized+bound: SPIR-V reflection requires every declared binding to be satisfied.
struct DDGILeakGateDebugShadeCpu {
    uint32_t ddgiLeakGateEnabled;
    uint32_t chebyshevTestEnabled;
    uint32_t nearProbeIndex;
    uint32_t farProbeIndex;
    float farShadingPosX, farShadingPosY, farShadingPosZ;
    float gatheredLuma;
    uint32_t diagNearProbeHitCount;
    float diagNearProbeAvgRadianceLuma;
    float diagNearProbeAvgDepth;
    float diagNearProbeAvgDepth2;
    uint32_t shadeM5IndirectLumaBits;
    uint32_t diagShadeAnyHitCount;
};

// Procedural BodyInstance layout (only the fields this test needs to set).
Vixen::SVO::BodyInstanceGpu MakeProceduralSphere(glm::vec3 center, float radius,
                                                 float r, float g, float b) {
    Vixen::SVO::BodyInstanceGpu inst{};
    inst.worldPos[0] = center.x; inst.worldPos[1] = center.y; inst.worldPos[2] = center.z;
    inst.renderScale  = 1.0f;   // unused by Procedural
    inst.color[0] = r; inst.color[1] = g; inst.color[2] = b;
    inst.octreeIndex  = 0u;     // unused by Procedural
    inst.providerKind = 1u;     // PROVIDER_PROCEDURAL
    inst.recipeId     = 0u;     // sphere
    inst.recipeParams[0] = radius;
    inst.recipeParams[1] = 0.0f;  // displaceAmp
    inst.recipeParams[2] = 0.0f;  // displaceFreq
    return inst;
}

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

// Independent CPU reference: analytic ray-vs-sphere intersection (NOT calling into any
// VIXEN traversal/shadow code) — the ground truth this test's shader dispatch is checked
// against.
bool RaySphereHit(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center,
                  float radius, float tMin, float tMax) {
    const glm::vec3 oc = ro - center;
    const float b = glm::dot(oc, rd);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;
    const float sq = std::sqrt(disc);
    const float t0 = -b - sq;
    const float t1 = -b + sq;
    const float t = (t0 >= tMin) ? t0 : t1;
    return (t >= tMin && t <= tMax);
}

// Reference occlusion test for a surface point against every sphere in the scene
// (excluding the sphere the point sits on, identified by index) — mirrors exactly what
// TraceWorldShadow should report for a given (point, lightDir) pair.
bool ReferenceOccluded(const glm::vec3& surfacePoint, const glm::vec3& normal,
                       const glm::vec3& lightDir, float biasEpsilon, float maxDist,
                       const std::vector<glm::vec3>& centers, const std::vector<float>& radii,
                       int skipIndex) {
    const glm::vec3 origin = surfacePoint + normal * biasEpsilon;
    for (size_t i = 0; i < centers.size(); ++i) {
        if (static_cast<int>(i) == skipIndex) continue;
        if (RaySphereHit(origin, lightDir, centers[i], radii[i], biasEpsilon, maxDist)) {
            return true;
        }
    }
    return false;
}

}  // namespace

class ShadowCorrectnessTest : public ::testing::Test {
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
        appInfo.pApplicationName = "test_shadow_correctness";
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

    void UploadHostBuffer(VkDeviceMemory mem, const void* data, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    // Compiles SpatialReuseShade.comp once per test process (ShaderBundleBuilder does its own
    // internal caching by content hash, but there's no reason to re-run reflection/SDI-gen per
    // RenderSceneShaded() call within a test). Mirrors BuildRenderGraph.cpp's own
    // spatialReuseShaderLibNode registration (AddStageFromFile + the same two include paths),
    // just without that function's env-gated debug #define injection (VIXEN_SHADOW_DBG /
    // VIXEN_SHADOW_NO_MIP_ANYHIT) -- this test doesn't need either.
    static const std::vector<uint32_t>& SpatialReuseShadeSpirv() {
        static const std::vector<uint32_t> spirv = [] {
            const std::filesystem::path shaderDir(VIXEN_SHADER_SOURCE_DIR);
            ShaderManagement::ShaderBundleBuilder builder;
            builder.SetProgramName("SpatialReuseShade")
                   .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                   .AddIncludePath(shaderDir)
                   .AddStageFromFile(ShaderManagement::ShaderStage::Compute,
                                     shaderDir / "SpatialReuseShade.comp", "main")
                   .EnableSdiGeneration(false);
            ShaderManagement::ShaderBundleBuilder::BuildResult result = builder.Build();
            if (!result.success || !result.bundle) {
                ADD_FAILURE() << "SpatialReuseShade.comp failed to build: " << result.errorMessage;
                return std::vector<uint32_t>{};
            }
            return result.bundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
        }();
        return spirv;
    }

    // Compiles the W1b visibility producer used by the current production chain. It must run
    // between BodyInstanceRayMarch.comp and SpatialReuseShade.comp: march initializes the
    // HitRecordBuffer policy word, this pass fills its analytic shadow bits, and shade consumes
    // those bits. Keeping this pass in the test is what makes its shadow-enabled path match the
    // production march -> ShadowVisibilityWave -> SpatialReuseShade contract.
    static const std::vector<uint32_t>& ShadowVisibilityWaveSpirv() {
        static const std::vector<uint32_t> spirv = [] {
            const std::filesystem::path shaderDir(VIXEN_SHADER_SOURCE_DIR);
            ShaderManagement::ShaderBundleBuilder builder;
            builder.SetProgramName("ShadowVisibilityWave")
                   .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                   .AddIncludePath(shaderDir)
                   .AddStageFromFile(ShaderManagement::ShaderStage::Compute,
                                     shaderDir / "ShadowVisibilityWave.comp", "main")
                   .EnableSdiGeneration(false);
            ShaderManagement::ShaderBundleBuilder::BuildResult result = builder.Build();
            if (!result.success || !result.bundle) {
                ADD_FAILURE() << "ShadowVisibilityWave.comp failed to build: " << result.errorMessage;
                return std::vector<uint32_t>{};
            }
            return result.bundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
        }();
        return spirv;
    }

    // Dispatches the REAL current production shading chain: BodyInstanceRayMarch.comp writes
    // HitRecordBuffer, ShadowVisibilityWave.comp writes its analytic visibility bits, and
    // SpatialReuseShade.comp reads that same HitRecordBuffer and writes outputImage. This is
    // the chain introduced by Sampled Lighting Inc3 M1/M5 and W1b (784adff7 / 747e156c / 6a8500a2).
    // Binding set for SpatialReuseShade.comp mirrors BuildRenderGraph.cpp's spatialReuseGatherer
    // wiring exactly: 0,1,2,3,5,10-13,15-27,31,32-35. reservoir/probeGrid stay disabled (see
    // the struct comments above), so shading reduces to computeLightingWithShadows after the
    // wave has populated the hit-record visibility word.
    void RenderSceneShaded(const std::vector<Vixen::SVO::BodyInstanceGpu>& instances,
                           const LightingConfigCpu& lighting, const ShadowConfigCpu& shadow,
                           const PushConstants& pc, uint32_t w, uint32_t h,
                           std::vector<uint8_t>& outRgba) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";

        const std::vector<uint32_t>& shadeSpirv = SpatialReuseShadeSpirv();
        ASSERT_FALSE(shadeSpirv.empty()) << "SpatialReuseShade.comp SPIR-V is empty (build failed above)";
        const std::vector<uint32_t>& waveSpirv = ShadowVisibilityWaveSpirv();
        ASSERT_FALSE(waveSpirv.empty()) << "ShadowVisibilityWave.comp SPIR-V is empty (build failed above)";

        // --- Scene SSBOs shared by BOTH dispatches (read-only in both, same content). ---
        constexpr VkDeviceSize kRayTraceBufferSize = 16 + 256 * (16 + 64 * 48);
        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, counterBuf, counterMem, true);

        VkBuffer dummyNodes = VK_NULL_HANDLE, dummyBricks = VK_NULL_HANDLE, dummyMats = VK_NULL_HANDLE,
                 dummyConfig = VK_NULL_HANDLE, dummySdf = VK_NULL_HANDLE, dummyLookup = VK_NULL_HANDLE,
                 dummyMip = VK_NULL_HANDLE, dummyTierRef = VK_NULL_HANDLE, dummyIter = VK_NULL_HANDLE;
        VkDeviceMemory dummyNodesMem = VK_NULL_HANDLE, dummyBricksMem = VK_NULL_HANDLE, dummyMatsMem = VK_NULL_HANDLE,
                       dummyConfigMem = VK_NULL_HANDLE, dummySdfMem = VK_NULL_HANDLE, dummyLookupMem = VK_NULL_HANDLE,
                       dummyMipMem = VK_NULL_HANDLE, dummyTierRefMem = VK_NULL_HANDLE, dummyIterMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyNodes, dummyNodesMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyBricks, dummyBricksMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyMats, dummyMatsMem, true);
        CreateHostBuffer(432, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyConfig, dummyConfigMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySdf, dummySdfMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyLookup, dummyLookupMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyMip, dummyMipMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyTierRef, dummyTierRefMem, true);
        CreateHostBuffer(static_cast<VkDeviceSize>(instances.size()) * sizeof(uint32_t) + 4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyIter, dummyIterMem, true);

        const VkDeviceSize instBufSize = static_cast<VkDeviceSize>(instances.size()) * sizeof(Vixen::SVO::BodyInstanceGpu);
        VkBuffer instBuf = VK_NULL_HANDLE; VkDeviceMemory instMem = VK_NULL_HANDLE;
        CreateHostBuffer(instBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf, instMem, false);
        UploadHostBuffer(instMem, instances.data(), instBufSize);

        VkBuffer lightBuf = VK_NULL_HANDLE; VkDeviceMemory lightMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(LightingConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lightBuf, lightMem, false);
        UploadHostBuffer(lightMem, &lighting, sizeof(LightingConfigCpu));

        VkBuffer shadowBuf = VK_NULL_HANDLE; VkDeviceMemory shadowMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(ShadowConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, shadowBuf, shadowMem, false);
        UploadHostBuffer(shadowMem, &shadow, sizeof(ShadowConfigCpu));

        VkBuffer dummySkipMask = VK_NULL_HANDLE; VkDeviceMemory dummySkipMaskMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySkipMask, dummySkipMaskMem, true);

        // HitRecordBuffer: WRITTEN by march, then read/written by ShadowVisibilityWave, then
        // READ by shade. All three dispatches go through ONE command buffer with barriers between
        // the producer/consumer transitions below, so no separate submit is needed.
        const VkDeviceSize hitRecordSize = static_cast<VkDeviceSize>(w) * h * 64;
        VkBuffer hitRecordBuf = VK_NULL_HANDLE; VkDeviceMemory hitRecordMem = VK_NULL_HANDLE;
        CreateHostBuffer(hitRecordSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);

        // --- SpatialReuseShade-only bindings (19/20/22/23/24/25/26/27/31/34; 21 is the
        // march's own too but unread by both here, still bound to satisfy reflection). ---
        const AccumulationConfigCpu accum{};  // all-zero: enabled=0 skips the accumulate seam
        VkBuffer accumBuf = VK_NULL_HANDLE; VkDeviceMemory accumMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(AccumulationConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, accumBuf, accumMem, false);
        UploadHostBuffer(accumMem, &accum, sizeof(AccumulationConfigCpu));

        const VkFormat kHistoryFmt = VK_FORMAT_R8G8B8A8_UNORM;
        VkImage historyImg = VK_NULL_HANDLE; VkDeviceMemory historyMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kHistoryFmt, historyImg, historyMem));
        VkImageView historyView = CreateView(historyImg, kHistoryFmt);
        ASSERT_NE(historyView, VK_NULL_HANDLE);

        const PrevCameraConfigCpu prevCamera{};  // all-zero: reprojectionEnabled=0 skips its use
        VkBuffer prevCameraBuf = VK_NULL_HANDLE; VkDeviceMemory prevCameraMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(PrevCameraConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, prevCameraBuf, prevCameraMem, false);
        UploadHostBuffer(prevCameraMem, &prevCamera, sizeof(PrevCameraConfigCpu));

        const VkFormat kWorldPosFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
        VkImage worldPosImg = VK_NULL_HANDLE; VkDeviceMemory worldPosMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kWorldPosFmt, worldPosImg, worldPosMem));
        VkImageView worldPosView = CreateView(worldPosImg, kWorldPosFmt);
        ASSERT_NE(worldPosView, VK_NULL_HANDLE);

        const ReservoirConfigCpu reservoirCfg{};  // all-zero: reservoirEnabled=0 (byte-identity escape hatch)
        VkBuffer reservoirCfgBuf = VK_NULL_HANDLE; VkDeviceMemory reservoirCfgMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(ReservoirConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, reservoirCfgBuf, reservoirCfgMem, false);
        UploadHostBuffer(reservoirCfgMem, &reservoirCfg, sizeof(ReservoirConfigCpu));

        const LightTreeBufferCpu lightTree{};  // all-zero: nodeCount=0, RIS/spatial reuse are no-ops
        VkBuffer lightTreeBuf = VK_NULL_HANDLE; VkDeviceMemory lightTreeMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(LightTreeBufferCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lightTreeBuf, lightTreeMem, false);
        UploadHostBuffer(lightTreeMem, &lightTree, sizeof(LightTreeBufferCpu));

        const VkDeviceSize reservoirBufSize = static_cast<VkDeviceSize>(w) * h * sizeof(ReservoirRecordCpu);
        VkBuffer reservoirA = VK_NULL_HANDLE, reservoirB = VK_NULL_HANDLE, spatialDebugBuf = VK_NULL_HANDLE;
        VkDeviceMemory reservoirAMem = VK_NULL_HANDLE, reservoirBMem = VK_NULL_HANDLE, spatialDebugMem = VK_NULL_HANDLE;
        CreateHostBuffer(reservoirBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, reservoirA, reservoirAMem, true);
        CreateHostBuffer(reservoirBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, reservoirB, reservoirBMem, true);
        CreateHostBuffer(reservoirBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, spatialDebugBuf, spatialDebugMem, true);

        const DDGILeakGateDebugShadeCpu ddgiDebug{};  // ddgiLeakGateEnabled=0
        VkBuffer ddgiDebugBuf = VK_NULL_HANDLE; VkDeviceMemory ddgiDebugMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(DDGILeakGateDebugShadeCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ddgiDebugBuf, ddgiDebugMem, false);
        UploadHostBuffer(ddgiDebugMem, &ddgiDebug, sizeof(DDGILeakGateDebugShadeCpu));

        // SpatialReuseShade.comp declares these at DIFFERENT channel counts (binding 32
        // rgba16f vs binding 33 rg16f) -- a native-hardware validation run caught a prior
        // version of this test using ONE 4-channel format for both, which is a genuine
        // format-mismatch on binding 33 (harmless here since probeGridEnabled=0 means
        // neither is ever actually read, but real UB per the Vulkan spec if it ever
        // were). Two formats, matching the shader exactly.
        const VkFormat kProbeIrrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;  // rgba16f, binding 32
        const VkFormat kProbeVisFmt = VK_FORMAT_R16G16_SFLOAT;        // rg16f,   binding 33
        VkImage probeIrrImg = VK_NULL_HANDLE, probeVisImg = VK_NULL_HANDLE;
        VkDeviceMemory probeIrrMem = VK_NULL_HANDLE, probeVisMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(4, 4, kProbeIrrFmt, probeIrrImg, probeIrrMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(4, 4, kProbeVisFmt, probeVisImg, probeVisMem));
        VkImageView probeIrrView = CreateView(probeIrrImg, kProbeIrrFmt);
        VkImageView probeVisView = CreateView(probeVisImg, kProbeVisFmt);
        ASSERT_NE(probeIrrView, VK_NULL_HANDLE); ASSERT_NE(probeVisView, VK_NULL_HANDLE);

        const ProbeGridConfigCpu probeGrid{};  // all-zero: probeGridEnabled=0 (byte-identity escape hatch)
        VkBuffer probeGridBuf = VK_NULL_HANDLE; VkDeviceMemory probeGridMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(ProbeGridConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, probeGridBuf, probeGridMem, false);
        UploadHostBuffer(probeGridMem, &probeGrid, sizeof(ProbeGridConfigCpu));

        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, idImg, idMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        VkImageView idView    = CreateView(idImg, kIdFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE); ASSERT_NE(idView, VK_NULL_HANDLE);

        // --- Pass 1: BodyInstanceRayMarch.comp. It initializes HitRecordBuffer and leaves
        // outputImage declared but unwritten; only HitRecordBuffer/idOutputImage are outputs. ---
        const std::vector<uint32_t> marchSpirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(marchSpirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo marchSmci{};
        marchSmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        marchSmci.codeSize = marchSpirv.size() * sizeof(uint32_t); marchSmci.pCode = marchSpirv.data();
        VkShaderModule marchModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &marchSmci, nullptr, &marchModule), VK_SUCCESS);

        VkShaderModuleCreateInfo shadeSmci{};
        shadeSmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shadeSmci.codeSize = shadeSpirv.size() * sizeof(uint32_t); shadeSmci.pCode = shadeSpirv.data();
        VkShaderModule shadeModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &shadeSmci, nullptr, &shadeModule), VK_SUCCESS);

        VkShaderModuleCreateInfo waveSmci{};
        waveSmci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        waveSmci.codeSize = waveSpirv.size() * sizeof(uint32_t); waveSmci.pCode = waveSpirv.data();
        VkShaderModule waveModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &waveSmci, nullptr, &waveModule), VK_SUCCESS);

        auto bind = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = t; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };

        // March's descriptor set layout: BodyInstanceRayMarch.comp's real reflected binding set,
        // matching test_hitrecord_readback.cpp's own layout EXACTLY (verified against the
        // shader's own layout(std430, binding=N) declarations, not against this file's own
        // previous (BROKEN) RenderScene binding table -- see that shader's own comments:
        // binding 16=OccupancyGridBuffer, 17=LightingConfigSSBO, 18=HitRecordBuffer,
        // 19=ShadowConfigSSBO. This traversal pass reads NEITHER LightingConfig nor
        // OccupancyGrid (declared but unread, KI-018) nor ShadowConfig -- 16/17/19 are
        // deliberately OMITTED here (test_hitrecord_readback.cpp's own comment: "was wrongly
        // 17" -- the SAME one-slot-shift bug this file's own prior RenderScene method had,
        // undetected because BodyInstanceRayMarch.comp stopped writing colour before this
        // test ever exercised its downstream shading path). Binding 18 is the ONLY one of
        // 16-19 the march's main() actually touches (hitRecords[idx] = rec).
        const std::array<VkDescriptorSetLayoutBinding, 16> marchBindings = {
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
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };
        VkDescriptorSetLayoutCreateInfo marchDslci{};
        marchDslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        marchDslci.bindingCount = static_cast<uint32_t>(marchBindings.size()); marchDslci.pBindings = marchBindings.data();
        VkDescriptorSetLayout marchDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &marchDslci, nullptr, &marchDsl), VK_SUCCESS);

        // Shade's descriptor set layout: SpatialReuseShade.comp's real reflected binding set
        // (BuildRenderGraph.cpp's spatialReuseGatherer wiring, see this method's own header
        // comment). Binding 9 (idOutputImage) is march-only -- SpatialReuseShade.comp never
        // declares it, so it's absent here, matching the "declare exactly what the shader
        // reflects" discipline this file's own binding-36 precedent (test_shadow_correctness's
        // sibling B1 tests) established.
        // Binding 35 (InstanceSkipMaskBuffer) is required too -- SpatialReuseShade.comp
        // #includes SceneBindings.glsl, same as every other consumer of that shared file.
        const std::array<VkDescriptorSetLayoutBinding, 27> shadeBindings = {
            bind(0,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(20, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(22, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(23, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(24, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(25, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(26, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(27, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(31, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(32, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(33, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(34, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };
        VkDescriptorSetLayoutCreateInfo shadeDslci{};
        shadeDslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        shadeDslci.bindingCount = static_cast<uint32_t>(shadeBindings.size()); shadeDslci.pBindings = shadeBindings.data();
        VkDescriptorSetLayout shadeDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &shadeDslci, nullptr, &shadeDsl), VK_SUCCESS);

        // ShadowVisibilityWave.comp's reflected storage-buffer bindings. It reads scene/light
        // inputs and the shadow config, and read/writes HitRecordBuffer at binding 17.
        const std::array<VkDescriptorSetLayoutBinding, 15> waveBindings = {
            bind(1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };
        VkDescriptorSetLayoutCreateInfo waveDslci{};
        waveDslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        waveDslci.bindingCount = static_cast<uint32_t>(waveBindings.size()); waveDslci.pBindings = waveBindings.data();
        VkDescriptorSetLayout waveDsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &waveDslci, nullptr, &waveDsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo marchPlci{};
        marchPlci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        marchPlci.setLayoutCount = 1; marchPlci.pSetLayouts = &marchDsl;
        marchPlci.pushConstantRangeCount = 1; marchPlci.pPushConstantRanges = &pcr;
        VkPipelineLayout marchPipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &marchPlci, nullptr, &marchPipelineLayout), VK_SUCCESS);

        VkPipelineLayoutCreateInfo shadePlci{};
        shadePlci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        shadePlci.setLayoutCount = 1; shadePlci.pSetLayouts = &shadeDsl;
        shadePlci.pushConstantRangeCount = 1; shadePlci.pPushConstantRanges = &pcr;
        VkPipelineLayout shadePipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &shadePlci, nullptr, &shadePipelineLayout), VK_SUCCESS);

        VkPipelineLayoutCreateInfo wavePlci{};
        wavePlci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        wavePlci.setLayoutCount = 1; wavePlci.pSetLayouts = &waveDsl;
        wavePlci.pushConstantRangeCount = 1; wavePlci.pPushConstantRanges = &pcr;
        VkPipelineLayout wavePipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &wavePlci, nullptr, &wavePipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo marchCpci{};
        marchCpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        marchCpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        marchCpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        marchCpci.stage.module = marchModule; marchCpci.stage.pName = "main";
        marchCpci.layout = marchPipelineLayout;
        VkPipeline marchPipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &marchCpci, nullptr, &marchPipeline), VK_SUCCESS);

        VkComputePipelineCreateInfo shadeCpci{};
        shadeCpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        shadeCpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shadeCpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shadeCpci.stage.module = shadeModule; shadeCpci.stage.pName = "main";
        shadeCpci.layout = shadePipelineLayout;
        VkPipeline shadePipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &shadeCpci, nullptr, &shadePipeline), VK_SUCCESS);

        VkComputePipelineCreateInfo waveCpci{};
        waveCpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        waveCpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        waveCpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        waveCpci.stage.module = waveModule; waveCpci.stage.pName = "main";
        waveCpci.layout = wavePipelineLayout;
        VkPipeline wavePipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &waveCpci, nullptr, &wavePipeline), VK_SUCCESS);

        // DIAG (temporary, root-causing the anyHitCount=0 regression): separate pools per set,
        // matching the plain RenderScene's own exact single-pool-single-set shape, in case a
        // shared pool across two differently-sized layouts confuses this non-conformant driver.
        const std::array<VkDescriptorPoolSize, 2> marchPoolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16},
        }};
        VkDescriptorPoolCreateInfo marchDpci{};
        marchDpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        marchDpci.maxSets = 1; marchDpci.poolSizeCount = static_cast<uint32_t>(marchPoolSizes.size()); marchDpci.pPoolSizes = marchPoolSizes.data();
        VkDescriptorPool marchDescPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &marchDpci, nullptr, &marchDescPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo marchDsai{};
        marchDsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        marchDsai.descriptorPool = marchDescPool; marchDsai.descriptorSetCount = 1; marchDsai.pSetLayouts = &marchDsl;
        VkDescriptorSet marchDescSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &marchDsai, &marchDescSet), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize, 2> shadePoolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  6},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 24},
        }};
        VkDescriptorPoolCreateInfo shadeDpci{};
        shadeDpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        shadeDpci.maxSets = 1; shadeDpci.poolSizeCount = static_cast<uint32_t>(shadePoolSizes.size()); shadeDpci.pPoolSizes = shadePoolSizes.data();
        VkDescriptorPool shadeDescPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &shadeDpci, nullptr, &shadeDescPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo shadeDsai{};
        shadeDsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        shadeDsai.descriptorPool = shadeDescPool; shadeDsai.descriptorSetCount = 1; shadeDsai.pSetLayouts = &shadeDsl;
        VkDescriptorSet shadeDescSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &shadeDsai, &shadeDescSet), VK_SUCCESS);

        const VkDescriptorPoolSize wavePoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 15};
        VkDescriptorPoolCreateInfo waveDpci{};
        waveDpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        waveDpci.maxSets = 1; waveDpci.poolSizeCount = 1; waveDpci.pPoolSizes = &wavePoolSize;
        VkDescriptorPool waveDescPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &waveDpci, nullptr, &waveDescPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo waveDsai{};
        waveDsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        waveDsai.descriptorPool = waveDescPool; waveDsai.descriptorSetCount = 1; waveDsai.pSetLayouts = &waveDsl;
        VkDescriptorSet waveDescSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &waveDsai, &waveDescSet), VK_SUCCESS);

        VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idInfo{VK_NULL_HANDLE, idView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesInfo{dummyNodes, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bricksInfo{dummyBricks, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo matsInfo{dummyMats, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo traceInfo{traceBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo configInfo{dummyConfig, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo counterInfo{counterBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo sdfInfo{dummySdf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lookupInfo{dummyLookup, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo mipInfo{dummyMip, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo iterInfo{dummyIter, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tierRefInfo{dummyTierRef, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lightInfo{lightBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo hitRecordInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo shadowInfo{shadowBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo skipMaskInfo{dummySkipMask, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo accumInfo{accumBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo historyInfo{VK_NULL_HANDLE, historyView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo prevCameraInfo{prevCameraBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo worldPosInfo{VK_NULL_HANDLE, worldPosView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo reservoirCfgInfo{reservoirCfgBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lightTreeInfo{lightTreeBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo reservoirAInfo{reservoirA, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo reservoirBInfo{reservoirB, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo spatialDebugInfo{spatialDebugBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo ddgiDebugInfo{ddgiDebugBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo probeIrrInfo{VK_NULL_HANDLE, probeIrrView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo probeVisInfo{VK_NULL_HANDLE, probeVisView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo probeGridInfo{probeGridBuf, 0, VK_WHOLE_SIZE};

        auto wImg = [&](VkDescriptorSet set, uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = set; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w2.pImageInfo = info;
            return w2;
        };
        auto wBuf = [&](VkDescriptorSet set, uint32_t b, VkDescriptorType t, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = set; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = t; w2.pBufferInfo = info;
            return w2;
        };

        const std::array<VkWriteDescriptorSet, 16> marchWrites = {
            wImg(marchDescSet, 0, &colorInfo),
            wBuf(marchDescSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(marchDescSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(marchDescSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(marchDescSet, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(marchDescSet, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(marchDescSet, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &counterInfo),
            wImg(marchDescSet, 9, &idInfo),
            wBuf(marchDescSet, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(marchDescSet, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(marchDescSet, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(marchDescSet, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(marchDescSet, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &iterInfo),
            wBuf(marchDescSet, 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),
            wBuf(marchDescSet, 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),  // HitRecordBuffer (real slot, not 17 -- see marchBindings' own comment)
            wBuf(marchDescSet, 35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &skipMaskInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(marchWrites.size()), marchWrites.data(), 0, nullptr);

        const std::array<VkWriteDescriptorSet, 27> shadeWrites = {
            wImg(shadeDescSet, 0, &colorInfo),
            wBuf(shadeDescSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(shadeDescSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(shadeDescSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(shadeDescSet, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(shadeDescSet, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(shadeDescSet, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(shadeDescSet, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(shadeDescSet, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(shadeDescSet, 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),
            wBuf(shadeDescSet, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightInfo),
            wBuf(shadeDescSet, 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),
            wBuf(shadeDescSet, 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &shadowInfo),
            wBuf(shadeDescSet, 19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &accumInfo),
            wImg(shadeDescSet, 20, &historyInfo),
            wBuf(shadeDescSet, 21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &prevCameraInfo),
            wImg(shadeDescSet, 22, &worldPosInfo),
            wBuf(shadeDescSet, 23, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &reservoirCfgInfo),
            wBuf(shadeDescSet, 24, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightTreeInfo),
            wBuf(shadeDescSet, 25, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &reservoirAInfo),
            wBuf(shadeDescSet, 26, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &reservoirBInfo),
            wBuf(shadeDescSet, 27, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &spatialDebugInfo),
            wBuf(shadeDescSet, 31, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ddgiDebugInfo),
            wImg(shadeDescSet, 32, &probeIrrInfo),
            wImg(shadeDescSet, 33, &probeVisInfo),
            wBuf(shadeDescSet, 34, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &probeGridInfo),
            wBuf(shadeDescSet, 35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &skipMaskInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(shadeWrites.size()), shadeWrites.data(), 0, nullptr);

        const std::array<VkWriteDescriptorSet, 15> waveWrites = {
            wBuf(waveDescSet, 1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(waveDescSet, 2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(waveDescSet, 3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(waveDescSet, 4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(waveDescSet, 5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(waveDescSet, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(waveDescSet, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(waveDescSet, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(waveDescSet, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(waveDescSet, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &iterInfo),
            wBuf(waveDescSet, 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),
            wBuf(waveDescSet, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightInfo),
            wBuf(waveDescSet, 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),
            wBuf(waveDescSet, 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &shadowInfo),
            wBuf(waveDescSet, 35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &skipMaskInfo),
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(waveWrites.size()), waveWrites.data(), 0, nullptr);

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
        barrierToGeneral(historyImg);
        barrierToGeneral(worldPosImg);
        barrierToGeneral(probeIrrImg);
        barrierToGeneral(probeVisImg);

        // Pass 1: march. Writes HitRecordBuffer (+ idOutputImage).
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, marchPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, marchPipelineLayout, 0, 1, &marchDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, marchPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        // Buffer barrier: march writes the record that the visibility wave will read and update.
        VkBufferMemoryBarrier hitRecordBarrier{};
        hitRecordBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hitRecordBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hitRecordBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        hitRecordBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.buffer = hitRecordBuf; hitRecordBarrier.offset = 0; hitRecordBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &hitRecordBarrier, 0, nullptr);

        // Pass 2: visibility wave. Reads scene/light inputs, traces the configured shadow rays,
        // and writes analytic visibility bits into HitRecordBuffer._pad0[2].
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wavePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wavePipelineLayout, 0, 1, &waveDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, wavePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w * h + 63) / 64, 1, 1);

        // Visibility wave write -> shade read.
        hitRecordBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hitRecordBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &hitRecordBarrier, 0, nullptr);

        // Pass 3: shade. Reads HitRecordBuffer, writes outputImage (colorImg).
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadePipelineLayout, 0, 1, &shadeDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, shadePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = colorImg; toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

        const VkDeviceSize rgbaSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer rgbaBuf = VK_NULL_HANDLE; VkDeviceMemory rgbaMem = VK_NULL_HANDLE;
        CreateHostBuffer(rgbaSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rgbaBuf, rgbaMem, false);
        VkBufferImageCopy colorCopy{};
        colorCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        colorCopy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rgbaBuf, 1, &colorCopy);

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

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rgbaBuf, nullptr); vkFreeMemory(logicalDevice_, rgbaMem, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, marchDescPool, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, shadeDescPool, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, waveDescPool, nullptr);
        vkDestroyPipeline(logicalDevice_, marchPipeline, nullptr);
        vkDestroyPipeline(logicalDevice_, shadePipeline, nullptr);
        vkDestroyPipeline(logicalDevice_, wavePipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, marchPipelineLayout, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, shadePipelineLayout, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, wavePipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, marchDsl, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, shadeDsl, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, waveDsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, marchModule, nullptr);
        vkDestroyShaderModule(logicalDevice_, shadeModule, nullptr);
        vkDestroyShaderModule(logicalDevice_, waveModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImageView(logicalDevice_, idView, nullptr);
        vkDestroyImageView(logicalDevice_, historyView, nullptr);
        vkDestroyImageView(logicalDevice_, worldPosView, nullptr);
        vkDestroyImageView(logicalDevice_, probeIrrView, nullptr);
        vkDestroyImageView(logicalDevice_, probeVisView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr); vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyImage(logicalDevice_, idImg, nullptr);    vkFreeMemory(logicalDevice_, idMem, nullptr);
        vkDestroyImage(logicalDevice_, historyImg, nullptr); vkFreeMemory(logicalDevice_, historyMem, nullptr);
        vkDestroyImage(logicalDevice_, worldPosImg, nullptr); vkFreeMemory(logicalDevice_, worldPosMem, nullptr);
        vkDestroyImage(logicalDevice_, probeIrrImg, nullptr); vkFreeMemory(logicalDevice_, probeIrrMem, nullptr);
        vkDestroyImage(logicalDevice_, probeVisImg, nullptr); vkFreeMemory(logicalDevice_, probeVisMem, nullptr);
        vkDestroyBuffer(logicalDevice_, traceBuf, nullptr);   vkFreeMemory(logicalDevice_, traceMem, nullptr);
        vkDestroyBuffer(logicalDevice_, counterBuf, nullptr); vkFreeMemory(logicalDevice_, counterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyNodes, nullptr);   vkFreeMemory(logicalDevice_, dummyNodesMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyBricks, nullptr);  vkFreeMemory(logicalDevice_, dummyBricksMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyMats, nullptr);    vkFreeMemory(logicalDevice_, dummyMatsMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyConfig, nullptr);  vkFreeMemory(logicalDevice_, dummyConfigMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummySdf, nullptr);     vkFreeMemory(logicalDevice_, dummySdfMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyLookup, nullptr);  vkFreeMemory(logicalDevice_, dummyLookupMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyMip, nullptr);     vkFreeMemory(logicalDevice_, dummyMipMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyTierRef, nullptr); vkFreeMemory(logicalDevice_, dummyTierRefMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyIter, nullptr);    vkFreeMemory(logicalDevice_, dummyIterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, instBuf, nullptr);      vkFreeMemory(logicalDevice_, instMem, nullptr);
        vkDestroyBuffer(logicalDevice_, lightBuf, nullptr);     vkFreeMemory(logicalDevice_, lightMem, nullptr);
        vkDestroyBuffer(logicalDevice_, hitRecordBuf, nullptr); vkFreeMemory(logicalDevice_, hitRecordMem, nullptr);
        vkDestroyBuffer(logicalDevice_, shadowBuf, nullptr);    vkFreeMemory(logicalDevice_, shadowMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummySkipMask, nullptr); vkFreeMemory(logicalDevice_, dummySkipMaskMem, nullptr);
        vkDestroyBuffer(logicalDevice_, accumBuf, nullptr); vkFreeMemory(logicalDevice_, accumMem, nullptr);
        vkDestroyBuffer(logicalDevice_, prevCameraBuf, nullptr); vkFreeMemory(logicalDevice_, prevCameraMem, nullptr);
        vkDestroyBuffer(logicalDevice_, reservoirCfgBuf, nullptr); vkFreeMemory(logicalDevice_, reservoirCfgMem, nullptr);
        vkDestroyBuffer(logicalDevice_, lightTreeBuf, nullptr); vkFreeMemory(logicalDevice_, lightTreeMem, nullptr);
        vkDestroyBuffer(logicalDevice_, reservoirA, nullptr); vkFreeMemory(logicalDevice_, reservoirAMem, nullptr);
        vkDestroyBuffer(logicalDevice_, reservoirB, nullptr); vkFreeMemory(logicalDevice_, reservoirBMem, nullptr);
        vkDestroyBuffer(logicalDevice_, spatialDebugBuf, nullptr); vkFreeMemory(logicalDevice_, spatialDebugMem, nullptr);
        vkDestroyBuffer(logicalDevice_, ddgiDebugBuf, nullptr); vkFreeMemory(logicalDevice_, ddgiDebugMem, nullptr);
        vkDestroyBuffer(logicalDevice_, probeGridBuf, nullptr); vkFreeMemory(logicalDevice_, probeGridMem, nullptr);
    }
};

namespace {

// A single directional light pointing "up and toward the camera" (matches Lighting.glsl's
// legacy hardcoded default direction convention: direction_or_position is the direction
// AWAY from the surface toward the light).
LightingConfigCpu MakeLighting(const glm::vec3& lightDir) {
    LightingConfigCpu cfg{};
    cfg.lightCount = 1u;
    cfg.ambientIntensity = 0.05f;  // low ambient so shadowed vs lit is visually decisive
    cfg.lights[0].directionX = lightDir.x;
    cfg.lights[0].directionY = lightDir.y;
    cfg.lights[0].directionZ = lightDir.z;
    cfg.lights[0].kind = 0u;  // directional
    cfg.lights[0].radianceX = 1.0f;
    cfg.lights[0].radianceY = 1.0f;
    cfg.lights[0].radianceZ = 1.0f;
    cfg.lights[0].range = 0.0f;
    return cfg;
}

ShadowConfigCpu MakeShadow(bool enabled) {
    ShadowConfigCpu cfg{};
    cfg.enabled = enabled ? 1u : 0u;
    cfg.raysPerLight = 1u;
    cfg.maxShadowDistance = 1000.0f;
    cfg.biasEpsilon = 0.02f;
    return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// The decisive test: an occluder sphere sits directly between a target sphere's
// camera-facing point and the light. With shadows enabled, that pixel must render
// dark (occluded classification agrees with the CPU reference); a second, unoccluded
// "litControl" sphere in the same frame must stay bright — proving the test isolates
// real occlusion, not a global darkening.
// ---------------------------------------------------------------------------
// @last-pass: 2026-09-01
TEST_F(ShadowCorrectnessTest, OccludedPixelMatchesCpuReferenceShadowRay) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    // Light comes from +Y (straight overhead) — an occluder placed directly above the
    // target's north pole casts a shadow onto that exact point, independent of camera
    // azimuth. Both target and litControl are on the ground plane; only target has an
    // occluder above it.
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 targetCenter(0.0f, 0.0f, 0.0f);
    const float targetRadius = 3.0f;
    const glm::vec3 occluderCenter(0.0f, targetRadius + 4.0f, 0.0f);  // directly above target
    const float occluderRadius = 2.0f;

    const glm::vec3 litControlCenter(12.0f, 0.0f, 0.0f);  // far to the side, no occluder above it
    const float litControlRadius = 3.0f;

    const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeProceduralSphere(targetCenter, targetRadius, 0.9f, 0.9f, 0.9f),        // slot 0: target
        MakeProceduralSphere(occluderCenter, occluderRadius, 0.2f, 0.2f, 0.2f),    // slot 1: occluder
        MakeProceduralSphere(litControlCenter, litControlRadius, 0.9f, 0.9f, 0.9f),// slot 2: control
    };

    // GEOMETRY FIX (2026-08-03): the camera used to sit at surfacePoint+(0,0,20) aimed at the
    // sphere CENTER -- but "aim at center from a point offset along the surface normal" only
    // actually LANDS on that surface point when the offset axis and the normal axis are the
    // SAME axis. Here they were perpendicular (offset along +Z, normal along +Y for target /
    // +Z for litControl's OWN offset, but computed independently of where the ray actually
    // lands) -- so neither dispatch ever sampled anywhere near its claimed "north pole"; the
    // real ray-sphere hit was NdotL~0.15 (target) / NdotL~0.0 (litControl) against an
    // overhead light, never the "nearly head-on" this test's own assertions assume. Fixed by
    // deriving the camera from a 30-degree-off-+Y offset (between +Y and +Z) for BOTH spheres:
    // verified independently (ray-sphere + ray-vs-occluder analytic checks) this keeps the
    // camera's own sightline clear of the occluder (occluder centered directly above target,
    // only ~30 degrees off +Y at its 2-unit radius), keeps target'S SHADOW ray genuinely
    // occluded (straight up from the near-+Y-facing hit point still passes through the
    // occluder), keeps litControl genuinely UNoccluded, and gets NdotL~0.87 for both --
    // strongly lit, not grazing. The hit point is computed via the SAME analytic ray-sphere
    // formula RaySphereHit uses (not assumed), so ReferenceOccluded below checks the EXACT
    // point the camera ray actually lands on, not a different unreachable point.
    auto rayHitPoint = [](const glm::vec3& origin, const glm::vec3& dir,
                          const glm::vec3& center, float radius) {
        const glm::vec3 oc = origin - center;
        const float b = glm::dot(oc, dir);
        const float c = glm::dot(oc, oc) - radius * radius;
        const float t = -b - std::sqrt(b * b - c);  // near intersection (b*b-c > 0 by construction)
        return origin + dir * t;
    };
    constexpr float kCameraAngleDeg = 30.0f;  // between +Y (light) and +Z; see comment above
    const glm::vec3 cameraOffsetDir = glm::normalize(glm::vec3(
        0.0f, std::cos(glm::radians(kCameraAngleDeg)), std::sin(glm::radians(kCameraAngleDeg))));

    const glm::vec3 eyeTarget = targetCenter + cameraOffsetDir * 20.0f;
    const glm::vec3 dirTarget = glm::normalize(targetCenter - eyeTarget);
    const glm::vec3 targetSurfacePoint = rayHitPoint(eyeTarget, dirTarget, targetCenter, targetRadius);
    const glm::vec3 targetNormal = glm::normalize(targetSurfacePoint - targetCenter);

    const glm::vec3 eyeLit = litControlCenter + cameraOffsetDir * 20.0f;
    const glm::vec3 dirLit = glm::normalize(litControlCenter - eyeLit);
    const glm::vec3 litSurfacePoint = rayHitPoint(eyeLit, dirLit, litControlCenter, litControlRadius);
    const glm::vec3 litNormal = glm::normalize(litSurfacePoint - litControlCenter);

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    auto makeCamera = [&](const glm::vec3& eye, const glm::vec3& dir, int32_t instanceCount) {
        glm::vec3 up = worldUp;
        glm::vec3 right = glm::cross(dir, up);
        if (glm::length(right) < 1e-4f) { up = glm::vec3(0.0f, 0.0f, 1.0f); right = glm::cross(dir, up); }
        right = glm::normalize(right);
        up = glm::normalize(glm::cross(right, dir));
        PushConstants pc{};
        pc.cameraPos = eye; pc.time = 0.0f;
        pc.cameraDir = dir; pc.fov = 1.0f;  // narrow FOV: dead-center pixel maps ~exactly to the surface point
        pc.cameraUp = up; pc.aspect = 1.0f;
        pc.cameraRight = right; pc.debugMode = 0;
        pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
        pc.instanceCount = instanceCount;
        return pc;
    };

    constexpr uint32_t kW = 8, kH = 8;  // small; only the dead-center pixel is checked
    const uint32_t centerX = kW / 2, centerY = kH / 2;

    const LightingConfigCpu lighting = MakeLighting(lightDir);

    // --- Reference: independent CPU ray-vs-sphere occlusion check ---
    const std::vector<glm::vec3> centers = {targetCenter, occluderCenter, litControlCenter};
    const std::vector<float> radii = {targetRadius, occluderRadius, litControlRadius};
    const float biasEps = 0.02f, maxDist = 1000.0f;

    const bool targetShouldBeOccluded =
        ReferenceOccluded(targetSurfacePoint, targetNormal, lightDir, biasEps, maxDist,
                          centers, radii, /*skipIndex=*/0);
    const bool litShouldBeOccluded =
        ReferenceOccluded(litSurfacePoint, litNormal, lightDir, biasEps, maxDist,
                          centers, radii, /*skipIndex=*/2);

    ASSERT_TRUE(targetShouldBeOccluded) << "test setup bug: the reference itself says target is NOT occluded";
    ASSERT_FALSE(litShouldBeOccluded) << "test setup bug: the reference itself says litControl IS occluded";

    // --- Real shader dispatch: shadows ENABLED ---
    // RenderSceneShaded (not the plain RenderScene): the march alone no longer writes colour
    // (see this file's ROOT CAUSE comment up top) — dispatch march -> ShadowVisibilityWave.comp
    // -> SpatialReuseShade.comp, the real current production chain, and read colour back from
    // the shade pass.
    const ShadowConfigCpu shadowOn = MakeShadow(true);
    std::vector<uint8_t> rgbaTarget, rgbaLit;
    ASSERT_NO_FATAL_FAILURE(RenderSceneShaded(instances, lighting, shadowOn,
        makeCamera(eyeTarget, dirTarget, static_cast<int32_t>(instances.size())), kW, kH, rgbaTarget));
    ASSERT_NO_FATAL_FAILURE(RenderSceneShaded(instances, lighting, shadowOn,
        makeCamera(eyeLit, dirLit, static_cast<int32_t>(instances.size())), kW, kH, rgbaLit));

    auto luminance = [&](const std::vector<uint8_t>& rgba, uint32_t x, uint32_t y, uint32_t w) {
        const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
        return (static_cast<int>(rgba[idx + 0]) + rgba[idx + 1] + rgba[idx + 2]) / 3;
    };

    const int targetLuma = luminance(rgbaTarget, centerX, centerY, kW);
    const int litLuma    = luminance(rgbaLit, centerX, centerY, kW);
    std::printf("[SHADOW-CORRECTNESS] target(occluded expected) luma=%d | litControl(unoccluded expected) luma=%d\n",
                targetLuma, litLuma);

    // The occluded target's dead-center pixel must be substantially darker than the
    // unoccluded control's (ambient-only vs ambient+direct) — the shader agreeing with
    // the independent CPU reference's occlusion classification.
    EXPECT_LT(targetLuma, litLuma - 20)
        << "occluded target pixel is not meaningfully darker than the unoccluded control; "
           "shader's shadow classification disagrees with the CPU reference";

    // Sanity: the occluded pixel is still non-black (ambient term survives) and the lit
    // pixel is bright (direct light landed, matching evalBRDF*radiance*NdotL — near
    // dead-center the light is nearly head-on for both spheres' facing normals).
    EXPECT_GT(targetLuma, 0) << "occluded pixel is pure black — ambient term missing";
    EXPECT_GT(litLuma, 60) << "unoccluded control pixel is too dark for a direct hit";

    // --- Control: shadows DISABLED — the target pixel must brighten back up (same
    // unshadowed direct term the litControl pixel already gets), proving the darkening
    // above was caused by the shadow term, not some other scene difference. ---
    const ShadowConfigCpu shadowOff = MakeShadow(false);
    std::vector<uint8_t> rgbaTargetNoShadow;
    ASSERT_NO_FATAL_FAILURE(RenderSceneShaded(instances, lighting, shadowOff,
        makeCamera(eyeTarget, dirTarget, static_cast<int32_t>(instances.size())), kW, kH, rgbaTargetNoShadow));
    const int targetLumaNoShadow = luminance(rgbaTargetNoShadow, centerX, centerY, kW);
    std::printf("[SHADOW-CORRECTNESS] target with shadows DISABLED luma=%d\n", targetLumaNoShadow);
    EXPECT_GT(targetLumaNoShadow, targetLuma + 20)
        << "disabling shadows did not brighten the previously-occluded pixel";
}
