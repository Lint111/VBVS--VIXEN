#pragma once
#include "Ui/ViewStore.h"
#include <cstddef>
#include <span>

namespace Vixen::RenderGraph {

// Reads a UTVA AoS wire buffer (View Contract Inc-3, spec §4) into a ViewStore, guided by the
// store's ViewBlob (declared field order + kinds). Version-checked at entry against store.Version().
// Returns false + logs (LT_ERROR) on any version mismatch or malformed input; never partially
// writes on failure, never throws, never over-reads (bounds-checked against the span).
class ViewWireReader {
public:
    static bool Apply(std::span<const std::byte> wire, ViewStore& store);
};

}  // namespace Vixen::RenderGraph
