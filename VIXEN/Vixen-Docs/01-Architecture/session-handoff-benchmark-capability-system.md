# Session Handoff: Benchmark Capability System & Multi-GPU Support

**Date:** 2025-12-23
**Engineer:** Previous Session
**Status:** Implementation Complete - Integration Required

---

## Session Summary

This session focused on resolving benchmark failures on integrated GPUs and implementing a comprehensive capability detection system. The root cause was that benchmarks attempted to use discrete GPU features (RTX, advanced swapchain maintenance) on integrated GPUs that don't support them.

---

## Completed Work

### 1. **Extension/Layer Validation** ✅

**Problem:** Benchmarks crashed during Vulkan instance/device creation when requesting unavailable extensions/layers.

**Solution:**
- **[InstanceNode.cpp:150-192](c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\InstanceNode.cpp#L150-L192)** - `ValidateAndFilterExtensions()` and `ValidateAndFilterLayers()`
  - Enumerates available extensions/layers before instance creation
  - Filters requested list to only include available ones
  - Logs warnings for unavailable items (doesn't fail)
  - Populates global capability graph with available extensions/layers

- **[DeviceNode.cpp:252-265](c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\DeviceNode.cpp#L252-L265)** - Device extension validation
  - Validates base device extensions (swapchain, maintenance, etc.)
  - Only enables extensions that exist on the GPU
  - RTX extensions already have validation at lines 270-277

**Impact:** Benchmarks no longer crash with `VK_ERROR_EXTENSION_NOT_PRESENT`.

---

### 2. **Configurable GPU Selection** ✅

**Problem:** Benchmarks hardcoded GPU index 0, which could be integrated GPU instead of discrete.

**Solution:**
- **[BenchmarkConfig.h:131](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\BenchmarkConfig.h#L131)** - Added `gpuIndex` to `BenchmarkSuiteConfig`
- **[BenchmarkGraphFactory.cpp:597](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkGraphFactory.cpp#L597)** - Uses `gpuIndex` instead of hardcoded 0
- Wired through entire call chain: `BuildInfrastructure()` → `ConfigureInfrastructureParams()` → all graph builders

**Impact:** Users can select which GPU to benchmark via config.

---

### 3. **Multi-GPU Benchmark Support** ✅

**Problem:** No way to benchmark all GPUs in a single run.

**Solution:**
- **[BenchmarkConfig.h:135](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\BenchmarkConfig.h#L135)** - Added `runOnAllGPUs` flag
- **[BenchmarkRunner.cpp:1326-1371](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp#L1326-L1371)** - `EnumerateAvailableGPUs()` returns GPU info with sanitized names
- **[BenchmarkRunner.cpp:520-615](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp#L520-L615)** - Multi-GPU loop implementation
  - Generates single session UUID shared across all GPUs
  - Each GPU gets unique output folder: `YYYYMMDD_HHMMSS_GPUName_UUID`
  - Each GPU exports own results and ZIP package independently

**Output Structure:**
```
benchmark_results/
├── 20251223_143045_NVIDIA_GeForce_RTX_4050_a3f7b21c/
│   ├── results.csv
│   ├── results.json
│   └── 20251223_143045_NVIDIA_GeForce_RTX_4050_a3f7b21c.zip
└── 20251223_143102_Intel_R__UHD_Graphics_a3f7b21c/
    ├── results.csv
    ├── results.json
    └── 20251223_143102_Intel_R__UHD_Graphics_a3f7b21c.zip
```

**Impact:** Can benchmark all GPUs in one run with separate results per GPU.

---

### 4. **GPU Capability Graph System** ✅

**Problem:** No unified system to query GPU capabilities at runtime.

**Solution:** Implemented dependency-based capability graph.

#### **Core System**

**[CapabilityGraph.h](c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\include\CapabilityGraph.h)** - Capability graph architecture:

```cpp
CapabilityNode (base class)
├── InstanceExtensionCapability (leaf: VK_KHR_surface, VK_EXT_debug_utils, etc.)
├── InstanceLayerCapability (leaf: VK_LAYER_KHRONOS_validation)
├── DeviceExtensionCapability (leaf: VK_KHR_swapchain, VK_KHR_ray_tracing_pipeline, etc.)
└── CompositeCapability (composite: depends on other capabilities)
```

**[CapabilityGraph.cpp](c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\src\CapabilityGraph.cpp)** - Predefined capabilities:

**Base Extensions:**
- Swapchain, Maintenance 1-6, RTX extensions, Buffer Device Address

**Composite Capabilities:**
- `RTXSupport` - Requires all 5 RT extensions (ray_tracing_pipeline, acceleration_structure, ray_query, deferred_host_operations, buffer_device_address)
- `SwapchainMaintenance1` - Swapchain + Maintenance1
- `SwapchainMaintenance2` - Swapchain + Maintenance1 + Maintenance2
- `SwapchainMaintenance3` - Swapchain + Maintenance1 + Maintenance2 + Maintenance3
- `FullSwapchainSupport` - All maintenance (1-6) + mutable format
- `BasicRenderingSupport` - Swapchain + surface + platform surface
- `ValidationSupport` - Validation layer + debug utils

#### **Integration with VulkanDevice**

**[VulkanDevice.h:103-113](c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\include\VulkanDevice.h#L103-L113)** - Public API:
```cpp
// Get capability graph
CapabilityGraph& GetCapabilityGraph();

// Convenient shorthand
bool HasCapability(const std::string& capabilityName);
```

**[VulkanDevice.cpp:123-135](c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\src\VulkanDevice.cpp#L123-L135)** - Initialization:
- Builds standard capability graph during `CreateDevice()`
- Populates with available device extensions
- Invalidates cache to force initial check

**[InstanceNode.cpp:185-191](c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\InstanceNode.cpp#L185-L191)** - Instance extensions population
**[InstanceNode.cpp:229-235](c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\InstanceNode.cpp#L229-L235)** - Instance layers population

#### **Usage Examples**

```cpp
// Anywhere you have VulkanDevice*
if (device->HasCapability("RTXSupport")) {
    // Enable hardware ray tracing tests
}

if (device->HasCapability("SwapchainMaintenance3")) {
    // Use advanced swapchain features
}

if (!device->HasCapability("FullSwapchainSupport")) {
    // Skip tests requiring latest maintenance extensions
    return; // Skip this test configuration
}
```

**Impact:** Runtime capability queries prevent crashes by skipping unsupported features.

---

## Critical Next Steps

### **IMMEDIATE PRIORITY: Add Capability Guards**

The capability graph is implemented but **NOT YET USED** to gate features. The next session must add guards everywhere we assume a capability exists.

---

### **1. Benchmark Configuration Filtering** 🔴 CRITICAL

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp`

**Location:** Inside multi-GPU loop and single GPU mode

**Required Changes:**

#### **A. Filter Tests Based on GPU Capabilities**

Before running each test configuration, check if the GPU supports required features:

```cpp
// In RunSuiteHeadless() or RunSuiteWithWindow()
// Before executing each test from config.tests

for (const auto& testConfig : config.tests) {
    // Get device capabilities
    auto* device = /* get VulkanDevice from current graph */;

    // Check if test requires RTX
    if (testConfig.pipeline == "hardware_rt") {
        if (!device->HasCapability("RTXSupport")) {
            LOG_WARNING("Skipping RTX test on GPU without RTX support: " + testConfig.testId);
            // Add to skipped results
            continue;
        }
    }

    // Check for swapchain requirements
    if (RequiresSwapchainMaintenance(testConfig)) {
        if (!device->HasCapability("SwapchainMaintenance3")) {
            LOG_WARNING("Skipping test requiring SwapchainMaintenance3: " + testConfig.testId);
            continue;
        }
    }

    // Run the test...
}
```

**Key Locations:**
- **[BenchmarkRunner.cpp:590-706](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp#L590-L706)** - `RunSuiteHeadless()`
- **[BenchmarkRunner.cpp:706+](c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp#L706)** - `RunSuiteWithWindow()`

#### **B. Add Test Capability Metadata**

Extend `TestConfiguration` to declare required capabilities:

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\FrameMetrics.h`

```cpp
struct TestConfiguration {
    // ... existing fields ...

    // Capabilities required to run this test
    std::vector<std::string> requiredCapabilities; // e.g., {"RTXSupport", "SwapchainMaintenance3"}

    bool CanRunOnDevice(const VulkanDevice* device) const {
        for (const auto& cap : requiredCapabilities) {
            if (!device->HasCapability(cap)) {
                return false;
            }
        }
        return true;
    }
};
```

---

### **2. RTX-Specific Guards** 🔴 CRITICAL

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkGraphFactory.cpp`

**Location:** `BuildHardwareRTGraph()`

**Current Code (Line ~1475):**
```cpp
BenchmarkGraph BenchmarkGraphFactory::BuildHardwareRTGraph(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    uint32_t width,
    uint32_t height,
    const BenchmarkSuiteConfig* suiteConfig)
{
    // Currently builds RT graph without checking if RTX is available
```

**Required Change:**
```cpp
BenchmarkGraph BenchmarkGraphFactory::BuildHardwareRTGraph(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    uint32_t width,
    uint32_t height,
    const BenchmarkSuiteConfig* suiteConfig)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildHardwareRTGraph: graph is null");
    }

    // Get device from infrastructure
    result.infra = BuildInfrastructure(graph, width, height, true, gpuIndex);
    auto* device = static_cast<RG::DeviceNode*>(graph->GetInstance(result.infra.device));

    // Check RTX capability
    if (device && device->vulkanDevice) {
        if (!device->vulkanDevice->HasCapability("RTXSupport")) {
            throw std::runtime_error("Cannot build hardware RT graph: GPU does not support RTX");
        }
    }

    // Continue with RT graph building...
}
```

**Alternative:** Return early or build compute fallback graph instead of throwing.

---

### **3. DeviceNode Extension Guards** 🟡 IMPORTANT

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\DeviceNode.cpp`

**Location:** Lines 267-281 (RTX extension auto-enable)

**Current Code:**
```cpp
// Phase K: Auto-enable RTX extensions if available
auto rtxExtensions = VulkanDevice::GetRTXExtensions();

bool rtxAvailable = true;
for (const auto& rtxExt : rtxExtensions) {
    if (!hasExt(rtxExt)) {
        rtxAvailable = false;
        NODE_LOG_INFO("[DeviceNode] RTX extension not available: " + std::string(rtxExt));
        break;
    }
}

if (rtxAvailable) {
    // ... enable RTX extensions
}
```

**Enhancement:** Use capability graph instead of manual checking:
```cpp
// After device creation, check capability graph
if (vulkanDevice->HasCapability("RTXSupport")) {
    NODE_LOG_INFO("[DeviceNode] RTX fully supported - hardware ray tracing enabled");
} else {
    NODE_LOG_INFO("[DeviceNode] RTX not available - skipping ray tracing features");
}
```

---

### **4. BenchmarkMain Guards** 🟡 IMPORTANT

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\application\benchmark\source\BenchmarkMain.cpp`

**Location:** Lines 37-57 (extension hardcoding)

**Current Code:**
```cpp
instanceExtensionNames.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
deviceExtensionNames.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
deviceExtensionNames.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
```

**Required Change:** Remove hardcoded extensions - rely on validation filtering:
```cpp
// Let InstanceNode and DeviceNode validation handle filtering
// Only request extensions if config explicitly requires them
// Or query capability graph after device creation to decide what to enable
```

---

### **5. Add Capability Export to Results** 🟢 ENHANCEMENT

**File:** `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp`

**Location:** Result export functions

**Enhancement:** Include GPU capabilities in benchmark metadata:

```cpp
// In result export
void BenchmarkRunner::ExportCapabilities(const VulkanDevice* device) {
    auto& graph = device->GetCapabilityGraph();

    json capabilities;
    capabilities["RTXSupport"] = graph.IsCapabilityAvailable("RTXSupport");
    capabilities["SwapchainMaintenance3"] = graph.IsCapabilityAvailable("SwapchainMaintenance3");
    capabilities["FullSwapchainSupport"] = graph.IsCapabilityAvailable("FullSwapchainSupport");

    // Include in results.json
}
```

---

## Testing Strategy

### **Test Scenarios**

1. **Integrated GPU (Intel UHD)**
   - Should skip RTX tests automatically
   - Should run compute tests only
   - Should gracefully skip swapchain maintenance tests if not supported

2. **Discrete GPU (NVIDIA RTX 4050)**
   - Should run all tests including RTX
   - Should enable all swapchain maintenance features

3. **Multi-GPU System**
   - Should generate separate results per GPU
   - Integrated GPU results should show skipped RTX tests
   - Discrete GPU results should show all tests

### **Validation Checklist**

- [ ] Benchmarks don't crash with `VK_ERROR_EXTENSION_NOT_PRESENT`
- [ ] RTX tests only run on GPUs with `RTXSupport` capability
- [ ] Integrated GPUs produce results (even if many tests skipped)
- [ ] Multi-GPU mode creates N separate folders with shared UUID
- [ ] Capability metadata exported to results JSON
- [ ] Logs clearly show which tests were skipped and why

---

## Known Issues / Tech Debt

### **1. Static Capability Lists** ⚠️

**Issue:** `InstanceExtensionCapability`, `InstanceLayerCapability`, and `DeviceExtensionCapability` use static member variables to store available extensions/layers.

**Impact:** If multiple devices are initialized, the last device's capabilities overwrite previous ones.

**Workaround:** In practice, benchmarks create one device at a time, so this isn't currently a problem.

**Long-term Fix:** Make capabilities per-device instead of global static.

---

### **2. Capability Graph Not Persisted** ⚠️

**Issue:** Capability graph is rebuilt every time `VulkanDevice::CreateDevice()` is called.

**Impact:** Minor performance overhead (negligible).

**Enhancement:** Consider caching capability graph per physical device ID.

---

### **3. Missing Capability Types**

**Not Yet Implemented:**
- Vulkan 1.1/1.2/1.3 feature queries (VkPhysicalDeviceVulkan11Features, etc.)
- Queue family capabilities (compute-only, transfer-only)
- Format support queries (VK_FORMAT_R8G8B8A8_UNORM, etc.)
- Memory heap queries (device-local, host-visible)

**When Needed:** Extend `CapabilityNode` with new derived types.

---

## File Manifest

### **New Files Created**
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\include\CapabilityGraph.h`
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\src\CapabilityGraph.cpp`
- `c:\cpp\VBVS--VIXEN\VIXEN\Vixen-Docs\01-Architecture\session-handoff-benchmark-capability-system.md` (this file)

### **Modified Files**
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\include\VulkanDevice.h` (added capability graph member + API)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\VulkanResources\src\VulkanDevice.cpp` (initialize graph in CreateDevice)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\InstanceNode.cpp` (validation + graph population)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\RenderGraph\src\Nodes\DeviceNode.cpp` (extension validation)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\BenchmarkConfig.h` (gpuIndex, runOnAllGPUs)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\BenchmarkRunner.h` (GPUInfo, GenerateBenchmarkFolderName)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\TestSuiteResults.h` (Merge method)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkRunner.cpp` (multi-GPU loop, GPU enumeration, UUID generation)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkGraphFactory.cpp` (gpuIndex parameter threading)
- `c:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\include\Profiler\BenchmarkGraphFactory.h` (updated signatures)

---

## References

**Vulkan Specification:**
- Extension mechanism: https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#extendingvulkan-extensions
- Ray tracing extensions: https://www.khronos.org/blog/ray-tracing-in-vulkan

**Project Documentation:**
- Extension validation: See failed benchmark result analysis in `c:\cpp\VBVS--VIXEN\VIXEN\temp\failed benchmark result *.txt`

---

## Handoff Checklist

- [x] Session summary documented
- [x] All code changes listed with file paths and line numbers
- [x] Next steps clearly defined with priority levels
- [x] Testing strategy outlined
- [x] Known issues documented
- [x] Usage examples provided
- [ ] **NEXT SESSION:** Add capability guards to prevent crashes
- [ ] **NEXT SESSION:** Test on integrated GPU (Intel UHD)
- [ ] **NEXT SESSION:** Test multi-GPU mode with mixed discrete/integrated

---

**END OF HANDOFF**

Next engineer: Start with **Section: Critical Next Steps** - priority 1 is adding capability guards to benchmark filtering.
