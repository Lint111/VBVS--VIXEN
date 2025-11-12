# Grain Artifact Diagnostic Guide

## Investigation Summary

**Issue**: Screen-space grain/noise artifact appearing **only when looking at the red wall** (material ID 1)

**Possible Root Causes**:
1. **Voxel coordinate clamping at brick boundaries** - Precision loss in `localPos = fract(hitPos) * 8.0`
2. **Octree parent-child traversal** - Child mask reversal may still have issues
3. **Floating-point accumulation** - `t_min` precision degradation through dense geometry

---

## Debug Features Added

Added three diagnostic debug modes to `VoxelRayMarch.comp`:

### 1. `VOXEL_RAY_DEBUG_MATERIAL_ID` (Currently ENABLED)
**What it shows**: Material ID as distinct colors
- **Dark gray** (0.2, 0.2, 0.2) = Material 0 (empty/invalid)
- **Pure Red** (1.0, 0.0, 0.0) = Material 1 (red wall)
- **Grayscale** = Materials 2-7 (proportional to ID)
- **Cyan** (0.0, 1.0, 1.0) = Materials 8+ (other surfaces)

**To test**:
1. Run the application
2. Look at the red wall - it should appear **pure red** if all voxels read as material 1
3. If you see **dark gray, grayscale, or cyan patches**, it means those voxels are reading the wrong material ID
4. This directly indicates whether voxel data is being read correctly

**Expected result**: The entire red wall should be uniformly pure red
**Problem diagnosis**:
- If you see patches of dark gray → Some voxels reading as 0 (empty)
- If you see patches of color → Some voxels reading wrong material IDs
- This would explain the "grain" - it's actually wrong voxel values being displayed

---

### 2. `VOXEL_RAY_DEBUG_CLAMP` (Disabled - Enable to test)
**What it shows**: Detects when voxel coordinates exceed brick bounds [0-7]
- **Normal colors** = No clamping needed (coordinates valid)
- **Yellow** (1.0, 1.0, 0.0) = Coordinates were clamped to [0-7]

**To test**:
1. Modify `VoxelRayMarch.comp` line 98: Change `#define VOXEL_RAY_DEBUG_CLAMP 0` to `1`
2. Rebuild
3. Run the application
4. Look at edges of geometry

**Expected result**: Very few or no yellow pixels (clamping rarely needed)
**Problem diagnosis**:
- Widespread yellow = Precision errors causing coordinate out-of-bounds
- This would cause wrong voxels to be read (boundary voxels used for everything near edges)

---

## How to Interpret Results

### Test Sequence

**Test 1: Material ID Debug (Currently Active)**
1. Run application
2. Look directly at red wall
3. Screenshot or observe the color
4. **If uniform pure red** → Voxel reads are correct, grain is shading/traversal issue
5. **If mixed colors/gray** → Voxel reads are WRONG, need to investigate:
   - Brick offset calculation
   - Voxel index calculation
   - Octree structure (missing bricks)

**Test 2: Enable Clamping Debug**
1. Re-enable `VOXEL_RAY_DEBUG_CLAMP`
2. Look for yellow pixels on red wall
3. **Many yellow pixels** = Coordinate precision problem
4. **Few yellow pixels** = Coordinate precision is OK

**Test 3: Compare Materials**
1. Keep material ID debug enabled
2. Look at:
   - Red wall (material 1) - should be pure red
   - Floor/walls (materials 3-5) - should be grayscale
   - Ceiling light (material 20) - should be cyan
3. If one material shows grain but others don't → Material-specific octree issue
4. If all materials show similar patterns → Global traversal issue

---

## Technical Details

### Material ID Range
```
0 = Empty/Invalid (reads as dark gray)
1 = Red (left wall)
2 = Green (right wall)
3-5 = White (walls, floor, ceiling)
6-7 = Gray (checkered floor)
8-9 = Reserved
10 = Beige (left cube)
11 = Blue (right cube)
12-18 = Reserved
19 = Magenta (debug corner marker)
20 = Ceiling light (emissive)
```

### Voxel Reading Path

```
Ray hits geometry
  → traverseOctreeSimple() finds brick containing hit point
  → getVoxelFromBrick(brickIndex, voxelCoord)
    → brickIndex * 128 = offset into brick buffer
    → voxelCoord.z * 64 + voxelCoord.y * 8 + voxelCoord.x = voxel offset
    → Read 1 byte from buffer
  → getMaterial(materialID)
    → materialID * 2 = offset into material palette
    → Read 2 vec4s (32 bytes)
  → Use material's albedo as hitColor
```

### Where Grain Could Originate

**Option A: Voxel data is wrong**
- Evidence: Material ID debug shows wrong IDs
- Cause: Brick packing, octree structure, or data upload error
- Fix: Check voxel grid generation and brick linearization

**Option B: Coordinate calculation is imprecise**
- Evidence: Material ID debug shows correct IDs but visually grainy
- Cause: `fract(hitPos) * 8.0` loses precision or produces out-of-range values
- Fix: Improve coordinate calculation or add epsilon handling

**Option C: Octree traversal skips bricks**
- Evidence: Material ID debug shows patches of material 0 (empty)
- Cause: Child mask reversal or traversal algorithm error
- Fix: Debug octree structure and child mask calculation

---

## Implementation Notes

Debug code is **completely compile-time** using preprocessor conditionals:
- Zero performance impact when disabled
- Zero shader bind point impact
- Simply conditional color assignment

Current state:
- `VOXEL_RAY_DEBUG_MATERIAL_ID` = **1** (enabled)
- `VOXEL_RAY_DEBUG_CLAMP` = **0** (disabled)

To test other modes:
1. Edit `VoxelRayMarch.comp` lines 91-99
2. Change `#define VALUE 0` to `#define VALUE 1`
3. Rebuild (recompiles shader)
4. Run application

---

## Next Steps

1. **Run current build** with `MATERIAL_ID` debug enabled
2. **Observe color of red wall**:
   - Pure red everywhere → Problem is NOT in voxel reads
   - Mixed colors → Problem IS in voxel reads, investigate octree/bricks
3. **Report findings** with screenshot or description
4. **Enable clamping debug** if needed for further diagnosis
5. **Compare different materials** to identify if issue is global or material-specific

This should definitively pinpoint whether the grain is:
- Voxel data read errors (fix in octree/brick construction)
- Coordinate precision (fix in ray march math)
- Traversal algorithm (fix in ESVO implementation)
