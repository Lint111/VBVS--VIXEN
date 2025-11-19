#pragma once

#include <cstdint>

namespace SVO {

// ============================================================================
// Domain-Specific Brick Layouts
// ============================================================================

/**
 * Sound propagation brick data.
 *
 * Models acoustic properties for voxel-based sound simulation:
 * - Density: Material density (affects impedance)
 * - Absorption: Sound energy absorbed per bounce [0,1]
 * - Reflection: Sound energy reflected [0,1]
 * - Transmission: Sound energy transmitted through material [0,1]
 *
 * Total: 16 bytes/voxel
 * 8³ brick = 512 voxels = 8KB (fits in L1 cache)
 */
struct SoundPropagationBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = float;    // Density (kg/m³, scaled)
    using Array1Type = float;    // Absorption coefficient [0,1]
    using Array2Type = float;    // Reflection coefficient [0,1]
    using Array3Type = float;    // Transmission coefficient [0,1]

    // Unused
    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Thermal simulation brick data.
 *
 * Models heat transfer for temperature/fire simulation:
 * - Temperature: Current temperature (Kelvin or Celsius)
 * - Conductivity: Thermal conductivity
 * - Capacity: Heat capacity
 * - EmissivityAbsorption: Combined emissivity/absorption (packed)
 *
 * Total: 16 bytes/voxel
 */
struct ThermalSimulationBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = float;     // Temperature (K)
    using Array1Type = float;     // Thermal conductivity (W/m·K)
    using Array2Type = float;     // Heat capacity (J/kg·K)
    using Array3Type = uint32_t;  // Emissivity+absorption (packed 16+16)

    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Fluid simulation brick data (SPH-compatible).
 *
 * Sparse voxel grid for hybrid particle-voxel fluid sim:
 * - Density: Fluid density at voxel
 * - Velocity: Packed velocity vector (3 × int16)
 * - Pressure: Fluid pressure
 * - Viscosity: Dynamic viscosity
 *
 * Total: 18 bytes/voxel
 */
struct FluidSimulationBrick {
    static constexpr size_t numArrays = 5;

    using Array0Type = float;     // Density (kg/m³)
    using Array1Type = uint16_t;  // Velocity X (scaled int16)
    using Array2Type = uint16_t;  // Velocity Y
    using Array3Type = uint16_t;  // Velocity Z
    using Array4Type = float;     // Pressure (Pa)

    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Lighting/GI brick data (voxel cone tracing).
 *
 * Stores precomputed lighting for real-time GI:
 * - Radiance: RGB radiance (packed)
 * - Normal: Surface normal (oct-encoded)
 * - Occlusion: Ambient occlusion
 * - Depth: Distance to nearest surface
 *
 * Total: 12 bytes/voxel
 * 8³ brick = 512 voxels = 6KB (excellent L1 fit)
 */
struct LightingGIBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = uint32_t;  // Radiance RGB (10:11:11 or 8:8:8:8)
    using Array1Type = uint16_t;  // Normal (oct-encoded 16-bit)
    using Array2Type = uint8_t;   // Ambient occlusion
    using Array3Type = uint16_t;  // Depth (half-float or scaled uint16)

    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * AI navigation brick data.
 *
 * Voxel-based pathfinding and spatial queries:
 * - Walkability: Can AI traverse? [0,255]
 * - CostMultiplier: Movement cost (e.g., mud = high cost)
 * - CoverValue: Tactical cover quality
 * - Visibility: Visibility flags (line-of-sight)
 *
 * Total: 4 bytes/voxel (very cache-friendly!)
 * 8³ brick = 512 voxels = 2KB
 */
struct AINavigationBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = uint8_t;   // Walkability [0=blocked, 255=free]
    using Array1Type = uint8_t;   // Cost multiplier
    using Array2Type = uint8_t;   // Cover value
    using Array3Type = uint8_t;   // Visibility flags

    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Destruction/physics brick data.
 *
 * For destructible voxel environments:
 * - Health: Structural integrity [0,255]
 * - MaterialID: Material type (determines debris)
 * - StressX/Y/Z: Accumulated stress vectors
 * - Fracture: Fracture pattern flags
 *
 * Total: 12 bytes/voxel
 */
struct DestructionPhysicsBrick {
    static constexpr size_t numArrays = 6;

    using Array0Type = uint8_t;   // Health/integrity
    using Array1Type = uint8_t;   // Material ID
    using Array2Type = uint16_t;  // Stress X (signed, scaled)
    using Array3Type = uint16_t;  // Stress Y
    using Array4Type = uint16_t;  // Stress Z
    using Array5Type = uint16_t;  // Fracture pattern

    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Particle simulation brick data.
 *
 * Voxel-based particle field (smoke, dust, magic effects):
 * - ParticleCount: Number of particles in voxel
 * - VelocityField: Average velocity (packed)
 * - ColorTint: Particle color (RGB)
 * - Lifetime: Average particle lifetime
 *
 * Total: 12 bytes/voxel
 */
struct ParticleFieldBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = uint16_t;  // Particle count (0-65535)
    using Array1Type = uint32_t;  // Velocity field (packed 10:11:11)
    using Array2Type = uint32_t;  // Color tint RGB8
    using Array3Type = uint16_t;  // Average lifetime (frames)

    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Minimal occupancy brick (for collision only).
 *
 * Ultra-compact layout for simple collision detection:
 * - Single bit per voxel using bitfield (8³ = 512 bits = 64 bytes total)
 * - But we use byte-per-voxel for simplicity and alignment
 *
 * Total: 1 byte/voxel
 * 8³ brick = 512 voxels = 512 bytes (super cache-friendly!)
 */
struct OccupancyBrick {
    static constexpr size_t numArrays = 1;

    using Array0Type = uint8_t;   // Occupied (0=empty, 1=solid)

    using Array1Type = void;
    using Array2Type = void;
    using Array3Type = void;
    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

/**
 * Multi-purpose debug visualization brick.
 *
 * For runtime debugging and profiling:
 * - HeatmapValue: Visualization intensity
 * - DebugFlags: Runtime debug flags
 * - RayHitCount: Number of ray intersections (debugging)
 * - CustomData: User-defined debug data
 *
 * Total: 8 bytes/voxel
 */
struct DebugVisualizationBrick {
    static constexpr size_t numArrays = 4;

    using Array0Type = float;     // Heatmap value (0-1)
    using Array1Type = uint8_t;   // Debug flags
    using Array2Type = uint16_t;  // Ray hit count
    using Array3Type = uint8_t;   // Custom debug data

    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

// ============================================================================
// Cache Budget Guidelines
// ============================================================================

/**
 * Recommended cache budgets for different scenarios:
 *
 * L1 Cache (32KB typical):
 * - OccupancyBrick:          512 bytes   (1.6% utilization) ✓✓✓
 * - AINavigationBrick:       2KB         (6.3% utilization) ✓✓
 * - LightingGIBrick:         6KB         (18.8% utilization) ✓
 * - SoundPropagationBrick:   8KB         (25% utilization) ✓
 * - FluidSimulationBrick:    9KB         (28% utilization) ~
 *
 * L2 Cache (256KB typical):
 * - All above layouts fit easily
 * - Can use larger brick sizes (16³ = 32KB for DefaultLeafData)
 *
 * Usage:
 *   BrickStorage<SoundPropagationBrick> audio(3, 1024, 32768);
 *   auto report = audio.getCacheBudgetReport();
 *   if (!report.fitsInCache) {
 *       // Consider reducing brick depth or simplifying layout
 *   }
 */

} // namespace SVO
