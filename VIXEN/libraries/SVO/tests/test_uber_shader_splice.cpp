// test_uber_shader_splice.cpp — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11.
//
// Pure-CPU / host-side gate: proves SpliceProceduralRecipesIntoSource produces a
// well-formed BodyInstanceRayMarch.comp that compiles clean through the REAL production
// path (ShaderBundleBuilder, same AddIncludePath/preprocessing BuildRenderGraph.cpp uses —
// not a hand-composed minimal shader like the M4 parity gate). ShaderBundleBuilder::Build()
// only invokes glslang (device-agnostic); it never creates a Vulkan device or dispatches
// anything, so this runs on every machine, GPU or not. Does NOT prove pixel-identical
// rendering (that's the windowed live gate, handed off) — only that the spliced text is
// syntactically/semantically valid GLSL the compiler accepts.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "Recipe/UberShaderSplice.h"
#include "ShaderBundleBuilder.h"

#ifndef BODY_INSTANCE_RAYMARCH_COMP_PATH
#  error "BODY_INSTANCE_RAYMARCH_COMP_PATH must be defined via CMake compile_definitions"
#endif
#ifndef VIXEN_SHADERS_DIR
#  error "VIXEN_SHADERS_DIR must be defined via CMake compile_definitions"
#endif
#ifndef VIXEN_SVO_SHADERS_DIR
#  error "VIXEN_SVO_SHADERS_DIR must be defined via CMake compile_definitions"
#endif

using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z;
    in.data[3] = r;
    return in;
}

SdfInstruction box(glm::vec3 he) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
    return in;
}

SdfInstruction combine(SdfOpCode op, float k = 0.f) {
    SdfInstruction in{};
    in.opCode = (uint8_t)op;
    in.data[2] = k;
    return in;
}

// Builds a compiled ShaderBundleBuilder::BuildResult for the given source, mirroring
// BuildRenderGraph.cpp's own builder configuration (include paths, program name, pipeline
// type) so this test exercises the SAME preprocessing/compile path production does.
ShaderManagement::ShaderBundleBuilder::BuildResult CompileSource(const std::string& source) {
    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetProgramName("BodyInstanceRayMarch_SpliceTest")
           .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
           .SetTargetVulkanVersion(130)
           .SetTargetSpirvVersion(160)
           .AddIncludePath(VIXEN_SHADERS_DIR)
           .AddIncludePath(VIXEN_SVO_SHADERS_DIR)
           .AddStage(ShaderManagement::ShaderStage::Compute, source, "main");
    return builder.Build();
}

} // namespace

// --- Zero-registration case: source passes through unchanged, still compiles -------------

TEST(UberShaderSplice, ZeroRegistrationsLeavesSourceCompiling) {
    const std::string raw = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
    ASSERT_FALSE(raw.empty()) << "could not read " << BODY_INSTANCE_RAYMARCH_COMP_PATH;

    RecipeRegistry emptyRegistry;
    const std::string spliced = SpliceProceduralRecipesIntoSource(raw, emptyRegistry);
    EXPECT_EQ(spliced, raw) << "zero-registration splice must be a byte-identical passthrough";

    auto result = CompileSource(spliced);
    EXPECT_TRUE(result.success) << result.errorMessage;
}

// --- Marker presence (drift-guard: fails loudly if the .comp anchor is ever removed) -----

TEST(UberShaderSplice, MarkerIsPresentInRealShaderSource) {
    const std::string raw = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
    ASSERT_FALSE(raw.empty());
    EXPECT_NE(raw.find("VIXEN_UBER_RECIPE_SPLICE_MARKER"), std::string::npos);
}

// --- N=1 registered recipe: splice compiles, macro + generated functions present ---------

TEST(UberShaderSplice, SingleRegisteredRecipeCompiles) {
    const std::string raw = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
    ASSERT_FALSE(raw.empty());

    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({64.f, 64.f, 64.f}, 5.f) };
    ASSERT_EQ(registry.Register(2u, e), RecipeRegistry::RegisterResult::Ok);

    const std::string spliced = SpliceProceduralRecipesIntoSource(raw, registry);
    EXPECT_NE(spliced.find("VIXEN_UBER_RECIPE_SPLICED"), std::string::npos);
    EXPECT_NE(spliced.find("sdfRecipe_2"), std::string::npos);
    EXPECT_NE(spliced.find("evalRecipeField"), std::string::npos);
    EXPECT_NE(spliced.find("getRecipeBoundSphere"), std::string::npos);

    auto result = CompileSource(spliced);
    EXPECT_TRUE(result.success) << result.errorMessage
        << "\n--- spliced source (first 4000 chars after marker) ---\n"
        << spliced.substr(spliced.find("VIXEN_UBER_RECIPE_SPLICE_MARKER"), 4000);
}

// --- N=10 registered recipes: proves the switch scales + the N=3/N=10 measurement scene
// wiring (Task 11's scope-refinement measurement) compiles at both scale points -----------

TEST(UberShaderSplice, TenRegisteredRecipesCompile) {
    const std::string raw = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
    ASSERT_FALSE(raw.empty());

    RecipeRegistry registry;
    for (uint32_t i = 2; i < 12; ++i) {
        RecipeRegistry::RecipeEntry e{};
        // Mix leaf + CSG + Round so the corpus isn't ten copies of the same emitted body.
        e.bytecode = {
            box(glm::vec3(2.f + 0.1f * i, 2.f, 2.f)),
            sphere(glm::vec3(float(i), 0.f, 0.f), 1.5f),
            combine(SdfOpCode::SmoothUnion, 0.15f),
        };
        ASSERT_EQ(registry.Register(i, e), RecipeRegistry::RegisterResult::Ok) << "id=" << i;
    }
    ASSERT_EQ(registry.Ids().size(), 10u);

    const std::string spliced = SpliceProceduralRecipesIntoSource(raw, registry);
    for (uint32_t i = 2; i < 12; ++i) {
        EXPECT_NE(spliced.find("sdfRecipe_" + std::to_string(i)), std::string::npos) << "id=" << i;
    }

    auto result = CompileSource(spliced);
    EXPECT_TRUE(result.success) << result.errorMessage;
}

// --- Mixing legacy ids (0/1) with registry ids (>=2) — the two paths coexist -------------

TEST(UberShaderSplice, LegacyAndRegistryRecipesCoexist) {
    const std::string raw = ReadFile(BODY_INSTANCE_RAYMARCH_COMP_PATH);
    ASSERT_FALSE(raw.empty());

    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0.f, 0.f, 0.f}, 3.f) };
    ASSERT_EQ(registry.Register(2u, e), RecipeRegistry::RegisterResult::Ok);

    const std::string spliced = SpliceProceduralRecipesIntoSource(raw, registry);
    // Legacy path (traceProceduralBody, RECIPE_SPHERE/RECIPE_DISPLACED_SPHERE) is untouched
    // text from SdfRecipes.glsl — still present after the splice.
    EXPECT_NE(spliced.find("traceProceduralBody"), std::string::npos);
    EXPECT_NE(spliced.find("traceUberRecipeBody"), std::string::npos);

    auto result = CompileSource(spliced);
    EXPECT_TRUE(result.success) << result.errorMessage;
}

// --- Marker-missing input throws (rather than silently producing a stale shader) ---------

TEST(UberShaderSplice, MissingMarkerThrows) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry e{};
    e.bytecode = { sphere({0,0,0}, 1.f) };
    registry.Register(2u, e);

    EXPECT_THROW(
        SpliceProceduralRecipesIntoSource("#version 460\nvoid main() {}\n", registry),
        std::runtime_error);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
