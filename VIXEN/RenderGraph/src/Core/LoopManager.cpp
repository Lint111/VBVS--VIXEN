#include "Core/LoopManager.h"
#include "Core/NodeLogging.h"
#include <algorithm>

namespace Vixen::RenderGraph {

LoopManager::LoopManager() {
}

uint32_t LoopManager::RegisterLoop(const LoopConfig& config) {
    uint32_t id = nextLoopID++;

    LoopState state;
    state.config = config;
    state.reference.loopID = id;
    state.reference.shouldExecuteThisFrame = false;
    state.reference.deltaTime = 0.0;
    state.reference.stepCount = 0;
    state.reference.lastExecutedFrame = 0;
    state.reference.lastExecutionTimeMs = 0.0;
    state.reference.catchupMode = config.catchupMode;
    state.accumulator = 0.0;

    loops[id] = state;

    return id;
}

const LoopReference* LoopManager::GetLoopReference(uint32_t loopID) {
    auto it = loops.find(loopID);
    if (it != loops.end()) {
        return &it->second.reference;
    }
    return nullptr;
}

void LoopManager::UpdateLoops(double frameTime) {
    // Clamp frame time (spiral of death protection + minimum delta)
    if (frameTime <= 0.0) {
        frameTime = 0.001;  // 1ms minimum
    }

    for (auto& [id, state] : loops) {
        // Apply per-loop max catchup time
        double clampedFrameTime = frameTime;
        if (clampedFrameTime > state.config.maxCatchupTime) {
            clampedFrameTime = state.config.maxCatchupTime;
#if VIXEN_DEBUG_BUILD
            // Log warning if frame time exceeded budget
            // Note: nodeLogger not available here (not a node), use conditional compilation
            // Future: Add LoopManager-specific logger
#endif
        }

        if (state.config.fixedTimestep == 0.0) {
            // Variable rate loop - always execute
            state.reference.shouldExecuteThisFrame = true;
            state.reference.deltaTime = clampedFrameTime;
            state.reference.lastExecutedFrame = currentFrameIndex;
        } else {
            // Fixed timestep accumulator
            state.accumulator += clampedFrameTime;

            switch (state.config.catchupMode) {
                case LoopCatchupMode::FireAndForget: {
                    // Execute once with accumulated time
                    if (state.accumulator >= state.config.fixedTimestep) {
                        state.reference.shouldExecuteThisFrame = true;
                        state.reference.deltaTime = state.accumulator;
                        state.reference.stepCount++;
                        state.reference.lastExecutedFrame = currentFrameIndex;
                        state.accumulator = 0.0;
                    } else {
                        state.reference.shouldExecuteThisFrame = false;
                    }
                    break;
                }

                case LoopCatchupMode::SingleCorrectiveStep: {
                    // Execute once with fixed dt, track debt
                    if (state.accumulator >= state.config.fixedTimestep) {
                        state.reference.shouldExecuteThisFrame = true;
                        state.reference.deltaTime = state.config.fixedTimestep;
                        state.reference.stepCount++;
                        state.reference.lastExecutedFrame = currentFrameIndex;
                        state.accumulator -= state.config.fixedTimestep;

                        // Log debt if significant
                        if (state.accumulator > state.config.fixedTimestep) {
#if VIXEN_DEBUG_BUILD
                            // Future: Log accumulated debt
#endif
                        }
                    } else {
                        state.reference.shouldExecuteThisFrame = false;
                    }
                    break;
                }

                case LoopCatchupMode::MultipleSteps:
                default: {
                    // Execute multiple times if needed
                    // Note: RenderGraph will call Execute() multiple times if shouldExecuteThisFrame remains true
                    // For now, just trigger once per UpdateLoops() call
                    if (state.accumulator >= state.config.fixedTimestep) {
                        state.reference.shouldExecuteThisFrame = true;
                        state.reference.deltaTime = state.config.fixedTimestep;
                        state.reference.stepCount++;
                        state.reference.lastExecutedFrame = currentFrameIndex;
                        state.accumulator -= state.config.fixedTimestep;
                    } else {
                        state.reference.shouldExecuteThisFrame = false;
                    }
                    break;
                }
            }
        }
    }
}

void LoopManager::SetCurrentFrame(uint64_t frameIndex) {
    currentFrameIndex = frameIndex;
}

} // namespace Vixen::RenderGraph
