#include "Time/EngineTime.h"

namespace Vixen {
namespace Core {

EngineTime::EngineTime()
    : deltaTime(0.0f), elapsedTime(0.0f), frameCount(0)
{
    startTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = startTime;
}

void EngineTime::Update()
{
    auto currentTime = std::chrono::high_resolution_clock::now();

    // Calculate delta time
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime);
    deltaTime = duration.count() / 1000000.0f;  // Convert to seconds

    // Calculate elapsed time
    auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime);
    elapsedTime = totalDuration.count() / 1000000.0f;  // Convert to seconds

    lastFrameTime = currentTime;
    frameCount++;
}

void EngineTime::Reset()
{
    startTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = startTime;
    deltaTime = 0.0f;
    elapsedTime = 0.0f;
    frameCount = 0;
}
} // namespace Core
} // namespace Vixen
