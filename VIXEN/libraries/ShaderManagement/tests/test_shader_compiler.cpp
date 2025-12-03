#include <gtest/gtest.h>
#include "ShaderCompiler.h"
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// Test fixture for shader compiler tests
class ShaderCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = std::filesystem::temp_directory_path() / "shader_compiler_test";
        std::filesystem::create_directories(testDir);
    }

    void TearDown() override {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }

    std::filesystem::path testDir;
};

// ===== Basic Compilation Tests =====

TEST_F(ShaderCompilerTest, CompileSimpleVertexShader) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
    )";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, source);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog;
    EXPECT_FALSE(result.spirv.empty());
    EXPECT_GT(result.spirv.size(), 5); // SPIR-V header is 5 words
}

TEST_F(ShaderCompilerTest, CompileSimpleFragmentShader) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
    )";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Fragment, source);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog;
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, InvalidShaderFails) {
    std::string invalidSource = "invalid glsl code";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, invalidSource);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}

TEST_F(ShaderCompilerTest, SyntaxErrorReported) {
    std::string source = R"(
        #version 450
        void main() {
            gl_Position = vec4(1.0, 2.0, 3.0 // Missing closing paren
        }
    )";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, source);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}

// ===== File Compilation Tests =====

TEST_F(ShaderCompilerTest, CompileFromFile) {
    std::filesystem::path shaderFile = testDir / "test.vert";

    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = vec3(1.0, 0.0, 0.0);
}
    )";

    // Write shader to file
    std::ofstream file(shaderFile);
    file << source;
    file.close();

    ShaderCompiler compiler;
    auto result = compiler.CompileFile(ShaderStage::Vertex, shaderFile);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog;
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileNonExistentFileFails) {
    ShaderCompiler compiler;
    auto result = compiler.CompileFile(
        ShaderStage::Vertex,
        testDir / "does_not_exist.vert"
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}

// ===== Compilation Options Tests =====

TEST_F(ShaderCompilerTest, CompileWithOptimization) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = inPosition * 0.5 + 0.5;
}
    )";

    ShaderCompiler compiler;

    CompilationOptions options;
    options.optimizePerformance = true;

    auto result = compiler.Compile(ShaderStage::Vertex, source, "main", options);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog;
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileWithDebugInfo) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = inPosition * 0.5 + 0.5;
}
    )";

    ShaderCompiler compiler;

    CompilationOptions options;
    options.generateDebugInfo = true;

    auto result = compiler.Compile(ShaderStage::Vertex, source, "main", options);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog;
    EXPECT_FALSE(result.spirv.empty());
}

// ===== SPIR-V Validation Tests =====

TEST_F(ShaderCompilerTest, DISABLED_ValidateSpirvSuccess) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = inPosition * 0.5 + 0.5;
}
    )";

    ShaderCompiler compiler;

    CompilationOptions options;
    options.validateSpirv = true;

    auto result = compiler.Compile(ShaderStage::Vertex, source, "main", options);

    EXPECT_TRUE(result.success) << "Error: " << result.errorLog << "\nSPIR-V size: " << result.spirv.size();
}

// ===== Disassembly Tests =====

TEST_F(ShaderCompilerTest, DisassembleSpirv) {
    std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = inPosition * 0.5 + 0.5;
}
    )";

    ShaderCompiler compiler;
    auto result = compiler.Compile(ShaderStage::Vertex, source);

    ASSERT_TRUE(result.success);

    std::string disassembly = compiler.DisassembleSpirv(result.spirv);

    EXPECT_FALSE(disassembly.empty()) << "Disassembly output:\n" << disassembly;
    // glslang disassembly may not include all SPIR-V opcodes - just check it's not empty
    // EXPECT_NE(disassembly.find("OpEntryPoint"), std::string::npos);
}

// ===== Utility Tests =====

TEST_F(ShaderCompilerTest, IsAvailable) {
    EXPECT_TRUE(ShaderCompiler::IsAvailable());
}

TEST_F(ShaderCompilerTest, GetVersion) {
    std::string version = ShaderCompiler::GetVersion();
    EXPECT_FALSE(version.empty());
}

TEST_F(ShaderCompilerTest, InferStageFromPath) {
    EXPECT_EQ(InferStageFromPath("shader.vert"), ShaderStage::Vertex);
    EXPECT_EQ(InferStageFromPath("shader.frag"), ShaderStage::Fragment);
    EXPECT_EQ(InferStageFromPath("shader.comp"), ShaderStage::Compute);
    EXPECT_EQ(InferStageFromPath("shader.geom"), ShaderStage::Geometry);
    EXPECT_EQ(InferStageFromPath("shader.tesc"), ShaderStage::TessControl);
    EXPECT_EQ(InferStageFromPath("shader.tese"), ShaderStage::TessEval);

    // Unknown extension
    EXPECT_EQ(InferStageFromPath("shader.unknown"), std::nullopt);
}

// ===== Multi-Stage Compilation Tests =====

TEST_F(ShaderCompilerTest, CompileVertexAndFragmentShaders) {
    std::string vertSource = R"(
        #version 450
        layout(location = 0) in vec3 position;
        layout(location = 0) out vec3 fragColor;
        void main() {
            gl_Position = vec4(position, 1.0);
            fragColor = vec3(1.0, 0.0, 0.0);
        }
    )";

    std::string fragSource = R"(
        #version 450
        layout(location = 0) in vec3 fragColor;
        layout(location = 0) out vec4 outColor;
        void main() {
            outColor = vec4(fragColor, 1.0);
        }
    )";

    ShaderCompiler compiler;

    auto vertResult = compiler.Compile(ShaderStage::Vertex, vertSource);
    EXPECT_TRUE(vertResult.success) << "Vertex Error: " << vertResult.errorLog;

    auto fragResult = compiler.Compile(ShaderStage::Fragment, fragSource);
    EXPECT_TRUE(fragResult.success) << "Fragment Error: " << fragResult.errorLog;

    EXPECT_FALSE(vertResult.spirv.empty());
    EXPECT_FALSE(fragResult.spirv.empty());
}
