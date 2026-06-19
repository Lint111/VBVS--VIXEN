#include "Core/NodeRegistration.h"

namespace Vixen::RenderGraph {

namespace detail {
    std::vector<std::function<void(NodeTypeRegistry&)>>& NodeRegistrars() {
        static std::vector<std::function<void(NodeTypeRegistry&)>> registrars;
        return registrars;
    }

    bool RegisterNodeFactory(std::function<void(NodeTypeRegistry&)> thunk) {
        NodeRegistrars().push_back(std::move(thunk));
        return true;
    }
}

void RegisterAllNodes(NodeTypeRegistry& registry) {
    for (auto& thunk : detail::NodeRegistrars()) {
        thunk(registry);
    }
}

} // namespace Vixen::RenderGraph
