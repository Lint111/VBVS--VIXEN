#include "VulkanGraphApplication.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/ReconstructionHistoryStateNode.h"
#include "Nodes/SceneRadianceNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Nodes/RenderTargetExtentNode.h"
#include "Data/Nodes/SceneRadianceNodeConfig.h"
#include <glm/gtc/packing.hpp>
#include "Debug/RenderTargetReadback.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Nodes/ReservoirConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Text/GlyphAtlas.h"
#include "Text/GlyphOutline.h"
#include "SdfBake.h"
#include "ShellOctreeGpu.h"

#include <gtest/gtest.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {
VulkanGraphApplication::RenderPathConfig gRenderPathConfig{};
}

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(TestIndirectIrradianceNodeConfig, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(INDIRECT_IRRADIANCE, glm::vec3, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);

    TestIndirectIrradianceNodeConfig() {
        HandleDescriptor irradianceDesc{"glm::vec3"};
        INIT_OUTPUT_DESC(INDIRECT_IRRADIANCE, "indirect_irradiance",
                         ResourceLifetime::Transient, irradianceDesc);
    }
};

class TestIndirectIrradianceNode final : public TypedNode<TestIndirectIrradianceNodeConfig> {
public:
    TestIndirectIrradianceNode(const std::string& name, NodeType* nodeType)
        : TypedNode(name, nodeType) {}

protected:
    void TypedExecuteImpl(TypedExecuteContext& ctx) override {
        ctx.Out(TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE,
                glm::vec3(2.0f, 0.18f, 0.04f));
    }
};

class TestIndirectIrradianceNodeType final
    : public TypedNodeType<TestIndirectIrradianceNodeConfig> {
public:
    TestIndirectIrradianceNodeType() : TypedNodeType("TestIndirectIrradiance") {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<TestIndirectIrradianceNode>(
            name, const_cast<TestIndirectIrradianceNodeType*>(this));
    }
};

CONSTEXPR_NODE_CONFIG(TestIndirectLightingPorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(INDIRECT_LIGHTING, glm::vec3, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);

    TestIndirectLightingPorts() {
        HandleDescriptor irradianceDesc{"glm::vec3"};
        INIT_OUTPUT_DESC(INDIRECT_LIGHTING, "moduleIndirectIrradiance",
                         ResourceLifetime::Transient, irradianceDesc);
    }
};

class TestIndirectLightingModule final : public SubGraphType<TestIndirectLightingModule> {
public:
    static constexpr std::string_view TYPE_NAME = "TestIndirectLightingModule";
    static constexpr std::string_view CAPABILITY_NAME = "Test:IndirectLighting";
    struct Params {};
    using PortConfig = TestIndirectLightingPorts;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle source = scope.AddNode<TestIndirectIrradianceNodeType>("irradiance");
        scope.BindOutput(TestIndirectLightingPorts::INDIRECT_LIGHTING, source,
                         TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE);
    }
};

class LightingConfigNodeTestAccess {
public:
    static uint32_t FrameCount(const LightingConfigNode& node) {
        return node.perFrame_.GetFrameCount();
    }

    static bool ReadMappedBytes(const LightingConfigNode& node, uint32_t frameIndex,
                                std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)>& bytes) {
        if (frameIndex >= node.perFrame_.GetFrameCount()) return false;
        const void* mapped = node.perFrame_.GetUniformBufferMapped(frameIndex);
        if (!mapped) return false;
        std::memcpy(bytes.data(), mapped, bytes.size());
        return true;
    }
};

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::TestIndirectIrradianceNodeType);
VIXEN_REGISTER_NODE(Vixen::RenderGraph::TestIndirectLightingModule);

namespace {

// Frames per angle are derived, not chosen: every per-frame ring (LightingConfig, camera,
// previous camera) holds kRingSlotCount slots and each must be rewritten for the new angle, so
// two full rings give a warmed, verified steady state. The reservoir path carries
// temporalCap=32 samples of history; when it is enabled the capture needs two full caps (64).
constexpr uint32_t kRingSlotCount = Vixen::RenderGraph::FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
constexpr uint32_t kReservoirHistoryCap = 32;
constexpr uint32_t kMaxFramesPerScenario = 2 * kReservoirHistoryCap;
constexpr uint32_t kDefaultFramesPerScenario = 2 * kRingSlotCount;
constexpr double kDefaultProcessBudgetMs = 300000.0; // Cold-driver dzn compile is ~100 s; see reports/capturespeed.md.
constexpr float kPitchRadians = 0.45f;
constexpr uint32_t kGlyphSdfRecipeId = 0x47594c50u; // GLYP
constexpr uint32_t kSdfFilterNonTextRecipeId = 0x5344464eu; // SDFN

using CaptureClock = std::chrono::steady_clock;

double MsBetween(CaptureClock::time_point from, CaptureClock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

double Percentile(std::vector<double> samples, double fraction) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<size_t>(fraction * static_cast<double>(samples.size() - 1) + 0.5);
    return samples[std::min(index, samples.size() - 1)];
}

uint32_t DefaultFramesPerScenario() {
    return Vixen::RenderGraph::ResolveReservoirEnabled() ? kMaxFramesPerScenario
                                                          : kDefaultFramesPerScenario;
}

uint32_t CaptureFramesPerScenario(std::string& error) {
    const char* requested = std::getenv("VIXEN_LOOKDEV_CAPTURE_FRAMES");
    if (!requested || *requested == '\0') return DefaultFramesPerScenario();

    char* end = nullptr;
    const unsigned long value = std::strtoul(requested, &end, 10);
    if (end == requested || *end != '\0' || value == 0 || value > kMaxFramesPerScenario) {
        error = "VIXEN_LOOKDEV_CAPTURE_FRAMES must be an integer from 1 through " +
            std::to_string(kMaxFramesPerScenario);
        return 0;
    }
    return static_cast<uint32_t>(value);
}

int CenteredBakeRegionSide(int gridResolution, float boundRadius,
                           float bandRecipeUnits, float recipeUnitsPerVoxel) {
    const float halfExtent = boundRadius + bandRecipeUnits +
        8.0f * recipeUnitsPerVoxel;
    const int requiredSide = static_cast<int>(std::ceil(
        2.0f * halfExtent / recipeUnitsPerVoxel));
    const int brickAlignedSide = ((requiredSide + 15) / 16) * 16;
    return std::clamp(brickAlignedSide, 16, gridResolution);
}

double ProcessBudgetMs() {
    const char* requested = std::getenv("VIXEN_LOOKDEV_CAPTURE_BUDGET_MS");
    if (!requested || *requested == '\0') return kDefaultProcessBudgetMs;
    char* end = nullptr;
    const double value = std::strtod(requested, &end);
    return (end == requested || *end != '\0' || value <= 0.0) ? kDefaultProcessBudgetMs : value;
}

struct CaptureScenario {
    Vixen::App::Lookdev::LightingPreset lighting;
    float yawRadians = 0.0f;
    std::string filename;
};

std::vector<CaptureScenario> BuildScenarios() {
    constexpr std::array<float, 4> yaws{{0.0f, 1.57079632679f, 3.14159265359f, 4.71238898038f}};
    const auto& presets = Vixen::App::Lookdev::LightingPresets();
    const char* requestedState = std::getenv("VIXEN_LOOKDEV_CAPTURE_STATE");
    const char* requestedAngleText = std::getenv("VIXEN_LOOKDEV_CAPTURE_ANGLE");
    float storedSdfYawRadians = 0.0f;
    if (const char* yawText = std::getenv("VIXEN_GLYPHSDF_CAPTURE_YAW_DEGREES")) {
        char* end = nullptr;
        const float yawDegrees = std::strtof(yawText, &end);
        if (end != yawText && *end == '\0' && std::isfinite(yawDegrees) &&
            yawDegrees >= -180.0f && yawDegrees <= 180.0f) {
            storedSdfYawRadians = yawDegrees * 0.017453292519943295f;
        }
    }
    int requestedAngle = -1;
    if (requestedAngleText && *requestedAngleText != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(requestedAngleText, &end, 10);
        if (end != requestedAngleText && *end == '\0' && parsed >= 0 && parsed < 4) {
            requestedAngle = static_cast<int>(parsed);
        }
    }
    std::vector<CaptureScenario> scenarios;
    if (std::getenv("VIXEN_GLYPHSDF_SCENE") || std::getenv("VIXEN_SDF_FILTER_NON_TEXT")) {
        const char* variant = std::getenv("VIXEN_GLYPHSDF_VARIANT");
        const bool compare = std::getenv("VIXEN_GLYPHSDF_COMPARE") != nullptr ||
            std::getenv("VIXEN_SDF_FILTER_NON_TEXT") != nullptr;
        const auto preset = std::find_if(presets.begin(), presets.end(), [](const auto& value) {
            return value.name == "midday";
        });
        if (preset != presets.end())
            scenarios.push_back({*preset, storedSdfYawRadians, compare
                ? (std::getenv("VIXEN_SDF_FILTER_NON_TEXT")
                    ? "sdf-filter-nontext-analytic-vs-voxel.png"
                    : "glyphsdf-analytic-vs-voxel.png")
                : std::string("glyphsdf-") + (variant ? variant : "latin-carved") + ".png"});
        return scenarios;
    }

    const bool slowPan = std::getenv("VIXEN_LOOKDEV_CAPTURE_PAN") != nullptr;
    constexpr size_t kPanAngles = 24;
    const size_t angleCount = slowPan ? kPanAngles : yaws.size();
    scenarios.reserve(presets.size() * angleCount);
    for (const auto& preset : presets) {
        if (requestedState && preset.name != std::string(requestedState)) continue;
        for (size_t angle = 0; angle < angleCount; ++angle) {
            if (!slowPan && requestedAngle >= 0 && static_cast<int>(angle) != requestedAngle)
                continue;
            const float yaw = slowPan
                ? 6.28318530718f * static_cast<float>(angle) / static_cast<float>(kPanAngles)
                : yaws[angle];
            std::ostringstream filename;
            filename << preset.name << (slowPan ? "-pan-" : "-angle-")
                     << std::setfill('0') << std::setw(slowPan ? 2 : 0) << angle << ".png";
            scenarios.push_back({preset, yaw, filename.str()});
        }
    }
    return scenarios;
}

struct GlyphSdfSceneBundle {
    Vixen::SVO::ConcatenatedOctrees pool;
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    std::vector<std::pair<uint32_t, Vixen::SVO::RecipeRegistry::RecipeEntry>> proceduralRecipes;
};

struct GlyphSdfRecipe {
    std::string label;
    std::vector<Vixen::SVO::Recipe::SdfInstruction> instructions;
    glm::vec3 bodyCenter{0.0f};
    glm::vec3 color{1.0f};
    bool carved = false;
};

bool BuildGlyphSdfScene(GlyphSdfSceneBundle& scene, const std::filesystem::path& evidenceDir,
                        bool compareAnalyticAndVoxel, std::string& error) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;
    using Vixen::Text::GlyphOutlineInstance;
    using Vixen::Text::PositionedGlyphOutline;
    using Vixen::Text::WorldTextLayout;

