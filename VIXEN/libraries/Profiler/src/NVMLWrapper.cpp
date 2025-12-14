#include "Profiler/NVMLWrapper.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Vixen::Profiler {

// NVML type definitions (from nvml.h)
typedef void* nvmlDevice_t;

typedef enum {
    NVML_SUCCESS = 0,
    NVML_ERROR_UNINITIALIZED = 1,
    NVML_ERROR_INVALID_ARGUMENT = 2,
    NVML_ERROR_NOT_SUPPORTED = 3,
    NVML_ERROR_NO_PERMISSION = 4,
    NVML_ERROR_ALREADY_INITIALIZED = 5,
    NVML_ERROR_NOT_FOUND = 6,
    NVML_ERROR_INSUFFICIENT_SIZE = 7,
    NVML_ERROR_INSUFFICIENT_POWER = 8,
    NVML_ERROR_DRIVER_NOT_LOADED = 9,
    NVML_ERROR_TIMEOUT = 10,
    NVML_ERROR_IRQ_ISSUE = 11,
    NVML_ERROR_LIBRARY_NOT_FOUND = 12,
    NVML_ERROR_FUNCTION_NOT_FOUND = 13,
    NVML_ERROR_CORRUPTED_INFOROM = 14,
    NVML_ERROR_GPU_IS_LOST = 15,
    NVML_ERROR_UNKNOWN = 999
} nvmlReturn_t;

typedef enum {
    NVML_TEMPERATURE_GPU = 0
} nvmlTemperatureSensors_t;

typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;

// Function pointer types
typedef nvmlReturn_t (*nvmlInit_t)(void);
typedef nvmlReturn_t (*nvmlShutdown_t)(void);
typedef nvmlReturn_t (*nvmlDeviceGetCount_t)(unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_t)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t (*nvmlDeviceGetName_t)(nvmlDevice_t, char*, unsigned int);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t, nvmlUtilization_t*);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_t)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_t)(nvmlDevice_t, unsigned int*);

NVMLWrapper::NVMLWrapper() = default;

NVMLWrapper::~NVMLWrapper() {
    Shutdown();
}

NVMLWrapper& NVMLWrapper::Instance() {
    static NVMLWrapper instance;
    return instance;
}

bool NVMLWrapper::LoadNVML() {
#ifdef _WIN32
    nvmlLibrary_ = LoadLibraryA("nvml.dll");
    if (!nvmlLibrary_) {
        return false;
    }

    fnInit_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlInit_v2"));
    fnShutdown_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlShutdown"));
    fnDeviceGetCount_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetCount_v2"));
    fnDeviceGetHandleByIndex_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetHandleByIndex_v2"));
    fnDeviceGetName_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetName"));
    fnDeviceGetUtilizationRates_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetUtilizationRates"));
    fnDeviceGetTemperature_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetTemperature"));
    fnDeviceGetPowerUsage_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetPowerUsage"));
    fnDeviceGetFanSpeed_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(nvmlLibrary_), "nvmlDeviceGetFanSpeed"));
#else
    nvmlLibrary_ = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!nvmlLibrary_) {
        nvmlLibrary_ = dlopen("libnvidia-ml.so", RTLD_LAZY);
    }
    if (!nvmlLibrary_) {
        return false;
    }

    fnInit_ = dlsym(nvmlLibrary_, "nvmlInit_v2");
    fnShutdown_ = dlsym(nvmlLibrary_, "nvmlShutdown");
    fnDeviceGetCount_ = dlsym(nvmlLibrary_, "nvmlDeviceGetCount_v2");
    fnDeviceGetHandleByIndex_ = dlsym(nvmlLibrary_, "nvmlDeviceGetHandleByIndex_v2");
    fnDeviceGetName_ = dlsym(nvmlLibrary_, "nvmlDeviceGetName");
    fnDeviceGetUtilizationRates_ = dlsym(nvmlLibrary_, "nvmlDeviceGetUtilizationRates");
    fnDeviceGetTemperature_ = dlsym(nvmlLibrary_, "nvmlDeviceGetTemperature");
    fnDeviceGetPowerUsage_ = dlsym(nvmlLibrary_, "nvmlDeviceGetPowerUsage");
    fnDeviceGetFanSpeed_ = dlsym(nvmlLibrary_, "nvmlDeviceGetFanSpeed");
#endif

    // Check required functions are available
    return fnInit_ && fnShutdown_ && fnDeviceGetCount_ &&
           fnDeviceGetHandleByIndex_ && fnDeviceGetUtilizationRates_;
}

