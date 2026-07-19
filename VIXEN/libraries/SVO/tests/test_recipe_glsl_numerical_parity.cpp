/**
 * @file test_recipe_glsl_numerical_parity.cpp
 * @brief Lazy-Procedural-Delta-Baseline Inc1 M4 Task 9 — GLSL field-function
 *        numerical-parity harness: proves EmitProceduralFieldFunctionGlsl's output,
 *        when run on a REAL (non-lavapipe) GPU, produces the same sdf values as the
 *        CPU reference VM (evalRecipe), for every program in ParityCorpus::GetAll().
 *
 * SPLIT (this file runs three checks; only one needs a GPU):
 *
 *  (1) RecipeGlslCompiles (standalone TEST, no fixture) — runs on EVERY machine,
 *      CI or dev, GPU or not:
 *        - Emit GLSL via EmitProceduralFieldFunctionGlsl() for every corpus program.
 *        - Compile the composed GLSL (SdfCoreKernels.glsl + emitted function + a
 *          small compute wrapper) through the REAL glslang-backed ShaderCompiler.
 *          This is pure CPU/host-side work — glslang does not touch a GPU device —
 *          so it runs unconditionally and asserts success for all 88 programs. This
 *          is "gate B": proof the emitted GLSL actually round-trips through the real
 *          compiler, provable on this machine without any Vulkan device at all.
 *
 *  (2) RecipeGlslOpcodeCoverage (standalone TEST, no fixture) — also pure CPU: it
 *      proves every RecipeRegistry::IsValidSdfOpCode() opcode appears somewhere in
 *      the corpus, so the coverage claim can't silently rot as opcodes are added.
 *
 *  (3) RecipeGlslNumericalParityTest::GlslMatchesCpuEvalAcrossCorpus (TEST_F) — only
 *      runs when a REAL (discrete/integrated, non-software, non-Dozen) Vulkan GPU is
 *      present: CPU-evals the corpus via evalRecipe() (ground truth), emits + compiles
 *      GLSL per program, uploads the fixed sample-point grid, dispatches the compiled
 *      SPIR-V compute shader, reads back GPU-evaluated sdf values, and compares them
 *      to the CPU evalRecipe() values within NearlyEqual's tolerance. On a machine
 *      with no such device (this one — WSL2, no Vulkan ICD, not even lavapipe is
 *      permitted here per project policy) the fixture's SetUp() calls GTEST_SKIP()
 *      and this TEST_F reports SKIPPED rather than FAILED/CRASHED — but (1) and (2)
 *      above still run and still assert, since they aren't part of this fixture. Every
 *      corpus program is dispatched with a ZERO-FILLED params[6] (matching evalRecipe()'s
 *      own default-empty-span behavior) — this test proves structural opcode coverage
 *      including ReadParam/ReadParamFloat3's bounds-check-fail-safe path, NOT the
 *      params-vary-without-recompile claim; that is (4) below.
 *
 *  (4) RecipeGlslNumericalParityTest::ReadParamSweepAcrossValuesWithoutRecompile (TEST_F,
 *      Recipe-Parameterization M2 Task 7) — the harder, more important check the M1-era
 *      harness above does not cover: compiles ONE ReadParam/ReadParamFloat3-using corpus
 *      program to SPIR-V EXACTLY ONCE, then re-dispatches that SAME compiled module
 *      several times with DIFFERENT params[6] buffer contents (never re-invoking
 *      glslang between dispatches), asserting CPU evalRecipe(prog, p, params) and GPU
 *      sdfRecipe_0(p, params) agree at EACH value. This is the actual property P4 exists
 *      for — params vary without a shader recompile — which a per-program-fixed-value
 *      corpus sweep cannot exercise on its own. Same GPU-required/SKIP behavior as (3).
 *
 * HANDOFF — run part (2) on Windows-native (never through lavapipe; this harness's
 * IsRealGpu() gate explicitly excludes both llvmpipe/lavapipe AND Dozen (WARP/d3d12
 * software-over-WSL) — this is a REAL-hardware-only proof). From a Windows shell,
 * after a fresh MSVC build (see repo build docs), run the gtest binary DIRECTLY —
 * per KI-014, do NOT invoke via ctest, which can swallow gtest's own device-gate
 * skip/fail signal:
 *
 *     build\libraries\SVO\tests\Debug\test_recipe_glsl_numerical_parity.exe
 *
 * (Substitute Release for Debug per your configure.) No VK_ICD_FILENAMES override is
 * needed on a machine with a real GPU driver installed — the loader's default ICD
 * discovery finds it directly; VK_ICD_FILENAMES is only relevant for forcing a
 * specific ICD (e.g. to compare against a software path), which this harness
 * deliberately refuses to run against.
 *
 * Design notes:
 *   - Sample grid: a 5x5x5 lattice over [-3,3]^3 (125 points) plus a handful of
 *     extra points at likely singularities (origin, axis points) — kept modest since
 *     it runs per-corpus-program (88 programs today).
 *   - Tolerance: NearlyEqual(gpu, cpu, tol=1e-4f) uses a RELATIVE test with an
 *     absolute floor: |gpu-cpu| <= tol * max(1.0f, |cpu|). This is deliberately
 *     generous headroom for transcendental-function (sin/cos/exp/log/sqrt/pow) drift
 *     between glslang's GPU math and libm's CPU math. Tolerance may need loosening
 *     once this is actually run on real hardware — record what's actually needed in
 *     a follow-up if this proves too tight (or too loose to catch real bugs).
 */

