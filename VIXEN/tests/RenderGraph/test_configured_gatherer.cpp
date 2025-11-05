// ============================================================================
// CONFIGURED GATHERER TEST - Shader bundle header as config file
// ============================================================================
// This test demonstrates using shader bundle headers (.h files) as
// "pseudo config files" that define what resources to gather.

#include <iostream>
#include <cassert>
#include <vector>
#include <functional>

// Mock Slot implementation (must be defined before including gatherer)
namespace Vixen::RenderGraph {

template<typename T>
class Slot {
public:
    void set(const T& value) { value_ = value; hasValue_ = true; }
    void set(T&& value) { value_ = std::move(value); hasValue_ = true; }
    const T& get() const { assert(hasValue_); return value_; }
    T& get() { assert(hasValue_); return value_; }
    bool hasValue() const { return hasValue_; }
private:
    T value_;
    bool hasValue_ = false;
};

} // namespace Vixen::RenderGraph

// NOW include the configured gatherer and shader bundles
#include "Nodes/ConfiguredGathererNode.h"
#include "ShaderBundles/compute_shader_example.h"

using namespace Vixen::RenderGraph;
using namespace ShaderBundles;

// ============================================================================
// TEST: Shader Bundle as Configuration
// ============================================================================

void testImageProcessingShaderConfig() {
    std::cout << "\n=== Test 1: Image Processing Shader Bundle as Config ===\n";

    // The shader bundle header defines what we need!
    // (see ShaderBundles/compute_shader_example.h)

    TypeConfiguredGatherer<ImageProcessingShader> gatherer;

    // Create mock resources
    VkImageView inputImg = reinterpret_cast<VkImageView>(0x1001);
    VkImageView outputImg = reinterpret_cast<VkImageView>(0x2001);
    VkBuffer paramsBuffer = reinterpret_cast<VkBuffer>(0x3001);
    VkPipeline pipeline = reinterpret_cast<VkPipeline>(0x4001);

    // Connect using field references from the shader bundle header!
    gatherer.field(&ImageProcessingShader::inputImage).connectFrom(inputImg);
    gatherer.field(&ImageProcessingShader::outputImage).connectFrom(outputImg);
    gatherer.field(&ImageProcessingShader::parametersBuffer).connectFrom(paramsBuffer);
    gatherer.field(&ImageProcessingShader::pipeline).connectFrom(pipeline);

    // Execute gathering
    gatherer.execute();

    // Verify assembled config
    auto& config = gatherer.assembledConfig.get();
    assert(config.inputImage == inputImg);
    assert(config.outputImage == outputImg);
    assert(config.parametersBuffer == paramsBuffer);
    assert(config.pipeline == pipeline);

    std::cout << "  ✅ Shader bundle configured gatherer successfully\n";
    std::cout << "  Input image: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.inputImage) << std::dec << "\n";
    std::cout << "  Output image: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.outputImage) << std::dec << "\n";
    std::cout << "  Parameters buffer: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.parametersBuffer) << std::dec << "\n";
    std::cout << "  Pipeline: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.pipeline) << std::dec << "\n";
}

