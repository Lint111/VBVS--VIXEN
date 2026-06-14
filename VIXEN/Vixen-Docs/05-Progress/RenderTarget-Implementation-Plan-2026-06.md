# RenderTargetNode + IRenderTarget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add offscreen render targets as a first-class graph concept — an `IRenderTarget` interface that both the swapchain and a new color-only `RenderTargetNode` implement, with all recording nodes migrated onto it (AR#28, P3 keystone).

**Architecture:** `IRenderTarget` is an abstract interface (virtual accessors + ergonomic conversion operators). `SwapChainPublicVariables` implements it (behaviorally unchanged); `RenderTargetData` is a new offscreen implementation produced by `RenderTargetNode` (allocated via the real `FindMemoryType`, tracked by `DeviceBudgetManager`, FR-7 lifecycle). Recording-node slots migrate from `SwapChainPublicVariables*` to `IRenderTarget*` incrementally — safe because the swapchain IS-A `IRenderTarget`.

**Tech Stack:** C++23, Vulkan 1.3, the VIXEN RenderGraph node system (`CONSTEXPR_NODE_CONFIG` slot configs, `TypedNode<Config>` lifecycle), GoogleTest.

**Design source:** [[RenderTarget-Design-2026-06]]. **Build:** `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16`. **Regression net:** the benchmark suite + the RenderGraph/VulkanResources test suites.

> **Reference files to read before starting** (models, not to be reproduced): `libraries/RenderGraph/include/Data/Nodes/WindowNodeConfig.h` (config macro template), `libraries/RenderGraph/include/Nodes/WindowNode.{h,cpp}` (TypedNode lifecycle + FR-7 branching), `libraries/RenderGraph/src/Nodes/DepthBufferNode.cpp` (image+view+memory allocation), `libraries/RenderGraph/include/NodeHelpers/BufferHelpers.h` (`FindMemoryType`), `libraries/VulkanResources/include/VulkanSwapChain.h` (`SwapChainPublicVariables`, `SwapChainBuffer`).

---

## STATUS (2026-06-14) — resume here

**Foundation merged to main (`3a7780fe`), build-green + app renders clean:**
- ✅ Task 1 — `IRenderTarget.h` (interface + `RenderTargetData`)
- ✅ Task 2 — `SwapChainPublicVariables` implements `IRenderTarget`
- ✅ Tasks 3–6 — `RenderTargetNode` (config/header/impl + registered in all sites)

**▶ RESUME AT Task 7** (round-trip test), then **Phase 4** (the ~13-node migration), then **Phase 5**
(verify + finish). The `claude/ar28-render-target` branch was merged + deleted — branch anew from main.

Carry-forward cleanup: `RenderTargetNode.cpp` has a local `DEFAULT_FRAMES_IN_FLIGHT=4` and a local
`FindSuitableMemoryType` (copies, to dodge a claimed MSVC `LNK1163`); consolidate to
`FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT` / `NodeHelpers::FindMemoryType` if that linker issue can be
resolved. Implementation discovery: `VulkanDevice` exposes `device` (VkDevice) + `gpu` (VkPhysicalDevice*)
as public fields (no getters).

---

## File Structure

- **Create** `libraries/VulkanResources/include/IRenderTarget.h` — the `IRenderTarget` interface + `RenderTargetData` impl.
- **Modify** `libraries/VulkanResources/include/VulkanSwapChain.h` — `SwapChainPublicVariables : public IRenderTarget`.
- **Create** `libraries/RenderGraph/include/Data/Nodes/RenderTargetNodeConfig.h` — slot config.
- **Create** `libraries/RenderGraph/include/Nodes/RenderTargetNode.h` + `libraries/RenderGraph/src/Nodes/RenderTargetNode.cpp` — the producer node.
- **Modify** the node-type registration sites (app `RegisterAllNodeTypes`, benchmark `RegisterAllNodeTypes`).
- **Create** `libraries/RenderGraph/tests/Nodes/test_render_target_node.cpp` — config + (device-gated) round-trip test.
- **Modify (Phase 3 migration)** ~13 node configs + 2 graph factories: slot type `SwapChainPublicVariables*` → `IRenderTarget*`.

