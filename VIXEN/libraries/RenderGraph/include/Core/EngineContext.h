#pragma once

#include <memory>

#include "Core/EngineConfig.h"

namespace Vixen::Log { class Logger; }  // exposed globally as ::Logger via a using-decl in Logger.h
namespace CashSystem { class MainCacher; }
namespace Vixen::EventBus { class MessageBus; }

namespace Vixen::RenderGraph {

class NodeTypeRegistry;
class RenderGraph;
class CalibrationStore;

/**
 * @brief Instantiable owner of VIXEN's core graph subsystems (AR#7).
 *
 * Replaces the aggregate the former VulkanGraphApplication singleton held: a host constructs an
 * EngineContext (no global state) and it stands up — in the one valid order — the NodeTypeRegistry,
 * MessageBus, RenderGraph, and (optionally) the autonomous CalibrationStore, wiring the shared
 * MainCacher. The graph creates its OWN Vulkan instance/device via in-graph nodes (InstanceNode ->
 * DeviceNode), so EngineContext needs no device injected. Both the application and BenchmarkRunner
 * can build on it (BenchmarkRunner is the factoring reference).
 *
 * Teardown is deterministic via member declaration order: the graph is destroyed (node cleanup,
 * which may publish/register) while the bus and registry are still alive. Publish an
 * ApplicationShuttingDownEvent on Bus() before destroying the context so the CalibrationStore
 * persists on the way down.
 */
class EngineContext {
public:
    explicit EngineContext(const EngineConfig& config);
    ~EngineContext();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;

    NodeTypeRegistry& Registry();
    Vixen::EventBus::MessageBus& Bus();
    RenderGraph& Graph();
    CalibrationStore* Calibration();  ///< null when enableCalibration was false.

private:
    // Non-owning.
    Vixen::Log::Logger* logger_;

    // AR#8: when the host injects no cacher, EngineContext OWNS one — no process-wide singleton.
    // Declared before the owned graph so it outlives the graph's node cleanup; the destructor
    // severs its bus subscription (MainCacher::Shutdown) before bus_ is destroyed.
    std::unique_ptr<CashSystem::MainCacher> ownedCacher_;
    CashSystem::MainCacher* mainCacher_ = nullptr;  // view: -> ownedCacher_ or config.mainCacher

    // Owned. Declaration order IS the construction order; destruction is the reverse
    // (calibration_ -> graph_ -> bus_ -> registry_ -> ownedCacher_), which keeps bus_/registry_ and
    // the cacher alive through the graph's node cleanup.
    std::unique_ptr<NodeTypeRegistry> registry_;
    std::unique_ptr<Vixen::EventBus::MessageBus> bus_;
    std::unique_ptr<RenderGraph> graph_;
    std::unique_ptr<CalibrationStore> calibration_;
};

} // namespace Vixen::RenderGraph
