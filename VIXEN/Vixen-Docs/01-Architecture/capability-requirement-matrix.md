---
tags: [architecture, vulkan, capabilities, extensions]
created: 2025-12-23
related: [[session-handoff-benchmark-capability-system]], [[gpu-capability-guards]]
---

# GPU Capability Requirement Matrix

## Overview

This document defines which GPU capabilities are required for various features, and how the system degrades gracefully when optional capabilities are unavailable.

## Extension Classification

### Device Extensions

| Extension | Status | Feature | Degradation Path |
|-----------|--------|---------|------------------|
| VK_KHR_SWAPCHAIN | REQUIRED | Windowed rendering | **Fail** - Cannot create swapchain |
| VK_EXT_SWAPCHAIN_MAINTENANCE_1 | OPTIONAL | Enhanced swapchain features | Skip - Use basic swapchain |
| VK_KHR_MAINTENANCE_6 | OPTIONAL | General maintenance features | Skip - Use standard features |
| VK_KHR_ACCELERATION_STRUCTURE | OPTIONAL (RTX) | Hardware ray tracing | Skip - Use compute/fragment pipeline |
| VK_KHR_RAY_TRACING_PIPELINE | OPTIONAL (RTX) | RT pipeline | Skip - Use compute/fragment pipeline |
| VK_KHR_BUFFER_DEVICE_ADDRESS | OPTIONAL (RTX) | Buffer device addresses | Skip - Use standard buffer access |

### Instance Extensions

| Extension | Status | Feature | Degradation Path |
|-----------|--------|---------|------------------|
| VK_KHR_SURFACE | REQUIRED | Surface creation | **Fail** - Cannot create surface |
| VK_KHR_WIN32_SURFACE | REQUIRED (Windows) | Windows surface | **Fail** - Platform-specific |
| VK_EXT_SURFACE_MAINTENANCE_1 | OPTIONAL | Enhanced surface features | Skip - Use basic surface |
| VK_KHR_GET_SURFACE_CAPABILITIES_2 | OPTIONAL | Extended capability queries | Skip - Use basic capability queries |
| VK_EXT_DEBUG_REPORT | OPTIONAL (Debug) | Debug messaging | Skip - No debug output |

## Version and Shader-Tool Dimensions

Version requirements use the same per-device `CapabilityGraph` as extensions and features. The
checked-in settings are in `VIXEN/cmake/vulkan-sdk-settings.env`; CMake reads them for static
configure checks and generates the values consumed by `CapabilityGraph`. Windows and WSL retain
different LunarG defaults because their official feeds differ. Set the process environment
variable `VIXEN_VULKAN_SDK_VERSION` to choose another SDK version; configure prints both the
expected and detected versions.

| Dimension | Requirement | Behaviour | Independent path |
|-----------|-------------|-----------|------------------|
| Vulkan API | SDK headers and each selected device must support Vulkan 1.2; `synchronization2` is core from the declared 1.3 threshold or uses `VK_KHR_synchronization2` below it | Configure fails if the SDK headers are below the declared API floor or omit the extension below the core threshold; the graph checks the selected adapter's API, extension and feature bit | None for the 1.2 API floor or synchronization2; both are required by the renderer |
| Vulkan SDK | The configured SDK package version is checked against `VIXEN_VULKAN_SDK_VERSION` | A different final revision is accepted when the API and tool capability checks pass; other package-release mismatches fail with the expected and found versions | Provision the declared SDK version |
| glslang | The version reported by the provisioned SDK's `glslangValidator` is a graph input | Configure records the compiler version; missing or unreportable glslang fails because the default shader-compilation path has no twin | Disable compile-time GLSL only in a build that explicitly supplies the shipped SPIR-V assets |
| SPIR-V target | Ray-query shaders require SPIR-V 1.4 | Configure reports a fallback when glslang's target is below 1.4; the graph then resolves `RayQueryLighting` to its capability-independent implementation | `CapabilityIndependent` ray-query twin |

The minimum API, synchronization2 promotion threshold and SPIR-V target floor are declared once in `vulkan-sdk-settings.env`. The
configure check reads those values, and the generated `VulkanCapabilityConfig.h` feeds the same
floors into `VersionRequirementCapability`. The SDK, glslang and SPIR-V target versions are graph
inputs; Vulkan API version is supplied from `VkPhysicalDeviceProperties` for each device.

## Pipeline Capability Requirements

### Compute Pipeline

**Required Capabilities:**
- None (works on all GPUs with Vulkan support)

**Optional Capabilities:**
- None

**Degradation:** Fully functional on all Vulkan-capable GPUs

---

### Fragment Pipeline

**Required Capabilities:**
- VK_KHR_SWAPCHAIN
- VK_KHR_SURFACE

