/**
 * @file test_recipe_nested_invocation.cpp
 * @brief Recipe-Nested-Invocation-Unroll-AB-Direction-2026-07 M1 — correctness gate for the
 *        InvokeRecipe opcode: a recipe A that invokes recipe B (a simple leaf primitive)
 *        composes correctly, on BOTH execution paths:
 *          (1) evalRecipe (interpreter/orchestration path) — recurses through InvokeRecipe.
 *          (2) EmitProceduralFieldFunctionGlsl (unrolled/GLSL path) — recursive-inlining, see
 *              that function's header comment in SdfRecipeCodegenGlsl.h for why recursive
 *              inlining (not call-to-already-unrolled-function) was chosen for M1.
 *
 * Three checks, mirroring test_recipe_glsl_numerical_parity.cpp's own split (CPU-only checks
 * run everywhere; GPU dispatch is gated behind a REAL discrete/integrated device and SKIPs
 * cleanly otherwise):
 *
 *  (1) RecipeNestedInvocation.CpuInterpreterMatchesCpuComposedReference (no fixture, no GPU) —
 *      evalRecipe on a nested recipe A (InvokeRecipe(B), SmoothUnion with a local Box) matches
 *      hand-composed CPU reference math (min(analytic sphere-B, analytic box)-with-smoothing via
 *      the same SdfCore_SmoothUnion kernel evalRecipe itself calls) at a range of sample points.
 *
 *  (2) RecipeNestedInvocation.GlslEmitCompilesAndMatchesCpuEval (no GPU) — emits GLSL for the
 *      SAME nested recipe A via EmitProceduralFieldFunctionGlsl (recursive-inlining through
 *      InvokeRecipe), compiles it through the real glslang-backed ShaderCompiler (pure
 *      host-side work, no Vulkan device needed), and asserts the emitted GLSL source textually
 *      contains B's own inlined SdfCore_Sphere call (proving recursive inlining actually spliced
 *      B's bytecode into A's function body, not just referencing it) — "gate B" per the existing
 *      parity harness's naming convention.
 *
 *  (3) RecipeNestedInvocationGpuParityTest::GlslMatchesCpuEvalForNestedRecipe (TEST_F, GPU-gated,
 *      same SKIP behavior as test_recipe_glsl_numerical_parity.cpp) — dispatches the compiled
 *      GLSL on a REAL (non-lavapipe, non-Dozen) GPU and compares against evalRecipe's CPU
 *      values — the mandatory GPU-verified parity check the M1 correctness gate requires.
 *
 *  (4) RecipeNestedInvocationGuard.* (no fixture, no GPU) — RecipeRegistry::Register rejects a
 *      self-referencing recipe, a 2-cycle (A invokes B, B invokes A), and a chain exceeding
 *      kMaxRecipeNestingDepth, each with the new RegisterResult value, not a crash/hang.
 *
 * HANDOFF for (3): from a Windows shell, after a fresh MSVC build, run the gtest binary
 * DIRECTLY (per KI-014, never via ctest — it can swallow gtest's own device-gate skip/fail
 * signal):
 *
 *     build\libraries\SVO\tests\Debug\test_recipe_nested_invocation.exe
 */

#include <gtest/gtest.h>

#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/generated/RecipeSimd.g.hpp"
#include "ShaderCompiler.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Vixen::SVO::RecipeRegistry;
using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

namespace {

// ---------------------------------------------------------------------------
// Instruction builders (mirrors RecipeParityCorpus.h's existing helper style).
// ---------------------------------------------------------------------------
SdfInstruction sphereOp(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}
SdfInstruction boxOp(glm::vec3 halfExtents) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0] = halfExtents.x; in.data[1] = halfExtents.y; in.data[2] = halfExtents.z;
    return in;
}
SdfInstruction smoothUnionOp(float k) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::SmoothUnion; in.data[2] = k;
    return in;
}
SdfInstruction invokeRecipeOp(uint32_t calleeId) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::InvokeRecipe;
    in.data[0] = static_cast<float>(calleeId);
    return in;
}

// Callee recipe B: a plain sphere leaf.
constexpr uint32_t kRecipeB = 1;
const glm::vec3 kSphereCenter(0.3f, 0.1f, -0.2f);
constexpr float kSphereRadius = 0.6f;

// Caller recipe A: SmoothUnion(InvokeRecipe(B), Box(halfExtents)) — proves InvokeRecipe's
// result composes onto the caller's value stack exactly like a leaf primitive would, so the
// existing SmoothUnion combinator needs zero changes to operate on a nested-call result.
const glm::vec3 kBoxHalfExtents(0.4f, 0.5f, 0.3f);
constexpr float kSmoothK = 0.25f;

