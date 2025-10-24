/**
 * @file test_integration.cpp
 * @brief Integration tests for ShaderManagement components working together
 */

#include <gtest/gtest.h>
#include <ShaderManagement/ShaderCacheManager.h>
#include <ShaderManagement/ShaderPreprocessor.h>
#include <ShaderManagement/ShaderCompiler.h>
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// ============================================================================
// Test Fixture
// ============================================================================

class ShaderManagementIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = std::filesystem::temp_directory_path() / "shader_integration_test";
        std::filesystem::create_directories(testDir);

        includeDir = testDir / "includes";
        cacheDir = testDir / "cache";
        std::filesystem::create_directories(includeDir);
        std::filesystem::create_directories(cacheDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir);
    }

    void CreateIncludeFile(const std::string& name, const std::string& content) {
        std::ofstream file(includeDir / name);
        file << content;
        file.close();
    }

    std::filesystem::path testDir;
    std::filesystem::path includeDir;
    std::filesystem::path cacheDir;
};

// ============================================================================
// Full Pipeline Tests
// ============================================================================

TEST_F(ShaderManagementIntegrationTest, PreprocessCompileCache) {
    // Create include files
    CreateIncludeFile("common.glsl", R"(
const float PI = 3.14159;
vec3 gammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}
)");

    // Shader source with include
    std::string source = R"(
#version 450
#include "common.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    #ifdef USE_GAMMA
        outColor = vec4(gammaCorrect(vec3(inTexCoord, 0.0)), 1.0);
    #else
        outColor = vec4(inTexCoord, 0.0, 1.0);
    #endif
}
)";

    // 1. Preprocess
    PreprocessorConfig preprocConfig;
    preprocConfig.includePaths = {includeDir};

    ShaderPreprocessor preprocessor(preprocConfig);
    preprocessor.AddGlobalDefine("USE_GAMMA");

    auto preprocessed = preprocessor.Preprocess(source);
    ASSERT_TRUE(preprocessed.success);
    EXPECT_NE(preprocessed.processedSource.find("vec3 gammaCorrect"), std::string::npos);
    EXPECT_NE(preprocessed.processedSource.find("#define USE_GAMMA"), std::string::npos);

    // 2. Compile
    ShaderCompiler compiler;
    auto compiled = compiler.Compile(
        ShaderStage::Fragment,
        preprocessed.processedSource,
        "main"
    );
    ASSERT_TRUE(compiled.success);
    EXPECT_FALSE(compiled.spirv.empty());

    // 3. Cache
    ShaderCacheConfig cacheConfig;
    cacheConfig.cacheDirectory = cacheDir;

    ShaderCacheManager cache(cacheConfig);

    std::vector<std::pair<std::string, std::string>> defines = {{"USE_GAMMA", ""}};
    std::string cacheKey = GenerateCacheKey(
        preprocessed.processedSource,
        "",
        static_cast<uint32_t>(ShaderStage::Fragment),
        defines,
        "main"
    );

    EXPECT_TRUE(cache.Store(cacheKey, compiled.spirv));

    // 4. Verify cache hit
    auto cached = cache.Lookup(cacheKey);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->size(), compiled.spirv.size());
    EXPECT_EQ(*cached, compiled.spirv);
}

