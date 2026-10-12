#include <gtest/gtest.h>
#include "SPIRVReflection.h"
#include "ShaderCompiler.h"
#include "ShaderProgram.h"

using namespace ShaderManagement;

TEST(SpirvDescriptorLayout, RejectsMultiSetProgramReusingBindingIndex) {
    std::string source = R"(
        #version 450
        layout(local_size_x = 1) in;
        layout(set = 0, binding = 0) buffer A { float a[]; };
        layout(set = 1, binding = 0) buffer B { float b[]; };
        void main() {
            a[0] = b[0];
        }
    )";

    ShaderCompiler compiler;
    auto compileResult = compiler.Compile(ShaderStage::Compute, source);
    ASSERT_TRUE(compileResult.success);

    CompiledProgram program;
    program.programId = 0;
    program.name = "MultiSetProbe";
    program.pipelineType = PipelineTypeConstraint::Compute;
    CompiledShaderStage stage;
    stage.stage = ShaderStage::Compute;
    stage.spirvCode = compileResult.spirv;
    stage.entryPoint = "main";
    program.stages.push_back(std::move(stage));

    EXPECT_FALSE(ReflectDescriptorLayout(program));
}
