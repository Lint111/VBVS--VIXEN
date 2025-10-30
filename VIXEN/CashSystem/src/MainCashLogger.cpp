#include "CashSystem/MainCashLogger.h"
#include <sstream>
#include <iomanip>

namespace CashSystem {

MainCashLogger::MainCashLogger(const std::string& name, Logger* parentLogger)
    : debugMode(false) {
    Initialize(parentLogger);
    // Note: Logger name is set during creation; GetName() returns const reference
}

MainCashLogger::~MainCashLogger() {
    // Clean up sub-loggers (they are managed externally, so we don't delete them)
    ClearAllLogs();
}

void MainCashLogger::Initialize(Logger* parentLogger) {
    mainLogger = std::make_unique<Logger>("CashSystem", true);
    
    if (parentLogger) {
        parentLogger->AddChild(mainLogger.get());
    }
}

void MainCashLogger::AddSubLogger(const std::string& typeName, Logger* subLogger) {
    if (!subLogger) {
        // Create default sub-logger if none provided
        subLogger = CreateDefaultSubLogger(typeName);
    }
    
    subLoggers[typeName] = subLogger;
    
    // Attach to main logger hierarchy
    if (mainLogger) {
        mainLogger->AddChild(subLogger);
    }
}

void MainCashLogger::RemoveSubLogger(const std::string& typeName) {
    auto it = subLoggers.find(typeName);
    if (it != subLoggers.end()) {
        // Remove from hierarchy
        if (mainLogger) {
            mainLogger->RemoveChild(it->second);
        }
        subLoggers.erase(it);
    }
}

Logger* MainCashLogger::GetOrCreateSubLogger(const std::string& typeName) {
    auto it = subLoggers.find(typeName);
    if (it != subLoggers.end()) {
        return it->second;
    }
    
    // Create new sub-logger
    Logger* newLogger = CreateDefaultSubLogger(typeName);
    AddSubLogger(typeName, newLogger);
    return newLogger;
}

void MainCashLogger::Log(LogLevel level, const std::string& message) {
    if (mainLogger) {
        mainLogger->Log(level, message);
    }
}

void MainCashLogger::LogToSubLogger(const std::string& typeName, LogLevel level, const std::string& message) {
    Logger* subLogger = GetOrCreateSubLogger(typeName);
    subLogger->Log(level, message);
}

void MainCashLogger::SetDebugMode(bool enabled) {
    debugMode = enabled;
    
    // Propagate debug mode to all sub-loggers
    for (auto& pair : subLoggers) {
        if (pair.second) {
            pair.second->SetEnabled(enabled);
        }
    }
    
    // Log mode change
    if (enabled) {
        Log(LogLevel::LOG_INFO, "Debug mode enabled - real-time caching logs activated");
    } else {
        Log(LogLevel::LOG_INFO, "Debug mode disabled - caching logs suppressed");
    }
}

std::string MainCashLogger::ExtractAllLogs() const {
    std::ostringstream result;
    
    if (mainLogger) {
        result << "=== CASH SYSTEM LOGS ===" << std::endl;
        result << mainLogger->ExtractLogs(0);
        
        // Extract logs from sub-loggers
        for (const auto& pair : subLoggers) {
            if (pair.second) {
                result << std::endl << "=== " << pair.first << " SUB-LOGGER ===" << std::endl;
                result << pair.second->ExtractLogs(1);
            }
        }
    }
    
    return result.str();
}

void MainCashLogger::ClearAllLogs() {
    if (mainLogger) {
        mainLogger->ClearAll();
    }
    
    // Clear child loggers
    for (auto& pair : subLoggers) {
        if (pair.second) {
            pair.second->ClearAll();
        }
    }
}

std::vector<std::string> MainCashLogger::GetSubLoggerTypes() const {
    std::vector<std::string> types;
    types.reserve(subLoggers.size());
    
    for (const auto& pair : subLoggers) {
        types.push_back(pair.first);
    }
    
    return types;
}

Logger* MainCashLogger::CreateDefaultSubLogger(const std::string& typeName) {
    std::string loggerName = "CashSystem_" + typeName;
    return new Logger(loggerName, true);
}

std::string MainCashLogger::FormatDebugMessage(const std::string& operation,
                                              const std::string& cacheType,
                                              const std::string& key,
                                              const std::string& deviceName,
                                              const std::string& details) const {
    std::ostringstream oss;
    oss << "[" << operation << "] " << cacheType;
    
    if (!deviceName.empty()) {
        oss << " (Device: " << deviceName << ")";
    }
    
    oss << " - Key: " << key;
    
    if (!details.empty()) {
        oss << " | " << details;
    }
    
    return oss.str();
}

} // namespace CashSystem