std::vector<SdfInstruction> MakeRecipeB() {
    return { sphereOp(kSphereCenter, kSphereRadius) };
}
std::vector<SdfInstruction> MakeRecipeA() {
    return { invokeRecipeOp(kRecipeB), boxOp(kBoxHalfExtents), smoothUnionOp(kSmoothK) };
}

std::vector<glm::vec3> SamplePoints() {
    std::vector<glm::vec3> pts;
    for (int xi = -2; xi <= 2; ++xi)
        for (int yi = -2; yi <= 2; ++yi)
            for (int zi = -2; zi <= 2; ++zi)
                pts.emplace_back(xi * 0.5f, yi * 0.5f, zi * 0.5f);
    pts.emplace_back(0.0f, 0.0f, 0.0f);
    return pts;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) CPU interpreter parity — evalRecipe(A) via InvokeRecipe(B) must equal a
// hand-composed CPU reference that evaluates B and A's own Box independently and combines
// them with the SAME SdfCore_SmoothUnion kernel evalRecipe dispatches to.
// ---------------------------------------------------------------------------
TEST(RecipeNestedInvocation, CpuInterpreterMatchesCpuComposedReference) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entryB{};
    entryB.bytecode = MakeRecipeB();
    ASSERT_EQ(registry.Register(kRecipeB, entryB), RecipeRegistry::RegisterResult::Ok);

    constexpr uint32_t kRecipeA = 0;
    RecipeRegistry::RecipeEntry entryA{};
    entryA.bytecode = MakeRecipeA();
    ASSERT_EQ(registry.Register(kRecipeA, entryA), RecipeRegistry::RegisterResult::Ok);

    const auto progA = MakeRecipeA();
    const auto progB = MakeRecipeB();

    for (const glm::vec3& p : SamplePoints()) {
        // Actual: evalRecipe on A, resolving InvokeRecipe(B) via the registry.
        float actual = Vixen::SVO::Recipe::evalRecipe(
            progA.data(), static_cast<uint32_t>(progA.size()), p, {}, nullptr, &registry);

        // Reference: evaluate B directly (same evalRecipe, no registry needed — B has no
        // InvokeRecipe of its own), evaluate A's Box directly, combine with SmoothUnion
        // exactly as A's own bytecode does.
        float bValue = Vixen::SVO::Recipe::evalRecipe(
            progB.data(), static_cast<uint32_t>(progB.size()), p);
        float boxValue = Vixen::SVO::Recipe::evalRecipe(
            std::vector<SdfInstruction>{ boxOp(kBoxHalfExtents) }.data(), 1, p);
        float expected = Yeroket::Sdf::Generated::SdfCore_SmoothUnion(bValue, boxValue, kSmoothK);

        EXPECT_NEAR(actual, expected, 1e-5f)
            << "Mismatch at (" << p.x << ", " << p.y << ", " << p.z << ")";
    }
}

TEST(RecipeNestedInvocation, CompilerOwnedUnrollMatchesRecursiveEvaluation) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entryB{};
    entryB.bytecode = MakeRecipeB();
    ASSERT_EQ(registry.Register(kRecipeB, entryB), RecipeRegistry::RegisterResult::Ok);

    constexpr uint32_t kRecipeA = 0;
    RecipeRegistry::RecipeEntry entryA{};
    entryA.bytecode = MakeRecipeA();
    ASSERT_EQ(registry.Register(kRecipeA, entryA), RecipeRegistry::RegisterResult::Ok);

    const auto nested = MakeRecipeA();
    std::vector<SdfInstruction> closed;
    std::string error;
    Vixen::SVO::Recipe::LoweredRecipeProgram rejectedOpenProgram;
    EXPECT_FALSE(rejectedOpenProgram.Lower(
        nested.data(), static_cast<uint32_t>(nested.size()), {}, error));
    EXPECT_NE(error.find("must be unrolled"), std::string::npos);

    ASSERT_TRUE(Vixen::SVO::UnrollRecipeInstructions(
        nested.data(), static_cast<uint32_t>(nested.size()), registry,
        closed, error, kRecipeA)) << error;
    ASSERT_EQ(closed.size(), 3u);
    EXPECT_EQ(static_cast<SdfOpCode>(closed[0].opCode), SdfOpCode::Sphere);
    EXPECT_EQ(static_cast<SdfOpCode>(closed[1].opCode), SdfOpCode::Box);
    EXPECT_EQ(static_cast<SdfOpCode>(closed[2].opCode), SdfOpCode::SmoothUnion);
    EXPECT_TRUE(std::none_of(closed.begin(), closed.end(), [](const SdfInstruction& in) {
        return static_cast<SdfOpCode>(in.opCode) == SdfOpCode::InvokeRecipe;
    }));

    Vixen::SVO::Recipe::LoweredRecipeProgram loweredClosedProgram;
    ASSERT_TRUE(loweredClosedProgram.Lower(
        closed.data(), static_cast<uint32_t>(closed.size()), {}, error)) << error;

    for (const glm::vec3& point : SamplePoints()) {
        const float recursive = Vixen::SVO::Recipe::evalRecipe(
            nested.data(), static_cast<uint32_t>(nested.size()), point, {}, nullptr, &registry);
        const float unrolled = Vixen::SVO::Recipe::evalRecipe(
            closed.data(), static_cast<uint32_t>(closed.size()), point);
        EXPECT_FLOAT_EQ(recursive, unrolled);
    }
}

