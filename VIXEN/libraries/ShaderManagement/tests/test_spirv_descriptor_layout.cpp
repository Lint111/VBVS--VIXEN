#include <gtest/gtest.h>
#include "SPIRVReflection.h"
#include "SdiVariantMerge.h"
#include "SpirvReflector.h"
#include "ShaderCompiler.h"
#include "ShaderProgram.h"

#include <algorithm>

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

namespace {

SpirvReflectionData ReflectFeatureVariant(bool traceHooks) {
    std::string source = "#version 450\n";
    if (traceHooks) source += "#define VIXEN_GPU_TRACE_HOOKS\n";
    source += R"(
        layout(local_size_x = 1) in;
        layout(std430, binding = 0) buffer Out { float outValues[]; };
        layout(std430, binding = 1) readonly buffer In { float inValues[]; };
        #ifdef VIXEN_GPU_TRACE_HOOKS
        layout(std430, binding = 14) writeonly buffer Trace { uint traceCount[]; };
        #endif
        void main() {
            outValues[0] = inValues[0];
        #ifdef VIXEN_GPU_TRACE_HOOKS
            traceCount[0] = 1u;
        #endif
        }
    )";

    ShaderCompiler compiler;
    auto compileResult = compiler.Compile(ShaderStage::Compute, source);
    EXPECT_TRUE(compileResult.success) << compileResult.errorLog;

    CompiledProgram program;
    program.programId = 0;
    program.name = "FeatureProbe";
    program.pipelineType = PipelineTypeConstraint::Compute;
    CompiledShaderStage stage;
    stage.stage = ShaderStage::Compute;
    stage.spirvCode = compileResult.spirv;
    stage.entryPoint = "main";
    program.stages.push_back(std::move(stage));

    SpirvReflector reflector;
    auto reflection = reflector.Reflect(program);
    EXPECT_TRUE(reflection);
    return reflection ? *reflection : SpirvReflectionData{};
}

}  // namespace

TEST(SpirvDescriptorLayout, OptionalFeatureAddsOneBindingToTheSameSet) {
    const auto off = ReflectFeatureVariant(false);
    const auto on = ReflectFeatureVariant(true);

    ASSERT_EQ(off.descriptorSets.size(), 1u);
    ASSERT_EQ(on.descriptorSets.size(), 1u);
    ASSERT_TRUE(off.descriptorSets.count(0));
    ASSERT_TRUE(on.descriptorSets.count(0));
    const auto& offBindings = off.descriptorSets.at(0);
    const auto& onBindings = on.descriptorSets.at(0);
    ASSERT_EQ(onBindings.size(), offBindings.size() + 1);

    for (const auto& b : offBindings) {
        const auto same = std::find_if(onBindings.begin(), onBindings.end(),
            [&](const auto& o) { return o.binding == b.binding; });
        ASSERT_NE(same, onBindings.end()) << "binding " << b.binding << " dropped";
        EXPECT_EQ(same->name, b.name);
    }

    SdiVariant disabled{{}, off};
    SdiVariant enabled{{"VIXEN_GPU_TRACE_HOOKS"}, on};
    auto merged = MergeSdiVariants("FeatureProbe", {disabled, enabled});
    ASSERT_TRUE(merged.success) << merged.errorMessage;
    ASSERT_EQ(merged.merged.bindings.size(), onBindings.size());
    for (const auto& b : merged.merged.bindings) {
        EXPECT_EQ(b.binding.set, 0u);
        EXPECT_EQ(b.requiredFeatures.empty(), b.binding.binding != 14u) << b.binding.name;
    }
}
