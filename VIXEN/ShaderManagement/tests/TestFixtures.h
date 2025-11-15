#pragma once

/**
 * @file TestFixtures.h
 * @brief Reusable test fixtures for ShaderManagement types
 *
 * MOTIVATION:
 * Multiple tests across RenderGraph and ShaderManagement need valid
 * ShaderDataBundle instances with realistic reflection data. Creating
 * these manually in each test is tedious and error-prone.
 *
 * SOLUTION:
 * Centralized factory functions that create pre-configured ShaderDataBundle
 * instances for common test scenarios. Tests can use these instead of
 * building bundles from scratch.
 *
 * USAGE:
 * ```cpp
 * #include <ShaderManagement/tests/TestFixtures.h>
 *
 * auto bundle = ShaderManagement::TestFixtures::CreateSimplePushConstantBundle();
 * // Use bundle in tests
 * ```
 */

#include <ShaderManagement/ShaderDataBundle.h>
#include <ShaderManagement/SpirvReflectionData.h>
#include <ShaderManagement/ShaderProgram.h>
#include <memory>
#include <vector>

namespace ShaderManagement::TestFixtures {

// ============================================================================
// TYPE BUILDERS (Reflection Data Structures)
// ============================================================================

/**
 * @brief Create scalar type info (float, int, uint, bool)
 */
inline SpirvTypeInfo MakeScalarType(SpirvTypeInfo::BaseType type, uint32_t width = 32) {
    SpirvTypeInfo info;
    info.baseType = type;
    info.width = width;
    info.vecSize = 1;
    info.columns = 1;
    info.rows = 1;
    info.sizeInBytes = width / 8;
    info.alignment = info.sizeInBytes;
    return info;
}

/**
 * @brief Create vector type info (vec2, vec3, vec4)
 */
inline SpirvTypeInfo MakeVectorType(SpirvTypeInfo::BaseType type, uint32_t size, uint32_t width = 32) {
    SpirvTypeInfo info;
    info.baseType = type;
    info.width = width;
    info.vecSize = size;
    info.columns = 1;
    info.rows = 1;
    info.sizeInBytes = (width / 8) * size;
    info.alignment = info.sizeInBytes;
    return info;
}

/**
 * @brief Create matrix type info (mat2, mat3, mat4)
 */
inline SpirvTypeInfo MakeMatrixType(SpirvTypeInfo::BaseType type, uint32_t cols, uint32_t rows, uint32_t width = 32) {
    SpirvTypeInfo info;
    info.baseType = type;
    info.width = width;
    info.vecSize = 1;
    info.columns = cols;
    info.rows = rows;
    info.sizeInBytes = (width / 8) * cols * rows;
    info.alignment = (width / 8) * rows; // Column-major alignment
    return info;
}

// ============================================================================
// STRUCT BUILDERS (Push Constants / UBO / SSBO)
// ============================================================================

/**
 * @brief Create simple push constant struct (vec3 cameraPos + float time)
 *
 * Layout:
 * ```glsl
 * layout(push_constant) uniform PushConstants {
 *     vec3 cameraPos;  // offset 0, size 12
 *     float time;      // offset 16, size 4 (aligned to 16)
 * };
 * ```
 */
inline SpirvStructDefinition MakeSimplePushConstantStruct() {
    SpirvStructDefinition structDef;
    structDef.name = "PushConstants";

    // vec3 cameraPos at offset 0
    SpirvStructMember cameraPos;
    cameraPos.name = "cameraPos";
    cameraPos.type = MakeVectorType(SpirvTypeInfo::BaseType::Float, 3);
    cameraPos.offset = 0;
    structDef.members.push_back(cameraPos);

    // float time at offset 16 (vec3 is 12 bytes, padded to 16 for std140)
    SpirvStructMember time;
    time.name = "time";
    time.type = MakeScalarType(SpirvTypeInfo::BaseType::Float);
    time.offset = 16;
    structDef.members.push_back(time);

    structDef.sizeInBytes = 20;
    structDef.alignment = 16;

    return structDef;
}

/**
 * @brief Create complex push constant struct (vec3 + float + int + mat4)
 *
 * Layout:
 * ```glsl
 * layout(push_constant) uniform PushConstants {
 *     vec3 position;       // offset 0, size 12
 *     float time;          // offset 16, size 4
 *     int frameCount;      // offset 20, size 4
 *     mat4 viewMatrix;     // offset 32, size 64 (16-byte aligned)
 * };
 * ```
 */
inline SpirvStructDefinition MakeComplexPushConstantStruct() {
    SpirvStructDefinition structDef;
    structDef.name = "ComplexPushConstants";

    SpirvStructMember position;
    position.name = "position";
    position.type = MakeVectorType(SpirvTypeInfo::BaseType::Float, 3);
    position.offset = 0;
    structDef.members.push_back(position);

    SpirvStructMember time;
    time.name = "time";
    time.type = MakeScalarType(SpirvTypeInfo::BaseType::Float);
    time.offset = 16;
    structDef.members.push_back(time);

    SpirvStructMember frameCount;
    frameCount.name = "frameCount";
    frameCount.type = MakeScalarType(SpirvTypeInfo::BaseType::Int);
    frameCount.offset = 20;
    structDef.members.push_back(frameCount);

    SpirvStructMember viewMatrix;
    viewMatrix.name = "viewMatrix";
    viewMatrix.type = MakeMatrixType(SpirvTypeInfo::BaseType::Float, 4, 4);
    viewMatrix.offset = 32;
    viewMatrix.matrixStride = 16;
    structDef.members.push_back(viewMatrix);

    structDef.sizeInBytes = 96;
    structDef.alignment = 16;

    return structDef;
}

// ============================================================================
// SHADER DATA BUNDLE FACTORIES
// ============================================================================

/**
 * @brief Create minimal empty bundle
 * Use for tests that need a valid bundle but don't care about contents
 */
inline std::shared_ptr<ShaderDataBundle> CreateEmptyBundle() {
    auto bundle = std::make_shared<ShaderDataBundle>();
    // Empty CompiledProgram and SpirvReflectionData
    return bundle;
}

/**
 * @brief Create bundle with simple push constants (vec3 cameraPos + float time)
 *
 * Common test case for push constant gathering and buffer packing.
 * No descriptors, no vertex inputs, just push constants.
 */
inline std::shared_ptr<ShaderDataBundle> CreateSimplePushConstantBundle() {
    auto bundle = std::make_shared<ShaderDataBundle>();

    // Create push constant range
    SpirvPushConstantRange pushRange;
    pushRange.name = "PushConstants";
    pushRange.offset = 0;
    pushRange.size = 20;  // vec3(12) + padding(4) + float(4)
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.structDef = MakeSimplePushConstantStruct();

    bundle->reflectionData.pushConstantRanges.push_back(pushRange);

    // Mark bundle as valid (has at least one reflection element)
    bundle->uuid = "test-simple-push";

    return bundle;
}

/**
 * @brief Create bundle with complex push constants (vec3 + float + int + mat4)
 *
 * Test case for complex type handling, alignment, and matrix packing.
 */
inline std::shared_ptr<ShaderDataBundle> CreateComplexPushConstantBundle() {
    auto bundle = std::make_shared<ShaderDataBundle>();

    SpirvPushConstantRange pushRange;
    pushRange.name = "ComplexPushConstants";
    pushRange.offset = 0;
    pushRange.size = 96;
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.structDef = MakeComplexPushConstantStruct();

    bundle->reflectionData.pushConstantRanges.push_back(pushRange);
    bundle->uuid = "test-complex-push";

    return bundle;
}

/**
 * @brief Create bundle with single scalar push constant (float deltaTime)
 *
 * Minimal test case for single-value push constants.
 */
inline std::shared_ptr<ShaderDataBundle> CreateSingleScalarPushBundle() {
    auto bundle = std::make_shared<ShaderDataBundle>();

    SpirvStructDefinition structDef;
    structDef.name = "SimplePush";

    SpirvStructMember deltaTime;
    deltaTime.name = "deltaTime";
    deltaTime.type = MakeScalarType(SpirvTypeInfo::BaseType::Float);
    deltaTime.offset = 0;
    structDef.members.push_back(deltaTime);

    structDef.sizeInBytes = 4;
    structDef.alignment = 4;

    SpirvPushConstantRange pushRange;
    pushRange.name = "SimplePush";
    pushRange.offset = 0;
    pushRange.size = 4;
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.structDef = structDef;

    bundle->reflectionData.pushConstantRanges.push_back(pushRange);
    bundle->uuid = "test-single-scalar";

    return bundle;
}

/**
 * @brief Create bundle with descriptor bindings (UBO + sampler)
 *
 * Test case for descriptor set creation and layout generation.
 * Includes both uniform buffer and combined image sampler.
 */
inline std::shared_ptr<ShaderDataBundle> CreateDescriptorBundle() {
    auto bundle = std::make_shared<ShaderDataBundle>();

    // UBO binding at set 0, binding 0
    SpirvDescriptorBinding ubo;
    ubo.set = 0;
    ubo.binding = 0;
    ubo.name = "CameraUBO";
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ubo.typeInfo = MakeMatrixType(SpirvTypeInfo::BaseType::Float, 4, 4);

    // Sampler binding at set 0, binding 1
    SpirvDescriptorBinding sampler;
    sampler.set = 0;
    sampler.binding = 1;
    sampler.name = "texSampler";
    sampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler.descriptorCount = 1;
    sampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    sampler.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    sampler.imageDimension = 2; // 2D texture

    bundle->reflectionData.descriptorBindings.push_back(ubo);
    bundle->reflectionData.descriptorBindings.push_back(sampler);
    bundle->uuid = "test-descriptors";

    return bundle;
}

/**
 * @brief Create bundle with everything (push constants + descriptors + vertex input)
 *
 * Full-featured test case exercising all reflection paths.
 */
inline std::shared_ptr<ShaderDataBundle> CreateFullFeaturedBundle() {
    auto bundle = CreateDescriptorBundle(); // Start with descriptors

    // Add push constants
    SpirvPushConstantRange pushRange;
    pushRange.name = "PushConstants";
    pushRange.offset = 0;
    pushRange.size = 20;
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.structDef = MakeSimplePushConstantStruct();
    bundle->reflectionData.pushConstantRanges.push_back(pushRange);

    bundle->uuid = "test-full-featured";
    return bundle;
}

} // namespace ShaderManagement::TestFixtures
