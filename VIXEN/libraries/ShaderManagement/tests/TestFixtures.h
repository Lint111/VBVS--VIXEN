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

#include <ShaderDataBundle.h>
#include <SpirvReflectionData.h>
#include <ShaderProgram.h>
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

    bundle->reflectionData->pushConstants.push_back(pushRange);

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

    bundle->reflectionData->pushConstants.push_back(pushRange);
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

    bundle->reflectionData->pushConstants.push_back(pushRange);
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

    bundle->reflectionData->descriptorSets[0].push_back(ubo);
    bundle->reflectionData->descriptorSets[0].push_back(sampler);
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
    bundle->reflectionData->pushConstants.push_back(pushRange);

    bundle->uuid = "test-full-featured";
    return bundle;
}

// ============================================================================
// FLUENT BUILDER FOR DUMMY SHADER BUNDLES
// ============================================================================

/**
 * @brief Fluent builder for creating test shader bundles
 *
 * Provides method chaining API for constructing ShaderDataBundle instances
 * with dummy SPIR-V bytecode, descriptors, push constants, and metadata.
 *
 * Usage:
 * ```cpp
 * auto bundle = ShaderBundleDummyBuilder()
 *     .addModule(ShaderStage::Vertex)
 *     .addModule(ShaderStage::Fragment)
 *     .addUBO(0, 0, "CameraUBO", 128)
 *     .addPushConstant(0, sizeof(PushConstants))
 *     .build();
 * ```
 */