#include <gtest/gtest.h>

#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "Recipe/SdfRecipeEval.h"
#include "ShaderCompiler.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDF_CORE_KERNELS_GLSL_PATH
#  error "SDF_CORE_KERNELS_GLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

using Vixen::SVO::Recipe::SdfInstruction;

// ---------------------------------------------------------------------------
// Tolerance helper — see file header comment for rationale.
// ---------------------------------------------------------------------------
bool NearlyEqual(float gpu, float cpu, float tol = 1e-4f) {
    const float allowed = tol * std::max(1.0f, std::fabs(cpu));
    return std::fabs(gpu - cpu) <= allowed;
}

// ---------------------------------------------------------------------------
// Fixed sample-point grid: 5x5x5 lattice over [-3,3]^3 plus a few singularity
// probes. ~150 points total, evaluated per corpus program.
// ---------------------------------------------------------------------------
std::vector<glm::vec3> BuildSamplePoints() {
    std::vector<glm::vec3> pts;
    pts.reserve(125 + 8);
    for (int xi = 0; xi < 5; ++xi) {
        for (int yi = 0; yi < 5; ++yi) {
            for (int zi = 0; zi < 5; ++zi) {
                const float x = -3.0f + xi * 1.5f;
                const float y = -3.0f + yi * 1.5f;
                const float z = -3.0f + zi * 1.5f;
                pts.emplace_back(x, y, z);
            }
        }
    }
    // Extra points at likely singularities: origin + axis points.
    pts.emplace_back(0.0f, 0.0f, 0.0f);
    pts.emplace_back(1.0f, 0.0f, 0.0f);
    pts.emplace_back(-1.0f, 0.0f, 0.0f);
    pts.emplace_back(0.0f, 1.0f, 0.0f);
    pts.emplace_back(0.0f, -1.0f, 0.0f);
    pts.emplace_back(0.0f, 0.0f, 1.0f);
    pts.emplace_back(0.0f, 0.0f, -1.0f);
    pts.emplace_back(0.001f, 0.001f, 0.001f);  // near-origin, avoids exact 0/0 in some math ops
    return pts;
}

// ---------------------------------------------------------------------------
// Compose the full GLSL compute shader source for one corpus program:
//   #version 450 + SdfCoreKernels.glsl + emitted sdfRecipe_0(vec3, float[6]) + wrapper main().
//
// Recipe-Parameterization M2 Task 7: params are read from a THIRD SSBO binding (2), not
// baked as literals — this is what lets Task 7's dedicated sweep test below rewrite the
// params buffer and redispatch the SAME already-compiled SPIR-V module for several different
// parameter-array values, which is the actual property under test (params vary without
// recompile). Every corpus program in the main GlslMatchesCpuEvalAcrossCorpus loop and the
// RecipeGlslCompiles gate below still pass a caller-chosen (usually zero-filled) params
// buffer once per compile, matching evalRecipe()'s own default-empty-span CPU reference.
// ---------------------------------------------------------------------------
std::string ComposeComputeShader(const std::string& sdfCoreGlsl,
                                  const std::string& emittedFieldFn) {
    std::ostringstream ss;
    ss << "#version 450\n";
    ss << sdfCoreGlsl << "\n";
    ss << emittedFieldFn << "\n";
    ss << R"GLSL(
layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly buffer InPoints {
    vec4 points[];
};
layout(set = 0, binding = 1, std430) writeonly buffer OutValues {
    float values[];
};
layout(set = 0, binding = 2, std430) readonly buffer InParams {
    float params[6];
};

void main() {
    if (gl_GlobalInvocationID.x >= points.length()) return;
    float p[6] = float[6](params[0], params[1], params[2], params[3], params[4], params[5]);
    values[gl_GlobalInvocationID.x] = sdfRecipe_0(points[gl_GlobalInvocationID.x].xyz, p);
}
)GLSL";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype variant.
// Same shape as ComposeComputeShader above, but the wrapper main() also reads back the
// emitted function's `out vec3 declaredPos` into a 4th SSBO binding (3) — proving the
// out-param convention actually threads a value out of the compute shader, not just that it
// compiles. sdfRecipe_0 here is expected to have been emitted with
// emitDeclaredPositionOutParam=true (3-arg signature with the trailing out-param).
// ---------------------------------------------------------------------------
std::string ComposeComputeShaderWithDeclaredPosition(const std::string& sdfCoreGlsl,
                                                      const std::string& emittedFieldFn) {
    std::ostringstream ss;
    ss << "#version 450\n";
    ss << sdfCoreGlsl << "\n";
    ss << emittedFieldFn << "\n";
    ss << R"GLSL(
layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly buffer InPoints {
    vec4 points[];
};
layout(set = 0, binding = 1, std430) writeonly buffer OutValues {
    float values[];
};
layout(set = 0, binding = 2, std430) readonly buffer InParams {
    float params[6];
};
layout(set = 0, binding = 3, std430) writeonly buffer OutDeclaredPos {
    vec4 declaredPositions[];
};

void main() {
    if (gl_GlobalInvocationID.x >= points.length()) return;
    float p[6] = float[6](params[0], params[1], params[2], params[3], params[4], params[5]);
    vec3 declaredPos;
    values[gl_GlobalInvocationID.x] = sdfRecipe_0(points[gl_GlobalInvocationID.x].xyz, p, declaredPos);
    declaredPositions[gl_GlobalInvocationID.x] = vec4(declaredPos, 0.0);
}
)GLSL";
    return ss.str();
}

