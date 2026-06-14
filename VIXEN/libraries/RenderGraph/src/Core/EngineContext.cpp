#include "Core/EngineContext.h"

#include "Core/NodeTypeRegistry.h"
#include "Core/RenderGraph.h"
#include "Core/CalibrationStore.h"
#include "MessageBus.h"
#include "MainCacher.h"

namespace Vixen::RenderGraph {

EngineContext::EngineContext(const EngineConfig& config)
    : logger_(config.logger),
      mainCacher_(config.mainCacher ? config.mainCacher
                                    : &CashSystem::MainCacher::Instance())
{
    // The order below is load-bearing (mirrors the former VulkanGraphApplication::Initialize):
    //   registry (+ caller node-type registration) -> bus -> MainCacher.Initialize(bus)
    //   -> graph(registry, bus, logger, cacher) -> calibrationStore(registry-from-graph, bus).
    registry_ = std::make_unique<NodeTypeRegistry>();
    if (config.registerNodeTypes) {
        config.registerNodeTypes(*registry_);
    }

    bus_ = std::make_unique<Vixen::EventBus::MessageBus>();
    mainCacher_->Initialize(bus_.get());

    graph_ = std::make_unique<RenderGraph>(registry_.get(), bus_.get(), logger_, mainCacher_);

    if (config.enableCalibration) {
        // CalibrationStore is autonomous: it subscribes to the bus and persists on
        // ApplicationShuttingDownEvent. Arg 2 must come from the graph (its TaskProfileRegistry),
        // hence construction after the graph.
        calibration_ = std::make_unique<CalibrationStore>(
            config.calibrationDir, graph_->GetTaskProfileRegistry(), bus_.get());
    }
}

// Defined here (not =default in the header) so the unique_ptr members can be destroyed with the
// complete types in scope. Member destruction runs in reverse declaration order.
EngineContext::~EngineContext() = default;

NodeTypeRegistry& EngineContext::Registry() { return *registry_; }
Vixen::EventBus::MessageBus& EngineContext::Bus() { return *bus_; }
RenderGraph& EngineContext::Graph() { return *graph_; }
CalibrationStore* EngineContext::Calibration() { return calibration_.get(); }

} // namespace Vixen::RenderGraph
