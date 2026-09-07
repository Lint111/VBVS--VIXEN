#pragma once
#include "Timer.h"
#include "KernelDispatch/Abi.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Vixen::RenderGraph {

using LoopDomainMetadata = Vixen::KernelDispatch::LoopDomainMetadata;

/**
 * @brief Defines how a loop handles missed timesteps
 *
 * When frame time exceeds fixed timestep (e.g., 100ms frame with 16.6ms physics):
 * - FireAndForget: Execute once with accumulated dt (dt=100ms)
 * - SingleCorrectiveStep: Execute once with fixed dt (dt=16.6ms), log 83.4ms debt
 * - MultipleSteps: Execute 6 times with fixed dt (6 * 16.6ms = 99.6ms)
 */
enum class LoopCatchupMode : uint8_t {
    FireAndForget,        ///< Variable timestep (use accumulated time)
    SingleCorrectiveStep, ///< Fixed timestep, single update, track debt
    MultipleSteps         ///< Fixed timestep, multiple updates (default)
};

/**
 * @brief Shared state representing a loop's current execution status
 *
 * Passed as const pointer to all nodes connected to this loop.
 * Memory address remains stable (stored in LoopManager's map).
 */
struct LoopReference {
    uint32_t loopID = 0;
    bool shouldExecuteThisFrame = false;
    double deltaTime = 0.0;
    uint64_t stepCount = 0;
    uint64_t lastExecutedFrame = 0;
    double lastExecutionTimeMs = 0.0;
    LoopCatchupMode catchupMode = LoopCatchupMode::MultipleSteps;
    LoopDomainMetadata domain{};             ///< Scheduler-facing cadence/ownership contract.
    uint64_t stateFrameIndex = 0;            ///< Frame that produced this reference state.
};

/**
 * @brief Configuration for creating a new loop
 */
struct LoopConfig {
    double fixedTimestep;          ///< Update rate (1/60.0 for 60Hz, 0.0 for variable)
    std::string name;              ///< Human-readable name for logging
    LoopCatchupMode catchupMode = LoopCatchupMode::MultipleSteps;
    double maxCatchupTime = 0.25;  ///< Spiral of death protection (250ms)
    LoopDomainMetadata domain{};   ///< Stable scheduler metadata; period 1 is per-frame.
};

/**
 * @brief Manages multiple loops with independent update rates
 *
 * RenderGraph-owned system (like ShaderLibrary) that maintains loop state
 * using fixed timestep accumulator pattern.
 *
 * Lifecycle:
 * 1. Application calls graph->RegisterLoop() → returns loopID
 * 2. LoopBridgeNode created with loopID parameter
 * 3. RenderGraph calls UpdateLoops() once per frame
 * 4. LoopReferences updated based on accumulator state
 * 5. Nodes check LoopReference->shouldExecuteThisFrame
 *
 * The `domain` metadata is the hand-off to KernelDispatch::FramePipeline. `periodFrames == 1`
 * describes per-frame work (PhysicsLoop in the current 60Hz render base); `periodFrames == 2`
 * describes the 30Hz SimLoop. The manager remains the authoritative ordered cadence source. A
 * domain may pipeline only immutable snapshot work, and its result must be committed at a graph
 * frame boundary in submission order.
 *
 * Example:
 *   LoopManager manager;
 *   uint32_t physicsID = manager.RegisterLoop({1.0/60.0, "Physics"});
 *   manager.UpdateLoops(frameTime);  // Called by RenderGraph::RenderFrame
 *   const LoopReference* ref = manager.GetLoopReference(physicsID);
 *   if (ref->shouldExecuteThisFrame) { ... }
 */
class LoopManager {
public:
    LoopManager();
    ~LoopManager() = default;

    /**
     * @brief Register a new loop with the manager
     *
     * @param config Loop configuration (timestep, name, catch-up mode)
     * @return Unique loop ID (used by LoopBridgeNode)
     */
    uint32_t RegisterLoop(const LoopConfig& config);

    /**
     * @brief Get stable pointer to loop state
     *
     * Pointer remains valid for lifetime of LoopManager (stored in map).
     *
     * @param loopID Loop ID returned by RegisterLoop()
     * @return Const pointer to LoopReference, or nullptr if invalid ID
     */
    const LoopReference* GetLoopReference(uint32_t loopID);

    /**
     * @brief Update all loop states based on frame time
     *
     * Called once per frame by RenderGraph::RenderFrame().
     * Updates accumulators and sets shouldExecuteThisFrame flags.
     *
     * @param frameTime Time since last frame in seconds
     */
    void UpdateLoops(double frameTime);

    /**
     * @brief Set current frame index (for lastExecutedFrame tracking)
     *
     * @param frameIndex Global frame counter from RenderGraph
     */
    void SetCurrentFrame(uint64_t frameIndex);

private:
    /**
     * @brief Internal state for a single loop
     */
    struct LoopState {
        LoopConfig config;
        LoopReference reference;  // Stable memory address
        double accumulator = 0.0;
    };

    std::unordered_map<uint32_t, LoopState> loops;
    uint32_t nextLoopID = 0;
    uint64_t currentFrameIndex = 0;
};

} // namespace Vixen::RenderGraph
