#include <gtest/gtest.h>
#include "ShaderManagement/ShaderBundleBuilder.h"
#include "ShaderManagement/SdiRegistryManager.h"
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

/**
 * @brief Test fixture for SDI (.si.h) lifecycle testing
 *
 * Tests the complete workflow:
 * 1. Compile GLSL → SPIR-V
 * 2. Reflect SPIR-V metadata
 * 3. Generate .si.h interface file
 * 4. Store in cache
 * 5. Access and validate
 * 6. Update and regenerate
 */
class SdiLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = std::filesystem::temp_directory_path() / "sdi_lifecycle_test";
        sdiOutputDir = testDir / "generated" / "sdi";
        cacheDir = testDir / "cache";

        // Clean up from previous runs
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }

        std::filesystem::create_directories(sdiOutputDir);
        std::filesystem::create_directories(cacheDir);
    }

    void TearDown() override {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }

    // Helper: Create a complete vertex shader with UBO
    std::string CreateTestVertexShader() {
        return R"(
#version 450

// Vertex Input
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniform Buffer Object
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPosition;
} camera;

layout(set = 0, binding = 1) uniform ModelUBO {
    mat4 model;
    vec4 color;
} model;

// Vertex Output
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

// Push Constants
layout(push_constant) uniform PushConstants {
    uint instanceID;
    float time;
} pushConsts;

void main() {
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    gl_Position = camera.projection * camera.view * worldPos;

    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(model.model) * inNormal;
    fragTexCoord = inTexCoord;
}
        )";
    }

    // Helper: Create a fragment shader with textures
    std::string CreateTestFragmentShader() {
        return R"(
#version 450

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

// Textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(albedoMap, fragTexCoord).rgb;
    vec3 normal = normalize(fragNormal);

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normal, lightDir), 0.0);

    outColor = vec4(albedo * diff, 1.0);
}
        )";
    }

    std::filesystem::path testDir;
    std::filesystem::path sdiOutputDir;
    std::filesystem::path cacheDir;
};

// ===== Phase 1: Build Complete Shader Bundle with SDI =====

TEST_F(SdiLifecycleTest, BuildCompleteShaderBundleWithSDI) {
    std::string vertSource = CreateTestVertexShader();
    std::string fragSource = CreateTestFragmentShader();

    // Configure SDI generation
    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;
    sdiConfig.namespacePrefix = "PBRShader";
    sdiConfig.generateComments = true;
    sdiConfig.generateLayoutInfo = true;

    // Build complete shader bundle
    ShaderBundleBuilder builder;
    auto result = builder
        .SetProgramName("PBR Material Shader")
        .SetUuid("pbr_material_v1_0")
        .AddStage(ShaderStage::Vertex, vertSource)
        .AddStage(ShaderStage::Fragment, fragSource)
        .SetSdiConfig(sdiConfig)
        .EnableSdiGeneration(true)
        .Build();

    ASSERT_TRUE(result.success) << "Build failed: " << result.errorMessage;
    ASSERT_NE(result.bundle, nullptr);

    // Verify bundle contents
    const auto& bundle = *result.bundle;
    EXPECT_EQ(bundle.uuid, "pbr_material_v1_0");
    EXPECT_EQ(bundle.GetProgramName(), "PBR Material Shader");
    EXPECT_TRUE(bundle.HasStage(ShaderStage::Vertex));
    EXPECT_TRUE(bundle.HasStage(ShaderStage::Fragment));
    EXPECT_FALSE(bundle.GetSpirv(ShaderStage::Vertex).empty());
    EXPECT_FALSE(bundle.GetSpirv(ShaderStage::Fragment).empty());

    // Verify reflection data
    EXPECT_NE(bundle.reflectionData, nullptr);
    EXPECT_FALSE(bundle.reflectionData->descriptorSets.empty());

    // Verify SDI generation
    EXPECT_TRUE(bundle.HasValidSdi());
    EXPECT_FALSE(bundle.sdiHeaderPath.empty());
    EXPECT_TRUE(std::filesystem::exists(bundle.sdiHeaderPath));

    // Read SDI file to verify content
    std::ifstream sdiFile(bundle.sdiHeaderPath);
    ASSERT_TRUE(sdiFile.is_open());
    std::string content((std::istreambuf_iterator<char>(sdiFile)),
                        std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("#pragma once"), std::string::npos);

    // Check for descriptor set namespaces
    EXPECT_NE(content.find("namespace Set0"), std::string::npos);
    EXPECT_NE(content.find("namespace Set1"), std::string::npos);

    // Check for binding structures (generalized or named)
    EXPECT_TRUE(content.find("struct camera") != std::string::npos ||
                content.find("struct Binding0") != std::string::npos)
        << "Should contain descriptor binding structures";

    // Check for required constants
    EXPECT_NE(content.find("static constexpr uint32_t SET"), std::string::npos);
    EXPECT_NE(content.find("static constexpr uint32_t BINDING"), std::string::npos);
}

// ===== Phase 2: Validate SDI Content Structure =====

