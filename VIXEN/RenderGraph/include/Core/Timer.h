#pragma once
#include <chrono>

namespace Vixen::RenderGraph {

/**
 * @brief High-resolution timer for delta time measurement
 *
 * Provides accurate time tracking using std::chrono::high_resolution_clock.
 * Used by LoopManager for fixed timestep accumulation.
 *
 * Usage:
 *   Timer timer;
 *   // ... work ...
 *   double dt = timer.GetDeltaTime();  // Time since last GetDeltaTime() call
 */
class Timer {
public:
    /**
     * @brief Construct timer and start timing
     */
    Timer();

    /**
     * @brief Get time elapsed since last call to GetDeltaTime()
     *
     * First call after construction or Reset() returns time since timer start.
     * Subsequent calls return time since previous GetDeltaTime() call.
     *
     * @return Elapsed time in seconds
     */
    double GetDeltaTime();

    /**
     * @brief Get total elapsed time since timer creation or last Reset()
     *
     * Does not affect GetDeltaTime() measurement.
     *
     * @return Total elapsed time in seconds
     */
    double GetElapsedTime() const;

    /**
     * @brief Reset timer to current time
     *
     * Sets both startTime and lastFrameTime to now.
     * Next GetDeltaTime() call will measure from this point.
     */
    void Reset();

private:
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;
};

} // namespace Vixen::RenderGraph
