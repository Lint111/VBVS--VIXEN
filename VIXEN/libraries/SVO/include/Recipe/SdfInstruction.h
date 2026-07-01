#pragma once
#include <cstdint>
#include "generated/SdfOpCodes.g.h"   // codegen-mirror of C# SDFOpCode; namespace Vixen::SVO::Recipe
#include "generated/RecipeContainer.g.h"  // codegen-emitted reader; namespace Yeroket::Sdf::Generated

namespace Vixen::SVO::Recipe {

// ponytail: alias the generated struct so every existing VIXEN call site
// (.opCode, .data[...], sizeof) compiles unchanged.
using SdfInstruction = Yeroket::Sdf::Generated::SdfInstruction;
static_assert(sizeof(SdfInstruction) == 132, "must match C# SDFInstruction (132 B)");

} // namespace Vixen::SVO::Recipe
