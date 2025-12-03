#include "Logger.h"
#include <iostream>

namespace Vixen::Log {

Logger::Logger(const std::string& name, bool enabled)
    : name(name), enabled(enabled)
{
}

Logger::~Logger()
{
}

void Logger::AddChild(std::shared_ptr<Logger> child)
{
    if (!child) {
        return;
    }
    children.push_back(child);
}

void Logger::RemoveChild(Logger *child)
{
    if(!child) {
        return;
    }
    // Remove by comparing raw pointers
    auto it = std::find_if(children.begin(), children.end(),
        [child](const std::shared_ptr<Logger>& ptr) {
            return ptr.get() == child;
        });
    if (it != children.end()) {
        children.erase(it);
    }
}

void Logger::Log(LogLevel level, const std::string& message)
{
    if (!enabled) {
        return;
    }

    std::ostringstream oss;
    oss << "[" << GetTimestamp() << "] "
        << "[" << name << "] "
        << "[" << LogLevelToString(level) << "] "
        << message;

    std::string logEntry = oss.str();
    logEntries.push_back(logEntry);

    // Print to terminal if enabled
    if (terminalOutput) {
        std::cout << logEntry << std::endl;
    }
}

void Logger::Debug(const std::string& message)
{
    Log(LogLevel::LOG_DEBUG, message);
}

void Logger::Info(const std::string& message)
{
    Log(LogLevel::LOG_INFO, message);
}

void Logger::Warning(const std::string& message)
{
    Log(LogLevel::LOG_WARNING, message);
}

void Logger::Error(const std::string& message)
{
    Log(LogLevel::LOG_ERROR, message);
}

void Logger::Critical(const std::string& message)
{
    Log(LogLevel::LOG_CRITICAL, message);
}

std::string Logger::ExtractLogs(int indentLevel) const
{
    std::ostringstream result;
    std::string indent = GetIndent(indentLevel);

    // Add separator for this logger
    result << indent << "=== Logger: " << name << " ===" << "\n";

    // Add all log entries for this logger
    for (const auto& entry : logEntries) {
        result << indent << entry << "\n";
    }

    // Recursively add children logs (shared_ptr guarantees validity)
    for (const auto& child : children) {
        if (child) {
            result << "\n";
            result << child->ExtractLogs(indentLevel + 1);
        }
    }

    return result.str();
}

void Logger::Clear()
{
    logEntries.clear();
}

void Logger::ClearAll()
{
    Clear();
    for (auto& child : children) {
        if (child) {
            child->ClearAll();
        }
    }
}

void Logger::ClearChildren()
{
    children.clear();
}

std::string Logger::GetTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::ostringstream oss;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

std::string Logger::LogLevelToString(LogLevel level) const
{
    switch (level) {
        case LogLevel::LOG_DEBUG:    return "DEBUG";
        case LogLevel::LOG_INFO:     return "INFO";
        case LogLevel::LOG_WARNING:  return "WARNING";
        case LogLevel::LOG_ERROR:    return "ERROR";
        case LogLevel::LOG_CRITICAL: return "CRITICAL";
        default:                     return "UNKNOWN";
    }
}

std::string Logger::GetIndent(int level) const
{
    return std::string(level * 2, ' ');
}

} // namespace Vixen::Log
