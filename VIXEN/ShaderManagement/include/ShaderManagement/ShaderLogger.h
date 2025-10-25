#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <atomic>

namespace ShaderManagement {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    Debug = 0,    ///< Detailed debug information
    Info = 1,     ///< General informational messages
    Warning = 2,  ///< Warning messages
    Error = 3,    ///< Error messages
    None = 4      ///< Disable all logging
};

/**
 * @brief Convert log level to string
 */
inline const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::None:    return "NONE";
        default:                return "UNKNOWN";
    }
}

/**
 * @brief Telemetry metrics for shader compilation pipeline
 */
struct ShaderTelemetry {
    // Compilation metrics
    std::atomic<uint64_t> totalCompilations{0};
    std::atomic<uint64_t> successfulCompilations{0};
    std::atomic<uint64_t> failedCompilations{0};

    // Timing metrics (in microseconds)
    std::atomic<uint64_t> totalCompileTimeUs{0};
    std::atomic<uint64_t> totalReflectTimeUs{0};
    std::atomic<uint64_t> totalSdiGenTimeUs{0};

    // Size metrics (in bytes)
    std::atomic<uint64_t> totalSourceSizeBytes{0};
    std::atomic<uint64_t> totalSpirvSizeBytes{0};

    // Cache metrics
    std::atomic<uint64_t> cacheHits{0};
    std::atomic<uint64_t> cacheMisses{0};

    /**
     * @brief Reset all metrics to zero
     */
    void Reset() {
        totalCompilations.store(0);
        successfulCompilations.store(0);
        failedCompilations.store(0);
        totalCompileTimeUs.store(0);
        totalReflectTimeUs.store(0);
        totalSdiGenTimeUs.store(0);
        totalSourceSizeBytes.store(0);
        totalSpirvSizeBytes.store(0);
        cacheHits.store(0);
        cacheMisses.store(0);
    }

    /**
     * @brief Get average compilation time in milliseconds
     */
    double GetAverageCompileTimeMs() const {
        uint64_t count = successfulCompilations.load();
        if (count == 0) return 0.0;
        return (totalCompileTimeUs.load() / static_cast<double>(count)) / 1000.0;
    }

    /**
     * @brief Get cache hit rate (0.0 to 1.0)
     */
    double GetCacheHitRate() const {
        uint64_t total = cacheHits.load() + cacheMisses.load();
        if (total == 0) return 0.0;
        return cacheHits.load() / static_cast<double>(total);
    }

    /**
     * @brief Get success rate (0.0 to 1.0)
     */
    double GetSuccessRate() const {
        uint64_t total = totalCompilations.load();
        if (total == 0) return 0.0;
        return successfulCompilations.load() / static_cast<double>(total);
    }
};

/**
 * @brief Log message structure
 */
struct LogMessage {
    LogLevel level;
    std::string message;
    std::string category;  ///< Optional category (e.g., "Compiler", "Reflector", "SDI")
    std::chrono::system_clock::time_point timestamp;

    LogMessage(LogLevel lvl, std::string msg, std::string cat = "")
        : level(lvl)
        , message(std::move(msg))
        , category(std::move(cat))
        , timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Callback function type for log messages
 *
 * Users can provide their own logging implementation via this callback.
 * The callback should be thread-safe as it may be called from multiple threads.
 */
using LogCallback = std::function<void(const LogMessage&)>;

/**
 * @brief Global shader logger
 *
 * Provides structured logging and telemetry for the shader compilation pipeline.
 * Thread-safe singleton accessible throughout the library.
 *
 * Usage:
 * @code
 * // Set custom logger
 * ShaderLogger::GetInstance().SetCallback([](const LogMessage& msg) {
 *     MyLogger::Log(msg.level, msg.category, msg.message);
 * });
 *
 * // Set minimum log level
 * ShaderLogger::GetInstance().SetMinimumLevel(LogLevel::Warning);
 *
 * // Log messages
 * ShaderLogger::Log(LogLevel::Info, "Compilation started", "Compiler");
 * ShaderLogger::LogError("Compilation failed: {}", errorMsg);
 *
 * // Access telemetry
 * auto& telemetry = ShaderLogger::GetTelemetry();
 * std::cout << "Cache hit rate: " << telemetry.GetCacheHitRate() << "\n";
 * @endcode
 */
class ShaderLogger {
public:
    /**
     * @brief Get singleton instance
     */
    static ShaderLogger& GetInstance() {
        static ShaderLogger instance;
        return instance;
    }

