// test_tier_ref.cpp — Tiered ESVO Inc2, M1 Task 1.
//
// Pure data/layout tests for TierRef, no octree/GPU needed. Covers field
// round-trip (construct, read back every field unchanged) and the exact
// std430-safe byte layout (size + per-field offsets), so a future edit that
// accidentally reintroduces padding is caught here rather than discovered as
// a GPU-side misread once M3 wires real shader consumption.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "TierRef.h"

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Field round-trip.
// ---------------------------------------------------------------------------

TEST(TierRef, ConstructAndReadFieldsBackUnchanged) {
    TierRef ref{};
    ref.childOctreeIndex = 7u;
    ref.childOriginLocal[0] = 1.25f;
    ref.childOriginLocal[1] = 1.5f;
    ref.childOriginLocal[2] = 1.75f;
    ref.childScale = 0.03125f;

    EXPECT_EQ(ref.childOctreeIndex, 7u);
    EXPECT_FLOAT_EQ(ref.childOriginLocal[0], 1.25f);
    EXPECT_FLOAT_EQ(ref.childOriginLocal[1], 1.5f);
    EXPECT_FLOAT_EQ(ref.childOriginLocal[2], 1.75f);
    EXPECT_FLOAT_EQ(ref.childScale, 0.03125f);
}

// Round-trip through a raw byte buffer (memcpy out, memcpy into a fresh
// TierRef, compare) — the exact form TierRefTable's serialization uses
// (Task 2), proving TierRef is safe to move through byte buffers verbatim.
TEST(TierRef, RoundTripsThroughByteBufferMemcpy) {
    TierRef original{};
    original.childOctreeIndex = 42u;
    original.childOriginLocal[0] = -0.25f;
    original.childOriginLocal[1] = 0.0f;
    original.childOriginLocal[2] = 0.9375f;
    original.childScale = 128.0f;

    std::array<uint8_t, sizeof(TierRef)> bytes{};
    std::memcpy(bytes.data(), &original, sizeof(TierRef));

    TierRef restored{};
    std::memcpy(&restored, bytes.data(), sizeof(TierRef));

    EXPECT_EQ(restored.childOctreeIndex, original.childOctreeIndex);
    EXPECT_FLOAT_EQ(restored.childOriginLocal[0], original.childOriginLocal[0]);
    EXPECT_FLOAT_EQ(restored.childOriginLocal[1], original.childOriginLocal[1]);
    EXPECT_FLOAT_EQ(restored.childOriginLocal[2], original.childOriginLocal[2]);
    EXPECT_FLOAT_EQ(restored.childScale, original.childScale);
}

// ---------------------------------------------------------------------------
// std430-safe byte layout (the Sparse-Mip Inc1 vec3-padding gotcha,
// re-verified for TierRef's plain float[3] representation — see TierRef.h's
// header comment for why float[3] rather than glm::vec3 sidesteps it).
// ---------------------------------------------------------------------------

TEST(TierRef, LayoutIsExactlyTwentyBytesWithNoHiddenPadding) {
    EXPECT_EQ(sizeof(TierRef), 20u);
    EXPECT_EQ(offsetof(TierRef, childOctreeIndex), 0u);
    EXPECT_EQ(offsetof(TierRef, childOriginLocal), 4u);
    EXPECT_EQ(offsetof(TierRef, childScale), 16u);
}

TEST(TierRef, IsTriviallyCopyableStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<TierRef>);
    EXPECT_TRUE(std::is_trivially_copyable_v<TierRef>);
}

// An array of TierRef must stride at exactly sizeof(TierRef) with no
// inter-element padding either (proves the earlier single-struct offset
// checks generalize to the TierRefTable array form Task 2 builds on top of).
TEST(TierRef, ArrayOfTwoStridesAtExactStructSize) {
    TierRef arr[2]{};
    const auto* p0 = reinterpret_cast<const uint8_t*>(&arr[0]);
    const auto* p1 = reinterpret_cast<const uint8_t*>(&arr[1]);
    EXPECT_EQ(static_cast<size_t>(p1 - p0), sizeof(TierRef));
}
