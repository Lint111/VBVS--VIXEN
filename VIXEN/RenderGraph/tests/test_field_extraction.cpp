// ============================================================================
// STANDALONE TEST: Field Extraction (TRIMMED BUILD COMPATIBLE)
// ============================================================================
// This test validates compile-time field extraction for struct-to-slot connections.
// NO Vulkan runtime needed - only headers!
// Compatible with VULKAN_TRIMMED_BUILD (headers only)

#include "Core/FieldExtractor.h"
#include "Data/Core/ResourceVariant.h"
#include "Data/Core/ResourceTypeTraits.h"

#include <iostream>
#include <vector>
#include <cassert>

// Use the RenderGraph namespace
using namespace Vixen::RenderGraph;

// ============================================================================
// TEST STRUCTURES - Mimic real render graph resources
// ============================================================================

// Example 1: SwapChain public variables (real use case)
struct TestSwapChainVariables {
    std::vector<VkImageView> images;
    VkSwapchainKHR swapchain;
    VkFormat format;
    uint32_t imageCount;
};

// Example 2: Pipeline state
struct TestPipelineState {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
};

// Example 3: Buffer collection
struct TestBufferCollection {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    std::vector<VkBuffer> uniformBuffers;
};

// ============================================================================
// COMPILE-TIME VALIDATION (static_assert)
// ============================================================================

namespace CompileTimeTests {

// ----------------------------------------------------------------------------
// Test 1: FieldExtractor construction and type deduction
// ----------------------------------------------------------------------------

// Explicit construction
using ImageExtractor = FieldExtractor<TestSwapChainVariables, std::vector<VkImageView>>;
constexpr auto explicitExtractor = ImageExtractor(&TestSwapChainVariables::images);

// Deduction guide (automatic type deduction)
constexpr auto deducedExtractor = FieldExtractor(&TestSwapChainVariables::images);
static_assert(std::is_same_v<decltype(deducedExtractor), decltype(explicitExtractor)>,
    "Deduction guide should produce same type as explicit construction");

// Helper function
constexpr auto helperExtractor = Field(&TestSwapChainVariables::images);
static_assert(std::is_same_v<decltype(helperExtractor), decltype(explicitExtractor)>,
    "Field() helper should produce same type as explicit construction");

// ----------------------------------------------------------------------------
// Test 2: Type trait introspection
// ----------------------------------------------------------------------------

// IsFieldExtractor
static_assert(IsFieldExtractor_v<decltype(deducedExtractor)>,
    "FieldExtractor should be detected by IsFieldExtractor_v");
static_assert(!IsFieldExtractor_v<int>,
    "Non-FieldExtractor types should not be detected");

// ExtractorStructType_t
static_assert(std::is_same_v<
    ExtractorStructType_t<decltype(deducedExtractor)>,
    TestSwapChainVariables>,
    "ExtractorStructType_t should extract struct type");

// ExtractorFieldType_t
static_assert(std::is_same_v<
    ExtractorFieldType_t<decltype(deducedExtractor)>,
    std::vector<VkImageView>>,
    "ExtractorFieldType_t should extract field type");

// ----------------------------------------------------------------------------
// Test 3: Multiple field extractors from same struct
// ----------------------------------------------------------------------------

constexpr auto imageExtractor = Field(&TestSwapChainVariables::images);
constexpr auto swapchainExtractor = Field(&TestSwapChainVariables::swapchain);
constexpr auto formatExtractor = Field(&TestSwapChainVariables::format);
constexpr auto countExtractor = Field(&TestSwapChainVariables::imageCount);

static_assert(!std::is_same_v<decltype(imageExtractor), decltype(swapchainExtractor)>,
    "Different field extractors should have different types");

// ----------------------------------------------------------------------------
// Test 4: Extractors for different field types
// ----------------------------------------------------------------------------

// Scalar Vulkan handle
constexpr auto pipelineExtractor = Field(&TestPipelineState::pipeline);
static_assert(std::is_same_v<
    ExtractorFieldType_t<decltype(pipelineExtractor)>,
    VkPipeline>,
    "Should extract scalar VkPipeline type");

// Vector of handles
constexpr auto uniformBuffersExtractor = Field(&TestBufferCollection::uniformBuffers);
static_assert(std::is_same_v<
    ExtractorFieldType_t<decltype(uniformBuffersExtractor)>,
    std::vector<VkBuffer>>,
    "Should extract vector<VkBuffer> type");

// ----------------------------------------------------------------------------
// Test 5: Validation checks (these should compile successfully)
// ----------------------------------------------------------------------------

// Valid: Field type matches target slot type exactly
constexpr bool test5a = ValidateFieldExtraction<
    decltype(Field(&TestSwapChainVariables::swapchain)),
    VkSwapchainKHR
>::value;
static_assert(test5a, "Exact type match should be valid");

// Valid: Field type is vector, target accepts vector
constexpr bool test5b = ValidateFieldExtraction<
    decltype(Field(&TestSwapChainVariables::images)),
    std::vector<VkImageView>
>::value;
static_assert(test5b, "Vector type match should be valid");

} // namespace CompileTimeTests

