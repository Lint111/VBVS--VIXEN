#pragma once

#include "Logger.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph {

/**
 * @brief Specialized logger for compute pipeline performance metrics
 *
 * Tracks compute shader dispatch performance, pipeline creation times,
 * and workgroup configuration details. Designed for debugging and optimization
 * of compute-heavy operations like ray marching and voxel generation.
 *
 * Usage:
 * @code
 * auto perfLogger = std::make_unique<ComputePerformanceLogger>("RayMarching");
 * nodeLogger->AddChild(perfLogger.get());
 *
 * perfLogger->LogPipelineCreation(pipelineHandle, shaderKey, 1.2f);
 * perfLogger->LogDispatch(8, 8, 1, 512, 512);
 * @endcode
 */
class ComputePerformanceLogger : public Logger {
public:
    explicit ComputePerformanceLogger(const std::string& name)
        : Logger(name + "_Performance", true)
    {
        Info("=== Compute Performance Logger Initialized ===");
    }

    ~ComputePerformanceLogger() override = default;

    /**
     * @brief Log compute pipeline creation
     * @param pipelineHandle Pipeline handle (for identification)
     * @param shaderKey Shader identifier
     * @param creationTimeMs Time taken to create pipeline in milliseconds
     */
    void LogPipelineCreation(uint64_t pipelineHandle, const std::string& shaderKey, float creationTimeMs) {
        std::ostringstream oss;
        oss << "Pipeline Created:"
            << "\n  Handle: 0x" << std::hex << pipelineHandle
            << "\n  Shader: " << shaderKey
            << "\n  Creation Time: " << std::fixed << std::setprecision(2) << creationTimeMs << " ms";
        Info(oss.str());
    }

    /**
     * @brief Log compute dispatch configuration
     * @param workgroupX Workgroup size X
     * @param workgroupY Workgroup size Y
     * @param workgroupZ Workgroup size Z
     * @param dispatchX Number of workgroups to dispatch X
     * @param dispatchY Number of workgroups to dispatch Y
     * @param dispatchZ Number of workgroups to dispatch Z (default 1)
     */
    void LogDispatch(uint32_t workgroupX, uint32_t workgroupY, uint32_t workgroupZ,
                     uint32_t dispatchX, uint32_t dispatchY, uint32_t dispatchZ = 1) {
        std::ostringstream oss;
        oss << "Compute Dispatch:"
            << "\n  Workgroup Size: " << workgroupX << "x" << workgroupY << "x" << workgroupZ
            << "\n  Dispatch Groups: " << dispatchX << "x" << dispatchY << "x" << dispatchZ
            << "\n  Total Invocations: "
            << (workgroupX * workgroupY * workgroupZ * dispatchX * dispatchY * dispatchZ);
        Debug(oss.str());
    }

    /**
     * @brief Log shader module creation from SPIRV
     * @param moduleSizeBytes Size of SPIRV bytecode
     * @param stageCount Number of shader stages
     */
    void LogShaderModule(size_t moduleSizeBytes, uint32_t stageCount) {
        std::ostringstream oss;
        oss << "Shader Module Created:"
            << "\n  SPIRV Size: " << moduleSizeBytes << " bytes"
            << "\n  Stages: " << stageCount;
        Debug(oss.str());
    }

    /**
     * @brief Log command buffer recording
     * @param commandBufferHandle Command buffer handle
     * @param recordingTimeMs Time taken to record commands
     */
    void LogCommandBuffer(uint64_t commandBufferHandle, float recordingTimeMs) {
        std::ostringstream oss;
        oss << "Command Buffer Recorded:"
            << "\n  Handle: 0x" << std::hex << commandBufferHandle
            << "\n  Recording Time: " << std::fixed << std::setprecision(3)
            << recordingTimeMs << " ms";
        Debug(oss.str());
    }

    /**
     * @brief Log descriptor set binding
     * @param setCount Number of descriptor sets bound
     * @param dynamicOffsetCount Number of dynamic offsets
     */
    void LogDescriptorSets(uint32_t setCount, uint32_t dynamicOffsetCount = 0) {
        std::ostringstream oss;
        oss << "Descriptor Sets Bound: " << setCount;
        if (dynamicOffsetCount > 0) {
            oss << " (Dynamic Offsets: " << dynamicOffsetCount << ")";
        }
        Debug(oss.str());
    }

    /**
     * @brief Log push constants update
     * @param sizeBytes Size of push constant data
     */
    void LogPushConstants(uint32_t sizeBytes) {
        std::ostringstream oss;
        oss << "Push Constants Updated: " << sizeBytes << " bytes";
        Debug(oss.str());
    }

    /**
     * @brief Log memory barrier for compute synchronization
     * @param barrierType Type of barrier (e.g., "Image", "Buffer")
     */
    void LogMemoryBarrier(const std::string& barrierType) {
        Debug("Memory Barrier: " + barrierType);
    }
};

} // namespace Vixen::RenderGraph
