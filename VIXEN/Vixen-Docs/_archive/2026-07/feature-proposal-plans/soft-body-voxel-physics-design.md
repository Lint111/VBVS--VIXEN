---
title: Soft Body Voxel Physics Design
aliases: [Voxel Physics, Gram-Schmidt Soft Body, Dual-Rep Physics]
tags: [design, physics, soft-body, gram-schmidt, lod]
created: 2026-01-09
status: approved
sprint: 11
---

# Soft Body Voxel Physics Design

Hybrid soft body physics system combining Gram-Schmidt voxel constraints with dual-representation LOD for real-time deformable voxel simulation.

---

## Design Goals

1. **Keep data voxelated** - Physics deformation must not break voxel grid alignment
2. **Lossy but good enough** - Visual plausibility over physical accuracy
3. **Emergent rigid body motion** - Rotation/translation emerges from voxel micro-interactions
4. **LOD scalability** - 94%+ compute reduction in large scenes
5. **VIXEN integration** - Fits existing RenderGraph, EventBus, DeviceBudgetManager

---

## References

Based on research from:
- [Gram-Schmidt Voxel Constraints (MIG 2024)](https://dl.acm.org/doi/10.1145/3677388.3696322) - Tim McGraw
- [Stable Cosserat Rods (SIGGRAPH 2025)](https://graphics.cs.utah.edu/research/projects/stable-cosserat-rods/) - Split position/rotation optimization
- [MGPBD Multigrid XPBD (SIGGRAPH 2025)](https://arxiv.org/html/2505.13390v1) - High-stiffness solver

---

## Core Concept: Dual-Representation LOD

Two voxel physics representations that can promote/demote based on activity:

### 8-Particle Mode (Full Fidelity)

From Gram-Schmidt paper - each voxel has 8 corner particles:

```cpp
struct VoxelParticles {
    vec3  corner_pos[8];      // 8 corner positions
    vec3  corner_vel[8];      // 8 corner velocities
    uint8_t face_flags;       // Which faces connected (bitmask)
    uint8_t face_strain[6];   // Per-face strain (0-255)
    uint16_t material_id;
};
// ~212 bytes per voxel
```

**Capabilities:**
- Arbitrary deformation (parallelepipeds, shearing)
- Volume-preserving via VGS constraint
- Proven Gram-Schmidt projection algorithm

### 1-Element Mode (Compact)

Voxel as semi-rigid unit with explicit orientation:

```cpp
struct VoxelElement {
    vec3     displacement;    // Offset from grid center
    vec3     velocity;        // Linear velocity
    uint32_t orientation;     // Packed quaternion (10-10-10-2)
    half     volume_bias;     // Conservation (0.5-2.0, default 1.0)
    uint8_t  face_strain[6];  // Per-face strain
    uint8_t  face_flags;      // Connection bitmask
    uint8_t  flags;           // IS_FREE, NEEDS_ORIENT_UPDATE, etc.
    uint16_t material_id;
};
// ~40 bytes per voxel (~5x smaller)
```

**Capabilities:**
- Translation + tilt as unit
- Orientation tracks "forward" direction
- Natural fit with mipmap LOD

### Representation Transitions

**Promotion (1-element → 8-particle):**
```cpp
mat3 R = matrix_from_quaternion(orientation);
vec3 center = grid_center + displacement;
for (int i = 0; i < 8; i++) {
    corners[i] = center + R * (unit_cube_corner[i] * voxel_half_size);
    velocities[i] = velocity;
}
```

**Demotion (8-particle → 1-element):**
```cpp
// Extract rotation via Gram-Schmidt
vec3 e_x = avg_edge_vector(corners, X_AXIS);
vec3 e_y = avg_edge_vector(corners, Y_AXIS);
vec3 e_z = avg_edge_vector(corners, Z_AXIS);

vec3 u_x = normalize(e_x);
vec3 u_y = normalize(e_y - dot(e_y, u_x) * u_x);
vec3 u_z = normalize(e_z - dot(e_z, u_x) * u_x - dot(e_z, u_y) * u_y);

orientation = quaternion_from_matrix(mat3(u_x, u_y, u_z));
displacement = centroid(corners) - grid_center;
velocity = avg(corner_velocities);
```

**Triggers:**
| Transition | Triggers |
|------------|----------|
| Promote | Camera proximity, high strain, collision, user interaction |
| Demote | Stable N frames, camera exit, low velocity/strain |

---

## Connection Model

### 6 Face-to-Face Constraints

Each voxel has up to 6 face connections (+X, -X, +Y, -Y, +Z, -Z):

```
Face connection states:
├── Connected (default): accumulates strain from neighbor displacement
├── Stretched: strain > 0 but < threshold, connection maintained
└── Fractured: strain > threshold, connection broken permanently
```

### Strain and Fracture

```cpp
// Per-frame strain update
for each face f in voxel:
    if face_flags & (1 << f):
        neighbor = get_neighbor(voxel, f)
        strain = compute_strain(voxel, neighbor)
        face_strain[f] = strain

        if strain > material.strain_threshold:
            face_flags &= ~(1 << f)  // Break connection
            emit FractureEvent(voxel, f)
```

### Emergent Rigid Body Motion

Connected voxel clumps move/rotate as units through constraint propagation:

```
Asymmetric force (corner impact) →
  → different displacement on impact side vs far side
  → stiff constraints propagate through chunk
  → whole chunk rotates to satisfy constraints
  → No explicit rigid body transform needed
```

---

## Re-Voxelization System

Keeps data grid-aligned while allowing motion.

### Grid Migration

When displacement exceeds threshold, voxel migrates to new cell:

```cpp
if length(displacement) > 0.5 * voxel_size:
    ivec3 new_cell = round(grid_center + displacement)

    // Transfer to new cell
    migrate_voxel(voxel, old_cell, new_cell)

    // Update displacement to be relative to new cell
    displacement = (grid_center + displacement) - new_cell_center

    // Recalculate orientation (1-element mode only)
    if is_1_element_mode:
        flags |= NEEDS_ORIENT_UPDATE

    // Update face connections
    update_face_connections(voxel, old_cell, new_cell)
```

### Volume Bias Conservation

Prevents volume drift during migrations:

```cpp
// During migration
target_cell.volume_bias += source_voxel.volume_bias

// Pressure propagation when bias exceeds threshold
if volume_bias > 1.5:
    excess = volume_bias - 1.0
    distribute_to_neighbors(excess)
    volume_bias = 1.0
```

---

## LOD System

Three LOD dimensions:

### 1. Spatial LOD (Octree Mipmap)

```
LOD 0: 1×1×1 voxels     (full resolution)
LOD 1: 2×2×2 clumps     (8× fewer elements)
LOD 2: 4×4×4 clumps     (64× fewer elements)
LOD 3: 8×8×8 clumps     (512× fewer elements)
```

### 2. Representation LOD

```
8-particle: Close/active voxels (~212 bytes)
1-element:  Medium/stable voxels (~40 bytes)
```

### 3. Temporal LOD (Multi-Rate)

```
Active:   every frame
Medium:   every 2nd frame
Far:      every 4th frame
Distant:  every 8th frame
Dormant:  skip simulation entirely
```

### Dormancy Detection

```cpp
struct DormancyState {
    uint8_t stable_frames;
    bool    is_dormant;
};

// Wake triggers:
// - Neighbor becomes active
// - External force applied
// - Collision detected
// - Camera enters proximity
```

### Combined Effect

```
1M voxels scene:
├── Close/active:    10K voxels × 8-particle × every frame
├── Medium:          50K voxels × 1-element × every frame
├── Far:            200K clumps × 1-element × every 2nd frame
├── Distant:        100K clumps × 1-element × every 8th frame
└── Dormant:        640K voxels × sleeping

Effective: ~60K element-equivalents per frame
Reduction: ~94% compute, ~85% memory vs naive
```

---

## Simulation Loop

```
┌─────────────────────────────────────────────────────────────┐
│ 1. LOD UPDATE                                               │
│    - Spatial LOD: merge/split voxel clumps by distance      │
│    - Representation LOD: promote/demote 8-particle ↔ 1-elem │
├─────────────────────────────────────────────────────────────┤
│ 2. EXTERNAL FORCES                                          │
│    - Gravity                                                │
│    - User forces (explosions, player interaction)           │
│    - Apply to velocities                                    │
├─────────────────────────────────────────────────────────────┤
│ 3. PREDICT POSITIONS                                        │
│    - 8-particle: corners += velocity * dt                   │
│    - 1-element: displacement += velocity * dt               │
├─────────────────────────────────────────────────────────────┤
│ 4. CONSTRAINT SOLVING (N iterations)                        │
│    a. Shape constraints (VGS for 8-particle voxels)         │
│    b. Face-to-face constraints (stretch between neighbors)  │
│    c. Collision constraints                                 │
│    d. Check strain → mark fractures                         │
├─────────────────────────────────────────────────────────────┤
│ 5. UPDATE VELOCITIES                                        │
│    - velocity = (new_pos - old_pos) / dt                    │
│    - Apply damping                                          │
├─────────────────────────────────────────────────────────────┤
│ 6. RE-VOXELIZATION                                          │
│    - Check displacement > 0.5 threshold                     │
│    - Migrate voxels to new grid cells                       │
│    - Update face connections                                │
│    - Recalculate orientation (1-element only)               │
├─────────────────────────────────────────────────────────────┤
│ 7. VOLUME CONSERVATION                                      │
│    - Propagate volume_bias from migrations                  │
│    - Apply pressure correction if bias exceeds threshold    │
└─────────────────────────────────────────────────────────────┘
```

### GPU Parallelization

- Steps 2, 3, 5: Trivially parallel (per-voxel)
- Step 4: Graph coloring (4 colors for cubic lattice)
- Steps 6, 7: Parallel with atomics for conflict resolution

---

## VIXEN Integration

### RenderGraph Node

```cpp
class SoftBodySimNode : public MultiDispatchNode {
    // Inputs
    SLOT(VoxelPhysicsBuffer, physics_data, SlotRole::Execute);
    SLOT(ForceFieldBuffer, external_forces, SlotRole::Sample);

    // Outputs
    SLOT(VoxelPhysicsBuffer, updated_physics, SlotRole::Execute);
    SLOT(MigrationList, migrations, SlotRole::Execute);

    // Config
    SoftBodyConfig config;  // iterations, dt, thresholds
};
```

### EventBus Events

```cpp
struct FractureEvent {
    uint32_t voxel_id;
    uint8_t  face;
    vec3     position;
};

struct MigrationEvent {
    uint32_t voxel_id;
    ivec3    old_cell;
    ivec3    new_cell;
};

struct CollisionEvent {
    uint32_t voxel_a;
    uint32_t voxel_b;
    vec3     contact_point;
    vec3     normal;
};
```

### DeviceBudgetManager

```cpp
// Memory pools
VoxelParticlesPool  // 8-particle mode buffer
VoxelElementsPool   // 1-element mode buffer
ConstraintBuffer    // Face-to-face constraints
MigrationBuffer     // Pending migrations
```

---

## Material Properties

```cpp
struct MaterialProperties {
    float stiffness;         // Force per unit strain
    float damping;           // Energy loss coefficient
    float strain_threshold;  // When face breaks
    float density;           // For mass calculation
};

// Examples:
// Rubber:  stiffness=0.1, strain_threshold=2.0
// Rock:    stiffness=10.0, strain_threshold=0.05
// Jelly:   stiffness=0.01, strain_threshold=5.0
// Wood:    stiffness=5.0, strain_threshold=0.1 (anisotropic)
```

---

## Sprint Plan (Extended)

Updates roadmap Sprint 11 with dual-representation:

| Phase | Task | Weeks | Status |
|-------|------|-------|--------|
| 11.1 | Gram-Schmidt Foundation | 4 | Planned |
| 11.2 | Dual-Representation LOD | 2 | NEW |
| 11.3 | Re-Voxelization System | 2 | NEW |
| 11.4 | LOD Integration | 2 | Enhanced |
| 11.5 | RenderGraph Integration | 2 | NEW |
| **Total** | | **12 weeks** | +4 from original |

### Phase Details

**11.1 Gram-Schmidt Foundation (4 weeks)**
- VoxelParticles data structure
- VGS constraint projection
- Face-to-face constraints (breakable)
- GPU compute shader
- Target: 10K voxels in 0.3ms

**11.2 Dual-Representation LOD (2 weeks)**
- VoxelElement data structure
- Promotion/demotion logic
- Gram-Schmidt orientation extraction
- LOD manager

**11.3 Re-Voxelization System (2 weeks)**
- Grid migration logic
- Connection update on migrate
- Orientation recalculation
- Volume bias conservation

**11.4 LOD Integration (2 weeks)**
- Spatial LOD via octree
- Temporal LOD (multi-rate)
- Dormancy detection
- Combined scheduling

**11.5 RenderGraph Integration (2 weeks)**
- SoftBodySimNode
- EventBus physics events
- DeviceBudgetManager integration
- TaskQueue budget-aware dispatch

---

## Success Metrics

| Metric | Target |
|--------|--------|
| Performance | 10K active voxels at 0.3ms |
| Memory | 5× reduction for stable voxels |
| LOD scaling | 94% compute reduction in large scenes |
| Visual | Smooth deformation without grid artifacts |
| Destruction | Clean fracture without artifacts |

---

## Open Questions (Future Work)

1. **Rendering** - How to render smooth surfaces from voxelated physics data
2. **Collision** - Integration with existing collision systems
3. **Cellular Automata** - Interaction with material state transitions
4. **Chunk Streaming** - Physics persistence across chunk boundaries

---

## Related Documentation

- [[Production-Roadmap-2026]] - Master roadmap
- [[gaiavoxelworld-backend-expansion]] - GaiaVoxelWorld physics overview
- [[Sprint6.3-Timeline-Capacity-System]] - Budget-aware scheduling reference

---

*Design approved 2026-01-09*
