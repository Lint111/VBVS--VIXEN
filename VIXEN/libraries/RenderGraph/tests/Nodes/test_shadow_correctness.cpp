/**
 * @file test_shadow_correctness.cpp
 * @brief Sampled Lighting Inc1 M4 gate: exercises the REAL TraceWorldShadow via a real
 *        dispatch of BodyInstanceRayMarch.comp against a known scene + known directional
 *        light, and asserts a pixel's occlusion state (as read from the shaded colour
 *        output) matches an independent CPU-traced reference shadow ray. This closes
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
 * test_hitrecord_readback.cpp, extended with bindings 16 (LightingConfig), 17 (HitRecord,
 * unused for assertions here but must be bound — the shader always writes it), and 18
 * (ShadowConfig).
 *
 * Run: ./test_shadow_correctness
 */

#include <gtest/gtest.h>

#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

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

    // Dispatches the real shader at w*h with the full binding set (0-18, minus 6/7 which
    // don't exist), including LightingConfig (16) and ShadowConfig (18), and reads back
    // the shaded colour image.
    void RenderScene(const std::vector<Vixen::SVO::BodyInstanceGpu>& instances,
                     const LightingConfigCpu& lighting, const ShadowConfigCpu& shadow,
                     const PushConstants& pc, uint32_t w, uint32_t h,
                     std::vector<uint8_t>& outRgba) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";

        // Baked-perf-pipeline M2: RayTraceBuffer (binding 4) is real, non-placeholder -- see
        // test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation.
        constexpr VkDeviceSize kRayTraceBufferSize = 16 /*header*/ + 256 /*slots*/ * (16 + 64 * 48) /*TRACE_RAY_SIZE*/;
        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, counterBuf, counterMem, true);

        // Procedural-only scene: octree/brick/materials/config/sdf/lookup/mip/tierref/iter
        // buffers are all unused by any instance (providerKind=1) but must still be bound
        // (glslc reflects the full binding set regardless of runtime provider path).
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

        // Binding 10: BodyInstanceBuffer (real content).
        const VkDeviceSize instBufSize = static_cast<VkDeviceSize>(instances.size()) * sizeof(Vixen::SVO::BodyInstanceGpu);
        VkBuffer instBuf = VK_NULL_HANDLE; VkDeviceMemory instMem = VK_NULL_HANDLE;
        CreateHostBuffer(instBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf, instMem, false);
        UploadHostBuffer(instMem, instances.data(), instBufSize);

        // Binding 16: LightingConfigSSBO.
        VkBuffer lightBuf = VK_NULL_HANDLE; VkDeviceMemory lightMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(LightingConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lightBuf, lightMem, false);
        UploadHostBuffer(lightMem, &lighting, sizeof(LightingConfigCpu));

        // Binding 17: HitRecordBuffer (written by the shader; not asserted here directly).
        const VkDeviceSize hitRecordSize = static_cast<VkDeviceSize>(w) * h * 64;  // sizeof(HitRecord)=64
        VkBuffer hitRecordBuf = VK_NULL_HANDLE; VkDeviceMemory hitRecordMem = VK_NULL_HANDLE;
        CreateHostBuffer(hitRecordSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);

        // Binding 18: ShadowConfigSSBO.
        VkBuffer shadowBuf = VK_NULL_HANDLE; VkDeviceMemory shadowMem = VK_NULL_HANDLE;
        CreateHostBuffer(sizeof(ShadowConfigCpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, shadowBuf, shadowMem, false);
        UploadHostBuffer(shadowMem, &shadow, sizeof(ShadowConfigCpu));

        // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer (binding 35) placeholder.
        VkBuffer dummySkipMask = VK_NULL_HANDLE; VkDeviceMemory dummySkipMaskMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySkipMask, dummySkipMaskMem, true);

        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, idImg, idMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        VkImageView idView    = CreateView(idImg, kIdFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE); ASSERT_NE(idView, VK_NULL_HANDLE);

        const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        auto bind = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = t; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };
        const std::array<VkDescriptorSetLayoutBinding, 18> bindings = {
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
            bind(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer
        };
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);
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
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16},
        }};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); dpci.pPoolSizes = poolSizes.data();
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

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
        const std::array<VkWriteDescriptorSet, 18> writes = {
            wImg(0, &colorInfo),
            wBuf(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &counterInfo),
            wImg(9, &idInfo),
            wBuf(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &iterInfo),
            wBuf(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),
            wBuf(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightInfo),
            wBuf(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),
            wBuf(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &shadowInfo),
            wBuf(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &skipMaskInfo),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

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

    // Camera looks straight down -Z at the target's north pole (the point directly under
    // the occluder) — the surface point rendered at dead-center UV is exactly
    // targetCenter + (0, targetRadius, 0) (before any displacement, which is 0 for a
    // pure sphere recipe/amp=0), the point ReferenceOccluded checks below.
    const glm::vec3 targetSurfacePoint = targetCenter + glm::vec3(0.0f, targetRadius, 0.0f);
    const glm::vec3 targetNormal = glm::normalize(targetSurfacePoint - targetCenter);
    const glm::vec3 eyeTarget = targetSurfacePoint + glm::vec3(0.0f, 0.0f, 20.0f);
    const glm::vec3 dirTarget = glm::normalize(targetCenter - eyeTarget);

    // litControl's camera-facing point (its own north-pole-toward-camera point, i.e.
    // +Z point since the camera looks down -Z at it too in this test's SEPARATE dispatch).
    const glm::vec3 litSurfacePoint = litControlCenter + glm::vec3(0.0f, 0.0f, litControlRadius);
    const glm::vec3 litNormal = glm::normalize(litSurfacePoint - litControlCenter);
    const glm::vec3 eyeLit = litSurfacePoint + glm::vec3(0.0f, 0.0f, 20.0f);
    const glm::vec3 dirLit = glm::normalize(litControlCenter - eyeLit);

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
    const ShadowConfigCpu shadowOn = MakeShadow(true);
    std::vector<uint8_t> rgbaTarget, rgbaLit;
    ASSERT_NO_FATAL_FAILURE(RenderScene(instances, lighting, shadowOn,
        makeCamera(eyeTarget, dirTarget, static_cast<int32_t>(instances.size())), kW, kH, rgbaTarget));
    ASSERT_NO_FATAL_FAILURE(RenderScene(instances, lighting, shadowOn,
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
    ASSERT_NO_FATAL_FAILURE(RenderScene(instances, lighting, shadowOff,
        makeCamera(eyeTarget, dirTarget, static_cast<int32_t>(instances.size())), kW, kH, rgbaTargetNoShadow));
    const int targetLumaNoShadow = luminance(rgbaTargetNoShadow, centerX, centerY, kW);
    std::printf("[SHADOW-CORRECTNESS] target with shadows DISABLED luma=%d\n", targetLumaNoShadow);
    EXPECT_GT(targetLumaNoShadow, targetLuma + 20)
        << "disabling shadows did not brighten the previously-occluded pixel";
}
