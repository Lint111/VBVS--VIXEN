# Cacher Implementation Template

**Created**: November 1, 2025
**Status**: Reference Template
**Based on**: RenderPassCacher (completed ✅)

## Overview

This template provides a step-by-step guide for implementing new cachers following the proven pattern established by RenderPassCacher.

---

## Implementation Checklist

### Step 1: Create Header File ✅

**File**: `CashSystem/include/CashSystem/{Name}Cacher.h`

```cpp
#pragma once

#include "Headers.h"
#include "TypedCacher.h"
#include <cstdint>
#include <string>
#include <memory>

namespace CashSystem {

/**
 * @brief Resource wrapper for {ResourceName}
 *
 * Stores Vk{ResourceName} and associated metadata.
 */
struct {Name}Wrapper {
    Vk{ResourceName} resource = VK_NULL_HANDLE;

    // Cache identification metadata (for debugging/logging)
    // Add relevant fields here
};

/**
 * @brief Creation parameters for {ResourceName}
 *
 * All parameters that affect {ResourceName} creation.
 * Used to generate cache keys and create resources.
 */
struct {Name}CreateParams {
    // Add all creation parameters here
    // These will be used in ComputeKey() to generate unique cache keys
};

/**
 * @brief TypedCacher for {ResourceName} resources
 *
 * Caches {ResourceName} based on [describe key parameters].
 * {ResourceName} is expensive to create because [explain why].
 *
 * Usage:
 * ```cpp
 * auto& mainCacher = GetOwningGraph()->GetMainCacher();
 *
 * // Register if needed (done in node)
 * if (!mainCacher.IsRegistered(std::type_index(typeid({Name}Wrapper)))) {
 *     mainCacher.RegisterCacher<{Name}Cacher, {Name}Wrapper, {Name}CreateParams>(
 *         std::type_index(typeid({Name}Wrapper)),
 *         "{Name}",
 *         true  // device-dependent
 *     );
 * }
 *
 * // Get cacher
 * auto* cacher = mainCacher.GetCacher<{Name}Cacher, {Name}Wrapper, {Name}CreateParams>(
 *     std::type_index(typeid({Name}Wrapper)), device
 * );
 *
 * // Create parameters
 * {Name}CreateParams params{};
 * // ... set params
 *
 * // Get or create cached resource
 * auto wrapper = cacher->GetOrCreate(params);
 * Vk{ResourceName} resource = wrapper->resource;
 * ```
 */
class {Name}Cacher : public TypedCacher<{Name}Wrapper, {Name}CreateParams> {
public:
    {Name}Cacher() = default;
    ~{Name}Cacher() override = default;

    // Override to add cache hit/miss logging
    std::shared_ptr<{Name}Wrapper> GetOrCreate(const {Name}CreateParams& ci);

protected:
    // TypedCacher implementation
    std::shared_ptr<{Name}Wrapper> Create(const {Name}CreateParams& ci) override;
    std::uint64_t ComputeKey(const {Name}CreateParams& ci) const override;

    // Resource cleanup
    void Cleanup() override;

    // Serialization (optional - only if resource can be serialized)
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "{Name}Cacher"; }
};

} // namespace CashSystem
```

---

### Step 2: Create Implementation File ✅

**File**: `CashSystem/src/{Name}Cacher.cpp`

