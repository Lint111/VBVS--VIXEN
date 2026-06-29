
#pragma once
#include <cstdint>
#include <stddef.h>
// <provenance: generated from Yeroket.GraphFramework.VM — do not edit by hand>
namespace Yeroket::Sdf::Generated {

struct SdfInstruction {            // mirrors C# SDFInstruction (132 B)
    uint8_t opCode; uint8_t inputMask; uint8_t paramMask; uint8_t _pad1;
    float data[32];                // Data0..7 → 8 float4 = 32 floats
};
static_assert(sizeof(SdfInstruction) == 132, "SdfInstruction must be 132 B");

struct RecipeContainerHeader {     // mirrors C# RecipeContainerHeader (32 B)
    uint32_t magic; uint32_t formatVersion; uint32_t instructionCount; uint32_t bakeResolution; float bandVoxels; uint32_t brickDepth; uint32_t reserved0; uint32_t reserved1;
};
static_assert(sizeof(RecipeContainerHeader) == 32, "RecipeContainerHeader must be 32 B");

struct RecipeContainerView {
    RecipeContainerHeader header;
    const SdfInstruction* instructions;
};
inline bool ReadRecipeContainer(const uint8_t* blob, size_t len, RecipeContainerView& out) {
    if (!blob || len < sizeof(RecipeContainerHeader)) return false;
    RecipeContainerHeader h{};
    for (size_t i = 0; i < sizeof(RecipeContainerHeader); ++i)
        reinterpret_cast<uint8_t*>(&h)[i] = blob[i];
    if (h.magic != 0x31435256u) return false;          // 'VRC1'
    if (h.formatVersion != 1u) return false;
    const size_t need = sizeof(RecipeContainerHeader) + (size_t)h.instructionCount * sizeof(SdfInstruction);
    if (len != need) return false;
    out.header = h;
    out.instructions = reinterpret_cast<const SdfInstruction*>(blob + sizeof(RecipeContainerHeader));
    return true;
}

} // namespace Yeroket::Sdf::Generated