TEST_F(ShaderManagementIntegrationTest, VariantGeneration) {
    std::string baseShader = R"(
#version 450

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = vec3(0.0);

    #ifdef QUALITY_LOW
        color = vec3(0.5);
    #elif defined(QUALITY_MEDIUM)
        color = vec3(0.75);
    #elif defined(QUALITY_HIGH)
        color = vec3(1.0);
    #endif

    outColor = vec4(color, 1.0);
}
)";

    ShaderPreprocessor preprocessor;
    ShaderCompiler compiler;
    ShaderCacheConfig cacheConfig;
    cacheConfig.cacheDirectory = cacheDir;
    ShaderCacheManager cache(cacheConfig);

    // Generate three quality variants
    std::vector<std::string> qualities = {"QUALITY_LOW", "QUALITY_MEDIUM", "QUALITY_HIGH"};
    std::vector<std::vector<uint32_t>> spirvVariants;

    for (const auto& quality : qualities) {
        // Preprocess with quality define
        std::unordered_map<std::string, std::string> defines;
        defines[quality] = "";

        auto preprocessed = preprocessor.Preprocess(baseShader, defines);
        ASSERT_TRUE(preprocessed.success);

        // Generate cache key
        std::vector<std::pair<std::string, std::string>> definesPairs = {{quality, ""}};
        std::string cacheKey = GenerateCacheKey(
            preprocessed.processedSource,
            "",
            static_cast<uint32_t>(ShaderStage::Fragment),
            definesPairs,
            "main"
        );

        // Check cache
        auto cached = cache.Lookup(cacheKey);
        std::vector<uint32_t> spirv;

        if (cached.has_value()) {
            spirv = *cached;
        } else {
            // Compile
            auto compiled = compiler.Compile(
                ShaderStage::Fragment,
                preprocessed.processedSource,
                "main"
            );
            ASSERT_TRUE(compiled.success);
            spirv = compiled.spirv;

            // Store in cache
            cache.Store(cacheKey, spirv);
        }

        spirvVariants.push_back(spirv);
    }

    // Verify all variants are different
    EXPECT_NE(spirvVariants[0], spirvVariants[1]);
    EXPECT_NE(spirvVariants[1], spirvVariants[2]);
    EXPECT_NE(spirvVariants[0], spirvVariants[2]);
}

TEST_F(ShaderManagementIntegrationTest, ComplexIncludeHierarchy) {
    // Create a complex include hierarchy
    CreateIncludeFile("constants.glsl", R"(
#ifndef CONSTANTS_GLSL
#define CONSTANTS_GLSL
const float PI = 3.14159;
const float E = 2.71828;
#endif
)");

    CreateIncludeFile("utils.glsl", R"(
#ifndef UTILS_GLSL
#define UTILS_GLSL
#include "constants.glsl"

float square(float x) { return x * x; }
#endif
)");

    CreateIncludeFile("lighting.glsl", R"(
#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL
#include "constants.glsl"
#include "utils.glsl"

vec3 calculateLighting(vec3 normal, vec3 lightDir) {
    float ndotl = max(dot(normal, lightDir), 0.0);
    return vec3(ndotl);
}
#endif
)");

    std::string mainShader = R"(
#version 450
#include "lighting.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = vec3(0.0, 1.0, 0.0);
    vec3 color = calculateLighting(inNormal, lightDir);
    outColor = vec4(color, 1.0);
}
)";

    // Preprocess
    PreprocessorConfig config;
    config.includePaths = {includeDir};
    ShaderPreprocessor preprocessor(config);

    auto preprocessed = preprocessor.Preprocess(mainShader);
    ASSERT_TRUE(preprocessed.success);

    // Should include all three files
    EXPECT_EQ(preprocessed.includedFiles.size(), 3);

    // Should contain content from all includes
    EXPECT_NE(preprocessed.processedSource.find("const float PI"), std::string::npos);
    EXPECT_NE(preprocessed.processedSource.find("float square"), std::string::npos);
    EXPECT_NE(preprocessed.processedSource.find("calculateLighting"), std::string::npos);

    // Compile
    ShaderCompiler compiler;
    auto compiled = compiler.Compile(
        ShaderStage::Fragment,
        preprocessed.processedSource,
        "main"
    );

    ASSERT_TRUE(compiled.success);
    EXPECT_FALSE(compiled.spirv.empty());
}

