#pragma once

#include "Core/NodeTypeRegistry.h"

namespace Vixen::RenderGraph {

namespace detail {
    // Intrusive self-registering linked list. Each node TU's static
    // NodeRegistrarLink constructor is what links it into the list — the
    // constructor running is guaranteed the moment its enclosing .obj is linked
    // in at all (whole-archiving RenderGraphNodes already guarantees that), so
    // there is no "unreferenced symbol" for linker COMDAT/section GC (e.g. MSVC
    // /OPT:REF) to strip, unlike the prior anonymous-namespace-static +
    // std::vector<std::function> mechanism. See
    // Node-Self-Registration-Portable-Fix-Direction-2026-07.md.
    struct NodeRegistrarLink {
        void (*registerFn)(NodeTypeRegistry&);
        NodeRegistrarLink* next;

        explicit NodeRegistrarLink(void (*fn)(NodeTypeRegistry&));
    };

    // Meyers-singleton head of the list. Init-on-first-use: any node TU's static
    // NodeRegistrarLink that runs at dynamic-init safely constructs this first.
    NodeRegistrarLink*& HeadLink();
}

// Replays every self-registered node into the given (per-EngineContext) registry.
// Wire this into EngineConfig::registerNodeTypes.
void RegisterAllNodes(NodeTypeRegistry& registry);

// Token-paste helpers so the registrar's symbol names are unique per use via
// __COUNTER__ (not derived from the type name) — this lets the macro be placed at
// global file scope with a fully-qualified type, e.g. at end of a node .cpp:
//   VIXEN_REGISTER_NODE(Vixen::RenderGraph::CameraNodeType);
#define VIXEN_REGISTER_NODE_CONCAT2(a, b) a##b
#define VIXEN_REGISTER_NODE_CONCAT(a, b) VIXEN_REGISTER_NODE_CONCAT2(a, b)

// One line per node, at file scope. The anonymous namespace gives both symbols
// internal linkage (no cross-TU clash). The free function performs the actual
// registration; the NodeRegistrarLink static object's constructor links it into
// the global list at dynamic-init. RenderGraphNodes MUST still be linked
// whole-archive so every node .obj is present on the link line at all (see
// test_node_self_registration) — that requirement is orthogonal to and
// unaffected by this mechanism.
//
// __COUNTER__ is captured into VIXEN_REGISTER_NODE_ID once and reused for both
// symbol names below — __COUNTER__ advances on every expansion, so referencing
// it directly a second time (to name the function pointed to by the link
// object) would yield a different, undeclared identifier.
#define VIXEN_REGISTER_NODE_IMPL(NodeTypeClass, Id)                               \
    namespace {                                                                   \
        void VIXEN_REGISTER_NODE_CONCAT(vixen_register_fn_, Id)(                  \
            ::Vixen::RenderGraph::NodeTypeRegistry& reg) {                        \
            reg.Register<NodeTypeClass>();                                        \
        }                                                                          \
        ::Vixen::RenderGraph::detail::NodeRegistrarLink                            \
            VIXEN_REGISTER_NODE_CONCAT(s_vixen_node_registrar_, Id)(               \
                &VIXEN_REGISTER_NODE_CONCAT(vixen_register_fn_, Id));              \
    }
#define VIXEN_REGISTER_NODE(NodeTypeClass) \
    VIXEN_REGISTER_NODE_IMPL(NodeTypeClass, __COUNTER__)

} // namespace Vixen::RenderGraph
