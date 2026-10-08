/**
 * @file test_recipe_declared_position_render.cpp
 * @brief Recipe-Diversity-Stress-Scene-Inc6 M1 — spatial-contract meta/resolve prototype's
 *        live-render/visual gate. Renders the hand-authored prototype recipe
 *        [ReadParamFloat3(idx=0), DeclarePosition, Sphere(center=0,r=0.5)] as a flat 2D
 *        distance-field slice (Z=0 plane, no ray marching needed since the emitted GLSL
 *        function IS the distance field) at 3 different ReadParam-supplied declared
 *        positions, writing one PNG per position for visual confirmation that changing the
 *        declared position actually moves where the shape renders — not a full 3D ray-traced
 *        render (that infrastructure doesn't exist for the field-function-only GLSL emitter;
 *        EmitProceduralComputeShader's HLSL trace-main is a different, unrelated emitter path
 *        per SdfRecipeCodegen.h), but a direct, cheap, real-GPU visual proof of the same claim
 *        test_recipe_glsl_numerical_parity.cpp's DeclaredPositionMatchesAcrossCpuAndGpu already
 *        proves numerically.
 *
 * Device selection/bring-up mirrors test_recipe_glsl_numerical_parity.cpp's
 * RecipeGlslNumericalParityTest fixture (real discrete/integrated GPU required; SKIPs
 * otherwise — no lavapipe/Dozen for this task).
 *
 * Run directly (per KI-014, not via ctest):
 *   build\ninja\libraries\SVO\tests\Debug\test_recipe_declared_position_render.exe
 *
 * Output: /tmp/declared_pos_{0,1,2}.png (256x256, white=inside sphere / black=outside,
 *   sampling the XY plane at Z=0, world extent [-4,4]^2).
 */

#include <gtest/gtest.h>

#include "Recipe/RecipeParityCorpus.h"
#include "Recipe/SdfInstruction.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeTileSpecialization.h"
#include "Recipe/RecipeWholeDomainCompaction.h"
#include "ShaderCompiler.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDF_CORE_KERNELS_GLSL_PATH
#  error "SDF_CORE_KERNELS_GLSL_PATH must be defined via CMake compile_definitions"
#endif
#ifndef SDF_RECIPE_TAPE_GLSL_PATH
#  error "SDF_RECIPE_TAPE_GLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

using namespace Vixen::SVO::Recipe;

// Compose a compute shader that samples the emitted field function across a WxH grid over
// the world-space XY plane at Z=0, writing sign(field) (1.0 = inside/on-surface, 0.0 =
// outside) into an RGBA32F storage image — a flat "which pixels are inside the shape" slice,
// not a ray-traced render. worldHalfExtent controls the sampled world-space square
// [-worldHalfExtent, +worldHalfExtent]^2.
std::string ComposeSliceShader(const std::string& sdfCoreGlsl, const std::string& emittedFieldFn,
                                float worldHalfExtent) {
    std::ostringstream ss;
    ss << "#version 450\n";
    ss << sdfCoreGlsl << "\n";
    ss << emittedFieldFn << "\n";
    ss << "layout(local_size_x = 8, local_size_y = 8) in;\n";
    ss << "layout(set = 0, binding = 0, rgba32f) uniform image2D outImage;\n";
    ss << "layout(set = 0, binding = 1, std430) readonly buffer InParams { float params[6]; };\n";
    ss << "void main() {\n";
    ss << "  ivec2 sz = imageSize(outImage);\n";
    ss << "  if (gl_GlobalInvocationID.x >= uint(sz.x) || gl_GlobalInvocationID.y >= uint(sz.y)) return;\n";
    ss << "  float u = (float(gl_GlobalInvocationID.x) + 0.5) / float(sz.x);\n";
    ss << "  float v = (float(gl_GlobalInvocationID.y) + 0.5) / float(sz.y);\n";
    ss << "  float he = " << worldHalfExtent << ";\n";
    ss << "  vec3 p = vec3((u * 2.0 - 1.0) * he, (v * 2.0 - 1.0) * he, 0.0);\n";
    ss << "  float pr[6] = float[6](params[0], params[1], params[2], params[3], params[4], params[5]);\n";
    ss << "  vec3 declaredPos;\n";
    ss << "  float d = sdfRecipe_0(p, pr, declaredPos);\n";
    ss << "  float inside = d <= 0.0 ? 1.0 : 0.0;\n";
    ss << "  imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), vec4(inside, inside, inside, 1.0));\n";
    ss << "}\n";
    return ss.str();
}

std::string ComposeTileTapeRaymarchShader(const std::string& sdfCoreGlsl,
    const std::string& evaluatorSource, const std::string& evaluatorAdapter) {
    std::ostringstream ss;
    ss << "#version 450\n" << sdfCoreGlsl << "\n";
    ss << "struct RecipeTapeInstruction { uint opCode; float data[32]; };\n";
    ss << "layout(set=0,binding=0,rgba32f) uniform image2D outImage;\n";
    ss << "layout(set=0,binding=1,std430) readonly buffer InRecipeTape { RecipeTapeInstruction recipeTape[]; };\n";
    ss << "layout(set=0,binding=2,std430) readonly buffer InTileRanges { uvec2 tileRanges[]; };\n";
    ss << "layout(set=0,binding=3,std430) writeonly buffer OutClauseCounts { uint clauseCounts[]; };\n";
    ss << evaluatorSource << "\n" << evaluatorAdapter << "\n";
    ss << R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    uint xTile = min(uint(pixel.x / 16), 3u);
    uint yTile = min(uint(pixel.y / 16), 3u);
    float u = (float(pixel.x) + 0.5) / float(size.x);
    float v = (float(pixel.y) + 0.5) / float(size.y);
    float worldX = (u * 2.0 - 1.0) * 6.0;
    float worldY = (v * 2.0 - 1.0) * 6.0;
    float hitDepth = -1000.0;
    uint totalClauses = 0u;
    for (uint stepIndex = 0u; stepIndex < 121u; ++stepIndex) {
        float z = -6.0 + float(stepIndex) * (12.0 / 120.0);
        uint zTile = min((stepIndex * 4u) / 121u, 3u);
        uint tileIndex = (zTile * 4u + yTile) * 4u + xTile;
        uvec2 tapeRange = tileRanges[tileIndex];
        float distanceValue = 0.0;
        uint executedClauses = 0u;
        if (!EvaluateAtTile(tapeRange.x, tapeRange.y, vec3(worldX, worldY, z),
                distanceValue, executedClauses)) {
            imageStore(outImage, pixel, vec4(-2.0, -1.0, -1.0, -1.0));
            clauseCounts[uint(pixel.y * size.x + pixel.x)] = 0u;
            return;
        }
        totalClauses += executedClauses;
        if (distanceValue <= 0.0) {
            hitDepth = z;
            break;
        }
    }
    // Material, selected channel and emission are fixture constants independent of the
    // geometry tape, matching the rv-a1 requirement for observable-output parity.
    imageStore(outImage, pixel, vec4(hitDepth, 0.25, 0.5, 0.75));
    clauseCounts[uint(pixel.y * size.x + pixel.x)] = totalClauses;
}
)GLSL";
    return ss.str();
}

std::string ComposeCompiledRecipeRaymarchShader(const std::string& sdfCoreGlsl,
    const std::string& recipeFieldGlsl) {
    std::ostringstream ss;
    ss << "#version 450\n" << sdfCoreGlsl << "\n" << recipeFieldGlsl << "\n";
    ss << R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0,rgba32f) uniform image2D outImage;
layout(set=0,binding=1,std430) readonly buffer InParams { float params[6]; };
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    float u = (float(pixel.x) + 0.5) / float(size.x);
    float v = (float(pixel.y) + 0.5) / float(size.y);
    float worldX = (u * 2.0 - 1.0) * 6.0;
    float worldY = (v * 2.0 - 1.0) * 6.0;
    float recipeParams[6] = float[6](params[0], params[1], params[2],
        params[3], params[4], params[5]);
    float hitDepth = -1000.0;
    for (uint stepIndex = 0u; stepIndex < 121u; ++stepIndex) {
        float z = -6.0 + float(stepIndex) * (12.0 / 120.0);
        float distanceValue = sdfRecipe_0(vec3(worldX, worldY, z), recipeParams);
        if (distanceValue <= 0.0) {
            hitDepth = z;
            break;
        }
    }
    // Non-geometry fixture outputs are constants, independent of the selected geometry term.
    imageStore(outImage, pixel, vec4(hitDepth, 0.25, 0.5, 0.75));
}
)GLSL";
    return ss.str();
}