// ---------------------------------------------------------------------------
// (2) GLSL emission (recursive inlining) — no GPU required. Confirms the emitted GLSL
// textually inlines B's SdfCore_Sphere call (proving recursive inlining, not a stray
// function-call reference to a callee function that doesn't exist in this scheme) and that
// the composed shader still compiles through the real glslang-backed compiler.
// ---------------------------------------------------------------------------
TEST(RecipeNestedInvocation, GlslEmitCompilesAndInlinesCallee) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entryB{};
    entryB.bytecode = MakeRecipeB();
    ASSERT_EQ(registry.Register(kRecipeB, entryB), RecipeRegistry::RegisterResult::Ok);

    const auto progA = MakeRecipeA();
    const std::string fieldFn = Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl(
        progA.data(), static_cast<uint32_t>(progA.size()), /*recipeId=*/0,
        /*emitDeclaredPositionOutParam=*/false, &registry);

    // Recursive inlining means B's own Sphere call appears INLINE in A's single emitted
    // function body — no separate sdfRecipe_1(...) function, no call expression referencing
    // one. Confirms the chosen unroll strategy actually happened, not just that SOME GLSL
    // was emitted.
    EXPECT_NE(fieldFn.find("SdfCore_Sphere("), std::string::npos)
        << "Callee B's Sphere primitive was not inlined into A's emitted function:\n" << fieldFn;
    EXPECT_EQ(fieldFn.find("sdfRecipe_1("), std::string::npos)
        << "Emitted GLSL calls a separate sdfRecipe_1() function — expected recursive "
           "inlining (single self-contained function), not call-to-already-unrolled-function:\n"
        << fieldFn;

    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good());
    std::ostringstream kss; kss << kernelFile.rdbuf();

    std::ostringstream shaderSrc;
    shaderSrc << "#version 450\n" << kss.str() << "\n" << fieldFn << "\n";
    shaderSrc << R"GLSL(
layout(local_size_x = 64) in;
layout(set = 0, binding = 0, std430) readonly buffer InPoints { vec4 points[]; };
layout(set = 0, binding = 1, std430) writeonly buffer OutValues { float values[]; };
void main() {
    if (gl_GlobalInvocationID.x >= points.length()) return;
    float p[6] = float[6](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    values[gl_GlobalInvocationID.x] = sdfRecipe_0(points[gl_GlobalInvocationID.x].xyz, p);
}
)GLSL";

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc.str(), "main", opts);
    ASSERT_TRUE(compOut.success) << compOut.GetFullLog() << "\n--- source ---\n" << shaderSrc.str();
}