TEST_F(ShaderManagementIntegrationTest, CacheEfficiency) {
    std::string shader = R"(
#version 450

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    ShaderPreprocessor preprocessor;
    ShaderCompiler compiler;

    ShaderCacheConfig cacheConfig;
    cacheConfig.cacheDirectory = cacheDir;
    ShaderCacheManager cache(cacheConfig);

    // First compilation (cache miss)
    auto preprocessed1 = preprocessor.Preprocess(shader);
    ASSERT_TRUE(preprocessed1.success);

    std::vector<std::pair<std::string, std::string>> noDefines;
    std::string cacheKey = GenerateCacheKey(
        preprocessed1.processedSource,
        "",
        static_cast<uint32_t>(ShaderStage::Fragment),
        noDefines,
        "main"
    );

    auto cached1 = cache.Lookup(cacheKey);
    EXPECT_FALSE(cached1.has_value());  // Cache miss

    auto compiled = compiler.Compile(
        ShaderStage::Fragment,
        preprocessed1.processedSource,
        "main"
    );
    ASSERT_TRUE(compiled.success);

    cache.Store(cacheKey, compiled.spirv);

    // Second lookup (cache hit)
    auto cached2 = cache.Lookup(cacheKey);
    ASSERT_TRUE(cached2.has_value());  // Cache hit
    EXPECT_EQ(*cached2, compiled.spirv);

    // Verify statistics
    auto stats = cache.GetStatistics();
    EXPECT_EQ(stats.totalCacheMisses, 1);
    EXPECT_EQ(stats.totalCacheHits, 1);
    EXPECT_FLOAT_EQ(stats.GetHitRate(), 0.5f);
}

TEST_F(ShaderManagementIntegrationTest, MultiStageProgram) {
    // Vertex shader
    std::string vertexShader = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outTexCoord = inTexCoord;
}
)";

    // Fragment shader
    std::string fragmentShader = R"(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inTexCoord, 0.0, 1.0);
}
)";

    ShaderPreprocessor preprocessor;
    ShaderCompiler compiler;
    ShaderCacheManager cache(ShaderCacheConfig{.cacheDirectory = cacheDir});

    // Process vertex shader
    auto vertPreprocessed = preprocessor.Preprocess(vertexShader);
    ASSERT_TRUE(vertPreprocessed.success);

    auto vertCompiled = compiler.Compile(
        ShaderStage::Vertex,
        vertPreprocessed.processedSource,
        "main"
    );
    ASSERT_TRUE(vertCompiled.success);

    // Process fragment shader
    auto fragPreprocessed = preprocessor.Preprocess(fragmentShader);
    ASSERT_TRUE(fragPreprocessed.success);

    auto fragCompiled = compiler.Compile(
        ShaderStage::Fragment,
        fragPreprocessed.processedSource,
        "main"
    );
    ASSERT_TRUE(fragCompiled.success);

    // Both should have valid SPIR-V
    EXPECT_FALSE(vertCompiled.spirv.empty());
    EXPECT_FALSE(fragCompiled.spirv.empty());

    // They should be different
    EXPECT_NE(vertCompiled.spirv, fragCompiled.spirv);
}

TEST_F(ShaderManagementIntegrationTest, ErrorPropagation) {
    // Invalid shader with syntax error
    std::string invalidShader = R"(
#version 450
#include "nonexistent.glsl"

void main() {
    invalid_code_here;
}
)";

    PreprocessorConfig config;
    config.includePaths = {includeDir};

    ShaderPreprocessor preprocessor(config);
    ShaderCompiler compiler;

    auto preprocessed = preprocessor.Preprocess(invalidShader);
    // Preprocessor may succeed but with warning about missing include

    if (preprocessed.success) {
        auto compiled = compiler.Compile(
            ShaderStage::Fragment,
            preprocessed.processedSource,
            "main"
        );

        // Compilation should fail due to syntax error
        EXPECT_FALSE(compiled.success);
        EXPECT_FALSE(compiled.errorLog.empty());
    }
}

