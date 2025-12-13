#pragma once

#include "IExportable.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Exit codes for ray traversal (must match shader constants)
 */
enum class DebugExitCode : uint32_t {
    None = 0,           // DEBUG_EXIT_NONE - traversal ongoing
    Hit = 1,            // DEBUG_EXIT_HIT - found solid voxel
    NoHit = 2,          // DEBUG_EXIT_NO_HIT - finished without hit
    StackExit = 3,      // DEBUG_EXIT_STACK - POP exited octree
    InvalidSpan = 4     // DEBUG_EXIT_INVALID_SPAN - t_min > t_max
};

/**
 * @brief C++ struct matching shader DebugRaySample
 *
 * IMPORTANT: This struct must match the GLSL layout EXACTLY for GPU readback.
 * The shader uses std430 layout, so we need proper alignment.
 * This struct MUST NOT inherit from classes with virtual methods to avoid vtable pointer.
 *
 * Shader definition (VoxelRayMarch.comp):
 * struct DebugRaySample {
 *     uvec2 pixel;          // 8 bytes
 *     uint octantMask;      // 4 bytes
 *     uint hitFlag;         // 4 bytes
 *     uint exitCode;        // 4 bytes
 *     uint lastStepMask;    // 4 bytes
 *     uint iterationCount;  // 4 bytes
 *     int scale;            // 4 bytes
 *     uint stateIdx;        // 4 bytes
 *     float tMin;           // 4 bytes
 *     float tMax;           // 4 bytes
 *     float scaleExp2;      // 4 bytes
 *     float reserved0;      // 4 bytes (padding)
 *     vec3 posMirrored;     // 12 bytes
 *     float reserved1;      // 4 bytes (padding to vec4)
 *     vec3 localNorm;       // 12 bytes
 *     float reserved2;      // 4 bytes (padding to vec4)
 *     vec3 rayDir;          // 12 bytes
 *     float reserved3;      // 4 bytes (padding to vec4)
 * };
 * Total: 96 bytes
 *
 * NOTE: Export methods are non-virtual to keep struct POD-compatible.
 */
struct alignas(16) DebugRaySample {
    // Pixel coordinates (uvec2) - offset 0
    uint32_t pixelX;
    uint32_t pixelY;

    // Traversal state - offset 8
    uint32_t octantMask;
    uint32_t hitFlag;
    uint32_t exitCode;
    uint32_t lastStepMask;
    uint32_t iterationCount;
    int32_t scale;
    uint32_t stateIdx;

    // T-span values - offset 36
    float tMin;
    float tMax;
    float scaleExp2;
    float reserved0;

    // std430 padding to align vec3 to 16-byte boundary - offset 52
    float _padding1;
    float _padding2;
    float _padding3;

    // Position in mirrored ESVO space [1,2]³ - offset 64 (16-byte aligned)
    float posMirroredX;
    float posMirroredY;
    float posMirroredZ;
    float reserved1;

    // Position in local normalized space [0,1]³ - offset 80 (16-byte aligned)
    float localNormX;
    float localNormY;
    float localNormZ;
    float reserved2;

    // Ray direction (world space) - offset 96 (16-byte aligned)
    float rayDirX;
    float rayDirY;
    float rayDirZ;
    float reserved3;

    // =========================================================================
    // Export methods (non-virtual to keep struct POD)
    // =========================================================================