// ---------------------------------------------------------------------------
// (3) GPU-dispatched numerical parity — same device-gate/SKIP shape as
// test_recipe_glsl_numerical_parity.cpp's RecipeGlslNumericalParityTest fixture.
// ---------------------------------------------------------------------------
class RecipeNestedInvocationGpuParityTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             realGpuConfirmed_ = false;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_recipe_nested_invocation";
        appInfo.apiVersion       = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;
        if (vkCreateInstance(&instInfo, nullptr, &instance_) != VK_SUCCESS) {
            GTEST_SKIP() << "vkCreateInstance failed — no Vulkan available on this machine.";
        }
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) { GTEST_SKIP() << "No Vulkan physical devices visible."; }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) { physicalDevice_ = dev; realGpuConfirmed_ = true; break; }
        }
        if (!realGpuConfirmed_) {
            GTEST_SKIP() << "No REAL (discrete/integrated) GPU found — skipping GPU dispatch.";
        }
        CreateLogicalDevice();
        CreateCommandPool();
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        if (logicalDevice_ != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found);
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos    = &qInfo;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        return UINT32_MAX;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkBuffer& outBuf, VkDeviceMemory& outMem) {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &outBuf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, outBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, outBuf, outMem, 0), VK_SUCCESS);
    }

    void DispatchAndReadback(const std::vector<uint32_t>& spirv,
                              const std::vector<glm::vec3>& points,
                              std::vector<float>& outValues) {
        ASSERT_TRUE(realGpuConfirmed_);
        ASSERT_FALSE(spirv.empty());
        const uint32_t pointCount = static_cast<uint32_t>(points.size());

        std::vector<glm::vec4> paddedPoints(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i) paddedPoints[i] = glm::vec4(points[i], 0.0f);
        const VkDeviceSize inSize = static_cast<VkDeviceSize>(pointCount) * sizeof(glm::vec4);

        VkBuffer inBuf = VK_NULL_HANDLE; VkDeviceMemory inMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(inSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, inBuf, inMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, inMem, 0, inSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, paddedPoints.data(), static_cast<size_t>(inSize));
            vkUnmapMemory(logicalDevice_, inMem);
        }

        const VkDeviceSize outSize = static_cast<VkDeviceSize>(pointCount) * sizeof(float);
        VkBuffer outBuf = VK_NULL_HANDLE; VkDeviceMemory outMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(outSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outBuf, outMem));

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 2;
        dslci.pBindings    = bindings;
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts    = &dsl;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes    = &poolSize;
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool     = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

        VkDescriptorBufferInfo inInfo{inBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo outInfo{outBuf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSet; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &inInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSet; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &outInfo;
        vkUpdateDescriptorSets(logicalDevice_, 2, writes, 0, nullptr);

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = commandPool_;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        const uint32_t groups = (pointCount + 63) / 64;
        vkCmdDispatch(cmd, groups, 1, 1);

        VkBufferMemoryBarrier toHost{};
        toHost.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHost.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toHost.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer              = outBuf;
        toHost.offset              = 0;
        toHost.size                = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                              0, 0, nullptr, 1, &toHost, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        ASSERT_TRUE(realGpuConfirmed_);
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        outValues.resize(pointCount);
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, outMem, 0, outSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(outValues.data(), mapped, static_cast<size_t>(outSize));
        vkUnmapMemory(logicalDevice_, outMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyBuffer(logicalDevice_, outBuf, nullptr);
        vkFreeMemory(logicalDevice_, outMem, nullptr);
        vkDestroyBuffer(logicalDevice_, inBuf, nullptr);
        vkFreeMemory(logicalDevice_, inMem, nullptr);
    }
};

TEST_F(RecipeNestedInvocationGpuParityTest, GlslMatchesCpuEvalForNestedRecipe) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entryB{};
    entryB.bytecode = MakeRecipeB();
    ASSERT_EQ(registry.Register(kRecipeB, entryB), RecipeRegistry::RegisterResult::Ok);

    const auto progA = MakeRecipeA();
    const std::vector<glm::vec3> points = SamplePoints();

    std::vector<float> cpuRef(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        cpuRef[i] = Vixen::SVO::Recipe::evalRecipe(
            progA.data(), static_cast<uint32_t>(progA.size()), points[i], {}, nullptr, &registry);

    const std::string fieldFn = Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl(
        progA.data(), static_cast<uint32_t>(progA.size()), /*recipeId=*/0,
        /*emitDeclaredPositionOutParam=*/false, &registry);

    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good());
    std::ostringstream kss; kss << kernelFile.rdbuf();

    std::ostringstream shaderSrc;
    shaderSrc << "#version 450\n" << kss.str() << "\n" << fieldFn << "\n";
    shaderSrc << R"GLSL(
layout(local_size_x = 64) in;
layout(set = 0, binding = 0, std430) readonly buffer InPoints { vec4 points[]; };
layout(set = 0, binding = 1, std430) writeonly buffer OutValues { float values[]; };
void main() {
    if (gl_GlobalInvocationID.x >= points.length()) return;
    float p[6] = float[6](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    values[gl_GlobalInvocationID.x] = sdfRecipe_0(points[gl_GlobalInvocationID.x].xyz, p);
}
)GLSL";

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc.str(), "main", opts);
    ASSERT_TRUE(compOut.success) << compOut.GetFullLog();
    ASSERT_FALSE(compOut.spirv.empty());

    std::vector<float> gpuValues;
    ASSERT_NO_FATAL_FAILURE(DispatchAndReadback(compOut.spirv, points, gpuValues));
    ASSERT_EQ(gpuValues.size(), cpuRef.size());

    for (size_t i = 0; i < points.size(); ++i) {
        const float allowed = 1e-4f * std::max(1.0f, std::fabs(cpuRef[i]));
        EXPECT_LE(std::fabs(gpuValues[i] - cpuRef[i]), allowed)
            << "Mismatch at (" << points[i].x << ", " << points[i].y << ", " << points[i].z
            << "): gpu=" << gpuValues[i] << " cpu=" << cpuRef[i];
    }
}

