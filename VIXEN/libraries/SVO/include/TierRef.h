#pragma once
// TierRef.h — Tiered ESVO Inc2, M1 Task 1.
//
// The indirect payload a tier-crossing leaf's ChildDescriptor::contourPointer
// resolves to (Tiered-ESVO-Observer-Addressing-Design-2026-07.md §3.2): which
// OTHER, independently-resident octree a farBit==1 leaf's contents actually
// live in, and the single scale+offset that remaps a ray from the CURRENT
// tree's local [1,2) frame into the CHILD tree's own [1,2) frame.
//
// Scope note (Tiered-ESVO-Inc2-Plan-2026-07.md M1 / §0): this file is PURE
// data — a plain-old-data struct plus its std430 layout proof. It has no
// dependency on ChildDescriptor/farBit, no construction-path wiring
// (SVORebuild.cpp/SVOBuilder.cpp, M2), and no traversal/shader consumption
// (LaineKarrasOctree traversal, BodyInstanceRayMarch.comp, M3). Task 2
// (TierRefTable as a ConcatenatedOctrees parallel array) and Task 3
// (OctreeConfig's tierRefTableBase field) build on top of this type but are
// implemented in ShellOctreeGpu.h / the OctreeConfig codegen schema
// respectively, not here.
//
// §3.3 float32-safety discipline: childOriginLocal/childScale are expressed
// ONLY in the immediate parent tree's own local [1,2) frame — never world
// space, never a flattened world-to-leaf matrix. There is deliberately no
// field wide enough to hold an accumulated transform; a ray crossing N tiers
// composes N of these local hops (TierDirection.h's SumTail is the CPU-side
// analogue of this same composition), each individually well-conditioned
// regardless of the absolute scale the tiers represent.
//
// std430 packing note (the Sparse-Mip Inc1 gotcha, re-applied here): a
// straight `float childOriginLocal[3]` (NOT glm::vec3) is used deliberately.
// A glsl `vec3` struct member has base alignment 16 but occupies only 12
// bytes on the GPU side — the std430 rule pads the *base alignment*, not the
// member size, so a scalar immediately following a vec3 packs at offset 12,
// not 16 (padding to a 16-byte boundary only happens once, at the very end
// of the enclosing struct, or when the NEXT member itself needs >4-byte
// alignment). A `float[3]` array of independent scalars has no such
// alignment quirk: each float is 4-byte aligned, and there is no reason for
// a std430-conformant GLSL mirror (`float childOriginLocal[3];`) to insert
// hidden padding either before or after it. The static_asserts below pin the
// exact byte layout so this claim is proven, not assumed.

#include <cstddef>
#include <cstdint>

namespace Vixen::SVO {

// One tier-crossing edge: which child octree, and the single scale+offset
// that maps the current (parent) tree's local [1,2) frame to the child's.
struct TierRef {
    uint32_t childOctreeIndex;    // @0  : index into ConcatenatedOctrees::configs[]
    float    childOriginLocal[3]; // @4  : child's [1,2)-space origin, in the PARENT's local frame
    float    childScale;          // @16 : linear scale of the child's unit cube, in parent-local units
};

// Layout proof: 20 bytes total, no hidden padding anywhere — every member is
// individually 4-byte aligned and the struct's own natural alignment (4, the
// widest member) requires no end-of-struct pad either. This is std430-safe
// as an SSBO array element as-is (an array of TierRef would stride at 20
// bytes with no compiler-inserted gaps) — should a future milestone find it
// needs a coarser (e.g. 16-byte) element stride for a different reason, that
// is a distinct, deliberate change, not an accidental packing surprise.
static_assert(sizeof(TierRef) == 20, "TierRef must be exactly 20 bytes (std430-safe, no hidden padding)");
static_assert(offsetof(TierRef, childOctreeIndex) == 0, "childOctreeIndex@0");
static_assert(offsetof(TierRef, childOriginLocal) == 4, "childOriginLocal@4");
static_assert(offsetof(TierRef, childScale) == 16, "childScale@16");
static_assert(std::is_standard_layout_v<TierRef>, "TierRef must be standard-layout for memcpy round-trip");
static_assert(std::is_trivially_copyable_v<TierRef>, "TierRef must be trivially copyable for byte-buffer serialization");

}  // namespace Vixen::SVO
