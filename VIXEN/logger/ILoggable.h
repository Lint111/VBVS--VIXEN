#pragma once

#include <memory>
#include <string>

// Forward declaration
class Logger;

namespace Vixen::RenderGraph {

/**
 * @brief Interface for graph subsystems that support logging
 *
 * Provides standardized logger registration, access, and management
 * for non-node graph components like GraphTopology, GraphLifecycleHooks, etc.
 *
 * Usage pattern:
 * @code
 * class GraphTopology : public ILoggable {
 * public:
 *     GraphTopology() { InitializeLogger("Topology"); }
 *
 *     void AddEdge(const GraphEdge& edge) {
 *         if (auto* log = GetLogger()) {
 *             log->Debug("Adding edge...");
 *         }
 *     }
 * };
 * @endcode
 */
class ILoggable {
public:
    virtual ~ILoggable() = default;

    /**
     * @brief Get the subsystem's logger
     * @return Logger pointer (may be nullptr if not initialized)
     */
    Logger* GetLogger() const { return logger.get(); }

    /**
     * @brief Register this subsystem's logger as a child of a parent logger
     * @param parentLogger Parent logger (typically RenderGraph main logger)
     */
    void RegisterToParentLogger(Logger* parentLogger);

    /**
     * @brief Deregister this subsystem's logger from its parent
     * @param parentLogger Parent logger to deregister from
     */
    void DeregisterFromParentLogger(Logger* parentLogger);

    /**
     * @brief Enable/disable logging for this subsystem
     * @param enabled True to enable logging
     */
    void SetLoggerEnabled(bool enabled);

    /**
     * @brief Enable/disable terminal output for this subsystem's logger
     * @param enabled True to print logs to console in real-time
     */
    void SetLoggerTerminalOutput(bool enabled);

protected:
    /**
     * @brief Initialize the logger with a subsystem name
     * @param subsystemName Name for this subsystem's logger (e.g., "Topology", "LifecycleHooks")
     * @param enabled Initial enabled state (default: false)
     *
     * Call this in the derived class constructor
     */
    void InitializeLogger(const std::string& subsystemName, bool enabled = false);

private:
    std::unique_ptr<Logger> logger;
};

} // namespace Vixen::RenderGraph