---

## Phase 1 — IRenderTarget interface

### Task 1: Define `IRenderTarget` + `RenderTargetData`

**Files:**
- Create: `libraries/VulkanResources/include/IRenderTarget.h`

- [ ] **Step 1: Write the interface header**

```cpp
#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Vixen::Vulkan::Resources {

/// Abstract render target: a set of color images (one per in-flight frame) the recording nodes
/// draw into / sample. Both the swapchain (SwapChainPublicVariables) and offscreen targets
/// (RenderTargetData) implement it, so recording nodes depend on this, not the swapchain (AR#28).
struct IRenderTarget {
    virtual ~IRenderTarget() = default;

    virtual uint32_t    GetImageCount()   const = 0;  ///< number of color images (>=1)
    virtual uint32_t    GetCurrentIndex() const = 0;  ///< index of the image in use this frame
    virtual VkImage     GetImage(uint32_t i) const = 0;
    virtual VkImageView GetView(uint32_t i)  const = 0;
    virtual VkFormat    GetFormat()   const = 0;
    virtual VkExtent2D  GetExtent()   const = 0;

    VkImage     GetCurrentImage() const { return GetImage(GetCurrentIndex()); }
    VkImageView GetCurrentView()  const { return GetView(GetCurrentIndex()); }

    // Ergonomic conversions (preserve call sites that relied on the swapchain's implicit
    // conversions). Resolve to the CURRENT image/view.
    operator VkImageView() const { return GetCurrentView(); }
    operator VkImage()     const { return GetCurrentImage(); }
};

/// One offscreen color buffer: image + its backing memory + a view.
struct RenderTargetBuffer {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
};

/// Offscreen render target produced by RenderTargetNode.
class RenderTargetData : public IRenderTarget {
public:
    std::vector<RenderTargetBuffer> buffers;
    uint32_t   currentIndex = 0;
    VkFormat   format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};

    uint32_t    GetImageCount()   const override { return static_cast<uint32_t>(buffers.size()); }
    uint32_t    GetCurrentIndex() const override { return currentIndex; }
    VkImage     GetImage(uint32_t i) const override { return i < buffers.size() ? buffers[i].image : VK_NULL_HANDLE; }
    VkImageView GetView(uint32_t i)  const override { return i < buffers.size() ? buffers[i].view  : VK_NULL_HANDLE; }
    VkFormat    GetFormat()   const override { return format; }
    VkExtent2D  GetExtent()   const override { return extent; }
};

} // namespace Vixen::Vulkan::Resources
```

- [ ] **Step 2: Build the VulkanResources lib to verify it compiles**

Run: `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16 --target VulkanResources`
Expected: EXIT 0 (header is included by the lib only once something references it; this step just confirms it parses — add a throwaway `#include "IRenderTarget.h"` in `VulkanSwapChain.h` in Task 2).

- [ ] **Step 3: Commit**

```bash
git add libraries/VulkanResources/include/IRenderTarget.h
git commit -m "feat(rendertarget): add IRenderTarget interface + RenderTargetData (AR#28)"
```

### Task 2: `SwapChainPublicVariables` implements `IRenderTarget`

**Files:**
- Modify: `libraries/VulkanResources/include/VulkanSwapChain.h` (the `struct SwapChainPublicVariables` definition)

- [ ] **Step 1: Include the interface + derive**

At the top of `VulkanSwapChain.h` add `#include "IRenderTarget.h"`. Change `struct SwapChainPublicVariables {` to `struct SwapChainPublicVariables : public Vixen::Vulkan::Resources::IRenderTarget {` (it is already in that namespace — use the unqualified `IRenderTarget` if so; verify the namespace).

- [ ] **Step 2: Implement the accessors from existing fields**

Add inside the struct (the existing fields `colorBuffers`, `currentColorBuffer`, `swapChainImageCount`, `Format`, `Extent` already exist):