```cpp
#include "CashSystem/{Name}Cacher.h"
#include "CashSystem/MainCacher.h"
#include "VulkanResources/VulkanDevice.h"
#include "Hash.h"
#include <sstream>
#include <stdexcept>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace CashSystem {

void {Name}Cacher::Cleanup() {
    std::cout << "[{Name}Cacher::Cleanup] Cleaning up " << m_entries.size() << " cached resources" << std::endl;

    // Destroy all cached Vulkan resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->resource != VK_NULL_HANDLE) {
                    std::cout << "[{Name}Cacher::Cleanup] Destroying Vk{ResourceName}: "
                              << reinterpret_cast<uint64_t>(entry.resource->resource) << std::endl;
                    vkDestroy{ResourceName}(GetDevice()->device, entry.resource->resource, nullptr);
                    entry.resource->resource = VK_NULL_HANDLE;
                }
            }
        }
    }

    // Clear the cache entries after destroying resources
    Clear();

    std::cout << "[{Name}Cacher::Cleanup] Cleanup complete" << std::endl;
}

std::shared_ptr<{Name}Wrapper> {Name}Cacher::GetOrCreate(const {Name}CreateParams& ci) {
    auto key = ComputeKey(ci);
    std::string resourceName = "[generate descriptive name from params]";

    // Check cache first
    {
        std::shared_lock rlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[{Name}Cacher::GetOrCreate] CACHE HIT for " << resourceName
                      << " (key=" << key << ", Vk{ResourceName}="
                      << reinterpret_cast<uint64_t>(it->second.resource->resource) << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) {
            std::cout << "[{Name}Cacher::GetOrCreate] CACHE PENDING for " << resourceName
                      << " (key=" << key << "), waiting..." << std::endl;
            return pit->second.get();
        }
    }

    std::cout << "[{Name}Cacher::GetOrCreate] CACHE MISS for " << resourceName
              << " (key=" << key << "), creating new resource..." << std::endl;

    // Call parent implementation which will invoke Create()
    return TypedCacher<{Name}Wrapper, {Name}CreateParams>::GetOrCreate(ci);
}

std::shared_ptr<{Name}Wrapper> {Name}Cacher::Create(const {Name}CreateParams& ci) {
    std::cout << "[{Name}Cacher::Create] CACHE MISS - Creating new resource" << std::endl;

    auto wrapper = std::make_shared<{Name}Wrapper>();

    // Store metadata
    // wrapper->field = ci.field;

    // Create Vulkan resource
    Vk{ResourceName}CreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_{RESOURCE_NAME}_CREATE_INFO;
    // ... fill in createInfo from ci

    VkResult result = vkCreate{ResourceName}(GetDevice()->device, &createInfo, nullptr, &wrapper->resource);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("{Name}Cacher: Failed to create resource (VkResult: " +
                                 std::to_string(result) + ")");
    }

    std::cout << "[{Name}Cacher::Create] Vk{ResourceName} created: "
              << reinterpret_cast<uint64_t>(wrapper->resource) << std::endl;

    return wrapper;
}

std::uint64_t {Name}Cacher::ComputeKey(const {Name}CreateParams& ci) const {
    // Combine all parameters into a unique key string
    std::ostringstream keyStream;
    keyStream << /* all relevant parameters with "|" separators */;

    // Use standard hash function (matching PipelineCacher pattern)
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

bool {Name}Cacher::SerializeToFile(const std::filesystem::path& path) const {
    std::cout << "[{Name}Cacher::SerializeToFile] Serializing " << m_entries.size()
              << " resource configs to " << path << std::endl;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cout << "[{Name}Cacher::SerializeToFile] Failed to open file for writing" << std::endl;
        return false;
    }

    // Write entry count
    std::shared_lock rlock(m_lock);
    uint32_t count = static_cast<uint32_t>(m_entries.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each entry: key + metadata (not Vulkan handle)
    for (const auto& [key, entry] : m_entries) {
        ofs.write(reinterpret_cast<const char*>(&key), sizeof(key));

        // Serialize metadata for recreation
        const auto& w = entry.resource;
        // ofs.write(reinterpret_cast<const char*>(&w->field), sizeof(w->field));
    }

    std::cout << "[{Name}Cacher::SerializeToFile] Serialization complete" << std::endl;
    return true;
}

bool {Name}Cacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    std::cout << "[{Name}Cacher::DeserializeFromFile] Deserializing from " << path << std::endl;

    if (!std::filesystem::exists(path)) {
        std::cout << "[{Name}Cacher::DeserializeFromFile] Cache file does not exist" << std::endl;
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "[{Name}Cacher::DeserializeFromFile] Failed to open file for reading" << std::endl;
        return false;
    }

    // Read entry count
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::cout << "[{Name}Cacher::DeserializeFromFile] Loading " << count
              << " resource metadata entries" << std::endl;

    // Note: Only deserialize metadata. Vulkan handles will be recreated on-demand
    // via GetOrCreate() when parameters match. This ensures driver compatibility.

    for (uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        ifs.read(reinterpret_cast<char*>(&key), sizeof(key));

        // Read metadata fields
        // ifs.read(reinterpret_cast<char*>(&field), sizeof(field));

        std::cout << "[{Name}Cacher::DeserializeFromFile] Loaded metadata for key " << key << std::endl;
    }

    std::cout << "[{Name}Cacher::DeserializeFromFile] Deserialization complete (handles will be created on-demand)" << std::endl;
    return true;
}

} // namespace CashSystem
```

