#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/DebugBufferReaderNodeConfig.h"
#include "Debug/DebugRaySample.h"
#include "Debug/DebugCaptureBuffer.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {

/**
 * @brief Node type for debug buffer reading
 */
class DebugBufferReaderNodeType : public TypedNodeType<DebugBufferReaderNodeConfig> {
public:
    DebugBufferReaderNodeType(const std::string& typeName = "DebugBufferReader")
        : TypedNodeType<DebugBufferReaderNodeConfig>(typeName) {}
    virtual ~DebugBufferReaderNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Export format options
 */
enum class DebugExportFormat {
    Console,    // Print to stdout
    CSV,        // Export to CSV file
    JSON,       // Export to JSON file
    All         // All formats
};

/**
 * @brief Debug buffer reader node
 *
 * Reads GPU debug buffers back to CPU and exports them for analysis.
 * Supports multiple export formats and filtering.
 *
 * Usage:
 * 1. Connect DEBUG_BUFFER to the shader's debug output SSBO
 * 2. Configure export format and output path
 * 3. Execute to read and export data
 *
 * The node automatically detects the buffer type (DebugRaySample, etc.)
 * based on the buffer size and header.
 */
class DebugBufferReaderNode : public TypedNode<DebugBufferReaderNodeConfig> {
public:
    DebugBufferReaderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~DebugBufferReaderNode() override = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set export format
     */
    void SetExportFormat(DebugExportFormat format) { exportFormat = format; }

    /**
     * @brief Set output file path (for CSV/JSON export)
     */
    void SetOutputPath(const std::string& path) { outputPath = path; }

    /**
     * @brief Set maximum samples to read (0 = all)
     */
    void SetMaxSamples(uint32_t max) { maxSamples = max; }

    /**
     * @brief Enable/disable automatic export on execute
     */
    void SetAutoExport(bool enable) { autoExport = enable; }

    /**
     * @brief Set filter function for samples (optional)
     * Return true to include sample, false to exclude
     */
    void SetFilter(std::function<bool(const Debug::DebugRaySample&)> filter) {
        sampleFilter = filter;
    }

    // =========================================================================
    // Data access
    // =========================================================================

    /**
     * @brief Get the last read samples
     */
    const std::vector<Debug::DebugRaySample>& GetSamples() const { return samples; }

    /**
     * @brief Get samples filtered by octant mask
     */
    std::vector<Debug::DebugRaySample> GetSamplesByOctant(uint32_t octantMask) const;

    /**
     * @brief Get samples filtered by exit code
     */
    std::vector<Debug::DebugRaySample> GetSamplesByExitCode(Debug::DebugExitCode code) const;

    /**
     * @brief Get samples that hit vs missed
     */
    std::vector<Debug::DebugRaySample> GetHitSamples() const;
    std::vector<Debug::DebugRaySample> GetMissSamples() const;

    /**
     * @brief Print summary statistics
     */
    void PrintSummary() const;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:

    // Export
    void ExportSamples();
    void ExportToConsole();
    void ExportToCSV();
    void ExportToJSON();

    // Configuration
    DebugExportFormat exportFormat = DebugExportFormat::Console;
    std::string outputPath = "debug_ray_samples";
    uint32_t maxSamples = 1000;
    bool autoExport = true;
    std::function<bool(const Debug::DebugRaySample&)> sampleFilter;


    // Data
    std::vector<Debug::DebugRaySample> samples;
    uint32_t totalSamplesInBuffer = 0;
};

} // namespace Vixen::RenderGraph