    const bool rtlProof = std::getenv("VIXEN_GLYPHSDF_RTL_PROOF") != nullptr;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    const char* fontPath = rtlProof ? TEXT_NOTO_ARABIC_FONT_PATH : TEXT_LATO_FONT_PATH;
    if (FT_Init_FreeType(&library) != 0 ||
        FT_New_Face(library, fontPath, 0, &face) != 0 ||
        FT_Set_Char_Size(face, 0, 24 * 64, 0, 0) != 0) {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
        error = "could not initialize the bundled Lato face for GlyphOutline scene capture";
        return false;
    }

    Vixen::Text::GlyphAtlas glyphAtlas;
    constexpr std::array<std::uint32_t, 2> requiredScriptTags{{
        (static_cast<std::uint32_t>('L') << 24U) | (static_cast<std::uint32_t>('a') << 16U) |
        (static_cast<std::uint32_t>('t') << 8U) | static_cast<std::uint32_t>('n'),
        (static_cast<std::uint32_t>('A') << 24U) | (static_cast<std::uint32_t>('r') << 16U) |
        (static_cast<std::uint32_t>('a') << 8U) | static_cast<std::uint32_t>('b')}};
    const std::span<const std::uint32_t> requiredScripts(
        requiredScriptTags.data(), rtlProof ? requiredScriptTags.size() : 1U);
    const auto makePreset = [rtlProof](Vixen::Text::TextDirection direction) {
        Vixen::Text::PresetDesc preset{};
        preset.direction = direction;
        preset.shapingClass = Vixen::Text::ShapingClass::Default;
        preset.lineBreakingClass = Vixen::Text::LineBreakingClass::Word;
        preset.requiredScriptOrdinalCount = rtlProof ? 2 : 1;
        return preset;
    };

