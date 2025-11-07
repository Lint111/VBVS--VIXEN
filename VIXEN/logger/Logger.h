#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

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

    // Hierarchical logging
    void AddChild(Logger* child);
    void RemoveChild(Logger* child);
    const std::vector<Logger*>& GetChildren() const { return children; }

    // Logging methods
    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Critical(const std::string& message);

    // Extract logs recursively
    std::string ExtractLogs(int indentLevel = 0) const;

    // Clear logs
    void Clear();
    void ClearAll(); // Clear this logger and all children
    void ClearChildren(); // Clear child logger pointers without deleting entries

    // Getters
    const std::string& GetName() const { return name; }

protected:
    std::string name;
    bool enabled;
    bool terminalOutput = false;
    std::vector<Logger*> children; // Non-owning pointers to child loggers
    std::vector<std::string> logEntries;

    std::string GetTimestamp() const;
    std::string LogLevelToString(LogLevel level) const;
    std::string GetIndent(int level) const;
};
