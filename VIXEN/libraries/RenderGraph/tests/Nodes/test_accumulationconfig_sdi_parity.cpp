/**
 * @file test_accumulationconfig_sdi_parity.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::AccumulationConfig` C++ layout MUST match
 *        the layout the shipped `BodyInstanceRayMarch.comp` actually compiles to.
 *
 * Sibling of test_shadowconfig_sdi_parity.cpp — reflects the compiled shader via
 * ShaderManagement's SpirvReflector (SPIRV-Reflect) and asserts the reflected
 * AccumulationConfig member's per-field byte offsets match the C++ struct's offsetof
 * values. Pure CPU — reflection only, no Vulkan device / no render.
 *
 * GLSL_RAYMARCH_SPV (path to the build-time-compiled BodyInstanceRayMarch.spv) is
 * supplied by CMake, reusing the same `body_instance_raymarch_spv` custom target
 * test_shadowconfig_sdi_parity/test_lightingconfig_sdi_parity/test_octree_config_sdi_parity
 * already build against.
 */

#include <gtest/gtest.h>

#include "SpirvReflector.h"
#include "SpirvReflectionData.h"
#include "ShaderStage.h"

#include "Generated/AccumulationConfig.g.h"

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

// Catches a future edit to BodyInstanceRayMarch.comp's AccumulationConfig /
// AccumulationConfigSSBO (or the [GpuStruct] schema regen) that desyncs the shader from the
// C++ struct — the same class of bug test_shadowconfig_sdi_parity / test_lightingconfig_sdi_parity guard for.
TEST(AccumulationConfigSdiParity, ReflectedLayoutMatchesCppStruct) {
    using Vixen::Gpu::AccumulationConfig;

    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Locate the accumulationConfig SSBO member (its type is the AccumulationConfig struct).
    const SpirvStructMember* accumulationConfig = FindMember(*refl, "accumulationConfig");
    ASSERT_NE(accumulationConfig, nullptr)
        << "reflection has no member 'accumulationConfig' — AccumulationConfigSSBO changed or names were stripped";

    std::cout << "[reflected AccumulationConfig] size=" << accumulationConfig->type.sizeInBytes
              << " members=" << accumulationConfig->members.size() << "\n";
    for (const auto& m : accumulationConfig->members) {
        std::cout << "    ." << m.name << " offset=" << m.offset << "\n";
    }

    // (a) Struct SIZE == C++ struct size (16 B, per the codegen's own static_asserts).
    EXPECT_EQ(static_cast<std::size_t>(accumulationConfig->type.sizeInBytes), sizeof(AccumulationConfig))
        << "AccumulationConfig size (" << accumulationConfig->type.sizeInBytes
        << ") != sizeof(AccumulationConfig) (" << sizeof(AccumulationConfig) << ") — std430 drift";
    EXPECT_EQ(accumulationConfig->type.sizeInBytes, 16u)
        << "AccumulationConfig size must be 16 (Inc2 M1 contract)";

    // (b) Per-field offsets.
    ASSERT_FALSE(accumulationConfig->members.empty())
        << "SpirvReflector did not surface AccumulationConfig members";

    struct Field { const char* name; std::size_t cppOffset; };
    const Field fields[] = {
        {"enabled",       offsetof(AccumulationConfig, enabled)},
        {"alpha",         offsetof(AccumulationConfig, alpha)},
        {"maxFrames",     offsetof(AccumulationConfig, maxFrames)},
        {"resetOnMotion", offsetof(AccumulationConfig, resetOnMotion)},
    };
    for (const auto& f : fields) {
        const SpirvStructMember* sm = FindSubMember(*accumulationConfig, f.name);
        ASSERT_NE(sm, nullptr) << "shader AccumulationConfig is missing field '" << f.name << "'";
        EXPECT_EQ(static_cast<std::size_t>(sm->offset), f.cppOffset)
            << "offset drift on '" << f.name << "': shader=" << sm->offset
            << " C++=" << f.cppOffset;
    }
}