    std::string ToString() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);

        ss << "Pixel(" << pixelX << "," << pixelY << ") ";
        ss << "octant=" << octantMask << " ";
        ss << "hit=" << hitFlag << " ";
        ss << "exit=" << ExitCodeToString(static_cast<DebugExitCode>(exitCode)) << " ";
        ss << "iter=" << iterationCount << " ";
        ss << "scale=" << scale << " ";
        ss << "idx=" << stateIdx << " ";
        ss << "t=[" << tMin << "," << tMax << "] ";
        ss << "scaleExp2=" << scaleExp2 << " ";
        ss << "posMir=(" << posMirroredX << "," << posMirroredY << "," << posMirroredZ << ") ";
        ss << "localNorm=(" << localNormX << "," << localNormY << "," << localNormZ << ") ";
        ss << "rayDir=(" << rayDirX << "," << rayDirY << "," << rayDirZ << ")";

        return ss.str();
    }

    std::string ToCSV() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);

        ss << pixelX << "," << pixelY << ",";
        ss << octantMask << "," << hitFlag << "," << exitCode << ",";
        ss << lastStepMask << "," << iterationCount << ",";
        ss << scale << "," << stateIdx << ",";
        ss << tMin << "," << tMax << "," << scaleExp2 << ",";
        ss << posMirroredX << "," << posMirroredY << "," << posMirroredZ << ",";
        ss << localNormX << "," << localNormY << "," << localNormZ << ",";
        ss << rayDirX << "," << rayDirY << "," << rayDirZ;

        return ss.str();
    }

    std::string GetCSVHeader() const {
        return "pixelX,pixelY,octantMask,hitFlag,exitCode,lastStepMask,iterationCount,"
               "scale,stateIdx,tMin,tMax,scaleExp2,"
               "posMirroredX,posMirroredY,posMirroredZ,"
               "localNormX,localNormY,localNormZ,"
               "rayDirX,rayDirY,rayDirZ";
    }

    std::string ToJSON() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);

        ss << "{";
        ss << "\"pixel\":[" << pixelX << "," << pixelY << "],";
        ss << "\"octantMask\":" << octantMask << ",";
        ss << "\"hitFlag\":" << hitFlag << ",";
        ss << "\"exitCode\":" << exitCode << ",";
        ss << "\"exitCodeName\":\"" << ExitCodeToString(static_cast<DebugExitCode>(exitCode)) << "\",";
        ss << "\"lastStepMask\":" << lastStepMask << ",";
        ss << "\"iterationCount\":" << iterationCount << ",";
        ss << "\"scale\":" << scale << ",";
        ss << "\"stateIdx\":" << stateIdx << ",";
        ss << "\"tMin\":" << tMin << ",";
        ss << "\"tMax\":" << tMax << ",";
        ss << "\"scaleExp2\":" << scaleExp2 << ",";
        ss << "\"posMirrored\":[" << posMirroredX << "," << posMirroredY << "," << posMirroredZ << "],";
        ss << "\"localNorm\":[" << localNormX << "," << localNormY << "," << localNormZ << "],";
        ss << "\"rayDir\":[" << rayDirX << "," << rayDirY << "," << rayDirZ << "]";
        ss << "}";

        return ss.str();
    }

    // =========================================================================
    // Helper methods
    // =========================================================================

    glm::uvec2 GetPixel() const { return glm::uvec2(pixelX, pixelY); }
    glm::vec3 GetPosMirrored() const { return glm::vec3(posMirroredX, posMirroredY, posMirroredZ); }
    glm::vec3 GetLocalNorm() const { return glm::vec3(localNormX, localNormY, localNormZ); }
    glm::vec3 GetRayDir() const { return glm::vec3(rayDirX, rayDirY, rayDirZ); }

    DebugExitCode GetExitCode() const { return static_cast<DebugExitCode>(exitCode); }
    bool IsHit() const { return hitFlag != 0; }

    static const char* ExitCodeToString(DebugExitCode code) {
        switch (code) {
            case DebugExitCode::None: return "NONE";
            case DebugExitCode::Hit: return "HIT";
            case DebugExitCode::NoHit: return "NO_HIT";
            case DebugExitCode::StackExit: return "STACK_EXIT";
            case DebugExitCode::InvalidSpan: return "INVALID_SPAN";
            default: return "UNKNOWN";
        }
    }

    // =========================================================================
    // Filtering helpers for analysis
    // =========================================================================

    /**
     * @brief Check if this ray has a specific octant mask
     */
    bool HasOctantMask(uint32_t mask) const { return octantMask == mask; }

    /**
     * @brief Check if ray direction is positive on an axis
     * bit 0 = X positive, bit 1 = Y positive, bit 2 = Z positive
     */
    uint32_t GetDirectionBits() const {
        uint32_t bits = 0;
        if (rayDirX > 0) bits |= 1;
        if (rayDirY > 0) bits |= 2;
        if (rayDirZ > 0) bits |= 4;
        return bits;
    }

    /**
     * @brief Check if octant_mask matches expected for ray direction
     * In ESVO: octant_mask bit = 0 means axis IS mirrored (positive ray direction)
     *          octant_mask bit = 1 means axis NOT mirrored (negative ray direction)
     */
    bool IsOctantMaskCorrect() const {
        // For each axis: if rayDir > 0, bit should be 0 (mirrored)
        //                if rayDir < 0, bit should be 1 (not mirrored)
        uint32_t expectedMask = 7; // Start with all bits set (negative dirs)
        if (rayDirX > 0) expectedMask &= ~1u;
        if (rayDirY > 0) expectedMask &= ~2u;
        if (rayDirZ > 0) expectedMask &= ~4u;
        return octantMask == expectedMask;
    }
};

// Verify struct size matches shader std430 layout
// std430 requires vec3 to align to 16 bytes inside structs
// Layout: 52 scalars + 12 padding + 48 (3x vec3+pad) = 112 bytes
static_assert(sizeof(DebugRaySample) == 112, "DebugRaySample size must match shader std430 layout (112 bytes)");

/**
 * @brief Header for debug capture buffer (matches shader std430 layout)
 *
 * In std430, the DebugRaySample array needs 16-byte alignment (due to vec3),
 * so there's padding between the header and the first sample.
 */
struct alignas(16) DebugCaptureHeader {
    uint32_t writeIndex;    // Current write position (atomic)
    uint32_t capacity;      // Maximum number of samples
    uint32_t _padding[2];   // Padding to align samples array to 16 bytes
};

static_assert(sizeof(DebugCaptureHeader) == 16, "DebugCaptureHeader must be 16 bytes (aligned for samples array)");

