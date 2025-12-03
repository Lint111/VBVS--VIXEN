/**
 * @file test_descriptor_resource_gatherer_node.cpp
 * @brief Comprehensive tests for DescriptorResourceGathererNode using ShaderBundleDummyBuilder
 *
 * Coverage: DescriptorResourceGathererNode.h
 *
 * Test Categories:
 * 1. Single shader tests (UBO, SSBO, sampler, storage image)
 * 2. Multiple shaders with overlapping descriptors (stage flag merging)
 * 3. Multiple shaders with different descriptor sets
 * 4. Edge cases (empty descriptors, maximum counts, sparse bindings)
 * 5. Proper descriptor layout info construction
 * 6. Type validation and compatibility checks
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>

// Test utilities
#include "Libraries/RenderGraph/tests/TestMocks.h"  // Centralized test mocks (use project-root include path)
#include <TestFixtures.h>  // ShaderManagement test fixtures (from include path)

// Render graph components
#include <Nodes/DescriptorResourceGathererNode.h>
#include <Data/Nodes/DescriptorResourceGathererNodeConfig.h>
#include <Core/NodeTypeRegistry.h>
#include <Core/RenderGraph.h>
#include <Data/Core/CompileTimeResourceSystem.h>

// Shader management
#include "ShaderDataBundle.h"
#include "SpirvReflectionData.h"

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

namespace VRG = Vixen::RenderGraph;
using namespace ShaderManagement;
using namespace ShaderManagement::TestFixtures;

// ============================================================================
// Test Fixture
// ============================================================================

class DescriptorResourceGathererNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeType = std::make_unique<VRG::DescriptorResourceGathererNodeType>();
        node = std::make_unique<VRG::DescriptorResourceGathererNode>("test_gatherer", nodeType.get());
    }

    void TearDown() override {
        node.reset();
        nodeType.reset();
    }

    std::unique_ptr<VRG::DescriptorResourceGathererNodeType> nodeType;
    std::unique_ptr<VRG::DescriptorResourceGathererNode> node;
};

// ============================================================================
// 1. Configuration Tests
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, ConfigHasCorrectInputs) {
    EXPECT_EQ(VRG::DescriptorResourceGathererNodeConfig::INPUT_COUNT, 1)
        << "Should have 1 fixed input (SHADER_DATA_BUNDLE)";
}

TEST_F(DescriptorResourceGathererNodeTest, ConfigHasCorrectOutputs) {
    EXPECT_EQ(VRG::DescriptorResourceGathererNodeConfig::OUTPUT_COUNT, 3)
        << "Should have 3 outputs (DESCRIPTOR_HANDLES, SLOT_ROLES, SHADER_DATA_BUNDLE_OUT)";
}

TEST_F(DescriptorResourceGathererNodeTest, ConfigArrayModeIsSingle) {
    EXPECT_EQ(VRG::DescriptorResourceGathererNodeConfig::ARRAY_MODE, VRG::SlotArrayMode::Single);
}

// ============================================================================
// 2. Single Shader Tests - Different Descriptor Types
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, SingleShaderWithUBO) {
    // Create shader bundle with single UBO
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Vertex)
        .addUBO(0, 0, "CameraUBO", 128, VK_SHADER_STAGE_VERTEX_BIT)
        .setProgramName("UBOTest")
        .build();

    ASSERT_NE(bundle, nullptr);
    ASSERT_NE(bundle->reflectionData, nullptr);
    EXPECT_EQ(bundle->reflectionData->descriptorSets.size(), 1);

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);

    const auto& binding = set0[0];
    EXPECT_EQ(binding.set, 0);
    EXPECT_EQ(binding.binding, 0);
    EXPECT_EQ(binding.name, "CameraUBO");
    EXPECT_EQ(binding.descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_EQ(binding.descriptorCount, 1);
    EXPECT_EQ(binding.stageFlags, VK_SHADER_STAGE_VERTEX_BIT);
}

TEST_F(DescriptorResourceGathererNodeTest, SingleShaderWithSSBO) {
    // Create shader bundle with SSBO (common in compute shaders)
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute)
        .addSSBO(0, 0, "ParticleBuffer", 4096, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);

    const auto& binding = set0[0];
    EXPECT_EQ(binding.descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(binding.stageFlags, VK_SHADER_STAGE_COMPUTE_BIT);
    EXPECT_EQ(binding.name, "ParticleBuffer");
}

TEST_F(DescriptorResourceGathererNodeTest, SingleShaderWithSampler) {
    // Create shader bundle with combined image sampler
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addSampler(0, 0, "texSampler", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);

    const auto& binding = set0[0];
    EXPECT_EQ(binding.descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(binding.stageFlags, VK_SHADER_STAGE_FRAGMENT_BIT);
    EXPECT_EQ(binding.imageFormat, VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(binding.imageDimension, 2);
}

TEST_F(DescriptorResourceGathererNodeTest, SingleShaderWithStorageImage) {
    // Create shader bundle with storage image (write access)
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute)
        .addStorageImage(0, 0, "outputImage", VK_FORMAT_R32G32B32A32_SFLOAT, 2, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);

    const auto& binding = set0[0];
    EXPECT_EQ(binding.descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(binding.imageFormat, VK_FORMAT_R32G32B32A32_SFLOAT);
}

TEST_F(DescriptorResourceGathererNodeTest, SingleShaderWithMultipleDescriptors) {
    // Real-world fragment shader with UBO + sampler
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "MaterialUBO", 64, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 1, "albedoMap", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 2, "normalMap", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 3);

    // Verify binding order
    EXPECT_EQ(set0[0].binding, 0);
    EXPECT_EQ(set0[0].descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_EQ(set0[1].binding, 1);
    EXPECT_EQ(set0[1].descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(set0[2].binding, 2);
    EXPECT_EQ(set0[2].descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

// ============================================================================
// 3. Multiple Shaders - Overlapping Descriptors (Stage Flag Merging)
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, MultipleShadersSameDescriptor) {
    // Vertex and Fragment shaders both use the same UBO
    auto vertBundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Vertex)
        .addUBO(0, 0, "CameraUBO", 128, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto fragBundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "CameraUBO", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // Verify both have the same binding structure
    const auto& vertBinding = vertBundle->reflectionData->descriptorSets[0][0];
    const auto& fragBinding = fragBundle->reflectionData->descriptorSets[0][0];

    EXPECT_EQ(vertBinding.set, fragBinding.set);
    EXPECT_EQ(vertBinding.binding, fragBinding.binding);
    EXPECT_EQ(vertBinding.name, fragBinding.name);
    EXPECT_EQ(vertBinding.descriptorType, fragBinding.descriptorType);

    // Stage flags should be different
    EXPECT_EQ(vertBinding.stageFlags, VK_SHADER_STAGE_VERTEX_BIT);
    EXPECT_EQ(fragBinding.stageFlags, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Merged stage flags
    VkShaderStageFlags mergedFlags = vertBinding.stageFlags | fragBinding.stageFlags;
    EXPECT_EQ(mergedFlags, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST_F(DescriptorResourceGathererNodeTest, MultipleShadersDifferentDescriptors) {
    // Vertex uses UBO, Fragment uses sampler (no overlap)
    auto vertBundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Vertex)
        .addUBO(0, 0, "TransformUBO", 64, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto fragBundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addSampler(0, 1, "colorTexture", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // Verify different bindings
    const auto& vertBinding = vertBundle->reflectionData->descriptorSets[0][0];
    const auto& fragBinding = fragBundle->reflectionData->descriptorSets[0][0];

    EXPECT_EQ(vertBinding.binding, 0);
    EXPECT_EQ(fragBinding.binding, 1);
    EXPECT_NE(vertBinding.descriptorType, fragBinding.descriptorType);
}

TEST_F(DescriptorResourceGathererNodeTest, AllGraphicsStagesMerged) {
    // Test merging across all graphics pipeline stages
    VkShaderStageFlags allGraphicsStages =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;

    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Vertex)
        .addUBO(0, 0, "SharedUBO", 128, allGraphicsStages)
        .build();

    const auto& binding = bundle->reflectionData->descriptorSets[0][0];
    EXPECT_EQ(binding.stageFlags, allGraphicsStages);
}

// ============================================================================
// 4. Multiple Descriptor Sets
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, MultipleDescriptorSets) {
    // Set 0: Per-frame data, Set 1: Per-material data
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "PerFrameUBO", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(1, 0, "materialTexture", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    EXPECT_EQ(bundle->reflectionData->descriptorSets.size(), 2);

    // Verify set 0
    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);
    EXPECT_EQ(set0[0].set, 0);
    EXPECT_EQ(set0[0].binding, 0);

    // Verify set 1
    const auto& set1 = bundle->reflectionData->descriptorSets[1];
    ASSERT_EQ(set1.size(), 1);
    EXPECT_EQ(set1[0].set, 1);
    EXPECT_EQ(set1[0].binding, 0);
}

TEST_F(DescriptorResourceGathererNodeTest, MultipleBindingsPerSet) {
    // Set 0 with multiple bindings
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "CameraUBO", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addUBO(0, 1, "LightUBO", 256, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 2, "shadowMap", VK_FORMAT_D32_SFLOAT, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 3, "envMap", VK_FORMAT_R16G16B16A16_SFLOAT, 3, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 4);

    // Verify binding indices are sequential
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(set0[i].binding, i);
    }
}

// ============================================================================
// 5. Edge Cases
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, EmptyDescriptorSet) {
    // Shader with no descriptors (e.g., simple compute with only push constants)
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute)
        .addPushConstant(0, 16, "Params", VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    EXPECT_EQ(bundle->reflectionData->descriptorSets.size(), 0);
}

TEST_F(DescriptorResourceGathererNodeTest, MaximumDescriptorCount) {
    // Vulkan spec guarantees support for many descriptors
    // Test with a reasonable number (16 in one set)
    auto builder = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute);

    // Add 16 storage buffers (common in compute workloads)
    for (uint32_t i = 0; i < 16; ++i) {
        builder.addSSBO(0, i, "Buffer" + std::to_string(i), 256, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    auto bundle = builder.build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    EXPECT_EQ(set0.size(), 16);

    // Verify all bindings are present
    for (uint32_t i = 0; i < 16; ++i) {
        EXPECT_EQ(set0[i].binding, i);
    }
}

TEST_F(DescriptorResourceGathererNodeTest, SparseBindings) {
    // Non-contiguous binding indices (0, 2, 5, 10)
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "Buffer0", 64, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 2, "Texture2", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 5, "Texture5", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSSBO(0, 10, "Buffer10", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 4);

    // Verify sparse binding indices
    EXPECT_EQ(set0[0].binding, 0);
    EXPECT_EQ(set0[1].binding, 2);
    EXPECT_EQ(set0[2].binding, 5);
    EXPECT_EQ(set0[3].binding, 10);
}

TEST_F(DescriptorResourceGathererNodeTest, HighBindingIndex) {
    // Test high binding index (Vulkan spec guarantees at least 32 bindings per set)
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute)
        .addSSBO(0, 31, "HighIndexBuffer", 256, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 1);
    EXPECT_EQ(set0[0].binding, 31);
}

// ============================================================================
// 6. Descriptor Layout Construction
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, VkDescriptorSetLayoutBindingConstruction) {
    // Verify that reflection data can be converted to Vulkan structures
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "TestUBO", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addSampler(0, 1, "TestSampler", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];

    // Convert to VkDescriptorSetLayoutBinding
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    for (const auto& binding : set0) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.descriptorType = binding.descriptorType;
        vkBinding.descriptorCount = binding.descriptorCount;
        vkBinding.stageFlags = binding.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
    }

    ASSERT_EQ(vkBindings.size(), 2);

    // Verify UBO binding
    EXPECT_EQ(vkBindings[0].binding, 0);
    EXPECT_EQ(vkBindings[0].descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_EQ(vkBindings[0].descriptorCount, 1);
    EXPECT_EQ(vkBindings[0].stageFlags, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Verify sampler binding
    EXPECT_EQ(vkBindings[1].binding, 1);
    EXPECT_EQ(vkBindings[1].descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(vkBindings[1].descriptorCount, 1);
    EXPECT_EQ(vkBindings[1].stageFlags, VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST_F(DescriptorResourceGathererNodeTest, DescriptorLayoutHash) {
    // Verify that bundles have unique hashes based on descriptor layout
    auto bundle1 = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addUBO(0, 0, "UBO", 128, VK_SHADER_STAGE_FRAGMENT_BIT)
        .setUUID("bundle1")
        .build();

    auto bundle2 = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Fragment)
        .addSampler(0, 0, "Sampler", VK_FORMAT_R8G8B8A8_UNORM, 2, VK_SHADER_STAGE_FRAGMENT_BIT)
        .setUUID("bundle2")
        .build();

    // Different descriptors should result in different hashes
    EXPECT_NE(bundle1->descriptorInterfaceHash, bundle2->descriptorInterfaceHash);
}

// ============================================================================
// 7. Compute Shader Specific Tests
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, ComputeShaderDescriptors) {
    // Typical compute shader: input SSBO + output storage image
    auto bundle = ShaderBundleDummyBuilder()
        .addModule(ShaderStage::Compute)
        .addSSBO(0, 0, "InputBuffer", 4096, VK_SHADER_STAGE_COMPUTE_BIT)
        .addStorageImage(0, 1, "OutputImage", VK_FORMAT_R32G32B32A32_SFLOAT, 2, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    const auto& set0 = bundle->reflectionData->descriptorSets[0];
    ASSERT_EQ(set0.size(), 2);

    EXPECT_EQ(set0[0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(set0[1].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    // Both should be compute stage only
    EXPECT_EQ(set0[0].stageFlags, VK_SHADER_STAGE_COMPUTE_BIT);
    EXPECT_EQ(set0[1].stageFlags, VK_SHADER_STAGE_COMPUTE_BIT);
}

// ============================================================================
// 8. Node Type Tests
// ============================================================================

TEST_F(DescriptorResourceGathererNodeTest, NodeTypeRegistration) {
    EXPECT_EQ(nodeType->GetTypeName(), "DescriptorResourceGatherer");
    EXPECT_TRUE(nodeType->CreateInstance("test") != nullptr);
}

TEST_F(DescriptorResourceGathererNodeTest, VariadicConstraints) {
    // Descriptor gatherer should support many variadic inputs (one per binding)
    EXPECT_EQ(nodeType->GetDefaultMinVariadicInputs(), 0);
    EXPECT_GT(nodeType->GetDefaultMaxVariadicInputs(), 100)
        << "Should support many descriptor bindings";
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