    constexpr std::string_view rtlProofText = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 ABC";
    const std::string_view latinText = rtlProof ? rtlProofText : "OPEN";
    const std::string_view rtlText = rtlProof ? rtlProofText : "NEPO";
    std::array<WorldTextLayout, 2> layouts{
        Vixen::Text::ShapeWorldText(makePreset(Vixen::Text::TextDirection::LeftToRight),
                                    requiredScripts, glyphAtlas, face, latinText, "en"),
        Vixen::Text::ShapeWorldText(makePreset(Vixen::Text::TextDirection::RightToLeft),
                                    requiredScripts, glyphAtlas, face, rtlText, "qps-rtl")};
    if (!layouts[0].valid || !layouts[1].valid || layouts[0].glyphs.empty() || layouts[1].glyphs.empty()) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        error = "HarfBuzz did not produce positioned Latin and qps-rtl glyph runs";
        return false;
    }
    if (rtlProof) {
        const auto hasRtlRun = [](const WorldTextLayout& layout) {
            return std::any_of(layout.glyphs.begin(), layout.glyphs.end(),
                               [](const PositionedGlyphOutline& glyph) { return glyph.rightToLeft; });
        };
        const auto clustersInVisualOrder = [](const WorldTextLayout& layout) {
            std::vector<std::uint32_t> clusters;
            clusters.reserve(layout.glyphs.size());
            for (const PositionedGlyphOutline& glyph : layout.glyphs)
                clusters.push_back(glyph.cluster);
            return clusters;
        };
        if (layouts[1].direction != Vixen::Text::TextDirection::RightToLeft ||
            !layouts[1].mirrorX || !hasRtlRun(layouts[1]) ||
            clustersInVisualOrder(layouts[0]) == clustersInVisualOrder(layouts[1])) {
            std::ostringstream diagnostic;
            diagnostic << "the qps-rtl proof did not produce a mirrored RTL run with a distinct cluster order"
                       << " (direction="
                       << (layouts[1].direction == Vixen::Text::TextDirection::RightToLeft ? "RTL" : "LTR")
                       << ", mirror=" << (layouts[1].mirrorX ? "true" : "false")
                       << ", rtl_run=" << (hasRtlRun(layouts[1]) ? "true" : "false")
                       << ", clusters_differ="
                       << (clustersInVisualOrder(layouts[0]) != clustersInVisualOrder(layouts[1])
                               ? "true" : "false") << ")\n"
                       << "LTR glyphs:";
            for (const PositionedGlyphOutline& glyph : layouts[0].glyphs)
                diagnostic << ' ' << glyph.glyphId << '@' << glyph.cluster
                           << (glyph.rightToLeft ? 'R' : 'L');
            diagnostic << "\nRTL glyphs:";
            for (const PositionedGlyphOutline& glyph : layouts[1].glyphs)
                diagnostic << ' ' << glyph.glyphId << '@' << glyph.cluster
                           << (glyph.rightToLeft ? 'R' : 'L');
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            error = diagnostic.str();
            return false;
        }
    }

    struct Variant {
        const char* name;
        std::size_t layoutIndex;
        bool carved;
    };
    constexpr std::array<Variant, 4> variants{{
        {"latin-carved", 0, true}, {"latin-embossed", 0, false},
        {"qps-rtl-carved", 1, true}, {"qps-rtl-embossed", 1, false}}};
    constexpr std::array<Variant, 1> rtlProofVariants{{
        {"rtl-proof-pair", 0, true}}};
    const char* requestedVariant = std::getenv("VIXEN_GLYPHSDF_VARIANT");
    const std::span<const Variant> selectedVariants = rtlProof
        ? std::span<const Variant>(rtlProofVariants)
        : std::span<const Variant>(variants);
    const auto selected = requestedVariant
        ? std::find_if(selectedVariants.begin(), selectedVariants.end(), [&](const Variant& value) {
              return value.name == std::string(requestedVariant);
          })
        : selectedVariants.begin();
    if (selected == selectedVariants.end()) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        error = rtlProof
            ? "VIXEN_GLYPHSDF_VARIANT must select rtl-proof-pair"
            : "VIXEN_GLYPHSDF_VARIANT must select latin-carved, latin-embossed, "
              "qps-rtl-carved, or qps-rtl-embossed";
        return false;
    }
    if (compareAnalyticAndVoxel && selected->name != std::string("latin-carved")) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        error = "the analytic-vs-voxel frame is authored for the Latin carved sign";
        return false;
    }

    if (rtlProof) {
        std::ofstream shaping(evidenceDir / "glyphsdf-shaping.txt", std::ios::out | std::ios::trunc);
        if (!shaping) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            error = "could not write the GlyphOutline shaping evidence";
            return false;
        }
        shaping << "font=" << fontPath << '\n'
                << "input_utf8=" << rtlProofText << '\n'
                << "same_literal_for_both_presets=true\n"
                << "capture=side-by-side\n"
                << "screen_assignment=left:ltr(en),right:qps-rtl\n\n";
        const std::array<const char*, 2> labels{{"ltr", "qps-rtl"}};
        const std::array<const char*, 2> languages{{"en", "qps-rtl"}};
        for (std::size_t layoutIndex = 0; layoutIndex < layouts.size(); ++layoutIndex) {
            const WorldTextLayout& layout = layouts[layoutIndex];
            shaping << '[' << labels[layoutIndex] << "]\n"
                    << "language=" << languages[layoutIndex] << '\n'
                    << "direction=" << (layout.direction == Vixen::Text::TextDirection::RightToLeft
                                               ? "RightToLeft" : "LeftToRight") << '\n'
                    << "mirror_x=" << (layout.mirrorX ? "true" : "false") << '\n'
                    << "required_script_table_matches_preset="
                    << (layout.requiredScriptTableMatchesPreset ? "true" : "false") << '\n'
                    << "glyphs_in_visual_run_order=\n";
            for (const PositionedGlyphOutline& glyph : layout.glyphs) {
                shaping << "  glyph_id=" << glyph.glyphId
                        << " cluster=" << glyph.cluster
                        << " x=" << glyph.x
                        << " y=" << glyph.y
                        << " advance_x=" << glyph.xAdvance
                        << " rtl_run=" << (glyph.rightToLeft ? "true" : "false") << '\n';
            }
            shaping << '\n';
        }
    }

    const float kGridUnitsPerPixel = rtlProof ? 0.22f : 0.30f;
    const float kOutlineScale = kGridUnitsPerPixel / 64.0f;
    std::vector<GlyphOutlineInstance> glyphRows;
    std::unordered_map<const GlyphOutlineInstance*, std::uint32_t> glyphRowsByIdentity;
    std::array<std::vector<std::uint32_t>, 2> layoutRows;
    auto rowFor = [&](const PositionedGlyphOutline& glyph, std::uint32_t& row) {
        if (!glyph.instance) return false;
        if (const auto found = glyphRowsByIdentity.find(glyph.instance.get());
            found != glyphRowsByIdentity.end()) {
            row = found->second;
            return true;
        }
        GlyphOutlineInstance normalized = *glyph.instance;
        const float centerX = (normalized.BoundsMin.X + normalized.BoundsMax.X) * 0.5f;
        const float centerY = (normalized.BoundsMin.Y + normalized.BoundsMax.Y) * 0.5f;
        for (std::uint32_t segmentIndex = 0; segmentIndex < normalized.SegmentCount; ++segmentIndex) {
            auto& segment = normalized.Segments[segmentIndex];
            for (Vixen::Text::GlyphOutlinePoint* point :
                 {&segment.P0, &segment.P1, &segment.P2, &segment.P3}) {
                point->X = (point->X - centerX) * kOutlineScale;
                point->Y = (point->Y - centerY) * kOutlineScale;
            }
        }
        normalized.BoundsMin.X = (normalized.BoundsMin.X - centerX) * kOutlineScale;
        normalized.BoundsMin.Y = (normalized.BoundsMin.Y - centerY) * kOutlineScale;
        normalized.BoundsMax.X = (normalized.BoundsMax.X - centerX) * kOutlineScale;
        normalized.BoundsMax.Y = (normalized.BoundsMax.Y - centerY) * kOutlineScale;
        row = static_cast<std::uint32_t>(glyphRows.size());
        glyphRows.push_back(std::move(normalized));
        glyphRowsByIdentity.emplace(glyph.instance.get(), row);
        return true;
    };

    const bool rtlProofPair = rtlProof && selected->name == std::string("rtl-proof-pair");
    for (std::size_t layoutIndex = 0; layoutIndex < layouts.size(); ++layoutIndex) {
        if (!rtlProofPair && layoutIndex != selected->layoutIndex) continue;
        for (const PositionedGlyphOutline& glyph : layouts[layoutIndex].glyphs) {
            std::uint32_t row = 0;
            if (!rowFor(glyph, row)) {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                error = "positioned text glyph did not carry its typed GlyphOutline bucket";
                return false;
            }
            layoutRows[layoutIndex].push_back(row);
        }
    }

    auto instruction = [](SdfOpCode opCode) {
        SdfInstruction result{};
        result.opCode = static_cast<std::uint8_t>(opCode);
        return result;
    };
    struct LayoutPlacement {
        std::size_t layoutIndex;
        float offsetX;
    };
    auto makeRecipe = [&](std::span<const LayoutPlacement> placements, bool carved,
                          glm::vec3 bodyCenter, glm::vec3 color, std::string label) {
        GlyphSdfRecipe result;
        result.label = std::move(label);
        result.bodyCenter = bodyCenter;
        result.color = color;
        result.carved = carved;
        const float bevel = 0.12f;
        const float halfDepth = carved ? 3.0f : 0.95f;
        const float textDepth = carved ? 0.0f : 2.25f;

        for (std::size_t placementIndex = 0; placementIndex < placements.size(); ++placementIndex) {
            const LayoutPlacement placement = placements[placementIndex];
            const WorldTextLayout& layout = layouts[placement.layoutIndex];
            if (placements.size() > 1) {
                SdfInstruction panelTransform = instruction(SdfOpCode::Transform);
                panelTransform.data[0] = placement.offsetX;
                panelTransform.data[7] = 1.0f;
                panelTransform.data[8] = 1.0f;
                panelTransform.data[9] = 1.0f;
                panelTransform.data[10] = 1.0f;
                panelTransform.data[11] = 1.0f;
                result.instructions.push_back(panelTransform);
            }
            SdfInstruction plaque = instruction(SdfOpCode::BoxRounded);
            plaque.data[0] = 14.0f;
            plaque.data[1] = 10.0f;
            plaque.data[2] = 1.8f;
            plaque.data[3] = 0.9f;
            result.instructions.push_back(plaque);
            if (placements.size() > 1) {
                result.instructions.push_back(instruction(SdfOpCode::RestorePos));
                if (placementIndex > 0)
                    result.instructions.push_back(instruction(SdfOpCode::Union));
            }

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            for (const PositionedGlyphOutline& glyph : layout.glyphs) {
                minX = std::min(minX, static_cast<float>(glyph.x) + glyph.instance->BoundsMin.X);
                minY = std::min(minY, static_cast<float>(glyph.y) + glyph.instance->BoundsMin.Y);
                maxX = std::max(maxX, static_cast<float>(glyph.x) + glyph.instance->BoundsMax.X);
                maxY = std::max(maxY, static_cast<float>(glyph.y) + glyph.instance->BoundsMax.Y);
            }
            const float textCenterX = (minX + maxX) * 0.5f;
            const float textCenterY = (minY + maxY) * 0.5f;

            for (std::size_t glyphIndex = 0; glyphIndex < layout.glyphs.size(); ++glyphIndex) {
                const PositionedGlyphOutline& glyph = layout.glyphs[glyphIndex];
                const float glyphCenterX =
                    (glyph.instance->BoundsMin.X + glyph.instance->BoundsMax.X) * 0.5f;
                const float glyphCenterY =
                    (glyph.instance->BoundsMin.Y + glyph.instance->BoundsMax.Y) * 0.5f;
                const float rawX = static_cast<float>(glyph.x) + glyphCenterX - textCenterX;
                const float rawY = static_cast<float>(glyph.y) + glyphCenterY - textCenterY;
                SdfInstruction transform = instruction(SdfOpCode::Transform);
                transform.data[0] = placement.offsetX +
                    (layout.mirrorX ? -rawX : rawX) * kOutlineScale;
                transform.data[1] = rawY * kOutlineScale;
                transform.data[2] = textDepth;
                transform.data[4] = 0.0f;
                transform.data[5] = 0.0f;
                transform.data[6] = 0.0f;
                transform.data[7] = 1.0f;
                transform.data[8] = 1.0f;
                transform.data[9] = 1.0f;
                transform.data[10] = 1.0f;
                transform.data[11] = 1.0f;
                result.instructions.push_back(transform);

                SdfInstruction glyphOp = instruction(SdfOpCode::GlyphOutline);
                glyphOp.data[0] = std::bit_cast<float>(layoutRows[placement.layoutIndex][glyphIndex]);
                result.instructions.push_back(glyphOp);

                SdfInstruction extrusion = instruction(SdfOpCode::Box);
                extrusion.data[0] = (glyph.instance->BoundsMax.X - glyph.instance->BoundsMin.X) *
                    kOutlineScale * 0.5f + bevel;
                extrusion.data[1] = (glyph.instance->BoundsMax.Y - glyph.instance->BoundsMin.Y) *
                    kOutlineScale * 0.5f + bevel;
                extrusion.data[2] = halfDepth;
                result.instructions.push_back(extrusion);
                result.instructions.push_back(instruction(SdfOpCode::Intersect));

                SdfInstruction round = instruction(SdfOpCode::Round);
                round.data[0] = 0.035f;
                result.instructions.push_back(round);
                result.instructions.push_back(instruction(SdfOpCode::RestorePos));
                result.instructions.push_back(instruction(carved ? SdfOpCode::Subtract : SdfOpCode::Union));
            }
        }
        return result;
    };

    const glm::vec3 hullColor = Vixen::App::Lookdev::MaterialColor(
        Vixen::App::Lookdev::Material::Hull);
    std::vector<std::pair<uint32_t, GlyphSdfRecipe>> recipes;
    if (rtlProofPair) {
        constexpr float sideBySideOffset = 14.5f;
        constexpr std::array<LayoutPlacement, 2> proofLayouts{{
            {0, -sideBySideOffset}, {1, sideBySideOffset}}};
        recipes.emplace_back(kGlyphSdfRecipeId,
            makeRecipe(proofLayouts, true, {0.0f, 0.0f, 5.0f}, hullColor, "rtl-proof-pair"));
    } else {
        const std::array<LayoutPlacement, 1> selectedLayout{{
            {selected->layoutIndex, 0.0f}}};
        recipes.emplace_back(kGlyphSdfRecipeId,
            makeRecipe(selectedLayout, selected->carved,
                       {0.0f, 0.0f, 5.0f}, hullColor, selected->name));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    for (const auto& [recipeId, recipe] : recipes) {
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        entry.bytecode = recipe.instructions;
        entry.glyphInstances = glyphRows;
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = 18.0f;
        entry.stepRelaxation = 0.9f;
        scene.proceduralRecipes.emplace_back(recipeId, std::move(entry));
    }

    auto makeAnalyticInstance = [&](const glm::vec3& center, uint32_t recipeId,
                                    const glm::vec3& color) {
        Vixen::SVO::BodyInstanceGpu instance{};
        constexpr float instanceScale = 0.30f;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), center) *
            glm::scale(glm::mat4(1.0f), glm::vec3(instanceScale));
        Vixen::SVO::SetInstanceTransform(instance, model);
        instance.material.color[0] = color.r;
        instance.material.color[1] = color.g;
        instance.material.color[2] = color.b;
        instance.material.providerKind = 1U;
        instance.material.recipeId = recipeId;
        return instance;
    };

    for (const auto& [recipeId, recipe] : recipes) {
        const glm::vec3 center = compareAnalyticAndVoxel
            ? glm::vec3(-5.25f, 0.0f, 5.0f) : recipe.bodyCenter;
        scene.instances.push_back(makeAnalyticInstance(center, recipeId, recipe.color));
    }
    if (!compareAnalyticAndVoxel) return true;

    const GlyphSdfRecipe& recipe = recipes.front().second;

    int bakeResolution = 256;
    if (const char* requestedResolution = std::getenv("VIXEN_GLYPHSDF_BAKE_RESOLUTION")) {
        char* parseEnd = nullptr;
        const long parsedResolution = std::strtol(requestedResolution, &parseEnd, 10);
        if (parseEnd == requestedResolution || *parseEnd != '\0' ||
            (parsedResolution != 128 && parsedResolution != 256)) {
            error = "VIXEN_GLYPHSDF_BAKE_RESOLUTION must be 128 or 256";
            return false;
        }
        bakeResolution = static_cast<int>(parsedResolution);
    }
    // Keep the same 64-unit world-space grid at both resolutions, but bake only
    // a centered, brick-aligned region around the sign. The additional eight
    // voxel half-margin preserves the baker's one-brick allocation halo.
    const float recipeUnitsPerVoxel = 64.0f / static_cast<float>(bakeResolution);
    constexpr float bandRecipeUnits = 2.5f;
    const float bakeBandDistance = bandRecipeUnits;
    const int bakeRegionSide = CenteredBakeRegionSide(
        bakeResolution, 18.0f, bakeBandDistance, recipeUnitsPerVoxel);
    const float bakeCenterCoordinate = static_cast<float>(bakeRegionSide) * 0.5f;
    const glm::vec3 bakeCenter{bakeCenterCoordinate};
    std::ofstream comparison(evidenceDir / "glyphsdf-voxel-vs-analytic.txt", std::ios::out | std::ios::trunc);
    if (!comparison) {
        error = "could not write the GlyphOutline bake comparison evidence";
        return false;
    }
    comparison << "variant bake_resolution recipe_units_per_voxel band_recipe_units sample_width sample_height "
                  "nearest_changed_samples trilinear_changed_samples nearest_max_delta "
                  "trilinear_max_delta trilinear_max_delta_with_band_clamp unallocated_samples\n";
    constexpr int imageWidth = 256;
    constexpr int imageHeight = 160;
    auto evaluate = [&](const glm::vec3& gridPoint) {
        return Vixen::SVO::Recipe::evalRecipe(
            recipe.instructions.data(), static_cast<std::uint32_t>(recipe.instructions.size()),
            (gridPoint - bakeCenter) * recipeUnitsPerVoxel, {}, nullptr, nullptr, glyphRows);
    };
    const auto neutralVoxelColor = [](const glm::vec3&) { return glm::vec3(1.0f); };
    auto baked = Vixen::SVO::BakeSdfWorld(evaluate, bakeCenter, bakeResolution, bakeBandDistance, 3,
                                           Vixen::SVO::NoEmission, neutralVoxelColor,
                                           glm::ivec3(bakeRegionSide));
    float nearestMaxDelta = 0.0f;
    float trilinearMaxDelta = 0.0f;
    float trilinearMaxDeltaWithBandClamp = 0.0f;
    std::uint32_t nearestChangedSamples = 0;
    std::uint32_t trilinearChangedSamples = 0;
    std::uint32_t unallocatedSamples = 0;
    const float sampleZ = recipe.carved ? 0.0f : 2.35f;
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            const glm::vec3 localPoint(
                -15.0f + (static_cast<float>(x) + 0.5f) * 30.0f / imageWidth,
                -10.0f + (static_cast<float>(y) + 0.5f) * 20.0f / imageHeight,
                sampleZ);
            const float analytic = Vixen::SVO::Recipe::evalRecipe(
                recipe.instructions.data(), static_cast<std::uint32_t>(recipe.instructions.size()),
                localPoint, {}, nullptr, nullptr, glyphRows);
            const glm::vec3 gridPoint = bakeCenter + localPoint / recipeUnitsPerVoxel;
            const auto nearest = baked.sampleStored(gridPoint);
            const auto trilinear = baked.sampleStoredTrilinear(gridPoint);
            const float voxel = trilinear.value_or(std::max(bandRecipeUnits + 1.0f,
                                                            analytic));
            if (nearest) {
                nearestMaxDelta = std::max(nearestMaxDelta, std::abs(analytic - *nearest));
                if ((analytic < 0.0f) != (*nearest < 0.0f)) ++nearestChangedSamples;
            } else {
                ++unallocatedSamples;
            }
            if (trilinear)
                trilinearMaxDelta = std::max(trilinearMaxDelta, std::abs(analytic - *trilinear));
            trilinearMaxDeltaWithBandClamp = std::max(
                trilinearMaxDeltaWithBandClamp, std::abs(analytic - voxel));
            if ((analytic < 0.0f) != (voxel < 0.0f)) ++trilinearChangedSamples;
        }
    }
    comparison << recipe.label << ' ' << bakeResolution << ' ' << recipeUnitsPerVoxel << ' '
               << bandRecipeUnits << ' '
               << imageWidth << ' ' << imageHeight
               << ' ' << nearestChangedSamples << ' ' << trilinearChangedSamples << ' '
               << nearestMaxDelta << ' ' << trilinearMaxDelta << ' '
               << trilinearMaxDeltaWithBandClamp
               << ' ' << unallocatedSamples << '\n';

    auto bakedBody = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    const std::vector<const Vixen::SVO::SdfBodyOctree*> bodyPointers{&bakedBody};
    scene.pool = Vixen::SVO::ConcatenateSdf(bodyPointers);
    Vixen::SVO::BodyInstanceGpu storedInstance{};
    // Stored octrees render in the existing normalized [0,1]^3 -> [0,10]^3
    // body frame. Undo that normalization here so the baked glyph has the same
    // world-space scale as the analytic recipe instance above.
    const float instanceScale = 0.30f * static_cast<float>(bakeResolution) *
        recipeUnitsPerVoxel / 10.0f;
    const glm::vec3 storedCenter{5.25f, 0.0f, 5.0f};
    const glm::vec3 storedOrigin = storedCenter - glm::vec3(
        10.0f * bakeCenterCoordinate / static_cast<float>(bakeResolution) * instanceScale);
    const glm::mat4 storedModel = glm::translate(glm::mat4(1.0f), storedOrigin) *
        glm::scale(glm::mat4(1.0f), glm::vec3(instanceScale));
    Vixen::SVO::SetInstanceTransform(storedInstance, storedModel);
    storedInstance.material.color[0] = recipe.color.r;
    storedInstance.material.color[1] = recipe.color.g;
    storedInstance.material.color[2] = recipe.color.b;
    storedInstance.material.octreeIndex = 0U;
    storedInstance.material.providerKind = 0U;
    scene.instances.push_back(storedInstance);
    return true;
}

