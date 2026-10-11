/**
 * @file test_shell_revalidate_node.cpp
 * @brief Surface-Shell ESVO cache — GPU dispatch live-gate + zero-barrier proof.
 *
 * Three things this test proves, on a REAL device:
 *
 *  (a) GPU-vs-CPU classification parity: bakes a sphere into a Stored-SDF octree,
 *      derives the CPU oracle (Vixen::SVO::DeriveShell), then dispatches the SHIPPED
 *      shaders/ShellDerive.comp (mode 0 = surface classify, mode 1 x shellDilation =
 *      dilation) against the SAME source buffers via ShellRevalidateNode, reads back
 *      shellFlags[], and asserts the SURFACE and SHELL bits match the CPU oracle for
 *      EVERY source brick.
 *
 *  (b) Zero-barrier generation isolation: assembles a real PassGroupNode from two
 *      ComputePassSteps — a "shell read" pass (reads the published generation's data+lookup)
 *      and a "revalidate" pass (this node's dispatch, which writes a DISTINCT candidate
 *      generation Resource) — and asserts PassGroupNode's baked
 *      intraSchedule_ contains ZERO entry barriers between the two groups, because the
 *      passes touch disjoint Resource objects (the FrameSyncScheduler hazard-tracks by
 *      Resource* pointer identity, not by binding number).
 *
 *  (c) Edit-generation coherence: a value edit derives a complete candidate off-thread,
 *      then publishes its shell and proxy AABB buffers together after the safe point.
 *
 * DEVICE SELECTION: identical contract to test_body_octree_lifetime.cpp / test_recipe_
 * pool_render.cpp — via VixenSelectWslGpuIcd(), a real discrete/integrated GPU is
 * PREFERRED, with software (lavapipe/llvmpipe) or Dozen used only as a fallback when no
 * real GPU is visible. Some usable device is still hard-asserted before any vkQueueSubmit.
 */

#include <gtest/gtest.h>

#include "Nodes/ShellRevalidateNode.h"
#include "Data/Nodes/ShellRevalidateNodeConfig.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Nodes/PassGroupNode.h"
#include "Data/PassStep.h"
#include "Core/PassGroupSchedule.h"   // BuildPassGroupSchedule
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource
#include "Core/NodeContext.h"          // CleanupReason
#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "MainCacher.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "ShellDerive.h"
#include "SdfBake.h"
#include "SdfRecipes.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifndef SHELLDERIVE_SPV
#error "SHELLDERIVE_SPV (path to compiled ShellDerive.spv) must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

constexpr uint32_t SURFACE_BIT  = 1u;
constexpr uint32_t SHELL_BIT    = 2u;

// ---------------------------------------------------------------------------
// Minimal Vulkan fixture — mirrors test_recipe_pool_render.cpp's RecipePoolRenderTest
// (no debug-utils messenger; validation layer enabled when the SDK is present, but not
// required to run — same as the sibling render-gate tests).
// ---------------------------------------------------------------------------
class ShellRevalidateNodeTest : public ::testing::Test, public ::testing::WithParamInterface<uint32_t> {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             deviceConfirmed_ = false;
    std::string      selectedDeviceName_;
    std::unique_ptr<VulkanDevice> deviceShell_;

