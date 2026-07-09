#include "Recipe/VoxelDocumentFlattener.h"

#include <cstring>

#include "Recipe/RecipeRegistry.h"     // IsValidSdfOpCode
#include "Recipe/RecipeStack.h"        // RecipeStackArity, StackArity
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/SdfOpCodes.g.h"

namespace Vixen::SVO {

using Yeroket::Sdf::Generated::RecipeContainerHeader;
using Yeroket::Sdf::Generated::SdfInstruction;
using Yeroket::Sdf::Generated::VoxelDocumentView;
using Recipe::SdfOpCode;

namespace {

// VRC1 header defaults — copied from the shipped recipe-pack test/tooling
// convention (VIXEN/libraries/SVO/tests/test_recipe_pack_loader.cpp,
// test_recipe_ingest.cpp: bakeResolution=64, bandVoxels=2.5f, brickDepth=3),
// the same values every existing recipe blob in this codebase carries.
constexpr uint32_t kBakeResolution = 64u;
constexpr float    kBandVoxels     = 2.5f;
constexpr uint32_t kBrickDepth     = 3u;

// Layer op -> SdfOpCode ordinal (design §3 "Layer ops v1" / §5 flatten
// semantics). Combine instruction packing cited in VoxelDocumentFlattener.h.
bool CombineOpCodeForLayerOp(uint8_t layerOp, SdfOpCode& outOp) {
    switch (layerOp) {
        case 0: outOp = SdfOpCode::Union;        return true;
        case 1: outOp = SdfOpCode::SmoothUnion;  return true;
        case 2: outOp = SdfOpCode::Subtract;     return true;
        case 3: outOp = SdfOpCode::Intersect;    return true;
        default: return false;
    }
}

SdfInstruction MakeCombineInstruction(SdfOpCode op, float blendRadius) {
    SdfInstruction in{};
    in.opCode    = static_cast<uint8_t>(op);
    in.inputMask = 3;             // both stack inputs present (A and B)
    in.data[2]   = blendRadius;   // Data0.z — smoothness (0 for non-smooth ops)
    return in;
}

// Validate every instruction opcode in a layer's own program (the design's
// "reject … instruction opcodes outside the vendored enum" clause covers a
// layer's raw program, not just the synthesized combine instruction).
bool ValidateProgramOpCodes(const SdfInstruction* prog, uint32_t count, std::string& err) {
    for (uint32_t i = 0; i < count; ++i) {
        if (!IsValidSdfOpCode(prog[i].opCode)) {
            err = "unknown opcode " + std::to_string(prog[i].opCode) +
                  " at instruction " + std::to_string(i) + " of layer program";
            return false;
        }
    }
    return true;
}

// Simulate the shared value/position stack arity across a sequence of
// instructions, starting from (sp, psp). Mirrors RecipeRegistry::Register's
// guard so a document that would blow the shipped VM's 64-slot stacks is
// rejected here, before ever reaching the registry.
bool AdvanceStack(const SdfInstruction* prog, uint32_t count, int& sp, int& psp, std::string& err) {
    for (uint32_t i = 0; i < count; ++i) {
        const auto a = Recipe::RecipeStackArity(static_cast<SdfOpCode>(prog[i].opCode));
        if (sp < a.vPop || psp < a.pPop) {
            err = "stack underflow at instruction " + std::to_string(i);
            return false;
        }
        sp  = sp  - a.vPop  + a.vPush;
        psp = psp - a.pPop  + a.pPush;
        if (sp > 64 || psp > 64) {
            err = "stack depth exceeds the VM's 64-slot guard at instruction " + std::to_string(i);
            return false;
        }
    }
    return true;
}

bool IsLayerEnabled(const VoxelDocumentView& doc, uint32_t layerIndex,
                    const std::vector<uint8_t>* enabledOverride) {
    if (enabledOverride && !enabledOverride->empty())
        return (*enabledOverride)[layerIndex] != 0;
    return doc.layers[layerIndex].header->enabled != 0;
}

} // namespace

bool FlattenVoxelDocument(const VoxelDocumentView& doc,
                          const std::vector<uint8_t>* enabledOverride,
                          std::vector<uint8_t>& outVrc1Blob,
                          std::string& err) {
    outVrc1Blob.clear();
    err.clear();

    const uint32_t layerCount = doc.header.layerCount;
    if (enabledOverride && !enabledOverride->empty() && enabledOverride->size() != layerCount) {
        err = "enabledOverride size (" + std::to_string(enabledOverride->size()) +
              ") does not match document layerCount (" + std::to_string(layerCount) + ")";
        return false;
    }

    std::vector<SdfInstruction> out;
    int sp = 0, psp = 0;
    bool haveBase = false;

    for (uint32_t l = 0; l < layerCount; ++l) {
        if (!IsLayerEnabled(doc, l, enabledOverride))
            continue;

        const auto& layerView = doc.layers[l];
        const uint32_t instrCount = layerView.header->instructionCount;
        const SdfInstruction* prog = layerView.instructions;

        if (!ValidateProgramOpCodes(prog, instrCount, err))
            return false;

        if (!haveBase) {
            // First enabled layer = base field; its op is IGNORED (design §5).
            if (!AdvanceStack(prog, instrCount, sp, psp, err))
                return false;
            out.insert(out.end(), prog, prog + instrCount);
            haveBase = true;
            continue;
        }

        SdfOpCode combineOp{};
        if (!CombineOpCodeForLayerOp(layerView.header->op, combineOp)) {
            err = "unknown layer op " + std::to_string(layerView.header->op) +
                  " on layer index " + std::to_string(l);
            return false;
        }

        if (!AdvanceStack(prog, instrCount, sp, psp, err))
            return false;
        out.insert(out.end(), prog, prog + instrCount);

        const SdfInstruction combine = MakeCombineInstruction(combineOp, layerView.header->blendRadius);
        if (!AdvanceStack(&combine, 1, sp, psp, err))
            return false;
        out.push_back(combine);
    }

    if (!haveBase) {
        err = "zero enabled layers — nothing to render";
        return false;
    }

    RecipeContainerHeader header{};
    header.magic            = 0x31435256u;   // 'VRC1'
    header.formatVersion    = 1u;
    header.instructionCount = static_cast<uint32_t>(out.size());
    header.bakeResolution   = kBakeResolution;
    header.bandVoxels       = kBandVoxels;
    header.brickDepth       = kBrickDepth;

    outVrc1Blob.resize(sizeof(header) + out.size() * sizeof(SdfInstruction));
    std::memcpy(outVrc1Blob.data(), &header, sizeof(header));
    if (!out.empty()) {
        std::memcpy(outVrc1Blob.data() + sizeof(header), out.data(),
                    out.size() * sizeof(SdfInstruction));
    }
    return true;
}

} // namespace Vixen::SVO
