#pragma once
#include <cstdint>

namespace Vixen::SVO::Recipe {

// Byte-compat mirror of Yeroket SDFOpCode. P2.4 M2b subset; values MUST match
// the C# enum in com.utility.graph-framework/Runtime/VM/SDFInstruction.cs.
// Sphere=0; Box=1; Union=24; SmoothUnion=25; MirrorX=41; RestorePos=97.
// APPEND-ONLY — never reorder existing values.
enum class SdfOpCode : uint8_t {
    Sphere      =  0,
    Box         =  1,
    Union       = 24,
    SmoothUnion = 25,
    MirrorX     = 41,
    RestorePos  = 97,
};

// 132-byte blittable mirror of SDFInstruction.
// ⚠ Alignment: C# struct is byte,byte,byte,byte + 8×float4 = 4 + 128 = 132 bytes packed.
// Using glm::vec4 data[8] would pad to 144 (16-byte aligned after 4-byte prefix).
// Use float data[32] (4-byte aligned) to stay at 132.
struct SdfInstruction {
    uint8_t opCode;
    uint8_t inputMask;
    uint8_t paramMask;
    uint8_t _pad1;
    float   data[32];   // 8 logical float4 lanes; data[0..3] = Data0.xyzw, etc.
};
static_assert(sizeof(SdfInstruction) == 132, "must match C# SDFInstruction (132 B)");

} // namespace Vixen::SVO::Recipe
