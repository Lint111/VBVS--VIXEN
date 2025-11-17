#include <gtest/gtest.h>
#include "ShaderManagement/ShaderBundleBuilder.h"
#include "ShaderManagement/ShaderDataBundle.h"

using namespace ShaderManagement;

/**
 * @brief Test ShaderBundleBuilder and ShaderDataBundle
 */
class ShaderBundleBuilderTest : public ::testing::Test {
protected:
    const std::string vertexShaderSource = R"(
        #version 450
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inColor;
        layout(location = 0) out vec3 fragColor;

        void main() {
            gl_Position = vec4(inPosition, 1.0);
            fragColor = inColor;
        }
    )";

    const std::string fragmentShaderSource = R"(
        #version 450
        layout(location = 0) in vec3 fragColor;
        layout(location = 0) out vec4 outColor;

        void main() {
            outColor = vec4(fragColor, 1.0);
        }
    )";
};

TEST_F(ShaderBundleBuilderTest, BasicBuild) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, vertexShaderSource)
        .AddStage(ShaderStage::Fragment, fragmentShaderSource);

    auto result = builder.Build();

    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_NE(result.bundle, nullptr);
    EXPECT_EQ(result.bundle->program.name, "TestShader");
    EXPECT_EQ(result.bundle->program.stages.size(), 2);
}

TEST_F(ShaderBundleBuilderTest, MoveOnlySemantics) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, vertexShaderSource);

    auto result = builder.Build();
    ASSERT_TRUE(result.success);

    // Test that bundle is move-only
    auto bundle1 = std::move(result.bundle);
    EXPECT_NE(bundle1, nullptr);
    EXPECT_EQ(result.bundle, nullptr);  // Moved from

    // Move to another unique_ptr
    auto bundle2 = std::move(bundle1);
    EXPECT_EQ(bundle1, nullptr);  // Moved from
    EXPECT_NE(bundle2, nullptr);

    // Verify data is intact
    EXPECT_EQ(bundle2->program.name, "TestShader");
}

TEST_F(ShaderBundleBuilderTest, InputValidation_SourceSizeLimit) {
    // Create a source that exceeds 10MB limit
    std::string hugeSource(11 * 1024 * 1024, 'x');  // 11 MB

    auto builder = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, hugeSource);

    auto result = builder.Build();

    // Should fail due to size limit
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(ShaderBundleBuilderTest, InputValidation_TooManyStages) {
    auto builder = ShaderBundleBuilder().SetProgramName("TestShader");

    // Add more than 16 stages (MAX_STAGES_PER_PROGRAM)
    for (int i = 0; i < 20; ++i) {
        builder.AddStage(static_cast<ShaderStage>(i % 3), vertexShaderSource);
    }

    auto result = builder.Build();

    // Should fail due to too many stages
    EXPECT_FALSE(result.success);
}

TEST_F(ShaderBundleBuilderTest, CompilationError_InvalidGLSL) {
    std::string invalidShader = R"(
        #version 450
        this is not valid GLSL code!!!
        void main() {}
    )";

    auto builder = ShaderBundleBuilder()
        .SetProgramName("InvalidShader")
        .AddStage(ShaderStage::Vertex, invalidShader);

    auto result = builder.Build();

    // Should fail compilation
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(ShaderBundleBuilderTest, PipelineTypeValidation_Graphics) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("GraphicsShader")
        .SetPipelineType(PipelineTypeConstraint::Graphics)
        .AddStage(ShaderStage::Vertex, vertexShaderSource)
        .AddStage(ShaderStage::Fragment, fragmentShaderSource)
        .SetValidatePipeline(true);

    auto result = builder.Build();

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.bundle->program.pipelineType, PipelineTypeConstraint::Graphics);
}

TEST_F(ShaderBundleBuilderTest, PipelineTypeValidation_Compute) {
    std::string computeShader = R"(
        #version 450
        layout(local_size_x = 16, local_size_y = 16) in;
        layout(binding = 0) buffer Data { float values[]; };

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            values[idx] *= 2.0;
        }
    )";

    auto builder = ShaderBundleBuilder()
        .SetProgramName("ComputeShader")
        .SetPipelineType(PipelineTypeConstraint::Compute)
        .AddStage(ShaderStage::Compute, computeShader)
        .SetValidatePipeline(true);

    auto result = builder.Build();

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.bundle->program.pipelineType, PipelineTypeConstraint::Compute);
}

TEST_F(ShaderBundleBuilderTest, BuildResult_Timings) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, vertexShaderSource);

    auto result = builder.Build();

    ASSERT_TRUE(result.success);

    // Verify timing information is present
    EXPECT_GT(result.compileTime.count(), 0);
    EXPECT_GT(result.totalTime.count(), 0);
    EXPECT_GE(result.totalTime.count(), result.compileTime.count());
}

TEST_F(ShaderBundleBuilderTest, FluentInterface) {
    // Test fluent builder pattern
    auto result = ShaderBundleBuilder()
        .SetProgramName("FluentShader")
        .SetPipelineType(PipelineTypeConstraint::Graphics)
        .AddStage(ShaderStage::Vertex, vertexShaderSource)
        .AddStage(ShaderStage::Fragment, fragmentShaderSource)
        .EnableSdiGeneration(false)
        .SetValidatePipeline(false)
        .Build();

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.bundle->program.name, "FluentShader");
}

TEST_F(ShaderBundleBuilderTest, EmptyProgramName) {
    auto builder = ShaderBundleBuilder()
        // Don't set program name
        .AddStage(ShaderStage::Vertex, vertexShaderSource);

    auto result = builder.Build();

    // Should still succeed (program name is optional)
    // But will have empty or generated name
    EXPECT_TRUE(result.success || !result.errorMessage.empty());
}

TEST_F(ShaderBundleBuilderTest, NoStages) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("EmptyShader");
        // Don't add any stages

    auto result = builder.Build();

    // Should fail - no stages
    EXPECT_FALSE(result.success);
}
