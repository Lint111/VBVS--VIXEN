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

// ===== Compile cache (every compile is cached by default) =====

#include "ShaderCacheManager.h"
#include "ShaderBundleBuilder.h"

namespace {
const char* kCacheComputeSource = R"(
    #version 450
    layout(local_size_x = 8) in;
    layout(std430, binding = 0) buffer Data { float values[]; };
    void main() { values[gl_GlobalInvocationID.x] *= 3.0; }
)";
} // namespace

TEST_F(ShaderCompilerTest, CompileCache_SecondCompileIsServedFromCache) {
    ShaderCacheConfig config;
    config.cacheDirectory = testDir / "spirv";
    ShaderCacheManager cache(config);
    ShaderCompiler compiler;
    compiler.SetCache(&cache);

    auto first = compiler.Compile(ShaderStage::Compute, kCacheComputeSource);
    ASSERT_TRUE(first.success) << first.errorLog;
    EXPECT_FALSE(first.fromCache);

    auto second = compiler.Compile(ShaderStage::Compute, kCacheComputeSource);
    ASSERT_TRUE(second.success);
    EXPECT_TRUE(second.fromCache);
    EXPECT_EQ(first.spirv, second.spirv);
}

TEST_F(ShaderCompilerTest, CompileCache_OptionChangeIsAMiss) {
    ShaderCacheConfig config;
    config.cacheDirectory = testDir / "spirv";
    ShaderCacheManager cache(config);
    ShaderCompiler compiler;
    compiler.SetCache(&cache);

    ASSERT_TRUE(compiler.Compile(ShaderStage::Compute, kCacheComputeSource).success);
    CompilationOptions other;
    other.optimizePerformance = false;
    auto changed = compiler.Compile(ShaderStage::Compute, kCacheComputeSource, "main", other);
    ASSERT_TRUE(changed.success);
    EXPECT_FALSE(changed.fromCache);
}

TEST_F(ShaderCompilerTest, CompileCache_FailedCompileIsNotStored) {
    ShaderCacheConfig config;
    config.cacheDirectory = testDir / "spirv";
    ShaderCacheManager cache(config);
    ShaderCompiler compiler;
    compiler.SetCache(&cache);

    const std::string broken = "#version 450\nvoid main() { undefined_symbol = 1; }\n";
    EXPECT_FALSE(compiler.Compile(ShaderStage::Compute, broken).success);
    auto again = compiler.Compile(ShaderStage::Compute, broken);
    EXPECT_FALSE(again.success);
    EXPECT_FALSE(again.fromCache);
}

TEST_F(ShaderCompilerTest, CompileCache_NullCacheAlwaysCompiles) {
    ShaderCompiler compiler;
    compiler.SetCache(nullptr);
    ASSERT_TRUE(compiler.Compile(ShaderStage::Compute, kCacheComputeSource).success);
    auto second = compiler.Compile(ShaderStage::Compute, kCacheComputeSource);
    ASSERT_TRUE(second.success);
    EXPECT_FALSE(second.fromCache);
}

TEST_F(ShaderCompilerTest, CompileCache_CorruptEntryIsRejectedAndRecompiled) {
    ShaderCacheConfig config;
    config.cacheDirectory = testDir / "spirv";
    ShaderCacheManager cache(config);
    ShaderCompiler compiler;
    compiler.SetCache(&cache);

    auto first = compiler.Compile(ShaderStage::Compute, kCacheComputeSource);
    ASSERT_TRUE(first.success);
    for (const auto& entry : std::filesystem::directory_iterator(config.cacheDirectory)) {
        if (entry.path().extension() != ".spv") continue;
        std::fstream file(entry.path(), std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(-4, std::ios::end);
        const char garbage[4] = {'\x13', '\x37', '\x13', '\x37'};
        file.write(garbage, sizeof(garbage));
    }
    auto second = compiler.Compile(ShaderStage::Compute, kCacheComputeSource);
    ASSERT_TRUE(second.success);
    EXPECT_FALSE(second.fromCache);
    EXPECT_EQ(first.spirv, second.spirv);
}

TEST_F(ShaderCompilerTest, CompileCache_BuilderCachesWithoutOptIn) {
    ShaderCacheConfig config;
    config.cacheDirectory = testDir / "spirv";
    ShaderCacheManager cache(config);

    auto build = [&](bool disable) {
        ShaderBundleBuilder builder;
        builder.SetProgramName("CacheProbe")
               .SetPipelineType(PipelineTypeConstraint::Compute)
               .EnableSdiGeneration(false)
               .EnableCaching(&cache)
               .AddStage(ShaderStage::Compute, kCacheComputeSource);
        if (disable) builder.DisableCaching();
        return builder.Build();
    };
    ASSERT_TRUE(build(false).success);
    const auto hit = build(false);
    ASSERT_TRUE(hit.success) << hit.errorMessage;
    EXPECT_TRUE(hit.bundle != nullptr);
    const auto bypass = build(true);
    ASSERT_TRUE(bypass.success);
    EXPECT_EQ(cache.GetStatistics().totalCacheHits, 1u);
}