class ShaderBundleDummyBuilder {
public:
    /**
     * @brief Add a shader module stage
     *
     * Generates minimal valid SPIR-V bytecode for the stage.
     *
     * @param stage Shader stage to add
     * @param entryPoint Entry point name (default: "main")
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addModule(ShaderStage stage, std::string entryPoint = "main") {
        CompiledShaderStage stageData;
        stageData.stage = stage;
        stageData.entryPoint = std::move(entryPoint);
        stageData.spirvCode = generateDummySpirv(stage);
        stageData.generation = 1;
        stages.push_back(std::move(stageData));
        return *this;
    }

    /**
     * @brief Add uniform buffer object descriptor
     *
     * @param set Descriptor set index
     * @param binding Binding index
     * @param name Descriptor name
     * @param sizeBytes Buffer size in bytes
     * @param stageFlags Shader stages (default: ALL)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addUBO(
        uint32_t set,
        uint32_t binding,
        std::string name,
        uint32_t sizeBytes,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL
    ) {
        SpirvDescriptorBinding desc;
        desc.set = set;
        desc.binding = binding;
        desc.name = std::move(name);
        desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        desc.descriptorCount = 1;
        desc.stageFlags = stageFlags;
        desc.typeInfo = MakeScalarType(SpirvTypeInfo::BaseType::Float);
        desc.typeInfo.sizeInBytes = sizeBytes;
        descriptorSets[set].push_back(std::move(desc));
        return *this;
    }

    /**
     * @brief Add storage buffer object descriptor
     *
     * @param set Descriptor set index
     * @param binding Binding index
     * @param name Descriptor name
     * @param sizeBytes Buffer size in bytes
     * @param stageFlags Shader stages (default: COMPUTE)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addSSBO(
        uint32_t set,
        uint32_t binding,
        std::string name,
        uint32_t sizeBytes,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    ) {
        SpirvDescriptorBinding desc;
        desc.set = set;
        desc.binding = binding;
        desc.name = std::move(name);
        desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc.descriptorCount = 1;
        desc.stageFlags = stageFlags;
        desc.typeInfo = MakeScalarType(SpirvTypeInfo::BaseType::Float);
        desc.typeInfo.sizeInBytes = sizeBytes;
        descriptorSets[set].push_back(std::move(desc));
        return *this;
    }

    /**
     * @brief Add combined image sampler descriptor
     *
     * @param set Descriptor set index
     * @param binding Binding index
     * @param name Descriptor name
     * @param format Image format (default: R8G8B8A8_UNORM)
     * @param dimension Image dimension (1D=1, 2D=2, 3D=3, default: 2)
     * @param stageFlags Shader stages (default: FRAGMENT)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addSampler(
        uint32_t set,
        uint32_t binding,
        std::string name,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
        uint32_t dimension = 2,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    ) {
        SpirvDescriptorBinding desc;
        desc.set = set;
        desc.binding = binding;
        desc.name = std::move(name);
        desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        desc.descriptorCount = 1;
        desc.stageFlags = stageFlags;
        desc.imageFormat = format;
        desc.imageDimension = dimension;
        descriptorSets[set].push_back(std::move(desc));
        return *this;
    }

    /**
     * @brief Add storage image descriptor
     *
     * @param set Descriptor set index
     * @param binding Binding index
     * @param name Descriptor name
     * @param format Image format (default: R8G8B8A8_UNORM)
     * @param dimension Image dimension (1D=1, 2D=2, 3D=3, default: 2)
     * @param stageFlags Shader stages (default: COMPUTE)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addStorageImage(
        uint32_t set,
        uint32_t binding,
        std::string name,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
        uint32_t dimension = 2,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    ) {
        SpirvDescriptorBinding desc;
        desc.set = set;
        desc.binding = binding;
        desc.name = std::move(name);
        desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        desc.descriptorCount = 1;
        desc.stageFlags = stageFlags;
        desc.imageFormat = format;
        desc.imageDimension = dimension;
        descriptorSets[set].push_back(std::move(desc));
        return *this;
    }

    /**
     * @brief Add push constant range
     *
     * @param offset Offset in bytes
     * @param size Size in bytes
     * @param name Push constant block name (default: "PushConstants")
     * @param stageFlags Shader stages (default: ALL)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addPushConstant(
        uint32_t offset,
        uint32_t size,
        std::string name = "PushConstants",
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL
    ) {
        SpirvPushConstantRange range;
        range.name = std::move(name);
        range.offset = offset;
        range.size = size;
        range.stageFlags = stageFlags;
        range.structDef = createDummyStruct(range.name, size);
        pushConstants.push_back(std::move(range));
        return *this;
    }

    /**
     * @brief Add custom push constant range with struct definition
     *
     * @param offset Offset in bytes
     * @param structDef Struct definition with members
     * @param stageFlags Shader stages (default: ALL)
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& addPushConstantStruct(
        uint32_t offset,
        SpirvStructDefinition structDef,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL
    ) {
        SpirvPushConstantRange range;
        range.name = structDef.name;
        range.offset = offset;
        range.size = structDef.sizeInBytes;
        range.stageFlags = stageFlags;
        range.structDef = std::move(structDef);
        pushConstants.push_back(std::move(range));
        return *this;
    }

    /**
     * @brief Set program name
     *
     * @param name Program name for debugging
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& setProgramName(std::string name) {
        programName = std::move(name);
        return *this;
    }

    /**
     * @brief Set pipeline type
     *
     * @param type Pipeline type constraint
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& setPipelineType(PipelineTypeConstraint type) {
        pipelineType = type;
        return *this;
    }

    /**
     * @brief Set bundle UUID
     *
     * @param id Unique identifier
     * @return Reference to builder for chaining
     */
    ShaderBundleDummyBuilder& setUUID(std::string id) {
        uuid = std::move(id);
        return *this;
    }

