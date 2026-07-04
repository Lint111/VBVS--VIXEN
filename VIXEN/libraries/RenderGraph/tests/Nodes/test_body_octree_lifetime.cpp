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
 * test exercises) — it was stale, not a live hazard. IsAcceptableDevice() still refuses to
 * run on an unrecognized device (anything that isn't the known software rasterizer or Dozen),
 * so an untriaged GPU still fails loud rather than risk it.
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
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"                        // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

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

    // Accepts the known-safe devices this test has actually been verified against: the
    // software rasterizer (llvmpipe/lavapipe, CPU) or Mesa-Dozen (Vulkan-over-D3D12, reports
    // as a real GPU type). Rejects anything else — an untriaged device still fails loud rather
    // than risk an unverified GPU path.
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

        // ---- Physical device: software rasterizer or Dozen ONLY ----------------
        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: selected device '" << selectedDeviceName_
            << "' is not a verified device (software rasterizer or Dozen). "
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

}  // namespace
