/**
 * @file test_shader_compiler.cpp
 * @brief Unit tests for ShaderCompiler class
 */

#include <gtest/gtest.h>
#include <ShaderManagement/ShaderCompiler.h>
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// ============================================================================
// Test Fixture
// ============================================================================

class ShaderCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for shader files
        testShaderDir = std::filesystem::temp_directory_path() / "shader_compiler_test";
        std::filesystem::create_directories(testShaderDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(testShaderDir);
    }

    // Helper to create a shader file
    void CreateShaderFile(const std::string& filename, const std::string& content) {
        std::filesystem::path path = testShaderDir / filename;
        std::ofstream file(path);
        file << content;
        file.close();
    }

    // Simple valid vertex shader
    static constexpr const char* VALID_VERTEX_SHADER = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outTexCoord = inTexCoord;
}
)";

    // Simple valid fragment shader
    static constexpr const char* VALID_FRAGMENT_SHADER = R"(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inTexCoord, 0.0, 1.0);
}
)";

    // Simple valid compute shader
    static constexpr const char* VALID_COMPUTE_SHADER = R"(
#version 450

layout (local_size_x = 16, local_size_y = 16) in;
layout (binding = 0, rgba8) uniform image2D outputImage;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    vec4 color = vec4(1.0, 0.0, 0.0, 1.0);
    imageStore(outputImage, pos, color);
}
)";

    std::filesystem::path testShaderDir;
};

// ============================================================================
// Construction and Availability Tests
// ============================================================================

TEST_F(ShaderCompilerTest, Construction) {
    ShaderCompiler compiler;
    // Should construct successfully
    SUCCEED();
}

TEST_F(ShaderCompilerTest, IsAvailable) {
    bool available = ShaderCompiler::IsAvailable();
    // glslang should be available in our build
    EXPECT_TRUE(available);
}

TEST_F(ShaderCompilerTest, GetVersion) {
    std::string version = ShaderCompiler::GetVersion();
    EXPECT_FALSE(version.empty());
}

// ============================================================================
// Basic Compilation Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CompileVertexShader) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
    EXPECT_GT(result.spirv.size(), 0);
}

TEST_F(ShaderCompilerTest, CompileFragmentShader) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Fragment,
        VALID_FRAGMENT_SHADER,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileComputeShader) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Compute,
        VALID_COMPUTE_SHADER,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

// ============================================================================
// Compilation Options Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CompileWithOptimization) {
    ShaderCompiler compiler;

    CompilationOptions opts;
    opts.optimizePerformance = true;
    opts.optimizeSize = false;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main",
        opts
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileWithDebugInfo) {
    ShaderCompiler compiler;

    CompilationOptions opts;
    opts.generateDebugInfo = true;

    auto result = compiler.Compile(
        ShaderStage::Fragment,
        VALID_FRAGMENT_SHADER,
        "main",
        opts
    );

    ASSERT_TRUE(result.success);
    // Debug info typically increases SPIR-V size
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileWithoutOptimization) {
    ShaderCompiler compiler;

    CompilationOptions opts;
    opts.optimizePerformance = false;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main",
        opts
    );

    ASSERT_TRUE(result.success);
    // Unoptimized code may be larger
    EXPECT_FALSE(result.spirv.empty());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CompileInvalidShader) {
    ShaderCompiler compiler;

    const char* invalidShader = R"(
#version 450
void main() {
    undefined_function();  // Syntax error
}
)";

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        invalidShader,
        "main"
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}

TEST_F(ShaderCompilerTest, CompileSyntaxError) {
    ShaderCompiler compiler;

    const char* syntaxError = R"(
#version 450
void main() {
    vec3 v = vec3(1.0, 2.0;  // Missing closing parenthesis
}
)";

    auto result = compiler.Compile(
        ShaderStage::Fragment,
        syntaxError,
        "main"
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorLog.empty());
}

