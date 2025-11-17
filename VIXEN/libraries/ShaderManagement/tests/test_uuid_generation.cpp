#include <gtest/gtest.h>
#include "ShaderBundleBuilder.h"

using namespace ShaderManagement;

/**
 * @brief Test deterministic UUID generation
 *
 * Same shader content should produce same UUID across builds
 */
class UuidGenerationTest : public ::testing::Test {
protected:
    const std::string testShaderSource = R"(
        #version 450
        layout(location = 0) in vec3 position;
        void main() {
            gl_Position = vec4(position, 1.0);
        }
    )";
};

TEST_F(UuidGenerationTest, SameSourceProducesSameUuid) {
    // Build first bundle
    auto builder1 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource);

    auto result1 = builder1.Build();
    ASSERT_TRUE(result1.success) << result1.errorMessage;

    // Build second bundle with identical source
    auto builder2 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource);

    auto result2 = builder2.Build();
    ASSERT_TRUE(result2.success) << result2.errorMessage;

    // UUIDs should match (content-based hashing)
    EXPECT_EQ(result1.bundle->uuid, result2.bundle->uuid);
}

TEST_F(UuidGenerationTest, DifferentSourceProducesDifferentUuid) {
    auto builder1 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource);

    auto result1 = builder1.Build();
    ASSERT_TRUE(result1.success);

    // Different source
    std::string differentSource = R"(
        #version 450
        layout(location = 0) in vec2 position;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";

    auto builder2 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, differentSource);

    auto result2 = builder2.Build();
    ASSERT_TRUE(result2.success);

    // UUIDs should differ
    EXPECT_NE(result1.bundle->uuid, result2.bundle->uuid);
}

TEST_F(UuidGenerationTest, DifferentOptionsProduceDifferentUuid) {
    CompilationOptions options1;
    options1.optimizePerformance = true;

    CompilationOptions options2;
    options2.optimizePerformance = false;

    auto builder1 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource, "main", options1);

    auto result1 = builder1.Build();
    ASSERT_TRUE(result1.success);

    auto builder2 = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource, "main", options2);

    auto result2 = builder2.Build();
    ASSERT_TRUE(result2.success);

    // UUIDs should differ (different compilation options)
    EXPECT_NE(result1.bundle->uuid, result2.bundle->uuid);
}

TEST_F(UuidGenerationTest, UuidIsValid32CharHex) {
    auto builder = ShaderBundleBuilder()
        .SetProgramName("TestShader")
        .AddStage(ShaderStage::Vertex, testShaderSource);

    auto result = builder.Build();
    ASSERT_TRUE(result.success);

    // UUID should be 32 hex characters
    EXPECT_EQ(result.bundle->uuid.length(), 32);

    // All characters should be hex
    for (char c : result.bundle->uuid) {
        EXPECT_TRUE(std::isxdigit(c)) << "Invalid character in UUID: " << c;
    }
}