---

### Step 3: Integrate into Node ✅

**File**: `RenderGraph/src/Nodes/{Node}Node.cpp`

**In CompileImpl(), before using the cacher**:

```cpp
// Register {Name}Cacher if not already registered
auto& mainCacher = GetOwningGraph()->GetMainCacher();
if (!mainCacher.IsRegistered(std::type_index(typeid(CashSystem::{Name}Wrapper)))) {
    NODE_LOG_INFO("Registering {Name}Cacher with MainCacher");
    mainCacher.RegisterCacher<
        CashSystem::{Name}Cacher,
        CashSystem::{Name}Wrapper,
        CashSystem::{Name}CreateParams
    >(
        std::type_index(typeid(CashSystem::{Name}Wrapper)),
        "{Name}",
        true  // device-dependent (or false if device-independent)
    );
}

// Get or create cached resource
auto* cacher = mainCacher.GetCacher<
    CashSystem::{Name}Cacher,
    CashSystem::{Name}Wrapper,
    CashSystem::{Name}CreateParams
>(std::type_index(typeid(CashSystem::{Name}Wrapper)), device);

if (!cacher) {
    throw std::runtime_error("{Node}Node: Failed to get {Name}Cacher from MainCacher");
}

// Build cache parameters
CashSystem::{Name}CreateParams cacheParams{};
// ... fill in cacheParams

// Get or create cached resource
cached{Name}Wrapper = cacher->GetOrCreate(cacheParams);

if (!cached{Name}Wrapper || cached{Name}Wrapper->resource == VK_NULL_HANDLE) {
    throw std::runtime_error("{Node}Node: Failed to get or create resource from cache");
}

resource = cached{Name}Wrapper->resource;
```

**In node header** (`{Node}Node.h`):

```cpp
// Add forward declaration before namespace
namespace CashSystem {
    struct {Name}Wrapper;
}

// Add member variable in private section
std::shared_ptr<CashSystem::{Name}Wrapper> cached{Name}Wrapper;
```

**In CleanupImpl()**:

```cpp
void {Node}Node::CleanupImpl() {
    // Release cached wrapper - cacher owns Vk{ResourceName} and destroys when appropriate
    if (cached{Name}Wrapper) {
        std::cout << "[{Node}Node::CleanupImpl] Releasing cached wrapper (cacher owns resource)" << std::endl;
        cached{Name}Wrapper.reset();
        resource = VK_NULL_HANDLE;
    }
}
```

---

### Step 4: Build and Test ✅

```bash
# Reconfigure CMake (if adding new files)
cmake -S . -B build

# Build CashSystem library
cmake --build build --config Debug --target CashSystem

# Build full application
cmake --build build --config Debug --target VIXEN

# Run application
cd binaries && ./VIXEN.exe
```