```cpp
    uint32_t    GetImageCount()   const override { return swapChainImageCount; }
    uint32_t    GetCurrentIndex() const override { return currentColorBuffer; }
    VkImage     GetImage(uint32_t i) const override { return i < colorBuffers.size() ? colorBuffers[i].image : VK_NULL_HANDLE; }
    VkImageView GetView(uint32_t i)  const override { return i < colorBuffers.size() ? colorBuffers[i].view  : VK_NULL_HANDLE; }
    VkFormat    GetFormat()   const override { return Format; }
    VkExtent2D  GetExtent()   const override { return Extent; }
```

Keep the existing swapchain-specific conversion operators (`operator VkSurfaceKHR()`, `operator VkSwapchainKHR()`, `operator std::vector<SwapChainBuffer>()`). **Remove** the struct's own `operator VkImageView()` / `operator VkImage()` (now inherited from `IRenderTarget`, with identical "current image/view" behavior) to avoid ambiguous overloads.

- [ ] **Step 3: Build the full solution to verify nothing broke**

Run: `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16`
Expected: EXIT 0. (If an "ambiguous conversion" error appears, it's the duplicate `operator VkImageView/VkImage` — confirm Step 2's removal landed.)

- [ ] **Step 4: Smoke-run the app to confirm the swapchain path still renders**

Run: `cd binaries && timeout 20 ./VIXEN.exe > /tmp/rt_smoke.log 2>&1; echo "EXIT=$?"; grep -ciE "VK_ERROR|validation layer:|VUID-" /tmp/rt_smoke.log`
Expected: exit 124 (timeout-killed = ran fine); grep count `0`.

- [ ] **Step 5: Commit**

```bash
git add libraries/VulkanResources/include/VulkanSwapChain.h
git commit -m "feat(rendertarget): SwapChainPublicVariables implements IRenderTarget (AR#28)"
```

---

## Phase 2 — RenderTargetNode (color-only producer)

### Task 3: `RenderTargetNodeConfig`

**Files:**
- Create: `libraries/RenderGraph/include/Data/Nodes/RenderTargetNodeConfig.h`

- [ ] **Step 1: Write the config** (model: `WindowNodeConfig.h`; struct-pointer slot model: `SwapChainNodeConfig.h` `SWAPCHAIN_PUBLIC`)

```cpp
#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using IRenderTarget = Vixen::Vulkan::Resources::IRenderTarget;

namespace RenderTargetNodeCounts {
    static constexpr size_t INPUTS  = 1;  // VULKAN_DEVICE
    static constexpr size_t OUTPUTS = 4;  // RENDER_TARGET, CURRENT_VIEW, EXTENT_W, EXTENT_H
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(RenderTargetNodeConfig,
                      RenderTargetNodeCounts::INPUTS,
                      RenderTargetNodeCounts::OUTPUTS,
                      RenderTargetNodeCounts::ARRAY_MODE) {
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    OUTPUT_SLOT(RENDER_TARGET, IRenderTarget*, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(CURRENT_VIEW,  VkImageView,     1, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(WIDTH_OUT,     uint32_t,        2, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(HEIGHT_OUT,    uint32_t,        3, SlotNullability::Required, SlotMutability::WriteOnly);

    static constexpr const char* PARAM_WIDTH       = "width";
    static constexpr const char* PARAM_HEIGHT      = "height";
    static constexpr const char* PARAM_FORMAT      = "format";       // VkFormat as uint32_t
    static constexpr const char* PARAM_IMAGE_COUNT = "imageCount";   // default = frames-in-flight
    static constexpr const char* PARAM_USAGE       = "usage";        // VkImageUsageFlags as uint32_t

    RenderTargetNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor rtDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(RENDER_TARGET, "render_target", ResourceLifetime::Persistent, rtDesc);
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_VIEW, "current_view", ResourceLifetime::Persistent, viewDesc);
        BufferDescription wDesc{}; INIT_OUTPUT_DESC(WIDTH_OUT,  "width",  ResourceLifetime::Transient, wDesc);
        BufferDescription hDesc{}; INIT_OUTPUT_DESC(HEIGHT_OUT, "height", ResourceLifetime::Transient, hDesc);
    }

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(RENDER_TARGET_Slot::index == 0);
    static_assert(std::is_same_v<RENDER_TARGET_Slot::Type, IRenderTarget*>);
    VALIDATE_NODE_CONFIG(RenderTargetNodeConfig, RenderTargetNodeCounts);
};

} // namespace Vixen::RenderGraph
```

