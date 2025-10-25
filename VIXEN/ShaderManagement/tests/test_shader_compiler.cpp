#include <gtest/gtest.h>
#include "ShaderManagement/ShaderCompiler.h"

using namespace ShaderManagement;

// Basic compiler tests
TEST(ShaderCompilerTest, CompileSimpleVertexShader) {
    std::string source = R"(
        #version 450
        layout(location = 0) in vec3 position;
        void main() {
            gl_Position = vec4(position, 1.0);
        }
    )";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, source);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST(ShaderCompilerTest, InvalidShaderFails) {
    std::string invalidSource = "invalid glsl code";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, invalidSource);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}
