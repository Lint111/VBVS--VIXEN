// ============================================================================
// CoordinateTransforms.glsl - Geometric Space Conversion Utilities
// ============================================================================
// Coordinate space conversions for ESVO traversal and brick addressing.
// Single source of truth for all geometric transformations.
//
// Coordinate Spaces:
//   - ESVO [1,2]^3: Internal NVIDIA ESVO traversal space
//   - Grid01 [0,1]^3: Normalized grid space
//   - Grid [0,resolution]^3: Voxel index space
//   - Brick [0,BRICK_SIZE]^3: Local brick voxel space
//   - World: Arbitrary world coordinates defined by gridMin/gridMax
//
// Dependencies:
//   - octreeConfig UBO (for gridMin, gridMax)
// ============================================================================

#ifndef COORDINATE_TRANSFORMS_GLSL
#define COORDINATE_TRANSFORMS_GLSL

// ============================================================================
// ESVO <-> GRID SPACE CONVERSIONS
// ============================================================================

// ESVO normalized space [1,2]^3 -> grid-normalized [0,1]^3
vec3 esvoToGrid01(vec3 esvoPos) {
    return esvoPos - vec3(1.0);
}

// Grid-normalized [0,1]^3 -> ESVO normalized space [1,2]^3
vec3 grid01ToEsvo(vec3 grid01) {
    return grid01 + vec3(1.0);
}

// ============================================================================
// GRID <-> WORLD SPACE CONVERSIONS
// ============================================================================

// Grid [0,resolution]^3 -> world using gridMin/gridMax and scalar resolution
vec3 gridToWorld(vec3 gridPos, vec3 gridMin, vec3 gridMax, float resolution) {
    vec3 gridSize = gridMax - gridMin;
    return gridMin + (gridPos / resolution) * gridSize;
}

// World -> Grid [0,resolution]^3
vec3 worldToGrid(vec3 worldPos, vec3 gridMin, vec3 gridMax, float resolution) {
    vec3 gridSize = gridMax - gridMin;
    return ((worldPos - gridMin) / gridSize) * resolution;
}

// ============================================================================
// BRICK SPACE CONVERSIONS
// ============================================================================

// Leaf-voxel-local [0,1]^3 -> brick-local [0,BRICK_SIZE]^3
vec3 voxelLocalToBrick(vec3 voxelLocal01, int brickSize) {
    return voxelLocal01 * float(brickSize);
}

// Brick-local [0,BRICK_SIZE]^3 + brick grid coord -> grid [0,resolution]^3
vec3 brickLocalToGrid(vec3 brickLocal, ivec3 brickCoord, int brickSize) {
    return (vec3(brickCoord) * float(brickSize)) + brickLocal;
}

// ============================================================================
// OCTANT MIRRORING UTILITIES
// ============================================================================

// Unmirror position from ESVO mirrored space to local canonical space
// mirroredPos: Position in ESVO traversal mirrored space
// octantSize: Current voxel size (state.scale_exp2)
// octantMask: Ray octant mask from RayCoefficients
vec3 unmirrorToLocalSpace(vec3 mirroredPos, float octantSize, int octantMask) {
    vec3 localPos = mirroredPos;
    if ((octantMask & 1) == 0) localPos.x = 3.0 - octantSize - localPos.x;
    if ((octantMask & 2) == 0) localPos.y = 3.0 - octantSize - localPos.y;
    if ((octantMask & 4) == 0) localPos.z = 3.0 - octantSize - localPos.z;
    return localPos;
}

// Compute normalized local position within current octant
// Returns position in [0,1]^3 relative to octant origin
vec3 computeLocalNorm(vec3 mirroredPos, float octantSize, int octantMask) {
    vec3 localPos = unmirrorToLocalSpace(mirroredPos, octantSize, octantMask);
    return clamp(localPos - vec3(1.0), vec3(0.0), vec3(1.0));
}

// ============================================================================
// BRICK COORDINATE COMPUTATION
// ============================================================================

// Compute brick-local position from ESVO traversal state
// This function converts from ESVO [1,2]^3 mirrored space to brick voxel coordinates
// hitPos12: Hit position in ESVO [1,2] space
// statePos: Current traversal state position (min corner of node)
// scaleExp2: Current node size (2^(scale - esvoMaxScale))
// octantMask: Ray octant mask
// brickSize: Size of brick in voxels (typically 8)
vec3 computePosInBrick(vec3 hitPos12, vec3 statePos, float scaleExp2, int octantMask, int brickSize) {
    // Convert to Mirrored Space (matching ESVO traversal)
    vec3 hitPosMirrored = hitPos12;
    if ((octantMask & 1) == 0) hitPosMirrored.x = 3.0 - hitPosMirrored.x;
    if ((octantMask & 2) == 0) hitPosMirrored.y = 3.0 - hitPosMirrored.y;
    if ((octantMask & 4) == 0) hitPosMirrored.z = 3.0 - hitPosMirrored.z;

    // Compute Offset in Node (Mirrored Space)
    // statePos is the min corner of the CURRENT node (the brick) in mirrored space
    vec3 offsetMirrored = hitPosMirrored - statePos;

    // Scale to Brick Coordinates [0, brickSize] (Mirrored Orientation)
    vec3 posInBrickMirrored = (offsetMirrored / scaleExp2) * float(brickSize);

    // Unmirror to Local Brick Coordinates [0, brickSize] (Canonical Orientation)
    vec3 posInBrick = posInBrickMirrored;
    if ((octantMask & 1) == 0) posInBrick.x = float(brickSize) - posInBrick.x;
    if ((octantMask & 2) == 0) posInBrick.y = float(brickSize) - posInBrick.y;
    if ((octantMask & 4) == 0) posInBrick.z = float(brickSize) - posInBrick.z;

    // Clamp to handle precision issues at boundaries
    return clamp(posInBrick, vec3(0.0), vec3(float(brickSize)));
}

#endif // COORDINATE_TRANSFORMS_GLSL
