// Semantic Shader Wiring S2 — synthesis slice B: ShaderFamily
// (undertow docs/plans/2026-08-03-semantic-shader-wiring.md)
//
// A ShaderFamily is ONE program identity with its feature variants cached as
// members: Get(featureSet) applies the canonical after-#version define splice
// and builds (or returns the cached) ShaderDataBundle for exactly that set.
// The manifest (shaders/sdi-variants.json) declares families statically; this
// is the runtime object. Real glslang compiles, CPU-only, no device — the
// probe_update_smoke precedent.

#include <gtest/gtest.h>
#include "ShaderFamily.h"

#include <string>

using namespace ShaderManagement;

namespace {

const char* kFamilySource = R"(#version 450
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer BaseBuffer { uint baseData[]; };
#ifdef FEATURE_EXTRA
layout(std430, binding = 1) buffer ExtraBuffer { uint extraData[]; };
#endif
void main() {
    baseData[0] = 1u;
#ifdef FEATURE_EXTRA
    extraData[0] = 2u;
#endif
}
)";

ShaderFamily MakeFamily() {
    ShaderFamily::Config cfg;
    cfg.name = "TestFamily";
    cfg.stage = ShaderStage::Compute;
    return ShaderFamily(cfg, [] { return std::string(kFamilySource); });
}

size_t BindingCount(const std::shared_ptr<ShaderDataBundle>& bundle) {
    if (!bundle || !bundle->reflectionData) return 0;
    size_t n = 0;
    for (const auto& [set, bindings] : bundle->reflectionData->descriptorSets)
        n += bindings.size();
    return n;
}

bool HasBinding(const std::shared_ptr<ShaderDataBundle>& bundle, uint32_t binding) {
    if (!bundle || !bundle->reflectionData) return false;
    for (const auto& [set, bindings] : bundle->reflectionData->descriptorSets)
        for (const auto& b : bindings)
            if (b.binding == binding) return true;
    return false;
}

} // namespace

TEST(ShaderFamily, VariantsSelectByFeatureSet) {
    auto family = MakeFamily();

    auto base = family.Get({});
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(BindingCount(base), 1u);
    EXPECT_TRUE(HasBinding(base, 0));
    EXPECT_FALSE(HasBinding(base, 1));

    auto extra = family.Get({"FEATURE_EXTRA"});
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(BindingCount(extra), 2u);
    EXPECT_TRUE(HasBinding(extra, 1));
}

TEST(ShaderFamily, MembersAreCachedPerCanonicalFeatureSet) {
    auto family = MakeFamily();

    auto a = family.Get({"FEATURE_EXTRA"});
    auto b = family.Get({"FEATURE_EXTRA"});
    EXPECT_EQ(a.get(), b.get());  // the SAME cached member

    auto base = family.Get({});
    EXPECT_NE(a.get(), base.get());

    // Canonicalization: order- and duplicate-insensitive keys. (Unused
    // defines are legal GLSL — they just gate nothing in this source.)
    auto d = family.Get({"B_FLAG", "A_FLAG"});
    auto e = family.Get({"A_FLAG", "B_FLAG", "A_FLAG"});
    EXPECT_EQ(d.get(), e.get());
}