std::string ReadWholeFile(const char* path) {
    std::ifstream file(path);
    if (!file.good()) return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

struct GpuRecipeTapeInstruction {
    std::uint32_t opCode;
    float data[32];
};
static_assert(sizeof(GpuRecipeTapeInstruction) == sizeof(SdfInstruction));

GpuRecipeTapeInstruction PackGpuInstruction(const SdfInstruction& instruction) {
    GpuRecipeTapeInstruction packed{};
    packed.opCode = instruction.opCode;
    std::memcpy(packed.data, instruction.data, sizeof(packed.data));
    return packed;
}

using TileTapeRange = std::array<std::uint32_t, 2>;

std::vector<GpuRecipeTapeInstruction> PackGpuTape(std::span<const SdfInstruction> instructions) {
    std::vector<GpuRecipeTapeInstruction> packed;
    packed.reserve(instructions.size());
    for (const SdfInstruction& instruction : instructions)
        packed.push_back(PackGpuInstruction(instruction));
    return packed;
}

SdfInstruction MakeTileSphere(float x, float y, float z, float radius) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Sphere);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    instruction.data[3] = radius;
    return instruction;
}

SdfInstruction MakeTileBox(float halfX, float halfY, float halfZ) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Box);
    instruction.data[0] = halfX;
    instruction.data[1] = halfY;
    instruction.data[2] = halfZ;
    return instruction;
}

SdfInstruction MakeTileCombine(SdfOpCode opcode) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(opcode);
    return instruction;
}

struct TileTapeFixtureInput {
    const char* name;
    std::vector<SdfInstruction> instructions;
    std::size_t referenceBaseInstruction;
    std::size_t preCutInstructionCount;
    bool capture;
};

