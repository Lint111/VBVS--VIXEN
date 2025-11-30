# Active Context

**Last Updated**: November 30, 2025 (Session 8H)
**Current Branch**: `claude/phase-h-voxel-infrastructure`
**Status**: 🔴 **Bug #9 Identified** | ESVO↔Brick Index Mismatch

---

## Phase H Summary - COMPLETE ✅

All CPU-side voxel infrastructure is implemented and tested:

| Feature | Status | Tests |
|---------|--------|-------|
| Entity-based SVO | ✅ Done | 47/47 |
| Ray casting (ESVO) | ✅ Done | 11/11 |
| Partial block updates | ✅ Done | 5/5 |
| GLSL shader sync | ✅ Done | - |

**Total**: 58 tests passing

### Key APIs

```cpp
// Create and populate voxel world
GaiaVoxelWorld world;
world.createVoxel(VoxelCreationRequest{pos, {Density{1.0f}, Color{red}}});

// Build octree from entities
LaineKarrasOctree octree(world, nullptr, 8, 3);
octree.rebuild(world, worldMin, worldMax);

// Ray cast
auto hit = octree.castRay(origin, direction);

// Partial updates (new in Phase H.2)
octree.updateBlock(blockMin, depth);  // Add/update brick
octree.removeBlock(blockMin, depth);  // Remove brick

// Thread-safe rendering
octree.lockForRendering();
// ... ray casting ...
octree.unlockAfterRendering();
```

### Performance (CPU)
- Debug: ~3K rays/sec
- Release: ~54K rays/sec

---

## Week 2: GPU Integration - WORKING ✅

### Session 8H Progress (Nov 30, 2025) - ESVO↔BRICK INDEX MISMATCH BUG

**Bug #9 Root Cause Identified**:

The shader's `handleLeafHit()` computes brick index from ESVO position, but this computed brick may not match the brick that the ESVO hierarchy marked as valid in the parent's `leafMask`.

**Issue Details**:
1. At scale 19 (depth 4), one ESVO node covers a 2×2×2 brick region (8 possible bricks)
2. The node's `leafMask` indicates which of the 8 octants contain populated bricks
3. When `childIsLeaf()` returns true, the shader enters `BRICK_ENTER`
4. **BUG**: The shader computes `brickCoord` from position using `floor(hitPosLocal / BRICK_SIZE)`
5. This computed brick index may differ from the brick that the ESVO hierarchy thinks should be there

**Example from Debug Trace**:
```
Ray at position (1.625, 1.4375, 1.8125) in mirrored space at scale 19
Unmirrors to localNorm (0.3125, 0.4375, 0.8125)
gridPos = localNorm × 128 = (40, 56, 104)
brickCoord = floor(gridPos / 8) = (5, 7, 13)
BUT brick (5, 7, 13) is EMPTY (interior of Cornell Box)
The ESVO hierarchy marked this region as having a leaf, expecting a DIFFERENT brick
```

**Architecture Conflict**: Two different indexing schemes (sparse ESVO octant vs dense grid brick slots) are not aligned.

**Fix Options Under Investigation**:
1. **Use ESVO octant info**: Derive brick coordinate from ESVO octant index (`state.idx`) instead of position
2. **Add brick existence check**: Check if computed brick actually has non-zero voxels before DDA
3. **Align ESVO structure with dense grid**: Ensure ESVO octants at brick level correspond exactly to dense grid brick slots

---

### Session 8G Progress (Nov 30, 2025) - DEBUG CAPTURE SYSTEM INTEGRATION

**Completed**:
- [x] Fixed `INPUT_STATE` lifetime: Changed from `Transient` to `Persistent` in `InputNodeConfig.h` and `CameraNodeConfig.h`
  - Field extraction requires stable memory addresses for member pointers
- [x] Fixed SDI binding name mismatch: `debugWriteIndex` → `traceWriteIndex` in `VulkanGraphApplication.cpp`
- [x] Implemented JSON export for ray traces in `DebugBufferReaderNode.cpp`
  - Full per-ray traversal path data with PUSH/ADVANCE/POP steps
  - Output: `binaries/compute_debug_output.json` (~1.9 MB, 130 traces)