> Note: `followSwapchainExtent` (optional swapchain input + resize) is deliberately deferred to a follow-up — this first node takes explicit `width`/`height` params (the spec lists it as an enhancement). Record that deferral in the node's header comment.

- [ ] **Step 2: Build the RenderGraph lib** — Run: `... --build build --target RenderGraph`. Expected EXIT 0 (the `static_assert`s validate slot indices/types at compile time).

- [ ] **Step 3: Commit** — `git add` the config; `git commit -m "feat(rendertarget): RenderTargetNodeConfig slots (AR#28)"`.

### Task 4: `RenderTargetNode` declaration

**Files:**
- Create: `libraries/RenderGraph/include/Nodes/RenderTargetNode.h`

- [ ] **Step 1: Write the header** (model: `WindowNode.h`)

```cpp
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"
#include "IRenderTarget.h"
#include <memory>

namespace Vixen::RenderGraph {

/// Type ID: pick the next free id (grep existing "Type ID:" in include/Nodes).
class RenderTargetNodeType : public TypedNodeType<RenderTargetNodeConfig> {
public:
    RenderTargetNodeType(const std::string& typeName = "RenderTarget")
        : TypedNodeType<RenderTargetNodeConfig>(typeName) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/// Allocates an offscreen color render target (color-only; compose with DepthBufferNode +
/// FramebufferNode like the swapchain path). Outputs an IRenderTarget* (its RenderTargetData).
class RenderTargetNode : public TypedNode<RenderTargetNodeConfig> {
public:
    using Base = TypedNode<RenderTargetNodeConfig>;
    RenderTargetNode(const std::string& instanceName, NodeType* nodeType);
    ~RenderTargetNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateTarget(Vixen::Vulkan::Resources::VulkanDevice* device);
    void DestroyTarget();

    Vixen::Vulkan::Resources::RenderTargetData target_;
    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;  // cached for cleanup
    uint32_t width_ = 0, height_ = 0, imageCount_ = 0;
    VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage_ = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
};

} // namespace Vixen::RenderGraph
```

- [ ] **Step 2: Commit** — `git add` the header; `git commit -m "feat(rendertarget): RenderTargetNode declaration (AR#28)"`.

### Task 5: `RenderTargetNode` implementation

**Files:**
- Create: `libraries/RenderGraph/src/Nodes/RenderTargetNode.cpp`
- (Add it to `libraries/RenderGraph/CMakeLists.txt` if sources are listed explicitly — grep for `DepthBufferNode.cpp` there and add the new file beside it.)

- [ ] **Step 1: Write the implementation** (allocation modeled on `DepthBufferNode.cpp`, memory via the **real** `FindMemoryType`)

