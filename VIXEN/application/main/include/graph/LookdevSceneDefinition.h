#pragma once

#include "Recipe/RecipeRegistry.h"
#include "ShellOctreeGpu.h"
#include "Generated/LightingConfig.g.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Vixen::App::Lookdev {

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

inline const std::array<LightingPreset, 4>& LightingPresets() {
    // Directions point from the scene toward the light source. Colors are the
    // C16 time-of-day swatches; fill values are the only look-dev tuning values.
    static const std::array<LightingPreset, 4> presets{{
        {"midday",          {-0.35f, 0.88f, 0.32f}, {0xfc/255.0f, 0xc6/255.0f, 0x64/255.0f}, 1.10f, 0.14f, -3.25f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"late-afternoon",  {-0.78f, 0.38f, 0.22f}, {0xe9/255.0f, 0xb1/255.0f, 0x63/255.0f}, 1.00f, 0.13f, -3.25f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"overcast",        {-0.18f, 0.96f, 0.16f}, {0xd6/255.0f, 0xdf/255.0f, 0xe7/255.0f}, 0.78f, 0.24f, -3.50f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 0.0f},
        {"night-service",   {-0.28f, 0.78f, 0.38f}, {0x99/255.0f, 0xb5/255.0f, 0xca/255.0f}, 0.10f, 0.05f, -4.75f, {0xf9/255.0f, 0xb5/255.0f, 0x5c/255.0f}, 6.0f},
    }};
    return presets;
}

inline const std::array<Primitive, 26>& Primitives() {
    static const std::array<Primitive, 26> primitives{{
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

inline glm::vec3 MaterialColor(Material material) {
    switch (material) {
        case Material::Hull:       return Srgb8ToLinear(0xe8e1da); // bible §3: hull off-white
        case Material::HullAccent: return Srgb8ToLinear(0xfa9f4e); // bible §3: accent orange
        case Material::Structure:  return Srgb8ToLinear(0x707276); // bible §3: structure grey
        case Material::DarkMetal:  return Srgb8ToLinear(0x3c4045); // bible §3: machinery dark
        case Material::Glass:      return Srgb8ToLinear(0x5e768c); // bible §3: glass
        case Material::Rock:       return Srgb8ToLinear(0xeac794); // C26: sunlit stone
        case Material::Bark:       return Srgb8ToLinear(0x6c4a37); // C26: bark
        case Material::Soil:       return Srgb8ToLinear(0x6c4a37); // C26: bark/earth brown
        case Material::Leaf:       return Srgb8ToLinear(0x7f945b); // C22: leaf green
        case Material::DeepLeaf:   return Srgb8ToLinear(0x2e5234); // G2: deep foliage
        case Material::Water:      return Srgb8ToLinear(0x62ade0); // C26: river blue
        case Material::Figure:     return Srgb8ToLinear(0xfcca5b); // safety yellow scale cue
        case Material::Lamp:       return Srgb8ToLinear(0xf9b55c); // lighting track: sodium pool
    }
    return Srgb8ToLinear(0x808080);
}

inline glm::vec3 GridToWorld(const glm::vec3& p) {
    return p * (kWorldSpan / static_cast<float>(kResolution));
}

struct MaterialRecipe {
    Material material;
    uint32_t recipeId;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
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

inline Vixen::SVO::Recipe::SdfInstruction PrimitiveInstruction(const Primitive& primitive) {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    const float scale = kWorldSpan / static_cast<float>(kResolution);
    const glm::vec3 center = GridToWorld(primitive.center);
    const glm::vec3 extent = GridToWorld(primitive.extent);
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

inline std::vector<MaterialRecipe> BuildMaterialRecipes() {
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;

    std::array<std::vector<Primitive>, static_cast<size_t>(Material::Count)> primitivesByMaterial;
    auto gather = [&](const Primitive& primitive) {
        primitivesByMaterial[static_cast<size_t>(primitive.material)].push_back(primitive);
    };
    for (const Primitive& primitive : Primitives()) gather(primitive);
    for (const Primitive& primitive : FigureAppendages()) gather(primitive);

    std::vector<MaterialRecipe> recipes;
    for (size_t materialIndex = 0; materialIndex < primitivesByMaterial.size(); ++materialIndex) {
        const auto& primitives = primitivesByMaterial[materialIndex];
        if (primitives.empty()) continue;

        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
        entry.boundCenter = glm::vec3(5.0f);
        entry.boundRadius = 9.0f;
        entry.stepRelaxation = 0.9f;
        entry.bytecode.reserve(primitives.size() * 2 - 1);
        entry.bytecode.push_back(PrimitiveInstruction(primitives.front()));
        for (size_t i = 1; i < primitives.size(); ++i) {
            entry.bytecode.push_back(PrimitiveInstruction(primitives[i]));
            SdfInstruction combine{};
            const float blend = SmoothBlend(primitives[i].group);
            combine.opCode = static_cast<uint8_t>(blend > 0.0f
                ? SdfOpCode::SmoothUnion : SdfOpCode::Union);
            combine.data[2] = blend * (kWorldSpan / static_cast<float>(kResolution));
            entry.bytecode.push_back(combine);
        }

        const Material material = static_cast<Material>(materialIndex);
        recipes.push_back({material, RecipeId(material), std::move(entry)});
    }
    return recipes;
}

inline std::vector<Vixen::SVO::BodyInstanceGpu> BodyInstances(
    const std::vector<MaterialRecipe>& recipes) {
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    instances.reserve(recipes.size());
    for (const MaterialRecipe& recipe : recipes) {
        const glm::vec3 color = MaterialColor(recipe.material);
        const glm::vec3 instanceOrigin = recipe.material == Material::Lamp
            ? GridToWorld(kFloodlightGridPosition) : glm::vec3(0.0f);
        Vixen::SVO::BodyInstanceGpu instance{};

        // Keep each procedural body at its authored origin and unit size while
        // routing all placement through the R424 affine transform pair.
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), instanceOrigin);
        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
        Vixen::SVO::SetInstanceTransform(instance, translation * rotation * scale);

        Vixen::SVO::BodyInstanceMaterialGpu& materialData = instance.material;
        materialData.color[0] = color.r;
        materialData.color[1] = color.g;
        materialData.color[2] = color.b;
        materialData.providerKind = 1u;
        materialData.recipeId = recipe.recipeId;
        if (recipe.material == Material::Lamp) materialData.recipeParams[3] = 2.0f;
        instances.push_back(instance);
    }
    return instances;
}

inline std::vector<Vixen::Gpu::Light> Lights(const LightingPreset& preset) {
    auto normalized = [](glm::vec3 v) { return glm::normalize(v); };
    const glm::vec3 keyColor = SrgbToLinear(preset.keyColorSrgb);
    const glm::vec3 floodColor = SrgbToLinear(preset.floodColorSrgb);

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
    const glm::vec3 floodPosition = GridToWorld(kFloodlightGridPosition);
    flood.direction_or_positionX = floodPosition.x;
    flood.direction_or_positionY = floodPosition.y;
    flood.direction_or_positionZ = floodPosition.z;
    flood.kind = 1u;
    flood.radianceX = floodColor.x * preset.floodIntensity;
    flood.radianceY = floodColor.y * preset.floodIntensity;
    flood.radianceZ = floodColor.z * preset.floodIntensity;
    flood.range = 7.0f;
    return {key, flood};
}

} // namespace Vixen::App::Lookdev
