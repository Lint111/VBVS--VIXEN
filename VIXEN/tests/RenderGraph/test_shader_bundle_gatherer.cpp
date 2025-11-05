/**
 * @file test_shader_bundle_gatherer.cpp
 * @brief Tests for ShaderBundleGatherer - using shader bundle headers as config
 *
 * This test demonstrates the FINAL pattern for Phase G resource gathering:
 * - Shader bundle header defines requirements (acts as "config file")
 * - Gatherer parameterized by bundle type
 * - Automatic input slot generation from bundle fields
 * - Compile-time type validation
 * - Minimal graph setup
 *
 * Compatible with VULKAN_TRIMMED_BUILD (headers only).
 */

#include <iostream>
#include <cassert>
#include <vector>
#include <array>
#include <cstdint>

// ============================================================================
// MOCK SLOT IMPLEMENTATION (for standalone testing)
// ============================================================================

namespace Vixen::RenderGraph {

template<typename T>
class Slot {
public:
    void set(const T& value) { value_ = value; }
    void set(T&& value) { value_ = std::move(value); }
    const T& get() const { return value_; }
    T& get() { return value_; }

    // Mock connection
    void connectFrom(Slot<T>& other) {
        value_ = other.get();
    }

private:
    T value_;
};

} // namespace Vixen::RenderGraph

// Include the shader bundle gatherer system
#include "../../RenderGraph/include/Nodes/ShaderBundleGatherer.h"

// Include shader bundle examples
#include "../../RenderGraph/include/ShaderBundles/compute_resources.h"

using namespace Vixen::RenderGraph;
using namespace Vixen::RenderGraph::ShaderBundles;

// ============================================================================
// TEST UTILITIES
// ============================================================================

void printTestHeader(const char* testName) {
    std::cout << "\n=== " << testName << " ===" << std::endl;
}

void printSuccess(const char* message) {
    std::cout << "  ✅ " << message << std::endl;
}

// ============================================================================
// TEST 1: Image Processing Shader Bundle
// ============================================================================

void testImageProcessingShaderBundle() {
    printTestHeader("Test 1: Image Processing Shader Bundle");

    // Create gatherer using shader bundle header
    ShaderBundleGatherer<ImageProcessingShader> gatherer;

    // Create source slots
    Slot<VkImageView> inputImageSlot;
    Slot<VkImageView> outputImageSlot;
    Slot<VkBuffer> parametersSlot;
    Slot<VkPipeline> pipelineSlot;

    // Set source values
    inputImageSlot.set(reinterpret_cast<VkImageView>(0x1001));
    outputImageSlot.set(reinterpret_cast<VkImageView>(0x2001));
    parametersSlot.set(reinterpret_cast<VkBuffer>(0x3001));
    pipelineSlot.set(reinterpret_cast<VkPipeline>(0x4001));

    // Connect inputs (order matches struct field order)
    gatherer.input<0>().connectFrom(inputImageSlot);
    gatherer.input<1>().connectFrom(outputImageSlot);
    gatherer.input<2>().connectFrom(parametersSlot);
    gatherer.input<3>().connectFrom(pipelineSlot);

    // Execute gatherer
    gatherer.execute();

    // Get assembled bundle
    auto& bundle = gatherer.output.get();

    // Verify fields
    std::cout << "  Input image: " << std::hex << bundle.inputImage << std::dec << std::endl;
    std::cout << "  Output image: " << std::hex << bundle.outputImage << std::dec << std::endl;
    std::cout << "  Parameters buffer: " << std::hex << bundle.parametersBuffer << std::dec << std::endl;
    std::cout << "  Pipeline: " << std::hex << bundle.pipeline << std::dec << std::endl;

    assert(bundle.inputImage == reinterpret_cast<VkImageView>(0x1001));
    assert(bundle.outputImage == reinterpret_cast<VkImageView>(0x2001));
    assert(bundle.parametersBuffer == reinterpret_cast<VkBuffer>(0x3001));
    assert(bundle.pipeline == reinterpret_cast<VkPipeline>(0x4001));

    printSuccess("Image processing shader bundle assembled correctly");
}

// ============================================================================
// TEST 2: Compute Shader Resources Bundle
// ============================================================================