// ============================================================================
// RUNTIME VALIDATION
// ============================================================================

void testFieldExtraction() {
    std::cout << "\n=== Field Extraction Tests ===\n\n";

    // Test 1: Create test data
    TestSwapChainVariables swapchainVars;
    swapchainVars.images = {
        reinterpret_cast<VkImageView>(0x1001),
        reinterpret_cast<VkImageView>(0x1002),
        reinterpret_cast<VkImageView>(0x1003)
    };
    swapchainVars.swapchain = reinterpret_cast<VkSwapchainKHR>(0x2001);
    swapchainVars.format = VK_FORMAT_B8G8R8A8_SRGB;
    swapchainVars.imageCount = 3;

    std::cout << "Test 1: Field extraction with data\n";
    std::cout << "  Original struct.imageCount: " << swapchainVars.imageCount << "\n";
    std::cout << "  Original struct.images.size(): " << swapchainVars.images.size() << "\n";

    // Test 2: Extract scalar field
    auto countExtractor = Field(&TestSwapChainVariables::imageCount);
    const uint32_t& extractedCount = countExtractor.extract(swapchainVars);
    std::cout << "\nTest 2: Extract scalar field (imageCount)\n";
    std::cout << "  Extracted value: " << extractedCount << "\n";
    assert(extractedCount == 3 && "Extracted count should match original");
    std::cout << "  ✅ Scalar extraction successful\n";

    // Test 3: Extract vector field
    auto imageExtractor = Field(&TestSwapChainVariables::images);
    const std::vector<VkImageView>& extractedImages = imageExtractor.extract(swapchainVars);
    std::cout << "\nTest 3: Extract vector field (images)\n";
    std::cout << "  Extracted vector size: " << extractedImages.size() << "\n";
    assert(extractedImages.size() == 3 && "Extracted vector size should match original");
    assert(extractedImages[0] == swapchainVars.images[0] && "Extracted elements should match");
    std::cout << "  ✅ Vector extraction successful\n";

    // Test 4: Extract VkHandle field
    auto swapchainExtractor = Field(&TestSwapChainVariables::swapchain);
    const VkSwapchainKHR& extractedSwapchain = swapchainExtractor.extract(swapchainVars);
    std::cout << "\nTest 4: Extract VkHandle field (swapchain)\n";
    std::cout << "  Extracted handle: 0x" << std::hex << reinterpret_cast<uintptr_t>(extractedSwapchain) << std::dec << "\n";
    assert(extractedSwapchain == swapchainVars.swapchain && "Extracted handle should match");
    std::cout << "  ✅ Handle extraction successful\n";

    // Test 5: Extract VkFormat (enum) field
    auto formatExtractor = Field(&TestSwapChainVariables::format);
    const VkFormat& extractedFormat = formatExtractor.extract(swapchainVars);
    std::cout << "\nTest 5: Extract VkFormat field\n";
    std::cout << "  Extracted format: " << static_cast<int>(extractedFormat) << "\n";
    assert(extractedFormat == VK_FORMAT_B8G8R8A8_SRGB && "Extracted format should match");
    std::cout << "  ✅ Enum extraction successful\n";

    // Test 6: Modify through extractor (non-const)
    auto mutableCountExtractor = Field(&TestSwapChainVariables::imageCount);
    uint32_t& mutableCount = mutableCountExtractor.extract(swapchainVars);
    uint32_t oldCount = mutableCount;
    mutableCount = 5;
    std::cout << "\nTest 6: Modify field through extractor\n";
    std::cout << "  Old value: " << oldCount << "\n";
    std::cout << "  New value: " << swapchainVars.imageCount << "\n";
    assert(swapchainVars.imageCount == 5 && "Field should be modified through extractor");
    std::cout << "  ✅ Mutable extraction successful\n";

    // Test 7: Multiple extractors on same instance
    TestPipelineState pipelineState;
    pipelineState.pipeline = reinterpret_cast<VkPipeline>(0x3001);
    pipelineState.layout = reinterpret_cast<VkPipelineLayout>(0x3002);
    pipelineState.renderPass = reinterpret_cast<VkRenderPass>(0x3003);

    auto pipelineExtractor = Field(&TestPipelineState::pipeline);
    auto layoutExtractor = Field(&TestPipelineState::layout);
    auto renderPassExtractor = Field(&TestPipelineState::renderPass);

    const VkPipeline& pipe = pipelineExtractor.extract(pipelineState);
    const VkPipelineLayout& layout = layoutExtractor.extract(pipelineState);
    const VkRenderPass& pass = renderPassExtractor.extract(pipelineState);

    std::cout << "\nTest 7: Multiple extractors on same struct\n";
    std::cout << "  Pipeline: 0x" << std::hex << reinterpret_cast<uintptr_t>(pipe) << std::dec << "\n";
    std::cout << "  Layout: 0x" << std::hex << reinterpret_cast<uintptr_t>(layout) << std::dec << "\n";
    std::cout << "  RenderPass: 0x" << std::hex << reinterpret_cast<uintptr_t>(pass) << std::dec << "\n";
    assert(pipe == pipelineState.pipeline && "Pipeline should match");
    assert(layout == pipelineState.layout && "Layout should match");
    assert(pass == pipelineState.renderPass && "RenderPass should match");
    std::cout << "  ✅ Multiple extractor usage successful\n";

    // Test 8: Buffer collection
    TestBufferCollection buffers;
    buffers.vertexBuffer = reinterpret_cast<VkBuffer>(0x4001);
    buffers.indexBuffer = reinterpret_cast<VkBuffer>(0x4002);
    buffers.uniformBuffers = {
        reinterpret_cast<VkBuffer>(0x4101),
        reinterpret_cast<VkBuffer>(0x4102)
    };

    auto vertexExtractor = Field(&TestBufferCollection::vertexBuffer);
    auto uniformsExtractor = Field(&TestBufferCollection::uniformBuffers);

    const VkBuffer& vertex = vertexExtractor.extract(buffers);
    const std::vector<VkBuffer>& uniforms = uniformsExtractor.extract(buffers);

    std::cout << "\nTest 8: Buffer collection extraction\n";
    std::cout << "  Vertex buffer: 0x" << std::hex << reinterpret_cast<uintptr_t>(vertex) << std::dec << "\n";
    std::cout << "  Uniform buffers count: " << uniforms.size() << "\n";
    assert(vertex == buffers.vertexBuffer && "Vertex buffer should match");
    assert(uniforms.size() == 2 && "Uniform buffer count should match");
    std::cout << "  ✅ Buffer collection extraction successful\n";
}