bool BuildSdfFilterNonTextScene(GlyphSdfSceneBundle& scene, std::string& error) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    int bakeResolution = 128;
    if (const char* requestedResolution = std::getenv("VIXEN_SDF_FILTER_NON_TEXT_RESOLUTION")) {
        char* parseEnd = nullptr;
        const long parsedResolution = std::strtol(requestedResolution, &parseEnd, 10);
        if (parseEnd == requestedResolution || *parseEnd != '\0' ||
            (parsedResolution != 128 && parsedResolution != 256)) {
            error = "VIXEN_SDF_FILTER_NON_TEXT_RESOLUTION must be 128 or 256";
            return false;
        }
        bakeResolution = static_cast<int>(parsedResolution);
    }
    const float recipeUnitsPerVoxel = 64.0f / static_cast<float>(bakeResolution);
    constexpr float bandRecipeUnits = 2.5f;
    float bakeBandDistance = bandRecipeUnits;
    if (std::getenv("VIXEN_SDF_FILTER_NON_TEXT_EXTRA_APRON")) {
        // Allocate one additional 8-voxel exterior brick while keeping every
        // stored distance value unchanged in the original active region.
        bakeBandDistance += 8.0f * recipeUnitsPerVoxel;
    }
    constexpr float sphereRadius = 16.0f;
    constexpr float instanceScale = 0.30f;
    const int bakeRegionSide = CenteredBakeRegionSide(
        bakeResolution, sphereRadius, bakeBandDistance, recipeUnitsPerVoxel);
    const float bakeCenterCoordinate = static_cast<float>(bakeRegionSide) * 0.5f;
    const glm::vec3 bakeCenter(bakeCenterCoordinate);

    SdfInstruction sphere{};
    sphere.opCode = static_cast<std::uint8_t>(SdfOpCode::Sphere);
    sphere.data[3] = sphereRadius;
    const std::vector<SdfInstruction> instructions{sphere};

    Vixen::SVO::RecipeRegistry::RecipeEntry recipe{};
    recipe.bytecode = instructions;
    recipe.boundCenter = glm::vec3(0.0f);
    recipe.boundRadius = sphereRadius;
    recipe.stepRelaxation = 0.9f;
    scene.proceduralRecipes.emplace_back(kSdfFilterNonTextRecipeId, std::move(recipe));

    auto evaluate = [&](const glm::vec3& gridPoint) {
        return Vixen::SVO::Recipe::evalRecipe(
            instructions.data(), static_cast<std::uint32_t>(instructions.size()),
            (gridPoint - bakeCenter) * recipeUnitsPerVoxel);
    };
    const auto flatVoxelColor = [](const glm::vec3&) { return glm::vec3(1.0f); };
    auto baked = Vixen::SVO::BakeSdfWorld(evaluate, bakeCenter, bakeResolution, bakeBandDistance, 3,
                                           Vixen::SVO::NoEmission, flatVoxelColor,
                                           glm::ivec3(bakeRegionSide));
    auto bakedBody = Vixen::SVO::BuildSdfBodyOctree(baked, 3);
    const std::vector<const Vixen::SVO::SdfBodyOctree*> bodyPointers{&bakedBody};
    scene.pool = Vixen::SVO::ConcatenateSdf(bodyPointers);

    const glm::vec3 hullColor = Vixen::App::Lookdev::MaterialColor(
        Vixen::App::Lookdev::Material::Hull);
    auto makeInstance = [&](const glm::vec3& center) {
        Vixen::SVO::BodyInstanceGpu instance{};
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), center) *
            glm::scale(glm::mat4(1.0f), glm::vec3(instanceScale));
        Vixen::SVO::SetInstanceTransform(instance, model);
        instance.material.color[0] = hullColor.r;
        instance.material.color[1] = hullColor.g;
        instance.material.color[2] = hullColor.b;
        return instance;
    };

    auto analytic = makeInstance(glm::vec3(-5.25f, 0.0f, 5.0f));
    analytic.material.providerKind = 1U;
    analytic.material.recipeId = kSdfFilterNonTextRecipeId;
    scene.instances.push_back(analytic);

    Vixen::SVO::BodyInstanceGpu stored{};
    const float storedScale = instanceScale * static_cast<float>(bakeResolution) *
        recipeUnitsPerVoxel / 10.0f;
    const glm::vec3 storedCenter{5.25f, 0.0f, 5.0f};
    const glm::vec3 storedOrigin = storedCenter - glm::vec3(
        10.0f * bakeCenterCoordinate / static_cast<float>(bakeResolution) * storedScale);
    const glm::mat4 storedModel = glm::translate(glm::mat4(1.0f), storedOrigin) *
        glm::scale(glm::mat4(1.0f), glm::vec3(storedScale));
    Vixen::SVO::SetInstanceTransform(stored, storedModel);
    stored.material.color[0] = hullColor.r;
    stored.material.color[1] = hullColor.g;
    stored.material.color[2] = hullColor.b;
    stored.material.octreeIndex = 0U;
    stored.material.providerKind = 0U;
    scene.instances.push_back(stored);

    if (scene.pool.channelPool.empty()) {
        error = "the non-text distance-filter recipe produced an empty stored-SDF pool";
        return false;
    }
    return true;
}

class LookdevCaptureApplication final : public VulkanGraphApplication {
public:
    LookdevCaptureApplication(std::filesystem::path outputDir,
                              std::vector<CaptureScenario> scenarios,
                              uint32_t framesPerScenario)
        : outputDir_(std::move(outputDir)), scenarios_(std::move(scenarios)),
          framesPerScenario_(framesPerScenario) {
        SetPresentationTarget(PresentationTarget::Offscreen);
        auto config = gRenderPathConfig;
        config.samplingPath = config.renderScale == 1.0f && config.upscaler == UpscalerKind::None
            ? Vixen::RenderGraph::ReconstructionSamplingPath::CanonicalCapture
            : Vixen::RenderGraph::ReconstructionSamplingPath::InteractiveVisual;
        if (std::getenv("VIXEN_LOOKDEV_JITTER_PROBE")) {
            config.samplingPath = Vixen::RenderGraph::ReconstructionSamplingPath::InteractiveVisual;
            config.temporalJitter = true;
        }
        SetRenderPathConfig(config);
    }

    void SetProcessStart(CaptureClock::time_point start) { processStart_ = start; }

    void BuildRenderGraph() override {
        const auto buildStart = CaptureClock::now();
        buildGraphBeginMs_ = MsBetween(processStart_, buildStart);
        BuildRenderGraphTimed();
        buildGraphEndMs_ = MsBetween(processStart_, CaptureClock::now());
    }

    void PrintTimingSummary() const {
        const double total = frameWallMs_.empty() ? 0.0
            : std::accumulate(frameWallMs_.begin(), frameWallMs_.end(), 0.0);
        std::printf("[CAPTURE_TIMING] startup_to_build_graph_ms=%.1f build_graph_ms=%.1f "
                    "build_graph_end_to_first_frame_ms=%.1f first_frame_ms=%.1f startup_total_ms=%.1f\n",
            buildGraphBeginMs_, buildGraphEndMs_ - buildGraphBeginMs_,
            firstFrameBeginMs_ - buildGraphEndMs_,
            frameWallMs_.empty() ? 0.0 : frameWallMs_.front(), firstFrameBeginMs_);
        std::printf("[CAPTURE_TIMING] frames=%zu frame_total_ms=%.1f frame_p50_ms=%.2f frame_p95_ms=%.2f "
                    "frame_max_ms=%.2f png_capture_total_ms=%.1f\n",
            frameWallMs_.size(), total, Percentile(frameWallMs_, 0.50), Percentile(frameWallMs_, 0.95),
            Percentile(frameWallMs_, 1.0), captureWallMs_);
        std::fflush(stdout);
    }

    void BuildRenderGraphTimed() {
        const auto recipes = Vixen::App::Lookdev::BuildMaterialRecipes(scenarios_.front().lighting);
        for (const auto& recipe : recipes) {
            const auto result = RegisterProceduralRecipe(recipe.recipeId, recipe.entry);
            if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                captureError_ = "could not register look-dev material recipe " +
                    std::to_string(recipe.recipeId) + " (status " +
                    std::to_string(static_cast<int>(result)) + ")";
                return;
            }
        }