TEST_F(ShaderCompilerTest, CompileMissingEntryPoint) {
    ShaderCompiler compiler;

    const char* noMain = R"(
#version 450
void someFunction() {}
)";

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        noMain,
        "main"  // Entry point doesn't exist
    );

    EXPECT_FALSE(result.success);
}

TEST_F(ShaderCompilerTest, CompileEmptySource) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        "",
        "main"
    );

    EXPECT_FALSE(result.success);
}

// ============================================================================
// Custom Entry Point Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CustomEntryPoint) {
    ShaderCompiler compiler;

    const char* customEntryShader = R"(
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec4 outColor;

void customMain() {
    gl_Position = vec4(inPos, 1.0);
    outColor = vec4(1.0);
}
)";

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        customEntryShader,
        "customMain"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

// ============================================================================
// File Compilation Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CompileFromFile) {
    CreateShaderFile("test.vert", VALID_VERTEX_SHADER);

    ShaderCompiler compiler;

    auto filePath = testShaderDir / "test.vert";
    auto result = compiler.CompileFile(
        ShaderStage::Vertex,
        filePath,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileFromNonexistentFile) {
    ShaderCompiler compiler;

    auto result = compiler.CompileFile(
        ShaderStage::Vertex,
        "/nonexistent/shader.vert",
        "main"
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

// ============================================================================
// SPIR-V Loading Tests
// ============================================================================

TEST_F(ShaderCompilerTest, LoadSpirvFile) {
    ShaderCompiler compiler;

    // First, compile a shader to get valid SPIR-V
    auto compileResult = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main"
    );
    ASSERT_TRUE(compileResult.success);

    // Save SPIR-V to file
    auto spirvPath = testShaderDir / "test.spv";
    std::ofstream file(spirvPath, std::ios::binary);
    file.write(
        reinterpret_cast<const char*>(compileResult.spirv.data()),
        compileResult.spirv.size() * sizeof(uint32_t)
    );
    file.close();

    // Load SPIR-V back
    auto loadResult = compiler.LoadSpirv(spirvPath, true);
    ASSERT_TRUE(loadResult.success);
    EXPECT_EQ(loadResult.spirv.size(), compileResult.spirv.size());
}

TEST_F(ShaderCompilerTest, LoadInvalidSpirvFile) {
    ShaderCompiler compiler;

    // Create invalid SPIR-V file
    auto spirvPath = testShaderDir / "invalid.spv";
    std::ofstream file(spirvPath, std::ios::binary);
    uint32_t invalidData[] = {0xDEADBEEF, 0xCAFEBABE};
    file.write(reinterpret_cast<const char*>(invalidData), sizeof(invalidData));
    file.close();

    auto result = compiler.LoadSpirv(spirvPath, true);
    // Should either fail or validation should catch the invalid SPIR-V
    EXPECT_FALSE(result.success);
}

// ============================================================================
// SPIR-V Validation Tests
// ============================================================================

TEST_F(ShaderCompilerTest, ValidateSpirvValid) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main"
    );
    ASSERT_TRUE(result.success);

    std::string error;
    bool valid = compiler.ValidateSpirv(result.spirv, error);
    EXPECT_TRUE(valid);
    EXPECT_TRUE(error.empty());
}

TEST_F(ShaderCompilerTest, ValidateSpirvInvalid) {
    ShaderCompiler compiler;

    std::vector<uint32_t> invalidSpirv = {0xDEADBEEF, 0xCAFEBABE};

    std::string error;
    bool valid = compiler.ValidateSpirv(invalidSpirv, error);
    EXPECT_FALSE(valid);
    EXPECT_FALSE(error.empty());
}

TEST_F(ShaderCompilerTest, ValidateSpirvEmpty) {
    ShaderCompiler compiler;

    std::vector<uint32_t> emptySpirv;

    std::string error;
    bool valid = compiler.ValidateSpirv(emptySpirv, error);
    EXPECT_FALSE(valid);
}

