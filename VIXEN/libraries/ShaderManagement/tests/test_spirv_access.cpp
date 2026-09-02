// Semantic Shader Wiring S3 sub-slice 1 — per-binding access from SPIR-V.
// (docs/plans/2026-08-03-semantic-shader-wiring.md, undertow repo, S3 entry)
//
// The reflector derives each binding's access mode: storage-kind descriptors
// from their NonWritable/NonReadable decorations, inherently-read-only kinds
// (UBO, sampled image, sampler...) normalized to ReadOnly. This is the
// prerequisite for DERIVING sync/hazard sets instead of hand-feeding them.
//
// Real glslang compile + real reflection, CPU-side (no Vulkan device) — same
// dependency shape as test_shader_family.

#include <gtest/gtest.h>
#include "SpirvReflector.h"
#include "ShaderCompiler.h"
#include "ShaderProgram.h"

#include <map>

using namespace ShaderManagement;

TEST(SpirvAccess, ReflectsAccessQualifiers) {
    std::string source = R"(
        #version 450
        layout(local_size_x = 1) in;
        layout(set = 0, binding = 0) uniform Config { vec4 params; } cfg;
        layout(set = 0, binding = 1) readonly buffer InBuf { float src[]; };
        layout(set = 0, binding = 2) writeonly buffer OutBuf { float dst[]; };
        layout(set = 0, binding = 3) buffer InOutBuf { float acc[]; };
        layout(set = 0, binding = 4, r32f) writeonly uniform image2D outImg;
        void main() {
            dst[0] = src[0] * cfg.params.x;
            acc[0] += src[0];
            imageStore(outImg, ivec2(0), vec4(acc[0]));
        }
    )";

    ShaderCompiler compiler;
    auto compileResult = compiler.Compile(ShaderStage::Compute, source);
    ASSERT_TRUE(compileResult.success);

    CompiledProgram program;
    program.programId = 0;
    program.name = "AccessProbe";
    program.pipelineType = PipelineTypeConstraint::Compute;
    CompiledShaderStage stage;
    stage.stage = ShaderStage::Compute;
    stage.spirvCode = compileResult.spirv;
    stage.entryPoint = "main";
    program.stages.push_back(std::move(stage));

    SpirvReflector reflector;
    auto reflection = reflector.Reflect(program);
    ASSERT_TRUE(reflection);
    const auto& data = *reflection;
    ASSERT_FALSE(data.descriptorSets.empty());

    std::map<uint32_t, SpirvResourceAccess> accessByBinding;
    for (const auto& [setIdx, bindings] : data.descriptorSets)
        for (const auto& b : bindings) accessByBinding[b.binding] = b.access;

    ASSERT_EQ(accessByBinding.size(), 5u);
    EXPECT_EQ(accessByBinding.at(0), SpirvResourceAccess::ReadOnly);   // UBO: inherent
    EXPECT_EQ(accessByBinding.at(1), SpirvResourceAccess::ReadOnly);   // readonly SSBO
    EXPECT_EQ(accessByBinding.at(2), SpirvResourceAccess::WriteOnly);  // writeonly SSBO
    EXPECT_EQ(accessByBinding.at(3), SpirvResourceAccess::ReadWrite);  // plain SSBO
    EXPECT_EQ(accessByBinding.at(4), SpirvResourceAccess::WriteOnly);  // writeonly image
}

TEST(SpirvAccess, UsesBufferBlockInstanceNameWhenDeclared) {
    std::string source = R"(
        #version 450
        layout(local_size_x = 1) in;
        layout(set = 0, binding = 0) buffer NamedBlock {
            float values[];
        } namedInstance;
        void main() {
            namedInstance.values[0] = 1.0;
        }
    )";

    ShaderCompiler compiler;
    auto compileResult = compiler.Compile(ShaderStage::Compute, source);
    ASSERT_TRUE(compileResult.success);

    CompiledProgram program;
    program.programId = 0;
    program.name = "BufferBlockInstanceNameProbe";
    program.pipelineType = PipelineTypeConstraint::Compute;
    CompiledShaderStage stage;
    stage.stage = ShaderStage::Compute;
    stage.spirvCode = compileResult.spirv;
    stage.entryPoint = "main";
    program.stages.push_back(std::move(stage));

    SpirvReflector reflector;
    auto reflection = reflector.Reflect(program);
    ASSERT_TRUE(reflection);
    ASSERT_EQ(reflection->descriptorSets.size(), 1u);
    ASSERT_EQ(reflection->descriptorSets.at(0).size(), 1u);
    EXPECT_EQ(reflection->descriptorSets.at(0).front().name, "namedInstance");
}
