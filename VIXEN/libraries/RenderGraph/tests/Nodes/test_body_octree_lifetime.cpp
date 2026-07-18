/**
 * @file test_body_octree_lifetime.cpp
 * @brief GPU-resource lifetime test for BodyOctreeSceneNode.
 *
 * Empirically verifies the per-frame RING + create-once/teardown-once lifecycle that
 * BodyOctreeSceneNode was fixed to use, under the Khronos validation layer. The whole
 * point is to RUN the real node lifecycle on a real device and let validation catch
 * object-lifetime errors (destroy-while-bound, double-free, freeing mapped memory,
 * leaked objects at device destroy).
 *
 * DEVICE SELECTION: uses VixenSelectWslGpuIcd() (same call every VIXEN executable makes)
 * so this runs on Mesa-Dozen (the real GPU, via Vulkan-over-D3D12) when available on WSL2,
 * falling back to lavapipe (software) only when no /dev/dxg GPU passthrough exists. Both
 * paths are asserted safe — 2026-07-04: this test (and the 5 render-gate siblings sharing
 * this pattern) was empirically re-run against Dozen after a session hit an unrelated hang
 * from a different bug and questioned the lavapipe restriction; all passed cleanly with no
 * validation errors and no VM instability. The earlier "LAVAPIPE ONLY / dxgkrnl kernel-panics
 * the VM" restriction predated BodyOctreeSceneNode::CleanupImpl's Recompile-persists-buffers
 * guard and the KI-004 DeviceLost fix (both already fix the destroy-while-in-flight race this
 * test exercises) — it was stale, not a live hazard. A real discrete/integrated GPU is now
 * PREFERRED, with software (lavapipe/llvmpipe) or Dozen used only as a fallback when no real
 * GPU is visible — the earlier software/Dozen-only gate was a lavapipe-era artifact that made
 * this test unable to run on real hardware. The picker still refuses to run when NO usable
 * device is visible, so an environment with none fails loud rather than risk it.
 *
 * WHAT THIS PROVES vs DEFERS (honest scope):
 *   PROVES (validation catches these on a real driver):
 *     - per-frame ring upload does NOT destroy/recreate per frame (no destroy-while-bound),
 *     - grow path (EnsureRingAllocated behind vkDeviceWaitIdle) reallocates with no
 *       destroy-while-bound and capacity actually grows,
 *     - Cleanup(Recompile) keeps the buffers (no "destroyed object still in use"),
 *     - Cleanup(FinalTeardown)+vkDestroyDevice reports NO leaked objects, no double-free.
 *   DEFERS: a true multi-frame GPU TIMING race on lavapipe (which serializes submits) — Dozen
 *     runs are a stronger check here since it does not serialize the same way.
 *
 * Run: ./test_body_octree_lifetime
 *   (VixenSelectWslGpuIcd() auto-selects Dozen on WSL2 if provisioned; set VK_ICD_FILENAMES
 *   explicitly to force a specific ICD, e.g. lavapipe for comparison.)
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"         // MAX_FRAMES_IN_FLIGHT (== node's kRingSize)
#include "Data/Core/CompileTimeResourceSystem.h"   // Resource
#include "Core/NodeContext.h"                       // CleanupReason
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"                          // Vixen::SVO::BodyInstanceGpu
#include "SdfBake.h"                                 // BakeRecipeToSdfWorld / BuildSdfBodyOctree
#include "SdfRecipes.h"                              // RECIPE_SPHERE, RecipeParams
#include "Recipe/RecipeRegistry.h"                   // Recipe-Parameterization M3 Task 9: ReadParam program
#include "Recipe/SdfInstruction.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"                        // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// ---------------------------------------------------------------------------
// Validation message capture
// ---------------------------------------------------------------------------
// A process-global sink the debug-utils messenger writes into. gtest fixtures are
// instantiated per-test; the messenger callback is a C function pointer, so the
// simplest robust wiring is a file-local sink guarded by a mutex.
struct ValidationSink {
    std::mutex mu;
    std::vector<std::string> errors;    // ERROR severity (always fails the test)
    std::vector<std::string> warnings;  // WARNING severity (reported; lifetime ones fail)

    void Reset() {
        std::lock_guard<std::mutex> lock(mu);
        errors.clear();
        warnings.clear();
    }
};

ValidationSink g_sink;

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/)
{
    if (!data || !data->pMessage) {
        return VK_FALSE;
    }
    std::lock_guard<std::mutex> lock(g_sink.mu);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        g_sink.errors.emplace_back(data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        g_sink.warnings.emplace_back(data->pMessage);
    }
    return VK_FALSE;  // never abort the offending call; we assert in the test
}

// Lifetime-relevant substrings — a WARNING containing any of these is treated as a
// hard failure (validation sometimes reports use-after-free / leaks at WARNING).
bool IsLifetimeMessage(const std::string& m) {
    static const char* kNeedles[] = {
        "in use", "still in use", "freed", "destroyed", "has not been destroyed",
        "was destroyed", "double", "leak", "VkBuffer", "VkDeviceMemory",
        "bound to", "mapped",
    };
    for (const char* n : kNeedles) {
        if (m.find(n) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lifetime-test harness fixture
// ---------------------------------------------------------------------------
class BodyOctreeLifetimeTest : public ::testing::Test {
protected:
    VkInstance               instance_       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_      = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_ = VK_NULL_HANDLE;
    VkDevice                 logicalDevice_  = VK_NULL_HANDLE;
    VkQueue                  queue_          = VK_NULL_HANDLE;
    VkCommandPool            commandPool_    = VK_NULL_HANDLE;
    uint32_t                 queueFamily_    = 0;
    std::string              selectedDeviceName_;
    bool                     deviceConfirmed_ = false;

    // The VulkanDevice shell the node consumes. The node only reads ->device and *->gpu
    // and calls vkGetPhysicalDeviceMemoryProperties(*gpu); it never calls CreateDevice.
    // This mirrors the existing mock pattern (test_per_frame_resources.cpp:40-43).
    std::unique_ptr<VulkanDevice> deviceShell_;

    PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateMessenger_  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyMessenger_ = nullptr;

    // Real discrete/integrated GPUs are now PREFERRED; software/Dozen is only a
    // fallback when no real GPU is visible.
    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    // The software rasterizer (llvmpipe/lavapipe, CPU) or Mesa-Dozen (Vulkan-over-D3D12)
    // — accepted as a fallback when no real GPU is visible.
    static bool IsAcceptableDevice(const VkPhysicalDeviceProperties& props) {
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
        g_sink.Reset();

        // Same call every VIXEN executable makes before any Vulkan instance: auto-selects
        // Dozen on WSL2 when provisioned and no ICD was already chosen (falls back to
        // whatever the loader finds otherwise, typically lavapipe).
        VixenSelectWslGpuIcd();

        // ---- Instance: enable validation layer + debug-utils -------------------
        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_body_octree_lifetime";
        appInfo.apiVersion       = VK_API_VERSION_1_2;

        // ponytail: validation is a debug aid — only enabled when the SDK layer is installed
        const auto  enabledLayers = EnabledValidationLayers();
        const char* extensions[]  = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        // Wire the messenger into instance creation so create/destroy-instance messages
        // are also captured (no-op when the layer is absent — messages simply don't arrive).
        VkDebugUtilsMessengerCreateInfoEXT msgInfo{};
        msgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        msgInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        msgInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        msgInfo.pfnUserCallback = DebugCallback;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pNext                   = &msgInfo;  // capture instance-scope messages too
        instInfo.pApplicationInfo        = &appInfo;
        instInfo.enabledLayerCount       = static_cast<uint32_t>(enabledLayers.size());
        instInfo.ppEnabledLayerNames     = enabledLayers.empty() ? nullptr : enabledLayers.data();
        instInfo.enabledExtensionCount   = 1;
        instInfo.ppEnabledExtensionNames = extensions;

        VkResult res = vkCreateInstance(&instInfo, nullptr, &instance_);
        ASSERT_EQ(res, VK_SUCCESS)
            << "vkCreateInstance failed (rc=" << res << ") — is a Vulkan device available?";

        // Messenger requires the layer to deliver messages; skip wiring if layer is absent.
        if (!enabledLayers.empty()) {
            pfnCreateMessenger_ = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            pfnDestroyMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            ASSERT_NE(pfnCreateMessenger_, nullptr) << "vkCreateDebugUtilsMessengerEXT not found";
            ASSERT_NE(pfnDestroyMessenger_, nullptr);
            ASSERT_EQ(pfnCreateMessenger_(instance_, &msgInfo, nullptr, &messenger_), VK_SUCCESS);
        }

        // ---- Physical device: prefer a real GPU, fall back to software/Dozen ---
        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'. "
               "Aborting before any vkQueueSubmit.";

        // ---- Logical device + queue + command pool -----------------------------
        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        // ---- VulkanDevice shell the node consumes ------------------------------
        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
        // gpu already points at physicalDevice_ via the ctor.

        // No validation messages may have fired during harness bring-up.
        ExpectNoValidationErrors("harness setup");
    }

    void TearDown() override {
        // Idempotent: the test body normally destroys the device explicitly (so the
        // leak-at-vkDestroyDevice check can be ASSERTED in-body). Whatever is left we
        // clean up here so a failed/early-returning test still tears down cleanly.
        ReleaseDeviceShell();          // non-owning view: do NOT let it vkDestroyDevice
        DestroyDeviceAndCommandPool();
        if (messenger_ != VK_NULL_HANDLE && pfnDestroyMessenger_) {
            pfnDestroyMessenger_(instance_, messenger_, nullptr);
            messenger_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    // The VulkanDevice shell is a NON-OWNING view over our VkDevice: its destructor calls
    // vkDestroyDevice on whatever ->device holds. We own logicalDevice_ in the harness, so
    // null the shell's handle BEFORE dropping it (destructor then no-ops) — otherwise the
    // shell would destroy our device out from under DestroyDeviceAndCommandPool(). Mirrors
    // the existing mock pattern (test_per_frame_resources.cpp: mockDevice->device = NULL).
    void ReleaseDeviceShell() {
        if (deviceShell_) {
            deviceShell_->device = VK_NULL_HANDLE;
            deviceShell_.reset();
        }
    }

    // Destroy the command pool + logical device (idempotent). vkDestroyDevice is where the
    // validation layer reports any leaked device-child objects ("...has not been destroyed").
    void DestroyDeviceAndCommandPool() {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyDevice(logicalDevice_, nullptr);
            logicalDevice_ = VK_NULL_HANDLE;
        }
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
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_    = true;
                return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsAcceptableDevice(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_    = true;
                return;
            }
        }
        // None matched: record the first device's name for the failure message, leave
        // deviceConfirmed_ == false so SetUp aborts before any GPU work.
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
            if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queueFamily_ = i;
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found) << "No graphics queue family on the software device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos    = &qInfo;

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

    // ---- Validation assertions ------------------------------------------------
    void ExpectNoValidationErrors(const char* phase) {
        std::lock_guard<std::mutex> lock(g_sink.mu);
        EXPECT_TRUE(g_sink.errors.empty())
            << "[" << phase << "] " << g_sink.errors.size() << " validation ERROR(s); first: "
            << (g_sink.errors.empty() ? "" : g_sink.errors.front());
        for (const std::string& w : g_sink.warnings) {
            EXPECT_FALSE(IsLifetimeMessage(w))
                << "[" << phase << "] lifetime-relevant validation WARNING: " << w;
        }
    }

    // Resource::SetHandle(T&&) is a forwarding-reference sink: with an explicit <T> it
    // only binds an rvalue. Store trivially-copyable handles/scalars by VALUE (the node
    // reads them back with GetHandle<T>()), passing a prvalue. (Idiom mirrors
    // test_passthroughstorage_handles.cpp: SetHandle<uint32_t>(42u).)
    template<typename T>
    static void SetHandleVal(Resource& res, T value) {
        res.SetHandle<T>(std::move(value));
    }

    // Build an N-element instance list (varying contents, fixed size unless n changes).
    static std::vector<Vixen::SVO::BodyInstanceGpu> MakeInstances(uint32_t n, float salt) {
        std::vector<Vixen::SVO::BodyInstanceGpu> v(n);
        for (uint32_t i = 0; i < n; ++i) {
            v[i].worldPos[0] = static_cast<float>(i) + salt;
            v[i].worldPos[1] = salt;
            v[i].worldPos[2] = -static_cast<float>(i);
            v[i].renderScale = 1.0f + 0.01f * static_cast<float>(i);
            v[i].color[0]    = 0.5f;
            v[i].color[1]    = 0.25f;
            v[i].color[2]    = 0.75f;
            v[i].octreeIndex = i % 3u;  // valid octree selector (3 shells built)
        }
        return v;
    }
};

// ---------------------------------------------------------------------------
// THE TEST: drive the real node lifecycle on a real device under validation.
// ---------------------------------------------------------------------------
TEST_F(BodyOctreeLifetimeTest, RealNodeRingLifecycleHasNoValidationErrors) {
    // Print the selected device so the run output PROVES it ran on the software rasterizer.
    std::cout << "[ device ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_octree_lifetime");
    ASSERT_NE(nodeBase, nullptr);
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr) << "CreateInstance did not return a BodyOctreeSceneNode";

    // ---- Inject inputs as Resources -----------------------------------------
    // These Resources must outlive every Compile()/Execute() call below; they live on
    // this stack frame and are wired into the node's input bundle at array index 0.
    using C = BodyOctreeSceneNodeConfig;

    Resource deviceRes;
    SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());

    Resource poolRes;
    SetHandleVal<VkCommandPool>(poolRes, commandPool_);

    Resource frameRes;
    uint32_t frameIndex = 0;
    SetHandleVal<uint32_t>(frameRes, frameIndex);

    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,   0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,       0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    const uint32_t kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
    ASSERT_GE(kRingSize, 1u);

    // =========================================================================
    // STEP 1 — Compile: build <=3 shell octrees + the instance ring.
    // =========================================================================
    const uint32_t kSmallCount = 4;
    node->SetInstances(MakeInstances(kSmallCount, 0.0f));
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ExpectNoValidationErrors("compile");

    // Capture the instance buffer the ring published at compile time (slot index 4).
    VkBuffer compileInstanceBuf =
        node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)
            ? node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>()
            : VK_NULL_HANDLE;
    EXPECT_NE(compileInstanceBuf, VK_NULL_HANDLE) << "Compile did not publish an instance buffer";

    // The octree buffers must be valid after compile.
    EXPECT_NE(node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>(),
              VK_NULL_HANDLE);
    EXPECT_NE(node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>(),
              VK_NULL_HANDLE);

    // =========================================================================
    // STEP 2 — Execute >= 2*kRingSize frames, advancing CURRENT_FRAME_INDEX
    //          0,1,..,kRingSize-1,0,1,... and calling SetInstances (same size,
    //          varying data) each frame. Assert: the ring slot written each frame
    //          IS the emitted INSTANCE_BUFFER, and no validation errors.
    // (Exercises Critical 1: per-frame ring, NO per-frame destroy/recompile.)
    // =========================================================================
    const uint32_t kFrames = 2u * kRingSize + 1u;  // at least two full ring cycles
    std::vector<VkBuffer> slotBuffers(kRingSize, VK_NULL_HANDLE);

    for (uint32_t f = 0; f < kFrames; ++f) {
        const uint32_t ringSlot = f % kRingSize;

        // New (same-size) data for this frame.
        node->SetInstances(MakeInstances(kSmallCount, static_cast<float>(f + 1)));

        // Advance the frame index input the node reads in ExecuteImpl.
        frameIndex = f;  // raw, unclamped — the node does (% kRingSize) itself
        SetHandleVal<uint32_t>(frameRes, frameIndex);

        ASSERT_NO_THROW(node->Execute()) << "Execute threw on frame " << f;

        // The buffer the node emitted this frame must be THIS ring slot's buffer.
        Resource* outRes = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0);
        ASSERT_NE(outRes, nullptr);
        VkBuffer emitted = outRes->GetHandle<VkBuffer>();
        EXPECT_NE(emitted, VK_NULL_HANDLE) << "frame " << f << " emitted a null instance buffer";

        if (slotBuffers[ringSlot] == VK_NULL_HANDLE) {
            slotBuffers[ringSlot] = emitted;  // first time we see this slot
        } else {
            // The ring is persistent: the same slot must re-emit the SAME buffer handle
            // every cycle (no per-frame destroy/recreate).
            EXPECT_EQ(emitted, slotBuffers[ringSlot])
                << "ring slot " << ringSlot << " changed buffer handle on frame " << f
                << " — a per-frame destroy/recreate regressed";
        }
    }
    ExpectNoValidationErrors("execute (ring cycling)");

    // All ring slots should be distinct buffers (a true ring, not one shared buffer).
    if (kRingSize >= 2) {
        EXPECT_NE(slotBuffers[0], slotBuffers[1])
            << "ring slots 0 and 1 share a buffer — not a real per-frame ring";
    }

    // =========================================================================
    // STEP 3 — Grow path: SetInstances with a LARGER count, then Compile again.
    //          EnsureRingAllocated must vkDeviceWaitIdle + reallocate. Assert:
    //          no validation error (no destroy-while-bound) and capacity grew.
    // (Exercises Critical 2.)
    // =========================================================================
    const uint32_t kLargeCount = kSmallCount * 8;  // forces ring capacity overflow
    node->SetInstances(MakeInstances(kLargeCount, 100.0f));
    ASSERT_NO_THROW(node->Compile()) << "re-Compile (grow path) threw";
    ExpectNoValidationErrors("compile (grow path)");

    // After growth, the published buffer is valid and the slot handles are refreshed.
    VkBuffer grownBuf =
        node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    EXPECT_NE(grownBuf, VK_NULL_HANDLE);

    // =========================================================================
    // STEP 4 — Execute more frames at the NEW (larger) size.
    // =========================================================================
    std::vector<VkBuffer> grownSlots(kRingSize, VK_NULL_HANDLE);
    for (uint32_t f = 0; f < kRingSize + 1u; ++f) {
        const uint32_t ringSlot = f % kRingSize;
        node->SetInstances(MakeInstances(kLargeCount, static_cast<float>(200 + f)));
        frameIndex = f;
        SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute()) << "Execute (post-grow) threw on frame " << f;

        VkBuffer emitted =
            node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        EXPECT_NE(emitted, VK_NULL_HANDLE);
        if (grownSlots[ringSlot] == VK_NULL_HANDLE) {
            grownSlots[ringSlot] = emitted;
        } else {
            EXPECT_EQ(emitted, grownSlots[ringSlot])
                << "post-grow ring slot " << ringSlot << " changed handle on frame " << f;
        }
    }
    ExpectNoValidationErrors("execute (post-grow)");

    // =========================================================================
    // STEP 5 — A full RECOMPILE cycle, mirroring the engine exactly
    //          (RenderGraph.cpp RecompileDirtyNodes / RecoverFromDeviceLoss):
    //              Cleanup(Recompile) -> Setup() -> Compile() ->
    //              ClearNeedsRecompile() -> ResetCleanupFlag().
    //          Cleanup(Recompile) must NOT destroy the buffers (the node keeps
    //          persistent GPU objects across recompile); the re-Compile must REUSE
    //          them (no destroy-while-bound). Then an Execute still works on the SAME
    //          ring-slot handles recorded before the recompile.
    // =========================================================================
    node->Cleanup(CleanupReason::Recompile);
    ExpectNoValidationErrors("cleanup (recompile)");

    // Engine rebuild bookkeeping (RenderGraph.cpp:655-660). ResetCleanupFlag() is the
    // operative call that re-arms a later Cleanup(FinalTeardown) — without it the base
    // class's cleanedUp guard would suppress the teardown and leak the buffers (this is
    // the engine's contract, not a node bug; verified against RenderGraph.cpp:659).
    node->Setup();
    node->ResetInputsUsedInCompile();
    ASSERT_NO_THROW(node->Compile()) << "re-Compile across recompile threw";
    node->ClearNeedsRecompile();
    node->ResetCleanupFlag();
    ExpectNoValidationErrors("recompile rebuild (Setup+Compile)");

    // Prove the buffers survived the recompile: Execute and confirm the emitted handle
    // matches the ring slot recorded BEFORE the recompile cycle (buffers were REUSED,
    // not recreated).
    {
        const uint32_t f = 0;
        node->SetInstances(MakeInstances(kLargeCount, 999.0f));
        frameIndex = f;
        SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute()) << "Execute after recompile cycle threw";
        VkBuffer emitted =
            node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
        EXPECT_NE(emitted, VK_NULL_HANDLE)
            << "instance buffer was destroyed by the recompile cycle — regression";
        EXPECT_EQ(emitted, grownSlots[f % kRingSize])
            << "ring slot handle changed across recompile — buffers were recreated, "
               "not kept (Critical: octree+ring persist across recompile)";
    }
    ExpectNoValidationErrors("execute (after recompile cycle)");

    // =========================================================================
    // STEP 6 — Cleanup(FinalTeardown): destroy all node buffers. Then destroy the
    //          device (in TearDown). Validation must report NO leaked objects and
    //          NO double-free. We wait-idle first (no in-flight work to alias).
    // =========================================================================
    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    ExpectNoValidationErrors("cleanup (final teardown)");

    // Destroy the node object itself (its dtor calls Cleanup() again — must be a safe
    // no-op now, NOT a double-free).
    nodeBase.reset();
    ExpectNoValidationErrors("node destruction");

    // Destroy the device HERE (not in TearDown) so the leak check is asserted in-body:
    // if the node leaked any VkBuffer/VkDeviceMemory, the validation layer fires
    // "...has not been destroyed" at vkDestroyDevice and the assertion below catches it.
    ReleaseDeviceShell();              // drop the non-owning shell (no vkDestroyDevice)
    DestroyDeviceAndCommandPool();     // vkDestroyDevice — leak/double-free report point
    ExpectNoValidationErrors("device destroy (leaked-object check)");
}

// ---------------------------------------------------------------------------
// Recipe-Parameterization M3 Task 9 — THE MOST IMPORTANT CLAIM IN THIS PLAN:
// pure recipeParams[] value updates (same registered bytecode, same instance count) must
// NEVER trigger BodyOctreeSceneNode::SetInstances's recompile-avoidance logic to fire
// MarkNeedsRecompile(). This is the exact fragile/load-bearing logic SetInstances's own
// comment calls out ("Do NOT call MarkNeedsRecompile for a steady same-size update — the
// per-tick recompile cascade was the race root cause"). Directly instruments the real
// production path — NodeInstance::NeedsRecompile() — the same observable flag the engine's
// own RenderGraph::RecompileDirtyNodes() checks every tick, not a log-grep proxy for it.
// ---------------------------------------------------------------------------
TEST_F(BodyOctreeLifetimeTest, ReadParamValueSweepNeverMarksNodeNeedsRecompile) {
    std::cout << "[ device ] no-recompile-proof device: '" << selectedDeviceName_ << "'\n";
    using C = BodyOctreeSceneNodeConfig;

    // A genuine ReadParam-using program: sphere(center, baseRadius) - params[0] (MathSub is
    // non-commutative a-b; stack [sphereSD, readParam] => a=sphereSD, b=params[0]) — the SAME
    // construction BuildRenderGraph.cpp's VIXEN_PROCEDURAL_UBER_DEMO live gate uses (M3 Task 8),
    // registered here directly against RecipeRegistry (no VulkanGraphApplication needed — the
    // recompile-avoidance property under test lives entirely in BodyOctreeSceneNode::
    // SetInstances, independent of what the shader-splice/registry layer above it does).
    using Vixen::SVO::Recipe::SdfOpCode;
    using Vixen::SVO::Recipe::SdfInstruction;
    Vixen::SVO::RecipeRegistry registry;
    constexpr uint32_t kReadParamRecipeId = 7u;
    {
        SdfInstruction sph{}; sph.opCode = (uint8_t)SdfOpCode::Sphere;
        sph.data[0] = 0.0f; sph.data[1] = 0.0f; sph.data[2] = 0.0f; sph.data[3] = 6.0f;
        SdfInstruction rp{}; rp.opCode = (uint8_t)SdfOpCode::ReadParam; rp.paramMask = 1; rp.data[0] = 0.0f;
        SdfInstruction sub{}; sub.opCode = (uint8_t)SdfOpCode::MathSub;

        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
        entry.bytecode    = { sph, rp, sub };
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = 12.0f;
        auto regResult = registry.Register(kReadParamRecipeId, entry);
        ASSERT_EQ(regResult, Vixen::SVO::RecipeRegistry::RegisterResult::Ok)
            << "ReadParam program failed to register, code " << static_cast<int>(regResult);
    }

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_octree_no_recompile_proof");
    ASSERT_NE(nodeBase, nullptr);
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // One PROVIDER_PROCEDURAL instance carrying the ReadParam recipe id — mirrors
    // VIXEN_PROCEDURAL_UBER_DEMO's own instance shape (BuildRenderGraph.cpp M3 Task 8).
    auto makeReadParamInstance = [&](float paramValue) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.renderScale     = 1.0f;
        inst.providerKind    = 1u;   // PROVIDER_PROCEDURAL
        inst.recipeId        = kReadParamRecipeId;
        inst.recipeParams[0] = paramValue;
        return inst;
    };

    const uint32_t kInstanceCount = 3u;
    std::vector<Vixen::SVO::BodyInstanceGpu> initial(kInstanceCount, makeReadParamInstance(0.0f));
    // The very FIRST SetInstances (instanceRingCapacity_ starts at 0, before any Compile ever
    // allocated a ring) correctly/expectedly flags NeedsRecompile — there is no ring yet to fit
    // into, so this is the documented growth path, not the steady-state claim under test. The
    // real "pure param update never recompiles" claim only applies POST-compile (below), once a
    // ring actually exists to compare capacity against.
    node->SetInstances(initial);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    node->ClearNeedsRecompile();  // isolate the assertion below to POST-compile SetInstances calls
    ExpectNoValidationErrors("no-recompile-proof compile");
    ASSERT_FALSE(node->NeedsRecompile()) << "post-Compile baseline must start clean";

    // =========================================================================
    // THE ASSERTION: N frames of PURE recipeParams[] value updates — SAME bytecode
    // (registry never re-registers), SAME instance COUNT (kInstanceCount every frame,
    // matching SetInstances's own "steady same-size update" language) — must produce
    // ZERO recompile requests. A sine sweep mirrors the live demo's actual value shape
    // (BuildRenderGraph.cpp / VulkanGraphApplication::PreTick), not just a monotonic ramp.
    // =========================================================================
    constexpr uint32_t kFrames = 50u;
    uint32_t recompileFlagCount = 0;
    for (uint32_t f = 0; f < kFrames; ++f) {
        const float sweepValue = 3.0f * std::sin(static_cast<float>(f) * 0.13f);
        std::vector<Vixen::SVO::BodyInstanceGpu> instances(kInstanceCount, makeReadParamInstance(sweepValue));
        // Same-size vector, different recipeParams[0] content only — the exact shape of a
        // real per-frame param sweep, never touching instance COUNT or recipe bytecode.
        node->SetInstances(std::move(instances));
        if (node->NeedsRecompile()) {
            ++recompileFlagCount;
            ADD_FAILURE() << "frame " << f << " (params[0]=" << sweepValue
                           << "): SetInstances raised NeedsRecompile on a pure "
                              "same-size param-value update — this is the recompile-per-"
                              "param-change regression this test exists to catch";
        }

        frameIndex = f;
        SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute()) << "Execute threw on frame " << f;
    }
    EXPECT_EQ(recompileFlagCount, 0u)
        << "expected ZERO recompiles across " << kFrames
        << " frames of pure recipeParams value updates; got " << recompileFlagCount;
    ExpectNoValidationErrors("no-recompile-proof execute sweep");

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    ReleaseDeviceShell();
    DestroyDeviceAndCommandPool();
    ExpectNoValidationErrors("no-recompile-proof device destroy");
}

// ---------------------------------------------------------------------------
// Recipe-Diversity-Stress-Scene Inc6 M3 — THE SCALED-UP VERSION OF THE ABOVE CLAIM:
// the exact same no-recompile invariant, but at the diversity-stress demo's own ceiling
// (min(N,192) instances, per M2's documented 192-instance tier-0 ceiling), with BOTH
// per-frame-updated parameters M3 introduces exercised together every frame — a scalar
// shape parameter (recipeParams[3]) on EVERY instance, plus an animated declared position
// (recipeParams[0..2]) on a quarter of them (index % 4 == 0, mirroring
// VulkanGraphApplication::PreTick's own subset choice). This is the single most important
// correctness bar for M3 per its own plan doc: a silent per-frame recompile at N=192 would
// be a severe, misleading performance artifact that would corrupt M4's entire measurement.
// ---------------------------------------------------------------------------
TEST_F(BodyOctreeLifetimeTest, DiversityStressParamSweepAtScaleNeverMarksNodeNeedsRecompile) {
    std::cout << "[ device ] no-recompile-at-scale-proof device: '" << selectedDeviceName_ << "'\n";
    using C = BodyOctreeSceneNodeConfig;
    using Vixen::SVO::Recipe::SdfOpCode;
    using Vixen::SVO::Recipe::SdfInstruction;

    constexpr uint32_t kInstanceCount = 192u;  // M2's documented tier-0 ceiling, this demo's own upper bound

    // Register kInstanceCount DISTINCT recipes — same meta-segment shape M2's
    // VIXEN_RECIPE_DIVERSITY_STRESS_DEMO uses: [ReadParamFloat3(0), DeclarePosition,
    // Sphere(origin, r), ReadParam(3), MathSub]. A bare sphere resolve segment is enough
    // here (this test is about SetInstances's recompile-avoidance bookkeeping, not
    // diversity-generation content — M2's own test/BuildRenderGraph.cpp already cover the
    // shape/CSG diversity itself); what matters is that EVERY recipe genuinely declares a
    // position AND reads a shape param, exactly like the real demo's programs.
    Vixen::SVO::RecipeRegistry registry;
    for (uint32_t i = 0; i < kInstanceCount; ++i) {
        const uint32_t recipeId = 100u + i;  // arbitrary id space, distinct from other tests' ids
        SdfInstruction readPos{}; readPos.opCode = (uint8_t)SdfOpCode::ReadParamFloat3;
        readPos.paramMask = 1; readPos.data[0] = 0.0f;
        SdfInstruction declarePos{}; declarePos.opCode = (uint8_t)SdfOpCode::DeclarePosition;
        SdfInstruction sphere{}; sphere.opCode = (uint8_t)SdfOpCode::Sphere;
        sphere.data[0] = 0.0f; sphere.data[1] = 0.0f; sphere.data[2] = 0.0f; sphere.data[3] = 6.0f;
        SdfInstruction readShapeParam{}; readShapeParam.opCode = (uint8_t)SdfOpCode::ReadParam;
        readShapeParam.paramMask = 1; readShapeParam.data[0] = 3.0f;
        SdfInstruction subShapeParam{}; subShapeParam.opCode = (uint8_t)SdfOpCode::MathSub;

        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
        entry.bytecode    = { readPos, declarePos, sphere, readShapeParam, subShapeParam };
        entry.boundCenter = glm::vec3(static_cast<float>(i) * 30.0f, 0.0f, 0.0f);
        entry.boundRadius = 6.0f + 3.0f + 6.0f;  // sphere radius + shape-param sweep + orbit margin
        auto regResult = registry.Register(recipeId, entry);
        ASSERT_EQ(regResult, Vixen::SVO::RecipeRegistry::RegisterResult::Ok)
            << "diversity-stress-shaped program " << i << " failed to register, code "
            << static_cast<int>(regResult);
    }

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_octree_diversity_scale_proof");
    ASSERT_NE(nodeBase, nullptr);
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Base declared positions (one per instance) — the orbit's fixed center, mirroring
    // VulkanGraphApplication::PreTick's own "cache the base, recompute absolute position from
    // base+phase every frame" approach (no drift/accumulation across frames).
    std::vector<glm::vec3> basePositions;
    basePositions.reserve(kInstanceCount);
    for (uint32_t i = 0; i < kInstanceCount; ++i) {
        basePositions.emplace_back(static_cast<float>(i) * 30.0f, 0.0f, 0.0f);
    }

    auto makeDiversityInstance = [&](uint32_t i, float shapeParam, const glm::vec3& declaredPos) {
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.renderScale     = 1.0f;
        inst.providerKind    = 1u;  // PROVIDER_PROCEDURAL
        inst.recipeId        = 100u + i;
        inst.recipeParams[0] = declaredPos.x;
        inst.recipeParams[1] = declaredPos.y;
        inst.recipeParams[2] = declaredPos.z;
        inst.recipeParams[3] = shapeParam;
        return inst;
    };

    std::vector<Vixen::SVO::BodyInstanceGpu> initial(kInstanceCount);
    for (uint32_t i = 0; i < kInstanceCount; ++i) {
        initial[i] = makeDiversityInstance(i, 0.0f, basePositions[i]);
    }
    // The very FIRST SetInstances legitimately flags NeedsRecompile (no ring exists yet) --
    // same documented growth-path exemption ReadParamValueSweepNeverMarksNodeNeedsRecompile
    // above relies on; the claim under test only applies POST-compile.
    node->SetInstances(initial);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    node->ClearNeedsRecompile();
    ExpectNoValidationErrors("no-recompile-at-scale-proof compile");
    ASSERT_FALSE(node->NeedsRecompile()) << "post-Compile baseline must start clean";

    // =========================================================================
    // THE ASSERTION: many frames of pure recipeParams[] value updates across ALL
    // kInstanceCount instances (shape param every instance, declared position on a quarter of
    // them) — SAME bytecode (registry never re-registers), SAME instance COUNT every frame —
    // must produce ZERO recompile requests. More frames than the single-body test (120 vs 50)
    // since this is the scale this milestone's live-run gate and M4's later sweep actually
    // depend on being recompile-free.
    // =========================================================================
    constexpr uint32_t kFrames = 120u;
    constexpr int kAnimatedPositionStride = 4;  // mirrors BuildRenderGraph.cpp/PreTick's own stride
    uint32_t recompileFlagCount = 0;
    uint32_t observedInstanceCount = 0;
    for (uint32_t f = 0; f < kFrames; ++f) {
        const float t = static_cast<float>(f);
        std::vector<Vixen::SVO::BodyInstanceGpu> instances(kInstanceCount);
        for (uint32_t i = 0; i < kInstanceCount; ++i) {
            const float shapeParam = 3.0f * std::sin(t * 0.05f + static_cast<float>(i));
            glm::vec3 declaredPos = basePositions[i];
            if (i % kAnimatedPositionStride == 0) {
                const float phase = t * 0.03f + static_cast<float>(i);
                declaredPos = glm::vec3(basePositions[i].x + 6.0f * std::cos(phase),
                                        basePositions[i].y,
                                        basePositions[i].z + 6.0f * std::sin(phase));
            }
            instances[i] = makeDiversityInstance(i, shapeParam, declaredPos);
        }
        observedInstanceCount = static_cast<uint32_t>(instances.size());
        ASSERT_EQ(observedInstanceCount, kInstanceCount)
            << "frame " << f << ": instance count must stay constant across the whole test";

        node->SetInstances(std::move(instances));
        if (node->NeedsRecompile()) {
            ++recompileFlagCount;
            ADD_FAILURE() << "frame " << f << ": SetInstances raised NeedsRecompile on a pure "
                              "same-size param-value update at N=" << kInstanceCount
                           << " — a per-frame recompile at this scale would silently corrupt "
                              "M4's later switch-cost measurement";
        }

        frameIndex = f;
        SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute()) << "Execute threw on frame " << f;
    }
    EXPECT_EQ(recompileFlagCount, 0u)
        << "expected ZERO recompiles across " << kFrames << " frames of pure recipeParams "
           "value updates at N=" << kInstanceCount << " instances; got " << recompileFlagCount;
    EXPECT_EQ(observedInstanceCount, kInstanceCount)
        << "instance count must have stayed exactly " << kInstanceCount << " for the entire run";
    ExpectNoValidationErrors("no-recompile-at-scale-proof execute sweep");

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    ReleaseDeviceShell();
    DestroyDeviceAndCommandPool();
    ExpectNoValidationErrors("no-recompile-at-scale-proof device destroy");
}

// A second test instance: a separate fixture lifecycle so the device-destroy leak check
// runs against a node that has been fully torn down. The body is intentionally identical
// in spirit but minimal — it verifies vkDestroyDevice (in TearDown) is clean after a
// compile + a few executes + final teardown.
TEST_F(BodyOctreeLifetimeTest, DeviceDestroyReportsNoLeakedObjects) {
    std::cout << "[ device ] (leak check) device: '" << selectedDeviceName_ << "'\n";

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_octree_leakcheck");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    node->SetInstances(MakeInstances(3, 0.0f));
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    for (uint32_t f = 0; f < FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT; ++f) {
        frameIndex = f; SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());
    }
    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    ExpectNoValidationErrors("leakcheck pre-teardown");

    // Destroy the device in-body so the leaked-object report (if any) is asserted here.
    ReleaseDeviceShell();
    DestroyDeviceAndCommandPool();
    ExpectNoValidationErrors("leakcheck device destroy");
}

// ---------------------------------------------------------------------------
// Surface-Shell ESVO cache — end-to-end through a live node Compile on a real
// device. Feeds a Stored-SDF sphere pool via SetRecipePool (the editor's path),
// compiles (→ CreateOctreeBuffers → DeriveShellCache → CreateShellBuffers), and
// asserts BOTH double-buffer slots are populated + byte-identical (bootstrap),
// the compact pool is render-equivalent (<= source, valid), and the SHELL_DATA/
// SHELL_LOOKUP outputs are non-null (the render's binding-11/12 source). Then it
// concretely proves the render reads the COMPACT shell, not the full pool, by
// confirming the shell buffer size differs from the full pool for a large body.
// ---------------------------------------------------------------------------
TEST_F(BodyOctreeLifetimeTest, ShellCachePopulatedThroughNodeCompile) {
    std::cout << "[ device ] shell-cache device: '" << selectedDeviceName_ << "'\n";
    using C = BodyOctreeSceneNodeConfig;

    // A LARGE sphere so deep interior-solid bricks exist to drop (real bandwidth
    // win): r56 in a 128^3 grid, bpa=16 (see SVO DIAG sweep: ~7.7% brick drop).
    const int n = 128; const float r = 56.0f;
    const glm::vec3 center{64.0f, 64.0f, 64.0f};
    Vixen::SVO::RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vixen::SVO::SdfBakeResult baked =
        Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, center, rp, n, 2.0f);
    Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    std::vector<const Vixen::SVO::SdfBodyOctree*> octs{ &body };
    Vixen::SVO::ConcatenatedOctrees pool = Vixen::SVO::ConcatenateSdf(octs);
    ASSERT_GT(pool.count, 0u);
    ASSERT_FALSE(pool.channelPool.empty()) << "sphere pool must have an SDF channel";
    const size_t fullPoolBytes = pool.channelPool.size();

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_octree_shellcache");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes;  SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;    SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    node->SetRecipePool(pool);
    // Lazy-Procedural-Delta-Baseline Inc0 M1: pin eager residency so this test's
    // pixel gates keep exercising the full brick march once mip-capable pools
    // default to lazy (a later milestone) — currently a no-op (default is eager).
    node->RequestBrickResidency(true);
    node->SetInstances(MakeInstances(1, 0.0f));
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ExpectNoValidationErrors("shellcache compile");

    // Shell derived into BOTH slots (octree 0 = perOctree[0]).
    const auto& s0 = node->ShellCacheSlot(0);
    const auto& s1 = node->ShellCacheSlot(1);
    EXPECT_GT(s0.sourceBrickCount, 0u);
    EXPECT_GT(s0.surfaceBrickCount, 0u) << "sphere must have surface bricks";
    EXPECT_GT(s0.shellBrickCount, 0u)   << "shell must be non-empty for a Stored-SDF body";
    EXPECT_LT(s0.shellBrickCount, s0.sourceBrickCount)
        << "a large sphere must drop deep interior-solid bricks (the bandwidth win)";
    EXPECT_EQ(node->ShellDilation(), 1u);

    // The compact pool is the render's actual data source (binding 11) and is
    // STRICTLY SMALLER than the full pool → render never touches the full dataset.
    const auto& sp0 = node->ShellPoolSlot(0);
    const size_t shellPoolBytes = sp0.compact.channelPool.size();
    EXPECT_LT(shellPoolBytes, fullPoolBytes)
        << "compact shell pool must be smaller than the full pool "
        << "(shell=" << shellPoolBytes << "B full=" << fullPoolBytes << "B)";
    std::cout << "[ shell-cache ] source=" << s0.sourceBrickCount
              << " surface=" << s0.surfaceBrickCount
              << " shell=" << s0.shellBrickCount
              << " | pool " << fullPoolBytes << " -> " << shellPoolBytes << " bytes ("
              << (100.0 * (double)shellPoolBytes / (double)fullPoolBytes) << "%)\n";

    // Both slots byte-identical on bootstrap (ping-pong-safe).
    ASSERT_EQ(sp0.compact.channelPool.size(),
              node->ShellPoolSlot(1).compact.channelPool.size());
    EXPECT_EQ(0, std::memcmp(sp0.compact.channelPool.data(),
                             node->ShellPoolSlot(1).compact.channelPool.data(),
                             shellPoolBytes));

    // Dilation 2 must re-derive a superset on the next compile.
    node->SetShellThickness(2u);
    EXPECT_EQ(node->ShellDilation(), 2u);
    node->Cleanup(CleanupReason::Recompile);
    node->Setup();
    node->ResetInputsUsedInCompile();
    ASSERT_NO_THROW(node->Compile());
    node->ClearNeedsRecompile();
    node->ResetCleanupFlag();
    ExpectNoValidationErrors("shellcache recompile (dilation 2)");
    EXPECT_GE(node->ShellCacheSlot(0).shellBrickCount, s0.shellBrickCount)
        << "dilation 2 must be a superset of dilation 1";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
    ReleaseDeviceShell();
    DestroyDeviceAndCommandPool();
    ExpectNoValidationErrors("shellcache device destroy");
}

}  // namespace