// ---------------------------------------------------------------------------
// GPU parity fixture. Device bring-up mirrors test_procedural_recipe_render.cpp's
// ProceduralRecipeRenderTest, but INVERTS the accept gate: this harness SKIPS
// (rather than asserting) when no REAL (discrete/integrated, non-software,
// non-Dozen) GPU is present, since lavapipe execution is forbidden for this task
// and Dozen-over-WSL is explicitly out of scope for M4's "Windows-native, real
// hardware" brief.
// ---------------------------------------------------------------------------
class RecipeGlslNumericalParityTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             realGpuConfirmed_ = false;
    std::string      selectedDeviceName_;

    // Strict: only DISCRETE_GPU / INTEGRATED_GPU count as "real" for this harness.
    // Unlike the M2 precedent test (which explicitly allows Dozen), M4's brief is
    // "Windows-native — handed off; NOT lavapipe", and Dozen-over-WSL is a grey area
    // that spec doesn't clear, so this harness excludes it too — CPU/software device
    // types (lavapipe/llvmpipe, Dozen/WARP reporting as CPU) never pass this gate.
    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_recipe_glsl_numerical_parity";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;

        if (vkCreateInstance(&instInfo, nullptr, &instance_) != VK_SUCCESS) {
            GTEST_SKIP() << "vkCreateInstance failed — no Vulkan available on this machine; "
                            "skipping GPU dispatch (see file header for the Windows-native "
                            "hand-off command).";
        }

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) {
            GTEST_SKIP() << "No Vulkan physical devices visible — skipping GPU dispatch.";
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                realGpuConfirmed_   = true;
                break;
            }
        }

        if (!realGpuConfirmed_) {
            GTEST_SKIP() << "No REAL (discrete/integrated) GPU found — only software "
                            "(lavapipe/llvmpipe) and/or Dozen devices are visible. Lavapipe "
                            "execution is forbidden for this task; skipping GPU dispatch. "
                            "See file header for the Windows-native hand-off command.";
        }

        CreateLogicalDevice();
        CreateCommandPool();
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyDevice(logicalDevice_, nullptr);
            logicalDevice_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
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
        ASSERT_TRUE(found) << "No compute queue family on the selected GPU";

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
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        }
        return UINT32_MAX;
    }

    // Host-visible/host-coherent buffer — mirrors ProceduralRecipeRenderTest::CreateHostBuffer.
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

    // Dispatch one compiled program against the sample-point grid; returns GPU sdf
    // values in outValues (one per input point). params (Recipe-Parameterization M2 Task 7,
    // default zero-filled to match evalRecipe()'s default-empty-span CPU reference) is
    // uploaded to a THIRD SSBO binding (2) — callers that want to exercise the "params vary
    // without recompile" claim can call this repeatedly with the SAME already-compiled
    // `spirv` blob and different `params` each time (glslang is never invoked again; only
    // the params buffer's contents change between calls).
    void DispatchAndReadback(const std::vector<uint32_t>& spirv,
                              const std::vector<glm::vec3>& points,
                              std::vector<float>& outValues,
                              const std::array<float, 6>& params = {}) {
        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());

        const uint32_t pointCount = static_cast<uint32_t>(points.size());

        // Input SSBO: vec4-padded points (std430 vec3 alignment simplicity).
        std::vector<glm::vec4> paddedPoints(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i)
            paddedPoints[i] = glm::vec4(points[i], 0.0f);
        const VkDeviceSize inSize = static_cast<VkDeviceSize>(pointCount) * sizeof(glm::vec4);

        VkBuffer inBuf = VK_NULL_HANDLE; VkDeviceMemory inMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(inSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, inBuf, inMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, inMem, 0, inSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, paddedPoints.data(), static_cast<size_t>(inSize));
            vkUnmapMemory(logicalDevice_, inMem);
        }

        // Output SSBO: one float per point.
        const VkDeviceSize outSize = static_cast<VkDeviceSize>(pointCount) * sizeof(float);
        VkBuffer outBuf = VK_NULL_HANDLE; VkDeviceMemory outMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(outSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outBuf, outMem));

        // Params SSBO: fixed 6 floats (mirrors BodyInstanceGpu::recipeParams[6]).
        const VkDeviceSize paramsSize = params.size() * sizeof(float);
        VkBuffer paramsBuf = VK_NULL_HANDLE; VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(paramsSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, paramsBuf, paramsMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, paramsMem, 0, paramsSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, params.data(), static_cast<size_t>(paramsSize));
            vkUnmapMemory(logicalDevice_, paramsMem);
        }

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 3;
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
        cpci.sType       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
                  VK_SUCCESS);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
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
        VkDescriptorBufferInfo paramsInfo{paramsBuf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = descSet;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo     = &inInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = descSet;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo     = &outInfo;
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = descSet;
        writes[2].dstBinding      = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo     = &paramsInfo;
        vkUpdateDescriptorSets(logicalDevice_, 3, writes, 0, nullptr);

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
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 pipelineLayout, 0, 1, &descSet, 0, nullptr);
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
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &toHost, 0, nullptr);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;

        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
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
        vkDestroyBuffer(logicalDevice_, paramsBuf, nullptr);
        vkFreeMemory(logicalDevice_, paramsMem, nullptr);
    }

    // Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype variant
    // of DispatchAndReadback above: same shape, plus a 4th SSBO (binding 3) that reads back
    // the emitted function's `out vec3 declaredPos` per sample point. `spirv` must have been
    // compiled from ComposeComputeShaderWithDeclaredPosition (a 4-binding pipeline layout),
    // not the plain 3-binding ComposeComputeShader.
    void DispatchAndReadbackWithDeclaredPosition(const std::vector<uint32_t>& spirv,
                                                  const std::vector<glm::vec3>& points,
                                                  std::vector<float>& outValues,
                                                  std::vector<glm::vec3>& outDeclaredPositions,
                                                  const std::array<float, 6>& params = {}) {
        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());

        const uint32_t pointCount = static_cast<uint32_t>(points.size());

        std::vector<glm::vec4> paddedPoints(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i)
            paddedPoints[i] = glm::vec4(points[i], 0.0f);
        const VkDeviceSize inSize = static_cast<VkDeviceSize>(pointCount) * sizeof(glm::vec4);

        VkBuffer inBuf = VK_NULL_HANDLE; VkDeviceMemory inMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(inSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, inBuf, inMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, inMem, 0, inSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, paddedPoints.data(), static_cast<size_t>(inSize));
            vkUnmapMemory(logicalDevice_, inMem);
        }

        const VkDeviceSize outSize = static_cast<VkDeviceSize>(pointCount) * sizeof(float);
        VkBuffer outBuf = VK_NULL_HANDLE; VkDeviceMemory outMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(outSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outBuf, outMem));

        const VkDeviceSize paramsSize = params.size() * sizeof(float);
        VkBuffer paramsBuf = VK_NULL_HANDLE; VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(paramsSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, paramsBuf, paramsMem));
        {
            void* mapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, paramsMem, 0, paramsSize, 0, &mapped), VK_SUCCESS);
            std::memcpy(mapped, params.data(), static_cast<size_t>(paramsSize));
            vkUnmapMemory(logicalDevice_, paramsMem);
        }

        // 4th binding: declared-position readback (vec4-padded, same std430 convention as points).
        const VkDeviceSize declPosSize = static_cast<VkDeviceSize>(pointCount) * sizeof(glm::vec4);
        VkBuffer declPosBuf = VK_NULL_HANDLE; VkDeviceMemory declPosMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(declPosSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, declPosBuf, declPosMem));

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[4]{};
        for (int i = 0; i < 4; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 4;
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
        cpci.sType       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
                  VK_SUCCESS);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
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
        VkDescriptorBufferInfo paramsInfo{paramsBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo declPosInfo{declPosBuf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[4]{};
        VkDescriptorBufferInfo* infos[4] = {&inInfo, &outInfo, &paramsInfo, &declPosInfo};
        for (int i = 0; i < 4; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = descSet;
            writes[i].dstBinding      = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo     = infos[i];
        }
        vkUpdateDescriptorSets(logicalDevice_, 4, writes, 0, nullptr);

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
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 pipelineLayout, 0, 1, &descSet, 0, nullptr);
        const uint32_t groups = (pointCount + 63) / 64;
        vkCmdDispatch(cmd, groups, 1, 1);

        VkBufferMemoryBarrier toHost[2]{};
        for (int i = 0; i < 2; ++i) {
            toHost[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            toHost[i].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
            toHost[i].dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
            toHost[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHost[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHost[i].offset              = 0;
            toHost[i].size                = VK_WHOLE_SIZE;
        }
        toHost[0].buffer = outBuf;
        toHost[1].buffer = declPosBuf;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 2, toHost, 0, nullptr);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;

        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        outValues.resize(pointCount);
        void* mappedOut = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, outMem, 0, outSize, 0, &mappedOut), VK_SUCCESS);
        std::memcpy(outValues.data(), mappedOut, static_cast<size_t>(outSize));
        vkUnmapMemory(logicalDevice_, outMem);

        std::vector<glm::vec4> paddedDeclPos(pointCount);
        void* mappedDecl = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, declPosMem, 0, declPosSize, 0, &mappedDecl), VK_SUCCESS);
        std::memcpy(paddedDeclPos.data(), mappedDecl, static_cast<size_t>(declPosSize));
        vkUnmapMemory(logicalDevice_, declPosMem);
        outDeclaredPositions.resize(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i)
            outDeclaredPositions[i] = glm::vec3(paddedDeclPos[i]);

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
        vkDestroyBuffer(logicalDevice_, paramsBuf, nullptr);
        vkFreeMemory(logicalDevice_, paramsMem, nullptr);
        vkDestroyBuffer(logicalDevice_, declPosBuf, nullptr);
        vkFreeMemory(logicalDevice_, declPosMem, nullptr);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// THE TEST — one program at a time (loop over the whole corpus inside a single
