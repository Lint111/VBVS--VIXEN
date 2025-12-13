// ============================================================================
// TraceRecording.glsl - Per-Ray Debug Trace Capture System
// ============================================================================
// Records traversal steps for debugging and validation.
// Captures PUSH/ADVANCE/POP phases and brick entry/exit events.
//
// Usage:
//   1. Call beginRayTrace(pixelCoords) at start of ray
//   2. Call recordTraceStep(...) during traversal
//   3. Call endRayTrace(hit) at end of ray
//
// Set DEBUG_GRID_SPACING to control sampling density.
// Set to 0 or large value to disable for benchmarking.
//
// Dependencies:
//   - RayTraceBuffer (binding 4)
// ============================================================================

#ifndef TRACE_RECORDING_GLSL
#define TRACE_RECORDING_GLSL

// ============================================================================
// CONFIGURATION
// ============================================================================

// Grid spacing for debug samples - captures one ray every N pixels in each dimension
// E.g., 64 means capture a grid of (width/64) x (height/64) samples
#ifndef DEBUG_GRID_SPACING
#define DEBUG_GRID_SPACING 64
#endif

#define MAX_TRACE_STEPS 64      // Max steps per traced ray

// ============================================================================
// TRACE STEP TYPE CONSTANTS
// ============================================================================

const uint TRACE_STEP_PUSH = 0u;        // Descended into child octant
const uint TRACE_STEP_ADVANCE = 1u;     // Advanced to sibling octant
const uint TRACE_STEP_POP = 2u;         // Popped back to parent
const uint TRACE_STEP_BRICK_ENTER = 3u; // Entered a brick volume
const uint TRACE_STEP_BRICK_DDA = 4u;   // DDA step within brick
const uint TRACE_STEP_BRICK_EXIT = 5u;  // Exited brick without hit
const uint TRACE_STEP_HIT = 6u;         // Found solid voxel
const uint TRACE_STEP_MISS = 7u;        // Exited octree without hit

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Per-step trace record (must match C++ exactly - std430 layout, 48 bytes)
struct TraceStep {
    uint stepType;        // TRACE_STEP_* constant
    uint nodeIndex;       // Current octree node index
    int scale;            // Current ESVO scale
    uint octantMask;      // Current octant mask (0-7)
    vec3 position;        // Position at this step (in [1,2]^3 space)
    float tMin;           // T-span min at this step
    float tMax;           // T-span max at this step
    uint childDescLow;    // Child descriptor (for context)
    uint childDescHigh;
    uint _padding;        // Align to 48 bytes
};

// Per-ray trace header (16 bytes)
struct RayTraceHeader {
    uvec2 pixel;          // Pixel coordinates
    uint stepCount;       // Number of steps recorded
    uint flags;           // Bit 0: hit, Bit 1: overflow (more than MAX_TRACE_STEPS)
};

// Debug visualization state (local struct, not a buffer)
// Used for in-shader debug visualization modes (keys 1-9)
struct DebugRaySample {
    uvec2 pixel;          // Pixel coordinates
    uint octantMask;      // Ray octant mask
    uint hitFlag;         // 1 if hit, 0 if miss
    uint exitCode;        // Exit reason
    uint lastStepMask;    // Last step direction mask
    uint iterationCount;  // Total iterations
    int scale;            // Final ESVO scale
    uint stateIdx;        // Final state index
    float tMin;           // Final t_min
    float tMax;           // Final t_max
    float scaleExp2;      // Final scale_exp2
    vec3 posMirrored;     // Final position in mirrored space
    vec3 localNorm;       // Computed local normal
    vec3 rayDir;          // Ray direction
};

// Exit code constants
const uint DEBUG_EXIT_NONE = 0u;
const uint DEBUG_EXIT_HIT = 1u;
const uint DEBUG_EXIT_NO_HIT = 2u;
const uint DEBUG_EXIT_STACK = 3u;
const uint DEBUG_EXIT_INVALID_SPAN = 4u;

// ============================================================================
// BUFFER LAYOUT CONSTANTS
// ============================================================================