// ============================================================================
// PER-RAY TRAVERSAL TRACE (Full path debugging)
// ============================================================================

/**
 * @brief Step types for ray traversal trace (must match shader constants)
 */
enum class TraceStepType : uint32_t {
    Push = 0,        // Descended into child octant
    Advance = 1,     // Advanced to sibling octant
    Pop = 2,         // Popped back to parent
    BrickEnter = 3,  // Entered a brick volume
    BrickDDA = 4,    // DDA step within brick
    BrickExit = 5,   // Exited brick without hit
    Hit = 6,         // Found solid voxel
    Miss = 7,        // Exited octree without hit
    InvalidChildIdx = 8, // Invalid child index in leaf hit
    InvalidBrickIdx = 9, // Invalid brick index in leaf hit
    CallingDDA = 10      // About to call DDA
};

inline const char* TraceStepTypeToString(TraceStepType type) {
    switch (type) {
        case TraceStepType::Push: return "PUSH";
        case TraceStepType::Advance: return "ADVANCE";
        case TraceStepType::Pop: return "POP";
        case TraceStepType::BrickEnter: return "BRICK_ENTER";
        case TraceStepType::BrickDDA: return "BRICK_DDA";
        case TraceStepType::BrickExit: return "BRICK_EXIT";
        case TraceStepType::Hit: return "HIT";
        case TraceStepType::Miss: return "MISS";
        case TraceStepType::InvalidChildIdx: return "INVALID_CHILD_IDX";
        case TraceStepType::InvalidBrickIdx: return "INVALID_BRICK_IDX";
        case TraceStepType::CallingDDA: return "CALLING_DDA";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Per-step trace record (must match shader std430 layout - 48 bytes)
 */
struct alignas(4) TraceStep {
    uint32_t stepType;      // TraceStepType enum
    uint32_t nodeIndex;     // Current octree node index
    int32_t scale;          // Current ESVO scale
    uint32_t octantMask;    // Current octant mask (0-7)
    float posX, posY, posZ; // Position at this step (in [1,2]³ space)
    float tMin;             // T-span min at this step
    float tMax;             // T-span max at this step
    uint32_t childDescLow;  // Child descriptor low bits
    uint32_t childDescHigh; // Child descriptor high bits
    uint32_t _padding;      // Align to 48 bytes

    TraceStepType GetStepType() const { return static_cast<TraceStepType>(stepType); }
    glm::vec3 GetPosition() const { return glm::vec3(posX, posY, posZ); }

    std::string ToString() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << TraceStepTypeToString(GetStepType());
        ss << " node=" << nodeIndex;
        ss << " scale=" << scale;
        ss << " oct=" << octantMask;
        ss << " pos=(" << posX << "," << posY << "," << posZ << ")";
        ss << " t=[" << tMin << "," << tMax << "]";
        return ss.str();
    }
};

static_assert(sizeof(TraceStep) == 48, "TraceStep must be 48 bytes");

/**
 * @brief Per-ray trace header (16 bytes)
 */
struct alignas(4) RayTraceHeader {
    uint32_t pixelX;
    uint32_t pixelY;
    uint32_t stepCount;     // Number of steps recorded
    uint32_t flags;         // Bit 0: hit, Bit 1: overflow

    bool IsHit() const { return (flags & 1u) != 0; }
    bool HasOverflow() const { return (flags & 2u) != 0; }
    glm::uvec2 GetPixel() const { return glm::uvec2(pixelX, pixelY); }
};

static_assert(sizeof(RayTraceHeader) == 16, "RayTraceHeader must be 16 bytes");

/**
 * @brief Constants for trace buffer layout
 */
constexpr uint32_t MAX_TRACE_STEPS = 64;
constexpr uint32_t TRACE_RAY_SIZE = sizeof(RayTraceHeader) + (MAX_TRACE_STEPS * sizeof(TraceStep));

/**
 * @brief Complete ray trace record (header + all steps)
 */
struct RayTrace {
    RayTraceHeader header;
    std::vector<TraceStep> steps;

    std::string ToString() const {
        std::ostringstream ss;
        ss << "=== Ray Trace for pixel (" << header.pixelX << "," << header.pixelY << ") ===\n";
        ss << "Steps: " << header.stepCount;
        if (header.HasOverflow()) ss << " (OVERFLOW)";
        ss << ", Result: " << (header.IsHit() ? "HIT" : "MISS") << "\n";

        for (size_t i = 0; i < steps.size(); ++i) {
            ss << "  [" << i << "] " << steps[i].ToString() << "\n";
        }
        return ss.str();
    }
};

/**
 * @brief Header for trace buffer (8 bytes + padding to 16)
 */
struct alignas(16) TraceBufferHeader {
    uint32_t writeIndex;    // Next ray slot to write
    uint32_t capacity;      // Max rays (not steps)
    uint32_t _padding[2];   // Align to 16 bytes
};

static_assert(sizeof(TraceBufferHeader) == 16, "TraceBufferHeader must be 16 bytes");

} // namespace Vixen::RenderGraph::Debug
