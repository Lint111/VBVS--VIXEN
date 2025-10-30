#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "Logger.h"

namespace CashSystem {

/**
 * @class MainCashLogger
 * @brief Main logging controller for CashSystem that manages sub-loggers for each registered sub-cacher.
 * 
 * Integrates with the existing hierarchical Logger system and provides:
 * - Main logger for CashSystem-wide messages
 * - Sub-loggers for each registered sub-cacher type
 * - Real-time debug logging when debug mode is enabled
 * - Hierarchical log collection and management
 */
class MainCashLogger {
public:
    explicit MainCashLogger(const std::string& name = "CashSystem", Logger* parentLogger = nullptr);
    ~MainCashLogger();

    /**
     * @brief Initialize the main logger and optionally attach to a parent logger
     * @param parentLogger Parent logger in the hierarchy (can be nullptr)
     */
    void Initialize(Logger* parentLogger = nullptr);

    /**
     * @brief Add a sub-logger for a specific sub-cacher type
     * @param typeName Name of the sub-cacher type
     * @param subLogger Pointer to the sub-logger
     */
    void AddSubLogger(const std::string& typeName, Logger* subLogger);

    /**
     * @brief Remove a sub-logger for a specific type
     * @param typeName Name of the sub-cacher type
     */
    void RemoveSubLogger(const std::string& typeName);

    /**
     * @brief Get or create a sub-logger for a specific type
     * @param typeName Name of the sub-cacher type
     * @return Pointer to the sub-logger (creates one if it doesn't exist)
     */
    Logger* GetOrCreateSubLogger(const std::string& typeName);

    /**
     * @brief Log a message to the main logger
     * @param level Log level
     * @param message Message to log
     */
    void Log(LogLevel level, const std::string& message);

    /**
     * @brief Log a message to a specific sub-logger
     * @param typeName Name of the sub-cacher type
     * @param level Log level
     * @param message Message to log
     */
    void LogToSubLogger(const std::string& typeName, LogLevel level, const std::string& message);

    /**
     * @brief Enable or disable debug mode for real-time caching logs
     * @param enabled True to enable debug logging, false to disable
     */
    void SetDebugMode(bool enabled);

    /**
     * @brief Check if debug mode is enabled
     * @return True if debug mode is enabled
     */
    bool IsDebugMode() const { return debugMode; }

    /**
     * @brief Get the main logger
     * @return Pointer to the main logger
     */
    Logger* GetMainLogger() { return mainLogger.get(); }

    /**
     * @brief Get all logs from main and sub-loggers
     * @return Combined log string with hierarchical formatting
     */
    std::string ExtractAllLogs() const;

    /**
     * @brief Clear all logs from main and sub-loggers
     */
    void ClearAllLogs();

    /**
     * @brief Get statistics about registered sub-loggers
     * @return Number of registered sub-loggers
     */
    size_t GetSubLoggerCount() const { return subLoggers.size(); }

    /**
     * @brief Get list of all registered sub-logger type names
     * @return Vector of type names
     */
    std::vector<std::string> GetSubLoggerTypes() const;

private:
    std::unique_ptr<Logger> mainLogger;
    std::unordered_map<std::string, Logger*> subLoggers;
    bool debugMode;
    
    /**
     * @brief Create a default sub-logger for a given type
     * @param typeName Name of the sub-cacher type
     * @return Pointer to the created logger
     */
    Logger* CreateDefaultSubLogger(const std::string& typeName);

    /**
     * @brief Format debug message with cache operation details
     * @param operation Operation type (CACHE, HIT, MISS, EVICT, etc.)
     * @param cacheType Type of cache
     * @param key Cache key
     * @param deviceName Device name (optional)
     * @param details Additional details
     * @return Formatted debug message
     */
    std::string FormatDebugMessage(const std::string& operation, 
                                  const std::string& cacheType,
                                  const std::string& key,
                                  const std::string& deviceName = "",
                                  const std::string& details = "") const;
};

} // namespace CashSystem