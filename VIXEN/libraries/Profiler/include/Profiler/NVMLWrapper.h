#pragma once

#include <cstdint>
#include <string>

namespace Vixen::Profiler {

/// GPU utilization metrics from NVML
struct GPUUtilization {
    uint32_t gpuUtilization = 0;      // GPU compute utilization (0-100%)
    uint32_t memoryUtilization = 0;   // Memory controller utilization (0-100%)
    uint32_t temperature = 0;          // GPU temperature in Celsius
    uint32_t powerUsageW = 0;          // Power usage in watts
    uint32_t fanSpeedPercent = 0;      // Fan speed (0-100%)
    bool valid = false;                // True if data was successfully retrieved
};

/// Wrapper for NVIDIA Management Library (NVML) with runtime loading
/// Gracefully handles systems without NVIDIA GPUs or NVML installed
class NVMLWrapper {
public:
    NVMLWrapper();
    ~NVMLWrapper();

    // Non-copyable
    NVMLWrapper(const NVMLWrapper&) = delete;
    NVMLWrapper& operator=(const NVMLWrapper&) = delete;

    /// Initialize NVML (call once at startup)
    /// @return true if NVML is available and initialized
    bool Initialize();

    /// Shutdown NVML (call at cleanup)
    void Shutdown();

    /// Check if NVML is available and initialized
    bool IsAvailable() const { return initialized_; }

    /// Get number of NVIDIA GPUs detected
    uint32_t GetDeviceCount() const { return deviceCount_; }

    /// Get GPU name for device index
    /// @param deviceIndex GPU index (0-based)
    /// @return GPU name or empty string if unavailable
    std::string GetDeviceName(uint32_t deviceIndex = 0) const;

    /// Sample current GPU utilization
    /// @param deviceIndex GPU index (0-based)
    /// @return Utilization metrics (check .valid field)
    GPUUtilization GetUtilization(uint32_t deviceIndex = 0) const;

    /// Get singleton instance
    static NVMLWrapper& Instance();

private:
    bool LoadNVML();
    void UnloadNVML();

    bool initialized_ = false;
    uint32_t deviceCount_ = 0;
    void* nvmlLibrary_ = nullptr;  // HMODULE on Windows

    // Function pointers for NVML API
    void* fnInit_ = nullptr;
    void* fnShutdown_ = nullptr;
    void* fnDeviceGetCount_ = nullptr;
    void* fnDeviceGetHandleByIndex_ = nullptr;
    void* fnDeviceGetName_ = nullptr;
    void* fnDeviceGetUtilizationRates_ = nullptr;
    void* fnDeviceGetTemperature_ = nullptr;
    void* fnDeviceGetPowerUsage_ = nullptr;
    void* fnDeviceGetFanSpeed_ = nullptr;
};

} // namespace Vixen::Profiler
