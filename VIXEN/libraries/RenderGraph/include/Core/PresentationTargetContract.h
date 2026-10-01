#pragma once
#include "Data/Core/SlotInfo.h"
#include "IRenderTarget.h"
#include <optional>

namespace Vixen::RenderGraph {

// Available before allocation, so graph construction wires the concrete target's declared
// ports. Synchronization comes from the same target type used by runtime IRenderTarget.
struct PresentationTargetContract {
    SlotInfo target;
    SlotInfo imageIndex;
    Vixen::Vulkan::Resources::TargetSynchronization synchronization;
    std::optional<SlotInfo> renderComplete;
};

} // namespace Vixen::RenderGraph