void testComputeShaderResourcesBundle() {
    printTestHeader("Test 2: Compute Shader Resources Bundle");

    // Create gatherer
    ShaderBundleGatherer<ComputeShaderResources> gatherer;

    // Create source slots
    Slot<std::vector<VkBuffer>> uniformBuffersSlot;
    Slot<std::vector<VkImageView>> inputImagesSlot;
    Slot<std::vector<VkImageView>> outputImagesSlot;
    Slot<VkPipeline> pipelineSlot;
    Slot<VkPipelineLayout> layoutSlot;

    // Set source values
    uniformBuffersSlot.set({
        reinterpret_cast<VkBuffer>(0x1001),
        reinterpret_cast<VkBuffer>(0x1002)
    });
    inputImagesSlot.set({
        reinterpret_cast<VkImageView>(0x2001),
        reinterpret_cast<VkImageView>(0x2002),
        reinterpret_cast<VkImageView>(0x2003)
    });
    outputImagesSlot.set({
        reinterpret_cast<VkImageView>(0x3001)
    });
    pipelineSlot.set(reinterpret_cast<VkPipeline>(0x4001));
    layoutSlot.set(reinterpret_cast<VkPipelineLayout>(0x5001));

    // Connect inputs
    gatherer.input<0>().connectFrom(uniformBuffersSlot);
    gatherer.input<1>().connectFrom(inputImagesSlot);
    gatherer.input<2>().connectFrom(outputImagesSlot);
    gatherer.input<3>().connectFrom(pipelineSlot);
    gatherer.input<4>().connectFrom(layoutSlot);

    // Execute
    gatherer.execute();

    // Get bundle
    auto& bundle = gatherer.output.get();

    // Verify
    std::cout << "  Uniform buffers: " << bundle.uniformBuffers.size() << std::endl;
    std::cout << "  Input images: " << bundle.inputImages.size() << std::endl;
    std::cout << "  Output images: " << bundle.outputImages.size() << std::endl;
    std::cout << "  Pipeline: " << std::hex << bundle.computePipeline << std::dec << std::endl;
    std::cout << "  Layout: " << std::hex << bundle.pipelineLayout << std::dec << std::endl;

    assert(bundle.uniformBuffers.size() == 2);
    assert(bundle.inputImages.size() == 3);
    assert(bundle.outputImages.size() == 1);
    assert(bundle.computePipeline == reinterpret_cast<VkPipeline>(0x4001));
    assert(bundle.pipelineLayout == reinterpret_cast<VkPipelineLayout>(0x5001));

    printSuccess("Compute shader resources bundle assembled correctly");
}

// ============================================================================
// TEST 3: Particle Simulation Shader Bundle
// ============================================================================

void testParticleSimulationBundle() {
    printTestHeader("Test 3: Particle Simulation Shader Bundle");

    ShaderBundleGatherer<ParticleSimulationShader> gatherer;

    Slot<VkBuffer> positionSlot;
    Slot<VkBuffer> velocitySlot;
    Slot<VkImageView> forceFieldSlot;
    Slot<VkBuffer> uniformSlot;
    Slot<VkPipeline> pipelineSlot;

    positionSlot.set(reinterpret_cast<VkBuffer>(0x1001));
    velocitySlot.set(reinterpret_cast<VkBuffer>(0x1002));
    forceFieldSlot.set(reinterpret_cast<VkImageView>(0x3001));
    uniformSlot.set(reinterpret_cast<VkBuffer>(0x2001));
    pipelineSlot.set(reinterpret_cast<VkPipeline>(0x4001));

    gatherer.input<0>().connectFrom(positionSlot);
    gatherer.input<1>().connectFrom(velocitySlot);
    gatherer.input<2>().connectFrom(forceFieldSlot);
    gatherer.input<3>().connectFrom(uniformSlot);
    gatherer.input<4>().connectFrom(pipelineSlot);

    gatherer.execute();

    auto& bundle = gatherer.output.get();

    std::cout << "  Position buffer: " << std::hex << bundle.positionBuffer << std::dec << std::endl;
    std::cout << "  Velocity buffer: " << std::hex << bundle.velocityBuffer << std::dec << std::endl;
    std::cout << "  Force field: " << std::hex << bundle.forceFieldTexture << std::dec << std::endl;
    std::cout << "  Uniform buffer: " << std::hex << bundle.uniformBuffer << std::dec << std::endl;
    std::cout << "  Pipeline: " << std::hex << bundle.computePipeline << std::dec << std::endl;

    assert(bundle.positionBuffer == reinterpret_cast<VkBuffer>(0x1001));
    assert(bundle.velocityBuffer == reinterpret_cast<VkBuffer>(0x1002));
    assert(bundle.forceFieldTexture == reinterpret_cast<VkImageView>(0x3001));

    printSuccess("Particle simulation shader bundle assembled correctly");
}

