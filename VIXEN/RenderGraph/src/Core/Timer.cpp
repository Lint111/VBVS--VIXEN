#include "Core/Timer.h"

namespace Vixen::RenderGraph {

Timer::Timer() {
    Reset();
}

double Timer::GetDeltaTime() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> delta = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    return delta.count();
}

double Timer::GetElapsedTime() const {
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = currentTime - startTime;
    return elapsed.count();
}

void Timer::Reset() {
    startTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = startTime;
}

} // namespace Vixen::RenderGraph