- [x] Fixed push constant field extraction for `InputState*` type
  - `PushConstantGathererNode.cpp` now tries multiple pointer types (CameraData*, InputState*)
  - Debug mode (keys 0-9) now properly passed to shader

**Debug Capture System**:
- Ring buffer captures up to 256 rays per frame
- Each ray trace records up to 64 steps (PUSH, ADVANCE, POP, BRICK_ENTER, HIT, MISS)
- JSON export includes: pixel, stepCount, hit/miss, overflow flag, and full step details
- Per-step data: type, nodeIndex, scale, octantMask, position, tMin/tMax, childDescriptor

**Files Modified**:
- [InputNodeConfig.h:51-54](libraries/RenderGraph/include/Data/Nodes/InputNodeConfig.h#L51-L54) - `INPUT_STATE` → Persistent
- [CameraNodeConfig.h:94](libraries/RenderGraph/include/Data/Nodes/CameraNodeConfig.h#L94) - `INPUT_STATE` input → Persistent
- [VulkanGraphApplication.cpp:1119](application/main/source/VulkanGraphApplication.cpp#L1119) - `traceWriteIndex` binding fix
- [DebugBufferReaderNode.cpp:182-229](libraries/RenderGraph/src/Nodes/DebugBufferReaderNode.cpp#L182-L229) - JSON export implementation
- [PushConstantGathererNode.cpp:436-472](libraries/RenderGraph/src/Nodes/PushConstantGathererNode.cpp#L436-L472) - Multi-type field extraction

**Debug Visualization Modes (Keys 0-9)**:
- Now properly working via `InputState::debugMode` → push constants → shader
- Press number keys to switch visualization modes in real-time

---

### Session 8F Progress (Nov 28, 2025) - AXIS-PARALLEL RAY TRAVERSAL FIX

**Root Cause Found**: Shader didn't filter axis-parallel rays in ADVANCE phase, causing incorrect octant stepping.

**Bug**: The `executeAdvancePhase()` function used a simple `min(tx_corner, ty_corner, tz_corner)` for tc_max, without filtering out axes where the ray is nearly parallel. This caused:
1. Misleading large t-values from perpendicular axes
2. Incorrect step decisions (stepping along parallel axes)
3. Partial rendering - some bricks work, others don't (viewing-angle dependent)

**Fix**: Port CPU's `computeCorrectedTcMax()` and axis-filtering logic to shader:

```glsl
// Filter out corners for axis-parallel rays
float computeCorrectedTcMax(float tx_corner, float ty_corner, float tz_corner,
                            vec3 rayDir, float t_max) {
    const float corner_threshold = 1000.0;
    const float dir_epsilon = 1e-5;
    bool useXCorner = (abs(rayDir.x) >= dir_epsilon);
    // ... filter invalid corners
}

// Only step non-parallel axes
if (canStepX && tx_corner <= tc_max) { step_mask ^= 1; ... }
```

**Files Modified**:
- [VoxelRayMarch.comp:342-358](shaders/VoxelRayMarch.comp#L342-L358) - Added `computeCorrectedTcMax()` helper
- [VoxelRayMarch.comp:391-393](shaders/VoxelRayMarch.comp#L391-L393) - `checkChildValidity()` uses corrected tc_max
- [VoxelRayMarch.comp:451-480](shaders/VoxelRayMarch.comp#L451-L480) - `executeAdvancePhase()` filters axis-parallel rays

### Session 8E Progress (Nov 28, 2025) - POV-DEPENDENT STRIPING FIX

**Root Cause Found**: Offset direction was inverted for positive ray directions in `handleLeafHit()`.

**Bug**: The `localRayDir` calculation used mirrored ray direction (`-rayDir` for positive rays) when computing the offset to apply to `localNorm`, which is in WORLD space. This caused the offset to push sample points BACKWARD for positive-direction rays, resulting in wrong brick lookups at boundaries.

**Fix**: Use world `rayDir` directly since `localNorm` is in world space after unmirroring.

Before (WRONG):
```glsl
localRayDir.x = ((coef.octant_mask & 1u) != 0u) ? rayDir.x : -rayDir.x;  // Inverts positive rays!
offsetDir.x = (localRayDir.x > 0.0) ? offset : -offset;
```

After (CORRECT):
```glsl
offsetDir.x = (rayDir.x > 0.0) ? offset : -offset;  // Use world direction
```

**Files Modified**:
- [VoxelRayMarch.comp:675-686](shaders/VoxelRayMarch.comp#L675-L686) - GPU shader fix
- [LaineKarrasOctree.cpp:1079-1088](libraries/SVO/src/LaineKarrasOctree.cpp#L1079-L1088) - CPU fix for consistency

### Session 8D Progress (Nov 28, 2025) - MAJOR BREAKTHROUGH

**Cornell Box Now Rendering on GPU!**

Fixed 6 critical bugs in `VoxelRayMarch.comp` shader through iterative bug-hunter sessions:

| # | Bug | Root Cause | Fix |
|---|-----|------------|-----|
| 1 | No rendering | Missing brick-level leaf forcing | Force `isLeaf=true` at brick scale |
| 2 | Yellow everywhere | Missing octant offset in handleLeafHit | Added boundary offset |
| 3 | Grid pattern | DDA `invDir` always positive | Preserve sign: `1.0/rayDir` not `1.0/abs(rayDir)` |
| 4 | Brick-level only | Wrong ESVO scale | `getBrickESVOScale()` = 18 not 20 |
| 5 | POV-dependent stripes | Corner+offset wrong for mirrored rays | Use octant center instead |
| 6 | Interior wall gaps | tMax from `pos` not `rayOrigin` | Absolute t parameter |
| 7 | Vertical striping | Offset direction inverted for +rays | Use world rayDir directly |
| 8 | Partial rendering | No axis-parallel filtering in ADVANCE | `computeCorrectedTcMax()` + canStepX/Y/Z |

**Files Modified**:
- [VoxelRayMarch.comp](shaders/VoxelRayMarch.comp) - All 6 bug fixes
- [InputState.h:101-121](libraries/RenderGraph/include/Data/InputState.h#L101-L121) - Arrow key look axes
- [InputNode.cpp:37-41](libraries/RenderGraph/src/Nodes/InputNode.cpp#L37-L41) - Arrow key tracking
- [CameraNode.cpp:133-138](libraries/RenderGraph/src/Nodes/CameraNode.cpp#L133-L138) - Arrow key rotation

**New Controls**:
| Key | Action |
|-----|--------|
| Arrow Left/Right | Look left/right (yaw) |
| Arrow Up/Down | Look up/down (pitch) |
| W/A/S/D | Move forward/left/back/right |
| Q/E | Move down/up |
| Mouse | Look (still works) |
| ESC | Exit |

**Current State**:
- ✅ Cornell Box renders with correct colors (red, green, yellow, white walls)
- ✅ Camera controls work smoothly
- ⚠️ Minor artifacts may remain at brick boundaries (needs verification)

**Next**:
- [ ] Final visual verification
- [ ] Benchmark Mrays/sec at 1080p
- [ ] Update progress.md

### Session 8B Progress (Nov 27, 2025)

**Completed**:
- [x] **MAJOR REFACTOR**: Replaced `SVOBuilder` with `LaineKarrasOctree` in `VoxelGridNode::CompileImpl()`
  - OLD: `SVOBuilder::buildFromVoxelGrid()` - Incomplete ESVO structure (childPointer always 0)
  - NEW: `GaiaVoxelWorld` + `LaineKarrasOctree::rebuild()` - Proper ESVO with valid childPointer
- [x] Build passes successfully
- [x] All 47 octree tests still pass

### Session 8A Progress (Nov 26, 2025)

**Completed**:
- [x] Analyzed GPU integration gaps - Found `VoxelGridNode` wasn't uploading ESVO data
- [x] Implemented `UploadESVOBuffers()` method in VoxelGridNode
  - Extracts `ChildDescriptor` data → `esvoNodes` SSBO (binding 1)
  - Extracts brick voxel data from `EntityBrickView` → `brickData` SSBO (binding 2)
  - Material palette → `materials` SSBO (binding 3)
- [x] Build passes successfully

### Goals
- [ ] Render graph integration for voxel ray marching
- [x] GPU buffer upload (ESVO structure to SSBO)
- [ ] GPU compute shader execution
- [ ] Target: >200 Mrays/sec at 1080p

### Prerequisites Complete ✅
- [x] GLSL shaders synced with C++ (`VoxelRayMarch.comp`, `OctreeTraversal-ESVO.glsl`)
- [x] Shader binding infrastructure exists (`VoxelRayMarchNames.h`)
- [x] Compute pipeline nodes exist (`ComputeDispatchNode`, `ComputePipelineNode`)
- [x] Camera and VoxelGrid nodes exist
- [x] **GPU buffer upload** - `UploadESVOBuffers()` implemented

### Week 2 Tasks
1. ~~**Buffer Upload**~~ ✅ - Upload ESVO structure to GPU (SSBO)
2. **Render Graph Wiring** - Connect compute shader to render graph (already wired)
3. **Dispatch** - Execute compute shader at 1080p (1920×1080 = 2M rays)
4. **Benchmark** - Measure actual Mrays/sec

### Key Files
- [VoxelRayMarch.comp](shaders/VoxelRayMarch.comp) - Main compute shader
- [OctreeTraversal-ESVO.glsl](shaders/OctreeTraversal-ESVO.glsl) - ESVO algorithm (include)
- [VulkanGraphApplication.cpp](application/main/source/VulkanGraphApplication.cpp) - Render graph setup

---

## Technical Reference

### ESVO Coordinate Spaces
- **LOCAL SPACE**: Octree storage (ray-independent, integer grid)
- **MIRRORED SPACE**: ESVO traversal (ray-direction-dependent)
- **WORLD SPACE**: 3D world coordinates (mat4 transform)

### octant_mask Convention
- Starts at 7, XOR each bit for positive ray direction
- bit=0 → axis IS mirrored, bit=1 → NOT mirrored
- Convert: `localIdx = mirroredIdx ^ (~octant_mask & 7)`

### Key Data Structures
| Structure | Size | Notes |
|-----------|------|-------|
| RayHit | 40 bytes | entity + hitPoint + t values |
| EntityBrickView | 16 bytes | zero-storage view |
| ChildDescriptor | 8 bytes | ESVO traversal node |

---

## Todo List (Active Tasks)

### Week 2: GPU Integration (Current) - COMPLETE ✅
- [x] Upload ESVO structure to GPU (SSBO) ✅
- [x] Wire compute shader in render graph ✅
- [x] Fix shader bugs (8 bugs fixed through Session 8F) ✅
- [x] Cornell Box rendering correctly ✅
- [x] Arrow key camera controls ✅
- [x] Debug capture system integrated (Session 8G) ✅
- [x] Per-ray traversal trace capture with JSON export ✅
- [x] Debug visualization modes (keys 0-9) working ✅
- [ ] Benchmark GPU performance (target >200 Mrays/sec)

### Week 3: DXT Compression
- [ ] Study ESVO DXT implementation
- [ ] CPU DXT encoding (DXT1/BC1 color, DXT5/BC3 normal)
- [ ] GLSL DXT decoding
- [ ] 16× memory reduction validation

### Week 4: Polish
- [ ] Normal calculation from voxel faces
- [ ] Adaptive LOD
- [ ] Streaming for large octrees

### Low Priority (Deferred)
- [ ] Multi-threaded CPU ray casting
- [ ] SIMD ray packets (AVX2)
- [ ] Memory profiling/validation

---

## Reference

**ESVO Paper**: Laine & Karras (2010) - "Efficient Sparse Voxel Octrees"

**Test Files**:
- [test_octree_queries.cpp](libraries/SVO/tests/test_octree_queries.cpp)
- [test_ray_casting_comprehensive.cpp](libraries/SVO/tests/test_ray_casting_comprehensive.cpp)

---

**End of Active Context**
