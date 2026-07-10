/**
 * @file test_hitrecord_sdi_parity.cpp
 * @brief Drift-guard: a hand-written C++ mirror of shaders/HitRecord.glsl's std430 layout
 *        MUST match the layout the shipped BodyInstanceRayMarch.comp actually compiles to.
 *
 * Sampled Lighting Inc1 M3 adds HitRecordBuffer (binding 17) to BodyInstanceRayMarch.comp.
 * HitRecord has NO C++ engine consumer this milestone (no host upload, no [GpuStruct]
 * codegen — see HitRecord.glsl's file header for why codegen was judged disproportionate
 * for a shader-only struct). This test is the substitute drift-guard: reflect the compiled
 * shader via ShaderManagement's SpirvReflector (SPIRV-Reflect) and assert the reflected
 * HitRecord member's per-field byte offsets match a LOCAL mirror struct's offsetof values —
 * exactly test_lightingconfig_sdi_parity.cpp's pattern, minus the generated header (the
 * mirror struct lives in this TU only, since nothing else needs it).
 */

#include <gtest/gtest.h>

#include "SpirvReflector.h"
#include "SpirvReflectionData.h"
#include "ShaderStage.h"

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

// Hand-written C++ mirror of shaders/HitRecord.glsl's std430 layout. NOT a [GpuStruct]
// generated type (no engine code includes this) — local to this test, whose entire job is
// to catch the two copies (this one and the GLSL one) drifting apart.
//
// Sized 60 B (the DECLARED struct size — albedo..flags, ending right after _pad0's 3 named
// uints), matching what SPIRV-Reflect actually reports for hitRecords[]'s element type below.
// The shader's REAL per-element buffer stride is 64 B (std430's array-stride rule pads a
// runtime array's element to its largest member's alignment — 16 B here — so consecutive
// elements land 64 B apart, not 60), but empirically or SPIRV-Reflect does not surface that
// stride for a runtime-length array the way it does for a fixed-size one like LightingConfig's
// `lights[4]` (see test_lightingconfig_sdi_parity.cpp, whose arrayStride assertion DOES work —
// confirmed by running both tests against the live SPIR-V: hitRecords reports
// arrayStride=0/size=60 where lights reports a real nonzero arrayStride). The 64 B real stride
// is instead proven by test_hitrecord_readback.cpp's live GPU dispatch, which sizes/indexes the
// buffer using the shader's actual generated offsets end-to-end.
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[3];
};
static_assert(sizeof(HitRecordCpu) == 60, "HitRecordCpu std430 mirror size (declared, pre-array-stride)");
static_assert(offsetof(HitRecordCpu, albedo)      == 0,  "albedo@0");
static_assert(offsetof(HitRecordCpu, roughness)   == 12, "roughness@12");
static_assert(offsetof(HitRecordCpu, worldNormal) == 16, "worldNormal@16");
static_assert(offsetof(HitRecordCpu, hitT)        == 28, "hitT@28");
static_assert(offsetof(HitRecordCpu, worldPos)    == 32, "worldPos@32");
static_assert(offsetof(HitRecordCpu, flags)       == 44, "flags@44");
static_assert(offsetof(HitRecordCpu, _pad0)       == 48, "_pad0@48");

// The REAL per-element buffer stride (see comment above) — what C++ code allocating/indexing
// a HitRecordBuffer-shaped SSBO must actually use (test_hitrecord_readback.cpp does).
constexpr std::size_t kHitRecordBufferStride = 64;

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

const SpirvStructMember* FindSubMember(const SpirvStructMember& parent, const std::string& name) {
    for (const auto& m : parent.members)
        if (m.name == name) return &m;
    return nullptr;
}

}  // namespace

// Catches a future edit to BodyInstanceRayMarch.comp's HitRecord / HitRecordBuffer (or
// shaders/HitRecord.glsl) that desyncs the shader layout from this test's C++ mirror —
// the same class of bug test_octree_config_sdi_parity / test_lightingconfig_sdi_parity guard.
TEST(HitRecordSdiParity, ReflectedLayoutMatchesCppMirror) {
    const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
    ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;

    SpirvReflector reflector;
    auto refl = reflector.ReflectStage(spirv, ShaderStage::Compute);
    ASSERT_NE(refl, nullptr) << "SPIR-V reflection failed";

    // Locate the hitRecords[] SSBO array member (its element type is the HitRecord struct).
    const SpirvStructMember* hitRecords = FindMember(*refl, "hitRecords");
    ASSERT_NE(hitRecords, nullptr)
        << "reflection has no member 'hitRecords' — HitRecordBuffer changed or names were stripped";

    std::cout << "[reflected HitRecord] size=" << hitRecords->type.sizeInBytes
              << " arrayStride=" << hitRecords->arrayStride
              << " members=" << hitRecords->members.size() << "\n";
    for (const auto& m : hitRecords->members) {
        std::cout << "    ." << m.name << " offset=" << m.offset << "\n";
    }

    // (a) Reflected declared element size == C++ mirror size (see HitRecordCpu's comment for
    // why this is 60, not the real 64 B buffer stride — SPIRV-Reflect doesn't surface array
    // stride for a runtime-length array member).
    EXPECT_EQ(static_cast<std::size_t>(hitRecords->type.sizeInBytes), sizeof(HitRecordCpu))
        << "HitRecord declared size (" << hitRecords->type.sizeInBytes
        << ") != sizeof(HitRecordCpu) (" << sizeof(HitRecordCpu) << ") — std430 drift";
    EXPECT_EQ(hitRecords->type.sizeInBytes, 60u) << "HitRecord declared size must be 60 (Inc1 M3 contract)";
    (void)kHitRecordBufferStride;  // documented, exercised by test_hitrecord_readback.cpp

    // (b) Per-field offsets.
    ASSERT_FALSE(hitRecords->members.empty())
        << "SpirvReflector did not surface HitRecord members";

    struct Field { const char* name; std::size_t cppOffset; };
    const Field fields[] = {
        {"albedo",      offsetof(HitRecordCpu, albedo)},
        {"roughness",   offsetof(HitRecordCpu, roughness)},
        {"worldNormal", offsetof(HitRecordCpu, worldNormal)},
        {"hitT",        offsetof(HitRecordCpu, hitT)},
        {"worldPos",    offsetof(HitRecordCpu, worldPos)},
        {"flags",       offsetof(HitRecordCpu, flags)},
    };
    for (const auto& f : fields) {
        const SpirvStructMember* sm = FindSubMember(*hitRecords, f.name);
        ASSERT_NE(sm, nullptr) << "shader HitRecord is missing field '" << f.name << "'";
        EXPECT_EQ(static_cast<std::size_t>(sm->offset), f.cppOffset)
            << "offset drift on '" << f.name << "': shader=" << sm->offset
            << " C++=" << f.cppOffset;
    }
}
