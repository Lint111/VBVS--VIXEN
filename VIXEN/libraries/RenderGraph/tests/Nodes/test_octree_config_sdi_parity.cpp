/**
 * @file test_octree_config_sdi_parity.cpp
 * @brief Drift-guard: the C++ `Vixen::SVO::OctreeConfig` element size MUST match the
 *        layout the shipped `BodyInstanceRayMarch.comp` actually compiles to.
 *
 * The shader-side `OctreeConfig` struct and the `configs[]` SSBO at binding 5 are
 * hand-maintained to be byte-identical to the C++ struct (the shader even says so:
 * "Struct layout must be byte-for-byte identical to the C++ OctreeConfig (432 B)").
 * That contract used to be verified BY HAND — reading `OpMemberDecorate ArrayStride 432`
 * out of the disassembly and hand-padding the C++ struct. A future edit to either side
 * silently breaks it (the std140->std430 restride trap the recipe pool work hit, where a
 * tighter element size misaligns configs[k>0] and those bodies render nothing).
 *
 * This makes the contract a FRAMEWORK-DRIVEN, repeatable gate: it reflects the built
 * SPIR-V via ShaderManagement's SpirvReflector (SPIRV-Reflect) and asserts the reflected
 * `configs[]` element size equals `sizeof(OctreeConfig)` (== 432). Pure CPU — reflection
 * only, no Vulkan device / no render.
 *
 * SCOPE / known limitation: the current `SpirvReflector` surfaces only the top-level
 * members of a descriptor block; it does NOT recurse into a nested struct member (here,
 * the `OctreeConfig` element of `configs[]`). So this guards the ELEMENT SIZE / std430
 * stride (the failure class that actually bit us) but not per-field offsets within the
 * struct. Per-field offset parity needs the reflector to surface nested-struct members —
 * tracked in [[Runtime-Kernel-Pipeline-Direction-2026-06]] as a follow-up. The C++ side's
 * field offsets remain pinned by the `static_assert(offsetof(...))` battery in
 * ShellOctreeGpu.h.
 *
 * GLSL_RAYMARCH_SPV (path to the build-time-compiled BodyInstanceRayMarch.spv) is
 * supplied by CMake, reusing the same `body_instance_raymarch_spv` custom target the
 * render gates already build.
 */

#include <gtest/gtest.h>

#include "SpirvReflector.h"
#include "SpirvReflectionData.h"
#include "ShaderStage.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::OctreeConfig

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

}  // namespace

// Catches a future edit to BodyInstanceRayMarch.comp's OctreeConfig / OctreeConfigsSSBO
// (or to the C++ struct) that changes the std430 element size — the exact failure the
// recipe pool work hit and verified by hand.
TEST(OctreeConfigSdiParity, ReflectedElementSizeMatchesCppStruct) {
    using Vixen::SVO::OctreeConfig;

    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Compact dump of what the shader compiled to — keeps the observed layout in the test
    // log next to the asserts (and is the first thing to read if this ever fails). Includes
    // descriptor sets so a future per-field strengthening has the binding map on hand.
    std::cout << "[reflected struct definitions]\n";
    for (const auto& sd : refl->structDefinitions) {
        std::cout << "  struct '" << sd.name << "' size=" << sd.sizeInBytes
                  << " members=" << sd.members.size() << "\n";
        for (const auto& m : sd.members) {
            std::cout << "    ." << m.name << " offset=" << m.offset
                      << " arrayStride=" << m.arrayStride
                      << " elemSize=" << m.type.sizeInBytes << "\n";
        }
    }
    std::cout << "[descriptor sets]\n";
    for (const auto& kv : refl->descriptorSets) {
        for (const auto& b : kv.second) {
            std::cout << "  set=" << kv.first << " binding=" << b.binding
                      << " name='" << b.name << "' structDefIndex=" << b.structDefIndex << "\n";
        }
    }

    // The element size of the configs[] SSBO member is the std430 stride the recipe pool
    // work verified by hand (432). It must equal the C++ element size, or configs[k>0]
    // misaligns on the GPU.
    const SpirvStructMember* configs = FindMember(*refl, "configs");
    ASSERT_NE(configs, nullptr)
        << "reflection has no member 'configs' — OctreeConfigsSSBO changed or names were stripped";

    EXPECT_EQ(static_cast<std::size_t>(configs->type.sizeInBytes), sizeof(OctreeConfig))
        << "configs[] element size (" << configs->type.sizeInBytes
        << ") != sizeof(OctreeConfig) (" << sizeof(OctreeConfig) << ") — std430 restride drift";
    EXPECT_EQ(configs->type.sizeInBytes, 432u)
        << "configs[] element size must be 432 (Inc3 M3 / I3.2 contract)";
}
