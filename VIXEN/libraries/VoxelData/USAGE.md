# VoxelData Library - Usage Guide

## Clear Lifecycle Management

The VoxelData library makes explicit the difference between **destructive** and **non-destructive** operations:

### Destructive Operations (Require Rebuild)
- **`changeKey()`** - Changes the attribute that determines spatial structure sparsity
- **Cost**: Spatial structure (octree/grid) must be completely rebuilt
- **Notification**: `onKeyChanged()` callback fired to all observers

### Non-Destructive Operations (Keep Structure)
- **`addAttribute()`** - Adds new attribute, allocates slots in existing bricks
- **`removeAttribute()`** - Removes attribute, frees slots for reuse
- **Cost**: O(num_bricks) slot allocations/deallocations, NO data movement
- **Notification**: `onAttributeAdded()` / `onAttributeRemoved()` callbacks

## Basic Setup

```cpp
#include <VoxelData/AttributeRegistry.h>
#include <VoxelData/BrickView.h>

using namespace VoxelData;

// Create registry
auto registry = std::make_shared<AttributeRegistry>();

// Register key attribute (determines structure)
registry->registerKey("density", AttributeType::Float, 0.0f);

// Add other attributes
registry->addAttribute("material", AttributeType::Uint32, 0u);
registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

// Reserve capacity for efficiency
registry->reserve(10000);  // Pre-allocate for 10k bricks
```

## Observer Pattern (SVO Integration)

```cpp
class VoxelOctree : public IAttributeRegistryObserver {
    AttributeRegistry* m_registry;
    // ... octree structure ...

public:
    VoxelOctree(AttributeRegistry* registry) : m_registry(registry) {
        // Register as observer
        registry->addObserver(this);
    }

    ~VoxelOctree() {
        m_registry->removeObserver(this);
    }

    // DESTRUCTIVE: Key changed - must rebuild octree
    void onKeyChanged(const std::string& oldKey, const std::string& newKey) override {
        std::cout << "Key changed from " << oldKey << " to " << newKey << "\n";
        std::cout << "Rebuilding octree structure...\n";

        // 1. Clear old octree structure
        clearOctree();

        // 2. Rebuild from key attribute
        buildOctreeFromKey(newKey);

        // 3. Re-allocate bricks
        repopulateBricks();
    }

    // NON-DESTRUCTIVE: Attribute added - optional update
    void onAttributeAdded(const std::string& name, AttributeType type) override {
        std::cout << "Attribute added: " << name << " (non-destructive)\n";

        // Optional: Update GPU shaders to use new attribute
        updateShaders();

        // Optional: Update GUI/editor to show new attribute
        updateEditorUI();
    }

    // NON-DESTRUCTIVE: Attribute removed - optional cleanup
    void onAttributeRemoved(const std::string& name) override {
        std::cout << "Attribute removed: " << name << " (non-destructive)\n";

        // Optional: Remove from shaders
        updateShaders();
    }
};
```

## Working with Bricks

```cpp
// Allocate brick
uint32_t brickID = registry->allocateBrick();

// Get zero-copy view
BrickView brick = registry->getBrick(brickID);

// Write voxel data (type-safe)
for (int i = 0; i < 512; ++i) {
    brick.set<float>("density", i, sampleDensity(i));
    brick.set<uint32_t>("material", i, sampleMaterial(i));
}

// Read voxel data
float density = brick.get<float>("density", 42);

// Get array view for bulk operations
auto densityArray = brick.getAttributeArray<float>("density");
for (size_t i = 0; i < 512; ++i) {
    densityArray[i] *= 0.5f;  // Direct write to backing storage
}

// Free brick when done
registry->freeBrick(brickID);
```

## Runtime Attribute Management