// TEST_F so a single SetUp/TearDown device lifetime is reused for all 88 programs).
// ---------------------------------------------------------------------------
TEST_F(RecipeGlslNumericalParityTest, GlslMatchesCpuEvalAcrossCorpus) {
    const std::vector<glm::vec3> samplePoints = BuildSamplePoints();
    const auto corpus = Vixen::SVO::Recipe::ParityCorpus::GetAll();
    ASSERT_FALSE(corpus.empty());

    ShaderManagement::ShaderCompiler compiler;

    // Step 1: read the vendored SdfCoreKernels GLSL (host-side).
    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    for (const auto& entry : corpus) {
        SCOPED_TRACE("corpus program: " + entry.name);

        // (1) CPU reference values — needed as the comparison baseline for this
        // fixture's GPU dispatch below. (Gate B — CPU-eval + emit + glslang-compile
        // for every corpus program, no GPU required — lives in the standalone
        // RecipeGlslCompiles test in this file, which runs on every machine.)
        std::vector<float> cpuRef(samplePoints.size());
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            cpuRef[i] = Vixen::SVO::Recipe::evalRecipe(
                entry.program.data(),
                static_cast<uint32_t>(entry.program.size()),
                samplePoints[i]);
        }

        // (2) Emit GLSL field function + compile it for this dispatch.
        const std::string fieldFn = Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl(
            entry.program.data(),
            static_cast<uint32_t>(entry.program.size()),
            /*recipeId=*/0);
        const std::string shaderSrc = ComposeComputeShader(sdfCoreGlsl, fieldFn);

        ShaderManagement::CompilationOptions opts;
        opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
        ASSERT_TRUE(compOut.success)
            << "GLSL compile failed for corpus program '" << entry.name << "':\n"
            << compOut.GetFullLog()
            << "\n--- emitted field function ---\n" << fieldFn
            << "\n--- full composed source (first 3000 chars) ---\n"
            << shaderSrc.substr(0, 3000);
        ASSERT_FALSE(compOut.spirv.empty());

        // (3) GPU dispatch + compare — only if a real GPU fixture is active (SetUp
        // didn't GTEST_SKIP; if it did, this whole TEST_F body never runs at all —
        // gtest reports the test as SKIPPED before reaching here).
        std::vector<float> gpuValues;
        ASSERT_NO_FATAL_FAILURE(DispatchAndReadback(compOut.spirv, samplePoints, gpuValues));
        ASSERT_EQ(gpuValues.size(), cpuRef.size());

        for (size_t i = 0; i < samplePoints.size(); ++i) {
            EXPECT_TRUE(NearlyEqual(gpuValues[i], cpuRef[i]))
                << "Mismatch for corpus program '" << entry.name << "' at point ("
                << samplePoints[i].x << ", " << samplePoints[i].y << ", " << samplePoints[i].z
                << "): gpu=" << gpuValues[i] << " cpu=" << cpuRef[i]
                << " |diff|=" << std::fabs(gpuValues[i] - cpuRef[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Recipe-Parameterization M2 Task 7 — the harder, more important check: compile ONE
// ReadParam/ReadParamFloat3-using program ONCE, then sweep SEVERAL DIFFERENT params[6]
// values through the SAME compiled SPIR-V module (never recompiling between them),
// confirming CPU evalRecipe(prog, p, params) and GPU sdfRecipe_0(p, params) agree at
// EACH value. This is the actual claim under test — param values vary without a shader
// recompile — which GlslMatchesCpuEvalAcrossCorpus above (one fixed zero-filled params
// buffer per program) does not exercise.
// ---------------------------------------------------------------------------
TEST_F(RecipeGlslNumericalParityTest, ReadParamSweepAcrossValuesWithoutRecompile) {
    const std::vector<glm::vec3> samplePoints = BuildSamplePoints();
    const auto corpus = Vixen::SVO::Recipe::ParityCorpus::GetAll();

    const auto it = std::find_if(corpus.begin(), corpus.end(), [](const auto& e) {
        return e.name == "M2_ReadParam_MatchesIndexedRead";
    });
    ASSERT_NE(it, corpus.end())
        << "M2_ReadParam_MatchesIndexedRead missing from ParityCorpus::GetAll()";
    const auto& entry = *it;

    ShaderManagement::ShaderCompiler compiler;
    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    // Compile EXACTLY ONCE — this SPIR-V module is reused verbatim for every params value
    // swept below, proving the recompile-avoidance claim (only the params SSBO's contents
    // change between vkCmdDispatch calls, never the shader module/pipeline).
    const std::string fieldFn = Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl(
        entry.program.data(), static_cast<uint32_t>(entry.program.size()), /*recipeId=*/0);
    const std::string shaderSrc = ComposeComputeShader(sdfCoreGlsl, fieldFn);
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "GLSL compile failed for '" << entry.name << "':\n" << compOut.GetFullLog();
    ASSERT_FALSE(compOut.spirv.empty());

    // Several distinct params[6] values, including one that deliberately exercises the
    // out-of-range bounds-check fail-safe (idx=0 is in range; the program only reads
    // params[0], so params[1..5] varying is inert but included for realism).
    const std::vector<std::array<float, 6>> paramSweep = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {7.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };

    for (const auto& params : paramSweep) {
        SCOPED_TRACE("params[0]=" + std::to_string(params[0]));

        std::vector<float> cpuRef(samplePoints.size());
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            cpuRef[i] = Vixen::SVO::Recipe::evalRecipe(
                entry.program.data(), static_cast<uint32_t>(entry.program.size()),
                samplePoints[i], std::span<const float>(params.data(), params.size()));
        }

        std::vector<float> gpuValues;
        ASSERT_NO_FATAL_FAILURE(
            DispatchAndReadback(compOut.spirv, samplePoints, gpuValues, params));
        ASSERT_EQ(gpuValues.size(), cpuRef.size());

        for (size_t i = 0; i < samplePoints.size(); ++i) {
            EXPECT_TRUE(NearlyEqual(gpuValues[i], cpuRef[i]))
                << "Mismatch at params[0]=" << params[0] << ", point ("
                << samplePoints[i].x << ", " << samplePoints[i].y << ", " << samplePoints[i].z
                << "): gpu=" << gpuValues[i] << " cpu=" << cpuRef[i]
                << " |diff|=" << std::fabs(gpuValues[i] - cpuRef[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype's own
// dedicated GPU parity test. Proves, on real hardware, the THREE claims the direction doc's
// "suggested first step" asks for:
//   (a) the declared position (meta segment's `out vec3 declaredPos`) matches between CPU
//       (evalRecipe's outDeclaredPos) and GPU (the emitted out-param, read back via SSBO),
//       and both match the actual ReadParam-supplied value;
//   (b) the resolve segment's own field value is correct GIVEN that position — i.e. changing
//       the declared position via ReadParam moves WHERE the shape renders (queried at a FIXED
//       world point, its sdf value changes as the declared position sweeps toward/away from
//       it), not just a disconnected reported number;
//   (c) compiled EXACTLY ONCE, re-dispatched with different params — mirrors P4's own
//       proven no-recompile invariant (ReadParamSweepAcrossValuesWithoutRecompile above),
//       confirmed to still hold for this new opcode.
// Same GPU-required/SKIP behavior as the rest of this fixture.
// ---------------------------------------------------------------------------
TEST_F(RecipeGlslNumericalParityTest, DeclaredPositionMatchesAcrossCpuAndGpu) {
    using namespace Vixen::SVO::Recipe;
    const std::vector<glm::vec3> samplePoints = BuildSamplePoints();

    // Meta segment: ReadParamFloat3(idx=0) + DeclarePosition. Resolve segment: Sphere(center=0,r=0.5).
    const SdfInstruction prog[] = {
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::ReadParamFloat3; in.paramMask = 1; in.data[0] = 0.0f; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::DeclarePosition; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere; in.data[3] = 0.5f; return in; }(),
    };
    constexpr uint32_t kCount = 3;

    ShaderManagement::ShaderCompiler compiler;
    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    // Compile EXACTLY ONCE (claim (c) above) — emitted WITH the out-param (emitDeclaredPositionOutParam=true).
    const std::string fieldFn = EmitProceduralFieldFunctionGlsl(
        prog, kCount, /*recipeId=*/0, /*emitDeclaredPositionOutParam=*/true);
    const std::string shaderSrc = ComposeComputeShaderWithDeclaredPosition(sdfCoreGlsl, fieldFn);

    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "GLSL compile failed for Inc6 M1 declared-position prototype:\n" << compOut.GetFullLog()
        << "\n--- emitted field function ---\n" << fieldFn;
    ASSERT_FALSE(compOut.spirv.empty());

    // Fixed query point in world space — used for claim (b): its sdf value must change as the
    // declared position sweeps toward/away from it.
    const glm::vec3 fixedQueryPoint(5.0f, 0.0f, 0.0f);

    const std::vector<glm::vec3> declaredPositionSweep = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(2.0f, 0.0f, 0.0f),
        glm::vec3(-1.5f, 3.0f, 0.75f),
        glm::vec3(5.0f, 0.0f, 0.0f),   // == fixedQueryPoint: sphere should land exactly on it
    };

    for (const glm::vec3& declared : declaredPositionSweep) {
        SCOPED_TRACE("declared=(" + std::to_string(declared.x) + "," +
                     std::to_string(declared.y) + "," + std::to_string(declared.z) + ")");
        const std::array<float, 6> params = {declared.x, declared.y, declared.z, 0.0f, 0.0f, 0.0f};

        // CPU reference: full sample grid + the fixed query point, plus outDeclaredPos capture.
        std::vector<float> cpuRef(samplePoints.size());
        glm::vec3 cpuDeclaredFromGrid(-999.0f);
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            cpuRef[i] = evalRecipe(prog, kCount, samplePoints[i],
                                    std::span<const float>(params.data(), params.size()),
                                    &cpuDeclaredFromGrid);
        }
        glm::vec3 cpuDeclaredAtQuery(-999.0f);
        float cpuAtFixedQuery = evalRecipe(prog, kCount, fixedQueryPoint,
                                            std::span<const float>(params.data(), params.size()),
                                            &cpuDeclaredAtQuery);

        // (a) CPU out-param must equal the ReadParam-supplied value exactly (evalRecipe is
        // deterministic C++, no GPU involved yet).
        EXPECT_NEAR(cpuDeclaredFromGrid.x, declared.x, 1e-6f);
        EXPECT_NEAR(cpuDeclaredFromGrid.y, declared.y, 1e-6f);
        EXPECT_NEAR(cpuDeclaredFromGrid.z, declared.z, 1e-6f);

        // GPU dispatch: same already-compiled SPIR-V, different params each iteration (claim (c)).
        std::vector<float> gpuValues;
        std::vector<glm::vec3> gpuDeclaredPositions;
        ASSERT_NO_FATAL_FAILURE(DispatchAndReadbackWithDeclaredPosition(
            compOut.spirv, samplePoints, gpuValues, gpuDeclaredPositions, params));
        ASSERT_EQ(gpuValues.size(), cpuRef.size());
        ASSERT_EQ(gpuDeclaredPositions.size(), cpuRef.size());

        for (size_t i = 0; i < samplePoints.size(); ++i) {
            EXPECT_TRUE(NearlyEqual(gpuValues[i], cpuRef[i]))
                << "Field-value mismatch at point (" << samplePoints[i].x << ","
                << samplePoints[i].y << "," << samplePoints[i].z << "): gpu=" << gpuValues[i]
                << " cpu=" << cpuRef[i];
            // (a) GPU out-param must match the CPU out-param (and thus the declared value)
            // at every dispatched invocation, not just invocation 0.
            EXPECT_NEAR(gpuDeclaredPositions[i].x, declared.x, 1e-4f) << "GPU declaredPos.x at sample " << i;
            EXPECT_NEAR(gpuDeclaredPositions[i].y, declared.y, 1e-4f) << "GPU declaredPos.y at sample " << i;
            EXPECT_NEAR(gpuDeclaredPositions[i].z, declared.z, 1e-4f) << "GPU declaredPos.z at sample " << i;
        }

        // (b) The resolve segment's field value at the FIXED query point must reflect the
        // declared position actually moving the shape: sdf(fixedQueryPoint) == -radius exactly
        // when declared == fixedQueryPoint, and > -radius (further from the surface, since the
        // sphere is elsewhere) otherwise. This is the crux check: a disconnected/reported-only
        // declared position would NOT correlate with this value at all.
        if (declared == fixedQueryPoint) {
            EXPECT_NEAR(cpuAtFixedQuery, -0.5f, 1e-5f)
                << "CPU: expected fixedQueryPoint to be the sphere's center when declared==fixedQueryPoint";
        } else {
            EXPECT_GT(cpuAtFixedQuery, -0.5f)
                << "CPU: expected fixedQueryPoint OUTSIDE the sphere's center when declared!=fixedQueryPoint";
        }
    }
}

// ---------------------------------------------------------------------------
// Gate B — glslang-compile gate, pure CPU/host-side. Standalone (non-fixture)
// TEST so it runs unconditionally on every machine, GPU or not (mirrors
// RecipeGlslOpcodeCoverage below, which is immune to the TEST_F's GTEST_SKIP()
// for the same reason). ShaderManagement::ShaderCompiler's ctor only calls
// glslang::InitializeProcess() — it never touches a Vulkan device — so this
// proves the emitter's output actually round-trips through the real compiler
// with no GPU required. Catches emitter syntax / undeclared-identifier /
// float-literal-class defects, which is exactly what the emitter is most
// likely to ship. Does NOT create a Vulkan device or dispatch anything, and
// does NOT compare GPU values to CPU evalRecipe() — that numerical comparison
// stays in the GPU-gated TEST_F above.
// ---------------------------------------------------------------------------
TEST(RecipeGlslCompiles, EmittedGlslCompilesForEveryCorpusProgram) {
    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good())
        << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    const auto corpus = Vixen::SVO::Recipe::ParityCorpus::GetAll();
    ASSERT_FALSE(corpus.empty());

    ShaderManagement::ShaderCompiler compiler;

    for (const auto& entry : corpus) {
        SCOPED_TRACE("corpus program: " + entry.name);

        const std::string fieldFn = Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl(
            entry.program.data(),
            static_cast<uint32_t>(entry.program.size()),
            /*recipeId=*/0);
        const std::string shaderSrc = ComposeComputeShader(sdfCoreGlsl, fieldFn);

        ShaderManagement::CompilationOptions opts;
        opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
        EXPECT_TRUE(compOut.success)
            << "GLSL compile failed for corpus program '" << entry.name << "':\n"
            << compOut.GetFullLog()
            << "\n--- emitted field function ---\n" << fieldFn
            << "\n--- full composed source (first 3000 chars) ---\n"
            << shaderSrc.substr(0, 3000);
        EXPECT_FALSE(compOut.spirv.empty());
    }
}

// ---------------------------------------------------------------------------
// Opcode-coverage assertion — pure CPU, no GPU, no glslang needed. Runs on every
// machine unconditionally (not part of the TEST_F fixture above, so it is
// immune to that fixture's GTEST_SKIP()).
//
// Proves: union of every SdfInstruction::opCode byte appearing across the whole
// ParityCorpus equals the full valid-opcode set accepted by
// Vixen::SVO::IsValidSdfOpCode(). If a new opcode is ever wired into
// RecipeRegistry without a corresponding corpus program exercising it, this
// test fails — the coverage claim can't silently rot.
// ---------------------------------------------------------------------------
TEST(RecipeGlslOpcodeCoverage, CorpusCoversEveryValidOpcode) {
    std::set<uint8_t> corpusOpcodes;
    for (const auto& entry : Vixen::SVO::Recipe::ParityCorpus::GetAll())
        for (const auto& instr : entry.program)
            corpusOpcodes.insert(instr.opCode);

    std::set<uint8_t> validOpcodes;
    for (int raw = 0; raw <= 255; ++raw)
        if (Vixen::SVO::IsValidSdfOpCode(static_cast<uint8_t>(raw)))
            validOpcodes.insert(static_cast<uint8_t>(raw));

    ASSERT_FALSE(validOpcodes.empty()) << "IsValidSdfOpCode accepted nothing 0..255 — broken enum?";

    // Recipe-Diversity-Stress-Scene-Inc6 M1 exemption: DeclarePosition (the spatial-contract
    // meta/resolve prototype's marker opcode) is deliberately NOT in GetAll()'s standard corpus
    // — it requires the emitDeclaredPositionOutParam=true emitter path and a 3rd out-param SSBO
    // readback the shared corpus loop below doesn't thread through (same class of exclusion as
    // the M4d_N1_*_KernelProbe cases above, which also opt out of the shared harness shape).
    // Its own dedicated GPU parity test (DeclaredPositionMatchesAcrossCpuAndGpu, below) exercises
    // it directly instead — this exemption only says "not in the SHARED loop," not "untested."
    validOpcodes.erase(static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::DeclarePosition));

    // Recipe-Nested-Invocation M1 exemption, same shape as DeclarePosition above: InvokeRecipe
    // requires a RecipeRegistry with an already-registered callee threaded through both
    // evalRecipe and EmitProceduralFieldFunctionGlsl — a call shape this shared corpus-loop
    // harness (single flat program, no registry) doesn't support. Its own dedicated correctness
    // gate (test_recipe_nested_invocation.cpp) exercises it directly, including the mandatory
    // GPU-verified CPU/GLSL parity check — this exemption only says "not in the SHARED loop,"
    // not "untested."
    validOpcodes.erase(static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::InvokeRecipe));

    std::vector<int> missingFromCorpus;   // valid but never exercised by the corpus
    for (uint8_t v : validOpcodes)
        if (!corpusOpcodes.count(v))
            missingFromCorpus.push_back(static_cast<int>(v));

    std::vector<int> extraInCorpus;       // corpus uses a byte IsValidSdfOpCode rejects
    for (uint8_t c : corpusOpcodes)
        if (!validOpcodes.count(c)) extraInCorpus.push_back(static_cast<int>(c));

    std::ostringstream diag;
    if (!missingFromCorpus.empty()) {
        diag << "Opcodes accepted by IsValidSdfOpCode but NOT exercised by any corpus program: ";
        for (int v : missingFromCorpus) diag << v << " ";
        diag << "\n";
    }
    if (!extraInCorpus.empty()) {
        diag << "Opcodes used by the corpus but rejected by IsValidSdfOpCode: ";
        for (int v : extraInCorpus) diag << v << " ";
        diag << "\n";
    }

    EXPECT_TRUE(missingFromCorpus.empty() && extraInCorpus.empty()) << diag.str();
}
