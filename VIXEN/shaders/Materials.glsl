// ============================================================================
// Materials.glsl - Material ID to Color Mapping
// ============================================================================
// The palette is authored once, as the [KernelCallable] MaterialColorChannel in
// libraries/SVO/schemas/MaterialColorCallables.cs, and generated for GLSL
// (Generated/MaterialColor.glsl) and C++ (Generated/MaterialColor.g.hpp). This
// file holds no table: getMaterialColor() only packs the three channel reads.
// ============================================================================

#ifndef MATERIALS_GLSL
#define MATERIALS_GLSL

#include "Generated/MaterialColor.glsl"

vec3 getMaterialColor(uint matID) {
    return vec3(MaterialColorChannel(matID, 0u),
                MaterialColorChannel(matID, 1u),
                MaterialColorChannel(matID, 2u));
}

#endif // MATERIALS_GLSL