        const bool glyphSdfScene = std::getenv("VIXEN_GLYPHSDF_SCENE") != nullptr;
        const bool glyphSdfCompare = std::getenv("VIXEN_GLYPHSDF_COMPARE") != nullptr;
        const bool sdfFilterNonText = std::getenv("VIXEN_SDF_FILTER_NON_TEXT") != nullptr;
        const char* glyphSdfVariant = std::getenv("VIXEN_GLYPHSDF_VARIANT");
        const bool glyphSdfRtlProofPair = glyphSdfVariant &&
            std::string_view(glyphSdfVariant) == "rtl-proof-pair";
        GlyphSdfSceneBundle glyphScene;
        if (glyphSdfScene || sdfFilterNonText) {
            const bool sceneBuilt = glyphSdfScene
                ? BuildGlyphSdfScene(glyphScene, outputDir_, glyphSdfCompare, captureError_)
                : BuildSdfFilterNonTextScene(glyphScene, captureError_);
            if (!sceneBuilt) return;
            for (auto& [recipeId, recipe] : glyphScene.proceduralRecipes) {
                const auto result = RegisterProceduralRecipe(recipeId, std::move(recipe));
                if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    captureError_ = "could not register GlyphOutline sign recipe " +
                        std::to_string(recipeId) + " (status " +
                        std::to_string(static_cast<int>(result)) + ")";
                    return;
                }
            }
        }

        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = graph ? dynamic_cast<Vixen::RenderGraph::CameraNode*>(
            graph->GetInstanceByName("raymarch_camera")) : nullptr;
        lighting_ = graph ? dynamic_cast<Vixen::RenderGraph::LightingConfigNode*>(
            graph->GetInstanceByName("lighting_config")) : nullptr;
        if (!camera_ || !lighting_) {
            captureError_ = "production graph is missing raymarch_camera or lighting_config";
            return;
        }

        // The ordinary look-dev capture below is procedural-only, so it cannot
        // prove the composed DDA fallback. When the existing stored-SDF scene is
        // requested, keep its six FORMAT_STORED_SDF instances and frame the
        // central displaced-sphere recipe (world center 64,64,64) for the capture.
        const bool storedSdfScene = std::getenv("VIXEN_BRICKMAP_SCENE") != nullptr;
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, 45.0f);
        if (storedSdfScene) {
            // The brickmap scene is authored at world-space centers near (64,64,64).
            // Use an explicit fixed pose so this capture does not depend on the main
            // graph's preset camera or its orbit-mode setup.
            camera_->SetPositionForTest(glm::vec3(64.0f, 64.0f, 88.0f));
            camera_->SetLookTargetNoOrbitForTest(glm::vec3(64.0f, 64.0f, 64.0f));
        } else {
            const float orbitCenterX = (glyphSdfScene || sdfFilterNonText) ? 0.0f : 5.0f;
            const float orbitCenterY = (glyphSdfScene || sdfFilterNonText) ? 0.0f : 5.0f;
            const float orbitCenterZ = 5.0f;
            const float orbitDistance = (glyphSdfScene || sdfFilterNonText)
                ? ((glyphSdfCompare || sdfFilterNonText) ? 16.0f
                    : (glyphSdfRtlProofPair ? 13.5f : 9.5f))
                : 14.0f;
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_X, orbitCenterX);
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Y, orbitCenterY);
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Z, orbitCenterZ);
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_DISTANCE, orbitDistance);
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_YAW, scenarios_.front().yawRadians);
            camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_PITCH, kPitchRadians);
        }

        lighting_->SetLights(Vixen::App::Lookdev::Lights(scenarios_.front().lighting),
                             scenarios_.front().lighting.ambient,
                             Vixen::App::Lookdev::CelSpillPurposeScales(scenarios_.front().lighting));
        lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                scenarios_.front().lighting.exposureCompensationEV);
        Vixen::App::ApplySceneLook(
            *lighting_,
            graph ? dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(graph->GetInstanceByName("sky_sphere")) : nullptr,
            Vixen::App::Lookdev::SceneLookDeclaration());
        if (glyphSdfScene || sdfFilterNonText) {
            if (glyphSdfCompare || sdfFilterNonText)
                SetRecipePool(std::move(glyphScene.pool));
            SetBodyInstances(std::move(glyphScene.instances));
        } else if (!storedSdfScene) {
            SetBodyInstances(Vixen::App::Lookdev::BodyInstances(
                recipes, scenarios_.front().lighting));
        }
        if (std::getenv("VIXEN_LOOKDEV_READBACK")) {
            auto* records = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
                graph->GetInstanceByName("hit_record_buffer"));
            if (!records) throw std::runtime_error("missing shadow snapshot source");
            records->SetParameter(Vixen::RenderGraph::StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS,
                                  static_cast<uint32_t>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
            auto* wave = dynamic_cast<Vixen::RenderGraph::ComputeStageNode*>(
                graph->GetInstanceByName("shadow_visibility_wave"));
            if (!wave) throw std::runtime_error("missing shadow snapshot stage");
            wave->SetPostDispatchRecorderForTest([this, records](VkCommandBuffer cmd) {
                auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
                    GetRenderGraph()->GetInstanceByName("main_device"));
                auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
                if (!device) throw std::runtime_error("missing shadow snapshot device");
                if (!snapshotBuffer_) {
                    snapshotSize_ = records->GetSizeBytes();
                    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    info.size = snapshotSize_;
                    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    if (vkCreateBuffer(device->device, &info, nullptr, &snapshotBuffer_) != VK_SUCCESS)
                        throw std::runtime_error("shadow snapshot buffer allocation failed");
                    snapshotDevice_ = device->device;
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(device->device, snapshotBuffer_, &requirements);
                    VkPhysicalDeviceMemoryProperties properties{};
                    vkGetPhysicalDeviceMemoryProperties(*device->gpu, &properties);
                    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocation.allocationSize = requirements.size;
                    allocation.memoryTypeIndex = properties.memoryTypeCount;
                    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                        if ((requirements.memoryTypeBits & (1u << i)) &&
                            (properties.memoryTypes[i].propertyFlags &
                             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                            allocation.memoryTypeIndex = i;
                            break;
                        }
                    if (allocation.memoryTypeIndex == properties.memoryTypeCount ||
                        vkAllocateMemory(device->device, &allocation, nullptr, &snapshotMemory_) != VK_SUCCESS ||
                        vkBindBufferMemory(device->device, snapshotBuffer_, snapshotMemory_, 0) != VK_SUCCESS)
                        throw std::runtime_error("shadow snapshot host memory allocation failed");
                }
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
                const VkBufferCopy copy{0, 0, snapshotSize_};
                vkCmdCopyBuffer(cmd, records->GetBufferHandle(), snapshotBuffer_, 1, &copy);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            });
            graph->GetLifecycleHooks().RegisterNodeHook(Vixen::RenderGraph::NodeLifecyclePhase::PreCleanup,
                [this](Vixen::RenderGraph::NodeInstance*) {
                    if (!snapshotDevice_) return;
                    vkDeviceWaitIdle(snapshotDevice_);
                    vkDestroyBuffer(snapshotDevice_, snapshotBuffer_, nullptr);
                    vkFreeMemory(snapshotDevice_, snapshotMemory_, nullptr);
                    snapshotBuffer_ = VK_NULL_HANDLE;
                    snapshotMemory_ = VK_NULL_HANDLE;
                    snapshotDevice_ = VK_NULL_HANDLE;
        }, "lookdev shadow snapshot cleanup", wave);
        }
    }

    bool Render() override {
        const auto frameStart = CaptureClock::now();
        if (frameWallMs_.empty()) firstFrameBeginMs_ = MsBetween(processStart_, frameStart);
        if (std::getenv("VIXEN_LOOKDEV_MOTION_PROBE") && camera_)
            camera_->SetYawForTest(scenarios_[scenarioIndex_].yawRadians + 0.002f * float(framesInScenario_));
        const bool historyProbe = std::getenv("VIXEN_LOOKDEV_HISTORY_PROBE") != nullptr;
        if (historyProbe && framesInScenario_ == 1u) SetReconstructionViewportActive(false);
        if (historyProbe && framesInScenario_ == 2u) {
            SetReconstructionViewportActive(true);
            NotifyReconstructionCameraCut();
        }
        if (!captureError_.empty() || !VulkanGraphApplication::Render()) return false;
        if (historyProbe && framesInScenario_ >= 1u && framesInScenario_ <= 3u) {
            auto* state = GetRenderGraph()->GetInstanceByName("render_path/reconstruction_history/state");
            const auto valid = state->GetOutput(ReconstructionHistoryStateNodeConfig::VALID.index)->GetHandle<uint32_t>();
            const auto reasons = state->GetOutput(ReconstructionHistoryStateNodeConfig::RESET_REASONS.index)->GetHandle<uint32_t>();
            if ((framesInScenario_ < 3u && valid != 0u) || (framesInScenario_ == 3u && valid != 1u) ||
                (framesInScenario_ == 2u && !(reasons & static_cast<uint32_t>(ReconstructionHistoryReset::CameraCut)))) {
                captureError_ = "viewport switch/cut history acknowledgement failed";
                return false;
            }
        }
        frameWallMs_.push_back(MsBetween(frameStart, CaptureClock::now()));
        if (!modulePushChecked_ && std::getenv("VIXEN_LOOKDEV_TEST_INDIRECT_LIGHTING")) {
            modulePushChecked_ = true;
            auto* graph = GetRenderGraph();
            auto* gatherer = graph ? graph->GetInstanceByName("spatial_reuse_push_gatherer") : nullptr;
            auto* packedResource = gatherer
                ? gatherer->GetOutput(PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA.index)
                : nullptr;
            if (!packedResource || !packedResource->IsValid()) {
                captureError_ = "indirect-light test module did not produce push-constant data";
                return false;
            }
            const auto packed = packedResource->GetHandle<std::vector<uint8_t>>();
            constexpr size_t kModuleIrradianceOffset = 96;
            constexpr glm::vec3 kExpectedModuleIrradiance{2.0f, 0.18f, 0.04f};
            if (packed.size() < kModuleIrradianceOffset + sizeof(kExpectedModuleIrradiance)) {
                captureError_ = "indirect-light push-constant block is too short: " +
                    std::to_string(packed.size()) + " bytes";
                return false;
            }
            glm::vec3 packedIrradiance{};
            std::memcpy(&packedIrradiance, packed.data() + kModuleIrradianceOffset,
                        sizeof(packedIrradiance));
            if (std::memcmp(&packedIrradiance, &kExpectedModuleIrradiance,
                            sizeof(packedIrradiance)) != 0) {
                std::ostringstream message;
                message << "indirect-light module push value mismatch: got ("
                        << packedIrradiance.x << ", " << packedIrradiance.y << ", "
                        << packedIrradiance.z << "), packedBytes=" << packed.size();
                auto* variadic = dynamic_cast<IVariadicNode*>(gatherer);
                const auto* slot = variadic ? variadic->GetVariadicSlotInfo(14) : nullptr;
                message << ", variadicSlot14=" << (slot ? "present" : "missing");
                if (slot) {
                    message << ", sourceHandle=" << slot->sourceNode.index
                            << ", sourceOutput=" << slot->sourceOutput
                            << ", resource=" << (slot->resource ? "present" : "null");
                }
                auto* sourceNode = graph->GetInstanceByName(
                    "capture_test_indirect_lighting/irradiance");
                auto* sourceOutput = sourceNode
                    ? sourceNode->GetOutput(TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE.index)
                    : nullptr;
                message << ", moduleOutput=" << (sourceOutput && sourceOutput->IsValid()
                    ? "valid" : "missing");
                if (sourceOutput && sourceOutput->IsValid()) {
                    const glm::vec3 sourceValue = sourceOutput->GetHandle<glm::vec3>();
                    message << " (" << sourceValue.x << ", " << sourceValue.y
                            << ", " << sourceValue.z << ")";
                }
                captureError_ = message.str();
                return false;
            }
        }
        ++framesInScenario_;
        if (framesInScenario_ < framesPerScenario_) return true;

        const auto& scenario = scenarios_[scenarioIndex_];
        // Verifying every LightingConfig ring slot needs each slot rewritten at least once;
        // a capture shorter than the ring only exercises rendering.
        if (scenarioIndex_ == 0 && framesPerScenario_ >= kRingSlotCount &&
            !VerifyUploadedExposure(scenario)) {
            return false;
        }
        const std::filesystem::path output = outputDir_ / scenario.filename;
        const auto captureStart = CaptureClock::now();
        if (!CaptureOffscreenFrameToPng(output.string(), captureError_)) return false;
        captureWallMs_ += MsBetween(captureStart, CaptureClock::now());
        if (std::getenv("VIXEN_LOOKDEV_READBACK") && !ExportLightingReadback(scenario)) return false;
        if (std::getenv("VIXEN_LOOKDEV_DEPTH_READBACK") && !ExportDepthReadback(scenario)) return false;
        ++capturedCount_;

        if (capturedCount_ < scenarios_.size()) {
            scenarioIndex_ = capturedCount_;
            framesInScenario_ = 0;
            const auto& next = scenarios_[scenarioIndex_];
            camera_->SetYawForTest(next.yawRadians);
            camera_->SetPitchForTest(kPitchRadians);
            lighting_->SetLights(Vixen::App::Lookdev::Lights(next.lighting),
                                 next.lighting.ambient,
                                 Vixen::App::Lookdev::CelSpillPurposeScales(next.lighting));
            lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                    next.lighting.exposureCompensationEV);
        }
        return true;
    }

    size_t CapturedCount() const { return capturedCount_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    bool ExportDepthReadback(const CaptureScenario& scenario) {
        using namespace Vixen::RenderGraph;
        auto* graph = GetRenderGraph();
        auto* deviceNode = dynamic_cast<DeviceNode*>(graph->GetInstanceByName("main_device"));
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        const auto targetByName = [graph](const char* name) {
            auto* node = graph->GetInstanceByName(name);
            auto* resource = node ? node->GetOutput(RenderTargetNodeConfig::RENDER_TARGET.index) : nullptr;
            return resource ? resource->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>() : nullptr;
        };
        auto* source = targetByName("compute_render_target");
        auto* view = targetByName("render_path/reconstruction_inputs/view_depth");
        auto* normalized = targetByName("render_path/reconstruction_inputs/normalized_depth");
        if (!device || !source || !view || !normalized ||
            view->GetFormat() != VK_FORMAT_R32_SFLOAT || normalized->GetFormat() != VK_FORMAT_R32_SFLOAT ||
            view->GetExtent().width != source->GetExtent().width ||
            view->GetExtent().height != source->GetExtent().height ||
            normalized->GetExtent().width != view->GetExtent().width ||
            normalized->GetExtent().height != view->GetExtent().height) {
            captureError_ = "reconstruction depth guides must be R32F at the internal extent";
            return false;
        }
        std::vector<uint8_t> viewBytes, normalizedBytes;
        if (!Debug::ReadRenderTargetBytes(device, view, device->queue, device->graphicsQueueIndex,
                                         viewBytes, captureError_) ||
            !Debug::ReadRenderTargetBytes(device, normalized, device->queue, device->graphicsQueueIndex,
                                         normalizedBytes, captureError_)) return false;
        void* mapped = nullptr;
        if (!snapshotMemory_ || vkMapMemory(device->device, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) {
            captureError_ = "depth witness requires the matching primary hit-record snapshot";
            return false;
        }
        const auto extent = view->GetExtent();
        const size_t count = size_t(extent.width) * extent.height;
        const auto& camera = camera_->GetCurrentCameraData();
        const glm::mat4 projection = glm::inverse(camera.invProjection);
        size_t hits = 0, misses = 0, offAxis = 0, failures = 0;
        float maxViewError = 0.0f, maxNormalizedError = 0.0f;
        std::vector<uint8_t> preview(count * 3);
        if (viewBytes.size() != count * sizeof(float) || normalizedBytes.size() != viewBytes.size() ||
            snapshotSize_ < count * 64) {
            vkUnmapMemory(device->device, snapshotMemory_);
            captureError_ = "depth readback byte count does not match the internal extent";
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            float viewDepth, normalizedDepth, hitT;
            uint32_t flags;
            glm::vec3 world;
            const auto* record = static_cast<const uint8_t*>(mapped) + i * 64;
            std::memcpy(&hitT, record + 28, sizeof(hitT));
            std::memcpy(&world, record + 32, sizeof(world));
            std::memcpy(&flags, record + 44, sizeof(flags));
            std::memcpy(&viewDepth, viewBytes.data() + i * 4, 4);
            std::memcpy(&normalizedDepth, normalizedBytes.data() + i * 4, 4);
            if ((flags & 1u) != 0u) {
                ++hits;
                const float expectedView = glm::dot(world - camera.cameraPos, camera.cameraDir);
                // Independent forward projection, rather than the shader's inverse equation.
                const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -expectedView, 1.0f);
                const float expectedNormalized = glm::clamp(clip.z / clip.w, 0.0f, 1.0f);
                maxViewError = std::max(maxViewError, std::abs(viewDepth - expectedView));
                maxNormalizedError = std::max(maxNormalizedError, std::abs(normalizedDepth - expectedNormalized));
                failures += !std::isfinite(viewDepth) || !std::isfinite(normalizedDepth) ||
                    std::abs(viewDepth - expectedView) > std::max(1.0e-4f, expectedView * 2.0e-5f) ||
                    std::abs(normalizedDepth - expectedNormalized) > 2.0e-6f;
                offAxis += hitT > expectedView + 1.0e-3f;
            } else {
                ++misses;
                failures += viewDepth != 1.0e30f || normalizedDepth != 1.0f;
            }
            // Fixed, documented visualization; retain unmodified R32F bytes below.
            const uint8_t shade = (flags & 1u) ? static_cast<uint8_t>(255.0f * std::exp(-viewDepth / 20.0f)) : 0;
            preview[i * 3] = preview[i * 3 + 1] = preview[i * 3 + 2] = shade;
        }
        vkUnmapMemory(device->device, snapshotMemory_);
        const auto stem = outputDir_ / std::filesystem::path(scenario.filename).stem();
        std::ofstream viewFile(stem.string() + ".view-depth.f32", std::ios::binary);
        viewFile.write(reinterpret_cast<const char*>(viewBytes.data()), viewBytes.size());
        std::ofstream normalizedFile(stem.string() + ".normalized-depth.f32", std::ios::binary);
        normalizedFile.write(reinterpret_cast<const char*>(normalizedBytes.data()), normalizedBytes.size());
        std::ofstream result(stem.string() + ".depth.json");
        result << "{\"width\":" << extent.width << ",\"height\":" << extent.height
               << ",\"hits\":" << hits << ",\"misses\":" << misses << ",\"off_axis_hits\":" << offAxis
               << ",\"failures\":" << failures << ",\"max_view_error\":" << maxViewError
               << ",\"max_normalized_error\":" << maxNormalizedError << "}\n";
        const bool png = stbi_write_png((stem.string() + ".view-depth.png").c_str(),
            int(extent.width), int(extent.height), 3, preview.data(), int(extent.width) * 3) != 0;
        if (!viewFile || !normalizedFile || !result || !png || !hits || !misses || !offAxis || failures) {
            captureError_ = "depth witness failed: " + std::to_string(failures) + " pixel mismatches; " +
                std::to_string(hits) + " hits, " + std::to_string(misses) + " misses";
            return false;
        }
        auto* motion = targetByName("render_path/reconstruction_motion/motion");
        auto* validity = targetByName("render_path/reconstruction_motion/validity");
        std::vector<uint8_t> motionBytes, validityBytes;
        if (!motion || !validity || motion->GetFormat() != VK_FORMAT_R32G32_SFLOAT ||
            validity->GetFormat() != VK_FORMAT_R32_SFLOAT ||
            !Debug::ReadRenderTargetBytes(device, motion, device->queue, device->graphicsQueueIndex, motionBytes, captureError_) ||
            !Debug::ReadRenderTargetBytes(device, validity, device->queue, device->graphicsQueueIndex, validityBytes, captureError_)) return false;
        if (motionBytes.size() != size_t(extent.width) * extent.height * 8u ||
            validityBytes.size() != size_t(extent.width) * extent.height * 4u) return false;
        if (vkMapMemory(device->device, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) return false;
        const auto previousVP = camera_->GetOutput(CameraNodeConfig::PREV_VIEW_PROJ.index)->GetHandle<const glm::mat4&>();
        const auto currentVP = camera_->GetCurrentViewProj();
        size_t motionFailures = 0, nonzeroMotion = 0;
        float maxMotionError = 0.0f;
        std::vector<uint8_t> motionPreview(size_t(extent.width) * extent.height * 3u, 0);
        for (size_t i = 0; i < size_t(extent.width) * extent.height; ++i) {
            float vector[2], correspondence;
            std::memcpy(vector, motionBytes.data() + i * 8u, 8u);
            std::memcpy(&correspondence, validityBytes.data() + i * 4u, 4u);
            const auto* record = static_cast<const uint8_t*>(mapped) + i * 64u;
            uint32_t flags;
            glm::vec3 world;
            std::memcpy(&world, record + 32u, sizeof(world));
            std::memcpy(&flags, record + 44u, sizeof(flags));
            glm::vec2 expected(0.0f);
            if (flags & 1u) {
                const auto previous = previousVP * glm::vec4(world, 1.0f);
                const auto current = currentVP * glm::vec4(world, 1.0f);
                if (previous.w > 0.0f && current.w > 0.0f)
                    expected = (glm::vec2(previous) / previous.w - glm::vec2(current) / current.w) * 0.5f;
            }
            const float error = glm::length(glm::vec2(vector[0], vector[1]) - expected);
            maxMotionError = std::max(maxMotionError, error);
            motionFailures += !std::isfinite(vector[0]) || !std::isfinite(vector[1]) || error > 3e-6f || correspondence != 0.0f;
            nonzeroMotion += glm::length(expected) > 1e-5f;
            if (flags & 1u) {
                motionPreview[i * 3] = uint8_t(glm::clamp(128.0f + vector[0] * float(extent.width) * 32.0f, 0.0f, 255.0f));
                motionPreview[i * 3 + 1] = uint8_t(glm::clamp(128.0f + vector[1] * float(extent.height) * 32.0f, 0.0f, 255.0f));
                motionPreview[i * 3 + 2] = 128u;
            }
        }
        vkUnmapMemory(device->device, snapshotMemory_);
        std::ofstream motionFile(stem.string() + ".motion.f32", std::ios::binary);
        motionFile.write(reinterpret_cast<const char*>(motionBytes.data()), motionBytes.size());
        std::ofstream validityFile(stem.string() + ".motion-validity.f32", std::ios::binary);
        validityFile.write(reinterpret_cast<const char*>(validityBytes.data()), validityBytes.size());
        std::ofstream motionResult(stem.string() + ".motion.json");
        motionResult << "{\"convention\":\"previousUV-currentUV; right/down; unjittered cameras\",\"failures\":"
                     << motionFailures << ",\"nonzero_camera_vectors\":" << nonzeroMotion
                     << ",\"max_uv_error\":" << maxMotionError
                     << ",\"procedural_correspondence_valid\":false}\n";
        if (!motionFile || !validityFile || !motionResult || motionFailures ||
            (std::getenv("VIXEN_LOOKDEV_MOTION_PROBE") && !nonzeroMotion) ||
            !stbi_write_png((stem.string() + ".motion.png").c_str(), int(extent.width), int(extent.height),
                            3, motionPreview.data(), int(extent.width) * 3)) {
            captureError_ = "motion guide oracle failed: " + std::to_string(motionFailures);
            return false;
        }
        auto* historyA = targetByName("render_path/reconstruction_history/history_a");
        auto* historyB = targetByName("render_path/reconstruction_history/history_b");
        auto* display = targetByName("main_offscreen_target");
        auto* historyState = dynamic_cast<ReconstructionHistoryStateNode*>(
            graph->GetInstanceByName("render_path/reconstruction_history/state"));
        if (!historyA || !historyB || !display || !historyState ||
            historyA->GetImageCount() != 1 || historyB->GetImageCount() != 1 ||
            historyA->GetImage(0) == VK_NULL_HANDLE || historyB->GetImage(0) == VK_NULL_HANDLE ||
            historyA->GetImage(0) == historyB->GetImage(0) ||
            (historyA->GetView(0) != VK_NULL_HANDLE && historyA->GetView(0) == historyB->GetView(0)) ||
            historyA->GetFormat() != VK_FORMAT_R16G16B16A16_SFLOAT ||
            historyB->GetFormat() != VK_FORMAT_R16G16B16A16_SFLOAT ||
            historyA->GetExtent().width != display->GetExtent().width ||
            historyA->GetExtent().height != display->GetExtent().height ||
            historyB->GetExtent().width != display->GetExtent().width ||
            historyB->GetExtent().height != display->GetExtent().height ||
            historyState->GetOutput(ReconstructionHistoryStateNodeConfig::RESET.index)->GetHandle<uint32_t>() != (framesPerScenario_ < 2u ? 1u : 0u) ||
            historyState->GetOutput(ReconstructionHistoryStateNodeConfig::VALID.index)->GetHandle<uint32_t>() != (framesPerScenario_ < 2u ? 0u : 1u)) {
            captureError_ = "viewport history must own distinct output-size images and commit only written frames";
            return false;
        }
        auto* radianceNode = graph->GetInstanceByName("scene_radiance");
        auto* radiance = radianceNode->GetOutput(SceneRadianceNodeConfig::HISTORY_TARGET.index)
            ->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
        std::vector<uint8_t> radianceBytes;
        if (!Debug::ReadRenderTargetBytes(device, radiance, device->queue, device->graphicsQueueIndex,
                                         radianceBytes, captureError_)) return false;
        std::ofstream sourceRaw(stem.string() + ".history-source.f16", std::ios::binary);
        sourceRaw.write(reinterpret_cast<const char*>(radianceBytes.data()), radianceBytes.size());
        if (!sourceRaw || radiance->GetExtent().width != extent.width || radiance->GetExtent().height != extent.height) {
            captureError_ = "scene history source extent mismatch";
            return false;
        }
        const auto writeIndex = historyState->GetOutput(ReconstructionHistoryStateNodeConfig::WRITE_INDEX.index)->GetHandle<uint32_t>();
        size_t historyFailures = 0, positiveHistory = 0;
        for (uint32_t imageIndex = 0; imageIndex < 2; ++imageIndex) {
            auto* target = imageIndex == 0 ? historyA : historyB;
            std::vector<uint8_t> bytes;
            if (!Debug::ReadRenderTargetBytes(device, target, device->queue, device->graphicsQueueIndex,
                                             bytes, captureError_)) return false;
            const auto outputExtent = target->GetExtent();
            std::vector<uint8_t> preview(size_t(outputExtent.width) * outputExtent.height * 3u);
            for (uint32_t y = 0; y < outputExtent.height; ++y) for (uint32_t x = 0; x < outputExtent.width; ++x) {
                const size_t i = size_t(y) * outputExtent.width + x;
                const uint32_t sx = std::min(uint32_t((float(x) + 0.5f) * float(extent.width) / float(outputExtent.width)), extent.width - 1);
                const uint32_t sy = std::min(uint32_t((float(y) + 0.5f) * float(extent.height) / float(outputExtent.height)), extent.height - 1);
                if (imageIndex == writeIndex)
                    historyFailures += std::memcmp(bytes.data() + i * 8u,
                        radianceBytes.data() + (size_t(sy) * extent.width + sx) * 8u, 8u) != 0;
                for (size_t c = 0; c < 3; ++c) {
                    uint16_t half;
                    std::memcpy(&half, bytes.data() + i * 8u + c * 2u, 2u);
                    const float value = glm::unpackHalf1x16(half);
                    historyFailures += !std::isfinite(value) || value < 0.0f;
                    positiveHistory += value > 0.0f;
                    const float mappedValue = std::pow(std::max(value, 0.0f) / (1.0f + std::max(value, 0.0f)), 1.0f / 2.2f);
                    preview[i * 3u + c] = uint8_t(glm::clamp(mappedValue * 255.0f, 0.0f, 255.0f));
                }
            }
            const std::string imageStem = stem.string() + (imageIndex == 0 ? ".history-a" : ".history-b");
            std::ofstream raw(imageStem + ".f16", std::ios::binary);
            raw.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!raw || !stbi_write_png((imageStem + ".png").c_str(), int(outputExtent.width), int(outputExtent.height),
                                        3, preview.data(), int(outputExtent.width) * 3)) return false;
        }
        std::ofstream historyResult(stem.string() + ".history.json");
        historyResult << "{\"kind\":\"unblended scene-linear seed; not temporal reconstruction\",\"failures\":"
                      << historyFailures << ",\"positive_channels\":" << positiveHistory
                      << ",\"write_index\":" << writeIndex << "}\n";
        if (!historyResult || historyFailures || !positiveHistory) {
            captureError_ = "history seed oracle failed: " + std::to_string(historyFailures);
            return false;
        }
        auto* pickNode = graph->GetInstanceByName("pick_id_target");
        if (!pickNode) { captureError_ = "missing native picking target"; return false; }
        Vixen::Vulkan::Resources::RenderTargetData pickTarget;
        pickTarget.buffers = {{pickNode->GetOutput(PickIdTargetNodeConfig::ID_IMAGE.index)->GetHandle<VkImage>(),
            VK_NULL_HANDLE, pickNode->GetOutput(PickIdTargetNodeConfig::ID_IMAGE_VIEW.index)->GetHandle<VkImageView>(),
            VK_IMAGE_LAYOUT_GENERAL}};
        pickTarget.format = VK_FORMAT_R32G32_UINT;
        pickTarget.imageUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        pickTarget.extent = extent;
        if (auto* nativeExtent = graph->GetInstanceByName("native_pick_extent")) {
            pickTarget.extent = {
                nativeExtent->GetOutput(RenderTargetExtentNodeConfig::WIDTH.index)->GetHandle<uint32_t>(),
                nativeExtent->GetOutput(RenderTargetExtentNodeConfig::HEIGHT.index)->GetHandle<uint32_t>()};
        }
        std::vector<uint8_t> pickingBytes;
        if (!Debug::ReadRenderTargetBytes(device, &pickTarget, device->queue, device->graphicsQueueIndex,
                                         pickingBytes, captureError_)) return false;
        std::ofstream pickRaw(stem.string() + ".picking.rg32ui", std::ios::binary);
        pickRaw.write(reinterpret_cast<const char*>(pickingBytes.data()), pickingBytes.size());
        auto* jitterResource = historyState->GetOutput(ReconstructionHistoryStateNodeConfig::JITTER.index);
        const auto jitter = jitterResource->GetHandle<glm::vec2>();
        std::ofstream samplingResult(stem.string() + ".sampling.json");
        samplingResult << "{\"jitter_input_pixels\":[" << jitter.x << ',' << jitter.y
                       << "],\"pick_width\":" << pickTarget.extent.width
                       << ",\"pick_height\":" << pickTarget.extent.height << "}\n";
        if (!pickRaw || !samplingResult ||
            (std::getenv("VIXEN_LOOKDEV_JITTER_PROBE") && jitter == glm::vec2(0.0f))) return false;
        auto* exposure = targetByName("render_path/reconstruction_exposure/exposure");
        std::vector<uint8_t> exposureBytes;
        auto* meter = dynamic_cast<StorageBufferNode*>(graph->GetInstanceByName("exposure_meter_buffer"));
        if (!exposure || exposure->GetExtent().width != 1 || exposure->GetExtent().height != 1 ||
            exposure->GetFormat() != VK_FORMAT_R32_SFLOAT || !meter ||
            !Debug::ReadRenderTargetBytes(device, exposure, device->queue, device->graphicsQueueIndex,
                                         exposureBytes, captureError_) || exposureBytes.size() != 4) {
            captureError_ = "reconstruction exposure must be a 1x1 R32F image";
            return false;
        }
        void* mappedMeter = meter->MapForReadback(device);
        if (!mappedMeter) {
            captureError_ = "cannot read the whole-frame exposure meter";
            return false;
        }
        float ev100, actualExposure;
        std::memcpy(&ev100, mappedMeter, sizeof(ev100));
        std::memcpy(&actualExposure, exposureBytes.data(), sizeof(actualExposure));
        meter->UnmapReadback(device);
        const float expectedExposure = std::exp2(ev100 + scenario.lighting.exposureCompensationEV);
        std::ofstream exposureFile(stem.string() + ".exposure.f32", std::ios::binary);
        exposureFile.write(reinterpret_cast<const char*>(exposureBytes.data()), exposureBytes.size());
        std::ofstream exposureResult(stem.string() + ".exposure.json");
        exposureResult << "{\"exposure\":" << actualExposure << ",\"expected\":" << expectedExposure
                       << ",\"ev100\":" << ev100 << ",\"pre_exposure\":1}\n";
        if (!exposureFile || !exposureResult || !std::isfinite(actualExposure) || actualExposure <= 0.0f ||
            std::abs(actualExposure - expectedExposure) > expectedExposure * 2.0e-5f) {
            captureError_ = "reconstruction exposure does not match the scene-linear meter scale";
            return false;
        }
        return true;
    }

    bool ExportLightingReadback(const CaptureScenario& scenario) {
        auto* graph = GetRenderGraph();
        auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
            graph->GetInstanceByName("main_device"));
        auto* records = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
            graph->GetInstanceByName("hit_record_buffer"));
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        if (!device || !records || vkDeviceWaitIdle(device->device) != VK_SUCCESS) {
            captureError_ = "could not synchronize look-dev shadow readback";
            return false;
        }
        const auto stem = outputDir_ / std::filesystem::path(scenario.filename).stem();
        void* mapped = nullptr;
        if (!snapshotMemory_ || vkMapMemory(device->device, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) {
            captureError_ = "could not map look-dev HitRecord buffer";
            return false;
        }
        std::ofstream hits(stem.string() + ".hits.bin", std::ios::binary);
        hits.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(snapshotSize_));
        vkUnmapMemory(device->device, snapshotMemory_);
        std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)> bytes{};
        if (!hits || !Vixen::RenderGraph::LightingConfigNodeTestAccess::ReadMappedBytes(*lighting_, 0, bytes)) {
            captureError_ = "could not export look-dev lighting buffers";
            return false;
        }
        std::ofstream config(stem.string() + ".lighting.bin", std::ios::binary);
        config.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!config) {
            captureError_ = "could not export look-dev LightingConfig";
            return false;
        }
        auto* meterBuffer = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
            graph->GetInstanceByName("exposure_meter_buffer"));
        void* mappedMeter = meterBuffer ? meterBuffer->MapForReadback(device) : nullptr;
        if (!mappedMeter) {
            captureError_ = "could not export exposure meter";
            return false;
        }
        std::ofstream meter(stem.string() + ".meter.bin", std::ios::binary);
        meter.write(static_cast<const char*>(mappedMeter), sizeof(float));
        meterBuffer->UnmapReadback(device);
        if (!meter) {
            captureError_ = "could not write exposure meter";
            return false;
        }
        std::ofstream scene(stem.string() + ".scene.json");
        const auto recipes = Vixen::App::Lookdev::BuildMaterialRecipes(scenario.lighting);
        scene << std::setprecision(9) << "{\"lightCapacity\":"
              << std::size(Vixen::Gpu::LightingConfig{}.lights)
              << ",\"exposureOffset\":" << offsetof(Vixen::Gpu::LightingConfig, exposureCompensationEV)
              << ",\"instances\":[";
        const auto* bodyScene = dynamic_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
            graph->GetInstanceByName("body_octree_scene"));
        if (!bodyScene) {
            captureError_ = "missing runtime body order for shadow snapshot";
            return false;
        }
        const auto& runtimeInstances = bodyScene->GetInstances();
        for (size_t i = 0; i < runtimeInstances.size(); ++i) {
            if (i) scene << ',';
            const auto recipe = std::find_if(recipes.begin(), recipes.end(), [&](const auto& value) {
                return value.recipeId == runtimeInstances[i].material.recipeId;
            });
            if (recipe == recipes.end()) throw std::runtime_error("unknown runtime recipe in capture");
            scene << "{\"index\":" << i << ",\"material\":" << static_cast<unsigned>(recipe->material)
                  << ",\"recipeId\":" << recipe->recipeId << '}';
        }
        scene << "],\"primitives\":[";
        bool first = true;
        auto primitive = [&](const Vixen::App::Lookdev::Primitive& authored) {
            if (std::none_of(recipes.begin(), recipes.end(), [&](const auto& recipe) {
                    return recipe.material == authored.material;
                })) return;
            const auto value = Vixen::App::Lookdev::PrimitiveForLighting(authored, scenario.lighting);
            if (!first) scene << ',';
            first = false;
            const auto center = Vixen::App::Lookdev::GridToWorld(value.center);
            const auto extent = Vixen::App::Lookdev::GridToWorld(value.extent);
            scene << "{\"group\":" << static_cast<unsigned>(value.group)
                  << ",\"material\":" << static_cast<unsigned>(value.material)
                  << ",\"center\":[" << center.x << ',' << center.y << ',' << center.z
                  << "],\"extent\":[" << extent.x << ',' << extent.y << ',' << extent.z << "]}";
        };
        for (const auto& value : Vixen::App::Lookdev::Primitives()) primitive(value);
        for (const auto& value : Vixen::App::Lookdev::FigureAppendages()) primitive(value);
        scene << "]}\n";
        if (!scene) {
            captureError_ = "could not export look-dev scene metadata";
            return false;
        }
        return true;
    }

    bool VerifyUploadedExposure(const CaptureScenario& scenario) {
        const std::size_t exposureOffset = offsetof(Vixen::Gpu::LightingConfig, exposureCompensationEV);
        std::ofstream readback(outputDir_ / "lighting-config-readback.txt", std::ios::trunc);
        if (!readback) {
            captureError_ = "could not create LightingConfig readback report";
            return false;
        }

        for (uint32_t frameIndex = 0;
             frameIndex < Vixen::RenderGraph::LightingConfigNodeTestAccess::FrameCount(*lighting_);
             ++frameIndex) {
            std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)> bytes{};
            if (!Vixen::RenderGraph::LightingConfigNodeTestAccess::ReadMappedBytes(
                    *lighting_, frameIndex, bytes)) {
                captureError_ = "could not read mapped LightingConfig ring slot " +
                    std::to_string(frameIndex);
                return false;
            }

            float uploadedExposureEV = 0.0f;
            std::memcpy(&uploadedExposureEV, bytes.data() + exposureOffset, sizeof(uploadedExposureEV));

            std::ostringstream line;
            line << "frame_slot=" << frameIndex
                 << " offset=" << exposureOffset
                 << " bytes=" << std::hex << std::setfill('0')
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 1]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 2]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 3])
                 << std::dec << std::setprecision(9)
                 << " value=" << uploadedExposureEV
                 << " expected=" << scenario.lighting.exposureCompensationEV;
            readback << line.str() << '\n';

            if (uploadedExposureEV != scenario.lighting.exposureCompensationEV) {
                captureError_ = "LightingConfig SSBO exposure readback mismatch: " + line.str();
                return false;
            }
        }
        return true;
    }

    std::filesystem::path outputDir_;
    std::vector<CaptureScenario> scenarios_;
    uint32_t framesPerScenario_ = kDefaultFramesPerScenario;
    CaptureClock::time_point processStart_ = CaptureClock::now();
    double buildGraphBeginMs_ = 0.0;
    double buildGraphEndMs_ = 0.0;
    double firstFrameBeginMs_ = 0.0;
    double captureWallMs_ = 0.0;
    std::vector<double> frameWallMs_;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::RenderGraph::LightingConfigNode* lighting_ = nullptr;
    size_t scenarioIndex_ = 0;
    size_t capturedCount_ = 0;
    uint32_t framesInScenario_ = 0;
    bool modulePushChecked_ = false;
    std::string captureError_;
    VkDevice snapshotDevice_ = VK_NULL_HANDLE;
    VkBuffer snapshotBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory snapshotMemory_ = VK_NULL_HANDLE;
    VkDeviceSize snapshotSize_ = 0;
};