    /**
     * @brief Build the final ShaderDataBundle
     *
     * Assembles all components into a complete bundle.
     * Generates default values for unset fields.
     *
     * @return Shared pointer to constructed bundle
     */
    std::shared_ptr<ShaderDataBundle> build() {
        auto bundle = std::make_shared<ShaderDataBundle>();

        // Set program metadata
        bundle->program.name = programName.empty() ? "TestShaderProgram" : programName;
        bundle->program.pipelineType = pipelineType;
        bundle->program.stages = std::move(stages);
        bundle->program.generation = 1;
        bundle->program.compiledAt = std::chrono::steady_clock::now();

        // Create reflection data
        bundle->reflectionData = std::make_shared<SpirvReflectionData>();
        bundle->reflectionData->descriptorSets = std::move(descriptorSets);
        bundle->reflectionData->pushConstants = std::move(pushConstants);

        // Set bundle metadata
        bundle->uuid = uuid.empty() ? "test-bundle-" + std::to_string(nextBundleId++) : uuid;
        bundle->createdAt = std::chrono::system_clock::now();
        bundle->descriptorInterfaceHash = "dummy-hash-" + bundle->uuid;

        // Dummy SDI path (not actually generated)
        bundle->sdiHeaderPath = "generated/sdi/" + bundle->uuid + "-SDI.h";
        bundle->sdiNamespace = "TestNamespace::" + bundle->uuid;

        return bundle;
    }

private:
    /**
     * @brief Generate minimal valid SPIR-V bytecode
     *
     * Creates a valid SPIR-V header and minimal module structure.
     *
     * @param stage Shader stage
     * @return SPIR-V bytecode (uint32_t words)
     */
    static std::vector<uint32_t> generateDummySpirv(ShaderStage stage) {
        // Minimal SPIR-V header (5 words)
        std::vector<uint32_t> spirv = {
            0x07230203,  // Magic number
            0x00010600,  // Version 1.6
            0x00000000,  // Generator (0 = unknown)
            0x0000000F,  // Bound (number of IDs)
            0x00000000   // Schema (reserved, must be 0)
        };

        // Add OpCapability based on stage
        switch (stage) {
            case ShaderStage::Vertex:
            case ShaderStage::Fragment:
            case ShaderStage::Geometry:
                spirv.push_back(0x00020011); // OpCapability Shader (capability=1)
                spirv.push_back(0x00000001);
                break;
            case ShaderStage::Compute:
                spirv.push_back(0x00020011); // OpCapability Shader
                spirv.push_back(0x00000001);
                break;
            default:
                spirv.push_back(0x00020011); // OpCapability Shader
                spirv.push_back(0x00000001);
                break;
        }

        // OpMemoryModel (word count=3, opcode=14)
        spirv.push_back(0x0003000E);
        spirv.push_back(0x00000000); // Logical
        spirv.push_back(0x00000001); // GLSL450

        // OpEntryPoint (minimal - just opcode for validity)
        uint32_t executionModel = 0;
        switch (stage) {
            case ShaderStage::Vertex:       executionModel = 0; break;
            case ShaderStage::Fragment:     executionModel = 4; break;
            case ShaderStage::Compute:      executionModel = 5; break;
            case ShaderStage::Geometry:     executionModel = 3; break;
            default:                        executionModel = 0; break;
        }
        spirv.push_back(0x0004000F); // OpEntryPoint (word count=4, opcode=15)
        spirv.push_back(executionModel);
        spirv.push_back(0x00000001); // Entry point ID
        spirv.push_back(0x6E69616D); // "main" encoded (4 bytes)

        return spirv;
    }

    /**
     * @brief Create dummy struct definition
     *
     * @param name Struct name
     * @param sizeBytes Total size in bytes
     * @return Struct definition with single float member
     */
    static SpirvStructDefinition createDummyStruct(const std::string& name, uint32_t sizeBytes) {
        SpirvStructDefinition structDef;
        structDef.name = name;
        structDef.sizeInBytes = sizeBytes;
        structDef.alignment = 16;

        // Add single dummy member to make struct valid
        SpirvStructMember member;
        member.name = "data";
        member.type = MakeScalarType(SpirvTypeInfo::BaseType::Float);
        member.offset = 0;
        structDef.members.push_back(member);

        return structDef;
    }

    // Builder state
    std::vector<CompiledShaderStage> stages;
    std::unordered_map<uint32_t, std::vector<SpirvDescriptorBinding>> descriptorSets;
    std::vector<SpirvPushConstantRange> pushConstants;
    std::string programName;
    std::string uuid;
    PipelineTypeConstraint pipelineType = PipelineTypeConstraint::Graphics;

    // Global bundle ID counter for unique UUIDs
    static inline uint32_t nextBundleId = 0;
};

} // namespace ShaderManagement::TestFixtures