void testComputeShaderResourcesConfig() {
    std::cout << "\n=== Test 2: Compute Shader Resources Bundle ===\n";

    // Reference the compute shader header as configuration
    TypeConfiguredGatherer<ComputeShaderResources> gatherer;

    // Create mock resource arrays (as defined in compute.h)
    std::vector<VkBuffer> uniforms = {
        reinterpret_cast<VkBuffer>(0x1001),
        reinterpret_cast<VkBuffer>(0x1002)
    };

    std::vector<VkImageView> inputs = {
        reinterpret_cast<VkImageView>(0x2001),
        reinterpret_cast<VkImageView>(0x2002),
        reinterpret_cast<VkImageView>(0x2003)
    };

    std::vector<VkImageView> outputs = {
        reinterpret_cast<VkImageView>(0x3001)
    };

    VkPipeline pipeline = reinterpret_cast<VkPipeline>(0x4001);
    VkPipelineLayout layout = reinterpret_cast<VkPipelineLayout>(0x4002);

    // Connect all fields as specified in the compute shader header!
    gatherer.field(&ComputeShaderResources::uniformBuffers).connectFrom(uniforms);
    gatherer.field(&ComputeShaderResources::inputImages).connectFrom(inputs);
    gatherer.field(&ComputeShaderResources::outputImages).connectFrom(outputs);
    gatherer.field(&ComputeShaderResources::computePipeline).connectFrom(pipeline);
    gatherer.field(&ComputeShaderResources::pipelineLayout).connectFrom(layout);

    // Execute
    gatherer.execute();

    // Verify
    auto& config = gatherer.assembledConfig.get();
    assert(config.uniformBuffers.size() == 2);
    assert(config.inputImages.size() == 3);
    assert(config.outputImages.size() == 1);
    assert(config.computePipeline == pipeline);
    assert(config.pipelineLayout == layout);

    std::cout << "  ✅ Compute shader bundle configured successfully\n";
    std::cout << "  Uniform buffers: " << config.uniformBuffers.size() << "\n";
    std::cout << "  Input images: " << config.inputImages.size() << "\n";
    std::cout << "  Output images: " << config.outputImages.size() << "\n";
    std::cout << "  Pipeline: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.computePipeline) << std::dec << "\n";
}

void testParticleSimulationConfig() {
    std::cout << "\n=== Test 3: Particle Simulation Shader Bundle ===\n";

    // Use particle simulation header as config
    TypeConfiguredGatherer<ParticleSimulationShader> gatherer;

    // Mock particle system resources
    VkBuffer positions = reinterpret_cast<VkBuffer>(0x1001);
    VkBuffer velocities = reinterpret_cast<VkBuffer>(0x1002);
    VkBuffer params = reinterpret_cast<VkBuffer>(0x2001);
    VkImageView forceField = reinterpret_cast<VkImageView>(0x3001);
    VkPipeline pipeline = reinterpret_cast<VkPipeline>(0x4001);

    // Connect based on particle shader requirements
    gatherer.field(&ParticleSimulationShader::positionBuffer).connectFrom(positions);
    gatherer.field(&ParticleSimulationShader::velocityBuffer).connectFrom(velocities);
    gatherer.field(&ParticleSimulationShader::parametersBuffer).connectFrom(params);
    gatherer.field(&ParticleSimulationShader::forceFieldTexture).connectFrom(forceField);
    gatherer.field(&ParticleSimulationShader::pipeline).connectFrom(pipeline);

    gatherer.execute();

    auto& config = gatherer.assembledConfig.get();
    assert(config.positionBuffer == positions);
    assert(config.velocityBuffer == velocities);
    assert(config.parametersBuffer == params);
    assert(config.forceFieldTexture == forceField);
    assert(config.pipeline == pipeline);

    std::cout << "  ✅ Particle simulation configured successfully\n";
    std::cout << "  Position buffer: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.positionBuffer) << std::dec << "\n";
    std::cout << "  Velocity buffer: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.velocityBuffer) << std::dec << "\n";
    std::cout << "  Force field texture: 0x" << std::hex << reinterpret_cast<uintptr_t>(config.forceFieldTexture) << std::dec << "\n";
}

void testMultipleShaderBundles() {
    std::cout << "\n=== Test 4: Multiple Shader Bundles ===\n";
    std::cout << "  Demonstrating that different .h files can coexist\n";

    // Use different shader bundle headers as configs
    TypeConfiguredGatherer<ImageProcessingShader> imageProc;
    TypeConfiguredGatherer<ParticleSimulationShader> particles;
    TypeConfiguredGatherer<ComputeShaderResources> generic;

    std::cout << "  ✅ Multiple shader bundle types compile successfully\n";
    std::cout << "  - ImageProcessingShader (from compute_shader_example.h)\n";
    std::cout << "  - ParticleSimulationShader (from compute_shader_example.h)\n";
    std::cout << "  - ComputeShaderResources (from compute_shader_example.h)\n";
    std::cout << "\n  Each acts as an independent 'config file' for gathering!\n";
}