bool CaptureSequence(const std::filesystem::path& outputDir,
                     const std::vector<CaptureScenario>& scenarios,
                     uint32_t framesPerScenario,
                     std::string& error) {
    std::error_code ec;
    // The directory is supplied by the CTest runner. Create it if needed, but
    // do not recursively delete caller-owned paths when the test is invoked
    // directly with a custom VIXEN_LOOKDEV_CAPTURE_DIR.
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        error = "could not create capture directory: " + outputDir.string() + ": " + ec.message();
        return false;
    }

    LookdevCaptureApplication app(outputDir, scenarios, framesPerScenario);
    const auto processStart = CaptureClock::now();
    app.SetProcessStart(processStart);
    const uint32_t frameCount = framesPerScenario * static_cast<uint32_t>(scenarios.size());
    const int result = app.Run(RunOptions{.exitAfterFrames = frameCount});
    const double processMs = MsBetween(processStart, CaptureClock::now());
    const double budgetMs = ProcessBudgetMs();
    app.PrintTimingSummary();
    std::printf("[CAPTURE_TIMING] frames_per_angle=%u process_total_ms=%.1f budget_ms=%.0f\n",
        framesPerScenario, processMs, budgetMs);
    std::fflush(stdout);
    if (result != 0 || app.CapturedCount() != scenarios.size()) {
        error = app.CaptureError().empty()
            ? "application exited before all look-dev captures were written"
            : app.CaptureError();
        return false;
    }
    if (processMs > budgetMs) {
        error = "look-dev capture exceeded its runtime budget: " + std::to_string(processMs) +
            " ms > " + std::to_string(budgetMs) + " ms (VIXEN_LOOKDEV_CAPTURE_BUDGET_MS)";
        return false;
    }
    for (const auto& scenario : scenarios) {
        const auto path = outputDir / scenario.filename;
        if (!std::filesystem::exists(path) || std::filesystem::file_size(path, ec) == 0) {
            error = "capture file was not written: " + path.string();
            return false;
        }
    }
    return true;
}

