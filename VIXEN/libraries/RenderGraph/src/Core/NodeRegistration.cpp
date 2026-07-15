#include "Core/NodeRegistration.h"

namespace Vixen::RenderGraph {

namespace detail {
    NodeRegistrarLink*& HeadLink() {
        static NodeRegistrarLink* head = nullptr;
        return head;
    }

    NodeRegistrarLink::NodeRegistrarLink(void (*fn)(NodeTypeRegistry&))
        : registerFn(fn), next(HeadLink()) {
        HeadLink() = this;
    }
}

void RegisterAllNodes(NodeTypeRegistry& registry) {
    for (detail::NodeRegistrarLink* link = detail::HeadLink(); link != nullptr; link = link->next) {
        link->registerFn(registry);
    }
}

} // namespace Vixen::RenderGraph
