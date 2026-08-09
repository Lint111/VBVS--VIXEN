#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>

namespace Vixen::Log {

enum class LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

class Logger {
public:
    explicit Logger(const std::string& name, bool enabled = false);
    virtual ~Logger();

    // Enable/disable logging
    void SetEnabled(bool enabled) { this->enabled = enabled; }
    bool IsEnabled() const { return enabled; }

    // Enable/disable terminal output (prints to console in addition to storing)
    void SetTerminalOutput(bool enable) { terminalOutput = enable; }
    bool HasTerminalOutput() const { return terminalOutput; }

    // Hierarchical logging (shared ownership model)
    void AddChild(std::shared_ptr<Logger> child);
    void RemoveChild(Logger* child);  // Remove by raw pointer (for backward compat)
    const std::vector<std::shared_ptr<Logger>>& GetChildren() const { return children; }

    // Logging methods
    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Critical(const std::string& message);

    // Process-wide minimum log level. Messages below this are dropped for ALL loggers (every
    // instance), so a consumer can silence chatty low-severity output without touching call sites.
    // Default LOG_DEBUG = no filtering (every level prints, the historical behaviour). Raise it (e.g.
    // to LOG_INFO) to drop per-frame DEBUG diagnostics.
    static void SetGlobalMinLevel(LogLevel level) { globalMinLevel = level; }
    static LogLevel GetGlobalMinLevel() { return globalMinLevel; }

    // Process-wide opt-in: when true, every instance prints to the terminal regardless of its
    // own SetTerminalOutput/SetEnabled state (still subject to SetGlobalMinLevel). Off by default.
    static void SetGlobalTerminalOutput(bool enable) { globalTerminalOutput = enable; }
    static bool GetGlobalTerminalOutput() { return globalTerminalOutput; }

    // VIXEN_NODE_LOG=<comma-list-of-names|all>: true if `name` is opted into logging by the env
    // knob. Exposed so every construction path (this ctor included) shares ONE parser instead of
    // each caller re-implementing the same comma-split (batch-3 review finding).
    static bool IsNodeLogEnvEnabled(const std::string& name);

    // Extract logs recursively
    std::string ExtractLogs(int indentLevel = 0) const;

    // Clear logs
    void Clear();
    void ClearAll(); // Clear this logger and all children
    void ClearChildren(); // Clear child logger pointers without deleting entries

    // Getters
    const std::string& GetName() const { return name; }

protected:
    static std::atomic<LogLevel> globalMinLevel;  // process-wide threshold; see SetGlobalMinLevel
    static std::atomic<bool> globalTerminalOutput;  // process-wide opt-in; see SetGlobalTerminalOutput
    std::string name;
    bool enabled;
    bool terminalOutput = false;
    // True when `enabled` came from VIXEN_NODE_LOG rather than the caller's own `enabled` arg.
    // The knob exists to make an otherwise-silent (SetEnabled never called) logger visible, so
    // Log() also treats it as satisfying the terminal-output gate — see Log().
    bool envForcedEnabled = false;
    std::vector<std::shared_ptr<Logger>> children; // Shared ownership of child loggers
    std::vector<std::string> logEntries;

    std::string GetTimestamp() const;
    std::string LogLevelToString(LogLevel level) const;
    std::string GetIndent(int level) const;
};

} // namespace Vixen::Log

// Backward compatibility: expose commonly-used types at global scope
using Vixen::Log::Logger;
using Vixen::Log::LogLevel;
