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
// ESVO Address Extraction, Slice V1 (2026-08-17, docs/superpowers/specs/
// 2026-08-17-esvo-address-extraction-design.md, RULING B): storage moved to
// the kernel-generated Vixen::SVO::EsvoAddress POD (Generated/EsvoAddress.g.h,
// depth + 8 flattened hop fields, [GpuStruct]) — the SAME struct undertow's
// Undertow.Substrate.EsvoAddress links via the C# face of the identical
// schema, so both domains read one address vocabulary with no translation
// layer. TierAddress itself stays the hand-authored C++ ergonomics wrapper
// (PushHop/Depth/Hop/equality/ToString — RULING B: only the value MATH that
// is genuinely shared cross-language crosses as a kernel-owned free function;
// a single-struct mutator/accessor needs no kernel derivation, [GpuStruct]
// already gives the byte-identical mirror struct for free). SharedPrefixLength
// now delegates to the kernel-generated Vixen::SVO::SharedPrefixLength
// (Generated/EsvoAddressMath.g.hpp) instead of a hand-written loop — same
// algorithm, one fewer place it could drift from the C# face.
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

#include <cstdint>
#include <string>

#include "Generated/EsvoAddress.g.h"
#include "Generated/EsvoAddressMath.g.hpp"

// This header uses std::min-shaped comparisons. When included after <windows.h> (pulled in
// transitively on the Windows build via Vulkan/GTest), the `min`/`max` function-like macros
// mangle unqualified calls into a syntax error. NOMINMAX only helps before windows.h is seen,
// which a header cannot guarantee, so drop the macros outright — no C++ code wants them. Same
// convention as GpuTraversalMirror.h.
#undef min
#undef max

namespace Vixen::SVO {

// Upper bound on nested tiers this type can address — matches the kernel-generated
// EsvoAddress's own fixed field count (Depth + Hop0..Hop7). Kept as a named constant here
// (rather than a bare "8" at every call site) exactly as before the extraction.
inline constexpr std::size_t kMaxTierAddressDepth = 8;

// A hop-chain identity: hops[i] = which child octant/TierRef was taken at
// tier i, ordered from the root tier downward. depth == 0 means the root
// itself (no hops taken yet — the address IS the root).
//
// Storage is the kernel-generated EsvoAddress POD (public Depth/Hop0..Hop7 fields, no methods —
// [GpuStruct] emits data only). TierAddress wraps it with the ergonomic C++ API callers already
// use; the wrapper adds no bytes beyond the POD itself (a single EsvoAddress member).
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
        if (addr_.Depth < kMaxTierAddressDepth) {
            HopSlot(addr_.Depth) = hop;
            ++addr_.Depth;
        }
    }

    // Number of hops actually stored (root = 0).
    std::size_t Depth() const { return addr_.Depth; }

    // Hop at tier index i (0 = topmost/root-adjacent hop). Caller must
    // ensure i < Depth().
    uint32_t Hop(std::size_t i) const { return HopSlot(i); }

    // Read-only access to the underlying kernel-generated POD — the shared address vocabulary
    // undertow's C# face and VIXEN's C++ face both read (spec §3.1).
    const EsvoAddress& Raw() const { return addr_; }

    bool operator==(const TierAddress& other) const {
        if (addr_.Depth != other.addr_.Depth) return false;
        for (std::size_t i = 0; i < addr_.Depth; ++i) {
            if (HopSlot(i) != other.HopSlot(i)) return false;
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
        std::string result = std::to_string(addr_.Depth) + ":";
        for (std::size_t i = 0; i < addr_.Depth; ++i) {
            if (i > 0) result += ".";
            result += std::to_string(HopSlot(i));
        }
        return result;
    }

    // Shared-prefix = shared ancestor (design doc §4). Returns how many
    // leading hops two addresses have in common — 0 if they diverge at the
    // root (or either address is itself the root), up to min(depth) if one
    // is a prefix of the other, and exactly Depth() for a self-comparison.
    // This is the primitive M2's direction/magnitude composition depends on:
    // only the hops *below* this shared prefix need to be composed, not the
    // full address chain. Delegates to the kernel-generated free function
    // (Generated/EsvoAddressMath.g.hpp) — same algorithm as before the
    // extraction, now shared with the C# face instead of hand-duplicated.
    static std::size_t SharedPrefixLength(const TierAddress& a, const TierAddress& b) {
        const EsvoAddress& x = a.addr_;
        const EsvoAddress& y = b.addr_;
        return static_cast<std::size_t>(Vixen::SVO::SharedPrefixLength(
            static_cast<int32_t>(x.Depth), x.Hop0, x.Hop1, x.Hop2, x.Hop3, x.Hop4, x.Hop5, x.Hop6, x.Hop7,
            static_cast<int32_t>(y.Depth), y.Hop0, y.Hop1, y.Hop2, y.Hop3, y.Hop4, y.Hop5, y.Hop6, y.Hop7));
    }

private:
    // Indexed access into the generated POD's flattened Hop0..Hop7 fields (RULING B: the
    // emitter gives no [i] operator — those stay hand-authored ergonomics).
    uint32_t& HopSlot(std::size_t i) {
        switch (i) {
            case 0: return addr_.Hop0;
            case 1: return addr_.Hop1;
            case 2: return addr_.Hop2;
            case 3: return addr_.Hop3;
            case 4: return addr_.Hop4;
            case 5: return addr_.Hop5;
            case 6: return addr_.Hop6;
            default: return addr_.Hop7;
        }
    }
    uint32_t HopSlot(std::size_t i) const {
        return const_cast<TierAddress*>(this)->HopSlot(i);
    }

    EsvoAddress addr_{};
};

}  // namespace Vixen::SVO
