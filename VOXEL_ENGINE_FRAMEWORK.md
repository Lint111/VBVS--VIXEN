# Voxel Engine Framework: Modern Approaches with Gaia ECS and C++

## Executive Summary

This document outlines the correct approach to constructing a voxel engine framework in C++ using the Gaia Entity Component System (ECS) as the data backend. The framework is designed to handle dynamic data and support diverse tasks including rendering, physics simulation, procedural generation, and microstructure analysis through sparse data structures, GPU-accelerated rendering, and modular architecture patterns.

**Key Technology:**
- **Language:** C++20/23
- **Data Backend:** Gaia ECS (Entity-Component-System)
- **Graphics API:** Vulkan
- **Performance:** SIMD, cache-optimized layouts, GPU compute

**References:**
- L. Herzberger et al., "Residency Octree: A Hybrid Approach for Scalable Web-Based Multi-Volume Rendering," IEEE TVCG, 2023
- S. Daubner et al., "evoxels: A differentiable physics framework for voxel-based microstructure simulations," arXiv:2507.21748, 2025
- "Writing an efficient Vulkan renderer" (zeux.io)
- "The Perfect Voxel Engine" (voxely.net)
- Gaia-ECS Documentation (https://github.com/richardbiely/gaia-ecs)

---

## Table of Contents

1. [Core Architecture Principles](#core-architecture-principles)
2. [Gaia ECS Integration](#gaia-ecs-integration)
3. [Data Structure Selection](#data-structure-selection)
4. [Multi-Purpose Design Patterns](#multi-purpose-design-patterns)
5. [GPU-Accelerated Rendering](#gpu-accelerated-rendering)
6. [Dynamic Data Management](#dynamic-data-management)
7. [Performance Optimization](#performance-optimization)
8. [Implementation Roadmap](#implementation-roadmap)

---

## Core Architecture Principles

### 1. Separation of Concerns

A modern voxel engine must separate:

```
┌─────────────────────────────────────┐
│     Application Layer               │
│  (Game Logic, Physics, Rendering)   │
└─────────────────────────────────────┘
           ↕ API Interface
┌─────────────────────────────────────┐
│     Voxel Engine Core               │
│  (Gaia ECS + Voxel Operations)      │
└─────────────────────────────────────┘
           ↕ Backend Interface
┌─────────────────────────────────────┐
│  Gaia ECS Storage & GPU Backend     │
│  (Components, Entities, Vulkan)     │
└─────────────────────────────────────┘
```

### 2. Data-Oriented Design with ECS

Gaia ECS provides excellent foundations for data-oriented voxel engines:

- **Cache-friendly storage**: Components stored contiguously in memory
- **Efficient queries**: Fast iteration over voxels with specific properties
- **Parallel processing**: Natural parallelism through entity batching
- **Memory efficiency**: Only store data for existing voxels (sparse storage)
- **Flexible relationships**: Entity relationships for spatial hierarchies

### 3. Modularity for Multiple Use Cases

Different tasks require different component combinations:

| Task | Primary Components | Query Pattern |
|------|-------------------|---------------|
| **Rendering** | Position, Material, LODLevel | Frustum culling + LOD query |
| **Physics** | Position, Velocity, Mass | Spatial hash + neighbor query |
| **Procedural Gen** | Position, GeneratorSeed | Lazy generation on-demand |
| **Simulation** | Position, State, Gradient | Dense grid + differentiable ops |

---

## Gaia ECS Integration

### 1. Why Gaia ECS for Voxels?

**Advantages:**
- ✓ **Sparse by default**: Only store voxels that exist (no empty space waste)
- ✓ **Cache-friendly**: Component arrays are contiguous
- ✓ **Fast queries**: Archetype-based queries for rendering/physics
- ✓ **Relationships**: Parent-child links for octree hierarchies
- ✓ **Multithreading**: Built-in support for parallel iteration
- ✓ **Memory efficiency**: Automatic packing and alignment

**Core Concept:**
Each voxel is an **Entity** with **Components** describing its properties.

### 2. Component Definitions

```cpp
#include <gaia.h>
#include <glm/glm.hpp>

namespace voxel {

// Core voxel components
struct Position {
    glm::ivec3 coord;       // Voxel grid coordinates

    // Morton encoding for spatial coherence
    uint64_t morton_code() const {
        return morton_encode(coord.x, coord.y, coord.z);
    }
};

struct Material {
    uint8_t material_id;
    uint8_t flags;          // Solid, transparent, etc.
    uint16_t padding;
};

struct VoxelData {
    uint32_t color;         // RGBA8888
    float density;          // For marching cubes
    uint16_t metadata;      // Custom data
};

// Rendering components
struct RenderData {
    uint32_t lod_level;
    float screen_space_size;
    bool visible;
};

struct OctreeNode {
    gaia::ecs::Entity parent;
    gaia::ecs::Entity children[8];  // Child entities
    uint8_t child_mask;              // Which children exist
    glm::vec3 bounds_min;
    glm::vec3 bounds_max;
};

// Physics components
struct PhysicsVoxel {
    glm::vec3 velocity;
    float mass;
    uint32_t collision_group;
};

// Streaming/residency components
struct Residency {
    bool resident;           // Is data in memory?
    float priority;          // LRU/importance score
    uint64_t last_access;    // Timestamp
};

// Dirty tracking for GPU uploads
struct DirtyFlag {
    bool modified;
    uint32_t frame_modified;
};

} // namespace voxel
```

### 3. World Initialization

```cpp
#include <gaia.h>

class VoxelWorld {
public:
    VoxelWorld() {
        // Initialize Gaia world
        world_ = gaia::ecs::World::create();

        // Register components
        register_components();

        // Create spatial index
        create_spatial_index();
    }

    // Create a voxel entity
    gaia::ecs::Entity create_voxel(const glm::ivec3& pos,
                                    uint8_t material,
                                    uint32_t color) {
        auto entity = world_.add();

        world_.add<voxel::Position>(entity, {pos});
        world_.add<voxel::Material>(entity, {material, 0, 0});
        world_.add<voxel::VoxelData>(entity, {color, 1.0f, 0});
        world_.add<voxel::DirtyFlag>(entity, {true, 0});

        return entity;
    }

    // Query voxels in a region
    template<typename Func>
    void query_region(const AABB& bounds, Func&& func) {
        auto query = world_.query()
            .all<voxel::Position, voxel::VoxelData>();

        query.each([&](gaia::ecs::Entity e,
                      const voxel::Position& pos,
                      const voxel::VoxelData& data) {
            if (bounds.contains(pos.coord)) {
                func(e, pos, data);
            }
        });
    }

private:
    gaia::ecs::World world_;

    void register_components() {
        // Gaia automatically registers components
        // when they're first used
    }

    void create_spatial_index() {
        // Create entities for spatial partitioning
        // (chunk entities, octree nodes, etc.)
    }
};
```

### 4. Chunk-Based Organization

```cpp
// Chunk entity wraps multiple voxel entities
struct ChunkComponent {
    static constexpr int SIZE = 32;
    glm::ivec3 chunk_coord;
    uint32_t voxel_count;       // Number of solid voxels
    AABB bounds;
};

class ChunkManager {
public:
    explicit ChunkManager(gaia::ecs::World& world) : world_(world) {}

    // Get or create chunk entity
    gaia::ecs::Entity get_chunk(const glm::ivec3& chunk_coord) {
        uint64_t key = hash_coord(chunk_coord);

        auto it = chunk_map_.find(key);
        if (it != chunk_map_.end()) {
            return it->second;
        }

        // Create new chunk entity
        auto chunk = world_.add();
        world_.add<ChunkComponent>(chunk, {
            chunk_coord,
            0,
            compute_bounds(chunk_coord)
        });

        chunk_map_[key] = chunk;
        return chunk;
    }

    // Add voxel to chunk (set parent relationship)
    void add_voxel_to_chunk(gaia::ecs::Entity voxel,
                           gaia::ecs::Entity chunk) {
        // Gaia supports entity relationships
        world_.add_pair<voxel::InChunk>(voxel, chunk);

        // Update chunk voxel count
        auto& chunk_comp = world_.get_mut<ChunkComponent>(chunk);
        chunk_comp.voxel_count++;
    }

    // Query all voxels in a chunk
    template<typename Func>
    void for_each_voxel_in_chunk(gaia::ecs::Entity chunk, Func&& func) {
        auto query = world_.query()
            .all<voxel::Position, voxel::VoxelData>()
            .pair<voxel::InChunk>(chunk);

        query.each([&](gaia::ecs::Entity e,
                      const voxel::Position& pos,
                      const voxel::VoxelData& data) {
            func(e, pos, data);
        });
    }

private:
    gaia::ecs::World& world_;
    std::unordered_map<uint64_t, gaia::ecs::Entity> chunk_map_;

    AABB compute_bounds(const glm::ivec3& chunk_coord) {
        constexpr int SIZE = ChunkComponent::SIZE;
        glm::vec3 min = glm::vec3(chunk_coord * SIZE);
        glm::vec3 max = min + glm::vec3(SIZE);
        return {min, max};
    }
};
```

---

## Data Structure Selection

### 1. Sparse Voxel Octree with Gaia ECS

**Best for:** Rendering, sparse data, large-scale scenes

```cpp
// Octree built using entity relationships
class OctreeBuilder {
public:
    explicit OctreeBuilder(gaia::ecs::World& world) : world_(world) {}

    // Build octree from voxel entities
    gaia::ecs::Entity build_octree(const std::vector<gaia::ecs::Entity>& voxels,
                                    const AABB& root_bounds,
                                    int max_depth) {
        // Create root node
        auto root = world_.add();
        world_.add<voxel::OctreeNode>(root, {
            gaia::ecs::Entity::INVALID,  // No parent
            {},                          // No children yet
            0,                           // No child mask
            root_bounds.min,
            root_bounds.max
        });

        // Recursively build tree
        build_recursive(root, voxels, root_bounds, 0, max_depth);

        return root;
    }

private:
    gaia::ecs::World& world_;

    void build_recursive(gaia::ecs::Entity node,
                        const std::vector<gaia::ecs::Entity>& voxels,
                        const AABB& bounds,
                        int depth,
                        int max_depth) {
        if (depth >= max_depth || voxels.size() <= 8) {
            // Leaf node: link voxels to this node
            for (auto voxel : voxels) {
                world_.add_pair<voxel::OctreeChild>(voxel, node);
            }
            return;
        }

        // Subdivide
        auto& node_data = world_.get_mut<voxel::OctreeNode>(node);
        glm::vec3 center = (bounds.min + bounds.max) * 0.5f;

        for (int i = 0; i < 8; ++i) {
            AABB child_bounds = compute_octant_bounds(bounds, center, i);

            // Collect voxels in this octant
            std::vector<gaia::ecs::Entity> child_voxels;
            for (auto voxel : voxels) {
                auto& pos = world_.get<voxel::Position>(voxel);
                if (child_bounds.contains(pos.coord)) {
                    child_voxels.push_back(voxel);
                }
            }

            if (child_voxels.empty()) continue;

            // Create child node
            auto child = world_.add();
            world_.add<voxel::OctreeNode>(child, {
                node,
                {},
                0,
                child_bounds.min,
                child_bounds.max
            });

            node_data.children[i] = child;
            node_data.child_mask |= (1 << i);

            // Recurse
            build_recursive(child, child_voxels, child_bounds, depth + 1, max_depth);
        }
    }

    AABB compute_octant_bounds(const AABB& parent,
                               const glm::vec3& center,
                               int octant) {
        // Compute bounds for octant 0-7
        AABB result;
        result.min.x = (octant & 1) ? center.x : parent.min.x;
        result.min.y = (octant & 2) ? center.y : parent.min.y;
        result.min.z = (octant & 4) ? center.z : parent.min.z;
        result.max.x = (octant & 1) ? parent.max.x : center.x;
        result.max.y = (octant & 2) ? parent.max.y : center.y;
        result.max.z = (octant & 4) ? parent.max.z : center.z;
        return result;
    }
};
```

### 2. Residency Octree with Streaming

**Best for:** Web-based rendering, multi-volume data, scalable systems

```cpp
struct ResidencyComponent {
    bool resident;
    float lru_priority;
    uint64_t last_frame_used;
    size_t data_size_bytes;
};

class ResidencyOctreeManager {
public:
    explicit ResidencyOctreeManager(gaia::ecs::World& world, size_t memory_budget)
        : world_(world), memory_budget_(memory_budget), resident_memory_(0) {}

    // Ensure node is resident for rendering
    void ensure_resident(gaia::ecs::Entity node, uint64_t current_frame) {
        auto& residency = world_.get_mut<ResidencyComponent>(node);

        if (!residency.resident) {
            // Load node data
            load_node(node);
            residency.resident = true;
            resident_memory_ += residency.data_size_bytes;
        }

        residency.last_frame_used = current_frame;
        residency.lru_priority = compute_priority(node);
    }

    // Evict low-priority nodes if over budget
    void manage_residency(uint64_t current_frame) {
        if (resident_memory_ <= memory_budget_) {
            return;
        }

        // Collect all resident nodes
        struct NodePriority {
            gaia::ecs::Entity entity;
            float priority;
        };
        std::vector<NodePriority> nodes;

        auto query = world_.query().all<voxel::OctreeNode, ResidencyComponent>();
        query.each([&](gaia::ecs::Entity e, const ResidencyComponent& res) {
            if (res.resident) {
                nodes.push_back({e, res.lru_priority});
            }
        });

        // Sort by priority (lowest first)
        std::sort(nodes.begin(), nodes.end(),
                 [](const auto& a, const auto& b) {
                     return a.priority < b.priority;
                 });

        // Evict until under budget
        for (const auto& node_priority : nodes) {
            if (resident_memory_ <= memory_budget_) break;

            evict_node(node_priority.entity);
        }
    }

private:
    gaia::ecs::World& world_;
    size_t memory_budget_;
    size_t resident_memory_;

    void load_node(gaia::ecs::Entity node) {
        // Load node data from disk/network
        // Populate voxel entities under this node
    }

    void evict_node(gaia::ecs::Entity node) {
        auto& residency = world_.get_mut<ResidencyComponent>(node);

        // Remove voxel entities from memory
        // Keep just the metadata

        resident_memory_ -= residency.data_size_bytes;
        residency.resident = false;
    }

    float compute_priority(gaia::ecs::Entity node) {
        // Higher priority = more important to keep
        // Based on: distance to camera, screen space size, access frequency
        return 0.0f;  // Placeholder
    }
};
```

### 3. Differentiable Voxel Grid

**Best for:** Physics simulation, optimization, neural networks

```cpp
// Dense grid stored as component on a single entity
struct DifferentiableGrid {
    std::vector<float> values;      // Voxel properties
    std::vector<float> gradients;   // ∂L/∂voxel
    glm::ivec3 resolution;
    glm::vec3 spacing;

    size_t index(int x, int y, int z) const {
        return x + y * resolution.x + z * resolution.x * resolution.y;
    }

    float& operator()(int x, int y, int z) {
        return values[index(x, y, z)];
    }

    const float& operator()(int x, int y, int z) const {
        return values[index(x, y, z)];
    }
};

// System for differentiable physics simulation
class DifferentiablePhysicsSystem {
public:
    explicit DifferentiablePhysicsSystem(gaia::ecs::World& world)
        : world_(world) {}

    // Forward pass: simulate physics
    void forward_pass(float dt) {
        auto query = world_.query().all<DifferentiableGrid>();

        query.each([&](DifferentiableGrid& grid) {
            // Implement physics simulation
            // e.g., fluid dynamics, heat transfer, etc.
            simulate_step(grid, dt);
        });
    }

    // Backward pass: compute gradients
    void backward_pass(const std::vector<float>& loss_gradient) {
        auto query = world_.query().all<DifferentiableGrid>();

        query.each([&](DifferentiableGrid& grid) {
            // Backpropagate gradients
            compute_gradients(grid, loss_gradient);
        });
    }

private:
    gaia::ecs::World& world_;

    void simulate_step(DifferentiableGrid& grid, float dt) {
        // Physics simulation code
    }

    void compute_gradients(DifferentiableGrid& grid,
                          const std::vector<float>& loss_grad) {
        // Gradient computation (reverse-mode autodiff)
    }
};
```

---

## Multi-Purpose Design Patterns

### Pattern 1: System-Based Processing

ECS systems naturally separate concerns:

```cpp
// Rendering system
class VoxelRenderingSystem {
public:
    explicit VoxelRenderingSystem(gaia::ecs::World& world, VulkanRenderer& renderer)
        : world_(world), renderer_(renderer) {}

    void update(const Camera& camera) {
        // 1. Frustum culling
        frustum_cull(camera);

        // 2. LOD selection
        update_lod(camera);

        // 3. Upload visible voxels to GPU
        upload_to_gpu();
    }

private:
    gaia::ecs::World& world_;
    VulkanRenderer& renderer_;

    void frustum_cull(const Camera& camera) {
        auto query = world_.query()
            .all<voxel::Position, voxel::RenderData>();

        query.each([&](const voxel::Position& pos,
                      voxel::RenderData& render) {
            render.visible = camera.frustum.contains(pos.coord);
        });
    }

    void update_lod(const Camera& camera) {
        auto query = world_.query()
            .all<voxel::Position, voxel::RenderData>()
            .any<voxel::OctreeNode>();

        query.each([&](const voxel::Position& pos,
                      voxel::RenderData& render) {
            if (!render.visible) return;

            float distance = glm::distance(
                glm::vec3(pos.coord),
                camera.position
            );
            render.lod_level = compute_lod_level(distance);
        });
    }

    void upload_to_gpu() {
        // Collect dirty visible voxels
        std::vector<GPUVoxel> gpu_voxels;

        auto query = world_.query()
            .all<voxel::Position, voxel::VoxelData,
                 voxel::RenderData, voxel::DirtyFlag>();

        query.each([&](const voxel::Position& pos,
                      const voxel::VoxelData& data,
                      const voxel::RenderData& render,
                      voxel::DirtyFlag& dirty) {
            if (render.visible && dirty.modified) {
                gpu_voxels.push_back(to_gpu_voxel(pos, data, render));
                dirty.modified = false;
            }
        });

        if (!gpu_voxels.empty()) {
            renderer_.upload_voxels(gpu_voxels);
        }
    }

    uint32_t compute_lod_level(float distance) const {
        // Distance-based LOD
        if (distance < 10.0f) return 0;
        if (distance < 50.0f) return 1;
        if (distance < 200.0f) return 2;
        return 3;
    }
};
```

```cpp
// Physics system
class VoxelPhysicsSystem {
public:
    explicit VoxelPhysicsSystem(gaia::ecs::World& world) : world_(world) {}

    void update(float dt) {
        // Update velocities
        auto query = world_.query()
            .all<voxel::Position, voxel::PhysicsVoxel>();

        query.each([&](voxel::Position& pos,
                      voxel::PhysicsVoxel& physics) {
            // Apply physics
            physics.velocity += glm::vec3(0, -9.8f, 0) * dt;  // Gravity
            pos.coord += glm::ivec3(physics.velocity * dt);

            // Mark as dirty for GPU upload
        });

        // Collision detection
        detect_collisions();
    }

private:
    gaia::ecs::World& world_;

    void detect_collisions() {
        // Use spatial hash for broad phase
        // Check voxel-voxel collisions
    }
};
```

### Pattern 2: Component Views for Different Tasks

```cpp
// Adapter class for different use cases
class VoxelWorld {
public:
    explicit VoxelWorld(gaia::ecs::World& world) : world_(world) {}

    // Get rendering view
    auto get_renderable_voxels() {
        return world_.query()
            .all<voxel::Position, voxel::VoxelData, voxel::RenderData>()
            .none<voxel::Residency>()  // Exclude non-resident
            .get();
    }

    // Get physics view
    auto get_physics_voxels() {
        return world_.query()
            .all<voxel::Position, voxel::PhysicsVoxel>()
            .get();
    }

    // Get simulation view
    auto get_simulation_grids() {
        return world_.query()
            .all<DifferentiableGrid>()
            .get();
    }

private:
    gaia::ecs::World& world_;
};
```

---

## GPU-Accelerated Rendering

### 1. Efficient Vulkan Architecture

**Key Concepts:**
- **Minimize state changes**: Batch by material/pipeline
- **Use indirect rendering**: GPU-driven culling
- **Persistent mapping**: Keep buffers mapped
- **Async compute**: Overlap rendering and updates

```cpp
class VulkanVoxelRenderer {
public:
    VulkanVoxelRenderer(VkDevice device, VkPhysicalDevice physical_device) {
        create_buffers();
        create_pipelines();
        create_descriptor_sets();
    }

    void render_frame(const Camera& camera, gaia::ecs::World& world) {
        // 1. Compute pass: Update LOD, cull voxels
        dispatch_compute_culling(camera, world);

        // 2. Compute pass: Generate indirect draw commands
        dispatch_indirect_generation();

        // 3. Graphics pass: Draw visible voxels
        draw_indirect();
    }

private:
    VkDevice device_;
    VkBuffer octree_buffer_;          // GPU-side octree
    VkBuffer material_buffer_;        // Material properties
    VkBuffer indirect_buffer_;        // VkDrawIndirectCommand
    VkBuffer visible_voxels_buffer_;  // Culled voxels

    VkPipeline compute_culling_pipeline_;
    VkPipeline compute_indirect_pipeline_;
    VkPipeline graphics_pipeline_;

    void dispatch_compute_culling(const Camera& camera, gaia::ecs::World& world) {
        // Upload camera frustum to GPU
        upload_camera_data(camera);

        // Dispatch compute shader for frustum culling
        VkCommandBuffer cmd = begin_compute_commands();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                         compute_culling_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);

        uint32_t voxel_count = get_voxel_count(world);
        uint32_t workgroup_count = (voxel_count + 255) / 256;
        vkCmdDispatch(cmd, workgroup_count, 1, 1);

        end_compute_commands(cmd);
    }

    void dispatch_indirect_generation() {
        // Generate VkDrawIndirectCommand on GPU
        VkCommandBuffer cmd = begin_compute_commands();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                         compute_indirect_pipeline_);
        vkCmdDispatch(cmd, 1, 1, 1);

        end_compute_commands(cmd);
    }

    void draw_indirect() {
        VkCommandBuffer cmd = begin_graphics_commands();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         graphics_pipeline_);
        vkCmdDrawIndirect(cmd, indirect_buffer_, 0, 1, 0);

        end_graphics_commands(cmd);
    }
};
```

### 2. GPU-Side Octree Traversal

```glsl
// Compute shader for voxel raycasting
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform OctreeData {
    uint root_index;
    float voxel_size;
    vec3 world_min;
    uint max_depth;
} octree;

layout(set = 0, binding = 1) buffer OctreeNodes {
    uvec2 nodes[];  // Packed: children ptr + metadata
};

layout(set = 0, binding = 2, rgba8) uniform image2D output_image;

layout(set = 0, binding = 3) uniform CameraData {
    mat4 view_proj_inverse;
    vec3 camera_position;
} camera;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(imageSize(output_image));

    // Generate ray from camera
    vec4 clip_near = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4 clip_far = vec4(uv * 2.0 - 1.0, 1.0, 1.0);

    vec4 world_near = camera.view_proj_inverse * clip_near;
    vec4 world_far = camera.view_proj_inverse * clip_far;

    world_near /= world_near.w;
    world_far /= world_far.w;

    vec3 ray_origin = world_near.xyz;
    vec3 ray_direction = normalize(world_far.xyz - world_near.xyz);

    // DDA-style octree traversal
    vec4 color = raycast_octree(ray_origin, ray_direction);

    imageStore(output_image, pixel, color);
}

vec4 raycast_octree(vec3 ray_origin, vec3 ray_direction) {
    // Stack-based iterative traversal (no recursion on GPU)
    const int MAX_STACK_DEPTH = 32;
    uint stack[MAX_STACK_DEPTH];
    int stack_ptr = 0;

    stack[stack_ptr++] = octree.root_index;

    while (stack_ptr > 0) {
        uint current = stack[--stack_ptr];
        uvec2 node_data = nodes[current];

        // Compute node bounds
        vec3 node_min, node_max;
        compute_node_bounds(current, node_min, node_max);

        // Intersect ray with node AABB
        float t_near, t_far;
        if (!intersect_aabb(ray_origin, ray_direction,
                           node_min, node_max, t_near, t_far)) {
            continue;
        }

        // Check if leaf node
        if ((node_data.y & 0x80000000u) != 0) {
            // Leaf node: return voxel color
            return unpack_color(node_data.x);
        }

        // Internal node: push children to stack (far to near)
        uint child_mask = node_data.y & 0xFF;
        push_children_sorted(stack, stack_ptr, current,
                           child_mask, ray_direction);
    }

    return vec4(0.0, 0.0, 0.0, 0.0); // Miss
}

bool intersect_aabb(vec3 origin, vec3 dir,
                   vec3 box_min, vec3 box_max,
                   out float t_near, out float t_far) {
    vec3 inv_dir = 1.0 / dir;
    vec3 t0 = (box_min - origin) * inv_dir;
    vec3 t1 = (box_max - origin) * inv_dir;

    vec3 t_min = min(t0, t1);
    vec3 t_max = max(t0, t1);

    t_near = max(max(t_min.x, t_min.y), t_min.z);
    t_far = min(min(t_max.x, t_max.y), t_max.z);

    return t_near <= t_far && t_far >= 0.0;
}
```

### 3. Level of Detail (LOD) System

```cpp
class LODSelector {
public:
    LODSelector(float error_threshold)
        : error_threshold_(error_threshold) {}

    uint32_t select_lod(const voxel::OctreeNode& node,
                       const Camera& camera) const {
        glm::vec3 node_center = (node.bounds_min + node.bounds_max) * 0.5f;
        float node_size = glm::length(node.bounds_max - node.bounds_min);

        float distance = glm::distance(node_center, camera.position);
        float projected_size = node_size / distance;
        float screen_space_size = projected_size * camera.fov;

        // Geometric error metric
        float error = node_size / screen_space_size;

        // Determine LOD level
        uint32_t lod = 0;
        while (error < error_threshold_ && lod < 4) {
            lod++;
            error *= 2.0f;
        }

        return lod;
    }

private:
    float error_threshold_;
};
```

---

## Dynamic Data Management

### 1. Incremental Updates with ECS

```cpp
// Modification tracking system
class VoxelModificationSystem {
public:
    explicit VoxelModificationSystem(gaia::ecs::World& world)
        : world_(world) {}

    // Modify voxel (marks as dirty)
    void set_voxel(gaia::ecs::Entity entity, uint32_t new_color) {
        auto& data = world_.get_mut<voxel::VoxelData>(entity);
        data.color = new_color;

        // Mark as dirty
        auto& dirty = world_.get_mut<voxel::DirtyFlag>(entity);
        dirty.modified = true;
        dirty.frame_modified = current_frame_;
    }

    // Batch updates
    void apply_batch(const std::vector<VoxelUpdate>& updates) {
        for (const auto& update : updates) {
            set_voxel(update.entity, update.new_color);
        }
    }

    // Process dirty voxels for GPU upload
    std::vector<GPUVoxelData> collect_dirty_voxels() {
        std::vector<GPUVoxelData> result;

        auto query = world_.query()
            .all<voxel::Position, voxel::VoxelData, voxel::DirtyFlag>();

        query.each([&](gaia::ecs::Entity e,
                      const voxel::Position& pos,
                      const voxel::VoxelData& data,
                      voxel::DirtyFlag& dirty) {
            if (dirty.modified) {
                result.push_back({
                    pos.coord,
                    data.color,
                    data.density
                });
                dirty.modified = false;
            }
        });

        return result;
    }

    void advance_frame() { current_frame_++; }

private:
    gaia::ecs::World& world_;
    uint32_t current_frame_ = 0;
};
```

### 2. Multi-Threaded Processing

Gaia ECS supports parallel iteration:

```cpp
class ParallelVoxelProcessor {
public:
    explicit ParallelVoxelProcessor(gaia::ecs::World& world)
        : world_(world) {}

    // Process voxels in parallel
    void process_all(std::function<void(voxel::VoxelData&)> func) {
        auto query = world_.query().all<voxel::VoxelData>();

        // Gaia supports parallel execution
        query.each_parallel([&](voxel::VoxelData& data) {
            func(data);
        });
    }

    // Example: Parallel density field evaluation
    void update_density_field(const std::function<float(glm::vec3)>& field_func) {
        auto query = world_.query()
            .all<voxel::Position, voxel::VoxelData>();

        query.each_parallel([&](const voxel::Position& pos,
                               voxel::VoxelData& data) {
            data.density = field_func(glm::vec3(pos.coord));
        });
    }

private:
    gaia::ecs::World& world_;
};
```

### 3. Streaming and Residency Management

```cpp
class StreamingManager {
public:
    StreamingManager(gaia::ecs::World& world, size_t memory_budget)
        : world_(world), memory_budget_(memory_budget) {}

    void update(const Camera& camera) {
        current_frame_++;

        // 1. Determine needed nodes based on camera
        auto needed_nodes = compute_working_set(camera);

        // 2. Ensure needed nodes are resident
        for (auto node : needed_nodes) {
            ensure_resident(node);
        }

        // 3. Evict low-priority nodes if over budget
        manage_memory_budget();
    }

private:
    gaia::ecs::World& world_;
    size_t memory_budget_;
    uint64_t current_frame_ = 0;

    std::vector<gaia::ecs::Entity> compute_working_set(const Camera& camera) {
        std::vector<gaia::ecs::Entity> needed;

        auto query = world_.query()
            .all<voxel::OctreeNode, ResidencyComponent>();

        query.each([&](gaia::ecs::Entity e,
                      const voxel::OctreeNode& node,
                      const ResidencyComponent& res) {
            // Frustum cull
            if (camera.frustum.intersects({node.bounds_min, node.bounds_max})) {
                // LOD check
                float distance = glm::distance(
                    (node.bounds_min + node.bounds_max) * 0.5f,
                    camera.position
                );

                if (should_load(node, distance)) {
                    needed.push_back(e);
                }
            }
        });

        return needed;
    }

    void ensure_resident(gaia::ecs::Entity node) {
        auto& res = world_.get_mut<ResidencyComponent>(node);

        if (!res.resident) {
            load_node_data(node);
            res.resident = true;
        }

        res.last_frame_used = current_frame_;
    }

    void manage_memory_budget() {
        // Similar to earlier implementation
    }

    bool should_load(const voxel::OctreeNode& node, float distance) const {
        // Distance-based decision
        return distance < 100.0f;  // Simplified
    }

    void load_node_data(gaia::ecs::Entity node) {
        // Load voxel entities for this node from disk/network
    }
};
```

---

## Performance Optimization

### 1. Memory Layout Optimization

```cpp
// Cache-friendly packed voxel
struct alignas(16) PackedVoxel {
    uint16_t x, y, z;       // Position (6 bytes)
    uint8_t material;       // Material ID (1 byte)
    uint8_t flags;          // Flags (1 byte)
    uint32_t color;         // RGBA8888 (4 bytes)
    uint32_t _padding;      // Padding to 16 bytes
};

static_assert(sizeof(PackedVoxel) == 16, "Voxel must be 16 bytes");

// Morton encoding for spatial coherence
inline uint64_t morton_encode(uint32_t x, uint32_t y, uint32_t z) {
    auto expand_bits = [](uint32_t v) -> uint64_t {
        uint64_t x = v & 0x1fffff;
        x = (x | x << 32) & 0x1f00000000ffff;
        x = (x | x << 16) & 0x1f0000ff0000ff;
        x = (x | x << 8)  & 0x100f00f00f00f00f;
        x = (x | x << 4)  & 0x10c30c30c30c30c3;
        x = (x | x << 2)  & 0x1249249249249249;
        return x;
    };

    return expand_bits(x) | (expand_bits(y) << 1) | (expand_bits(z) << 2);
}

// Sort voxels by Morton code for cache coherence
void sort_voxels_by_morton(gaia::ecs::World& world) {
    // Gaia maintains component arrays
    // We can influence locality through entity creation order
    // Or use custom sorting in our systems
}
```

### 2. SIMD Optimization

```cpp
#include <immintrin.h>  // AVX2

// Process 8 voxel densities in parallel
void evaluate_density_field_simd(const std::vector<glm::vec3>& positions,
                                 std::vector<float>& densities) {
    const size_t count = positions.size();
    densities.resize(count);

    // Process 8 at a time with AVX2
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 x = _mm256_set_ps(
            positions[i+7].x, positions[i+6].x, positions[i+5].x, positions[i+4].x,
            positions[i+3].x, positions[i+2].x, positions[i+1].x, positions[i+0].x
        );
        __m256 y = _mm256_set_ps(
            positions[i+7].y, positions[i+6].y, positions[i+5].y, positions[i+4].y,
            positions[i+3].y, positions[i+2].y, positions[i+1].y, positions[i+0].y
        );
        __m256 z = _mm256_set_ps(
            positions[i+7].z, positions[i+6].z, positions[i+5].z, positions[i+4].z,
            positions[i+3].z, positions[i+2].z, positions[i+1].z, positions[i+0].z
        );

        // Compute: sqrt(x² + y² + z²)
        __m256 x2 = _mm256_mul_ps(x, x);
        __m256 y2 = _mm256_mul_ps(y, y);
        __m256 z2 = _mm256_mul_ps(z, z);
        __m256 sum = _mm256_add_ps(_mm256_add_ps(x2, y2), z2);
        __m256 result = _mm256_sqrt_ps(sum);

        // Store results
        _mm256_storeu_ps(&densities[i], result);
    }

    // Handle remainder
    for (; i < count; ++i) {
        const auto& p = positions[i];
        densities[i] = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    }
}
```

### 3. GPU Compute for Bulk Operations

```cpp
// Marching Cubes on GPU
class MarchingCubesCompute {
public:
    MarchingCubesCompute(VkDevice device) {
        create_pipeline();
    }

    VkBuffer generate_mesh(VkBuffer voxel_density_buffer,
                          const glm::ivec3& grid_size) {
        VkCommandBuffer cmd = begin_compute();

        // Bind marching cubes pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

        // Set push constants (grid size, etc.)
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(grid_size), &grid_size);

        // Dispatch one workgroup per voxel
        vkCmdDispatch(cmd,
                     (grid_size.x + 7) / 8,
                     (grid_size.y + 7) / 8,
                     (grid_size.z + 7) / 8);

        end_compute(cmd);

        return mesh_buffer_;  // Vertex + index buffers
    }

private:
    VkDevice device_;
    VkPipeline pipeline_;
    VkPipelineLayout pipeline_layout_;
    VkBuffer mesh_buffer_;

    void create_pipeline() {
        // Load marching_cubes.comp.spv
        // Create compute pipeline
    }
};
```

### 4. Profiling and Metrics with ECS

```cpp
// Metrics component on world entity
struct VoxelEngineMetrics {
    // Memory
    size_t total_voxels;
    size_t resident_nodes;
    size_t memory_used;

    // Performance
    float frame_time_ms;
    float traversal_time_ms;
    float modification_time_ms;

    // Rendering
    size_t visible_voxels;
    uint32_t draw_calls;
    uint32_t triangles_rendered;
};

class MetricsCollector {
public:
    explicit MetricsCollector(gaia::ecs::World& world) : world_(world) {
        // Create metrics entity
        metrics_entity_ = world_.add();
        world_.add<VoxelEngineMetrics>(metrics_entity_, {});
    }

    void update() {
        auto& metrics = world_.get_mut<VoxelEngineMetrics>(metrics_entity_);

        // Count voxels
        metrics.total_voxels = world_.query()
            .all<voxel::Position>()
            .count();

        // Count resident nodes
        auto res_query = world_.query()
            .all<ResidencyComponent>();
        metrics.resident_nodes = res_query.count_if(
            [](const ResidencyComponent& res) { return res.resident; }
        );

        // Memory usage
        metrics.memory_used = estimate_memory_usage();
    }

    void report() const {
        const auto& metrics = world_.get<VoxelEngineMetrics>(metrics_entity_);

        std::cout << "Voxel Engine Metrics:\n";
        std::cout << "  Memory: " << (metrics.memory_used / 1000000.0) << " MB\n";
        std::cout << "  Frame time: " << metrics.frame_time_ms << " ms\n";
        std::cout << "  Visible voxels: " << metrics.visible_voxels << "\n";
    }

private:
    gaia::ecs::World& world_;
    gaia::ecs::Entity metrics_entity_;

    size_t estimate_memory_usage() const {
        // Estimate based on component sizes and entity count
        return world_.query().all<voxel::Position>().count() * sizeof(PackedVoxel);
    }
};
```

---

## Implementation Roadmap

### Phase 1: Gaia ECS Foundation (Weeks 1-3)

**Goals:**
- Integrate Gaia ECS library
- Define voxel components
- Implement basic CRUD operations

**Deliverables:**
```cpp
✓ Gaia ECS integration in CMake
✓ Component definitions (Position, Material, VoxelData, etc.)
✓ VoxelWorld class with entity creation
✓ Basic queries (get/set/region)
✓ Unit tests for component operations
```

**Key Files:**
```
src/voxel/components.hpp       // Component definitions
src/voxel/world.hpp            // VoxelWorld class
src/voxel/world.cpp            // Implementation
tests/test_voxel_world.cpp     // Unit tests
```

### Phase 2: Octree and Spatial Structures (Weeks 4-6)

**Goals:**
- Build octree using entity relationships
- Implement chunk management
- Spatial queries and traversal

**Deliverables:**
```cpp
✓ OctreeBuilder class
✓ ChunkManager class
✓ Spatial query system
✓ Entity relationship graph for hierarchy
✓ Tests for octree operations
```

**Key Files:**
```
src/voxel/octree.hpp           // Octree builder
src/voxel/chunk_manager.hpp    // Chunk management
src/voxel/spatial_query.hpp    // Query interface
```

### Phase 3: Vulkan Rendering (Weeks 7-9)

**Goals:**
- Set up Vulkan renderer
- GPU buffer management
- Compute shaders for culling

**Deliverables:**
```cpp
✓ VulkanVoxelRenderer class
✓ Buffer upload system (ECS -> GPU)
✓ Compute shader for frustum culling
✓ Raycasting shader
✓ Basic rendering loop
```

**Key Files:**
```
src/render/vulkan_renderer.hpp
src/render/vulkan_renderer.cpp
shaders/raycast.comp           // Raycasting shader
shaders/culling.comp           // Frustum culling
```

### Phase 4: Dynamic Updates and Streaming (Weeks 10-12)

**Goals:**
- Modification tracking system
- Residency management
- Streaming from disk/network

**Deliverables:**
```cpp
✓ VoxelModificationSystem
✓ DirtyFlag tracking
✓ StreamingManager with LOD
✓ ResidencyOctreeManager
✓ Parallel update system
```

**Key Files:**
```
src/voxel/modification_system.hpp
src/voxel/streaming_manager.hpp
src/voxel/residency.hpp
```

### Phase 5: Physics and Simulation (Weeks 13-15)

**Goals:**
- Physics system integration
- Differentiable grid support
- Collision detection

**Deliverables:**
```cpp
✓ VoxelPhysicsSystem
✓ DifferentiableGrid component
✓ DifferentiablePhysicsSystem
✓ Collision detection with spatial hash
✓ Integration tests
```

**Key Files:**
```
src/physics/voxel_physics.hpp
src/physics/differentiable.hpp
src/physics/collision.hpp
```

### Phase 6: Optimization (Weeks 16-18)

**Goals:**
- Profile and optimize hotspots
- SIMD implementation
- Advanced GPU techniques

**Deliverables:**
```cpp
✓ MetricsCollector system
✓ SIMD-optimized density evaluation
✓ Morton-ordered entity creation
✓ Indirect rendering
✓ Performance benchmarks
```

**Key Files:**
```
src/voxel/metrics.hpp
src/voxel/simd_ops.hpp
benchmarks/bench_voxel.cpp
```

---

## Best Practices Summary

### DO:
✓ **Use Gaia ECS components for all voxel data** - Leverage cache-friendly storage
✓ **Query only needed components** - Don't query Position if you don't need it
✓ **Use entity relationships for hierarchies** - Natural octree/chunk parent-child links
✓ **Batch modifications** - Group updates to minimize ECS overhead
✓ **Exploit parallel queries** - Use Gaia's parallel iteration
✓ **Implement LOD based on screen-space error** - Essential for large scenes
✓ **Use Morton ordering** - Create entities in spatial order for cache coherence
✓ **Profile with metrics component** - Track performance in ECS itself
✓ **Separate rendering/physics/simulation** - Different systems for different tasks
✓ **Use dirty flags for GPU uploads** - Only upload changed voxels

### DON'T:
✗ **Store empty space as entities** - ECS overhead per entity; only create solid voxels
✗ **Query all components when filtering** - Specify minimal component set
✗ **Modify entities during parallel iteration** - Use mutation queue pattern
✗ **Mix rendering and simulation data** - Keep concerns separate
✗ **Ignore memory budget** - Implement residency management early
✗ **Skip frustum culling** - Always cull before rendering
✗ **Use deep recursion on GPU** - Iterative octree traversal only
✗ **Create entities for every potential voxel** - Sparse storage only
✗ **Reupload entire buffers** - Incremental updates via dirty tracking
✗ **Lock entire world for updates** - Gaia is thread-safe for reads

---

## Gaia ECS Best Practices for Voxels

### 1. Component Design

**Do:**
- Keep components small (prefer multiple small components over one large)
- Use standard-layout types for components
- Align components to cache lines when beneficial

**Example:**
```cpp
// Good: Small, focused components
struct Position { glm::ivec3 coord; };
struct Material { uint8_t id; uint8_t flags; };

// Bad: Monolithic component
struct VoxelEverything {
    glm::ivec3 position;
    uint8_t material;
    glm::vec3 velocity;
    float temperature;
    uint32_t neighbors[26];
    // ... etc
};
```

### 2. Query Optimization

**Do:**
- Minimize component set in queries
- Use `.any<>()` and `.none<>()` for filtering
- Cache query results when iterating multiple times

**Example:**
```cpp
// Good: Minimal query
auto query = world.query()
    .all<Position>()
    .any<Visible, NeedsUpdate>()
    .none<Deleted>();

// Bad: Querying unnecessary components
auto query = world.query()
    .all<Position, Material, RenderData, PhysicsData>();  // Too many!
```

### 3. Entity Creation Order

Create entities in Morton order for spatial coherence:

```cpp
struct VoxelCreationRequest {
    glm::ivec3 coord;
    uint8_t material;
    uint32_t color;

    uint64_t morton_code() const {
        return morton_encode(coord.x, coord.y, coord.z);
    }
};

void create_voxels_optimized(gaia::ecs::World& world,
                             std::vector<VoxelCreationRequest> requests) {
    // Sort by Morton code for cache-friendly access
    std::sort(requests.begin(), requests.end(),
             [](const auto& a, const auto& b) {
                 return a.morton_code() < b.morton_code();
             });

    // Create entities in sorted order
    for (const auto& req : requests) {
        auto entity = world.add();
        world.add<voxel::Position>(entity, {req.coord});
        world.add<voxel::Material>(entity, {req.material, 0});
        world.add<voxel::VoxelData>(entity, {req.color, 1.0f, 0});
    }
}
```

---

## References and Further Reading

1. **Gaia ECS:**
   - GitHub: https://github.com/richardbiely/gaia-ecs
   - Documentation: https://github.com/richardbiely/gaia-ecs/wiki
   - Key feature: Archetype-based ECS with excellent performance

2. **Residency Octree:**
   - L. Herzberger et al., IEEE TVCG 2023
   - Key insight: Separate logical structure from physical residency
   - Application: Web-based multi-volume rendering

3. **Differentiable Voxels:**
   - S. Daubner et al., arXiv:2507.21748, 2025
   - Key insight: Make voxel operations differentiable for optimization
   - Application: Physics simulation, inverse design

4. **Efficient Rendering:**
   - zeux.io Vulkan renderer guide
   - Key insight: Minimize state changes, use indirect rendering
   - Application: High-performance GPU rendering

5. **Sparse Voxel Octrees:**
   - Laine & Karras, HPG 2010
   - Key insight: GPU raycasting without explicit triangles
   - Application: Real-time rendering of massive voxel scenes

6. **Morton Ordering:**
   - Improves cache locality for 3D spatial data
   - Essential for ECS-based voxel engines
   - Application: Faster neighbor queries and traversal

---

## Conclusion

Building a robust voxel engine with **Gaia ECS** and **C++** provides:

1. **Data-oriented foundation** - Cache-friendly component storage
2. **Sparse representation** - Only store voxels that exist
3. **Flexible queries** - Fast iteration over relevant voxels
4. **Natural parallelism** - Multi-threaded system updates
5. **GPU acceleration** - Efficient Vulkan rendering pipeline

### Key Architectural Decisions:

- **Gaia ECS as backend**: Provides sparse storage, fast queries, and parallelism
- **Entity-per-voxel**: Each voxel is an entity (sparse by nature)
- **Hierarchical entities**: Chunks and octree nodes are also entities
- **Component-based views**: Different systems query different components
- **Dirty tracking**: Incremental GPU uploads via DirtyFlag component
- **Residency management**: Stream large datasets with ResidencyComponent

### Framework Capabilities:

✓ Real-time voxel manipulation
✓ Large-scale scenes via streaming
✓ Multi-volume rendering
✓ Physics simulation
✓ Differentiable operations
✓ Efficient GPU rendering
✓ Multi-threaded updates
✓ Modular system architecture

### Getting Started:

1. **Integrate Gaia ECS** - Add to project via CMake
2. **Define components** - Position, Material, VoxelData, etc.
3. **Create VoxelWorld** - Entity creation and basic queries
4. **Build octree** - Entity relationships for hierarchy
5. **Add rendering** - Vulkan pipeline with compute shaders
6. **Implement systems** - Rendering, physics, modification tracking
7. **Optimize** - Profile, SIMD, Morton ordering, GPU compute

This architecture provides a solid foundation for a modern, high-performance voxel engine capable of handling rendering, physics, and simulation tasks efficiently.
