/**
 * @file test_sampling_compile_gate.cpp
 * @brief SPIR-V compile gate: shaders/Sampling.glsl (Sampled Lighting Inc0 M4 VNDF
 *        sampler) must #include and compile cleanly through the real runtime shader
 *        pipeline (ShaderManagement::ShaderBundleBuilder -> ShaderPreprocessor ->
 *        ShaderCompiler/glslang), the same path the live app uses for every .comp
 *        shader (see shaders/BodyInstanceRayMarch.comp's load site).
 *
 * Sampling.glsl is not consumed by any live shader yet, so this compiles a minimal
 * standalone compute kernel that #includes it and calls every public entry point,
 * proving the file is syntactically valid GLSL and its #include resolves via the
 * same AddIncludePath(VIXEN_SHADER_SOURCE_DIR) wiring BuildRenderGraph.cpp uses.
 * Pure CPU (glslang compilation only) -- no Vulkan device, no live app.
 */

#include <gtest/gtest.h>

#include "ShaderBundleBuilder.h"

#ifndef VIXEN_SHADER_SOURCE_DIR
#error "VIXEN_SHADER_SOURCE_DIR must be defined by CMake"
#endif

using namespace ShaderManagement;

namespace {

// Minimal compute kernel that #includes Sampling.glsl and touches every public
// entry point, so a signature typo or missing symbol fails compilation here
// rather than silently at first real use in Inc3/Inc5.
constexpr const char* kSamplingProbeKernelSource = R"(
#version 450

#include "Sampling.glsl"

layout(local_size_x = 1) in;

layout(binding = 0) buffer ProbeOutput {
    vec4 result;
} probeOutput;

void main() {
    vec3 N = vec3(0.0, 0.0, 1.0);
    vec3 tangent, bitangent;
    buildOrthonormalBasis(N, tangent, bitangent);

    vec3 Ve_world = normalize(vec3(0.3, 0.2, 0.9));
    vec3 Ve = toTangentSpace(Ve_world, N, tangent, bitangent);

    float alpha = 0.25;
    vec3 Ne = sampleGGXVNDF(Ve, alpha, 0.37, 0.81);
    float pdf = vndfPdf(Ve, Ne, alpha);

    vec3 Ne_world = fromTangentSpace(Ne, N, tangent, bitangent);

    probeOutput.result = vec4(Ne_world, pdf);
}
)";

}  // namespace

TEST(SamplingCompileGate, IncludesAndCompilesToValidSpirv) {
    ShaderBundleBuilder builder;
    builder.SetProgramName("SamplingProbeKernel")
        .SetPipelineType(PipelineTypeConstraint::Compute)
        .AddIncludePath(std::filesystem::path(VIXEN_SHADER_SOURCE_DIR))
        .AddStage(ShaderStage::Compute, kSamplingProbeKernelSource)
        .EnableSdiGeneration(false);

    ShaderBundleBuilder::BuildResult result = builder.Build();

    ASSERT_TRUE(result.success) << "Sampling.glsl probe kernel failed to build: " << result.errorMessage;
    ASSERT_NE(result.bundle, nullptr);

    const std::vector<uint32_t> spirv = result.bundle->GetSpirv(ShaderStage::Compute);
    EXPECT_FALSE(spirv.empty()) << "Compiled SPIR-V for the Sampling.glsl probe kernel is empty";
    EXPECT_GT(spirv.size(), 5u) << "SPIR-V header alone is 5 words; a real kernel must be larger";

    // SPIR-V module magic number sanity check (0x07230203).
    ASSERT_GE(spirv.size(), 1u);
    EXPECT_EQ(spirv[0], 0x07230203u) << "First word of compiled SPIR-V is not the module magic number";
}