// ============================================================================
// SPIR-V Disassembly Tests
// ============================================================================

TEST_F(ShaderCompilerTest, DisassembleSpirv) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        VALID_VERTEX_SHADER,
        "main"
    );
    ASSERT_TRUE(result.success);

    std::string disassembly = compiler.DisassembleSpirv(result.spirv);
    EXPECT_FALSE(disassembly.empty());
    // Disassembly should contain SPIR-V assembly instructions
    EXPECT_NE(disassembly.find("OpCapability"), std::string::npos);
}

TEST_F(ShaderCompilerTest, DisassembleEmptySpirv) {
    ShaderCompiler compiler;

    std::vector<uint32_t> emptySpirv;
    std::string disassembly = compiler.DisassembleSpirv(emptySpirv);

    // Should either be empty or contain an error message
    EXPECT_TRUE(disassembly.empty() || disassembly.find("error") != std::string::npos);
}

// ============================================================================
// Compilation Time Tracking
// ============================================================================

TEST_F(ShaderCompilerTest, CompilationTimeTracking) {
    ShaderCompiler compiler;

    auto result = compiler.Compile(
        ShaderStage::Fragment,
        VALID_FRAGMENT_SHADER,
        "main"
    );

    ASSERT_TRUE(result.success);
    // Compilation should take some measurable time
    EXPECT_GT(result.compilationTime.count(), 0);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(GetShaderStageExtensionTest, AllStages) {
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Vertex), "vert");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Fragment), "frag");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Compute), "comp");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Geometry), "geom");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::TessControl), "tesc");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::TessEval), "tese");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Mesh), "mesh");
    EXPECT_STREQ(GetShaderStageExtension(ShaderStage::Task), "task");
}

TEST(InferStageFromPathTest, VertexShader) {
    auto stage = InferStageFromPath("/path/to/shader.vert");
    ASSERT_TRUE(stage.has_value());
    EXPECT_EQ(*stage, ShaderStage::Vertex);
}

TEST(InferStageFromPathTest, FragmentShader) {
    auto stage = InferStageFromPath("/path/to/shader.frag");
    ASSERT_TRUE(stage.has_value());
    EXPECT_EQ(*stage, ShaderStage::Fragment);
}

TEST(InferStageFromPathTest, ComputeShader) {
    auto stage = InferStageFromPath("shader.comp");
    ASSERT_TRUE(stage.has_value());
    EXPECT_EQ(*stage, ShaderStage::Compute);
}

TEST(InferStageFromPathTest, UnknownExtension) {
    auto stage = InferStageFromPath("shader.txt");
    EXPECT_FALSE(stage.has_value());
}

TEST(InferStageFromPathTest, NoExtension) {
    auto stage = InferStageFromPath("shader");
    EXPECT_FALSE(stage.has_value());
}

// ============================================================================
// Complex Shader Tests
// ============================================================================

TEST_F(ShaderCompilerTest, CompileShaderWithUniforms) {
    ShaderCompiler compiler;

    const char* shaderWithUniforms = R"(
#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inPosition;
}
)";

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        shaderWithUniforms,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileShaderWithSamplers) {
    ShaderCompiler compiler;

    const char* shaderWithSamplers = R"(
#version 450

layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, inTexCoord);
}
)";

    auto result = compiler.Compile(
        ShaderStage::Fragment,
        shaderWithSamplers,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}

TEST_F(ShaderCompilerTest, CompileShaderWithPushConstants) {
    ShaderCompiler compiler;

    const char* shaderWithPushConstants = R"(
#version 450

layout(push_constant) uniform PushConstants {
    vec4 color;
    float time;
} pushConstants;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = vec4(0.0);
    outColor = pushConstants.color * pushConstants.time;
}
)";

    auto result = compiler.Compile(
        ShaderStage::Vertex,
        shaderWithPushConstants,
        "main"
    );

    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.spirv.empty());
}