```cpp
// Start with minimal attributes
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("material", AttributeType::Uint32, 0u);

// Build octree with 1000 bricks
OctreeBuilder builder(registry);
builder.buildFromSampler(sampler);  // Creates 1000 bricks

// Later: Add roughness WITHOUT touching existing data
registry->addAttribute("roughness", AttributeType::Float, 0.5f);
// ✅ All 1000 bricks automatically get roughness slots
// ✅ Density and material data stays in same memory locations
// ✅ Zero data movement!

// Populate new attribute
for (uint32_t brickID : octree->getAllBrickIDs()) {
    BrickView brick = registry->getBrick(brickID);
    auto roughness = brick.getAttributeArray<float>("roughness");
    // ... populate roughness ...
}

// Later: Remove light attribute (if it exists)
registry->removeAttribute("light");
// ✅ Slots freed for reuse
// ✅ Other attributes untouched
```

## Changing Key Attribute (Destructive!)

```cpp
// Initial setup
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("distance", AttributeType::Float, 0.0f);

// Build octree based on density
octree->build();  // Uses density for sparsity

// Later: Change key to distance field
if (registry->changeKey("distance")) {
    // Returns true = rebuild required
    std::cout << "Key changed - octree MUST rebuild!\n";

    // Octree observer receives onKeyChanged() callback
    // and automatically rebuilds structure
}
```

## GPU Upload

```cpp
// Get contiguous buffer for attribute
auto* storage = registry->getStorage("density");
const std::vector<uint8_t>& data = storage->getData();

// Upload to GPU (all bricks in one buffer!)
glBindBuffer(GL_SHADER_STORAGE_BUFFER, densitySSBO);
glBufferData(GL_SHADER_STORAGE_BUFFER,
             data.size(),
             data.data(),
             GL_STATIC_DRAW);

// In shader:
// layout(std430, binding = 0) buffer DensityData {
//     float density[];  // [brick0: 512 floats][brick1: 512 floats]...
// };
//
// float getDensity(uint brickID, uint voxelIndex) {
//     return density[brickID * 512 + voxelIndex];
// }
```

## Memory Layout

```
AttributeStorage "density":
[Brick0: 512 floats][Brick1: 512 floats][Brick2: 512 floats]...
 └─ slot 0          └─ slot 1          └─ slot 2

AttributeStorage "material":
[Brick0: 512 u32s][Brick1: 512 u32s][Brick2: 512 u32s]...
 └─ slot 0         └─ slot 1         └─ slot 2

BrickView(brickID=1):
  - density → storage["density"].slot[1]   (zero-copy reference)
  - material → storage["material"].slot[1] (zero-copy reference)
```

## Error Handling

```cpp
try {
    // Can't remove key attribute
    registry->removeAttribute("density");
} catch (const std::runtime_error& e) {
    std::cout << "Error: " << e.what() << "\n";
    // "Cannot remove key attribute: density"
}

try {
    // Can't change to non-existent attribute
    registry->changeKey("nonexistent");
} catch (const std::runtime_error& e) {
    std::cout << "Error: " << e.what() << "\n";
    // "Cannot change to non-existent attribute: nonexistent"
}

try {
    // Can't access attribute brick doesn't have
    BrickView brick = registry->getBrick(brickID);
    brick.get<float>("nonexistent", 0);
} catch (const std::runtime_error& e) {
    std::cout << "Error: " << e.what() << "\n";
    // "Brick does not have attribute: nonexistent"
}
```

## Performance Tips

1. **Reserve capacity**: Call `registry->reserve(maxBricks)` before bulk allocation
2. **Use array views**: `getAttributeArray()` for bulk operations (avoid get/set per voxel)
3. **Batch attribute changes**: Add all attributes at once, then build structure
4. **Monitor key changes**: They're expensive - only change when necessary
5. **Reuse freed bricks**: `freeBrick()` returns slots to pool for reuse

## Next Steps

- See `README.md` for architecture overview
- See `AttributeRegistry.h` for full API reference
- See SVO library for octree integration example