TEST(LookdevCapture, RendersFourStatesAtFourAnglesDeterministically) {
    const char* outputEnv = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the look-dev capture directory";
    const std::filesystem::path outputDir(outputEnv);
    const std::vector<CaptureScenario> scenarios = BuildScenarios();
    const bool slowPan = std::getenv("VIXEN_LOOKDEV_CAPTURE_PAN") != nullptr;
    const bool oneAngleRequested = std::getenv("VIXEN_LOOKDEV_CAPTURE_ANGLE") != nullptr;
    ASSERT_EQ(scenarios.size(), slowPan ? 24u : (oneAngleRequested ? 1u : 4u))
        << "CTest must select one supported look-dev lighting preset";
    std::string error;
    const uint32_t framesPerScenario = CaptureFramesPerScenario(error);
    ASSERT_NE(framesPerScenario, 0u) << error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, framesPerScenario, error)) << error;
}

TEST(LookdevCapture, RendersGlyphSdfLocalizedSign) {
    ASSERT_NE(std::getenv("VIXEN_GLYPHSDF_SCENE"), nullptr)
        << "CTest must enable the fixed-bucket localized sign scene";
    ASSERT_NE(std::getenv("VIXEN_GLYPHSDF_VARIANT"), nullptr)
        << "CTest must select one Latin or qps-rtl carve/emboss variant";
    ASSERT_EQ(std::getenv("VIXEN_GLYPHSDF_COMPARE"), nullptr)
        << "use the analytic-vs-voxel capture test for the split comparison frame";
    const char* outputEnv = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the glyph look-dev capture directory";
    const std::filesystem::path outputDir(outputEnv);
    const std::vector<CaptureScenario> scenarios = BuildScenarios();
    ASSERT_EQ(scenarios.size(), 1U);
    std::string error;
    const uint32_t framesPerScenario = CaptureFramesPerScenario(error);
    ASSERT_NE(framesPerScenario, 0u) << error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, framesPerScenario, error)) << error;
}