TEST_F(SdiLifecycleTest, ValidateSdiContentStructure) {
    std::string vertSource = CreateTestVertexShader();
    std::string fragSource = CreateTestFragmentShader();

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;
    sdiConfig.generateComments = true;

    ShaderBundleBuilder builder;
    auto result = builder
        .SetProgramName("Test Vertex")
        .SetUuid("test_vertex_002")
        .AddStage(ShaderStage::Vertex, vertSource)
        .AddStage(ShaderStage::Fragment, fragSource)
        .SetSdiConfig(sdiConfig)
        .Build();

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.bundle->HasValidSdi());

    // Read generated SDI header
    std::ifstream file(result.bundle->sdiHeaderPath);
    std::string headerContent((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    ASSERT_FALSE(headerContent.empty());

    // Verify header contains expected structures
    EXPECT_NE(headerContent.find("#pragma once"), std::string::npos);

    // Check for descriptor set namespace
    EXPECT_NE(headerContent.find("namespace Set0"), std::string::npos);

    // Check for binding structures with constants
    EXPECT_NE(headerContent.find("static constexpr uint32_t SET"), std::string::npos);
    EXPECT_NE(headerContent.find("static constexpr uint32_t BINDING"), std::string::npos);
    EXPECT_NE(headerContent.find("VkDescriptorType"), std::string::npos);

    // Verify it includes type information
    EXPECT_TRUE(headerContent.find("uint32_t") != std::string::npos ||
                headerContent.find("#include <cstdint>") != std::string::npos);
}

// ===== Phase 3: Registry Integration =====

TEST_F(SdiLifecycleTest, RegisterShaderInSDIRegistry) {
    std::string vertSource = CreateTestVertexShader();
    std::string fragSource = CreateTestFragmentShader();

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;

    // Create registry
    SdiRegistryManager::Config registryConfig;
    registryConfig.sdiDirectory = sdiOutputDir;
    registryConfig.registryHeaderPath = sdiOutputDir / "SDI_Registry.h";
    SdiRegistryManager registry(registryConfig);

    // Build shader with registry integration
    ShaderBundleBuilder builder;
    auto result = builder
        .SetProgramName("PBR Shader")
        .SetUuid("pbr_shader_v1")
        .AddStage(ShaderStage::Vertex, vertSource)
        .AddStage(ShaderStage::Fragment, fragSource)
        .SetSdiConfig(sdiConfig)
        .EnableRegistryIntegration(&registry, "PBRMaterial")
        .Build();

    ASSERT_TRUE(result.success);

    // Verify shader is registered
    EXPECT_TRUE(registry.IsRegistered("pbr_shader_v1"));

    // Verify registry header was created
    EXPECT_TRUE(std::filesystem::exists(registryConfig.registryHeaderPath));
}

// ===== Phase 4: Update and Regenerate =====

TEST_F(SdiLifecycleTest, UpdateShaderAndRegenerateSdi) {
    std::string vertSource = CreateTestVertexShader();
    std::string fragSource = CreateTestFragmentShader();

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;

    ShaderBundleBuilder builder1;
    auto result1 = builder1
        .SetProgramName("Evolving Shader")
        .SetUuid("evolving_shader")
        .AddStage(ShaderStage::Vertex, vertSource)
        .AddStage(ShaderStage::Fragment, fragSource)
        .SetSdiConfig(sdiConfig)
        .Build();

    ASSERT_TRUE(result1.success);
    auto timestamp1 = std::filesystem::last_write_time(result1.bundle->sdiHeaderPath);

    // Wait to ensure timestamp changes
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Modify vertex shader - add a new uniform
    std::string modifiedVertSource = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
} camera;

// NEW: Additional lighting uniform
layout(set = 0, binding = 2) uniform LightingUBO {
    vec3 lightDirection;
    vec3 lightColor;
} lighting;

void main() {
    gl_Position = camera.projection * camera.view * vec4(inPosition, 1.0);
    outColor = lighting.lightColor;
}
    )";

    std::string simpleFragSource = R"(
#version 450

layout(location = 0) in vec3 outColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(outColor, 1.0);
}
    )";

    // Rebuild with same UUID (simulating shader update)
    ShaderBundleBuilder builder2;
    auto result2 = builder2
        .SetProgramName("Evolving Shader")
        .SetUuid("evolving_shader")  // Same UUID
        .AddStage(ShaderStage::Vertex, modifiedVertSource)
        .AddStage(ShaderStage::Fragment, simpleFragSource)
        .SetSdiConfig(sdiConfig)
        .Build();

    ASSERT_TRUE(result2.success);

    // Verify file was overwritten
    EXPECT_EQ(result1.bundle->sdiHeaderPath, result2.bundle->sdiHeaderPath);

    auto timestamp2 = std::filesystem::last_write_time(result2.bundle->sdiHeaderPath);
    EXPECT_NE(timestamp1, timestamp2) << "File should have been updated";

    // Verify new content includes the new binding
    std::ifstream file(result2.bundle->sdiHeaderPath);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Check for the new binding at set 0, binding 2
    EXPECT_TRUE(content.find("BINDING = 2") != std::string::npos ||
                content.find("Binding2") != std::string::npos)
        << "Should contain the new lighting uniform binding";
}

// ===== Phase 5: Error Handling =====

TEST_F(SdiLifecycleTest, HandleInvalidShaderGracefully) {
    std::string invalidSource = "This is not valid GLSL!";

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;

    ShaderBundleBuilder builder;
    auto result = builder
        .SetProgramName("Invalid Shader")
        .SetUuid("invalid_shader")
        .AddStage(ShaderStage::Vertex, invalidSource)
        .SetSdiConfig(sdiConfig)
        .Build();

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
    EXPECT_EQ(result.bundle, nullptr);
}

// ===== Phase 6: Vertex Shader Only Test =====

TEST_F(SdiLifecycleTest, BuildVertexShaderOnly) {
    std::string source = CreateTestVertexShader();

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = sdiOutputDir;

    ShaderBundleBuilder builder;
    auto result = builder
        .SetProgramName("Vertex Only")
        .SetUuid("vertex_only_001")
        .AddStage(ShaderStage::Vertex, source)
        .SetSdiConfig(sdiConfig)
        .SetValidatePipeline(false)  // Disable pipeline validation for single-stage test
        .Build();

    ASSERT_TRUE(result.success) << "Build failed: " << result.errorMessage;
    EXPECT_TRUE(result.bundle->HasValidSdi());
}
