#pragma once

#include "Core/NodeTypeRegistry.h"
#include <functional>
#include <vector>

namespace Vixen::RenderGraph {

namespace detail {
    // Meyers-singleton list of node registrars. Init-on-first-use: any node TU's
    // static registrar that runs at dynamic-init safely constructs this first.
    std::vector<std::function<void(NodeTypeRegistry&)>>& NodeRegistrars();

    // Appends a registrar thunk; returns true so it can initialise a file-scope bool.
    bool RegisterNodeFactory(std::function<void(NodeTypeRegistry&)> thunk);
}

// Replays every self-registered node into the given (per-EngineContext) registry.
// Wire this into EngineConfig::registerNodeTypes.
void RegisterAllNodes(NodeTypeRegistry& registry);

// One-liner each node .cpp uses to self-register. Place at file scope.
//   VIXEN_REGISTER_NODE(CameraNodeType);
#define VIXEN_REGISTER_NODE(NodeTypeClass)                                        \
    namespace {                                                                   \
        const bool s_vixen_registered_##NodeTypeClass =                           \
            ::Vixen::RenderGraph::detail::RegisterNodeFactory(                     \
                [](::Vixen::RenderGraph::NodeTypeRegistry& reg) {                 \
                    reg.Register<NodeTypeClass>();                                 \
                });                                                               \
    }

} // namespace Vixen::RenderGraph