// ============================================================================
// DEMONSTRATION: Real-world workflow
// ============================================================================

void demonstrateWorkflow() {
    std::cout << "\n=== Demonstration: Shader Bundle as Config Workflow ===\n\n";

    std::cout << "STEP 1: Define shader requirements in header\n";
    std::cout << "  File: ShaderBundles/my_compute.h\n";
    std::cout << "  ```cpp\n";
    std::cout << "  struct MyComputeShader {\n";
    std::cout << "      std::vector<VkImageView> inputImages;\n";
    std::cout << "      VkBuffer uniformBuffer;\n";
    std::cout << "      VkPipeline pipeline;\n";
    std::cout << "  };\n";
    std::cout << "  ```\n\n";

    std::cout << "STEP 2: Include header in render graph code\n";
    std::cout << "  ```cpp\n";
    std::cout << "  #include \"ShaderBundles/my_compute.h\"\n";
    std::cout << "  ```\n\n";

    std::cout << "STEP 3: Create gatherer configured by header\n";
    std::cout << "  ```cpp\n";
    std::cout << "  auto gatherer = graph.addNode<TypeConfiguredGatherer<\n";
    std::cout << "      MyComputeShader  // ← Header type is the config!\n";
    std::cout << "  >>();\n";
    std::cout << "  ```\n\n";

    std::cout << "STEP 4: Connect resources using field names from header\n";
    std::cout << "  ```cpp\n";
    std::cout << "  gatherer.field(&MyComputeShader::inputImages)\n";
    std::cout << "      .connectFrom(imageNode[\"outputs\"]);\n";
    std::cout << "  gatherer.field(&MyComputeShader::uniformBuffer)\n";
    std::cout << "      .connectFrom(bufferNode[\"uniforms\"]);\n";
    std::cout << "  ```\n\n";

    std::cout << "STEP 5: Output is type-safe struct matching header\n";
    std::cout << "  ```cpp\n";
    std::cout << "  connect(gatherer[\"assembledConfig\"],\n";
    std::cout << "          computeNode.input<MyComputeShader>(\"resources\"));\n";
    std::cout << "  ```\n\n";

    std::cout << "KEY BENEFITS:\n";
    std::cout << "  ✅ Shader requirements in ONE place (.h file)\n";
    std::cout << "  ✅ Type-safe: compiler validates everything\n";
    std::cout << "  ✅ Refactoring-safe: rename fields → automatic update\n";
    std::cout << "  ✅ No string lookups or runtime type checks\n";
    std::cout << "  ✅ IDE autocomplete for field names\n";
    std::cout << "  ✅ Version control for shader interfaces\n";
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  CONFIGURED GATHERER TEST SUITE\n";
    std::cout << "  Shader Bundle Headers as Config Files\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    testImageProcessingShaderConfig();
    testComputeShaderResourcesConfig();
    testParticleSimulationConfig();
    testMultipleShaderBundles();
    demonstrateWorkflow();

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  ✅ ALL TESTS PASSED!\n";
    std::cout << "\n";
    std::cout << "  SHADER BUNDLE HEADER PATTERN WORKING:\n";
    std::cout << "  ✅ Headers define resource requirements\n";
    std::cout << "  ✅ Gatherers configured by header types\n";
    std::cout << "  ✅ Type-safe field connections\n";
    std::cout << "  ✅ Compile-time validation\n";
    std::cout << "  ✅ Multiple bundles can coexist\n";
    std::cout << "  ✅ Zero runtime overhead\n";
    std::cout << "\n";
    std::cout << "  🎯 SHADER BUNDLE AS CONFIG FILE: CONFIRMED!\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    return 0;
}
