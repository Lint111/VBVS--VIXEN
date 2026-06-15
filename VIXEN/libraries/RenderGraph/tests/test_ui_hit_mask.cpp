// test_ui_hit_mask.cpp — unit tests for the per-element UI hit-mask math (Ui/UiHitMask.h).
//
// Pure CPU; no RmlUi / Vulkan / GPU. Covers:
//   * ParseHitMask for every authoring form (none/box/ellipse/rounded-rect[ px]/url(...)),
//     whitespace + case tolerance, and fail-open on garbage.
//   * HitMaskContains for Box (always), Ellipse (center in, outside-the-ellipse corner out),
//     and RoundedRect (corner cut out, edge-band + center in), plus degenerate sizes.
//   * Image: a 2x2 RGBA mask is written to a temp PNG (via stb_image_write) — one opaque quadrant,
//     three transparent — and sampled to confirm alpha gating + the fail-open path on a bad path.
//     (Guarded: if writing the temp file fails, the image assertions are skipped, not failed.)

#include <gtest/gtest.h>

#include "Ui/UiHitMask.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

// ---------------------------------------------------------------------------
// ParseHitMask
// ---------------------------------------------------------------------------

TEST(UiHitMask_Parse, EmptyAndNoneAndBoxAreBox) {
    EXPECT_EQ(ParseHitMask("").shape, HitMaskShape::Box);
    EXPECT_EQ(ParseHitMask("none").shape, HitMaskShape::Box);
    EXPECT_EQ(ParseHitMask("box").shape, HitMaskShape::Box);
    EXPECT_EQ(ParseHitMask("   ").shape, HitMaskShape::Box);
}

TEST(UiHitMask_Parse, Ellipse) {
    EXPECT_EQ(ParseHitMask("ellipse").shape, HitMaskShape::Ellipse);
    // Case + surrounding whitespace tolerant.
    EXPECT_EQ(ParseHitMask("  ELLIPSE ").shape, HitMaskShape::Ellipse);
}

TEST(UiHitMask_Parse, RoundedRectDefaultRadius) {
    const HitMaskSpec s = ParseHitMask("rounded-rect");
    EXPECT_EQ(s.shape, HitMaskShape::RoundedRect);
    EXPECT_FLOAT_EQ(s.radius, kDefaultRoundedRectRadius);
}

TEST(UiHitMask_Parse, RoundedRectExplicitRadius) {
    const HitMaskSpec s = ParseHitMask("rounded-rect 12");
    EXPECT_EQ(s.shape, HitMaskShape::RoundedRect);
    EXPECT_FLOAT_EQ(s.radius, 12.0f);

    // Fractional + extra whitespace.
    const HitMaskSpec s2 = ParseHitMask("  rounded-rect   4.5  ");
    EXPECT_EQ(s2.shape, HitMaskShape::RoundedRect);
    EXPECT_FLOAT_EQ(s2.radius, 4.5f);

    // A non-positive / non-numeric trailer keeps the default (fail-open).
    const HitMaskSpec s3 = ParseHitMask("rounded-rect nope");
    EXPECT_EQ(s3.shape, HitMaskShape::RoundedRect);
    EXPECT_FLOAT_EQ(s3.radius, kDefaultRoundedRectRadius);
}

TEST(UiHitMask_Parse, ImageUrlForms) {
    const HitMaskSpec s = ParseHitMask("url(masks/heart.png)");
    EXPECT_EQ(s.shape, HitMaskShape::Image);
    EXPECT_EQ(s.imagePath, "masks/heart.png");

    // Quoted paths (single + double) strip the quotes; whitespace inside the parens is trimmed.
    EXPECT_EQ(ParseHitMask("url(\"a/b.png\")").imagePath, "a/b.png");
    EXPECT_EQ(ParseHitMask("url( 'c.png' )").imagePath, "c.png");
}

TEST(UiHitMask_Parse, GarbageFailsOpenToBox) {
    EXPECT_EQ(ParseHitMask("wibble").shape, HitMaskShape::Box);
    EXPECT_EQ(ParseHitMask("url()").shape, HitMaskShape::Box);       // empty url → Box
    EXPECT_EQ(ParseHitMask("url(").shape, HitMaskShape::Box);        // malformed → Box
}

