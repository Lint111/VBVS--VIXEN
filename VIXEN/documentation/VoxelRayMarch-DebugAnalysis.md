# VoxelRayMarch Traversal & Brick Issues – Debug Analysis

**Date:** 2025-11-30  
**Context:** Phase H – GPU ESVO integration (`VoxelRayMarch.comp`)  
**Artifacts:** `binaries/binaries/compute_debug_output.json`, `shaders/VoxelRayMarch.comp`

---

## 1. ESVO Traversal Health Check

### Observations
- Ray log shows correct ESVO traversal shape:
  - Root at scale 21 → child at scale 20 → `BRICK_ENTER` → `HIT`.
  - Occasional `ADVANCE` + `POP` to scale 22 followed by re-entry into valid nodes.
- `scale` stays within [20, 22]; no out-of-range values.
- `tMin`/`tMax` remain monotone and bounded (~[0, 0.56]).
- `octantMask` values vary consistently across rays (1–7), matching different ray directions.

### Conclusion
- Core ESVO logic (PUSH/ADVANCE/POP, scale updates, `executePopPhase` exponent tricks) behaves correctly for the captured rays.
- Current issues are not caused by catastrophic traversal failure but by **how leaf nodes map to bricks and how brick content is queried**.

---

## 2. Brick Parent Scale and Leaf Forcing

### Where
- `shaders/VoxelRayMarch.comp`:
  - `getBrickESVOScale()` and UBO: `brickESVOScale = 20`.
  - `checkChildValidity()` forces `isLeaf` at brick parent scale:

```glsl
int brickESVOScale = getBrickESVOScale();  // Returns 20 from UBO
if (state.scale == brickESVOScale && child_valid) {
    isLeaf = true;
}
```

### Observations
- Log: all `BRICK_ENTER` events happen at `scale: 20`, matching the intended brick parent level.
- Parent node indices at 20 (e.g., 13, 39, 40) are stable across many rays.

### Conclusion
- Brick parent scale selection (20) and leaf forcing at that scale are correct and consistent with CPU logic.
- The remaining mismatch is **within that leaf handling path**, not the scale at which it is triggered.

---

## 3. Core Problem: ESVO Leaf → Brick Index Mapping

### Where
- `shaders/VoxelRayMarch.comp` – `handleLeafHit()`:

```glsl
vec3 localPos = unmirrorToLocalSpace(state.pos, state.scale_exp2, coef.octant_mask);
vec3 localNorm = localPos - vec3(1.0);
...
vec3 octantInside = localNorm + offsetDir;
...
vec3 hitPosLocal = octantInside * gridSize;
...
ivec3 brickCoord = ivec3(floor(hitPosLocal / float(BRICK_SIZE)));
brickCoord = clamp(brickCoord, ivec3(0), ivec3(bricksPerAxis - 1));

uint brickIndex = uint(brickCoord.z * bricksPerAxis * bricksPerAxis +
                      brickCoord.y * bricksPerAxis +
                      brickCoord.x);
```

### Issue
- At scale 20 each node covers a **2×2×2 brick region**.
- ESVO’s `validMask`/`leafMask` (at the next finer scale) tell you **which of the 8 child bricks actually exist**.
- The shader, however, computes `brickIndex` *only* from a continuous position inside the parent octant (`hitPosLocal`), **ignoring**:
  - Which child octant was selected at ESVO scale 19.
  - The compacted leaf ordering (some children may be internal, some may be absent).
- Result:
  - ESVO says: “There is at least one brick somewhere inside this 2×2×2 region.”
  - Shader position pick may land in an **interior hole** or in a different valid child’s subregion.
  - Brick index can point at an empty brick or at a brick that does not correspond to the ESVO leaf that triggered `handleLeafHit()`.

### Symptoms
- Brick-level only / missing or wrong bricks even though traversal is correct.
- Visual inconsistencies at brick boundaries and within regions where ESVO says data exists.

### Fix Direction
- **Brick index must be derived from ESVO’s discrete leaf child index and masks, not from continuous position.**

#### Required Data
- For the leaf level:
  - Parent descriptor at scale 20: `validMask`, `leafMask`, `childPointer`.
  - `state.idx` (mirrored child index) and `coef.octant_mask`.

#### Correct Mapping Sketch
1. Convert mirrored child index at scale 20 to local-space child index at scale 19:
   ```glsl
   int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
   ```
2. Use `validMask`/`leafMask` to check and order bricks:
   ```glsl
   bool isLeafChild = childIsLeaf(leafMask, localChildIdx);
   uint leafOffset = countLeavesBefore(validMask, leafMask, localChildIdx);
   ```