TileTapeFixtureInput MakeRun2DeadTermsFixture() {
    std::vector<SdfInstruction> source;
    source.reserve(325);
    source.push_back(MakeTileSphere(0.0f, 0.0f, 0.0f, 4.5f));
    source.push_back(MakeTileSphere(100.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(MakeTileSphere(102.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(MakeTileCombine(SdfOpCode::Union));
    source.push_back(MakeTileCombine(SdfOpCode::Union));
    for (int edit = 0; edit < 160; ++edit) {
        const float x = 100.0f + static_cast<float>(edit % 17) * 0.5f;
        const float y = static_cast<float>(edit % 9 - 4) * 0.25f;
        source.push_back(MakeTileSphere(x, y, 0.0f, 10.0f));
        source.push_back(MakeTileCombine(edit < 80 ? SdfOpCode::Subtract : SdfOpCode::Union));
    }
    return {"run2-dead-terms", std::move(source), 0, 325, false};
}

TileTapeFixtureInput MakeVisibleEditHeavyFixture() {
    std::vector<SdfInstruction> source;
    source.reserve(433);
    source.push_back(MakeTileSphere(0.0f, 0.0f, 0.0f, 4.5f));

    // These 32 small changes sit inside the body. A later, slightly larger shell refills
    // that volume, leaving the source edits buried/overridden in the original history.
    for (int edit = 0; edit < 32; ++edit) {
        const float x = static_cast<float>(edit % 8 - 4) * 0.12f;
        const float y = static_cast<float>(edit / 8 - 2) * 0.12f;
        const float z = static_cast<float>(edit % 3 - 1) * 0.08f;
        const float radius = 0.18f + static_cast<float>(edit % 4) * 0.025f;
        source.push_back(MakeTileSphere(x, y, z, radius));
        source.push_back(MakeTileCombine(edit % 2 == 0 ? SdfOpCode::Union : SdfOpCode::Subtract));
    }
    source.push_back(MakeTileSphere(0.0f, 0.0f, 0.0f, 4.65f));
    source.push_back(MakeTileCombine(SdfOpCode::Union));

    // Two centered boxes form a cross-shaped armor plate raised in front of the sphere.
    source.push_back(MakeTileBox(3.25f, 0.42f, 5.2f));
    source.push_back(MakeTileCombine(SdfOpCode::Union));
    source.push_back(MakeTileBox(0.42f, 1.75f, 5.2f));
    source.push_back(MakeTileCombine(SdfOpCode::Union));

    // Eight rounded patches cross the body's projected silhouette and remain visible.
    for (const auto& [x, y, z] : std::array<std::array<float, 3>, 8>{
            std::array<float, 3>{-4.2f, 0.0f, -1.4f},
            std::array<float, 3>{ 4.2f, 0.0f, -1.4f},
            std::array<float, 3>{0.0f, -4.2f, -1.4f},
            std::array<float, 3>{0.0f,  4.2f, -1.4f},
            std::array<float, 3>{-3.0f, -3.0f, -1.8f},
            std::array<float, 3>{-3.0f,  3.0f, -1.8f},
            std::array<float, 3>{ 3.0f, -3.0f, -1.8f},
            std::array<float, 3>{ 3.0f,  3.0f, -1.8f}}) {
        source.push_back(MakeTileSphere(x, y, z, 0.85f));
        source.push_back(MakeTileCombine(SdfOpCode::Union));
    }

    // Small studs are added along both plate arms before the later cuts perforate them.
    for (const float x : {-2.7f, -1.8f, -0.9f, 0.9f, 1.8f, 2.7f}) {
        for (const float y : {-0.30f, 0.30f}) {
            source.push_back(MakeTileSphere(x, y, -5.62f, 0.24f));
            source.push_back(MakeTileCombine(SdfOpCode::Union));
        }
    }
    for (const float y : {-1.45f, -0.72f, 0.72f, 1.45f}) {
        for (const float x : {-0.30f, 0.30f}) {
            source.push_back(MakeTileSphere(x, y, -5.62f, 0.24f));
            source.push_back(MakeTileCombine(SdfOpCode::Union));
        }
    }

    // A round opening and four edge cuts read as a hole and notches in the capture.
    source.push_back(MakeTileSphere(0.0f, 0.0f, -5.2f, 0.82f));
    source.push_back(MakeTileCombine(SdfOpCode::Subtract));
    for (const auto& [x, y, z, radius] : std::array<std::array<float, 4>, 4>{
            std::array<float, 4>{-3.1f, 0.0f, -5.2f, 0.75f},
            std::array<float, 4>{ 3.1f, 0.0f, -5.2f, 0.75f},
            std::array<float, 4>{0.0f, -1.58f, -5.2f, 0.72f},
            std::array<float, 4>{0.0f,  1.58f, -5.2f, 0.72f}}) {
        source.push_back(MakeTileSphere(x, y, z, radius));
        source.push_back(MakeTileCombine(SdfOpCode::Subtract));
    }

    // Twenty overlapping cutters make repeated, visible pock marks on both plate arms.
    for (const float x : {-2.8f, -2.1f, -1.4f, 1.4f, 2.1f, 2.8f}) {
        for (const float y : {-0.22f, 0.22f}) {
            source.push_back(MakeTileSphere(x, y, -5.3f, 0.34f));
            source.push_back(MakeTileCombine(SdfOpCode::Subtract));
        }
    }
    for (const float y : {-1.32f, -0.78f, 0.78f, 1.32f}) {
        for (const float x : {-0.22f, 0.22f}) {
            source.push_back(MakeTileSphere(x, y, -5.3f, 0.32f));
            source.push_back(MakeTileCombine(SdfOpCode::Subtract));
        }
    }

    // A final overlapping offscreen edit cluster is outside every certified view tile.
    // It is part of the history but should not survive any tile's evaluation tape.
    for (int edit = 0; edit < 128; ++edit) {
        const float x = 100.0f + static_cast<float>(edit % 17) * 0.5f;
        const float y = static_cast<float>(edit % 9 - 4) * 0.25f;
        source.push_back(MakeTileSphere(x, y, 0.0f, 10.0f));
        source.push_back(MakeTileCombine(edit % 2 == 0 ? SdfOpCode::Subtract : SdfOpCode::Union));
    }
    return {"visible-edit-heavy", std::move(source), 65, 127, true};
}

bool WriteVisibleEditCapture(const std::filesystem::path& path,
    const std::vector<float>& rgba32f, const std::vector<float>& baseRgba32f,
    const std::vector<float>& beforeCutsRgba32f,
    std::uint32_t width, std::uint32_t height,
    const std::vector<std::uint32_t>& retainedCounts) {
    constexpr std::uint32_t kTilesX = 4;
    constexpr std::uint32_t kTilesY = 4;
    constexpr std::uint32_t kScale = 8;
    constexpr std::uint32_t kGap = 12;
    const std::uint32_t panelWidth = width * kScale;
    const std::uint32_t panelHeight = height * kScale;
    const std::uint32_t outputWidth = panelWidth * 2 + kGap;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(outputWidth) * panelHeight * 3, 0);

    const auto writePixel = [&](std::uint32_t x, std::uint32_t y,
                                std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        const std::size_t offset = (static_cast<std::size_t>(y) * outputWidth + x) * 3;
        rgb[offset + 0] = red;
        rgb[offset + 1] = green;
        rgb[offset + 2] = blue;
    };
    const auto depthChanged = [](float first, float second) {
        return (first == -1000.0f) != (second == -1000.0f)
            || std::abs(first - second) > 1e-4f;
    };

    // Left panel: depth-mapped GPU capture, with tile boundaries matching the heatmap.
    for (std::uint32_t y = 0; y < panelHeight; ++y) {
        for (std::uint32_t x = 0; x < panelWidth; ++x) {
            const std::uint32_t sourceX = x / kScale;
            const std::uint32_t sourceY = y / kScale;
            const std::size_t sourceIndex = (static_cast<std::size_t>(sourceY) * width + sourceX) * 4;
            const float depth = rgba32f[sourceIndex];
            const float baseDepth = baseRgba32f[sourceIndex];
            const float beforeCutsDepth = beforeCutsRgba32f[sourceIndex];
            std::uint8_t red = 12, green = 18, blue = 30;
            if (depth != -1000.0f) {
                const float t = std::clamp((depth + 6.0f) / 12.0f, 0.0f, 1.0f);
                red = static_cast<std::uint8_t>(35.0f + 205.0f * t);
                green = static_cast<std::uint8_t>(125.0f - 35.0f * t);
                blue = static_cast<std::uint8_t>(225.0f - 185.0f * t);
            }
            if (depthChanged(depth, beforeCutsDepth)) {
                red = 20;
                green = 230;
                blue = 255;
            } else if (depthChanged(beforeCutsDepth, baseDepth)) {
                red = 255;
                green = 190;
                blue = 35;
            }
            const std::uint32_t tilePixelWidth = panelWidth / kTilesX;
            const std::uint32_t tilePixelHeight = panelHeight / kTilesY;
            if (x % tilePixelWidth < 2 || y % tilePixelHeight < 2) {
                red = static_cast<std::uint8_t>(red / 2);
                green = static_cast<std::uint8_t>(green / 2);
                blue = static_cast<std::uint8_t>(blue / 2);
            }
            writePixel(x, y, red, green, blue);
        }
    }

    if (retainedCounts.size() != kTilesX * kTilesY * 4)
        return false;
    const auto [minimum, maximum] = std::minmax_element(retainedCounts.begin(), retainedCounts.end());
    const std::uint32_t countMin = *minimum;
    const std::uint32_t countMax = *maximum;
    const std::uint32_t heatTileWidth = panelWidth / (kTilesX * 2);
    const std::uint32_t heatTileHeight = panelHeight / (kTilesY * 2);

    // Right panel: all 64 retained counts are shown as four 4x4 maps, one per Z tile layer.
    // The four maps are arranged in a 2x2 block and use the same XY tile ordering as the capture.
    for (std::uint32_t y = 0; y < panelHeight; ++y) {
        for (std::uint32_t x = 0; x < panelWidth; ++x) {
            const std::uint32_t mapX = x / (panelWidth / 2);
            const std::uint32_t mapY = y / (panelHeight / 2);
            const std::uint32_t tileZ = mapY * 2 + mapX;
            const std::uint32_t tileX = (x % (panelWidth / 2)) / heatTileWidth;
            const std::uint32_t tileY = (y % (panelHeight / 2)) / heatTileHeight;
            const std::uint32_t tileIndex = (tileZ * kTilesY + tileY) * kTilesX + tileX;
            const std::uint32_t count = retainedCounts[tileIndex];
            const float t = countMax == countMin ? 0.5f
                : static_cast<float>(count - countMin) / static_cast<float>(countMax - countMin);
            std::uint8_t red = static_cast<std::uint8_t>(30.0f + 225.0f * t);
            std::uint8_t green = static_cast<std::uint8_t>(80.0f + 100.0f * (1.0f - std::abs(2.0f * t - 1.0f)));
            std::uint8_t blue = static_cast<std::uint8_t>(230.0f - 200.0f * t);
            if (x % heatTileWidth < 2 || y % heatTileHeight < 2) {
                red = static_cast<std::uint8_t>(red / 3);
                green = static_cast<std::uint8_t>(green / 3);
                blue = static_cast<std::uint8_t>(blue / 3);
            }
            writePixel(panelWidth + kGap + x, y, red, green, blue);
        }
    }
    return stbi_write_png(path.string().c_str(), static_cast<int>(outputWidth),
        static_cast<int>(panelHeight), 3, rgb.data(), static_cast<int>(outputWidth * 3)) != 0;
}

class DeclaredPositionRenderTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             realGpuConfirmed_ = false;
    bool             timestampsSupported_ = false;
    std::string      selectedDeviceName_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_recipe_declared_position_render";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;

        if (vkCreateInstance(&instInfo, nullptr, &instance_) != VK_SUCCESS) {
            GTEST_SKIP() << "vkCreateInstance failed — no Vulkan available on this machine.";
        }

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) GTEST_SKIP() << "No Vulkan physical devices visible.";
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
            GTEST_SKIP() << "No REAL (discrete/integrated) GPU found — skipping live-render gate.";
        }

        CreateLogicalDevice();
        CreateCommandPool();
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                queueFamily_ = i;
                timestampsSupported_ = qfs[i].timestampValidBits != 0;
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found) << "No compute queue family on the selected GPU";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        // shaderStorageImageWriteWithoutFormat: rgba32f imageStore needs this on some drivers
        // (same gotcha as test_procedural_recipe_render.cpp).
        VkPhysicalDeviceFeatures features{};
        features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos    = &qInfo;
        dInfo.pEnabledFeatures     = &features;
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
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImage& outImage, VkDeviceMemory& outMem) {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = format;
        ci.extent        = {w, h, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &outImage), VK_SUCCESS);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(logicalDevice_, outImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, outImage, outMem, 0), VK_SUCCESS);
    }

    VkImageView CreateView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo vi{};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateImageView(logicalDevice_, &vi, nullptr, &view), VK_SUCCESS);
        return view;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuf, VkDeviceMemory& outMem) {
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

    // Dispatch the slice shader once, readback RGBA32F pixels.
    void RenderSlice(const std::vector<uint32_t>& spirv, const std::array<float, 6>& params,
                      uint32_t w, uint32_t h, std::vector<float>& outRgba32f,
                      double* gpuMilliseconds = nullptr) {
        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());

        const VkFormat kColorFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
        VkImage colorImg = VK_NULL_HANDLE; VkDeviceMemory colorMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE);

        const VkDeviceSize paramsSize = params.size() * sizeof(float);
        VkBuffer paramsBuf = VK_NULL_HANDLE; VkDeviceMemory paramsMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(paramsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, paramsBuf, paramsMem));
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

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
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

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes    = poolSizes;
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool     = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts        = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

        VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo paramsInfo{paramsBuf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSet; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &colorInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSet; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &paramsInfo;
        vkUpdateDescriptorSets(logicalDevice_, 2, writes, 0, nullptr);

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = commandPool_;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

        VkQueryPool queryPool = VK_NULL_HANDLE;
        if (gpuMilliseconds != nullptr) {
            VkQueryPoolCreateInfo queryPoolInfo{};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryPoolInfo.queryCount = 2;
            ASSERT_EQ(vkCreateQueryPool(logicalDevice_, &queryPoolInfo, nullptr, &queryPool), VK_SUCCESS);
        }

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);
        if (queryPool != VK_NULL_HANDLE) vkCmdResetQueryPool(cmd, queryPool, 0, 2);

        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image               = colorImg;
        toGeneral.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toGeneral.srcAccessMask       = 0;
        toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        if (queryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 0);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
        if (queryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);

        VkImageMemoryBarrier toSrc{};
        toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image               = colorImg;
        toSrc.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSrc);

        const VkDeviceSize rbSize = static_cast<VkDeviceSize>(w) * h * 4 * sizeof(float);
        VkBuffer rbBuf = VK_NULL_HANDLE; VkDeviceMemory rbMem = VK_NULL_HANDLE;
        CreateHostBuffer(rbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rbBuf, rbMem);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent      = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &copy);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;

        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        if (queryPool != VK_NULL_HANDLE) {
            std::uint64_t timestamps[2]{};
            ASSERT_EQ(vkGetQueryPoolResults(logicalDevice_, queryPool, 0, 2, sizeof(timestamps),
                timestamps, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT), VK_SUCCESS);
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
            *gpuMilliseconds = static_cast<double>(timestamps[1] - timestamps[0])
                * static_cast<double>(properties.limits.timestampPeriod) / 1.0e6;
        }

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rbMem, 0, rbSize, 0, &mapped), VK_SUCCESS);
        outRgba32f.resize(static_cast<size_t>(w) * h * 4);
        std::memcpy(outRgba32f.data(), mapped, static_cast<size_t>(rbSize));
        vkUnmapMemory(logicalDevice_, rbMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rbBuf, nullptr); vkFreeMemory(logicalDevice_, rbMem, nullptr);
        if (queryPool != VK_NULL_HANDLE) vkDestroyQueryPool(logicalDevice_, queryPool, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr); vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyBuffer(logicalDevice_, paramsBuf, nullptr); vkFreeMemory(logicalDevice_, paramsMem, nullptr);
    }

    void RenderRecipeTape(const std::vector<std::uint32_t>& spirv,
        const std::vector<GpuRecipeTapeInstruction>& tape,
        const std::array<TileTapeRange, 64>& ranges, std::uint32_t w, std::uint32_t h,
        std::vector<float>& outRgba32f, std::vector<std::uint32_t>& outClauseCounts,
        double& gpuMilliseconds) {
        ASSERT_TRUE(realGpuConfirmed_) << "ABORT: not a confirmed real GPU; refusing vkQueueSubmit.";
        ASSERT_FALSE(spirv.empty());
        ASSERT_FALSE(tape.empty());

        constexpr VkFormat kColorFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        VkImage colorImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFormat, colorImage, colorMemory));
        VkImageView colorView = CreateView(colorImage, kColorFormat);
        ASSERT_NE(colorView, VK_NULL_HANDLE);

        const VkDeviceSize tapeBytes = tape.size() * sizeof(GpuRecipeTapeInstruction);
        const VkDeviceSize rangeBytes = ranges.size() * sizeof(TileTapeRange);
        const VkDeviceSize clauseBytes = static_cast<VkDeviceSize>(w) * h * sizeof(std::uint32_t);
        VkBuffer tapeBuffer = VK_NULL_HANDLE; VkDeviceMemory tapeMemory = VK_NULL_HANDLE;
        VkBuffer rangeBuffer = VK_NULL_HANDLE; VkDeviceMemory rangeMemory = VK_NULL_HANDLE;
        VkBuffer clauseBuffer = VK_NULL_HANDLE; VkDeviceMemory clauseMemory = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(tapeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            tapeBuffer, tapeMemory));
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(rangeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            rangeBuffer, rangeMemory));
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(clauseBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            clauseBuffer, clauseMemory));
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, tapeMemory, 0, tapeBytes, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, tape.data(), static_cast<size_t>(tapeBytes));
        vkUnmapMemory(logicalDevice_, tapeMemory);
        ASSERT_EQ(vkMapMemory(logicalDevice_, rangeMemory, 0, rangeBytes, 0, &mapped), VK_SUCCESS);
        std::memcpy(mapped, ranges.data(), static_cast<size_t>(rangeBytes));
        vkUnmapMemory(logicalDevice_, rangeMemory);
        ASSERT_EQ(vkMapMemory(logicalDevice_, clauseMemory, 0, clauseBytes, 0, &mapped), VK_SUCCESS);
        std::memset(mapped, 0, static_cast<size_t>(clauseBytes));
        vkUnmapMemory(logicalDevice_, clauseMemory);

        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &shaderInfo, nullptr, &shaderModule), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[4]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        for (std::uint32_t binding = 1; binding < 4; ++binding)
            bindings[binding] = {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = 4;
        setLayoutInfo.pBindings = bindings;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &setLayoutInfo, nullptr, &setLayout), VK_SUCCESS);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &setLayout;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &pipelineLayoutInfo, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &pipelineInfo,
            nullptr, &pipeline), VK_SUCCESS);

        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 2;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateQueryPool(logicalDevice_, &queryPoolInfo, nullptr, &queryPool), VK_SUCCESS);

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &poolInfo, nullptr, &descriptorPool), VK_SUCCESS);
        VkDescriptorSetAllocateInfo setAllocateInfo{};
        setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAllocateInfo.descriptorPool = descriptorPool;
        setAllocateInfo.descriptorSetCount = 1;
        setAllocateInfo.pSetLayouts = &setLayout;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &setAllocateInfo, &descriptorSet), VK_SUCCESS);

        VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bufferInfos[3] = {
            {tapeBuffer, 0, VK_WHOLE_SIZE},
            {rangeBuffer, 0, VK_WHOLE_SIZE},
            {clauseBuffer, 0, VK_WHOLE_SIZE},
        };
        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet; writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &imageInfo;
        for (std::uint32_t index = 0; index < 3; ++index) {
            writes[index + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index + 1].dstSet = descriptorSet;
            writes[index + 1].dstBinding = index + 1;
            writes[index + 1].descriptorCount = 1;
            writes[index + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index + 1].pBufferInfo = &bufferInfos[index];
        }
        vkUpdateDescriptorSets(logicalDevice_, 4, writes, 0, nullptr);

        VkCommandBufferAllocateInfo commandAllocateInfo{};
        commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAllocateInfo.commandPool = commandPool_;
        commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocateInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &commandAllocateInfo, &commandBuffer), VK_SUCCESS);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(commandBuffer, &beginInfo), VK_SUCCESS);
        vkCmdResetQueryPool(commandBuffer, queryPool, 0, 2);

        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = colorImage;
        toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 0);
        vkCmdDispatch(commandBuffer, (w + 7) / 8, (h + 7) / 8, 1);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = colorImage;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        const VkDeviceSize readbackBytes = static_cast<VkDeviceSize>(w) * h * 4 * sizeof(float);
        VkBuffer readbackBuffer = VK_NULL_HANDLE; VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateHostBuffer(readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            readbackBuffer, readbackMemory));
        VkBufferImageCopy imageCopy{};
        imageCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        imageCopy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(commandBuffer, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readbackBuffer, 1, &imageCopy);

        VkBufferMemoryBarrier hostBarriers[2]{};
        hostBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hostBarriers[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[0].buffer = clauseBuffer;
        hostBarriers[0].size = clauseBytes;
        hostBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarriers[1].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarriers[1].buffer = readbackBuffer;
        hostBarriers[1].size = readbackBytes;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 2, hostBarriers, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(commandBuffer), VK_SUCCESS);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        std::uint64_t timestamps[2]{};
        ASSERT_EQ(vkGetQueryPoolResults(logicalDevice_, queryPool, 0, 2, sizeof(timestamps),
            timestamps, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT), VK_SUCCESS);
        VkPhysicalDeviceProperties physicalProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &physicalProperties);
        gpuMilliseconds = static_cast<double>(timestamps[1] - timestamps[0])
            * static_cast<double>(physicalProperties.limits.timestampPeriod) / 1.0e6;

        ASSERT_EQ(vkMapMemory(logicalDevice_, readbackMemory, 0, readbackBytes, 0, &mapped), VK_SUCCESS);
        outRgba32f.resize(static_cast<size_t>(w) * h * 4);
        std::memcpy(outRgba32f.data(), mapped, static_cast<size_t>(readbackBytes));
        vkUnmapMemory(logicalDevice_, readbackMemory);
        ASSERT_EQ(vkMapMemory(logicalDevice_, clauseMemory, 0, clauseBytes, 0, &mapped), VK_SUCCESS);
        outClauseCounts.resize(static_cast<size_t>(w) * h);
        std::memcpy(outClauseCounts.data(), mapped, static_cast<size_t>(clauseBytes));
        vkUnmapMemory(logicalDevice_, clauseMemory);

        vkDestroyBuffer(logicalDevice_, readbackBuffer, nullptr); vkFreeMemory(logicalDevice_, readbackMemory, nullptr);
        vkFreeCommandBuffers(logicalDevice_, commandPool_, 1, &commandBuffer);
        vkDestroyDescriptorPool(logicalDevice_, descriptorPool, nullptr);
        vkDestroyQueryPool(logicalDevice_, queryPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, setLayout, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImage(logicalDevice_, colorImage, nullptr); vkFreeMemory(logicalDevice_, colorMemory, nullptr);
        vkDestroyBuffer(logicalDevice_, clauseBuffer, nullptr); vkFreeMemory(logicalDevice_, clauseMemory, nullptr);
        vkDestroyBuffer(logicalDevice_, rangeBuffer, nullptr); vkFreeMemory(logicalDevice_, rangeMemory, nullptr);
        vkDestroyBuffer(logicalDevice_, tapeBuffer, nullptr); vkFreeMemory(logicalDevice_, tapeMemory, nullptr);
    }
};

} // namespace