// ---------------------------------------------------------------------------
// HitMaskContains — Box
// ---------------------------------------------------------------------------

TEST(UiHitMask_Box, AlwaysHitsInsideAabb) {
    HitMaskSpec box;  // default Box
    EXPECT_TRUE(HitMaskContains(box, 0.0f, 0.0f, 100.0f, 50.0f));    // corner
    EXPECT_TRUE(HitMaskContains(box, 50.0f, 25.0f, 100.0f, 50.0f));  // center
    EXPECT_TRUE(HitMaskContains(box, 100.0f, 50.0f, 100.0f, 50.0f)); // far corner
}

TEST(UiHitMask_Box, DegenerateSizeMisses) {
    HitMaskSpec box;
    EXPECT_FALSE(HitMaskContains(box, 0.0f, 0.0f, 0.0f, 50.0f));
    EXPECT_FALSE(HitMaskContains(box, 0.0f, 0.0f, 100.0f, 0.0f));
}

TEST(UiHitMask_Box, OutsideAabbMisses) {
    HitMaskSpec box;
    EXPECT_FALSE(HitMaskContains(box, -1.0f, 10.0f, 100.0f, 50.0f));
    EXPECT_FALSE(HitMaskContains(box, 101.0f, 10.0f, 100.0f, 50.0f));
}

// ---------------------------------------------------------------------------
// HitMaskContains — Ellipse
// ---------------------------------------------------------------------------

TEST(UiHitMask_Ellipse, CenterIn_CornersOut) {
    HitMaskSpec e; e.shape = HitMaskShape::Ellipse;
    const float w = 100.0f, h = 60.0f;

    EXPECT_TRUE(HitMaskContains(e, w * 0.5f, h * 0.5f, w, h));   // center
    EXPECT_TRUE(HitMaskContains(e, 1.0f, h * 0.5f, w, h));       // left edge mid (just inside)
    EXPECT_TRUE(HitMaskContains(e, w * 0.5f, 1.0f, w, h));       // top edge mid

    // The four AABB corners lie OUTSIDE the inscribed ellipse.
    EXPECT_FALSE(HitMaskContains(e, 0.0f, 0.0f, w, h));
    EXPECT_FALSE(HitMaskContains(e, w, 0.0f, w, h));
    EXPECT_FALSE(HitMaskContains(e, 0.0f, h, w, h));
    EXPECT_FALSE(HitMaskContains(e, w, h, w, h));
}

TEST(UiHitMask_Ellipse, BoundaryPointOnAxisIsHit) {
    HitMaskSpec e; e.shape = HitMaskShape::Ellipse;
    // Exactly on the ellipse boundary at the top of the minor axis: nx=0, ny=-1 → on the curve.
    EXPECT_TRUE(HitMaskContains(e, 50.0f, 0.0f, 100.0f, 60.0f));
}

// ---------------------------------------------------------------------------
// HitMaskContains — RoundedRect
// ---------------------------------------------------------------------------

TEST(UiHitMask_RoundedRect, CornerCutOut_EdgeAndCenterIn) {
    HitMaskSpec rr; rr.shape = HitMaskShape::RoundedRect; rr.radius = 20.0f;
    const float w = 100.0f, h = 80.0f;

    // Center + edge-band points (only one axis inside a corner band) are hits.
    EXPECT_TRUE(HitMaskContains(rr, 50.0f, 40.0f, w, h));   // center
    EXPECT_TRUE(HitMaskContains(rr, 50.0f, 0.0f, w, h));    // top edge mid (edge band)
    EXPECT_TRUE(HitMaskContains(rr, 0.0f, 40.0f, w, h));    // left edge mid (edge band)
    EXPECT_TRUE(HitMaskContains(rr, 20.0f, 20.0f, w, h));   // exactly the TL corner-circle center

    // The extreme AABB corners are cut away (distance to the corner center is r*sqrt(2) > r).
    EXPECT_FALSE(HitMaskContains(rr, 0.0f, 0.0f, w, h));    // TL
    EXPECT_FALSE(HitMaskContains(rr, w, 0.0f, w, h));       // TR
    EXPECT_FALSE(HitMaskContains(rr, 0.0f, h, w, h));       // BL
    EXPECT_FALSE(HitMaskContains(rr, w, h, w, h));          // BR

    // A point just inside the TL corner arc (on the diagonal toward the center) is a hit.
    // Corner center (20,20); a point at (20 - 14, 20 - 14) has distance ~19.8 < 20 → in.
    EXPECT_TRUE(HitMaskContains(rr, 6.0f, 6.0f, w, h));
    // A point further out along that diagonal, (20 - 15, 20 - 15) dist ~21.2 > 20 → out.
    EXPECT_FALSE(HitMaskContains(rr, 5.0f, 5.0f, w, h));
}