3. Map leaf offset to a **brick index base** for this parent (same scheme CPU uses to pack `EntityBrickView` bricks).
4. `brickIndex = brickBase + leafOffset`.

> Note: Step (3) requires mirroring the CPU’s exact ESVO→brick packing convention; that mapping is currently missing in GLSL and must be reintroduced or passed via an SSBO or UBO.

---

## 4. Brick Occupancy vs. `DEBUG_FIRST_VOXEL`

### Where
- `shaders/VoxelRayMarch.comp` – `handleLeafHit()` debug path:

```glsl
#define DEBUG_FIRST_VOXEL 1
...
uint anyNonZero = 0u;
for (int vz = 0; vz < 8; vz++) {
    for (int vy = 0; vy < 8; vy++) {
        for (int vx = 0; vx < 8; vx++) {
            uint voxelLinearIdx = uint(vz * 64 + vy * 8 + vx);
            uint voxelData = brickData[brickIndex * 512u + voxelLinearIdx];
            if (voxelData != 0u) {
                anyNonZero = voxelData;
                break;
            }
        }
        if (anyNonZero != 0u) break;
    }
    if (anyNonZero != 0u) break;
}

if (anyNonZero != 0u) {
    // Green = brick has data
    hitColor = vec3(0.0, 1.0, 0.0);
} else {
    // Red = brick is completely empty
    hitColor = vec3(1.0, 0.0, 0.0);
}
...
return true;
```

### Issues
1. **This path short-circuits real DDA and material rendering.**
   - It only tells you “this brickIndex has at least one non-zero voxel”.
   - It does *not* guarantee the ESVO leaf is mapped to the correct brick for that leaf.

2. **It can hide brick-index bugs.**
   - If `brickIndex` is wrong but happens to point at *some* non-empty brick, you see green even though the mapping is incorrect.

3. **Missing occupancy-based skip.**
   - The design in `activeContext.md` mentions a 5-sample occupancy check that returns `false` for empty bricks so ESVO traversal can continue.
   - This occupancy guard does **not** exist in the shader snapshot you provided; only `DEBUG_FIRST_VOXEL` is active.
   - That means once `isLeaf` is set at scale 20, the code always commits to a brick (even empty) instead of skipping interior gaps.

### Fix Direction
1. **Replace `DEBUG_FIRST_VOXEL` with a proper occupancy check** used in normal path, not just for debug:
   - Sample 4 corners + center in brick-local space.
   - If all samples are zero, treat brick as empty and `return false` from `handleLeafHit()`, so traversal continues.
2. **Use debug modes that expose mapping, not just “any non-zero”**:
   - `DEBUG_BRICK_INDEX`: encode `brickIndex` as color.
   - `DEBUG_LEAF_OFFSET`: encode leaf offset inside parent’s 2×2×2 region.

---

## 5. Axis-Parallel Rays and tc_max

### Where
- `computeCorrectedTcMax()` and `executeAdvancePhase()` in `VoxelRayMarch.comp`.

```glsl
float computeCorrectedTcMax(float tx_corner, float ty_corner, float tz_corner,
                            vec3 rayDir, float t_max) {
    const float corner_threshold = 1000.0;

    bool useXCorner = (abs(rayDir.x) >= DIR_EPSILON);
    bool useYCorner = (abs(rayDir.y) >= DIR_EPSILON);
    bool useZCorner = (abs(rayDir.z) >= DIR_EPSILON);

    float tx_valid = (useXCorner && abs(tx_corner) < corner_threshold) ? tx_corner : t_max;
    float ty_valid = (useYCorner && abs(ty_corner) < corner_threshold) ? ty_corner : t_max;
    float tz_valid = (useZCorner && abs(tz_corner) < corner_threshold) ? tz_corner : t_max;

    return min(min(tx_valid, ty_valid), tz_valid);
}
```

### Observations
- Log shows coherent `ADVANCE`/`POP` behavior and no runaway `tMin > tMax` or wild scales.
- Axis-parallel filtering (using `DIR_EPSILON`) is in place and consistent with CPU.

### Conclusion
- The axis-parallel bug described earlier (stepping along parallel axes) appears fixed in this shader version.
- No new issues inferred from the current ray log for this subsystem.

---

## 6. Summary of Root Causes

1. **Leaf→Brick Mapping Still Position-Based**
   - ESVO leaf selection at scale 20 is discrete; shader uses continuous position to infer brick.
   - This can choose interior/empty bricks or the wrong valid brick inside a 2×2×2 region.

2. **Missing Occupancy-Driven Skip of Empty Bricks**
   - No early exit when the chosen brick has no non-zero voxels.
   - Traversal stops at empty bricks instead of continuing deeper or advancing.