const uint TRACE_HEADER_SIZE = 16;   // sizeof(RayTraceHeader) = 4*4 = 16 bytes
const uint TRACE_STEP_SIZE = 48;     // sizeof(TraceStep) = 48 bytes
const uint TRACE_RAY_SIZE = TRACE_HEADER_SIZE + (MAX_TRACE_STEPS * TRACE_STEP_SIZE);

// ============================================================================
// THREAD-LOCAL TRACE STATE
// ============================================================================

// These are stored in registers during traversal
uint g_traceRaySlot = 0xFFFFFFFF;  // Slot in trace buffer (0xFFFFFFFF = not tracing)
uint g_traceStepCount = 0;         // Current step count for this ray

// ============================================================================
// TRACE RECORDING FUNCTIONS
// ============================================================================

// Returns true if this pixel should capture debug data (grid-based sampling)
// PERFORMANCE: Set DEBUG_GRID_SPACING to large value for benchmarking
bool shouldCaptureDebug(ivec2 pixelCoords) {
    // Capture center pixel (assuming 800x600)
    if (pixelCoords.x == 400 && pixelCoords.y == 300) return true;

    // Capture if pixel is on grid intersection
    return (pixelCoords.x % DEBUG_GRID_SPACING == 0) &&
           (pixelCoords.y % DEBUG_GRID_SPACING == 0);
}

// Initialize tracing for a pixel (call once at start of ray)
bool beginRayTrace(ivec2 pixelCoords) {
    if (!shouldCaptureDebug(pixelCoords)) {
        g_traceRaySlot = 0xFFFFFFFF;
        return false;
    }

    // Allocate a ray slot (atomic increment with ring buffer wrap)
    uint slot = atomicAdd(traceWriteIndex, 1u);
    if (slot >= 256u) {
        g_traceRaySlot = 0xFFFFFFFF;
        return false;
    }

    g_traceRaySlot = slot;
    g_traceStepCount = 0;

    // Write header (pixel coords, stepCount will be updated at end)
    uint baseOffset = (slot * TRACE_RAY_SIZE) / 4;  // Convert bytes to uint offset
    traceData[baseOffset + 0] = uint(pixelCoords.x);
    traceData[baseOffset + 1] = uint(pixelCoords.y);
    traceData[baseOffset + 2] = 0u;  // stepCount (updated at end)
    traceData[baseOffset + 3] = 0u;  // flags

    return true;
}

// Record a traversal step
void recordTraceStep(uint stepType, uint nodeIndex, int scale, uint octantMask,
                     vec3 pos, float tMin, float tMax, uvec2 childDesc) {
    if (g_traceRaySlot == 0xFFFFFFFF || g_traceStepCount >= MAX_TRACE_STEPS) {
        return;
    }

    // Calculate offset for this step
    uint rayBase = (g_traceRaySlot * TRACE_RAY_SIZE) / 4;
    uint stepBase = rayBase + (TRACE_HEADER_SIZE / 4) + (g_traceStepCount * TRACE_STEP_SIZE / 4);

    // Write step data (must match TraceStep struct layout)
    traceData[stepBase + 0] = stepType;
    traceData[stepBase + 1] = nodeIndex;
    traceData[stepBase + 2] = uint(scale);  // int to uint
    traceData[stepBase + 3] = octantMask;
    traceData[stepBase + 4] = floatBitsToUint(pos.x);
    traceData[stepBase + 5] = floatBitsToUint(pos.y);
    traceData[stepBase + 6] = floatBitsToUint(pos.z);
    traceData[stepBase + 7] = floatBitsToUint(tMin);
    traceData[stepBase + 8] = floatBitsToUint(tMax);
    traceData[stepBase + 9] = childDesc.x;
    traceData[stepBase + 10] = childDesc.y;
    traceData[stepBase + 11] = 0u;  // padding

    g_traceStepCount++;
}

// Finalize trace recording for this ray
void endRayTrace(bool hit) {
    if (g_traceRaySlot == 0xFFFFFFFF) return;

    uint offset = g_traceRaySlot * (TRACE_RAY_SIZE / 4);
    traceData[offset + 2] = g_traceStepCount;

    uint flags = 0u;
    if (hit) flags |= 1u;
    if (g_traceStepCount >= MAX_TRACE_STEPS) flags |= 2u;

    traceData[offset + 3] = flags;
}

#endif // TRACE_RECORDING_GLSL
