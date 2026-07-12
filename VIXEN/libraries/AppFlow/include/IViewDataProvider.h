#pragma once
#include <cstdint>

// The view<->model accessor seam (View-Data-Provider-Seam-Design-2026-07.md). Generated/handwritten
// handlers read/write a view noun through this interface instead of a concrete store directly, so
// swapping the backing store (direct field today, Gaia-ECS later, per the binding-framework design)
// is a provider swap, not a rewrite of every handler. Synchronous + immediate by design (Gaia's
// get/set/ref are synchronous with no commit/defer gate -- see the seam doc's "decisive finding").

namespace Vixen::AppFlow {

// Typed noun id, generated from the same schema source as the view (NOT a raw string) so both
// providers implement a compile-checked switch. Inc-A hand-declares the one noun it needs;
// a future codegen pass emits this enum from the [View] schema.
enum class ViewNounId : uint32_t {
    LayerMask = 0,
};

// Provider-interpreted instance slot. Direct-field providers ignore it; a future Gaia provider
// may use it as an entity/morton index. Present from day one so retrofitting per-instance
// identity never has to touch generated handlers.
struct ViewNounKey {
    ViewNounId noun;
    uint64_t instance = 0;
};

struct IViewDataProvider {
    virtual ~IViewDataProvider() = default;
    virtual bool ReadU32(ViewNounKey key, uint32_t& out) const = 0;  // fallible: false = absent
    virtual void WriteU32(ViewNounKey key, uint32_t value) = 0;      // immediate, by value (never a held ref)
};

}  // namespace Vixen::AppFlow
