#pragma once

#include "Headers.h"

namespace Vixen {
namespace Core {

class EngineTime {
public:
    EngineTime();

    // Update time values - call once per frame
    void Update();

    // Get delta time in seconds
    float GetDeltaTime() const { return deltaTime; }

    // Get total elapsed time since start in seconds
    float GetElapsedTime() const { return elapsedTime; }

    // Get frame count
    uint64_t GetFrameCount() const { return frameCount; }

    // Reset timer
    void Reset();

private:
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;

    float deltaTime;      // Time since last frame (seconds)
    float elapsedTime;    // Total time since start (seconds)
    uint64_t frameCount;  // Total frames rendered
};
} // namespace Core
} // namespace Vixen

