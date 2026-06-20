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

// Token-paste helpers so the registrar's variable name is unique per use via
// __COUNTER__ (not derived from the type name) — this lets the macro be placed at
// global file scope with a fully-qualified type, e.g. at end of a node .cpp:
//   VIXEN_REGISTER_NODE(Vixen::RenderGraph::CameraNodeType);
#define VIXEN_REGISTER_NODE_CONCAT2(a, b) a##b
#define VIXEN_REGISTER_NODE_CONCAT(a, b) VIXEN_REGISTER_NODE_CONCAT2(a, b)

// One line per node, at file scope. The anonymous namespace gives the registrar
// internal linkage (no cross-TU clash); the registrar runs at dynamic-init and
// only appends a thunk — the NodeType itself is constructed later, in
// RegisterAllNodes(). RenderGraphNodes MUST be linked whole-archive or the linker
// strips these registrars (see test_node_self_registration).
#define VIXEN_REGISTER_NODE(NodeTypeClass)                                        \
    namespace {                                                                   \
        const bool VIXEN_REGISTER_NODE_CONCAT(s_vixen_node_registrar_, __COUNTER__) = \
            ::Vixen::RenderGraph::detail::RegisterNodeFactory(                     \
                [](::Vixen::RenderGraph::NodeTypeRegistry& reg) {                  \
                    reg.Register<NodeTypeClass>();                                 \
                });                                                                \
    }

} // namespace Vixen::RenderGraph