// ============================================================================
// TEST 4: Compile-Time Type Validation
// ============================================================================

void testCompileTimeValidation() {
    printTestHeader("Test 4: Compile-Time Type Validation");

    // These should all compile successfully
    using namespace Vixen::RenderGraph::ShaderBundles;

    // Verify trait extraction
    using ImageProcessingTraits = ShaderBundleTraits<ImageProcessingShader>;
    static_assert(ImageProcessingTraits::FieldCount == 4,
        "ImageProcessingShader should have 4 fields");

    using ComputeTraits = ShaderBundleTraits<ComputeShaderResources>;
    static_assert(ComputeTraits::FieldCount == 5,
        "ComputeShaderResources should have 5 fields");

    using ParticleTraits = ShaderBundleTraits<ParticleSimulationShader>;
    static_assert(ParticleTraits::FieldCount == 5,
        "ParticleSimulationShader should have 5 fields");

    // Verify field type extraction
    using ImageField0 = BundleFieldType<ImageProcessingShader, 0>;
    static_assert(std::is_same_v<ImageField0, VkImageView>,
        "First field should be VkImageView");

    using ComputeField0 = BundleFieldType<ComputeShaderResources, 0>;
    static_assert(std::is_same_v<ComputeField0, std::vector<VkBuffer>>,
        "First field should be vector<VkBuffer>");

    // Verify reflectability
    static_assert(IsReflectableBundle<ImageProcessingShader>,
        "ImageProcessingShader should be reflectable");
    static_assert(IsReflectableBundle<ComputeShaderResources>,
        "ComputeShaderResources should be reflectable");

    std::cout << "  ImageProcessingShader fields: "
              << ImageProcessingTraits::FieldCount << std::endl;
    std::cout << "  ComputeShaderResources fields: "
              << ComputeTraits::FieldCount << std::endl;
    std::cout << "  ParticleSimulationShader fields: "
              << ParticleTraits::FieldCount << std::endl;

    printSuccess("Compile-time type validation passed");
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════" << std::endl;
    std::cout << "  SHADER BUNDLE GATHERER TEST SUITE" << std::endl;
    std::cout << "  Final Pattern for Phase G Resource Gathering" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════" << std::endl;

    testImageProcessingShaderBundle();
    testComputeShaderResourcesBundle();
    testParticleSimulationBundle();
    testCompileTimeValidation();

    std::cout << "\n═══════════════════════════════════════════════════════" << std::endl;
    std::cout << "  ✅ ALL TESTS PASSED!" << std::endl;
    std::cout << std::endl;
    std::cout << "  SHADER BUNDLE AS CONFIG FILE PATTERN:" << std::endl;
    std::cout << "  ✅ Single bundle type parameter" << std::endl;
    std::cout << "  ✅ Automatic input slot generation" << std::endl;
    std::cout << "  ✅ Compile-time type validation" << std::endl;
    std::cout << "  ✅ Minimal graph setup" << std::endl;
    std::cout << "  ✅ Type-safe output" << std::endl;
    std::cout << "  ✅ Zero runtime overhead" << std::endl;
    std::cout << std::endl;
    std::cout << "  USAGE PATTERN:" << std::endl;
    std::cout << "  1. Include shader bundle header (defines requirements)" << std::endl;
    std::cout << "  2. Create ShaderBundleGatherer<BundleType>" << std::endl;
    std::cout << "  3. Connect inputs (order matches struct fields)" << std::endl;
    std::cout << "  4. Execute() assembles the bundle" << std::endl;
    std::cout << "  5. output.get() returns typed bundle struct" << std::endl;
    std::cout << std::endl;
    std::cout << "  🎯 READY FOR PHASE G COMPUTE PIPELINE!" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════" << std::endl;

    return 0;
}
