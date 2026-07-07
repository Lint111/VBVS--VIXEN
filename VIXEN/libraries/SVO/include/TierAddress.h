#pragma once
// TierAddress.h — Tiered ESVO Observer Addressing, Inc1 M1 (Task 1).
//
// The short hop-chain identity that names any cell/object across all nested
// ESVO tiers (Tiered-ESVO-Observer-Addressing-Design-2026-07.md §4): an
// ordered list of "which child octant/TierRef was taken at tier i". This is
// the object-persistent form of what a tier-crossing ray traversal already
// produces as a side effect (§4) — NOT a new traversal mechanism, and NOT
// GPU-resident (§4: "a small CPU-side identity, cheap to store/compare/
// serialize").
//
// Representation choice: fixed-capacity inline array, not std::vector.
// The design doc's own §4 sketch used std::vector<uint32_t>, but estimates
// "4-5 entries typical" (§4) and this increment's own tier math (see
// TierMath.h) confirms the concrete range: 5 tiers (T0 planet .. galaxy)
// means at most 5 hops today, with headroom to 8 kept here for a couple of
// future tiers without another representation change. A type that is
// compared (shared-prefix) and copied (per-object, per-frame, potentially
// many fleet objects) benefits from being a small trivially-copyable value
// with no heap allocation — std::vector's indirection and allocation cost
// buys nothing here since the capacity is small and bounded by the tier
// count, not by arbitrary user input. Do NOT read this as the wire format:
// §4/§11 explicitly defer the undertow wire-format reconciliation to a
// later increment — this is only the in-process VIXEN-side value type.
//
// Scope note (Tiered-ESVO-Inc1-Plan-2026-07.md §0): pure CPU type, no
// dependency on ChildDescriptor, farBit, SVORebuild, or LaineKarrasOctree's
// traversal code. Those are explicitly out of scope for this increment.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace Vixen::SVO {

// Upper bound on nested tiers this type can address. The tier math this
// increment establishes (TierMath.h) uses 5 tiers (T0 planet, T1 region, T2
// bedrock, system, galaxy); this is kept a couple of entries larger than
// that concrete count so a future tier addition doesn't immediately force a
// representation change, without over-provisioning into "arbitrary length"
// territory (which is what would make std::vector the right choice instead).
inline constexpr std::size_t kMaxTierAddressDepth = 8;

// A hop-chain identity: hops[i] = which child octant/TierRef was taken at
// tier i, ordered from the root tier downward. depth == 0 means the root
// itself (no hops taken yet — the address IS the root).
class TierAddress {
public:
    TierAddress() = default;

    // Construct from an initializer list of hops, e.g. TierAddress{2, 5, 0}.
    TierAddress(std::initializer_list<uint32_t> hopList) {
        for (uint32_t hop : hopList) {
            PushHop(hop);
        }
    }

    // Appends one more hop (descend one tier). No-op past capacity — callers
    // constructing addresses from real tier math should never approach
    // kMaxTierAddressDepth; this is a defensive clamp, not a silent-failure
    // API a caller should rely on.
    void PushHop(uint32_t hop) {
        if (depth_ < kMaxTierAddressDepth) {
            hops_[depth_] = hop;
            ++depth_;
        }
    }

    // Number of hops actually stored (root = 0).
    std::size_t Depth() const { return depth_; }

    // Hop at tier index i (0 = topmost/root-adjacent hop). Caller must
    // ensure i < Depth().
    uint32_t Hop(std::size_t i) const { return hops_[i]; }

    bool operator==(const TierAddress& other) const {
        if (depth_ != other.depth_) return false;
        for (std::size_t i = 0; i < depth_; ++i) {
            if (hops_[i] != other.hops_[i]) return false;
        }
        return true;
    }
    bool operator!=(const TierAddress& other) const { return !(*this == other); }

    // Stable serialization form: a simple depth-prefixed, dot-separated
    // string ("3:2.5.0"). This is NOT the eventual undertow wire format
    // (§4/§11 explicitly defer that decision) — it exists only so this
    // increment's own tests/tools have something deterministic to compare
    // and log, and so a later increment has an obvious, simple starting
    // point to replace rather than an awkward internal layout to unwind.
    std::string ToString() const {
        std::string result = std::to_string(depth_) + ":";
        for (std::size_t i = 0; i < depth_; ++i) {
            if (i > 0) result += ".";
            result += std::to_string(hops_[i]);
        }
        return result;
    }

    // Shared-prefix = shared ancestor (design doc §4). Returns how many
    // leading hops two addresses have in common — 0 if they diverge at the
    // root (or either address is itself the root), up to min(depth) if one
    // is a prefix of the other, and exactly Depth() for a self-comparison.
    // This is the primitive M2's direction/magnitude composition depends on:
    // only the hops *below* this shared prefix need to be composed, not the
    // full address chain.
    static std::size_t SharedPrefixLength(const TierAddress& a, const TierAddress& b) {
        const std::size_t limit = std::min(a.depth_, b.depth_);
        std::size_t i = 0;
        while (i < limit && a.hops_[i] == b.hops_[i]) {
            ++i;
        }
        return i;
    }

private:
    std::array<uint32_t, kMaxTierAddressDepth> hops_{};
    std::size_t depth_ = 0;
};

}  // namespace Vixen::SVO