TEST_F(DeclaredPositionRenderTest, ThreeDeclaredPositionsMoveTheRenderedShape) {
    constexpr uint32_t W = 256, H = 256;
    constexpr float kWorldHalfExtent = 4.0f; // world extent [-4,4]^2

    // Same prototype recipe as test_recipe_eval_parity.cpp /
    // test_recipe_glsl_numerical_parity.cpp's dedicated test.
    const SdfInstruction prog[] = {
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::ReadParamFloat3; in.paramMask = 1; in.data[0] = 0.0f; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::DeclarePosition; return in; }(),
        [] { SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere; in.data[3] = 0.5f; return in; }(),
    };
    constexpr uint32_t kCount = 3;

    std::ifstream kernelFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_TRUE(kernelFile.good()) << "Cannot open vendored GLSL: " << SDF_CORE_KERNELS_GLSL_PATH;
    std::ostringstream kss;
    kss << kernelFile.rdbuf();
    const std::string sdfCoreGlsl = kss.str();

    const std::string fieldFn = EmitProceduralFieldFunctionGlsl(
        prog, kCount, /*recipeId=*/0, /*emitDeclaredPositionOutParam=*/true);
    const std::string shaderSrc = ComposeSliceShader(sdfCoreGlsl, fieldFn, kWorldHalfExtent);

    ShaderManagement::ShaderCompiler compiler;
    ShaderManagement::CompilationOptions opts;
    opts.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
    auto compOut = compiler.Compile(ShaderManagement::ShaderStage::Compute, shaderSrc, "main", opts);
    ASSERT_TRUE(compOut.success)
        << "GLSL compile failed:\n" << compOut.GetFullLog() << "\n--- source ---\n" << shaderSrc;
    ASSERT_FALSE(compOut.spirv.empty());

    struct Case { const char* path; glm::vec3 declared; };
    const std::array<Case, 3> cases = {{
        {"/tmp/declared_pos_0.png", glm::vec3(0.0f, 0.0f, 0.0f)},   // sphere at world origin
        {"/tmp/declared_pos_1.png", glm::vec3(2.0f, 1.5f, 0.0f)},   // moved up-right
        {"/tmp/declared_pos_2.png", glm::vec3(-2.5f, -1.0f, 0.0f)}, // moved down-left
    }};

    for (const auto& c : cases) {
        const std::array<float, 6> params = {c.declared.x, c.declared.y, c.declared.z, 0.0f, 0.0f, 0.0f};
        std::vector<float> rgba32f;
        ASSERT_NO_FATAL_FAILURE(RenderSlice(compOut.spirv, params, W, H, rgba32f));
        ASSERT_EQ(rgba32f.size(), static_cast<size_t>(W) * H * 4);

        int insidePixels = 0;
        std::vector<uint8_t> rgba8(W * H * 4);
        for (uint32_t i = 0; i < W * H; ++i) {
            const float v = rgba32f[i * 4 + 0]; // R==G==B (grayscale inside mask)
            const uint8_t b = static_cast<uint8_t>(v > 0.5f ? 255 : 0);
            rgba8[i * 4 + 0] = b; rgba8[i * 4 + 1] = b; rgba8[i * 4 + 2] = b; rgba8[i * 4 + 3] = 255;
            if (v > 0.5f) ++insidePixels;
        }
        const int pngOk = stbi_write_png(c.path, static_cast<int>(W), static_cast<int>(H), 4,
                                          rgba8.data(), static_cast<int>(W) * 4);
        printf("[DeclaredPositionRender] declared=(%.2f,%.2f,%.2f) insidePixels=%d PNG=%s path=%s\n",
               c.declared.x, c.declared.y, c.declared.z, insidePixels, pngOk ? "YES" : "NO", c.path);
        fflush(stdout);
        EXPECT_TRUE(pngOk) << "stbi_write_png failed for " << c.path;

        // Sanity: some pixels must be inside (the sphere is within the sampled world extent
        // for all 3 declared positions above) and it must not be the WHOLE image (i.e. the
        // shape is a bounded disc, not degenerate).
        EXPECT_GT(insidePixels, 20) << "declared=(" << c.declared.x << "," << c.declared.y
                                     << "," << c.declared.z << ") — sphere disc too small/absent";
        EXPECT_LT(insidePixels, static_cast<int>(W * H) / 2)
            << "declared=(" << c.declared.x << "," << c.declared.y << "," << c.declared.z
            << ") — unexpectedly large inside region";

        // Compute the pixel-space centroid of the inside mask and confirm it lands near the
        // expected screen position for the declared world position (world->pixel: same
        // mapping as ComposeSliceShader's u/v -> world formula, inverted).
        double sumX = 0.0, sumY = 0.0;
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                if (rgba32f[(y * W + x) * 4 + 0] > 0.5f) { sumX += x; sumY += y; }
            }
        }
        ASSERT_GT(insidePixels, 0);
        const double centroidPxX = sumX / insidePixels;
        const double centroidPxY = sumY / insidePixels;
        const double expectedU = (c.declared.x / kWorldHalfExtent + 1.0) * 0.5;
        const double expectedV = (c.declared.y / kWorldHalfExtent + 1.0) * 0.5;
        const double expectedPxX = expectedU * W;
        const double expectedPxY = expectedV * H;
        EXPECT_NEAR(centroidPxX, expectedPxX, 4.0)
            << "declared=(" << c.declared.x << "," << c.declared.y << ") centroid X mismatch — "
               "declared position did not move the rendered shape as expected";
        EXPECT_NEAR(centroidPxY, expectedPxY, 4.0)
            << "declared=(" << c.declared.x << "," << c.declared.y << ") centroid Y mismatch — "
               "declared position did not move the rendered shape as expected";
    }
}

