#include "FrameRateLogger.h"
#include <sstream>
#include <iomanip>
#include <limits>

FrameRateLogger::FrameRateLogger(const std::string& name, bool enabled)
    : Logger(name, enabled),
      currentFPS(0.0),
      minFPS(std::numeric_limits<double>::max()),
      maxFPS(0.0),
      totalFrameTime(0.0),
      frameCount(0),
      isFirstFrame(true)
{
}

void FrameRateLogger::FrameStart()
{
    frameStartTime = std::chrono::high_resolution_clock::now();

    if (isFirstFrame) {
        lastFrameTime = frameStartTime;
        isFirstFrame = false;
    }
}

void FrameRateLogger::FrameEnd()
{
    if (!enabled) {
        return;
    }

    auto frameEndTime = std::chrono::high_resolution_clock::now();

    // Calculate frame time in seconds
    auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        frameEndTime - lastFrameTime
    );
    double frameTimeSeconds = frameDuration.count() / 1000000.0;

    if (frameTimeSeconds > 0.0) {
        currentFPS = 1.0 / frameTimeSeconds;

        // Update statistics
        if (currentFPS < minFPS) {
            minFPS = currentFPS;
        }
        if (currentFPS > maxFPS) {
            maxFPS = currentFPS;
        }

        totalFrameTime += frameTimeSeconds;
        frameCount++;

        // Log every 60 frames to avoid spam
        if (frameCount % 60 == 0) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << "FPS: " << currentFPS
                << " | Avg: " << GetAverageFPS()
                << " | Min: " << minFPS
                << " | Max: " << maxFPS
                << " | Frames: " << frameCount;
            Info(oss.str());
        }
    }

    lastFrameTime = frameEndTime;
}

double FrameRateLogger::GetAverageFPS() const
{
    if (frameCount == 0 || totalFrameTime == 0.0) {
        return 0.0;
    }
    return static_cast<double>(frameCount) / totalFrameTime;
}

double FrameRateLogger::GetMinFPS() const
{
    return minFPS == std::numeric_limits<double>::max() ? 0.0 : minFPS;
}

double FrameRateLogger::GetMaxFPS() const
{
    return maxFPS;
}

void FrameRateLogger::ResetStats()
{
    currentFPS = 0.0;
    minFPS = std::numeric_limits<double>::max();
    maxFPS = 0.0;
    totalFrameTime = 0.0;
    frameCount = 0;
    isFirstFrame = true;
    Clear();
}