    // Real discrete/integrated GPUs are now PREFERRED; software/Dozen is only a
    // fallback when no real GPU is visible.
    static bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
        return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    static bool IsAcceptableDevice(const VkPhysicalDeviceProperties& p) {
        std::string n(p.deviceName);
        for (char& c : n) c = static_cast<char>(::tolower(c));
        const bool isSoftware =
            (n.find("llvmpipe") != std::string::npos || n.find("lavapipe") != std::string::npos) &&
            p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = n.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_shell_revalidate_node";
        ai.apiVersion = VK_API_VERSION_1_2;
        const auto layers = EnabledValidationLayers();
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
        ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        ASSERT_EQ(vkCreateInstance(&ci, nullptr, &instance_), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";
        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());
        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
        // ShellRevalidateNode::ExecuteImpl submits via vulkanDevice->queue (mirrors
        // ComputeDispatchNode's real-engine contract, where VulkanDevice::queue is always
        // populated at real device creation) — the harness must set it explicitly since
        // VulkanDevice's ctor does not query a queue itself.
        deviceShell_->queue = queue_;
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        if (logicalDevice_ != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    void PickPhysicalDevice() {
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
                physicalDevice_ = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true;
                return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsAcceptableDevice(props)) {
                physicalDevice_ = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true;
                return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found) << "No compute queue family on the selected device";
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = queueFamily_;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        VkDeviceCreateInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &di, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &pi, nullptr, &commandPool_), VK_SUCCESS);
    }

    template <typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        return UINT32_MAX;
    }

    // Host-visible buffer, optionally seeded with `data` (nullptr => zero-fill).
    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                          VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &buf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, buf, mem, 0), VK_SUCCESS);
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        if (data) std::memcpy(mapped, data, static_cast<size_t>(size));
        else std::memset(mapped, 0, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }
};

