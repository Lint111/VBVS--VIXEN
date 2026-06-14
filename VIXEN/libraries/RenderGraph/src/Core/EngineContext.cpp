#include "Core/EngineContext.h"

#include "Core/NodeTypeRegistry.h"
#include "Core/RenderGraph.h"
#include "Core/CalibrationStore.h"
#include "MessageBus.h"
#include "MainCacher.h"

namespace Vixen::RenderGraph {

EngineContext::EngineContext(const EngineConfig& config)
    : logger_(config.logger)
{
    // AR#8: own a MainCacher when the host supplies none — no process-wide singleton fallback.
    // This is what lets multiple EngineContexts coexist without sharing cache state.
    if (config.mainCacher) {
        mainCacher_ = config.mainCacher;  // non-owning; the host manages its lifetime
    } else {
        ownedCacher_ = std::make_unique<CashSystem::MainCacher>();
        mainCacher_ = ownedCacher_.get();
    }

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
EngineContext::~EngineContext() {
    // AR#8: sever the cacher's subscription to our bus while the bus is still alive
    // (MainCacher::Shutdown contract — "call before MessageBus is destroyed"). Covers both the
    // owned cacher and an injected one we Initialize()d onto bus_. Members are then destroyed in
    // reverse declaration order (calibration_ -> graph_ -> bus_ -> registry_ -> ownedCacher_), so
    // the graph's node cleanup still sees a live cacher.
    if (mainCacher_) {
        mainCacher_->Shutdown();
    }
}

NodeTypeRegistry& EngineContext::Registry() { return *registry_; }
Vixen::EventBus::MessageBus& EngineContext::Bus() { return *bus_; }
RenderGraph& EngineContext::Graph() { return *graph_; }
CalibrationStore* EngineContext::Calibration() { return calibration_.get(); }

} // namespace Vixen::RenderGraph
