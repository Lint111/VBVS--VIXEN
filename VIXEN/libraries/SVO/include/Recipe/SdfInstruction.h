#pragma once
#include <cstdint>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "generated/SdfOpCodes.g.h"   // codegen-mirror of C# SDFOpCode; namespace Vixen::SVO::Recipe
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "generated/RecipeContainer.g.h"  // codegen-emitted reader; namespace Yeroket::Sdf::Generated

namespace Vixen::SVO::Recipe {

// ponytail: alias the generated struct so every existing VIXEN call site
// (.opCode, .data[...], sizeof) compiles unchanged.
using SdfInstruction = Yeroket::Sdf::Generated::SdfInstruction;
static_assert(sizeof(SdfInstruction) == 132, "must match C# SDFInstruction (132 B)");

} // namespace Vixen::SVO::Recipe
