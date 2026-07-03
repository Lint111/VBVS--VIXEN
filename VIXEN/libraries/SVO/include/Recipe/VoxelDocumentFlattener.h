#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Recipe/generated/VoxelDocument.g.h"

namespace Vixen::SVO {

// FlattenVoxelDocument — document -> VRC1 recipe blob (design §5,
// Voxel-Authoring-App-Inc1-Design-2026-07.md). The recipe VM is a postfix
// stack machine; every layer's program is a self-contained postfix
// expression netting one field value. Flattening walks the enabled `rule`
// layers in order, treats the first one as the base field (its `op` is
// IGNORED — documented in the design), and appends each subsequent layer's
// program plus one combine SdfInstruction for that layer's op. The result
// is a single postfix program the existing RecipeRegistry/octree-pool path
// (ReadRecipeContainer / RecipeRegistry::Register) consumes unchanged.
//
// Combine instruction packing (opCode ordinal, InputMask, which data[i]
// holds the blend radius) is read verbatim from the canonical C# op
// kernels via the golden test builder that produced sample_tri_layer.vxd
// (Yeroket VoxelDocumentGoldenTests.cs, SDFOperationNode.CompileToBurst):
//   InputMask = 3                              (both stack inputs present)
//   Data0     = float4(0, 0, smoothness, 0)    (blend radius in Data0.z,
//                                                0 for non-smooth ops)
// Layer op -> SdfOpCode ordinal (vendored SdfOpCodes.g.h, matches VDC1
// design §3 "Layer ops v1"):
//   0 union -> Union(24), 1 smooth_union -> SmoothUnion(25),
//   2 subtract -> Subtract(26), 3 intersect -> Intersect(28)
//
// enabledOverride: optional per-layer enabled bytes (one entry per layer,
// index == layer index in document order), for editor toggles that haven't
// been written back into the document yet. nullptr (or empty) means "use
// each layer's own header.enabled flag". A non-null override must have
// exactly doc.header.layerCount entries.
//
// Fails (returns false, err set) on: zero enabled `rule` layers after
// applying the override, an instruction opcode outside the vendored
// SdfOpCode enum (including inside a layer's own program), a layer op
// outside {0,1,2,3}, or a stack depth that would exceed the shared VM
// guard (RecipeStackArity, the same sp<64 / psp<64 bound
// RecipeRegistry::Register enforces).
bool FlattenVoxelDocument(const Yeroket::Sdf::Generated::VoxelDocumentView& doc,
                          const std::vector<uint8_t>* enabledOverride,
                          std::vector<uint8_t>& outVrc1Blob,
                          std::string& err);

} // namespace Vixen::SVO
