#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"

namespace Vixen::SVO::Recipe {

// SpliceProceduralRecipesIntoSource — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11.
//
// Textually inserts, right after BodyInstanceRayMarch.comp's
// "// VIXEN_UBER_RECIPE_SPLICE_MARKER" line:
//   1. #define VIXEN_UBER_RECIPE_SPLICED (gates the recipeId>=2 branch in main(), so an
//      unspliced source — zero registrations — never references an undeclared identifier)
//   2. one EmitProceduralFieldFunctionGlsl-emitted sdfRecipe_<id>(vec3) per registered recipe
//   3. a generated evalRecipeField(uint recipeId, vec3 p) switch dispatching to those functions
//   4. a generated getRecipeBoundSphere(uint recipeId, out vec3 center, out float radius,
//      out float relaxation) switch — each case's bound/relaxation are baked as GLSL float
//      literals directly in the switch body (compile-time constants per case, per Task 11's
//      "no new binding" scope line — registration already forces a recompile, so an SSBO here
//      buys nothing in v1; that one-new-binding budget stays reserved for M6's occupancy grid)
//
// The switch's default case returns a zero-radius bound-sphere miss and a zero field value —
// unreachable in practice (a BodyInstance's recipeId is only ever set to an id the CPU side
// actually registered), but keeps the generated GLSL well-formed regardless (glslang requires
// every path through a value-returning function to return something).
//
// Marker not found -> throws: this means BodyInstanceRayMarch.comp's marker comment was
// edited/removed without updating this splice function, which is a build-breaking drift this
// function should fail loudly on rather than silently produce an un-recipe-aware shader.
inline std::string SpliceProceduralRecipesIntoSource(
    const std::string& rawSource,
    const RecipeRegistry& registry)
{
    static const std::string kMarker = "// VIXEN_UBER_RECIPE_SPLICE_MARKER";
    const size_t markerPos = rawSource.find(kMarker);
    if (markerPos == std::string::npos) {
        throw std::runtime_error(
            "SpliceProceduralRecipesIntoSource: VIXEN_UBER_RECIPE_SPLICE_MARKER not found in "
            "BodyInstanceRayMarch.comp — the splice anchor was removed or renamed without "
            "updating UberShaderSplice.h");
    }
    const size_t insertPos = markerPos + kMarker.size();

    const auto ids = registry.Ids();  // ascending (RecipeRegistry::Ids' own guarantee)
    if (ids.empty()) {
        // Nothing registered: splice in only the SPLICED macro's absence is correct here —
        // do NOT define VIXEN_UBER_RECIPE_SPLICED, so main()'s recipeId>=2 branch compiles
        // out entirely and the source is byte-identical in spirit to the pre-M5 shader.
        return rawSource;
    }

    // ponytail: matches EmitProceduralFieldFunctionGlsl's own float-literal guard — every
    // literal this splice writes must carry a decimal point so GLSL sees it as a float, not
    // an int (the same "1/6 integer-divides to 0" failure class that guard exists for).
    auto f = [](float v) {
        std::string s = std::to_string(v);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    std::ostringstream out;
    out << "\n#define VIXEN_UBER_RECIPE_SPLICED 1\n";

    for (uint32_t id : ids) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        // Register() already guarantees a non-null, non-empty bytecode for every id in Ids() —
        // defensive only, mirrors the assert-not-throw discipline the CPU emitter itself uses.
        if (!entry) continue;
        out << EmitProceduralFieldFunctionGlsl(entry->bytecode.data(),
                                               static_cast<uint32_t>(entry->bytecode.size()),
                                               id)
            << "\n";
    }

    out << "float evalRecipeField(uint recipeId, vec3 p) {\n"
           "  switch (recipeId) {\n";
    for (uint32_t id : ids) {
        out << "    case " << id << "u: return sdfRecipe_" << id << "(p);\n";
    }
    out << "    default: return 0.0;\n"
           "  }\n"
           "}\n";

    out << "void getRecipeBoundSphere(uint recipeId, out vec3 center, out float radius, "
           "out float relaxation) {\n"
           "  switch (recipeId) {\n";
    for (uint32_t id : ids) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        if (!entry) continue;
        out << "    case " << id << "u: center = vec3("
            << f(entry->boundCenter.x) << ", " << f(entry->boundCenter.y) << ", " << f(entry->boundCenter.z)
            << "); radius = " << f(entry->boundRadius) << "; relaxation = "
            << f(entry->stepRelaxation) << "; return;\n";
    }
    out << "    default: center = vec3(0.0); radius = 0.0; relaxation = 1.0; return;\n"
           "  }\n"
           "}\n";

    return rawSource.substr(0, insertPos) + out.str() + rawSource.substr(insertPos);
}

} // namespace Vixen::SVO::Recipe