TEST_F(ShaderManagementIntegrationTest, RealWorldPBRShader) {
    CreateIncludeFile("pbr_common.glsl", R"(
#ifndef PBR_COMMON_GLSL
#define PBR_COMMON_GLSL

const float PI = 3.14159265359;

struct Material {
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
};

struct Light {
    vec3 position;
    vec3 color;
};

#endif
)");

    CreateIncludeFile("pbr_lighting.glsl", R"(
#ifndef PBR_LIGHTING_GLSL
#define PBR_LIGHTING_GLSL

#include "pbr_common.glsl"

vec3 calculatePBR(vec3 worldPos, vec3 normal, vec3 viewDir,
                  Material material, Light light) {
    vec3 L = normalize(light.position - worldPos);
    vec3 H = normalize(viewDir + L);

    float NdotL = max(dot(normal, L), 0.0);

    // Simplified PBR
    vec3 radiance = light.color * NdotL;
    vec3 color = material.albedo * radiance;

    return color;
}

#endif
)");

    std::string pbrShader = R"(
#version 450

#include "pbr_lighting.glsl"

layout(binding = 0) uniform UniformData {
    mat4 viewProj;
    vec3 viewPos;
    Light light;
} ubo;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    Material material;
    material.albedo = vec3(0.8, 0.2, 0.2);
    material.metallic = METALLIC_VALUE;
    material.roughness = ROUGHNESS_VALUE;
    material.ao = 1.0;

    vec3 viewDir = normalize(ubo.viewPos - inWorldPos);

    vec3 color = calculatePBR(inWorldPos, inNormal, viewDir, material, ubo.light);

    outColor = vec4(color, 1.0);
}
)";

    // Compile with different material properties
    PreprocessorConfig config;
    config.includePaths = {includeDir};

    ShaderPreprocessor preprocessor(config);
    ShaderCompiler compiler;
    ShaderCacheManager cache(ShaderCacheConfig{.cacheDirectory = cacheDir});

    std::unordered_map<std::string, std::string> defines;
    defines["METALLIC_VALUE"] = "0.5";
    defines["ROUGHNESS_VALUE"] = "0.3";

    auto preprocessed = preprocessor.Preprocess(pbrShader, defines);
    ASSERT_TRUE(preprocessed.success);

    // Verify includes were processed
    EXPECT_GT(preprocessed.includedFiles.size(), 0);
    EXPECT_NE(preprocessed.processedSource.find("calculatePBR"), std::string::npos);
    EXPECT_NE(preprocessed.processedSource.find("#define METALLIC_VALUE 0.5"), std::string::npos);

    auto compiled = compiler.Compile(
        ShaderStage::Fragment,
        preprocessed.processedSource,
        "main"
    );

    ASSERT_TRUE(compiled.success);
    EXPECT_FALSE(compiled.spirv.empty());

    // Validate SPIR-V
    std::string error;
    bool valid = compiler.ValidateSpirv(compiled.spirv, error);
    EXPECT_TRUE(valid);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(ShaderManagementIntegrationTest, CachePerformanceComparison) {
    std::string shader = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = vec4(1.0);
}
)";

    ShaderPreprocessor preprocessor;
    ShaderCompiler compiler;
    ShaderCacheManager cache(ShaderCacheConfig{.cacheDirectory = cacheDir});

    auto preprocessed = preprocessor.Preprocess(shader);
    ASSERT_TRUE(preprocessed.success);

    std::vector<std::pair<std::string, std::string>> noDefines;
    std::string cacheKey = GenerateCacheKey(
        preprocessed.processedSource,
        "",
        static_cast<uint32_t>(ShaderStage::Vertex),
        noDefines,
        "main"
    );

    // Time compilation
    auto compileStart = std::chrono::high_resolution_clock::now();
    auto compiled = compiler.Compile(
        ShaderStage::Vertex,
        preprocessed.processedSource,
        "main"
    );
    auto compileEnd = std::chrono::high_resolution_clock::now();
    auto compileTime = std::chrono::duration_cast<std::chrono::milliseconds>(compileEnd - compileStart);

    ASSERT_TRUE(compiled.success);
    cache.Store(cacheKey, compiled.spirv);

    // Time cache lookup
    auto lookupStart = std::chrono::high_resolution_clock::now();
    auto cached = cache.Lookup(cacheKey);
    auto lookupEnd = std::chrono::high_resolution_clock::now();
    auto lookupTime = std::chrono::duration_cast<std::chrono::milliseconds>(lookupEnd - lookupStart);

    ASSERT_TRUE(cached.has_value());

    // Cache should be significantly faster
    EXPECT_LT(lookupTime.count(), compileTime.count());
}