TEST(LookdevCapture, RendersGlyphSdfAnalyticVsVoxel) {
    ASSERT_NE(std::getenv("VIXEN_GLYPHSDF_SCENE"), nullptr)
        << "CTest must enable the fixed-bucket localized sign scene";
    ASSERT_NE(std::getenv("VIXEN_GLYPHSDF_COMPARE"), nullptr)
        << "CTest must request the analytic-vs-voxel split frame";
    const char* outputEnv = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the glyph comparison directory";
    const std::filesystem::path outputDir(outputEnv);
    const std::vector<CaptureScenario> scenarios = BuildScenarios();
    ASSERT_EQ(scenarios.size(), 1U);
    std::string error;
    const uint32_t framesPerScenario = CaptureFramesPerScenario(error);
    ASSERT_NE(framesPerScenario, 0u) << error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, framesPerScenario, error)) << error;
}

TEST(LookdevCapture, RendersSdfFilterNonText) {
    ASSERT_NE(std::getenv("VIXEN_SDF_FILTER_NON_TEXT"), nullptr)
        << "CTest must enable the analytic and stored SDF sphere pair";
    ASSERT_EQ(std::getenv("VIXEN_GLYPHSDF_SCENE"), nullptr)
        << "the distance-filter witness is a non-text recipe";
    const char* outputEnv = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the non-text SDF capture directory";
    const std::filesystem::path outputDir(outputEnv);
    const std::vector<CaptureScenario> scenarios = BuildScenarios();
    ASSERT_EQ(scenarios.size(), 1U);
    std::string error;
    const uint32_t framesPerScenario = CaptureFramesPerScenario(error);
    ASSERT_NE(framesPerScenario, 0u) << error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, framesPerScenario, error)) << error;
}

} // namespace

int main(int argc, char** argv) {
    int forwardedArgc = 1;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.starts_with("--render-scale=")) {
            gRenderPathConfig.renderScale = std::stof(argument.substr(15));
        } else if (argument == "--upscaler=none") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::None;
        } else if (argument == "--upscaler=linear") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::Linear;
        } else if (argument == "--upscaler=fsr1") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::Fsr1;
        } else if (argument == "--upscaler=fsr3") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::Fsr3;
        } else {
            argv[forwardedArgc++] = argv[index];
        }
    }
    argc = forwardedArgc;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