    /**
     * @brief Set log callback
     *
     * If not set, logs are printed to stderr by default.
     *
     * @param callback Function to handle log messages (must be thread-safe)
     */
    void SetCallback(LogCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    /**
     * @brief Set minimum log level
     *
     * Messages below this level will be filtered out.
     *
     * @param level Minimum level to log
     */
    void SetMinimumLevel(LogLevel level) {
        minLevel_.store(static_cast<int>(level));
    }

    /**
     * @brief Get current minimum log level
     */
    LogLevel GetMinimumLevel() const {
        return static_cast<LogLevel>(minLevel_.load());
    }

    /**
     * @brief Log a message
     *
     * @param level Log severity level
     * @param message Message to log
     * @param category Optional category (e.g., "Compiler", "Reflector")
     */
    void Log(LogLevel level, const std::string& message, const std::string& category = "") {
        // Filter by level
        if (static_cast<int>(level) < minLevel_.load()) {
            return;
        }

        LogMessage msg(level, message, category);

        std::lock_guard<std::mutex> lock(mutex_);

        if (callback_) {
            callback_(msg);
        } else {
            // Default: print to stderr
            DefaultLog(msg);
        }
    }

    /**
     * @brief Get telemetry data
     */
    static ShaderTelemetry& GetTelemetry() {
        static ShaderTelemetry telemetry;
        return telemetry;
    }

    // Convenience methods

    static void LogDebug(const std::string& message, const std::string& category = "") {
        GetInstance().Log(LogLevel::Debug, message, category);
    }

    static void LogInfo(const std::string& message, const std::string& category = "") {
        GetInstance().Log(LogLevel::Info, message, category);
    }

    static void LogWarning(const std::string& message, const std::string& category = "") {
        GetInstance().Log(LogLevel::Warning, message, category);
    }

    static void LogError(const std::string& message, const std::string& category = "") {
        GetInstance().Log(LogLevel::Error, message, category);
    }

private:
    ShaderLogger() : minLevel_(static_cast<int>(LogLevel::Info)) {}

    // Non-copyable, non-movable (singleton)
    ShaderLogger(const ShaderLogger&) = delete;
    ShaderLogger& operator=(const ShaderLogger&) = delete;

    void DefaultLog(const LogMessage& msg) {
        // Default logger: print to stderr with timestamp
        auto time = std::chrono::system_clock::to_time_t(msg.timestamp);
        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&time));

        fprintf(stderr, "[%s] [%s]", timeStr, LogLevelToString(msg.level));

        if (!msg.category.empty()) {
            fprintf(stderr, " [%s]", msg.category.c_str());
        }

        fprintf(stderr, " %s\n", msg.message.c_str());
    }

    std::mutex mutex_;
    LogCallback callback_;
    std::atomic<int> minLevel_;
};

/**
 * @brief RAII helper to track operation duration and update telemetry
 *
 * Usage:
 * @code
 * {
 *     ScopedTelemetryTimer timer(ShaderLogger::GetTelemetry().totalCompileTimeUs);
 *     // ... perform compilation ...
 * }  // Timer automatically updates totalCompileTimeUs on destruction
 * @endcode
 */
class ScopedTelemetryTimer {
public:
    explicit ScopedTelemetryTimer(std::atomic<uint64_t>& counter)
        : counter_(counter)
        , start_(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTelemetryTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        counter_.fetch_add(duration.count());
    }

private:
    std::atomic<uint64_t>& counter_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace ShaderManagement