// ---------------------------------------------------------------------------
// (a) GPU-vs-CPU classification parity.
// ---------------------------------------------------------------------------
TEST_P(ShellRevalidateNodeTest, GpuDispatchMatchesCanonicalFlagsAndCompactedBytes) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());

    // ---- Bake a sphere (same fixture shape as test_shell_derive.cpp's ShellFixture) ----
    using namespace Vixen::SVO;
    const int n = 64;
    const float r = 24.0f;
    const glm::vec3 center{32.0f, 32.0f, 32.0f};
    RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
    SdfBodyOctree body = BuildSdfBodyOctree(baked, 3);
    std::vector<const SdfBodyOctree*> octs{&body};
    ConcatenatedOctrees cat = ConcatenateSdf(octs);

    ASSERT_GT(cat.brickCounts[0], 0u);
    ASSERT_FALSE(cat.channelPool.empty());

    const uint32_t shellDilation = GetParam();
    ShellDeriveResult oracle = DeriveShell(cat, 0, ShellDeriveParams{shellDilation});
    ASSERT_GT(oracle.surfaceBrickCount, 0u) << "a sphere must have surface bricks";

    const OctreeConfig& cfg0 = cat.configs[0];
    const uint32_t brickCount = cat.brickCounts[0];
    const uint32_t bpa        = static_cast<uint32_t>(cfg0.bricksPerAxis);

    // ---- Upload the 3 source buffers ShellDerive.comp reads (bindings 0,1,3) ----
    VkBuffer sourcePoolBuf = VK_NULL_HANDLE, brickLookupBuf = VK_NULL_HANDLE, configBuf = VK_NULL_HANDLE;
    VkDeviceMemory sourcePoolMem = VK_NULL_HANDLE, brickLookupMem = VK_NULL_HANDLE, configMem = VK_NULL_HANDLE;

    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        cat.channelPool.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        cat.channelPool.data(), sourcePoolBuf, sourcePoolMem));
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        cat.brickGridLookup.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        cat.brickGridLookup.data(), brickLookupBuf, brickLookupMem));
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        sizeof(OctreeConfig), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &cfg0, configBuf, configMem));

    // ---- Bring up the node and dispatch ----
    using C = ShellRevalidateNodeConfig;
    ShellRevalidateNodeType nodeType("ShellRevalidate");
    auto nodeBase = nodeType.CreateInstance("shell_revalidate_parity_test");
    auto* node = dynamic_cast<ShellRevalidateNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);
    node->SetParameter(C::PARAM_SPIRV_PATH, std::string(SHELLDERIVE_SPV));

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource srcRes;     SetHandleVal<VkBuffer>(srcRes, sourcePoolBuf);
    Resource lookupRes;  SetHandleVal<VkBuffer>(lookupRes, brickLookupBuf);
    Resource cfgRes;     SetHandleVal<VkBuffer>(cfgRes, configBuf);
    Resource brickCountRes;    SetHandleVal<uint32_t>(brickCountRes, brickCount);
    Resource bpaRes;           SetHandleVal<uint32_t>(bpaRes, bpa);
    Resource dilationRes;      SetHandleVal<uint32_t>(dilationRes, shellDilation);

    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,     0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,         0, &poolRes);
    node->SetInput(C::SOURCE_POOL_BUFFER_Slot::index,   0, &srcRes);
    node->SetInput(C::BRICK_LOOKUP_BUFFER_Slot::index,  0, &lookupRes);
    node->SetInput(C::CONFIG_BUFFER_Slot::index,        0, &cfgRes);
    node->SetInput(C::BRICK_COUNT_Slot::index,          0, &brickCountRes);
    node->SetInput(C::BRICKS_PER_AXIS_Slot::index,      0, &bpaRes);
    node->SetInput(C::SHELL_DILATION_Slot::index,       0, &dilationRes);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ASSERT_NO_THROW(node->Execute());

    Resource* flagsOut = node->GetOutput(C::SHELL_FLAGS_BUFFER_Slot::index, 0);
    ASSERT_NE(flagsOut, nullptr);
    VkBuffer shellFlagsBuf = flagsOut->GetHandle<VkBuffer>();
    ASSERT_NE(shellFlagsBuf, VK_NULL_HANDLE);

    // ---- Read back shellFlags[] ----
    // ShellRevalidateNode owns the VkDeviceMemory behind shellFlagsBuf privately (matching
    // every other node's encapsulation — it only publishes the VkBuffer handle), so we can't
    // vkMapMemory it directly here. Instead, copy it to a host-visible buffer we DO own the
    // memory for, then map that.
    const VkDeviceSize flagsSize = static_cast<VkDeviceSize>(brickCount) * sizeof(uint32_t);
    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        flagsSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr, readbackBuf, readbackMem));

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer copyCmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &copyCmd), VK_SUCCESS);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ASSERT_EQ(vkBeginCommandBuffer(copyCmd, &bi), VK_SUCCESS);
    VkBufferCopy region{0, 0, flagsSize};
    vkCmdCopyBuffer(copyCmd, shellFlagsBuf, readbackBuf, 1, &region);
    ASSERT_EQ(vkEndCommandBuffer(copyCmd), VK_SUCCESS);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &copyCmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    std::vector<uint32_t> shellFlags(brickCount, 0u);
    void* mapped = nullptr;
    ASSERT_EQ(vkMapMemory(logicalDevice_, readbackMem, 0, flagsSize, 0, &mapped), VK_SUCCESS);
    std::memcpy(shellFlags.data(), mapped, static_cast<size_t>(flagsSize));
    vkUnmapMemory(logicalDevice_, readbackMem);

    // ---- Compare GPU classification against the CPU oracle for EVERY brick ----
    uint32_t surfaceMismatches = 0, shellMismatches = 0;
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        const bool gpuSurface = (shellFlags[bi] & SURFACE_BIT) != 0u;
        const bool gpuShell   = (shellFlags[bi] & SHELL_BIT) != 0u;
        const bool cpuSurface = oracle.surface[bi] != 0u;
        const bool cpuShell   = oracle.shell[bi] != 0u;
        if (gpuSurface != cpuSurface) {
            ++surfaceMismatches;
            ADD_FAILURE() << "SURFACE mismatch at brick " << bi
                          << ": gpu=" << gpuSurface << " cpu=" << cpuSurface;
        }
        if (gpuShell != cpuShell) {
            ++shellMismatches;
            ADD_FAILURE() << "SHELL mismatch at brick " << bi
                          << ": gpu=" << gpuShell << " cpu=" << cpuShell;
        }
    }
    std::printf("[ shell parity ] brickCount=%u surfaceBricks(cpu)=%u shellBricks(cpu)=%u "
                "surfaceMismatches=%u shellMismatches=%u\n",
                brickCount, oracle.surfaceBrickCount, oracle.shellBrickCount,
                surfaceMismatches, shellMismatches);
    EXPECT_EQ(surfaceMismatches, 0u);
    EXPECT_EQ(shellMismatches, 0u);

    // FRONTIER is observable in this node's flags contract, but not consumed by
    // the production cache. Define it as the canonical final layer's new bricks.
    const uint32_t effectiveLayers = std::max(1u, shellDilation);
    const auto previousShell = effectiveLayers == 1 ? oracle.surface
        : DeriveShell(cat, 0, ShellDeriveParams{effectiveLayers - 1}).shell;
    std::vector<uint32_t> expectedFlags(brickCount);
    std::vector<uint32_t> gpuLookup;
    std::vector<uint8_t> gpuData;
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        expectedFlags[bi] = (oracle.surface[bi] ? 1u : 0u) | (oracle.shell[bi] ? 2u : 0u)
            | (oracle.shell[bi] && !previousShell[bi] ? 4u : 0u);
        if ((shellFlags[bi] & SHELL_BIT) == 0) continue;
        gpuLookup.push_back(bi);
        const size_t offset = (cfg0.poolBrickBase + bi * cfg0.brickStrideFloats) * sizeof(float);
        const auto* begin = reinterpret_cast<const uint8_t*>(cat.channelPool.data()) + offset;
        gpuData.insert(gpuData.end(), begin, begin + cfg0.brickStrideFloats * sizeof(float));
    }
    EXPECT_EQ(shellFlags, expectedFlags); // all 32 bits, no masked parity claim
    EXPECT_EQ(gpuLookup, oracle.shellLookup);
    ASSERT_EQ(gpuData.size(), oracle.shellData.size());
    EXPECT_EQ(std::memcmp(gpuData.data(), oracle.shellData.data(), gpuData.size()), 0);

    // Cleanup (device teardown happens in TearDown; free what we own here).
    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    vkFreeCommandBuffers(logicalDevice_, commandPool_, 1, &copyCmd);
    vkDestroyBuffer(logicalDevice_, readbackBuf, nullptr); vkFreeMemory(logicalDevice_, readbackMem, nullptr);
    vkDestroyBuffer(logicalDevice_, sourcePoolBuf, nullptr); vkFreeMemory(logicalDevice_, sourcePoolMem, nullptr);
    vkDestroyBuffer(logicalDevice_, brickLookupBuf, nullptr); vkFreeMemory(logicalDevice_, brickLookupMem, nullptr);
    vkDestroyBuffer(logicalDevice_, configBuf, nullptr); vkFreeMemory(logicalDevice_, configMem, nullptr);
}

