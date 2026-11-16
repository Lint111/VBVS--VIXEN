#pragma once

#include "graph/SceneLook.h"
#include "RuntimeMaterialTable.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/RecipeRegistry.h"
#include "ShellOctreeGpu.h"
#include "Generated/LightingConfig.g.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Vixen::App::Lookdev {

// Look-dev declares the engine's long-standing sky gradient and cel thresholds explicitly, so
// the captures do not depend on LightingConfigNode defaults. Values are unchanged by R507.3.
inline SceneLook SceneLookDeclaration() {
    SceneLook look;
    look.environment.background = EnvironmentDeclaration::Background::Gradient;
    look.environment.color[0] = 0.02f;
    look.environment.color[1] = 0.02f;
    look.environment.color[2] = 0.06f;
    look.environment.gradientSlope = 0.4f;
    look.environment.starfield = false;
    look.presentationMode = PresentationMode::Presentation;
    look.cel.shadowThreshold = 0.16f;   // R432 cel defaults
    look.cel.litThreshold = 0.86f;
    look.cel.rampSoftness = 0.08f;
    look.cel.dimBandFloor = 0.0f;       // black shadow step: indoor and night look-dev lights fill by ambient
    // An ordinary lit scene: the meter adapts with the default preset's -8 EV100 floor (R670.2).
    look.exposure.mode = ExposureDeclaration::Mode::Auto;
    look.exposure.preset = "default";
    return look;
}


// Authored fixture data for the engine's shared visual-review stage. Geometry is
// registered through the existing procedural SDF recipe provider; this is not a
// new node family or runtime content format.
enum class Shape : uint8_t { Box, Ellipsoid };
enum class Group : uint8_t { Machine, Rock, Foliage, Water, Figure, Ground };
enum class Material : uint8_t {
    Hull,
    HullAccent,
    Structure,
    DarkMetal,
    Glass,
    Rock,
    Bark,
    Soil,
    Leaf,
    DeepLeaf,
    Water,
    Figure,
    Lamp,
    Window,
    Worklight,
    Count,
};

struct Primitive {
    Shape shape;
    Group group;
    Material material;
    glm::vec3 center;
    glm::vec3 extent;
    float roundness;
};

struct LightingPreset {
    const char* name;
    glm::vec3 keyDirection;
    glm::vec3 keyColorSrgb;
    float keyIntensity;
    float ambient;
    float exposureCompensationEV;
    glm::vec3 floodColorSrgb;
    float floodIntensity;
};

inline constexpr int kResolution = 64;
inline constexpr int kBrickDepth = 3;
inline constexpr float kWorldSpan = 10.0f;
inline constexpr glm::vec3 kFloodlightGridPosition{39.0f, 36.0f, 25.0f};
inline constexpr glm::vec3 kNightFloodlightGridPosition{47.0f, 26.0f, 16.0f};
inline constexpr glm::vec3 kWindowLightGridPosition{32.0f, 37.0f, 20.0f};
inline constexpr glm::vec3 kWorklightGridPosition{17.0f, 26.0f, 46.0f};

inline const std::array<LightingPreset, 4>& LightingPresets() {
    // Directions point from the scene toward the light source. Colors are the
    // C16 time-of-day swatches; fill values are the only look-dev tuning values.
    // Compensation is the look on top of the key-0.18 meter: the mean over the four
    // review angles of the EV offset to the reviewed hdrmeter captures (the old
    // key-1.0 meter needed about -2.9 EV more; reports/exposure.md round 2).
    static const std::array<LightingPreset, 4> presets{{
        {"midday",          {-0.35f, 0.88f, 0.32f}, {0xfc/255.0f, 0xc6/255.0f, 0x64/255.0f}, 1.10f, 0.14f, -0.38f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"late-afternoon",  {-0.78f, 0.38f, 0.22f}, {0xe9/255.0f, 0xb1/255.0f, 0x63/255.0f}, 1.00f, 0.13f, -0.41f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"overcast",        {-0.18f, 0.96f, 0.16f}, {0xd6/255.0f, 0xdf/255.0f, 0xe7/255.0f}, 0.78f, 0.24f, -0.50f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"night-service",   {-0.28f, 0.78f, 0.38f}, {0x99/255.0f, 0xb5/255.0f, 0xca/255.0f}, 0.10f, 0.05f, -2.34f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 8.0f},
    }};
    return presets;
}

