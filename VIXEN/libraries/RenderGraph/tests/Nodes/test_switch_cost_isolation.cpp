/**
 * @file test_switch_cost_isolation.cpp
 * @brief Recipe Bucketed-Dispatch Overhead Inc3 M0 live-run gate (Task 1) — GATING SPIKE.
 *
 * Isolates whether the ORIGINAL tier-0 switch's own N=100 knee (Perf-Ledger.md "Switch-scaling
 * measurement," ~8x FPS collapse, 2026-07-10) is caused by switch/branch-dispatch cost, by
 * register-pressure/instruction-cache thrash from code-size-per-case, or by instance-count
 * (Sigma k_i) pressure. This is a STANDALONE synthetic-shader measurement (mirrors M3/M4's own
 * "cold-path stand-in, not the real BodyInstanceRayMarch.comp" precedent — see those files'
 * header comments for why touching the real production shader is out of scope) — it reuses
 * UberShaderSplice.h's REAL switch-generation SHAPE (N sdfRecipe_<id> field functions + an
 * evalRecipeField(uint recipeId, vec3 p) switch dispatching to them) but wraps it in a
 * self-contained sphere-traced main(), not the full production TraceWorld.glsl chain.
 *
 * Sweeps THREE axes at once, randomized:
 *   - N            — distinct recipes (3, 10, 100 — matches the existing switch-scaling table).
 *   - m_i           — each recipe's opcode step count, randomized per-recipe in [3, 50].
 *   - k_i           — instances of recipe i rendered simultaneously, randomized per-recipe.
 * Total scene instance count = Sigma k_i. Each recipe's program is a randomly-generated,
 * ARITY-VALID binary tree of real SdfOpCodes (leaf primitives + binary CSG + optional unary
 * modifiers), validated by RecipeRegistry::Register's own stack-arity check before use — a
 * disqualified (invalid) draw is deterministically resampled, never silently coerced.
 *
 * TWO renders per (N, m_i, k_i) draw:
 *   (a) REAL — each case's field function reflects its actual randomized m_i-step program.
 *   (b) CONTROL — same N, same per-case m_i (so switch/case COUNT and SIZE-per-case are
 *       preserved), but every case's body is forced to an IDENTICAL trivial expression chain
 *       (Sphere then repeated no-op-equivalent Union-with-self, m_i-1 times) — same instruction
 *       COUNT per case, content-invariant. This isolates whether the knee tracks real varying
 *       computation or persists purely from switch/case-count structure at the same size.
 *
 * DEVICE SELECTION: mirrors DeviceNode::SelectPhysicalDevice() / M4's own fix — discrete GPU
 * preferred, device name printed+asserted for every case (Inc3 M0 prompt's explicit requirement,
 * called out because Inc2 M1-M3 silently ran on an integrated GPU for weeks before M4 caught it).
 *
 * RANDOMIZATION: a single fixed seed (see kSeed below) drives a deterministic
 * std::mt19937 — printed at the start of every test case for reproducibility.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeStack.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "ShaderCompiler.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDF_CORE_KERNELS_GLSL_PATH
#error "SDF_CORE_KERNELS_GLSL_PATH must be defined by CMake"
#endif

using Vixen::SVO::Recipe::SdfOpCode;
using Vixen::SVO::Recipe::RecipeStackArity;
using Vixen::SVO::Recipe::EmitProceduralFieldFunctionGlsl;
using Vixen::SVO::RecipeRegistry;
using Vixen::SVO::Recipe::SdfInstruction;

namespace {

// Fixed seed -- every run of this harness is reproducible. Printed at the start of every test.
constexpr uint32_t kSeed = 0x5EC0DE01u;

// --- Randomized recipe generator ---------------------------------------------------------
// Builds a valid binary-tree SDF program: leaf primitives push 1 value (arity {0,1,0,0}),
// binary CSG ops pop 2 push 1 (arity {2,1,0,0}), unary modifiers pop 1 push 1 (arity {1,1,0,0}).
// A tree of L leaves and L-1 binary ops, with unary modifiers optionally inserted after any op,
// is arity-valid BY CONSTRUCTION (stack depth never underflows, always ends at exactly 1) --
// this is checked anyway via RecipeRegistry::Register before use, never assumed.
enum class LeafKind { Sphere, Box, Torus, Capsule, Cylinder, BoxRounded };
enum class BinKind { Union, SmoothUnion, Subtract, Intersect };
enum class UnaryKind { Round, Onion };

SdfInstruction MakeLeaf(LeafKind k, std::mt19937& rng) {
    std::uniform_real_distribution<float> smallExtent(0.3f, 1.5f);
    std::uniform_real_distribution<float> pos(-2.0f, 2.0f);
    SdfInstruction in{};
    in.paramMask = 0;
    switch (k) {
        case LeafKind::Sphere:
            in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
            in.data[0] = pos(rng); in.data[1] = pos(rng); in.data[2] = pos(rng);
            in.data[3] = smallExtent(rng);
            break;
        case LeafKind::Box:
            in.opCode = static_cast<uint8_t>(SdfOpCode::Box);
            in.data[0] = smallExtent(rng); in.data[1] = smallExtent(rng); in.data[2] = smallExtent(rng);
            break;
        case LeafKind::Torus:
            in.opCode = static_cast<uint8_t>(SdfOpCode::Torus);
            in.data[0] = smallExtent(rng) + 0.5f; in.data[1] = smallExtent(rng) * 0.3f;
            break;
        case LeafKind::Capsule:
            in.opCode = static_cast<uint8_t>(SdfOpCode::Capsule);
            in.data[0] = smallExtent(rng); in.data[1] = smallExtent(rng) * 0.3f;
            break;
        case LeafKind::Cylinder:
            in.opCode = static_cast<uint8_t>(SdfOpCode::Cylinder);
            in.data[0] = smallExtent(rng); in.data[1] = smallExtent(rng) * 0.3f;
            break;
        case LeafKind::BoxRounded:
            in.opCode = static_cast<uint8_t>(SdfOpCode::BoxRounded);
            in.data[0] = smallExtent(rng); in.data[1] = smallExtent(rng); in.data[2] = smallExtent(rng);
            in.data[3] = 0.1f;
            break;
    }
    return in;
}

SdfInstruction MakeBin(BinKind k) {
    SdfInstruction in{};
    in.paramMask = 0;
    switch (k) {
        case BinKind::Union:       in.opCode = static_cast<uint8_t>(SdfOpCode::Union); break;
        case BinKind::SmoothUnion: in.opCode = static_cast<uint8_t>(SdfOpCode::SmoothUnion); in.data[2] = 0.3f; break;
        case BinKind::Subtract:    in.opCode = static_cast<uint8_t>(SdfOpCode::Subtract); break;
        case BinKind::Intersect:   in.opCode = static_cast<uint8_t>(SdfOpCode::Intersect); break;
    }
    return in;
}

SdfInstruction MakeUnary(UnaryKind k) {
    SdfInstruction in{};
    in.paramMask = 0;
    switch (k) {
        case UnaryKind::Round: in.opCode = static_cast<uint8_t>(SdfOpCode::Round); in.data[0] = 0.05f; break;
        case UnaryKind::Onion: in.opCode = static_cast<uint8_t>(SdfOpCode::Onion); in.data[0] = 0.05f; break;
    }
    return in;
}

// Generates a real, arity-valid, randomized-content program with exactly `steps` instructions
// (m_i). Structure: ceil(steps/2)+1-ish leaves joined by binary ops, with unary modifiers
// filling any remaining step budget. Deterministic given rng state.
std::vector<SdfInstruction> GenerateRandomProgram(uint32_t steps, std::mt19937& rng) {
    steps = std::max<uint32_t>(steps, 1u);
    std::uniform_int_distribution<int> leafPick(0, 5);
    std::uniform_int_distribution<int> binPick(0, 3);
    std::uniform_int_distribution<int> unaryPick(0, 1);

    std::vector<SdfInstruction> prog;
    prog.reserve(steps);

    // First leaf is mandatory (a program must start with something on the stack).
    prog.push_back(MakeLeaf(static_cast<LeafKind>(leafPick(rng)), rng));
    uint32_t remaining = steps - 1;

    // Interleave: for each remaining "slot," 50/50 either (leaf + binary-op) as a pair when
    // budget allows, or a single unary modifier when only 1 slot is left. This guarantees the
    // stack always has >=1 value and ends at exactly 1 (binary trees preserve that invariant).
    while (remaining > 0) {
        if (remaining >= 2) {
            prog.push_back(MakeLeaf(static_cast<LeafKind>(leafPick(rng)), rng));
            prog.push_back(MakeBin(static_cast<BinKind>(binPick(rng))));
            remaining -= 2;
        } else {
            prog.push_back(MakeUnary(static_cast<UnaryKind>(unaryPick(rng))));
            remaining -= 1;
        }
    }
    return prog;
}

// Control variant: SAME step count (m_i preserved) but content forced trivial/identical —
// Sphere(fixed unit sphere at origin) followed by (steps-1) Union-with-a-fresh-identical-sphere
// pairs collapsed into unary-shaped no-ops is not quite arity-safe as a 1:1 op swap, so instead:
// build the SAME binary-tree SHAPE as GenerateRandomProgram would for `steps`, but every leaf is
// the IDENTICAL unit sphere at the origin, every binary op is Union, every unary op is Round with
// a fixed negligible radius. Same instruction COUNT and switch-case SIZE, zero varying content.
std::vector<SdfInstruction> GenerateControlProgram(uint32_t steps) {
    steps = std::max<uint32_t>(steps, 1u);
    SdfInstruction unitSphere{};
    unitSphere.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    unitSphere.data[0] = 0.0f; unitSphere.data[1] = 0.0f; unitSphere.data[2] = 0.0f; unitSphere.data[3] = 1.0f;

    std::vector<SdfInstruction> prog;
    prog.reserve(steps);
    prog.push_back(unitSphere);
    uint32_t remaining = steps - 1;
    while (remaining > 0) {
        if (remaining >= 2) {
            prog.push_back(unitSphere);
            prog.push_back(MakeBin(BinKind::Union));
            remaining -= 2;
        } else {
            prog.push_back(MakeUnary(UnaryKind::Round));
            remaining -= 1;
        }
    }
    return prog;
}

// Validates via the SAME arity/registry check the real production path uses -- never assumed.
bool IsProgramValid(const std::vector<SdfInstruction>& prog) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry entry;
    entry.bytecode = prog;
    entry.boundRadius = 2.0f;
    entry.stepRelaxation = 1.0f;
    return reg.Register(1000u, entry) == RecipeRegistry::RegisterResult::Ok;
}

// Byte-identical to HitRecord.glsl's std430 layout (64 B/element) -- same mirror M3/M4 use.
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

// Per-instance record: worldPos/color/recipeId + fixed recipeParams (unused by generated
// programs here -- every recipe bakes its own literals, mirrors M3/M4's own convention).
struct InstanceCpu {
    float worldPos[3];
    float _pad0;
    float color[3];
    uint32_t recipeId;
    float recipeParams[6];
    float _pad1[2];
};
static_assert(sizeof(InstanceCpu) == 64, "InstanceCpu std430 mirror size");

struct SwitchPush {
    glm::vec3 cameraPos; float _p0;
    glm::vec3 cameraDir; float fov;
    glm::vec3 cameraUp;  float aspect;
    glm::vec3 cameraRight; float _p1;
    uint32_t  instanceCount;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  _p2;
};

std::string ReadTextFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Emits ONE self-contained switch-shader: N sdfRecipe_<id> field functions (mirrors
// UberShaderSplice.h's own per-recipe emission) + an evalRecipeField(uint recipeId, vec3 p)
// switch, wrapped in a fixed instance-loop main() that sphere-traces via that switch. This is
// the REAL switch-generation shape the production tier-0 shader uses (same emitter,
// EmitProceduralFieldFunctionGlsl, same switch structure UberShaderSplice.h builds) -- what
// differs from the live app is only the surrounding main()/binding scaffolding, exactly the
// same "standalone synthetic harness, not the live render graph" scoping M3/M4 already
// established (see this file's header comment).
std::string EmitSwitchShader(
    const std::vector<std::pair<uint32_t, std::vector<SdfInstruction>>>& recipes,
    const std::string& sdfCoreKernelsGlsl)
{
    std::ostringstream out;
    out << "#version 460\n\n";
    out << "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n\n";
    out << sdfCoreKernelsGlsl << "\n\n";

    for (const auto& [id, prog] : recipes) {
        out << EmitProceduralFieldFunctionGlsl(prog.data(), static_cast<uint32_t>(prog.size()), id) << "\n";
    }

    out << "float evalRecipeField(uint recipeId, vec3 p) {\n";
    out << "  switch (recipeId) {\n";
    for (const auto& [id, prog] : recipes) {
        out << "    case " << id << "u: return sdfRecipe_" << id << "(p, float[6](0.0,0.0,0.0,0.0,0.0,0.0));\n";
    }
    out << "    default: return 1e30;\n";
    out << "  }\n";
    out << "}\n\n";

    out << R"(
struct Instance {
    vec3  worldPos; float _pad0;
    vec3  color; uint recipeId;
    float recipeParams[6]; float _pad1[2];
};
layout(std430, binding = 0) readonly buffer InstanceBuffer { Instance instances[]; };

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL
#define HITRECORD_FLAG_HIT 0x1u
struct HitRecord {
    vec3 albedo; float roughness;
    vec3 worldNormal; float hitT;
    vec3 worldPos; uint flags;
    uint _pad0[3];
};
#endif
layout(std430, binding = 1) buffer HitRecordBuffer { HitRecord hitRecords[]; };

layout(push_constant) uniform Push {
    vec3 cameraPos; float _p0;
    vec3 cameraDir; float fov;
    vec3 cameraUp;  float aspect;
    vec3 cameraRight; float _p1;
    uint instanceCount;
    uint screenWidth;
    uint screenHeight;
    uint _p2;
} pc;

vec3 getRayDir(vec2 uv) {
    float tanHalfFov = tan(radians(pc.fov * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return normalize(pc.cameraDir + pc.cameraRight * ndc.x * tanHalfFov * pc.aspect
                                   + pc.cameraUp    * ndc.y * tanHalfFov);
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= int(pc.screenWidth) || pixelCoords.y >= int(pc.screenHeight)) return;

    vec2 uv = (vec2(pixelCoords) + 0.5) / vec2(float(pc.screenWidth), float(pc.screenHeight));
    vec3 rayOrigin = pc.cameraPos;
    vec3 rayDir    = getRayDir(uv);

    bool  anyHit = false;
    float bestT  = 1e30;
    vec3  bestNormal = vec3(0.0, 1.0, 0.0);
    vec3  bestColor  = vec3(1.0);

    const float boundRadius = 3.0;
    for (uint m = 0u; m < pc.instanceCount; ++m) {
        Instance inst = instances[m];
        vec3  oc = rayOrigin - inst.worldPos;
        float b  = dot(oc, rayDir);
        float c  = dot(oc, oc) - boundRadius * boundRadius;
        float disc = b * b - c;
        if (disc < 0.0) continue;
        float sq = sqrt(disc);
        float tNear = max(-b - sq, 0.0);
        float tFar  = -b + sq;
        if (tFar < 0.0 || tNear >= bestT) continue;

        float t = tNear;
        const int   MAX_STEPS = 96;
        const float EPS = 1e-3;
        for (int i = 0; i < MAX_STEPS; ++i) {
            vec3  p = rayOrigin + rayDir * t - inst.worldPos;
            float d = evalRecipeField(inst.recipeId, p);
            if (d < EPS) {
                if (t < bestT) {
                    bestT = t;
                    bestNormal = normalize(p);
                    bestColor  = inst.color;
                    anyHit = true;
                }
                break;
            }
            t += d;
            if (t > tFar) break;
        }
    }

    if (!anyHit) return;

    uint hitIdx = uint(pixelCoords.y) * pc.screenWidth + uint(pixelCoords.x);
    if (bestT < hitRecords[hitIdx].hitT || hitRecords[hitIdx].flags == 0u) {
        HitRecord rec;
        rec.albedo = bestColor;
        rec.roughness = 1.0;
        rec.worldNormal = bestNormal;
        rec.hitT = bestT;
        rec.worldPos = rayOrigin + rayDir * bestT;
        rec.flags = HITRECORD_FLAG_HIT;
        rec._pad0 = uint[3](0u, 0u, 0u);
        hitRecords[hitIdx] = rec;
    }
}
)";
    return out.str();
}

}  // namespace

class SwitchCostIsolationTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;
    bool             discreteGpuSelected_ = false;
    std::string      sdfCoreKernelsGlsl_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    static bool IsDiscreteGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    }
    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& props) {
        std::string name(props.deviceName);
        for (char& c : name) c = static_cast<char>(::tolower(c));
        const bool isSoftware =
            (name.find("llvmpipe") != std::string::npos ||
             name.find("lavapipe") != std::string::npos) &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = name.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_switch_cost_isolation";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        const auto  enabledLayers = EnabledValidationLayers();
        const char* extensions[]  = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        VkInstanceCreateInfo instInfo{};
        instInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo        = &appInfo;
        instInfo.enabledLayerCount       = static_cast<uint32_t>(enabledLayers.size());
        instInfo.ppEnabledLayerNames     = enabledLayers.empty() ? nullptr : enabledLayers.data();
        instInfo.enabledExtensionCount   = 1;
        instInfo.ppEnabledExtensionNames = extensions;

        ASSERT_EQ(vkCreateInstance(&instInfo, nullptr, &instance_), VK_SUCCESS)
            << "vkCreateInstance failed — is a Vulkan device available?";

        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found; nearest was '"
            << selectedDeviceName_ << "'.";
        // Hard requirement (M0 prompt): confirm discrete GPU for EVERY number reported.
        ASSERT_TRUE(discreteGpuSelected_)
            << "Refusing to capture perf numbers on a non-discrete device ('"
            << selectedDeviceName_ << "') — Inc2's M1-M3 silently ran on integrated for weeks; "
            << "this gate requires a confirmed discrete GPU.";
        std::printf("[switch-cost-isolation] selected physical device: '%s' (discrete=%d)\n",
                    selectedDeviceName_.c_str(), discreteGpuSelected_ ? 1 : 0);

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        sdfCoreKernelsGlsl_ = ReadTextFile(SDF_CORE_KERNELS_GLSL_PATH);
        ASSERT_FALSE(sdfCoreKernelsGlsl_.empty()) << "Failed to read " << SDF_CORE_KERNELS_GLSL_PATH;
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    }

    // Mirrors DeviceNode::SelectPhysicalDevice()'s exact logic — see test_recipe_bucketing_perf.cpp.
    void PickPhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);

        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsDiscreteGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = true; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = false; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; discreteGpuSelected_ = false; return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
        deviceConfirmed_    = false;
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
        ASSERT_TRUE(found) << "No compute queue family on the selected device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_; qInfo.queueCount = 1; qInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VkDeviceMemory& outMem, bool zero) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &outBuf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, outBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, outBuf, outMem, 0), VK_SUCCESS);
        if (zero) {
            void* m = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, outMem, 0, size, 0, &m), VK_SUCCESS);
            std::memset(m, 0, static_cast<size_t>(size));
            vkUnmapMemory(logicalDevice_, outMem);
        }
    }

    void UploadBuffer(VkDeviceMemory mem, const void* data, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    void ZeroBuffer(VkDeviceMemory mem, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memset(m, 0, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    template <typename T>
    void ReadbackBuffer(VkDeviceMemory mem, VkDeviceSize size, std::vector<T>& out) {
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &mapped), VK_SUCCESS);
        out.resize(static_cast<size_t>(size) / sizeof(T));
        std::memcpy(out.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }

    struct SweepResult {
        double avgMsPerIter = 0.0;
        double avgFps = 0.0;
        uint32_t hitCount = 0;
        uint32_t totalInstances = 0;
        std::vector<uint32_t> mPerRecipe;
        std::vector<uint32_t> kPerRecipe;
    };

    // Compiles+dispatches ONE switch-shader (real or control) built from `recipes`
    // (recipeId -> program) against `instances`, over kSteadyIters, returns steady FPS/ms.
    void RunOneShader(const std::vector<std::pair<uint32_t, std::vector<SdfInstruction>>>& recipes,
                       const std::vector<InstanceCpu>& instances,
                       const char* label,
                       SweepResult& out) {
        constexpr uint32_t kScreenWidth = 256, kScreenHeight = 256;
        constexpr int kSteadyIters = 30;

        const std::string src = EmitSwitchShader(recipes, sdfCoreKernelsGlsl_);

        ShaderManagement::ShaderCompiler compiler;
        ShaderManagement::CompilationOptions opts;
        opts.validateSpirv = false;
        auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, src, "main", opts);
        ASSERT_TRUE(compOut.success) << "[" << label << "] shader compile failed:\n" << compOut.GetFullLog();

        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = compOut.spirv.size() * sizeof(uint32_t); smci.pCode = compOut.spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &module), VK_SUCCESS);

        auto bind = [](uint32_t b) {
            VkDescriptorSetLayoutBinding db{};
            db.binding = b; db.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            db.descriptorCount = 1; db.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return db;
        };
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {bind(0), bind(1)};
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(SwitchPush);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &layout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = module; cpci.stage.pName = "main";
        cpci.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto compileStart = std::chrono::steady_clock::now();
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);
        const auto compileEnd = std::chrono::steady_clock::now();
        std::printf("[switch-cost-isolation][%s] pipeline compile: %.2f ms (excluded from steady-state)\n",
                    label, std::chrono::duration<double, std::milli>(compileEnd - compileStart).count());

        VkBuffer instBuf, hitBuf;
        VkDeviceMemory instMem, hitMem;
        const VkDeviceSize instSize = instances.size() * sizeof(InstanceCpu);
        const VkDeviceSize hitSize = static_cast<VkDeviceSize>(kScreenWidth) * kScreenHeight * sizeof(HitRecordCpu);
        CreateHostBuffer(instSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf, instMem, false);
        CreateHostBuffer(hitSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitBuf, hitMem, true);
        UploadBuffer(instMem, instances.data(), instSize);

        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &pool), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet set = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &set), VK_SUCCESS);

        VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo hitInfo{hitBuf, 0, VK_WHOLE_SIZE};
        auto w = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet wr{};
            wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet = set; wr.dstBinding = b; wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wr.pBufferInfo = info;
            return wr;
        };
        const std::array<VkWriteDescriptorSet, 2> writes = {w(0, &instInfo), w(1, &hitInfo)};
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        const glm::vec3 eye(0.0f, 0.0f, 18.0f);
        const glm::vec3 target(0.0f, 0.0f, 0.0f);
        const float fovDeg = 60.0f;
        const float aspect = float(kScreenWidth) / float(kScreenHeight);
        const glm::vec3 camDir = glm::normalize(target - eye);
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 camRight = glm::normalize(glm::cross(camDir, worldUp));
        const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camDir));

        SwitchPush pc{};
        pc.cameraPos = eye; pc.cameraDir = camDir; pc.fov = fovDeg; pc.cameraUp = camUp;
        pc.aspect = aspect; pc.cameraRight = camRight;
        pc.instanceCount = static_cast<uint32_t>(instances.size());
        pc.screenWidth = kScreenWidth; pc.screenHeight = kScreenHeight;

        auto runIter = [&]() {
            VkCommandBufferAllocateInfo cbai{};
            cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

            VkCommandBufferBeginInfo cbbi{};
            cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ASSERT_EQ(vkBeginCommandBuffer(cmd, &cbbi), VK_SUCCESS);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (kScreenWidth + 7) / 8, (kScreenHeight + 7) / 8, 1);

            ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
            VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
            ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
            ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
            vkFreeCommandBuffers(logicalDevice_, commandPool_, 1, &cmd);
        };

        ZeroBuffer(hitMem, hitSize);
        runIter();  // warm-up

        ZeroBuffer(hitMem, hitSize);
        const auto start = std::chrono::steady_clock::now();
        for (int iter = 0; iter < kSteadyIters; ++iter) runIter();
        const auto end = std::chrono::steady_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        out.avgMsPerIter = totalMs / double(kSteadyIters);
        out.avgFps = out.avgMsPerIter > 0.0 ? 1000.0 / out.avgMsPerIter : 0.0;
        {
            std::vector<HitRecordCpu> hr;
            ReadbackBuffer(hitMem, hitSize, hr);
            for (const auto& r : hr) if (r.flags & 0x1u) ++out.hitCount;
        }
        out.totalInstances = static_cast<uint32_t>(instances.size());

        std::printf("[switch-cost-isolation][%s] N=%zu instances=%u: %.4f ms/iter, %.1f fps (%d steady iters), hits=%u\n",
                    label, recipes.size(), out.totalInstances, out.avgMsPerIter, out.avgFps, kSteadyIters, out.hitCount);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyDescriptorPool(logicalDevice_, pool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, layout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, module, nullptr);
        vkDestroyBuffer(logicalDevice_, instBuf, nullptr); vkFreeMemory(logicalDevice_, instMem, nullptr);
        vkDestroyBuffer(logicalDevice_, hitBuf, nullptr); vkFreeMemory(logicalDevice_, hitMem, nullptr);
    }

    // RunSweepCase(N, seedOffset, mRange, kRange) — generates N randomized recipes (m_i drawn
    // from mRange steps, k_i drawn from kRange instances each), builds both REAL and CONTROL
    // switch shaders sharing the SAME per-recipe m_i/k_i draw, and runs both. seedOffset lets
    // each TEST_F draw an independent-but-reproducible stream from the shared kSeed. Default
    // mRange=[3,50]/kRange=[1,20] matches the main N=3/10/100 sweep (justified in the Perf-Ledger
    // entry); axis-decoupling cases (below) pass narrow/fixed ranges to isolate N from m_i/k_i.
    void RunSweepCase(uint32_t N, uint32_t seedOffset, SweepResult& outReal, SweepResult& outControl,
                       std::pair<uint32_t, uint32_t> mRange = {3, 50},
                       std::pair<uint32_t, uint32_t> kRange = {1, 20}) {
        ASSERT_TRUE(deviceConfirmed_);
        std::mt19937 rng(kSeed + seedOffset);
        std::uniform_int_distribution<uint32_t> mDist(mRange.first, mRange.second);
        std::uniform_int_distribution<uint32_t> kDist(kRange.first, kRange.second);

        std::printf("[switch-cost-isolation] N=%u seed=0x%08X (kSeed=0x%08X + offset=%u)\n",
                    N, kSeed + seedOffset, kSeed, seedOffset);

        std::vector<std::pair<uint32_t, std::vector<SdfInstruction>>> realRecipes, controlRecipes;
        std::vector<uint32_t> mPerRecipe, kPerRecipe;
        realRecipes.reserve(N); controlRecipes.reserve(N);

        const uint32_t gridCols = static_cast<uint32_t>(std::ceil(std::sqrt(double(N))));
        constexpr float kSpacing = 5.0f;
        std::vector<InstanceCpu> realInstances, controlInstances;

        for (uint32_t i = 0; i < N; ++i) {
            const uint32_t recipeId = 1000u + i;  // avoid colliding with any reserved low id
            const uint32_t m_i = mDist(rng);
            const uint32_t k_i = kDist(rng);
            mPerRecipe.push_back(m_i);
            kPerRecipe.push_back(k_i);

            // Resample until arity-valid (constructive generator should always pass first try,
            // but validated explicitly per the "confirm, don't assume" discipline).
            std::vector<SdfInstruction> realProg;
            for (int attempt = 0; attempt < 8; ++attempt) {
                realProg = GenerateRandomProgram(m_i, rng);
                if (IsProgramValid(realProg)) break;
            }
            ASSERT_TRUE(IsProgramValid(realProg)) << "recipe " << i << " (m_i=" << m_i << ") failed to generate a valid program after 8 attempts";
            std::vector<SdfInstruction> controlProg = GenerateControlProgram(m_i);
            ASSERT_TRUE(IsProgramValid(controlProg)) << "control recipe " << i << " (m_i=" << m_i << ") invalid";

            realRecipes.emplace_back(recipeId, realProg);
            controlRecipes.emplace_back(recipeId, controlProg);

            const uint32_t gx = i % gridCols, gy = i / gridCols;
            const glm::vec3 center(
                (float(gx) - float(gridCols) * 0.5f) * kSpacing,
                (float(gy) - float((N + gridCols - 1) / gridCols) * 0.5f) * kSpacing,
                0.0f);

            for (uint32_t k = 0; k < k_i; ++k) {
                InstanceCpu inst{};
                inst.worldPos[0] = center.x; inst.worldPos[1] = center.y; inst.worldPos[2] = center.z;
                inst.color[0] = 0.8f; inst.color[1] = 0.3f; inst.color[2] = 0.3f;
                inst.recipeId = recipeId;
                realInstances.push_back(inst);
                controlInstances.push_back(inst);
            }
        }

        const uint32_t gridExtent = static_cast<uint32_t>(float(gridCols) * kSpacing);
        std::printf("[switch-cost-isolation] N=%u m_i=[", N);
        for (size_t i = 0; i < mPerRecipe.size(); ++i) std::printf("%s%u", i ? "," : "", mPerRecipe[i]);
        std::printf("] k_i=[");
        for (size_t i = 0; i < kPerRecipe.size(); ++i) std::printf("%s%u", i ? "," : "", kPerRecipe[i]);
        std::printf("] Sigma(k_i)=%zu gridExtent=%u\n", realInstances.size(), gridExtent);

        ASSERT_NO_FATAL_FAILURE(RunOneShader(realRecipes, realInstances, "REAL", outReal));
        ASSERT_NO_FATAL_FAILURE(RunOneShader(controlRecipes, controlInstances, "CONTROL", outControl));
        outReal.mPerRecipe = mPerRecipe; outReal.kPerRecipe = kPerRecipe;
        outControl.mPerRecipe = mPerRecipe; outControl.kPerRecipe = kPerRecipe;

        ASSERT_GT(outReal.hitCount, 0u) << "REAL path produced zero hits — scene/camera framing broken";
        ASSERT_GT(outControl.hitCount, 0u) << "CONTROL path produced zero hits — scene/camera framing broken";
    }
};

TEST_F(SwitchCostIsolationTest, N3_RandomizedStress) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(3, 1u, real, control));
    std::printf("[switch-cost-isolation][SUMMARY][N=3] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}

TEST_F(SwitchCostIsolationTest, N10_RandomizedStress) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(10, 2u, real, control));
    std::printf("[switch-cost-isolation][SUMMARY][N=10] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}

TEST_F(SwitchCostIsolationTest, N100_RandomizedStress) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(100, 3u, real, control));
    std::printf("[switch-cost-isolation][SUMMARY][N=100] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}

// ============================================================================
// Axis-decoupling cases (added after the main N=3/10/100 sweep showed a monotonic real/control
// ratio growth with N: 0.9-1.1x at N=3, 1.8-1.9x at N=10, 2.1-4.3x at N=100). The main sweep
// draws m_i/k_i randomly WITH N, so N, total switch-case code size, and Sigma(k_i) all grow
// together -- these cases hold two axes near-fixed while N varies, to see which axis the ratio
// actually tracks.
// ============================================================================

// N=100 but m_i pinned to a TINY fixed range [3,5] (minimal per-case code) and k_i pinned low
// [1,3] (Sigma(k_i) ~150-300, an order of magnitude below the main N=100 case's ~1000). If the
// ratio collapses back toward ~1x here, N=100's case-COUNT alone (switch breadth) isn't
// sufficient to explain the knee -- code size and/or instance count are doing the work.
TEST_F(SwitchCostIsolationTest, N100_TinyMi_LowKi_Decoupling) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(100, 10u, real, control, {3, 5}, {1, 3}));
    std::printf("[switch-cost-isolation][SUMMARY][N=100,tiny-mi,low-ki] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}

// N=10 (small switch breadth) but m_i pinned HIGH [45,50] (near-maximal per-case code, same
// ballpark as the largest cases the main N=100 draw saw) and k_i pinned HIGH [15,20] (Sigma(k_i)
// ~150-200, comparable to instance counts the main N=10/N=100 draws saw). If the ratio here
// tracks CLOSE TO N=100's (not N=10's ~1.8x), that argues for m_i/k_i-driven, not N-case-count-
// driven. If it stays near N=10's normal ratio despite large m_i/k_i, that argues N (case count)
// itself, not per-case size or instance count, is what's driving the knee.
TEST_F(SwitchCostIsolationTest, N10_LargeMi_HighKi_Decoupling) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(10, 20u, real, control, {45, 50}, {15, 20}));
    std::printf("[switch-cost-isolation][SUMMARY][N=10,large-mi,high-ki] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}

// N=100 with m_i pinned HIGH [45,50] but k_i pinned LOW [1,2] (Sigma(k_i) ~100-200, similar
// order to N=10's normal draw) -- large switch breadth AND large per-case code, but LOW
// instance count. If the ratio is high here (tracking N=100's normal case), that argues N
// and/or m_i (not k_i/instance-count) drive the knee. If it drops toward N=10-like levels
// despite N=100's case count, that argues k_i/instance-count is actually doing the work (each
// instance re-enters the switch on every march step, so more instances means more switch
// evaluations even at fixed N).
TEST_F(SwitchCostIsolationTest, N100_LargeMi_LowKi_Decoupling) {
    SweepResult real, control;
    ASSERT_NO_FATAL_FAILURE(RunSweepCase(100, 30u, real, control, {45, 50}, {1, 2}));
    std::printf("[switch-cost-isolation][SUMMARY][N=100,large-mi,low-ki] real=%.4fms(%.1ffps) control=%.4fms(%.1ffps) ratio=%.3f\n",
                real.avgMsPerIter, real.avgFps, control.avgMsPerIter, control.avgFps,
                real.avgMsPerIter / control.avgMsPerIter);
}
