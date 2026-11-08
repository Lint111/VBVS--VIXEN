#include <gtest/gtest.h>

#include "closure_manifest.h"

using namespace Vixen::Closure;

TEST(ClosureManifest, SelectsOnlyFamiliesWhoseClosureHoldsTheChangedPath) {
    Manifest manifest;
    manifest.Add("VIXEN/shaders/ExposureMeter.comp", "lookdev-exposure");
    manifest.Add("VIXEN/shaders/ExposureMeter.comp", "lookdev-tonemap");
    manifest.Add("VIXEN/libraries/Text/src/Shaper.cpp", "lookdev-glyphsdf");
    EXPECT_EQ(manifest.Select({"VIXEN/shaders/ExposureMeter.comp"}), (Families{"lookdev-exposure", "lookdev-tonemap"}));
    EXPECT_EQ(manifest.Select({"VIXEN/shaders/ExposureMeter.comp", "VIXEN/libraries/Text/src/Shaper.cpp"}).size(), 3u);
}

TEST(ClosureManifest, PathOutsideEveryClosureSelectsNothing) {
    Manifest manifest;
    manifest.Add("VIXEN/shaders/A.comp", "lookdev-states");
    EXPECT_TRUE(manifest.Select({"docs/readme.md"}).empty());
}

TEST(ClosureManifest, SerializeParseRoundTrips) {
    Manifest manifest;
    manifest.AddAll("VIXEN/a.cpp", {"lookdev-states", "lookdev-exposure"});
    manifest.Add("VIXEN/b.glsl", "lookdev-states");
    std::istringstream in(manifest.Serialize());
    EXPECT_EQ(Manifest::Parse(in).Rows(), manifest.Rows());
}