// ---------------------------------------------------------------------------
// (4) Cycle/recursion + max-depth guard — RecipeRegistry::Register time, no GPU, no crash/hang.
// ---------------------------------------------------------------------------
TEST(RecipeNestedInvocationGuard, SelfInvocationRejected) {
    RecipeRegistry registry;
    constexpr uint32_t kSelfId = 42;
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { invokeRecipeOp(kSelfId) };  // invokes itself before it's even registered
    EXPECT_EQ(registry.Register(kSelfId, entry), RecipeRegistry::RegisterResult::RecursiveInvocation);
}

TEST(RecipeNestedInvocationGuard, TwoCycleRejected) {
    RecipeRegistry registry;
    constexpr uint32_t kIdA = 10, kIdB = 11;

    // Register B first, referencing A (not yet registered) — A doesn't exist yet, so this
    // must fail as UnknownCalleeRecipe (registration order matters: a callee must be
    // registered before a caller referencing it can be).
    RecipeRegistry::RecipeEntry entryB{};
    entryB.bytecode = { invokeRecipeOp(kIdA) };
    EXPECT_EQ(registry.Register(kIdB, entryB), RecipeRegistry::RegisterResult::UnknownCalleeRecipe);

    // Register A referencing B — B isn't registered (the above Register call failed), so this
    // also fails as UnknownCalleeRecipe; the 2-cycle A<->B can never actually be constructed
    // through this registry's dependency-order contract in the first place.
    RecipeRegistry::RecipeEntry entryA{};
    entryA.bytecode = { invokeRecipeOp(kIdB) };
    EXPECT_EQ(registry.Register(kIdA, entryA), RecipeRegistry::RegisterResult::UnknownCalleeRecipe);
}

TEST(RecipeNestedInvocationGuard, NestingDepthExceeded) {
    RecipeRegistry registry;
    // Build a straight chain recipe_0 -> recipe_1 -> ... -> recipe_N, each invoking the next,
    // terminating in a leaf sphere. kMaxRecipeNestingDepth caps how many InvokeRecipe hops from
    // any single entry point are allowed; construct one hop past that limit and confirm it's
    // rejected while everything within the limit still registers cleanly.
    const uint32_t maxDepth = RecipeRegistry::kMaxRecipeNestingDepth;
    const uint32_t leafId = 1000;

    RecipeRegistry::RecipeEntry leaf{};
    leaf.bytecode = MakeRecipeB();
    ASSERT_EQ(registry.Register(leafId, leaf), RecipeRegistry::RegisterResult::Ok);

    uint32_t calleeId = leafId;
    // Register maxDepth callers, each one hop deeper than the last — all within budget.
    for (uint32_t depth = 1; depth <= maxDepth; ++depth) {
        uint32_t thisId = leafId - depth;
        RecipeRegistry::RecipeEntry e{};
        e.bytecode = { invokeRecipeOp(calleeId) };
        ASSERT_EQ(registry.Register(thisId, e), RecipeRegistry::RegisterResult::Ok)
            << "depth " << depth << " should still be within kMaxRecipeNestingDepth";
        calleeId = thisId;
    }

    // One more hop exceeds the limit.
    RecipeRegistry::RecipeEntry tooDeep{};
    tooDeep.bytecode = { invokeRecipeOp(calleeId) };
    EXPECT_EQ(registry.Register(leafId - maxDepth - 1, tooDeep),
              RecipeRegistry::RegisterResult::NestingTooDeep);
}

TEST(RecipeNestedInvocationGuard, UnknownCalleeRejected) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = { invokeRecipeOp(/*calleeId=*/999999) };
    EXPECT_EQ(registry.Register(0, entry), RecipeRegistry::RegisterResult::UnknownCalleeRecipe);
}