```cpp
#include "Nodes/RenderTargetNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "NodeHelpers/BufferHelpers.h"   // FindMemoryType
#include "VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {
using namespace Vixen::Vulkan::Resources;

std::unique_ptr<NodeInstance> RenderTargetNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<RenderTargetNode>(n, const_cast<RenderTargetNodeType*>(this));
}

RenderTargetNode::RenderTargetNode(const std::string& n, NodeType* t)
    : TypedNode<RenderTargetNodeConfig>(n, t) {}

void RenderTargetNode::SetupImpl(TypedSetupContext& ctx) {
    width_      = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_WIDTH, 512);
    height_     = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_HEIGHT, 512);
    format_     = static_cast<VkFormat>(GetParameterValue<uint32_t>(
                      RenderTargetNodeConfig::PARAM_FORMAT, VK_FORMAT_R8G8B8A8_UNORM));
    imageCount_ = GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, 0); // 0 => derive in Compile
    usage_      = static_cast<VkImageUsageFlags>(GetParameterValue<uint32_t>(
                      RenderTargetNodeConfig::PARAM_USAGE,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
}

void RenderTargetNode::CompileImpl(TypedCompileContext& ctx) {
    device_ = ctx.In(RenderTargetNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) throw std::runtime_error("[RenderTargetNode] VULKAN_DEVICE_IN is null");

    // Persistent across recompile: only (re)create if not yet built (FR-7). A future
    // followSwapchainExtent mode recreates here when the extent changes.
    if (target_.buffers.empty()) {
        if (imageCount_ == 0) imageCount_ = ctx.GetFramesInFlight(); // see note below
        CreateTarget(device_);
    }

    ctx.Out(RenderTargetNodeConfig::RENDER_TARGET, static_cast<IRenderTarget*>(&target_));
    ctx.Out(RenderTargetNodeConfig::CURRENT_VIEW,  target_.GetCurrentView());
    ctx.Out(RenderTargetNodeConfig::WIDTH_OUT,     width_);
    ctx.Out(RenderTargetNodeConfig::HEIGHT_OUT,    height_);
}

void RenderTargetNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Advance the in-flight index so consumers read/write the right buffer this frame.
    if (target_.GetImageCount() > 0)
        target_.currentIndex = (target_.currentIndex + 1) % target_.GetImageCount();
}

void RenderTargetNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason != CleanupReason::FinalTeardown) return; // FR-7: persist across recompile
    DestroyTarget();
}

void RenderTargetNode::CreateTarget(VulkanDevice* device) {
    VkDevice vk = device->GetDevice();
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProps);

    target_.format = format_;
    target_.extent = {width_, height_};
    target_.currentIndex = 0;
    target_.buffers.resize(imageCount_);

    for (auto& b : target_.buffers) {
        VkImageCreateInfo img{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        img.imageType = VK_IMAGE_TYPE_2D; img.format = format_;
        img.extent = {width_, height_, 1}; img.mipLevels = 1; img.arrayLayers = 1;
        img.samples = VK_SAMPLE_COUNT_1_BIT; img.tiling = VK_IMAGE_TILING_OPTIMAL;
        img.usage = usage_; img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &img, nullptr, &b.image) != VK_SUCCESS)
            throw std::runtime_error("[RenderTargetNode] vkCreateImage failed");

        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(vk, b.image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(memProps, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "RenderTarget");
        if (vkAllocateMemory(vk, &ai, nullptr, &b.memory) != VK_SUCCESS)
            throw std::runtime_error("[RenderTargetNode] vkAllocateMemory failed");
        vkBindImageMemory(vk, b.image, b.memory, 0);

        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = b.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(vk, &vi, nullptr, &b.view) != VK_SUCCESS)
            throw std::runtime_error("[RenderTargetNode] vkCreateImageView failed");
    }
    NODE_LOG_INFO("[RenderTargetNode] created " + std::to_string(imageCount_) + " offscreen targets " +
                  std::to_string(width_) + "x" + std::to_string(height_));
}

void RenderTargetNode::DestroyTarget() {
    if (!device_) return;
    VkDevice vk = device_->GetDevice();
    for (auto& b : target_.buffers) {
        if (b.view)   vkDestroyImageView(vk, b.view, nullptr);
        if (b.image)  vkDestroyImage(vk, b.image, nullptr);
        if (b.memory) vkFreeMemory(vk, b.memory, nullptr);
    }
    target_.buffers.clear();
}

} // namespace Vixen::RenderGraph
```