3. **Debug Mode Masks Mapping Errors**
   - `DEBUG_FIRST_VOXEL` reports “some data exists at this index”, not “this is the correct brick for the ESVO leaf”.

---

## 7. Concrete Fix Plan

### Step 1 – Plumb Parent Descriptor into `handleLeafHit()`

**Goal:** Give `handleLeafHit()` enough ESVO context to compute brick index from descriptors, not position.

**Changes:**
- In `traverseOctree()`, when calling `handleLeafHit()`, pass:
  - `parent_descriptor` (uvec2)
  - `validMask`, `leafMask`, `childPointer`

**Example signature change:**
```glsl
bool handleLeafHit(TraversalState state, RayCoefficients coef,
                   vec3 rayOrigin, vec3 rayDir, vec3 gridMin, vec3 gridSize,
                   uvec2 parentDesc, uint validMask, uint leafMask, uint childPointer,
                   out vec3 hitColor, out vec3 hitNormal, out float hitT);
```

### Step 2 – Compute Brick Index from Leaf Child Index

**Goal:** Reproduce CPU’s compacted leaf ordering to map ESVO leaf → brick index.

**Within `handleLeafHit()`:**

1. Determine local child index at brick level:
   ```glsl
   int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
   ```
2. Ensure this child is a leaf:
   ```glsl
   if (!childIsLeaf(leafMask, localChildIdx)) {
       // Should not happen if leaf forcing is correct, but be defensive
       return false;
   }
   ```
3. Compute leaf offset in this parent:
   ```glsl
   uint leafOffset = countLeavesBefore(validMask, leafMask, localChildIdx);
   ```
4. Map `(parent node, leafOffset)` to `brickIndex` using **the same scheme the CPU uses** to pack bricks:
   - Either:
     - Encode a `brickBase` per parent in ESVO buffer (additional field), or
     - Maintain a separate SSBO that maps ESVO node index → first brick index.
   - Then:
     ```glsl
     uint brickBase = fetchBrickBaseForNode(state.parentPtr); // via SSBO or packed descriptor
     uint brickIndex = brickBase + leafOffset;
     ```

> This is the key structural fix: brick index becomes a function of ESVO topology, not world position.

### Step 3 – Add Real Brick Occupancy Check

**Goal:** Avoid committing to interior/empty bricks.

**Within `handleLeafHit()` after computing `brickIndex`:**

1. Sample a small set of voxels in local brick space (e.g. 4 corners + center):
   ```glsl
   const ivec3 sampleOffsets[5] = ivec3[5](
       ivec3(0, 0, 0),
       ivec3(7, 0, 0),
       ivec3(0, 7, 0),
       ivec3(0, 0, 7),
       ivec3(3, 3, 3)
   );

   bool hasData = false;
   for (int i = 0; i < 5; ++i) {
       ivec3 v = sampleOffsets[i];
       uint idx = uint(v.z * 64 + v.y * 8 + v.x);
       uint voxel = brickData[brickIndex * 512u + idx];
       if (voxel != 0u) { hasData = true; break; }
   }

   if (!hasData) {
       // Empty brick: allow ESVO traversal to continue
       return false;
   }
   ```
2. Only if `hasData == true`, proceed to `marchBrick(...)` and shading.

### Step 4 – Replace `DEBUG_FIRST_VOXEL` with Mapping-Oriented Debug Modes

**Goal:** Make mapping problems visible.

**Suggested modes:**
- `DEBUG_BRICK_INDEX`: color = `brickIndex / float(totalBricks)`.
- `DEBUG_LEAF_OFFSET`: color = `leafOffset / 8.0` for visualization of 2×2×2 region.
- `DEBUG_LEAF_MASK`: visualize bits of `leafMask` and `validMask`.

These will reveal if a given ESVO leaf systematically maps to the wrong brick index, independent of voxel content.

---

## 8. Expected Outcome After Fixes

- Rays that currently stop on “brick-level only” empty regions will:
  - Reject empty bricks at leaf level via occupancy check, and
  - Either advance to the correct occupied brick in the same 2×2×2 region, or
  - Continue traversal and exit to background if no true brick is present.
- GPU traversal will match CPU’s `LaineKarrasOctree::castRay` in:
  - Which brick is selected for a given ray and ESVO path.
  - Which voxels inside that brick produce hits/misses.
- Debug captures (`compute_debug_output.json`) will show:
  - Fewer `BRICK_ENTER` → `BRICK_EXIT` at empty leaves.
  - Brick indices and leaf offsets aligned with CPU expectations for a small set of reference rays.