inline bool ServiceLightsEnabled(const LightingPreset& preset) {
    // The existing flood channel is authored only for night-service; use that
    // established preset input instead of adding a separate switch.
    return preset.floodIntensity > 0.0f;
}

inline const std::array<Primitive, 39>& Primitives() {
    static const std::array<Primitive, 39> primitives{{
        // Angular crawler hull, tracks, raised command module and 70/30 accent
        // plates use the working-machine palette from visual bible §3.
        {Shape::Box,       Group::Machine, Material::Hull,       {31, 27, 32}, {13, 8, 10}, 0.35f},
        {Shape::Box,       Group::Machine, Material::DarkMetal,  {20, 16, 32}, { 5, 3, 11}, 0.45f},
        {Shape::Box,       Group::Machine, Material::DarkMetal,  {42, 16, 32}, { 5, 3, 11}, 0.45f},
        {Shape::Box,       Group::Machine, Material::Structure,  {32, 37, 32}, { 8, 3,  7}, 0.45f},
        {Shape::Box,       Group::Machine, Material::Hull,       {32, 42, 32}, { 4, 2,  5}, 0.20f},
        {Shape::Box,       Group::Machine, Material::HullAccent, {32, 27, 22.7f}, { 7, 5, 0.8f}, 0.12f},
        {Shape::Box,       Group::Machine, Material::HullAccent, {32, 27, 42.7f}, { 7, 5, 0.8f}, 0.12f},
        {Shape::Box,       Group::Machine, Material::HullAccent, {18.7f, 26, 32}, {0.8f, 6, 7}, 0.12f},
        {Shape::Box,       Group::Machine, Material::HullAccent, {43.3f, 26, 32}, {0.8f, 6, 7}, 0.12f},
        {Shape::Box,       Group::Machine, Material::HullAccent, {32, 40.7f, 32}, { 5, 0.55f, 6}, 0.08f},
        {Shape::Ellipsoid, Group::Machine, Material::Glass,      {32, 36, 24}, { 3.2f, 1.8f, 1.4f}, 0.0f},
        {Shape::Box,       Group::Machine, Material::DarkMetal,  {28, 33, 21.3f}, {2.2f, 0.75f, 0.4f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::DarkMetal,  {35, 33, 21.3f}, {2.2f, 0.75f, 0.4f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Lamp,       kFloodlightGridPosition, {1.0f, 0.8f, 1.0f}, 0.12f},
        // Warm service windows run around the raised cab so every review angle
        // sees an emissive row, with the same point-light source used for spill.
        {Shape::Box,       Group::Machine, Material::Window,     {28.5f, 37, 24.75f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {32.0f, 37, 24.75f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {35.5f, 37, 24.75f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {28.5f, 37, 39.25f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {32.0f, 37, 39.25f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {35.5f, 37, 39.25f}, {0.8f, 0.65f, 0.15f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {23.75f, 37, 29.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {23.75f, 37, 32.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {23.75f, 37, 35.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {40.25f, 37, 29.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {40.25f, 37, 32.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Window,     {40.25f, 37, 35.0f}, {0.15f, 0.65f, 0.8f}, 0.05f},
        {Shape::Box,       Group::Machine, Material::Worklight,  kWorklightGridPosition, {0.7f, 0.35f, 0.5f}, 0.08f},

        // Layered rock and foliage masses based on the C16 reference-world colors.
        {Shape::Ellipsoid, Group::Rock, Material::Rock,     {10, 17, 45}, {6, 7, 6}, 0.0f},
        {Shape::Ellipsoid, Group::Rock, Material::Rock,     {15, 15, 48}, {5, 4, 5}, 0.0f},
        {Shape::Ellipsoid, Group::Rock, Material::Rock,     { 6, 14, 42}, {4, 3, 5}, 0.0f},
        {Shape::Ellipsoid, Group::Foliage, Material::Leaf,     {51, 29, 45}, {6, 8, 6}, 0.0f},
        {Shape::Ellipsoid, Group::Foliage, Material::DeepLeaf, {46, 28, 44}, {5, 6, 5}, 0.0f},
        {Shape::Ellipsoid, Group::Foliage, Material::Leaf,     {56, 30, 49}, {4, 6, 4}, 0.0f},
        {Shape::Ellipsoid, Group::Foliage, Material::DeepLeaf, {50, 34, 40}, {5, 5, 5}, 0.0f},
        {Shape::Ellipsoid, Group::Foliage, Material::Bark,     {51, 18, 45}, {2, 5, 2}, 0.0f},

        // Water is a calm, thin SDF slab: no waves or reflective response exist
        // in this renderer path. It sits in the foreground in front of the machine.
        {Shape::Box,       Group::Ground, Material::Soil,  {32, 10, 35}, {25, 1.5f, 24}, 0.35f},
        {Shape::Ellipsoid, Group::Water, Material::Water, {18, 13, 52}, {14, 0.75f, 12}, 0.0f},

        // Human-scale maintenance figure: head, torso, arms and legs as simple
        // ellipsoids, sized against the hull rather than a new character system.
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {32, 26, 50}, {1.2f, 1.4f, 1.2f}, 0.0f},
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {32, 22, 50}, {1.8f, 3.2f, 1.6f}, 0.0f},
    }};
    return primitives;
}

inline const std::array<Primitive, 4>& FigureAppendages() {
    static const std::array<Primitive, 4> appendages{{
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {29, 22, 50}, {1.0f, 2.8f, 0.9f}, 0.0f},
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {35, 22, 50}, {1.0f, 2.8f, 0.9f}, 0.0f},
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {31, 17, 50}, {0.8f, 2.5f, 0.9f}, 0.0f},
        {Shape::Ellipsoid, Group::Figure, Material::Figure, {33, 17, 50}, {0.8f, 2.5f, 0.9f}, 0.0f},
    }};
    return appendages;
}

inline float SrgbChannelToLinear(float s) {
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline glm::vec3 SrgbToLinear(const glm::vec3& color) {
    return {SrgbChannelToLinear(color.r), SrgbChannelToLinear(color.g), SrgbChannelToLinear(color.b)};
}

inline glm::vec3 Srgb8ToLinear(uint32_t rgb) {
    auto decode = [](uint32_t value) {
        return SrgbChannelToLinear(static_cast<float>(value) / 255.0f);
    };
    return {decode((rgb >> 16) & 0xffu), decode((rgb >> 8) & 0xffu), decode(rgb & 0xffu)};
}

inline std::size_t MaterialDeclarationIndex(Material material) {
    switch (material) {
        // These are declaration-order roles in the runtime pack table, not content ids.
        case Material::Hull:       return 4u;
        case Material::HullAccent: return 5u;
        case Material::Structure:  return 6u;
        case Material::DarkMetal:  return 4u;
        case Material::Glass:      return 2u;
        case Material::Rock:       return 0u;
        case Material::Bark:       return 1u;
        case Material::Soil:       return 1u;
        case Material::Leaf:       return 3u;
        case Material::DeepLeaf:   return 3u;
        case Material::Water:      return 2u;
        case Material::Figure:     return 5u;
        case Material::Lamp:       return 7u;
        case Material::Window:     return 7u;
        case Material::Worklight:  return 7u;
        case Material::Count:      return 0u;
    }
    return 0u;
}

inline std::uint32_t MaterialReference(const Vixen::SVO::RuntimeMaterialTable* table, Material material) {
    return table == nullptr ? Vixen::SVO::kMaterialRefNone
                            : table->RefAt(MaterialDeclarationIndex(material));
}

inline glm::vec3 MaterialColor(Material material,
                               const Vixen::SVO::RuntimeMaterialTable* table = nullptr) {
    if (table != nullptr) {
        const std::uint32_t ref = MaterialReference(table, material);
        if (ref != Vixen::SVO::kMaterialRefNone && ref <= table->Rows().size()) {
            const auto& row = table->Rows()[ref - 1u];
            return {row.linearAlbedo[0], row.linearAlbedo[1], row.linearAlbedo[2]};
        }
    }
    return glm::vec3(0.5f);
}

inline glm::vec3 GridToWorld(const glm::vec3& p) {
    return p * (kWorldSpan / static_cast<float>(kResolution));
}

struct MaterialRecipe {
    Material material;
    uint32_t recipeId;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    float edgeSmoothness = 0.0f;
};

inline uint32_t RecipeId(Material material) {
    return 100u + static_cast<uint32_t>(material);
}

inline float SmoothBlend(Group group) {
    switch (group) {
        case Group::Foliage: return 2.6f;
        case Group::Rock: return 0.9f;
        case Group::Figure: return 0.25f;
        default: return 0.0f;
    }
}

inline Vixen::SVO::Recipe::SdfInstruction PrimitiveInstruction(
    const Primitive& primitive, bool gridUnits = false,
    const glm::vec3& recipeOriginGrid = glm::vec3(0.0f)) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    const float scale = gridUnits ? 1.0f : kWorldSpan / static_cast<float>(kResolution);
    const glm::vec3 center = gridUnits ? primitive.center : GridToWorld(primitive.center - recipeOriginGrid);
    const glm::vec3 extent = gridUnits ? primitive.extent : GridToWorld(primitive.extent);
    SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(primitive.shape == Shape::Box
        ? SdfOpCode::RoundedBox : SdfOpCode::Ellipsoid);
    instruction.data[0] = extent.x;
    instruction.data[1] = extent.y;
    instruction.data[2] = extent.z;
    instruction.data[3] = primitive.roundness * scale;
    instruction.data[4] = center.x;
    instruction.data[5] = center.y;
    instruction.data[6] = center.z;
    return instruction;
}

inline bool IsServiceEmitter(Material material) {
    return material == Material::Lamp || material == Material::Window || material == Material::Worklight;
}

inline glm::vec3 EmitterOrigin(Material material, bool serviceLightsEnabled) {
    switch (material) {
        case Material::Lamp:
            return serviceLightsEnabled ? kNightFloodlightGridPosition : kFloodlightGridPosition;
        case Material::Window: return kWindowLightGridPosition;
        case Material::Worklight: return kWorklightGridPosition;
        default: return glm::vec3(0.0f);
    }
}

inline Primitive PrimitiveForLighting(Primitive primitive, const LightingPreset& preset) {
    if (ServiceLightsEnabled(preset) && primitive.material == Material::Lamp) {
        primitive.center = kNightFloodlightGridPosition;
        primitive.extent = {1.2f, 0.9f, 1.2f};
    }
    return primitive;
}

inline std::vector<MaterialRecipe> BuildMaterialRecipes(
    const LightingPreset& preset,
    const Vixen::SVO::RuntimeMaterialTable* materialTable = nullptr) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;
    const bool serviceLightsEnabled = ServiceLightsEnabled(preset);

    std::array<std::vector<Primitive>, static_cast<size_t>(Material::Count)> primitivesByMaterial;
    auto gather = [&](const Primitive& sourcePrimitive) {
        const Primitive primitive = PrimitiveForLighting(sourcePrimitive, preset);
        const Material recipeMaterial = primitive.material == Material::HullAccent
            ? Material::Hull : primitive.material;
        primitivesByMaterial[static_cast<size_t>(recipeMaterial)].push_back(primitive);
    };
    for (const Primitive& primitive : Primitives()) gather(primitive);
    for (const Primitive& primitive : FigureAppendages()) gather(primitive);

    std::vector<MaterialRecipe> recipes;
    for (size_t materialIndex = 0; materialIndex < primitivesByMaterial.size(); ++materialIndex) {
        const auto& primitives = primitivesByMaterial[materialIndex];
        if (primitives.empty()) continue;

        const Material material = static_cast<Material>(materialIndex);
        if (!serviceLightsEnabled && material != Material::Lamp && IsServiceEmitter(material)) continue;
        const glm::vec3 recipeOrigin = serviceLightsEnabled && IsServiceEmitter(material)
            ? EmitterOrigin(material, serviceLightsEnabled) : glm::vec3(0.0f);

        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
        const bool hullRecipe = material == Material::Hull;
        entry.boundCenter = hullRecipe ? glm::vec3(static_cast<float>(kResolution) * 0.5f)
                                       : glm::vec3(5.0f);
        entry.boundRadius = hullRecipe ? 26.0f : 9.0f;
        entry.stepRelaxation = 0.9f;
        entry.bytecode.reserve(primitives.size() * 2 - 1);
        auto appendUnion = [&](const std::vector<Primitive>& unionParts) {
            if (unionParts.empty()) return;
            entry.bytecode.push_back(PrimitiveInstruction(unionParts.front(), hullRecipe, recipeOrigin));
            for (size_t i = 1; i < unionParts.size(); ++i) {
                entry.bytecode.push_back(PrimitiveInstruction(unionParts[i], hullRecipe, recipeOrigin));
                SdfInstruction combine{};
                const float blend = SmoothBlend(unionParts[i].group);
                combine.opCode = static_cast<uint8_t>(blend > 0.0f
                    ? SdfOpCode::SmoothUnion : SdfOpCode::Union);
                combine.data[2] = blend * (hullRecipe ? 1.0f
                    : kWorldSpan / static_cast<float>(kResolution));
                entry.bytecode.push_back(combine);
            }
        };
        appendUnion(primitives);

        float edgeSmoothness = 0.0f;
        if (hullRecipe) {
            edgeSmoothness = 0.5f;
            entry.parameterValues = {
                edgeSmoothness,
                static_cast<float>(MaterialReference(materialTable, Material::Hull)),
                static_cast<float>(MaterialReference(materialTable, Material::HullAccent)),
            };

            SdfInstruction distanceOutput{};
            distanceOutput.opCode = static_cast<uint8_t>(SdfOpCode::Output);
            distanceOutput.data[1] = 1.0f;
            entry.bytecode.push_back(distanceOutput);

            std::vector<Primitive> accentParts;
            std::copy_if(primitives.begin(), primitives.end(), std::back_inserter(accentParts),
                [](const Primitive& part) { return part.material == Material::HullAccent; });
            appendUnion(accentParts);

            auto readParameter = [](uint32_t parameterIndex) {
                SdfInstruction read{};
                read.opCode = static_cast<uint8_t>(SdfOpCode::ReadParam);
                read.paramMask = 1u;
                read.data[0] = static_cast<float>(parameterIndex);
                return read;
            };
            entry.bytecode.push_back(readParameter(1u));
            entry.bytecode.push_back(readParameter(2u));
            SdfInstruction selectMaterial{};
            selectMaterial.opCode = static_cast<uint8_t>(SdfOpCode::Select);
            entry.bytecode.push_back(selectMaterial);

            SdfInstruction materialOutput{};
            materialOutput.opCode = static_cast<uint8_t>(SdfOpCode::Output);
            materialOutput.data[0] = static_cast<float>(
                Vixen::SVO::RecipeOutputInstructionChannelFor(Vixen::SVO::SEM_MATERIAL_ID));
            materialOutput.data[1] = 1.0f;
            entry.bytecode.push_back(materialOutput);

            entry.bytecode.push_back(readParameter(0u));
            SdfInstruction smoothnessOutput{};
            smoothnessOutput.opCode = static_cast<uint8_t>(SdfOpCode::Output);
            smoothnessOutput.data[0] = static_cast<float>(
                Vixen::SVO::RecipeOutputInstructionChannelFor(Vixen::SVO::SEM_EDGE_SMOOTHNESS));
            smoothnessOutput.data[1] = 1.0f;
            entry.bytecode.push_back(smoothnessOutput);
        }

        recipes.push_back({material, RecipeId(material), std::move(entry), edgeSmoothness});
    }
    return recipes;
}

inline Vixen::SVO::RecipeBakeResult BakeStoredHull(const MaterialRecipe& recipe) {
    Vixen::SVO::RecipeRegistry registry;
    const auto registration = registry.Register(recipe.recipeId, recipe.entry);
    if (registration != Vixen::SVO::RecipeRegistry::RegisterResult::Ok)
        throw std::runtime_error("could not register the look-dev hull bake recipe");

    Vixen::SVO::RecipeBakeConfig config;
    config.center = glm::vec3(0.0f);
    config.defaultResolution = kResolution;
    config.defaultBand = 2.5f;
    config.defaultBrickDepth = kBrickDepth;
    return Vixen::SVO::BakeRegistryToPool(registry, config);
}

struct HullCoverageMeasurement {
    std::uint32_t majorVoxels = 0u;
    std::uint32_t minorVoxels = 0u;
    float majorFraction = 0.0f;
    float minorFraction = 0.0f;
};

inline HullCoverageMeasurement MeasureHullMaterialCoverage(
    const Vixen::SVO::BakedRecipeOutputSet& outputSet,
    std::uint32_t majorRef, std::uint32_t minorRef, float surfaceHalfWidth = 0.5f) {
    std::uint8_t distanceOffset = std::numeric_limits<std::uint8_t>::max();
    std::uint8_t materialOffset = std::numeric_limits<std::uint8_t>::max();
    for (const auto& output : outputSet.outputs) {
        const auto* channel = Vixen::SVO::FindRecipeOutputChannelForDescriptor(output.channel);
        if (channel == nullptr) continue;
        if (channel->semantic == Vixen::SVO::SEM_SDF) distanceOffset = output.valueOffset;
        if (channel->semantic == Vixen::SVO::SEM_MATERIAL_ID) materialOffset = output.valueOffset;
    }
    if (distanceOffset == std::numeric_limits<std::uint8_t>::max() ||
        materialOffset == std::numeric_limits<std::uint8_t>::max()) return {};

    HullCoverageMeasurement result;
    for (int z = 0; z < outputSet.resolution; ++z) {
        for (int y = 0; y < outputSet.resolution; ++y) {
            for (int x = 0; x < outputSet.resolution; ++x) {
                const float* values = outputSet.sample(glm::ivec3(x, y, z));
                if (values == nullptr || std::abs(values[distanceOffset]) > surfaceHalfWidth) continue;
                const auto materialRef = static_cast<std::uint32_t>(std::lround(values[materialOffset]));
                if (materialRef == majorRef) ++result.majorVoxels;
                else if (materialRef == minorRef) ++result.minorVoxels;
            }
        }
    }
    const std::uint32_t total = result.majorVoxels + result.minorVoxels;
    if (total != 0u) {
        result.majorFraction = static_cast<float>(result.majorVoxels) / static_cast<float>(total);
        result.minorFraction = static_cast<float>(result.minorVoxels) / static_cast<float>(total);
    }
    return result;
}

inline std::vector<Vixen::SVO::BodyInstanceGpu> BodyInstances(
    const std::vector<MaterialRecipe>& recipes, const LightingPreset& preset,
    const Vixen::SVO::RuntimeMaterialTable* materialTable = nullptr,
    bool storedHull = false) {
    const bool serviceLightsEnabled = ServiceLightsEnabled(preset);
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    instances.reserve(recipes.size());
    for (const MaterialRecipe& recipe : recipes) {
        // The stored fixture contains only the hull recipe. Keep the other look-dev bodies
        // procedural, and map the hull's 64-cell bake into the same 10-unit world span as the
        // ordinary camera. Previously every recipe was marked stored and left in grid space,
        // so the camera could render a valid frame while the entire stored asset sat outside it.
        const bool useStoredHull = storedHull && recipe.material == Material::Hull;
        const glm::vec3 color = useStoredHull ? glm::vec3(1.0f)
                                            : MaterialColor(recipe.material, materialTable);
        const glm::vec3 instanceOrigin = recipe.material == Material::Lamp ||
                (serviceLightsEnabled && IsServiceEmitter(recipe.material))
            ? GridToWorld(EmitterOrigin(recipe.material, serviceLightsEnabled)) : glm::vec3(0.0f);
        Vixen::SVO::BodyInstanceGpu instance{};

        // Keep each procedural body at its authored origin and unit size while
        // routing all placement through the R424 affine transform pair.
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), instanceOrigin);
        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        const float storedWorldScale = kWorldSpan / static_cast<float>(kResolution);
        const glm::mat4 scale = glm::scale(glm::mat4(1.0f),
                                            glm::vec3(useStoredHull ? storedWorldScale : 1.0f));
        Vixen::SVO::SetInstanceTransform(instance, translation * rotation * scale);

        Vixen::SVO::BodyInstanceMaterialGpu& materialData = instance.material;
        materialData.color[0] = color.r;
        materialData.color[1] = color.g;
        materialData.color[2] = color.b;
        materialData.providerKind = useStoredHull ? 0u : 1u;
        materialData.octreeIndex = 0u;
        materialData.recipeId = recipe.recipeId;
        if (useStoredHull) materialData.recipeParams[0] = recipe.edgeSmoothness;
        if (recipe.material == Material::Lamp) materialData.recipeParams[3] = serviceLightsEnabled ? 0.25f : 2.0f;
        if (recipe.material == Material::Window || recipe.material == Material::Worklight)
            materialData.recipeParams[3] = 0.25f;
        instances.push_back(instance);
    }
    return instances;
}

inline std::vector<Vixen::Gpu::Light> Lights(const LightingPreset& preset) {
    auto normalized = [](glm::vec3 v) { return glm::normalize(v); };
    const glm::vec3 keyColor = SrgbToLinear(preset.keyColorSrgb);
    const glm::vec3 floodColor = SrgbToLinear(preset.floodColorSrgb);
    const glm::vec3 serviceColor = Srgb8ToLinear(0xe9bf6b);

    Vixen::Gpu::Light key{};
    const glm::vec3 keyDirection = normalized(preset.keyDirection);
    key.direction_or_positionX = keyDirection.x;
    key.direction_or_positionY = keyDirection.y;
    key.direction_or_positionZ = keyDirection.z;
    key.kind = 0u;
    key.radianceX = keyColor.x * preset.keyIntensity;
    key.radianceY = keyColor.y * preset.keyIntensity;
    key.radianceZ = keyColor.z * preset.keyIntensity;

    Vixen::Gpu::Light flood{};
    const bool serviceLightsEnabled = ServiceLightsEnabled(preset);
    const glm::vec3 floodPosition = GridToWorld(serviceLightsEnabled
        ? kNightFloodlightGridPosition : kFloodlightGridPosition);
    flood.direction_or_positionX = floodPosition.x;
    flood.direction_or_positionY = floodPosition.y;
    flood.direction_or_positionZ = floodPosition.z;
    flood.kind = 1u;
    flood.radianceX = floodColor.x * preset.floodIntensity;
    flood.radianceY = floodColor.y * preset.floodIntensity;
    flood.radianceZ = floodColor.z * preset.floodIntensity;
    flood.range = 7.0f;
    if (!serviceLightsEnabled) return {key, flood};

    auto pointLight = [](const glm::vec3& position, const glm::vec3& color, float intensity, float range) {
        Vixen::Gpu::Light light{};
        const glm::vec3 worldPosition = GridToWorld(position);
        light.direction_or_positionX = worldPosition.x;
        light.direction_or_positionY = worldPosition.y;
        light.direction_or_positionZ = worldPosition.z;
        light.kind = 1u;
        light.radianceX = color.r * intensity;
        light.radianceY = color.g * intensity;
        light.radianceZ = color.b * intensity;
        light.range = range;
        return light;
    };

    Vixen::Gpu::Light windows = pointLight(kWindowLightGridPosition, serviceColor, 3.5f, 8.0f);
    Vixen::Gpu::Light worklight = pointLight(kWorklightGridPosition, floodColor, 5.0f, 8.0f);
    flood.range = 8.0f;
    return {key, windows, worklight, flood};
}

inline std::vector<float> CelSpillPurposeScales(const LightingPreset& preset) {
    return ServiceLightsEnabled(preset)
        ? std::vector<float>{1.0f, 4.0f, 4.0f, 4.0f}
        : std::vector<float>{1.0f, 1.0f};
}

} // namespace Vixen::App::Lookdev