// ============================================================================
// TYPE INFORMATION DISPLAY
// ============================================================================

template<typename Extractor>
void printExtractorInfo(const char* name) {
    using StructType = ExtractorStructType_t<Extractor>;
    using FieldType = ExtractorFieldType_t<Extractor>;

    std::cout << "  " << name << ":\n";
    std::cout << "    IsFieldExtractor: " << IsFieldExtractor_v<Extractor> << "\n";
    std::cout << "    Field type valid: " << ResourceTypeTraits<FieldType>::isValid << "\n";
    std::cout << "    Field is vector: " << ResourceTypeTraits<FieldType>::isVector << "\n";
    std::cout << "    Field is array: " << ResourceTypeTraits<FieldType>::isArray << "\n";
}

void testTypeIntrospection() {
    std::cout << "\n=== Type Introspection Tests ===\n\n";

    std::cout << "SwapChain field extractors:\n";
    printExtractorInfo<decltype(Field(&TestSwapChainVariables::images))>("images");
    printExtractorInfo<decltype(Field(&TestSwapChainVariables::swapchain))>("swapchain");
    printExtractorInfo<decltype(Field(&TestSwapChainVariables::format))>("format");
    printExtractorInfo<decltype(Field(&TestSwapChainVariables::imageCount))>("imageCount");

    std::cout << "\nPipeline field extractors:\n";
    printExtractorInfo<decltype(Field(&TestPipelineState::pipeline))>("pipeline");
    printExtractorInfo<decltype(Field(&TestPipelineState::layout))>("layout");

    std::cout << "\nBuffer field extractors:\n";
    printExtractorInfo<decltype(Field(&TestBufferCollection::vertexBuffer))>("vertexBuffer");
    printExtractorInfo<decltype(Field(&TestBufferCollection::uniformBuffers))>("uniformBuffers");

    std::cout << "\n✅ Type introspection complete\n";
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  FIELD EXTRACTION TEST SUITE\n";
    std::cout << "  Trimmed Build Compatible (Headers Only)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    testFieldExtraction();
    testTypeIntrospection();

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  ✅ ALL TESTS PASSED!\n";
    std::cout << "  Compile-time checks: PASSED\n";
    std::cout << "  Runtime extraction: PASSED\n";
    std::cout << "  Type introspection: PASSED\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    return 0;
}