**Expected Log Output**:
```
[{Name}Cacher::GetOrCreate] CACHE MISS for {resource} (key=...), creating new resource...
[{Name}Cacher::Create] Vk{ResourceName} created: ...

# On recompilation (e.g., window resize):
[{Name}Cacher::GetOrCreate] CACHE HIT for {resource} (key=..., Vk{ResourceName}=...)
```

---

## Phase A Priority Queue

### P0 - High Priority (Expensive Creation, High Reuse)

1. **✅ RenderPassCacher** - COMPLETED
   - Status: ✅ Implemented, tested, verified
   - Node: RenderPassNode
   - File: `CashSystem/{include,src}/RenderPassCacher.{h,cpp}`

2. **⏳ TextureCacher** - NEXT
   - Node: TextureLoaderNode
   - Benefits: Heavy I/O (file loading) + expensive decode (PNG, JPEG, KTX)
   - Key: File path + format + mip levels
   - Special: Caches **decoded pixel data** in addition to VkImage

3. **⏳ MeshCacher** (GeometryCacher)
   - Node: VertexBufferNode
   - Benefits: Heavy I/O (OBJ, GLTF parsing) + large binary data
   - Key: File path (or hash of vertex/index data if procedural)
   - Special: Caches **parsed vertex/index arrays** in addition to VkBuffer

### P1 - Medium Priority

4. **⏳ SamplerCacher**
   - Node: TextureLoaderNode (currently creates samplers inline)
   - Benefits: Small resource but frequently reused, limited combinations
   - Key: Filter modes + address modes + anisotropy

5. **⏳ DescriptorPoolSizingCache**
   - Node: DescriptorSetNode
   - Benefits: Reusable pool sizing calculations
   - Note: Caches **sizing strategy**, not VkDescriptorPool itself

---

## Common Patterns

### Cache Key Generation
```cpp
std::ostringstream keyStream;
keyStream << param1 << "|" << param2 << "|" << param3;
const std::string keyString = keyStream.str();
return std::hash<std::string>{}(keyString);
```

### Error Handling
```cpp
VkResult result = vkCreateXxx(...);
if (result != VK_SUCCESS) {
    throw std::runtime_error("XxxCacher: Failed to create resource (VkResult: " +
                             std::to_string(result) + ")");
}
```

### Cleanup Pattern
```cpp
if (GetDevice()) {
    for (auto& [key, entry] : m_entries) {
        if (entry.resource && entry.resource->handle != VK_NULL_HANDLE) {
            vkDestroyXxx(GetDevice()->device, entry.resource->handle, nullptr);
            entry.resource->handle = VK_NULL_HANDLE;
        }
    }
}
Clear();
```

---

## Files to Modify Per Cacher

### New Files
- `CashSystem/include/CashSystem/{Name}Cacher.h`
- `CashSystem/src/{Name}Cacher.cpp`

### Modified Files
- `RenderGraph/include/Nodes/{Node}Node.h` (add forward declaration, member variable)
- `RenderGraph/src/Nodes/{Node}Node.cpp` (integrate cacher, add registration, update cleanup)

---

## Success Criteria

- ✅ Build succeeds without errors
- ✅ Application runs without crashes
- ✅ Console shows "CACHE MISS" on first creation
- ✅ Console shows "CACHE HIT" on recompilation (e.g., window resize)
- ✅ Cleanup logs show proper resource release
- ✅ No Vulkan validation errors

---

## Notes

- **Device Dependency**: Most Vulkan resources are device-dependent (use `true` in RegisterCacher)
- **Serialization**: Only serialize metadata, not Vulkan handles (recreate on load)
- **Thread Safety**: TypedCacher provides thread-safe access via `m_lock`
- **Logging**: Add detailed logging for debugging (match RenderPassCacher verbosity)
- **Registration**: Cachers self-register on first use (idempotent pattern)

---

## Reference Implementation

See `RenderPassCacher.{h,cpp}` and `RenderPassNode.cpp` for complete working example.
