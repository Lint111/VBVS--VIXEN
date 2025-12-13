// ============================================================================
// ShaderCounters.glsl - GPU-side atomic counters for performance metrics
// ============================================================================
// Provides atomic counter buffer for collecting ray traversal statistics.
// CPU readback via MetricsCollector extracts avgVoxelsPerRay, etc.
//
// Usage:
//   1. Define ENABLE_SHADER_COUNTERS before including this file
//   2. Bind counter buffer at binding slot SHADER_COUNTERS_BINDING
//   3. Call initShaderCounters() once per frame from thread (0,0)
//   4. Call recordRayStart() at ray cast begin
//   5. Call recordVoxelStep() for each voxel/node tested
//   6. Call recordRayEnd(hit) when ray completes
// ============================================================================

#ifndef SHADER_COUNTERS_GLSL
#define SHADER_COUNTERS_GLSL

// Counter buffer binding (override before including if needed)
#ifndef SHADER_COUNTERS_BINDING
#define SHADER_COUNTERS_BINDING 6
#endif

#ifdef ENABLE_SHADER_COUNTERS

// Counter buffer layout - matches ShaderCounters struct in FrameMetrics.h
layout(std430, binding = SHADER_COUNTERS_BINDING) buffer ShaderCountersBuffer {
    uint totalVoxelsTraversed;    // Atomic: total voxels/nodes tested
    uint totalRaysCast;           // Atomic: total rays cast
    uint totalNodesVisited;       // Atomic: octree nodes visited
    uint totalLeafNodesVisited;   // Atomic: leaf nodes (bricks) visited
    uint totalEmptySpaceSkipped;  // Atomic: voxels skipped via empty-space
    uint rayHitCount;             // Atomic: rays that hit geometry
    uint rayMissCount;            // Atomic: rays that missed
    uint earlyTerminations;       // Atomic: rays that hit max iterations
    uint _padding[8];             // Padding for cache line alignment
} shaderCounters;

// Initialize counters (call from single thread at frame start)
// Note: Typically done by CPU before dispatch; this is backup
void initShaderCounters() {
    if (gl_GlobalInvocationID.x == 0 && gl_GlobalInvocationID.y == 0) {
        atomicExchange(shaderCounters.totalVoxelsTraversed, 0);
        atomicExchange(shaderCounters.totalRaysCast, 0);
        atomicExchange(shaderCounters.totalNodesVisited, 0);
        atomicExchange(shaderCounters.totalLeafNodesVisited, 0);
        atomicExchange(shaderCounters.totalEmptySpaceSkipped, 0);
        atomicExchange(shaderCounters.rayHitCount, 0);
        atomicExchange(shaderCounters.rayMissCount, 0);
        atomicExchange(shaderCounters.earlyTerminations, 0);
    }
    barrier();
    memoryBarrierBuffer();
}

// Record start of a new ray
void recordRayStart() {
    atomicAdd(shaderCounters.totalRaysCast, 1);
}

// Record a voxel/node step during traversal
void recordVoxelStep() {
    atomicAdd(shaderCounters.totalVoxelsTraversed, 1);
}

// Record octree node visit
void recordNodeVisit(bool isLeaf) {
    atomicAdd(shaderCounters.totalNodesVisited, 1);
    if (isLeaf) {
        atomicAdd(shaderCounters.totalLeafNodesVisited, 1);
    }
}

// Record empty space skip (optimization success)
void recordEmptySpaceSkip(uint voxelsSkipped) {
    atomicAdd(shaderCounters.totalEmptySpaceSkipped, voxelsSkipped);
}

// Record ray completion
void recordRayEnd(bool hit, bool earlyTermination) {
    if (hit) {
        atomicAdd(shaderCounters.rayHitCount, 1);
    } else {
        atomicAdd(shaderCounters.rayMissCount, 1);
    }
    if (earlyTermination) {
        atomicAdd(shaderCounters.earlyTerminations, 1);
    }
}

// Convenience: Record voxel step count (batch)
void recordVoxelSteps(uint count) {
    atomicAdd(shaderCounters.totalVoxelsTraversed, count);
}

#else
// No-op stubs when counters disabled (zero overhead)
void initShaderCounters() {}
void recordRayStart() {}
void recordVoxelStep() {}
void recordNodeVisit(bool isLeaf) {}
void recordEmptySpaceSkip(uint voxelsSkipped) {}
void recordRayEnd(bool hit, bool earlyTermination) {}
void recordVoxelSteps(uint count) {}
#endif // ENABLE_SHADER_COUNTERS

#endif // SHADER_COUNTERS_GLSL