INSTANTIATE_TEST_SUITE_P(CanonicalDilation, ShellRevalidateNodeTest, ::testing::Values(0u, 1u, 2u, 3u));

// ---------------------------------------------------------------------------
// (b) Zero-barrier published/candidate generation isolation via a REAL PassGroupNode.
//
// Assembles two ComputePassSteps carrying REAL pipeline/descriptor-set handles (this
// node's own, reused for both — only the `accesses` Resource* identity matters to the
// scheduler) whose `accesses` declare DISJOINT Resource objects: a "shell read" pass
// touching published-generation Resources, and a "revalidate" pass touching candidate
// generation Resources (mirroring the disjoint handles before and after publication).
// BuildPassGroupSchedule is exercised directly (it is the exact
// function PassGroupNode::CompileImpl calls) to bake intraSchedule_ and assert it has
// ZERO entry barriers between the two pass groups.
// ---------------------------------------------------------------------------
TEST_F(ShellRevalidateNodeTest, DisjointShellGenerationPassesGetZeroBarriers) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());

    // ---- Bring up a real ShellRevalidateNode (Compile only — we need a real pipeline/
    //      descriptor set to populate ComputePassStep with, proving these are REAL Vulkan
    //      handles, not placeholders). A minimal 8-brick synthetic octree keeps this fast. ----
    using namespace Vixen::SVO;
    const uint32_t bpa = 2;
    const uint32_t brickCount = bpa * bpa * bpa;  // 8
    const uint32_t stride = SerializedOctree::kVoxelsPerBrick;  // SDF-only, 1 channel

    std::vector<float> pool(static_cast<size_t>(brickCount) * stride, -100.0f);
    // Make brick 0 a surface brick (crosses the iso-surface) so the dispatch does real work.
    for (uint32_t v = 0; v < stride; ++v) pool[v] = (v % 2 == 0) ? 5.0f : -5.0f;
    std::vector<uint32_t> lookup(brickCount);
    for (uint32_t i = 0; i < brickCount; ++i) lookup[i] = i;

    OctreeConfig cfg{};
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.brickSize = 8;
    cfg.bricksPerAxis = static_cast<int32_t>(bpa);
    cfg.poolBrickBase = 0;
    cfg.channelCount = 1;
    cfg.brickStrideFloats = stride;
    cfg.channels[0].semanticId = static_cast<uint32_t>(SEM_SDF);
    cfg.channels[0].elemCount = 1;
    cfg.channels[0].channelBaseFloats = 0;

    VkBuffer sourcePoolBuf = VK_NULL_HANDLE, brickLookupBuf = VK_NULL_HANDLE, configBuf = VK_NULL_HANDLE;
    VkDeviceMemory sourcePoolMem = VK_NULL_HANDLE, brickLookupMem = VK_NULL_HANDLE, configMem = VK_NULL_HANDLE;
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        pool.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pool.data(),
        sourcePoolBuf, sourcePoolMem));
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        lookup.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lookup.data(),
        brickLookupBuf, brickLookupMem));
    ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(
        sizeof(OctreeConfig), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &cfg, configBuf, configMem));

    using C = ShellRevalidateNodeConfig;
    ShellRevalidateNodeType nodeType("ShellRevalidate");
    auto nodeBase = nodeType.CreateInstance("shell_revalidate_barrier_test");
    auto* node = dynamic_cast<ShellRevalidateNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);
    node->SetParameter(C::PARAM_SPIRV_PATH, std::string(SHELLDERIVE_SPV));

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource srcRes;     SetHandleVal<VkBuffer>(srcRes, sourcePoolBuf);
    Resource lookupRes;  SetHandleVal<VkBuffer>(lookupRes, brickLookupBuf);
    Resource cfgRes;     SetHandleVal<VkBuffer>(cfgRes, configBuf);
    Resource brickCountRes;    SetHandleVal<uint32_t>(brickCountRes, brickCount);
    Resource bpaRes;           SetHandleVal<uint32_t>(bpaRes, bpa);
    Resource dilationRes;      SetHandleVal<uint32_t>(dilationRes, 1u);

    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,     0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,         0, &poolRes);
    node->SetInput(C::SOURCE_POOL_BUFFER_Slot::index,   0, &srcRes);
    node->SetInput(C::BRICK_LOOKUP_BUFFER_Slot::index,  0, &lookupRes);
    node->SetInput(C::CONFIG_BUFFER_Slot::index,        0, &cfgRes);
    node->SetInput(C::BRICK_COUNT_Slot::index,          0, &brickCountRes);
    node->SetInput(C::BRICKS_PER_AXIS_Slot::index,      0, &bpaRes);
    node->SetInput(C::SHELL_DILATION_Slot::index,       0, &dilationRes);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ASSERT_NE(node->GetPipeline(), VK_NULL_HANDLE);
    ASSERT_NE(node->GetPipelineLayout(), VK_NULL_HANDLE);
    ASSERT_NE(node->GetDescriptorSet(), VK_NULL_HANDLE);

    // ---- Assemble two ComputePassSteps with DISJOINT Resource* identities ----
    // "Shell read" pass reads the active generation's data + lookup as a render pass would.
    // "Revalidate" writes a candidate generation through a DISTINCT Resource identity, so
    // the schedule cannot infer a false read/write dependency between generations.
    Resource shellSlotNData;
    Resource shellSlotNLookup;
    Resource shellSlotNPlus1Data;   // what the revalidate pass writes (distinct object)
    Resource shellSlotNPlus1SourcePool;  // the revalidate pass's read (also distinct)

    ComputePassStep shellReadPass;
    shellReadPass.debugName = "shell_read_published_generation";
    shellReadPass.accesses = {
        {&shellSlotNData,   AccessKind::ComputeStorageRead, false},
        {&shellSlotNLookup, AccessKind::ComputeStorageRead, false},
    };

    ComputePassStep revalidatePass;
    revalidatePass.debugName = "shell_revalidate_candidate_generation";
    revalidatePass.pipeline = node->GetPipeline();
    revalidatePass.layout   = node->GetPipelineLayout();
    revalidatePass.descriptorSets = {node->GetDescriptorSet()};
    revalidatePass.workGroupCount = {(brickCount + 63u) / 64u, 1, 1};
    revalidatePass.accesses = {
        {&shellSlotNPlus1SourcePool, AccessKind::ComputeStorageRead,  false},
        {&shellSlotNPlus1Data,       AccessKind::ComputeStorageWrite, false},
    };

    // Order render-first then revalidate (as the live frame would record them) — mirrors
    // test_pass_group_schedule.cpp's ShellDoubleBufferRenderAndRevalidateNoBarrier case,
    // but here the revalidate pass carries REAL pipeline/layout/descriptorSet handles from
    // a REAL compiled node instead of placeholder VK_NULL_HANDLE structs.
    std::vector<PassStep> passes = {shellReadPass, revalidatePass};
    FrameSyncSchedule schedule = BuildPassGroupSchedule(passes);

    ASSERT_TRUE(schedule.valid);
    ASSERT_EQ(schedule.groups.size(), 2u);

    for (size_t i = 0; i < schedule.groups.size(); ++i) {
        std::printf("[ schedule ] group %zu entryBarriers.size() = %zu\n",
                    i, schedule.groups[i].entryBarriers.size());
    }

    EXPECT_TRUE(schedule.groups[0].entryBarriers.empty())
        << "shell-read pass (group 0) got a false entry barrier";
    EXPECT_TRUE(schedule.groups[1].entryBarriers.empty())
        << "revalidate pass (group 1) got a false barrier against the disjoint shell-read pass — "
           "double-buffer parallelism broken";

    // Reverse order must ALSO produce zero barriers (order-independent guarantee: the
    // resource sets are disjoint regardless of recording order).
    std::vector<PassStep> reversed = {revalidatePass, shellReadPass};
    FrameSyncSchedule schedule2 = BuildPassGroupSchedule(reversed);
    ASSERT_EQ(schedule2.groups.size(), 2u);
    EXPECT_TRUE(schedule2.groups[0].entryBarriers.empty());
    EXPECT_TRUE(schedule2.groups[1].entryBarriers.empty());

    // ---- Also assemble a REAL PassGroupNode (host assembly API) to prove the SAME
    //      property end-to-end through the node's own CompileImpl-baked schedule, not
    //      just the free function. PassGroupNode::CompileImpl requires SWAPCHAIN_INFO
    //      (for command-buffer count) — out of scope for this dispatch-focused node, so
    //      we exercise AddComputePass/PassCount (no-GPU host API) here and rely on the
    //      direct BuildPassGroupSchedule call above (the EXACT function CompileImpl
    //      calls, per PassGroupNode.cpp:85) for the live-schedule assertion.
    PassGroupNodeType groupType;
    PassGroupNode groupNode("shell_pass_group", &groupType);
    groupNode.AddComputePass(shellReadPass);
    groupNode.AddComputePass(revalidatePass);
    EXPECT_EQ(groupNode.PassCount(), 2u);

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    vkDestroyBuffer(logicalDevice_, sourcePoolBuf, nullptr); vkFreeMemory(logicalDevice_, sourcePoolMem, nullptr);
    vkDestroyBuffer(logicalDevice_, brickLookupBuf, nullptr); vkFreeMemory(logicalDevice_, brickLookupMem, nullptr);
    vkDestroyBuffer(logicalDevice_, configBuf, nullptr); vkFreeMemory(logicalDevice_, configMem, nullptr);
}

