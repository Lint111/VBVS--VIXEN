/**
 * @file test_octree_config_sdi_parity.cpp
 * @brief Drift-guard: the C++ `Vixen::SVO::OctreeConfig` layout MUST match the layout
 *        the shipped `BodyInstanceRayMarch.comp` actually compiles to.
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
 * SPIR-V via ShaderManagement's SpirvReflector (SPIRV-Reflect) and asserts BOTH
 *   (a) the `configs[]` element SIZE == sizeof(OctreeConfig) (== 432, the std430 stride), and
 *   (b) each field the shader reads sits at the same byte OFFSET as the C++ struct.
 * SpirvReflector recurses nested SSBO struct members, so the `OctreeConfig` element's
 * fields are surfaced with per-field offsets. Pure CPU — reflection only, no Vulkan
 * device / no render.
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

// A named sub-member of a struct member (by name).
const SpirvStructMember* FindSubMember(const SpirvStructMember& parent, const std::string& name) {
    for (const auto& m : parent.members)
        if (m.name == name) return &m;
    return nullptr;
}

}  // namespace

// Catches a future edit to BodyInstanceRayMarch.comp's OctreeConfig / OctreeConfigsSSBO
// (or to the C++ struct) that desyncs the two — the exact failure the recipe pool work
// hit and verified by hand.
TEST(OctreeConfigSdiParity, ReflectedLayoutMatchesCppStruct) {
    using Vixen::SVO::OctreeConfig;

    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Locate the configs[] SSBO member (its element type is the OctreeConfig struct).
    const SpirvStructMember* configs = FindMember(*refl, "configs");
    ASSERT_NE(configs, nullptr)
        << "reflection has no member 'configs' — OctreeConfigsSSBO changed or names were stripped";

    // Compact dump of the reflected OctreeConfig element next to the asserts (first thing
    // to read if this fails).
    std::cout << "[reflected OctreeConfig element] size=" << configs->type.sizeInBytes
              << " members=" << configs->members.size() << "\n";
    for (const auto& m : configs->members) {
        std::cout << "    ." << m.name << " offset=" << m.offset << "\n";
    }

    // (a) Element SIZE == C++ element size (the std430 stride the recipe pool work verified
    // by hand; a tighter element misaligns configs[k>0] on the GPU).
    EXPECT_EQ(static_cast<std::size_t>(configs->type.sizeInBytes), sizeof(OctreeConfig))
        << "configs[] element size (" << configs->type.sizeInBytes
        << ") != sizeof(OctreeConfig) (" << sizeof(OctreeConfig) << ") — std430 restride drift";
    EXPECT_EQ(configs->type.sizeInBytes, 432u)
        << "configs[] element size must be 432 (Inc3 M3 / I3.2 contract)";

    // (b) Per-field byte offsets. The SpirvReflector now recurses nested struct members.
    ASSERT_FALSE(configs->members.empty())
        << "SpirvReflector did not surface nested OctreeConfig members — recursion regressed";

    // Fields the shader reads, present identically on both sides. (traceBoundsMin/Max —
    // Baked-Perf M5 Task 5.1, formerly gridMin/gridMax — are vec3 in the shader vs scalar
    // triples in C++, and channels[] is uvec4 vs ChannelDesc — same bytes, different member
    // shape — so they are intentionally not name-matched here; the element-size check above
    // covers the total.)
    struct Field { const char* name; std::size_t cppOffset; };
    const Field fields[] = {
        {"localToWorld",      offsetof(OctreeConfig, localToWorld)},      //  64
        {"worldToLocal",      offsetof(OctreeConfig, worldToLocal)},      // 128
        {"nodeArrayBase",     offsetof(OctreeConfig, nodeArrayBase)},     // 192
        {"brickArrayBase",    offsetof(OctreeConfig, brickArrayBase)},    // 196
        {"formatId",          offsetof(OctreeConfig, formatId)},          // 200
        {"bricksPerAxisSdf",  offsetof(OctreeConfig, bricksPerAxisSdf)},  // 204
        {"poolBrickBase",     offsetof(OctreeConfig, poolBrickBase)},     // 208
        {"channelCount",      offsetof(OctreeConfig, channelCount)},      // 212
        {"brickStrideFloats", offsetof(OctreeConfig, brickStrideFloats)}, // 216
    };
    for (const auto& f : fields) {
        const SpirvStructMember* sm = FindSubMember(*configs, f.name);
        ASSERT_NE(sm, nullptr) << "shader OctreeConfig is missing field '" << f.name << "'";
        EXPECT_EQ(static_cast<std::size_t>(sm->offset), f.cppOffset)
            << "offset drift on '" << f.name << "': shader=" << sm->offset
            << " C++=" << f.cppOffset;
    }
}
