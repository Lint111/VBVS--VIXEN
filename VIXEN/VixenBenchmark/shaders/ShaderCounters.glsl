// ============================================================================
// ShaderCounters.glsl - GPU-side atomic counters for performance metrics
// ============================================================================
// Provides atomic counter buffer for collecting ray traversal statistics.
// CPU readback via MetricsCollector extracts avgIterationsPerRay, etc.
//
// NOTE: "iterations" refers to ESVO traversal loop iterations (PUSH/ADVANCE/POP),
// not individual voxels. Each iteration visits one octree node.
//
// Usage:
//   1. Define ENABLE_SHADER_COUNTERS before including this file
//   2. Bind counter buffer at binding slot SHADER_COUNTERS_BINDING
//   3. Call initShaderCounters() once per frame from thread (0,0)
//   4. Call recordRayStart() at ray cast begin
//   5. Call recordVoxelStep() for each octree node visited
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
// Maximum SVO levels supported for per-level statistics (typically 10-12 levels max)
#define MAX_SVO_LEVELS 16

layout(std430, binding = SHADER_COUNTERS_BINDING) buffer ShaderCountersBuffer {
    uint totalVoxelsTraversed;    // Atomic: total ESVO iterations (node visits)
    uint totalRaysCast;           // Atomic: total rays cast
    uint totalNodesVisited;       // Atomic: octree nodes visited
    uint totalLeafNodesVisited;   // Atomic: leaf nodes (bricks) visited
    uint totalEmptySpaceSkipped;  // Atomic: voxels skipped via empty-space
    uint rayHitCount;             // Atomic: rays that hit geometry
    uint rayMissCount;            // Atomic: rays that missed
    uint earlyTerminations;       // Atomic: rays that hit max iterations

    // Per-level node visit statistics (for cache locality analysis)
    // Cache coherence approximation: sequential/sibling node access vs random
    uint nodeVisitsPerLevel[MAX_SVO_LEVELS];  // Visits per octree level
    uint cacheHitsPerLevel[MAX_SVO_LEVELS];   // Consecutive/sibling node accesses (cache-friendly)
    uint cacheMissesPerLevel[MAX_SVO_LEVELS]; // Non-consecutive node accesses (cache-unfriendly)

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
        // Clear per-level statistics
        for (int i = 0; i < MAX_SVO_LEVELS; ++i) {
            atomicExchange(shaderCounters.nodeVisitsPerLevel[i], 0);
            atomicExchange(shaderCounters.cacheHitsPerLevel[i], 0);
            atomicExchange(shaderCounters.cacheMissesPerLevel[i], 0);
        }
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

// Record per-level node visit with cache locality tracking
// @param level: Current octree level (0 = root, increasing toward leaves)
// @param nodeIndex: Index of node being accessed
// @param prevNodeIndex: Index of previous node accessed (for locality detection)
// Cache hit: consecutive or sibling node (indices differ by < 8)
// Cache miss: random access (indices differ by >= 8)
void recordLevelVisit(int level, uint nodeIndex, uint prevNodeIndex) {
    if (level >= 0 && level < MAX_SVO_LEVELS) {
        atomicAdd(shaderCounters.nodeVisitsPerLevel[level], 1);

        // Approximate cache locality: sibling nodes are within 8 indices
        // This correlates with L1/L2 cache line utilization
        uint indexDiff = (nodeIndex > prevNodeIndex) ? (nodeIndex - prevNodeIndex) : (prevNodeIndex - nodeIndex);
        if (indexDiff < 8 || prevNodeIndex == 0) {
            // Cache-friendly: sequential or sibling access, or first access
            atomicAdd(shaderCounters.cacheHitsPerLevel[level], 1);
        } else {
            // Cache-unfriendly: random access pattern
            atomicAdd(shaderCounters.cacheMissesPerLevel[level], 1);
        }
    }
}

// Simplified: record level visit without locality tracking (for compatibility)
void recordLevelVisitSimple(int level) {
    if (level >= 0 && level < MAX_SVO_LEVELS) {
        atomicAdd(shaderCounters.nodeVisitsPerLevel[level], 1);
        // Default to cache hit for simple tracking (assumes good locality)
        atomicAdd(shaderCounters.cacheHitsPerLevel[level], 1);
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
void recordLevelVisit(int level, uint nodeIndex, uint prevNodeIndex) {}
void recordLevelVisitSimple(int level) {}
void recordEmptySpaceSkip(uint voxelsSkipped) {}
void recordRayEnd(bool hit, bool earlyTermination) {}
void recordVoxelSteps(uint count) {}
#endif // ENABLE_SHADER_COUNTERS

#endif // SHADER_COUNTERS_GLSL