TEST(UiHitMask_RoundedRect, ZeroRadiusIsPlainBox) {
    HitMaskSpec rr; rr.shape = HitMaskShape::RoundedRect; rr.radius = 0.0f;
    EXPECT_TRUE(HitMaskContains(rr, 0.0f, 0.0f, 100.0f, 80.0f));  // corner still hits
}

TEST(UiHitMask_RoundedRect, RadiusClampedToHalfExtent) {
    // radius far exceeds the box → clamped to min(w,h)/2, behaving like a stadium/ellipse-ish blob,
    // but the center is always a hit and the extreme corner is always cut.
    HitMaskSpec rr; rr.shape = HitMaskShape::RoundedRect; rr.radius = 1000.0f;
    const float w = 40.0f, h = 40.0f;  // clamps r to 20 → a circle inscribed in the square
    EXPECT_TRUE(HitMaskContains(rr, 20.0f, 20.0f, w, h));   // center
    EXPECT_FALSE(HitMaskContains(rr, 0.0f, 0.0f, w, h));    // corner cut
}

// ---------------------------------------------------------------------------
// HitMaskContains — Image (generated 2x2 mask)
// ---------------------------------------------------------------------------

// Bad path → fail-open (treated as Box → always a hit).
TEST(UiHitMask_Image, MissingFileFailsOpen) {
    HitMaskSpec img; img.shape = HitMaskShape::Image;
    img.imagePath = "/nonexistent/definitely-not-a-real-mask-xyz.png";
    EXPECT_TRUE(HitMaskContains(img, 5.0f, 5.0f, 10.0f, 10.0f));  // fail-open ⇒ hit
}

// 2x2 mask: only the TOP-LEFT texel opaque (alpha 255), the other three transparent (alpha 0).
// Element 100x100 ⇒ the top-left quadrant samples texel (0,0) [opaque ⇒ hit]; the other quadrants
// sample transparent texels [miss]. Skipped (not failed) if the temp file can't be written.
TEST(UiHitMask_Image, AlphaGatesHit) {
    // Build the 2x2 RGBA buffer: index = (y*2 + x)*4; alpha at +3.
    std::vector<uint8_t> rgba(2 * 2 * 4, 0);
    auto setPx = [&](int x, int y, uint8_t a) { rgba[(static_cast<size_t>(y) * 2 + x) * 4 + 3] = a; };
    setPx(0, 0, 255);  // top-left opaque
    setPx(1, 0, 0);
    setPx(0, 1, 0);
    setPx(1, 1, 0);

    // Write a temp PNG. stb_image_write's implementation is compiled in the engine (STBTextureLoader
    // pulls stb_image_write too? — no): include + define the writer locally for the test only.
    const std::string path = std::string(::testing::TempDir()) + "uihitmask_2x2.png";

    extern int WriteTestPng2x2(const char* p, const std::vector<uint8_t>& px);  // defined below
    const int ok = WriteTestPng2x2(path.c_str(), rgba);
    if (!ok) {
        GTEST_SKIP() << "could not write temp PNG mask at " << path << " — skipping image sampling";
    }

    HitMaskSpec img; img.shape = HitMaskShape::Image; img.imagePath = path;
    const float w = 100.0f, h = 100.0f;

    EXPECT_TRUE(HitMaskContains(img, 25.0f, 25.0f, w, h));   // top-left quadrant → opaque texel → hit
    EXPECT_FALSE(HitMaskContains(img, 75.0f, 25.0f, w, h));  // top-right quadrant → transparent → miss
    EXPECT_FALSE(HitMaskContains(img, 25.0f, 75.0f, w, h));  // bottom-left → transparent → miss
    EXPECT_FALSE(HitMaskContains(img, 75.0f, 75.0f, w, h));  // bottom-right → transparent → miss

    std::remove(path.c_str());
}
