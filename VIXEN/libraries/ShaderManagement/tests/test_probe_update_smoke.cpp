// Sampled Lighting Inc4 M1: proves SceneBindings.glsl's scene-binding chain
// (+ TraceWorld/TraceWorldShadow) compiles from a call site distinct from
// BodyInstanceRayMarch.comp/DirectLighting.comp/SpatialReuseShade.comp, via
// the real ShaderBundleBuilder/glslang pipeline (with #include resolution) --
// the same builder shader_tool uses for real bundles. Separate executable so
// it has no Vulkan device dependency, mirroring test_hlsl_ingestion.cpp.
#include <gtest/gtest.h>
#include <filesystem>

#include "ShaderBundleBuilder.h"

using namespace ShaderManagement;

TEST(ProbeUpdateSmokeTest, SceneBindingsChainCompilesFromNewCallSite) {
    ShaderBundleBuilder builder;
    builder.SetProgramName("ProbeUpdateSmokeTest")
        .SetPipelineType(PipelineTypeConstraint::Compute)
        .EnableSdiGeneration(false)
        .AddIncludePath(VIXEN_SHADERS_DIR)
        .AddStageFromFile(ShaderStage::Compute,
            std::filesystem::path(VIXEN_SHADERS_DIR) / "ProbeUpdateSmokeTest.comp");

    auto result = builder.Build();
    EXPECT_TRUE(result.success) << result.errorMessage;
}