TEST_F(DeclaredPositionRenderTest, IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels) {
    if (!timestampsSupported_)
        GTEST_SKIP() << "Selected compute queue has no timestamp support for the GPU-time witness.";

    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    constexpr std::uint32_t kTilesX = 4;
    constexpr std::uint32_t kTilesY = 4;
    constexpr std::uint32_t kTilesZ = 4;
    constexpr std::uint32_t kSteps = 121;
    constexpr std::uint32_t kTileCount = kTilesX * kTilesY * kTilesZ;
    constexpr std::uint32_t kTilePixelsX = kWidth / kTilesX;
    constexpr std::uint32_t kTilePixelsY = kHeight / kTilesY;
    constexpr float kWorldHalfExtent = 6.0f;

    const auto pixelWorld = [](std::uint32_t pixel, std::uint32_t extent) {
        const float u = (static_cast<float>(pixel) + 0.5f) / static_cast<float>(extent);
        return (u * 2.0f - 1.0f) * kWorldHalfExtent;
    };
    const auto stepWorld = [](std::uint32_t step) {
        return -kWorldHalfExtent + static_cast<float>(step)
            * ((2.0f * kWorldHalfExtent) / static_cast<float>(kSteps - 1));
    };

    const std::array<TileTapeFixtureInput, 2> fixtures{
        MakeRun2DeadTermsFixture(),
        MakeVisibleEditHeavyFixture()};
    const std::string sdfCoreGlsl = ReadWholeFile(SDF_CORE_KERNELS_GLSL_PATH);
    const std::string tapeGlsl = ReadWholeFile(SDF_RECIPE_TAPE_GLSL_PATH);
    ASSERT_FALSE(sdfCoreGlsl.empty()) << "Cannot read generated SDF kernels: " << SDF_CORE_KERNELS_GLSL_PATH;
    ASSERT_FALSE(tapeGlsl.empty()) << "Cannot read generated tape evaluator: " << SDF_RECIPE_TAPE_GLSL_PATH;

    for (const TileTapeFixtureInput& fixture : fixtures) {
        const std::vector<SdfInstruction>& source = fixture.instructions;
        const std::vector<SdfInstruction> sourceHistory = source;
        if (std::string(fixture.name) == "run2-dead-terms")
            ASSERT_EQ(source.size(), 325u);
        else
            ASSERT_EQ(source.size(), 433u);

        RecipeWholeDomainCompactionRequest compactionRequest;
        compactionRequest.sourceRevision = std::string(fixture.name) == "run2-dead-terms" ? 1u : 2u;
        compactionRequest.domain = {
            glm::vec3(-kWorldHalfExtent), glm::vec3(kWorldHalfExtent)};
        // ComposeCompiledRecipeRaymarchShader samples every point inside this box: pixel
        // centers are in (-6,6) for X/Y and the 121 z steps include both endpoints.
        compactionRequest.domainIsEnforced = true;
        compactionRequest.requiredOutputChannels = kAllRecipeOutputChannels;
        compactionRequest.channelDependencies = {
            RecipeChannelDependency::GeometryWinner,
            RecipeChannelDependency::Independent,
            RecipeChannelDependency::Independent,
            RecipeChannelDependency::Independent,
            RecipeChannelDependency::Independent,
        };
        const RecipeWholeDomainCompactionResult wholeDomain =
            CompactRecipeOverWholeDomain(source, compactionRequest);
        EXPECT_EQ(wholeDomain.sourceRevision, compactionRequest.sourceRevision);
        ASSERT_TRUE(wholeDomain.stats.intervalProofApplied)
            << "whole-domain proof fallback=" << static_cast<int>(wholeDomain.stats.intervalFallback);
        ASSERT_LT(wholeDomain.instructions.size(), source.size());
        EXPECT_EQ(std::memcmp(source.data(), sourceHistory.data(),
            source.size() * sizeof(SdfInstruction)), 0) << "source recipe history was mutated";

        const auto specializeStart = std::chrono::steady_clock::now();
        std::vector<GpuRecipeTapeInstruction> prunedTape;
        std::array<TileTapeRange, kTileCount> prunedRanges{};
        std::uint64_t intervalEvaluations = 0;
        std::uint64_t individualSpecializationNs = 0;
        std::uint64_t prunedInstructions = 0;
        for (std::uint32_t tileZ = 0; tileZ < kTilesZ; ++tileZ) {
            const std::uint32_t firstStep = (tileZ * kSteps + kTilesZ - 1) / kTilesZ;
            const std::uint32_t lastStep = ((tileZ + 1) * kSteps + kTilesZ - 1) / kTilesZ - 1;
            for (std::uint32_t tileY = 0; tileY < kTilesY; ++tileY) {
                const std::uint32_t firstY = tileY * kTilePixelsY;
                const std::uint32_t lastY = (tileY + 1) * kTilePixelsY - 1;
                for (std::uint32_t tileX = 0; tileX < kTilesX; ++tileX) {
                    const std::uint32_t firstX = tileX * kTilePixelsX;
                    const std::uint32_t lastX = (tileX + 1) * kTilePixelsX - 1;
                    constexpr float kDomainPadding = 0.001f;
                    const RecipeTileDomain domain{
                        glm::vec3(pixelWorld(firstX, kWidth) - kDomainPadding,
                            pixelWorld(firstY, kHeight) - kDomainPadding,
                            stepWorld(firstStep) - kDomainPadding),
                        glm::vec3(pixelWorld(lastX, kWidth) + kDomainPadding,
                            pixelWorld(lastY, kHeight) + kDomainPadding,
                            stepWorld(lastStep) + kDomainPadding)};
                    const auto specialized = SpecializeRecipeTapeForTile(source, domain);
                    ASSERT_EQ(specialized.stats.fallback, RecipeTileFallback::None)
                        << "tile=" << tileX << "," << tileY << "," << tileZ;
                    ASSERT_EQ(specialized.instructions.size(), specialized.stats.retainedInstructions)
                        << "tile=" << tileX << "," << tileY << "," << tileZ;
                    const std::uint32_t tileIndex = (tileZ * kTilesY + tileY) * kTilesX + tileX;
                    prunedRanges[tileIndex] = {
                        static_cast<std::uint32_t>(prunedTape.size()),
                        static_cast<std::uint32_t>(specialized.instructions.size())};
                    const auto packed = PackGpuTape(specialized.instructions);
                    prunedTape.insert(prunedTape.end(), packed.begin(), packed.end());
                    intervalEvaluations += specialized.stats.intervalEvaluations;
                    individualSpecializationNs += specialized.stats.specializationNanoseconds;
                    prunedInstructions += specialized.stats.prunedInstructions;
                }
            }
        }
        const double specializationCpuMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - specializeStart).count();
        ASSERT_EQ(prunedRanges.size(), kTileCount);
        ASSERT_GT(prunedTape.size(), 0u);
        std::vector<std::uint32_t> retainedCounts;
        retainedCounts.reserve(kTileCount);
        for (const TileTapeRange& range : prunedRanges)
            retainedCounts.push_back(range[1]);

        std::array<TileTapeRange, kTileCount> fullRanges{};
        fullRanges.fill(TileTapeRange{0u, static_cast<std::uint32_t>(source.size())});
        const auto fullTape = PackGpuTape(source);
        ASSERT_EQ(fullTape.size(), source.size());

        Vixen::SVO::RecipeRegistry emptyRegistry;
        std::string unrollError;
        std::vector<SdfInstruction> sourceUnrolled;
        ASSERT_TRUE(Vixen::SVO::UnrollRecipeInstructions(source.data(),
            static_cast<std::uint32_t>(source.size()), emptyRegistry, sourceUnrolled, unrollError))
            << unrollError;
        std::vector<SdfInstruction> compactedUnrolled;
        ASSERT_TRUE(Vixen::SVO::UnrollRecipeInstructions(wholeDomain.instructions.data(),
            static_cast<std::uint32_t>(wholeDomain.instructions.size()), emptyRegistry,
            compactedUnrolled, unrollError)) << unrollError;
        EXPECT_EQ(std::memcmp(source.data(), sourceHistory.data(),
            source.size() * sizeof(SdfInstruction)), 0) << "unrolling mutated source recipe history";
        const std::string unrolledField = EmitProceduralFieldFunctionGlsl(sourceUnrolled.data(),
            static_cast<std::uint32_t>(sourceUnrolled.size()), 0);

        const std::string tapeAdapter = R"GLSL(
    bool EvaluateAtTile(uint offset, uint count, vec3 p, out float distanceValue, out uint clauses) {
        if (EvalRecipeTape(offset, count, p, distanceValue, clauses)) return true;
        float params[6] = float[6](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        distanceValue = sdfRecipe_0(p, params);
        clauses = count;
        return true;
    }
    )GLSL";
        const std::string tapeShaderSource = ComposeTileTapeRaymarchShader(
            sdfCoreGlsl, tapeGlsl + "\n" + unrolledField, tapeAdapter);

        ShaderManagement::ShaderCompiler compiler;
        ShaderManagement::CompilationOptions options;
        options.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::GLSL;
        const auto tapeCompileStart = std::chrono::steady_clock::now();
        const auto tapeCompile = compiler.Compile(ShaderManagement::ShaderStage::Compute,
            tapeShaderSource, "main", options);
        const double tapeCompileMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tapeCompileStart).count();
        ASSERT_TRUE(tapeCompile.success) << "Tape GLSL compile failed:\n" << tapeCompile.GetFullLog()
            << "\n--- source ---\n" << tapeShaderSource;

        const std::string unrolledAdapter = R"GLSL(
    bool EvaluateAtTile(uint offset, uint count, vec3 p, out float distanceValue, out uint clauses) {
        float params[6] = float[6](0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        distanceValue = sdfRecipe_0(p, params);
        clauses = count;
        return true;
    }
    )GLSL";
        const std::string unrolledShaderSource = ComposeTileTapeRaymarchShader(
            sdfCoreGlsl, unrolledField, unrolledAdapter);
        const auto unrolledCompileStart = std::chrono::steady_clock::now();
        const auto unrolledCompile = compiler.Compile(ShaderManagement::ShaderStage::Compute,
            unrolledShaderSource, "main", options);
        const double unrolledCompileMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - unrolledCompileStart).count();
        ASSERT_TRUE(unrolledCompile.success) << "Unrolled GLSL compile failed:\n"
            << unrolledCompile.GetFullLog() << "\n--- source ---\n" << unrolledShaderSource;

        const std::string sourceCompiledShaderSource = ComposeCompiledRecipeRaymarchShader(
            sdfCoreGlsl, unrolledField);
        const auto sourceCompiledStart = std::chrono::steady_clock::now();
        const auto sourceCompiled = compiler.Compile(ShaderManagement::ShaderStage::Compute,
            sourceCompiledShaderSource, "main", options);
        const double sourceCompiledMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - sourceCompiledStart).count();
        ASSERT_TRUE(sourceCompiled.success) << "Source unrolled recipe compile failed:\n"
            << sourceCompiled.GetFullLog() << "\n--- source ---\n" << sourceCompiledShaderSource;

        const std::string compactedField = EmitProceduralFieldFunctionGlsl(
            compactedUnrolled.data(), static_cast<std::uint32_t>(compactedUnrolled.size()), 0);
        const std::string compactedShaderSource = ComposeCompiledRecipeRaymarchShader(
            sdfCoreGlsl, compactedField);
        const auto compactedCompileStart = std::chrono::steady_clock::now();
        const auto compactedCompile = compiler.Compile(ShaderManagement::ShaderStage::Compute,
            compactedShaderSource, "main", options);
        const double compactedCompileMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - compactedCompileStart).count();
        ASSERT_TRUE(compactedCompile.success) << "Compacted unrolled recipe compile failed:\n"
            << compactedCompile.GetFullLog() << "\n--- source ---\n" << compactedShaderSource;

        std::vector<float> fullPixels;
        std::vector<std::uint32_t> fullClauses;
        std::vector<double> fullGpuTimes;
        for (int run = 0; run < 4; ++run) {
            std::vector<float> pixels;
            std::vector<std::uint32_t> clauses;
            double gpuMs = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderRecipeTape(tapeCompile.spirv, fullTape, fullRanges,
                kWidth, kHeight, pixels, clauses, gpuMs));
            if (run > 0) {
                EXPECT_EQ(std::memcmp(fullPixels.data(), pixels.data(), pixels.size() * sizeof(float)), 0);
                EXPECT_EQ(fullClauses, clauses);
                fullGpuTimes.push_back(gpuMs);
            }
            fullPixels = std::move(pixels);
            fullClauses = std::move(clauses);
        }

        std::vector<float> prunedPixels;
        std::vector<std::uint32_t> prunedClauses;
        std::vector<double> prunedGpuTimes;
        for (int run = 0; run < 4; ++run) {
            std::vector<float> pixels;
            std::vector<std::uint32_t> clauses;
            double gpuMs = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderRecipeTape(tapeCompile.spirv, prunedTape, prunedRanges,
                kWidth, kHeight, pixels, clauses, gpuMs));
            if (run > 0) {
                EXPECT_EQ(std::memcmp(prunedPixels.data(), pixels.data(), pixels.size() * sizeof(float)), 0);
                EXPECT_EQ(prunedClauses, clauses);
                prunedGpuTimes.push_back(gpuMs);
            }
            prunedPixels = std::move(pixels);
            prunedClauses = std::move(clauses);
        }

        std::vector<float> unrolledPixels;
        std::vector<std::uint32_t> unrolledClauses;
        std::vector<double> unrolledGpuTimes;
        for (int run = 0; run < 4; ++run) {
            std::vector<float> pixels;
            std::vector<std::uint32_t> clauses;
            double gpuMs = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderRecipeTape(unrolledCompile.spirv, fullTape, fullRanges,
                kWidth, kHeight, pixels, clauses, gpuMs));
            if (run > 0) {
                EXPECT_EQ(std::memcmp(fullPixels.data(), pixels.data(), pixels.size() * sizeof(float)), 0);
                EXPECT_EQ(fullClauses, clauses);
                unrolledGpuTimes.push_back(gpuMs);
            }
            unrolledPixels = std::move(pixels);
            unrolledClauses = std::move(clauses);
        }

        const std::array<float, 6> recipeParams{};
        std::vector<float> sourceCompiledPixels;
        std::vector<double> sourceCompiledGpuTimes;
        for (int run = 0; run < 4; ++run) {
            std::vector<float> pixels;
            double gpuMs = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderSlice(sourceCompiled.spirv, recipeParams,
                kWidth, kHeight, pixels, &gpuMs));
            if (run > 0) sourceCompiledGpuTimes.push_back(gpuMs);
            sourceCompiledPixels = std::move(pixels);
        }

        std::vector<float> compactedCompiledPixels;
        std::vector<double> compactedCompiledGpuTimes;
        for (int run = 0; run < 4; ++run) {
            std::vector<float> pixels;
            double gpuMs = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderSlice(compactedCompile.spirv, recipeParams,
                kWidth, kHeight, pixels, &gpuMs));
            if (run > 0) compactedCompiledGpuTimes.push_back(gpuMs);
            compactedCompiledPixels = std::move(pixels);
        }

        ASSERT_LT(fixture.referenceBaseInstruction, source.size());
        const auto baseTape = PackGpuTape(std::span<const SdfInstruction>(
            source.data() + fixture.referenceBaseInstruction, 1));
        std::array<TileTapeRange, kTileCount> baseRanges{};
        baseRanges.fill(TileTapeRange{0u, 1u});
        std::vector<float> basePixels;
        std::vector<std::uint32_t> baseClauses;
        double baseGpuMilliseconds = 0.0;
        ASSERT_NO_FATAL_FAILURE(RenderRecipeTape(tapeCompile.spirv, baseTape, baseRanges,
            kWidth, kHeight, basePixels, baseClauses, baseGpuMilliseconds));

        std::vector<float> beforeCutsPixels;
        if (fixture.capture) {
            ASSERT_LE(fixture.preCutInstructionCount, source.size());
            const auto beforeCutsTape = PackGpuTape(std::span<const SdfInstruction>(
                source.data(), fixture.preCutInstructionCount));
            std::array<TileTapeRange, kTileCount> beforeCutsRanges{};
            beforeCutsRanges.fill(TileTapeRange{0u,
                static_cast<std::uint32_t>(fixture.preCutInstructionCount)});
            std::vector<std::uint32_t> beforeCutsClauses;
            double beforeCutsGpuMilliseconds = 0.0;
            ASSERT_NO_FATAL_FAILURE(RenderRecipeTape(tapeCompile.spirv, beforeCutsTape,
                beforeCutsRanges, kWidth, kHeight, beforeCutsPixels, beforeCutsClauses,
                beforeCutsGpuMilliseconds));
        } else {
            beforeCutsPixels = fullPixels;
        }

        const auto totalClauses = [](const std::vector<std::uint32_t>& counts) {
            std::uint64_t total = 0;
            for (const std::uint32_t count : counts) total += count;
            return total;
        };
        const auto hitPixels = [](const std::vector<float>& pixels) {
            std::uint32_t hits = 0;
            for (std::size_t pixel = 0; pixel < pixels.size() / 4; ++pixel)
                if (pixels[pixel * 4] != -1000.0f) ++hits;
            return hits;
        };
        const auto clausesForHits = [](const std::vector<float>& pixels,
                                       const std::vector<std::uint32_t>& clauses) {
            std::uint64_t total = 0;
            for (std::size_t pixel = 0; pixel < pixels.size() / 4; ++pixel)
                if (pixels[pixel * 4] != -1000.0f) total += clauses[pixel];
            return total;
        };
        const auto median = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };

        ASSERT_EQ(fullPixels.size(), prunedPixels.size());
        const std::size_t differingBytes = [&]() {
            const auto* full = reinterpret_cast<const std::uint8_t*>(fullPixels.data());
            const auto* pruned = reinterpret_cast<const std::uint8_t*>(prunedPixels.data());
            std::size_t differences = 0;
            for (std::size_t byte = 0; byte < fullPixels.size() * sizeof(float); ++byte)
                if (full[byte] != pruned[byte]) ++differences;
            return differences;
        }();
        EXPECT_EQ(differingBytes, 0u);
        EXPECT_EQ(std::memcmp(fullPixels.data(), unrolledPixels.data(), fullPixels.size() * sizeof(float)), 0);
        EXPECT_EQ(hitPixels(fullPixels), hitPixels(prunedPixels));
        EXPECT_EQ(hitPixels(fullPixels), hitPixels(unrolledPixels));
        EXPECT_GT(hitPixels(fullPixels), 0u);
        for (std::size_t pixel = 0; pixel < fullPixels.size() / 4; ++pixel) {
            EXPECT_FLOAT_EQ(fullPixels[pixel * 4 + 1], 0.25f);
            EXPECT_FLOAT_EQ(fullPixels[pixel * 4 + 2], 0.5f);
            EXPECT_FLOAT_EQ(fullPixels[pixel * 4 + 3], 0.75f);
        }

        const std::uint64_t fullClauseTotal = totalClauses(fullClauses);
        const std::uint64_t prunedClauseTotal = totalClauses(prunedClauses);
        const double fullClausesPerPixel = static_cast<double>(fullClauseTotal) / (kWidth * kHeight);
        const double prunedClausesPerPixel = static_cast<double>(prunedClauseTotal) / (kWidth * kHeight);
        const double clauseReductionPercent = fullClauseTotal == 0 ? 0.0
            : 100.0 * (1.0 - static_cast<double>(prunedClauseTotal) / fullClauseTotal);
        const std::uint32_t fullHitPixels = hitPixels(fullPixels);
        const std::uint32_t prunedHitPixels = hitPixels(prunedPixels);
        EXPECT_EQ(fullHitPixels, prunedHitPixels);
        const double fullClausesPerHitPixel = static_cast<double>(clausesForHits(fullPixels, fullClauses))
            / fullHitPixels;
        const double prunedClausesPerHitPixel = static_cast<double>(clausesForHits(prunedPixels, prunedClauses))
            / prunedHitPixels;
        const std::uint64_t fullUploadBytes = fullTape.size() * sizeof(GpuRecipeTapeInstruction)
            + fullRanges.size() * sizeof(TileTapeRange);
        const std::uint64_t prunedUploadBytes = prunedTape.size() * sizeof(GpuRecipeTapeInstruction)
            + prunedRanges.size() * sizeof(TileTapeRange);
        const double fullGpuMs = median(fullGpuTimes);
        const double prunedGpuMs = median(prunedGpuTimes);
        const double unrolledGpuMs = median(unrolledGpuTimes);
        const double sourceCompiledGpuMs = median(sourceCompiledGpuTimes);
        const double compactedCompiledGpuMs = median(compactedCompiledGpuTimes);

        ASSERT_EQ(sourceCompiledPixels.size(), compactedCompiledPixels.size());
        const std::size_t compactedPixelDifferingBytes = [&]() {
            const auto* original = reinterpret_cast<const std::uint8_t*>(sourceCompiledPixels.data());
            const auto* compacted = reinterpret_cast<const std::uint8_t*>(compactedCompiledPixels.data());
            std::size_t differences = 0;
            for (std::size_t byte = 0; byte < sourceCompiledPixels.size() * sizeof(float); ++byte)
                if (original[byte] != compacted[byte]) ++differences;
            return differences;
        }();
        EXPECT_EQ(compactedPixelDifferingBytes, 0u);
        EXPECT_EQ(std::memcmp(sourceCompiledPixels.data(), compactedCompiledPixels.data(),
            sourceCompiledPixels.size() * sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(fullPixels.data(), sourceCompiledPixels.data(),
            fullPixels.size() * sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(unrolledPixels.data(), sourceCompiledPixels.data(),
            sourceCompiledPixels.size() * sizeof(float)), 0);
        for (std::size_t pixel = 0; pixel < compactedCompiledPixels.size() / 4; ++pixel) {
            EXPECT_FLOAT_EQ(compactedCompiledPixels[pixel * 4 + 1], 0.25f);
            EXPECT_FLOAT_EQ(compactedCompiledPixels[pixel * 4 + 2], 0.5f);
            EXPECT_FLOAT_EQ(compactedCompiledPixels[pixel * 4 + 3], 0.75f);
        }
        const std::uint64_t sourceCompiledUploadBytes = sourceCompiled.spirv.size()
            * sizeof(std::uint32_t) + recipeParams.size() * sizeof(float);
        const std::uint64_t compactedCompiledUploadBytes = compactedCompile.spirv.size()
            * sizeof(std::uint32_t) + recipeParams.size() * sizeof(float);
        std::cout << "[RecipeWholeDomain] fixture=" << fixture.name
            << " sourceRevision=" << wholeDomain.sourceRevision
            << " instructionsBefore=" << sourceUnrolled.size()
            << " instructionsAfter=" << compactedUnrolled.size()
            << " removedNoOps=" << wholeDomain.stats.removedIdentityNoOps
            << " removedIdempotentDuplicateInstructions="
                << wholeDomain.stats.removedIdempotentDuplicateInstructions
            << " removedByIntervalProof=" << wholeDomain.stats.removedByIntervalProof
            << " proofCpuMs=" << wholeDomain.stats.proofNanoseconds / 1.0e6
            << " sourceCompileMs=" << sourceCompiledMilliseconds
            << " compactedCompileMs=" << compactedCompileMilliseconds
            << " sourceUploadBytes=" << sourceCompiledUploadBytes
            << " compactedUploadBytes=" << compactedCompiledUploadBytes
            << " sourceGpuMedianMs=" << sourceCompiledGpuMs
            << " compactedGpuMedianMs=" << compactedCompiledGpuMs
            << " pixelDifferingBytes=" << compactedPixelDifferingBytes
            << " pixelChannels=depth+fixture-RGB-identical"
            << " domainEnforced=true channelDependencies=declared" << std::endl;

        std::vector<std::uint32_t> sortedRetainedCounts = retainedCounts;
        std::sort(sortedRetainedCounts.begin(), sortedRetainedCounts.end());
        const std::uint32_t retainedMinimum = sortedRetainedCounts.front();
        const double retainedMedian = (sortedRetainedCounts[kTileCount / 2 - 1]
            + sortedRetainedCounts[kTileCount / 2]) / 2.0;
        const std::uint32_t retainedMaximum = sortedRetainedCounts.back();
        const std::uint32_t manyTermTiles = static_cast<std::uint32_t>(std::count_if(
            retainedCounts.begin(), retainedCounts.end(), [](std::uint32_t count) { return count >= 32; }));
        std::map<std::uint32_t, std::uint32_t> retainedHistogram;
        for (const std::uint32_t count : retainedCounts)
            ++retainedHistogram[count];
        std::ostringstream histogramText;
        bool firstHistogramEntry = true;
        for (const auto& [count, tileCount] : retainedHistogram) {
            if (!firstHistogramEntry) histogramText << ',';
            histogramText << count << ':' << tileCount;
            firstHistogramEntry = false;
        }

        std::uint32_t visibleChangedPixels = 0;
        std::uint32_t visibleAddedPixels = 0;
        std::uint32_t visibleCutPixels = 0;
        for (std::size_t pixel = 0; pixel < fullPixels.size() / 4; ++pixel) {
            const float editedDepth = fullPixels[pixel * 4];
            const float baseDepth = basePixels[pixel * 4];
            const float beforeCutsDepth = beforeCutsPixels[pixel * 4];
            if ((editedDepth == -1000.0f) != (baseDepth == -1000.0f)
                || std::abs(editedDepth - baseDepth) > 1e-4f)
                ++visibleChangedPixels;
            if ((beforeCutsDepth == -1000.0f) != (baseDepth == -1000.0f)
                || std::abs(beforeCutsDepth - baseDepth) > 1e-4f)
                ++visibleAddedPixels;
            if ((editedDepth == -1000.0f) != (beforeCutsDepth == -1000.0f)
                || std::abs(editedDepth - beforeCutsDepth) > 1e-4f)
                ++visibleCutPixels;
        }
        if (std::string(fixture.name) == "run2-dead-terms") {
            EXPECT_EQ(visibleChangedPixels, 0u) << "The dead-terms fixture must match its base sphere";
            EXPECT_EQ(visibleCutPixels, 0u) << "The dead-terms fixture must not alter the base sphere";
        }

        std::cout << "[RecipeTileTape] device=" << selectedDeviceName_
            << " fixture=" << fixture.name
            << " sourceInstructions=" << source.size() << " tiles=" << kTileCount
            << " hitPixels=" << fullHitPixels << "/" << (kWidth * kHeight)
            << " visibleChangedPixelsVsBase=" << visibleChangedPixels
            << " visibleAddedPixels=" << visibleAddedPixels
            << " visibleCutPixels=" << visibleCutPixels
            << " baseRenderForVisibility=true"
            << " fullClausesPerPixel=" << fullClausesPerPixel
            << " prunedClausesPerPixel=" << prunedClausesPerPixel
            << " fullClausesPerHitPixel=" << fullClausesPerHitPixel
            << " prunedClausesPerHitPixel=" << prunedClausesPerHitPixel
            << " reductionPercent=" << clauseReductionPercent
            << " proofEvaluations=" << intervalEvaluations
            << " prunedSourceInstructions=" << prunedInstructions
            << " cpuSpecializationMs=" << specializationCpuMilliseconds
            << " summedTileSpecializationNs=" << individualSpecializationNs
            << " retainedInstructionMin=" << retainedMinimum
            << " retainedInstructionMedian=" << retainedMedian
            << " retainedInstructionMax=" << retainedMaximum
            << " retainedInstructionHistogram=" << histogramText.str()
            << " tilesRetainingAtLeast32=" << manyTermTiles
            << " fullUploadBytes=" << fullUploadBytes
            << " prunedUploadBytes=" << prunedUploadBytes
            << " fullGpuMs=" << fullGpuMs << " prunedGpuMs=" << prunedGpuMs
            << " unrolledGpuMs=" << unrolledGpuMs
            << " tapeCompileMs=" << tapeCompileMilliseconds
            << " unrolledCompileMs=" << unrolledCompileMilliseconds
            << " tapeShaderBytes=" << tapeShaderSource.size()
            << " unrolledShaderBytes=" << unrolledShaderSource.size()
            << " totalCostMs=" << specializationCpuMilliseconds + prunedGpuMs
            << " proofIncludedInCpuSpecialization=true" << std::endl;

        EXPECT_GE(clauseReductionPercent, 25.0);
        EXPECT_EQ(fullClauseTotal, totalClauses(unrolledClauses));
        EXPECT_EQ(fullClauses, unrolledClauses);
        if (fixture.capture) {
            EXPECT_GE(visibleChangedPixels, 100u);
            EXPECT_GE(visibleAddedPixels, 100u);
            EXPECT_GE(visibleCutPixels, 50u);
            EXPECT_GE(manyTermTiles, 2u);
            if (const char* captureDirectory = std::getenv("RVA1_CAPTURE_DIR")) {
                const std::filesystem::path outputPath = std::filesystem::path(captureDirectory)
                    / "visible-edit-heavy-capture-and-retained-heatmap.png";
                std::filesystem::create_directories(outputPath.parent_path());
                ASSERT_TRUE(WriteVisibleEditCapture(outputPath, fullPixels, basePixels,
                    beforeCutsPixels, kWidth, kHeight, retainedCounts))
                    << "Failed to write capture " << outputPath;
                std::cout << "[RecipeTileTape] capture=" << outputPath << std::endl;
            }
        }
    }
}
