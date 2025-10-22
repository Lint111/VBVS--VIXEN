#pragma once

namespace Vixen::RenderGraph {

    /**
 * @brief Unique identifier for node types
 */
using NodeTypeId = uint32_t;

/**
 * @brief Pipeline type enumeration
 */
enum class PipelineType {
    None,       // Not a pipeline node (e.g., resource management)
    Graphics,
    Compute,
    RayTracing,
    Transfer
};

/**
 * @brief Device capability flags
 */
enum class DeviceCapability : uint32_t {
    None = 0,
    Graphics = 1 << 0,
    Compute = 1 << 1,
    Transfer = 1 << 2,
    RayTracing = 1 << 3,
    GeometryShader = 1 << 4,
    TessellationShader = 1 << 5,
    MeshShader = 1 << 6,
    MultiDrawIndirect = 1 << 7,
    DepthClamp = 1 << 8,
    FillModeNonSolid = 1 << 9
};

inline DeviceCapability operator|(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DeviceCapability operator&(DeviceCapability a, DeviceCapability b) {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasCapability(DeviceCapability flags, DeviceCapability check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

using DeviceCapabilityFlags = DeviceCapability;

/**
 * @brief Workload metrics for scheduling
 */
struct WorkloadMetrics {
    // Space complexity
    size_t estimatedMemoryFootprint = 0;  // Bytes
    
    // Time complexity (relative units)
    float estimatedComputeCost = 1.0f;    // Relative to simple pass
    float estimatedBandwidthCost = 1.0f;  // Relative to simple pass
    
    // Parallelization potential
    bool canRunInParallel = true;
    uint32_t preferredBatchSize = 1;      // For instanced operations
};
}