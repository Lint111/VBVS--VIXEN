# VoxelData Library

**Zero-copy, runtime-configurable voxel data management**

## Purpose

The VoxelData library provides a **zero-copy, reference-based architecture** for managing voxel attributes in large-scale voxel systems. It's designed to be **independent of any specific spatial data structure** (octrees, grids, etc.) and focuses purely on efficient attribute storage and access.

## Key Features

- **Zero-Copy Architecture**: Bricks are views into shared backing arrays, not data copies
- **Runtime Attribute Management**: Add/remove attributes without rebuilding data structures
- **Type-Safe Access**: Template-based API with compile-time type checking
- **Memory Efficiency**: Slot-based allocation with automatic reuse
- **GPU-Ready**: Contiguous buffers per attribute for efficient GPU upload

## Core Components

### AttributeRegistry
Central manager for all voxel attributes. Owns AttributeStorage instances and manages brick allocations.

```cpp
auto registry = std::make_shared<AttributeRegistry>();
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("material", AttributeType::Uint32, 0u);

uint32_t brickID = registry->allocateBrick();
```

### AttributeStorage
Owns raw data for one attribute across ALL bricks. Single contiguous buffer with 512-element slots.

```cpp
// 10k bricks × 512 voxels × 4 bytes = 20MB per attribute
AttributeStorage storage("density", AttributeType::Float, 0.0f);
storage.reserve(10000);
```

### BrickView
Zero-copy view of brick data. Does NOT own data - references AttributeStorage slots.

```cpp
BrickView brick = registry->getBrick(brickID);
brick.set<float>("density", 42, 1.0f);  // Set voxel 42
float d = brick.get<float>("density", 42);  // Get voxel 42
```

### ArrayView (std::span)
Type alias for `std::span` - non-owning view over contiguous array.

```cpp
auto densityArray = brick.getAttributeArray<float>("density");
densityArray[0] = 1.0f;  // Direct write to backing storage
```

## Memory Layout

```
AttributeStorage "density" (float):
[Brick0: 512 floats][Brick1: 512 floats][Brick2: 512 floats]...
 ^                  ^                    ^
 BrickView 0        BrickView 1          BrickView 2

AttributeStorage "material" (uint32):
[Brick0: 512 u32s][Brick1: 512 u32s][Brick2: 512 u32s]...
 ^                ^                  ^
 BrickView 0      BrickView 1        BrickView 2
```

## Runtime Attribute Addition (Zero-Copy!)

```cpp
// Start with basic attributes
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("material", AttributeType::Uint32, 0u);

// Build 10k bricks
for (int i = 0; i < 10000; ++i) {
    uint32_t brickID = registry->allocateBrick();
    // Populate density + material
}

// Later: Add "roughness" WITHOUT copying existing data!
registry->addAttribute("roughness", AttributeType::Float, 0.5f);

// Existing data (density, material) stays at same memory locations
// Zero data movement!
```

## Integration with Spatial Structures

The SVO library (or any spatial structure) can use VoxelData by:

1. Creating an AttributeRegistry
2. Registering the key attribute (determines structure sparsity)
3. Adding additional attributes as needed
4. Allocating bricks through the registry
5. Using BrickViews to access voxel data

```cpp
// In VoxelInjection or similar:
void inject(AttributeRegistry* registry, IVoxelSampler* sampler) {
    // Sample voxels
    uint32_t brickID = registry->allocateBrick();
    BrickView brick = registry->getBrick(brickID);

    for (int i = 0; i < 512; ++i) {
        VoxelData voxel = sampler->sample(position);
        brick.set<float>("density", i, voxel.density);
        brick.set<uint32_t>("material", i, voxel.materialID);
    }
}
```

## Dependencies

- **GLM**: For vec3 types (vec3 attributes stored as 3 separate arrays)
- **C++23**: For `std::span` and fold expressions
- **No Vulkan**: This library is data-only, no graphics dependencies

## Future Enhancements

- [ ] GPU buffer management utilities
- [ ] Compression support (DXT, custom codecs)
- [ ] Serialization/deserialization
- [ ] Memory-mapped file backing
- [ ] Attribute type validation at runtime