void NVMLWrapper::UnloadNVML() {
    if (nvmlLibrary_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(nvmlLibrary_));
#else
        dlclose(nvmlLibrary_);
#endif
        nvmlLibrary_ = nullptr;
    }

    fnInit_ = nullptr;
    fnShutdown_ = nullptr;
    fnDeviceGetCount_ = nullptr;
    fnDeviceGetHandleByIndex_ = nullptr;
    fnDeviceGetName_ = nullptr;
    fnDeviceGetUtilizationRates_ = nullptr;
    fnDeviceGetTemperature_ = nullptr;
    fnDeviceGetPowerUsage_ = nullptr;
    fnDeviceGetFanSpeed_ = nullptr;
}

bool NVMLWrapper::Initialize() {
    if (initialized_) {
        return true;
    }

    if (!LoadNVML()) {
        return false;
    }

    auto nvmlInit = reinterpret_cast<nvmlInit_t>(fnInit_);
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        UnloadNVML();
        return false;
    }

    // Get device count
    auto nvmlDeviceGetCount = reinterpret_cast<nvmlDeviceGetCount_t>(fnDeviceGetCount_);
    unsigned int count = 0;
    result = nvmlDeviceGetCount(&count);
    if (result == NVML_SUCCESS) {
        deviceCount_ = count;
    }

    initialized_ = true;
    return true;
}

void NVMLWrapper::Shutdown() {
    if (initialized_ && fnShutdown_) {
        auto nvmlShutdown = reinterpret_cast<nvmlShutdown_t>(fnShutdown_);
        nvmlShutdown();
    }
    initialized_ = false;
    deviceCount_ = 0;
    UnloadNVML();
}

std::string NVMLWrapper::GetDeviceName(uint32_t deviceIndex) const {
    if (!initialized_ || !fnDeviceGetHandleByIndex_ || !fnDeviceGetName_) {
        return "";
    }

    auto nvmlDeviceGetHandleByIndex = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(fnDeviceGetHandleByIndex_);
    auto nvmlDeviceGetName = reinterpret_cast<nvmlDeviceGetName_t>(fnDeviceGetName_);

    nvmlDevice_t device;
    nvmlReturn_t result = nvmlDeviceGetHandleByIndex(deviceIndex, &device);
    if (result != NVML_SUCCESS) {
        return "";
    }

    char name[256];
    result = nvmlDeviceGetName(device, name, sizeof(name));
    if (result != NVML_SUCCESS) {
        return "";
    }

    return std::string(name);
}

GPUUtilization NVMLWrapper::GetUtilization(uint32_t deviceIndex) const {
    GPUUtilization util;

    if (!initialized_ || !fnDeviceGetHandleByIndex_) {
        return util;
    }

    auto nvmlDeviceGetHandleByIndex = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(fnDeviceGetHandleByIndex_);

    nvmlDevice_t device;
    nvmlReturn_t result = nvmlDeviceGetHandleByIndex(deviceIndex, &device);
    if (result != NVML_SUCCESS) {
        return util;
    }

    // Get utilization rates
    if (fnDeviceGetUtilizationRates_) {
        auto nvmlDeviceGetUtilizationRates = reinterpret_cast<nvmlDeviceGetUtilizationRates_t>(fnDeviceGetUtilizationRates_);
        nvmlUtilization_t utilization;
        result = nvmlDeviceGetUtilizationRates(device, &utilization);
        if (result == NVML_SUCCESS) {
            util.gpuUtilization = utilization.gpu;
            util.memoryUtilization = utilization.memory;
            util.valid = true;
        }
    }

    // Get temperature
    if (fnDeviceGetTemperature_) {
        auto nvmlDeviceGetTemperature = reinterpret_cast<nvmlDeviceGetTemperature_t>(fnDeviceGetTemperature_);
        unsigned int temp = 0;
        result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
        if (result == NVML_SUCCESS) {
            util.temperature = temp;
        }
    }

    // Get power usage (returned in milliwatts)
    if (fnDeviceGetPowerUsage_) {
        auto nvmlDeviceGetPowerUsage = reinterpret_cast<nvmlDeviceGetPowerUsage_t>(fnDeviceGetPowerUsage_);
        unsigned int power = 0;
        result = nvmlDeviceGetPowerUsage(device, &power);
        if (result == NVML_SUCCESS) {
            util.powerUsageW = power / 1000;  // Convert to watts
        }
    }

    // Get fan speed
    if (fnDeviceGetFanSpeed_) {
        auto nvmlDeviceGetFanSpeed = reinterpret_cast<nvmlDeviceGetFanSpeed_t>(fnDeviceGetFanSpeed_);
        unsigned int fanSpeed = 0;
        result = nvmlDeviceGetFanSpeed(device, &fanSpeed);
        if (result == NVML_SUCCESS) {
            util.fanSpeedPercent = fanSpeed;
        }
    }

    return util;
}

} // namespace Vixen::Profiler
