/**
 * @file test_lightingconfig_sdi_parity.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::LightingConfig` C++ layout MUST match
 *        the layout the shipped `BodyInstanceRayMarch.comp` actually compiles to.
 *
 * Sampled Lighting Inc0 M1 (test_lightingconfig_parity.cpp) proved the generated header's
 * own static_asserts hold, but had no shader consumer to reflect against yet. M3 wires
 * LightingConfigSSBO (binding 16) into BodyInstanceRayMarch.comp, so this is the promised
 * SPIR-V-reflection sibling — mirrors test_octree_config_sdi_parity.cpp's pattern exactly:
 * reflect the compiled shader via ShaderManagement's SpirvReflector (SPIRV-Reflect) and
 * assert the reflected LightingConfig member's per-field byte offsets match the C++
 * struct's offsetof values. Pure CPU — reflection only, no Vulkan device / no render.
 *
 * GLSL_RAYMARCH_SPV (path to the build-time-compiled BodyInstanceRayMarch.spv) is
 * supplied by CMake, reusing the same `body_instance_raymarch_spv` custom target
 * test_octree_config_sdi_parity already builds against.
 */

#include <gtest/gtest.h>

#include "SpirvReflector.h"
#include "SpirvReflectionData.h"
#include "ShaderStage.h"

#include "Generated/LightingConfig.g.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

using namespace ShaderManagement;

namespace {

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

// First member named `memberName` across all reflected struct definitions.
const SpirvStructMember* FindMember(const SpirvReflectionData& r, const std::string& memberName) {
    for (const auto& sd : r.structDefinitions)
        for (const auto& m : sd.members)
            if (m.name == memberName) return &m;
    return nullptr;
}

// A named sub-member of a struct member (by name).
const SpirvStructMember* FindSubMember(const SpirvStructMember& parent, const std::string& name) {
    for (const auto& m : parent.members)
        if (m.name == name) return &m;
    return nullptr;
}

}  // namespace

// Catches a future edit to BodyInstanceRayMarch.comp's LightingConfig / LightingConfigSSBO
// (or the [GpuStruct] schema regen) that desyncs the shader from the C++ struct — the same
// class of bug test_octree_config_sdi_parity guards for OctreeConfig.
TEST(LightingConfigSdiParity, ReflectedLayoutMatchesCppStruct) {
    using Vixen::Gpu::Light;
    using Vixen::Gpu::LightingConfig;

    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Locate the lightingConfig SSBO member (its type is the LightingConfig struct).
    const SpirvStructMember* lightingConfig = FindMember(*refl, "lightingConfig");
    ASSERT_NE(lightingConfig, nullptr)
        << "reflection has no member 'lightingConfig' — LightingConfigSSBO changed or names were stripped";

    std::cout << "[reflected LightingConfig] size=" << lightingConfig->type.sizeInBytes
              << " members=" << lightingConfig->members.size() << "\n";
    for (const auto& m : lightingConfig->members) {
        std::cout << "    ." << m.name << " offset=" << m.offset << "\n";
    }

    // (a) Struct SIZE == C++ struct size (the std430 layout Inc0 M1 pinned at 144 B).
    EXPECT_EQ(static_cast<std::size_t>(lightingConfig->type.sizeInBytes), sizeof(LightingConfig))
        << "LightingConfig size (" << lightingConfig->type.sizeInBytes
        << ") != sizeof(LightingConfig) (" << sizeof(LightingConfig) << ") — std430 drift";
    EXPECT_EQ(lightingConfig->type.sizeInBytes, 144u)
        << "LightingConfig size must be 144 (Inc0 M1 contract)";

    // (b) Top-level field offsets.
    ASSERT_FALSE(lightingConfig->members.empty())
        << "SpirvReflector did not surface LightingConfig members";

    const SpirvStructMember* lightCount = FindSubMember(*lightingConfig, "lightCount");
    ASSERT_NE(lightCount, nullptr) << "shader LightingConfig is missing field 'lightCount'";
    EXPECT_EQ(static_cast<std::size_t>(lightCount->offset), offsetof(LightingConfig, lightCount));

    const SpirvStructMember* ambientIntensity = FindSubMember(*lightingConfig, "ambientIntensity");
    ASSERT_NE(ambientIntensity, nullptr) << "shader LightingConfig is missing field 'ambientIntensity'";
    EXPECT_EQ(static_cast<std::size_t>(ambientIntensity->offset), offsetof(LightingConfig, ambientIntensity));

    const SpirvStructMember* lights = FindSubMember(*lightingConfig, "lights");
    ASSERT_NE(lights, nullptr) << "shader LightingConfig is missing field 'lights'";
    EXPECT_EQ(static_cast<std::size_t>(lights->offset), offsetof(LightingConfig, lights));

    // (c) Nested Light element stride == C++ Light size (32 B) — the array-stride proof.
    // (lights->type.sizeInBytes is the WHOLE array's byte size — kMaxLightsInc0 * stride —
    // not a single element's; arrayStride is the per-element field.)
    ASSERT_FALSE(lights->members.empty())
        << "SpirvReflector did not recurse into the nested Light struct — recursion regressed";
    EXPECT_EQ(static_cast<std::size_t>(lights->arrayStride), sizeof(Light))
        << "lights[] array stride (" << lights->arrayStride
        << ") != sizeof(Light) (" << sizeof(Light) << ") — std430 restride drift";

    struct Field { const char* name; std::size_t cppOffset; };
    const Field lightFields[] = {
        {"direction_or_position", offsetof(Light, direction_or_positionX)},  // 0
        {"kind",                  offsetof(Light, kind)},                   // 12
        {"radiance",               offsetof(Light, radianceX)},             // 16
        {"range",                 offsetof(Light, range)},                  // 28
    };
    for (const auto& f : lightFields) {
        const SpirvStructMember* sm = FindSubMember(*lights, f.name);
        ASSERT_NE(sm, nullptr) << "shader Light is missing field '" << f.name << "'";
        EXPECT_EQ(static_cast<std::size_t>(sm->offset), f.cppOffset)
            << "offset drift on '" << f.name << "': shader=" << sm->offset
            << " C++=" << f.cppOffset;
    }
}
