/**
 * @file test_prevcameraconfig_sdi_parity.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::PrevCameraConfig` C++ layout MUST match
 *        the layout the shipped `BodyInstanceRayMarch.comp` actually compiles to.
 *
 * Sibling of test_accumulationconfig_sdi_parity.cpp — reflects the compiled shader via
 * ShaderManagement's SpirvReflector (SPIRV-Reflect) and asserts the reflected
 * PrevCameraConfig member's per-field byte offsets match the C++ struct's offsetof
 * values. Pure CPU — reflection only, no Vulkan device / no render.
 *
 * GLSL_RAYMARCH_SPV (path to the build-time-compiled BodyInstanceRayMarch.spv) is
 * supplied by CMake, reusing the same `body_instance_raymarch_spv` custom target
 * test_accumulationconfig_sdi_parity/test_shadowconfig_sdi_parity already build against.
 */

#include <gtest/gtest.h>

#include "SpirvReflector.h"
#include "SpirvReflectionData.h"
#include "ShaderStage.h"

#include "Generated/PrevCameraConfig.g.h"

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

// Catches a future edit to BodyInstanceRayMarch.comp's PrevCameraConfig /
// PrevCameraConfigSSBO (or the [GpuStruct] schema regen) that desyncs the shader from the
// C++ struct — the same class of bug test_accumulationconfig_sdi_parity / test_shadowconfig_sdi_parity guard for.
TEST(PrevCameraConfigSdiParity, ReflectedLayoutMatchesCppStruct) {
    using Vixen::Gpu::PrevCameraConfig;

    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Locate the prevCameraConfig SSBO member (its type is the PrevCameraConfig struct).
    const SpirvStructMember* prevCameraConfig = FindMember(*refl, "prevCameraConfig");
    ASSERT_NE(prevCameraConfig, nullptr)
        << "reflection has no member 'prevCameraConfig' — PrevCameraConfigSSBO changed or names were stripped";

    std::cout << "[reflected PrevCameraConfig] size=" << prevCameraConfig->type.sizeInBytes
              << " members=" << prevCameraConfig->members.size() << "\n";
    for (const auto& m : prevCameraConfig->members) {
        std::cout << "    ." << m.name << " offset=" << m.offset << "\n";
    }

    // (a) Struct SIZE == C++ struct size (64 B, per the codegen's own static_asserts).
    EXPECT_EQ(static_cast<std::size_t>(prevCameraConfig->type.sizeInBytes), sizeof(PrevCameraConfig))
        << "PrevCameraConfig size (" << prevCameraConfig->type.sizeInBytes
        << ") != sizeof(PrevCameraConfig) (" << sizeof(PrevCameraConfig) << ") — std430 drift";
    EXPECT_EQ(prevCameraConfig->type.sizeInBytes, 64u)
        << "PrevCameraConfig size must be 64 (Inc2 M3 contract — a single mat4)";

    // (b) Per-field offsets.
    ASSERT_FALSE(prevCameraConfig->members.empty())
        << "SpirvReflector did not surface PrevCameraConfig members";

    struct Field { const char* name; std::size_t cppOffset; };
    const Field fields[] = {
        {"prevViewProj", offsetof(PrevCameraConfig, prevViewProj)},
    };
    for (const auto& f : fields) {
        const SpirvStructMember* sm = FindSubMember(*prevCameraConfig, f.name);
        ASSERT_NE(sm, nullptr) << "shader PrevCameraConfig is missing field '" << f.name << "'";
        EXPECT_EQ(static_cast<std::size_t>(sm->offset), f.cppOffset)
            << "offset drift on '" << f.name << "': shader=" << sm->offset
            << " C++=" << f.cppOffset;
    }
}
