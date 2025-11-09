#pragma once

#include <memory>
#include <string>
#include "Logger.h"

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
 *         LOG_DEBUG("Adding edge...");
 *     }
 * };
 * @endcode
 */

/**
 * @brief Logging macros for ILoggable-derived classes
 *
 * These macros automatically check if logger exists before calling.
 * Use these instead of manual GetLogger() checks for cleaner code.
 *
 * Available macros:
 * - LOG_TRACE(msg)   - Trace-level logging
 * - LOG_DEBUG(msg)   - Debug-level logging
 * - LOG_INFO(msg)    - Info-level logging
 * - LOG_WARNING(msg) - Warning-level logging
 * - LOG_ERROR(msg)   - Error-level logging
 */
#define LOG_TRACE(msg)   do { if (auto* log = GetLogger()) { log->Trace(msg); } } while(0)
#define LOG_DEBUG(msg)   do { if (auto* log = GetLogger()) { log->Debug(msg); } } while(0)
#define LOG_INFO(msg)    do { if (auto* log = GetLogger()) { log->Info(msg); } } while(0)
#define LOG_WARNING(msg) do { if (auto* log = GetLogger()) { log->Warning(msg); } } while(0)
#define LOG_ERROR(msg)   do { if (auto* log = GetLogger()) { log->Error(msg); } } while(0)

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