// ---------------------------------------------------------------------------
// (c) A value edit publishes a fresh complete generation without writing into
// the shell buffer that an in-flight frame may still be reading.
// ---------------------------------------------------------------------------
TEST_F(ShellRevalidateNodeTest, EditPublishesFreshGenerationAcrossFourFrames) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());

    using namespace Vixen::SVO;
    using C = BodyOctreeSceneNodeConfig;

    const int n = 64;
    const glm::vec3 center{32.0f, 32.0f, 32.0f};
    RecipeParams rp{24.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
    SdfBodyOctree body = BuildSdfBodyOctree(baked, 3);
    std::vector<const SdfBodyOctree*> octrees{&body};
    ConcatenatedOctrees pool = ConcatenateSdf(octrees);

    CashSystem::MainCacher mainCacher;
    NodeTypeRegistry registry;
    RenderGraph asyncGraph(&registry, nullptr, nullptr, &mainCacher);
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("dirty_shell_proxy_slot_test");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);
    node->SetOwningGraph(&asyncGraph);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes; SetHandleVal<uint32_t>(frameRes, 0u);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index, 0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index, 0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);
    node->SetRecipePool(pool);
    node->RequestBrickResidency(true);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ASSERT_FALSE(node->HasPublishedShellGeneration());
    ASSERT_EQ(node->ShellFullDeriveCount(), 0u);
    for (uint32_t frame = 0; frame < 1000u && !node->HasPublishedShellGeneration(); ++frame) {
        SetHandleVal<uint32_t>(frameRes, frame % 2u);
        ASSERT_NO_THROW(node->Execute());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(node->HasPublishedShellGeneration()) << "initial shell generation timed out";
    ASSERT_EQ(node->ShellFullDeriveCount(), 1u);
    const uint64_t proxyUploadsBeforeEdit = node->ProxyAabbUploadCount();
    ASSERT_GT(proxyUploadsBeforeEdit, 0u);

    const uint64_t oldGeneration = node->PublishedShellGeneration();
    Resource* shellBufferOut = node->GetOutput(C::SHELL_DATA_BUFFER_Slot::index, 0);
    Resource* proxyBufferOut = node->GetOutput(C::PROXY_AABB_BUFFER_Slot::index, 0);
    ASSERT_NE(shellBufferOut, nullptr);
    ASSERT_NE(proxyBufferOut, nullptr);
    const VkBuffer oldShellBuffer = shellBufferOut->GetHandle<VkBuffer>();
    const VkBuffer oldProxyBuffer = proxyBufferOut->GetHandle<VkBuffer>();
    ASSERT_NE(oldShellBuffer, VK_NULL_HANDLE);
    ASSERT_NE(oldProxyBuffer, VK_NULL_HANDLE);

    const ShellDeriveResult oldShell = node->ShellCache();
    ASSERT_FALSE(oldShell.shellLookup.empty());
    const uint32_t dirtyBrick = oldShell.shellLookup.front();
    ASSERT_LT(dirtyBrick, oldShell.sourceToShellSlot.size());
    const uint32_t shellSlot = oldShell.sourceToShellSlot[dirtyBrick];
    ASSERT_NE(shellSlot, UINT32_MAX);
    const uint32_t shellStride = oldShell.shellBrickStrideFloats;
    ASSERT_GT(shellStride, 0u);
    const size_t byteOffset = static_cast<size_t>(shellSlot) * shellStride * sizeof(float);
    const size_t byteCount = static_cast<size_t>(shellStride) * sizeof(float);
    ASSERT_LE(byteOffset + byteCount, node->ShellPool().compact.channelPool.size());
    std::vector<uint8_t> oldShellBytes(byteCount);
    std::memcpy(oldShellBytes.data(),
                node->ShellPool().compact.channelPool.data() + byteOffset,
                byteCount);

    std::vector<float> editedSdf(SerializedOctree::kVoxelsPerBrick, 37.0f);
    ASSERT_TRUE(node->EditSourceBrickSdf(dirtyBrick, editedSdf));

    // Four frame executions must keep the old published handles stable while the
    // producer works. The old parity-indexed path rewrote a buffer during this window.
    for (uint32_t frame = 0; frame < 4u; ++frame) {
        SetHandleVal<uint32_t>(frameRes, frame % 2u);
        ASSERT_NO_THROW(node->Execute());
        if (node->PublishedShellGeneration() == oldGeneration) {
            shellBufferOut = node->GetOutput(C::SHELL_DATA_BUFFER_Slot::index, 0);
            ASSERT_NE(shellBufferOut, nullptr);
            EXPECT_EQ(shellBufferOut->GetHandle<VkBuffer>(), oldShellBuffer);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (uint32_t frame = 0; frame < 1000u &&
         node->PublishedShellGeneration() == oldGeneration; ++frame) {
        SetHandleVal<uint32_t>(frameRes, frame % 2u);
        ASSERT_NO_THROW(node->Execute());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GT(node->PublishedShellGeneration(), oldGeneration) << "edited generation timed out";
    EXPECT_EQ(node->ShellFullDeriveCount(), 2u);
    EXPECT_EQ(node->ProxyAabbUploadCount(), proxyUploadsBeforeEdit + 1u);
    EXPECT_EQ(node->ShellRevalidateCount(), 0u)
        << "edits now publish complete generations instead of mutating the published payload";

    shellBufferOut = node->GetOutput(C::SHELL_DATA_BUFFER_Slot::index, 0);
    proxyBufferOut = node->GetOutput(C::PROXY_AABB_BUFFER_Slot::index, 0);
    ASSERT_NE(shellBufferOut, nullptr);
    ASSERT_NE(proxyBufferOut, nullptr);
    const VkBuffer newShellBuffer = shellBufferOut->GetHandle<VkBuffer>();
    const VkBuffer newProxyBuffer = proxyBufferOut->GetHandle<VkBuffer>();
    EXPECT_NE(newShellBuffer, oldShellBuffer);
    EXPECT_NE(newProxyBuffer, oldProxyBuffer);

    const auto& newPoolBytes = node->ShellPool().compact.channelPool;
    EXPECT_TRUE(newPoolBytes.empty() || byteOffset + byteCount > newPoolBytes.size() ||
                std::memcmp(oldShellBytes.data(), newPoolBytes.data() + byteOffset, byteCount) != 0)
        << "the edited source brick should change the new generation's shell payload";
    const uint64_t newGeneration = node->PublishedShellGeneration();
    const auto stableBytes = newPoolBytes;
    // Four more frames after the flip must keep the generation and its CPU payload
    // stable while the retired handles remain outside the active publication.
    for (uint32_t frame = 0; frame < 4u; ++frame) {
        SetHandleVal<uint32_t>(frameRes, frame % 2u);
        ASSERT_NO_THROW(node->Execute());
        EXPECT_EQ(node->PublishedShellGeneration(), newGeneration);
        EXPECT_EQ(node->ShellPool().compact.channelPool, stableBytes);
    }

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

}  // namespace
