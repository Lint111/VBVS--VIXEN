#pragma once

#include "Logger.h"
#include <chrono>

class FrameRateLogger : public Logger {
public:
    explicit FrameRateLogger(const std::string& name, bool enabled = true);

    // Frame tracking
    void FrameStart();
    void FrameEnd();

    // Get statistics
    double GetAverageFPS() const;
    double GetMinFPS() const;
    double GetMaxFPS() const;
    double GetCurrentFPS() const { return currentFPS; }

    // Reset statistics
    void ResetStats();

private:
    std::chrono::high_resolution_clock::time_point frameStartTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;

    double currentFPS;
    double minFPS;
    double maxFPS;
    double totalFrameTime;
    uint64_t frameCount;

    bool isFirstFrame;
};
