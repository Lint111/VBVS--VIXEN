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
 *      above still run and still assert, since they aren't part of this fixture.
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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
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
//   #version 450 + SdfCoreKernels.glsl + emitted sdfRecipe_0(vec3) + wrapper main().
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

void main() {
    if (gl_GlobalInvocationID.x >= points.length()) return;
    values[gl_GlobalInvocationID.x] = sdfRecipe_0(points[gl_GlobalInvocationID.x].xyz);
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
    // values in outValues (one per input point).
    void DispatchAndReadback(const std::vector<uint32_t>& spirv,
                              const std::vector<glm::vec3>& points,
                              std::vector<float>& outValues) {
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

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t);
        smci.pCode    = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

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
        cpci.sType       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName  = "main";
        cpci.layout       = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
                  VK_SUCCESS);

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

    std::vector<int> missingFromCorpus;   // valid but never exercised by the corpus
    for (uint8_t v : validOpcodes)
        if (!corpusOpcodes.count(v)) missingFromCorpus.push_back(static_cast<int>(v));

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