**Optional Capabilities:**
- VK_EXT_SWAPCHAIN_MAINTENANCE_1 (enhanced swapchain features)

**Degradation:** Uses basic swapchain if maintenance extension unavailable

---

### Hardware Ray Tracing Pipeline

**Required Capabilities:**
- RTXSupport (includes VK_KHR_ACCELERATION_STRUCTURE, VK_KHR_RAY_TRACING_PIPELINE, VK_KHR_BUFFER_DEVICE_ADDRESS)
- VK_KHR_SWAPCHAIN
- VK_KHR_SURFACE

**Optional Capabilities:**
- None (all RT features are required for RT pipeline)

**Degradation:** **Cannot run** on GPUs without RTX support
- Tests with `requiredCapabilities: ["RTXSupport"]` are skipped automatically
- BuildHardwareRTGraph() throws error if RTXSupport unavailable

---

## Validation Implementation

### TestConfiguration Level

Tests declare required capabilities in `TestConfiguration::requiredCapabilities` vector:

```cpp
TestConfiguration rtTest;
rtTest.pipeline = "hardware_rt";
rtTest.requiredCapabilities = {"RTXSupport"};  // Will skip on integrated GPUs
```

**Validation Points:**
- BenchmarkRunner::RunSuiteHeadless() - line 757
- BenchmarkRunner::RunSuiteWithWindow() - line 913

**Behavior:** Tests with unavailable capabilities are skipped with clear warning logs

---

### Graph Build Level

BuildHardwareRTGraph() validates RTX capability before constructing RT pipeline:

**Validation Point:**
- BenchmarkGraphFactory::BuildHardwareRTGraph() - line 1502

**Behavior:** Throws `std::runtime_error` with clear message if RTXSupport unavailable

---

### Extension Enable Level

VulkanDevice validates each extension before enabling features:

**Validation Point:**
- VulkanDevice::CreateLogicalDevice() - line 65

**Behavior:** Skips optional extensions that aren't available, continues with available extensions only

---

## Usage Examples

### Example 1: Test That Requires RTX

```cpp
TestConfiguration rtBenchmark;
rtBenchmark.testId = "RT_256_CORNELL";
rtBenchmark.pipeline = "hardware_rt";
rtBenchmark.requiredCapabilities = {"RTXSupport"};  // GPU must support RTX
rtBenchmark.voxelResolution = 256;
rtBenchmark.sceneType = "cornell";

// On RTX GPU: Test runs normally
// On Integrated GPU: Test is SKIPPED with warning
```

### Example 2: Test That Runs Everywhere

```cpp
TestConfiguration computeBenchmark;
computeBenchmark.testId = "COMPUTE_512_CORNELL";
computeBenchmark.pipeline = "compute";
computeBenchmark.requiredCapabilities = {};  // No requirements - runs on all GPUs
computeBenchmark.voxelResolution = 512;
computeBenchmark.sceneType = "cornell";

// Runs on ALL Vulkan-capable GPUs (RTX, discrete, integrated)
```

### Example 3: Optional Feature Degradation

```cpp
// VulkanDevice checks if VK_EXT_SWAPCHAIN_MAINTENANCE_1 is available
if (HasExtension(extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
    // Enable swapchainMaintenance1 feature
    swapchainFeatures->swapchainMaintenance1 = VK_TRUE;
} else {
    // Skipped - basic swapchain features only
    // No error, no crash, graceful degradation
}
```

---

## Debugging Skipped Tests

When a test is skipped due to missing capabilities, the output shows:

```
Test 3/10 - hardware_rt... SKIPPED
  [Capability Check] Test requires capabilities not available on this GPU:
    ✗ RTXSupport (not available)
```

**Verbose Mode:** Add `--verbose` flag to see detailed capability checks

---

## Adding New Capability Requirements

### Step 1: Define Capability in CapabilityGraph

Add capability detection logic in `CapabilityGraph::BuildGraph()`:

```cpp
graph.AddCapability("NewFeatureSupport", hasNewFeatureExtension);
```

### Step 2: Add to TestConfiguration

Specify capability requirement in test config:

```cpp
testConfig.requiredCapabilities.push_back("NewFeatureSupport");
```

### Step 3: Validate Before Use

Check capability before using feature:

```cpp
if (!device->HasCapability("NewFeatureSupport")) {
    throw std::runtime_error("GPU does not support NewFeature");
}
```

---

## Related Documentation

- [[session-handoff-benchmark-capability-system]] - Implementation details
- [[gpu-capability-guards]] - Feature documentation
- `VulkanDevice::HasCapability()` - [VulkanDevice.h:119](file://VulkanDevice.h#L119)
- `CapabilityGraph` - [CapabilityGraph.h](file://CapabilityGraph.h)
- `TestConfiguration::requiredCapabilities` - [FrameMetrics.h:251](file://FrameMetrics.h#L251)