> **Open detail for the implementer to confirm against the codebase (don't guess):** the exact way to get frames-in-flight in `CompileImpl` (shown as `ctx.GetFramesInFlight()`). Grep `MAX_FRAMES_IN_FLIGHT` / how SwapChainNode/FrameSyncNode learn the count; if there's no context accessor, read it from a graph constant or add `imageCount` default to that constant. Also confirm `device->GetDevice()`/`GetPhysicalDevice()` names against `VulkanDevice.h`.

- [ ] **Step 2: Build the full solution** — Run the full build. Expected EXIT 0.

- [ ] **Step 3: Commit** — `git commit -m "feat(rendertarget): RenderTargetNode implementation (AR#28)"`.

### Task 6: Register the node type

**Files:**
- Modify: app `RegisterAllNodeTypes` (grep `registry.Register<WindowNodeType>` to find both the app and benchmark sites) and the benchmark's `RegisterAllNodeTypes` (`libraries/Profiler/src/BenchmarkRunner.cpp:~423`).

- [ ] **Step 1:** Add `registry.Register<RenderTargetNodeType>();` beside the other `Register<...>()` calls in each site; add `#include "Nodes/RenderTargetNode.h"`.
- [ ] **Step 2:** Build full solution — EXIT 0.
- [ ] **Step 3:** Commit — `git commit -m "feat(rendertarget): register RenderTargetNodeType (AR#28)"`.

---

## Phase 3 — Round-trip test

### Task 7: RenderTargetNode test

**Files:**
- Create: `libraries/RenderGraph/tests/Nodes/test_render_target_node.cpp` (add to `libraries/RenderGraph/tests/CMakeLists.txt` mirroring `test_swap_chain_node`)

- [ ] **Step 1: Config tests (no device needed)**

```cpp
#include <gtest/gtest.h>
#include "Data/Nodes/RenderTargetNodeConfig.h"
using namespace Vixen::RenderGraph;

TEST(RenderTargetNodeTest, ConfigSlotCounts) {
    EXPECT_EQ(RenderTargetNodeConfig::INPUT_COUNT, 1u);
    EXPECT_EQ(RenderTargetNodeConfig::OUTPUT_COUNT, 4u);
}
TEST(RenderTargetNodeTest, RenderTargetIsIRenderTargetPtr) {
    static_assert(std::is_same_v<RenderTargetNodeConfig::RENDER_TARGET_Slot::Type,
                                 Vixen::Vulkan::Resources::IRenderTarget*>);
    SUCCEED();
}
```

> Confirm the `INPUT_COUNT`/`OUTPUT_COUNT` member names against an existing config test (e.g. `test_swap_chain_node.cpp` uses `SwapChainNodeConfig::INPUT_COUNT`).

- [ ] **Step 2: Run** — `./build/.../test_render_target_node.exe --gtest_brief=1`. Expected: PASS.
- [ ] **Step 3: Device round-trip test (gate behind device availability like other device tests)** — build a minimal graph (InstanceNode→DeviceNode→RenderTargetNode), compile, assert `RENDER_TARGET` output is non-null, `GetImageCount()==imageCount`, `GetExtent()` matches params, and each `GetView(i)`/`GetImage(i)` is non-null. Model the device setup on an existing device-gated test (e.g. `test_device_node.cpp`). If device-gated tests are skipped in this environment, mark it and rely on the app smoke + benchmark.
- [ ] **Step 4: Commit** — `git commit -m "test(rendertarget): RenderTargetNode config + round-trip (AR#28)"`.

---

## Phase 4 — Migration (slots → `IRenderTarget*`)

**Canonical edit (apply per node):** in each `*NodeConfig.h`, change every slot whose type is `SwapChainPublicVariables*` to `IRenderTarget*` (add `namespace Vixen::Vulkan::Resources { struct IRenderTarget; }` fwd-decl + a `using IRenderTarget = ...;` alias, mirroring the existing `SwapChainPublicVariables` usage), and update the matching `HandleDescriptor{"..."}`/`INIT_*_DESC` string to `"IRenderTarget*"`. In the node `.cpp`, the consuming code already calls `->` accessors / relies on the conversion operators, which `IRenderTarget` provides — but replace any direct field access (`->colorBuffers`, `->currentColorBuffer`, `->Format`, `->Extent`, `->swapChainImageCount`) with the interface accessors (`GetView/GetImage/GetCurrentIndex/GetFormat/GetExtent/GetImageCount`). Grep each node's `.cpp` for `->colorBuffers` etc. before editing.

**Process for each task below:** make the edit → build the full solution (EXIT 0) → run the benchmark suite + app smoke (no `VK_ERROR`/VUID) → commit. Because `SwapChainPublicVariables` IS-A `IRenderTarget`, each node keeps working with the swapchain after migration.

- [ ] **Task 8:** `SwapChainNode` — change its `SWAPCHAIN_PUBLIC` **output** slot type to `IRenderTarget*` (it points at the node's `SwapChainPublicVariables`). Build + benchmark + smoke + commit.
- [ ] **Task 9:** `FramebufferNode` (`FramebufferNodeConfig.h` + `.cpp`) — the only `vkCreateFramebuffer` site; migrate its swapchain slot + any direct field access. Build + benchmark + smoke + commit.
- [ ] **Task 10:** `RenderPassNode`. - [ ] **Task 11:** `GeometryRenderNode`. - [ ] **Task 12:** `ComputeDispatchNode`. - [ ] **Task 13:** `MultiDispatchNode`. - [ ] **Task 14:** `TraceRaysNode`. - [ ] **Task 15:** `UIRenderNode`. - [ ] **Task 16:** `DescriptorSetNode` (not `*_OLD.h`). - [ ] **Task 17:** `CameraNode`. - [ ] **Task 18:** `StructSpreaderNode`. - [ ] **Task 19:** `SwapChainStructSpreaderNode`. - [ ] **Task 20:** `DepthBufferNode` (its swapchain slot; **also** fix its `memoryTypeIndex = 0 // Placeholder` to the real `FindMemoryType` while here — AR#18 adjacent fix). - [ ] **Task 21:** Profiler `FrameCapture` (`.h`/`.cpp`).

Each of Tasks 10–21: apply the canonical edit to that node's config + `.cpp`, then build → benchmark → smoke → commit (`git commit -m "refactor(<node>): consume IRenderTarget* not SwapChainPublicVariables* (AR#28)"`).

- [ ] **Task 22:** Graph factories — update the app `BuildRenderGraph` + benchmark `BenchmarkGraphFactory` for any explicit `SwapChainPublicVariables*` typing in connections/presets. Build → benchmark → smoke → commit.

---

## Phase 5 — Verify + close out

- [ ] **Task 23:** Full Debug build green; run RenderGraph + VulkanResources + CashSystem test suites; run the benchmark exe briefly; 25 s app smoke (no `VK_ERROR`/VUID; renders). Confirm **zero** remaining `SwapChainPublicVariables*` in recording-node slot types: `grep -rnE "SwapChainPublicVariables\s*\*" libraries/RenderGraph/include/Data/Nodes | grep -v build` returns only intra-`SwapChainNode` internals (if any).
- [ ] **Task 24:** Update [[Maturation-Backlog-2026-06]] AR#28 → done; note the `followSwapchainExtent` + headless + CompositeNode follow-ups. Commit docs.

---

## Self-review notes (author)

- **Spec coverage:** IRenderTarget interface (Task 1–2), RenderTargetNode color-only producer with the real FindMemoryType + FR-7 lifecycle (Task 3–6), full migration of the ~13 nodes + factories (Task 8–22), benchmark-as-regression + round-trip test (Task 7, 23), DepthBufferNode placeholder fix folded into Task 20. ✔
- **Known confirm-against-codebase items (flagged inline, must not be guessed):** frames-in-flight accessor in `CompileImpl`; `VulkanDevice` getter names; `INPUT_COUNT`/`OUTPUT_COUNT` config member names; exact CMake source-list edits; per-node direct-field-access sites (`->colorBuffers` etc.). These are lookups, not open design.
- **Deferred (out of scope, noted):** `followSwapchainExtent`/resize, headless pipeline, CompositeNode, MRT-in-one-node.
