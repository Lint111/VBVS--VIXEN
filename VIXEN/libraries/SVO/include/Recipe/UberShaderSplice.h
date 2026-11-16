#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <stdexcept>
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeServices.h"

namespace Vixen::SVO::Recipe {

// Ordinary Output instructions use generated output slots. The material-capable variant is
// selected by MaterialRef/Attach/default state or by the generated semantic-7 sidecar channel;
// a compact slot such as MaterialId's 8 is not itself a material-carry request.
inline bool RecipeRequiresMaterialVariant(const RecipeRegistry::RecipeEntry* entry) noexcept {
    if (!entry) return false;
    if (entry->defaultMaterialId != 0u) return true;
    const auto materialSidecarChannel = static_cast<float>(
        RecipeMaterialSidecarChannelFor(SEM_MATERIAL_ID));
    for (const auto& instruction : entry->bytecode) {
        const auto op = static_cast<SdfOpCode>(instruction.opCode);
        if (op == SdfOpCode::MaterialRef || op == SdfOpCode::Attach) return true;
        if (op == SdfOpCode::Output && std::isfinite(instruction.data[0]) &&
            instruction.data[0] == materialSidecarChannel)
            return true;
    }
    return false;
}

inline bool RegistryRequiresMaterialVariant(const RecipeRegistry& registry) noexcept {
    for (const std::uint32_t id : registry.Ids())
        if (RecipeRequiresMaterialVariant(registry.Get(id))) return true;
    return false;
}

static_assert(RecipeMaterialSidecarChannelFor(SEM_MATERIAL_ID) < SEM_COUNT,
              "generated channel vocabulary must expose MaterialId as a semantic sidecar ordinal");
static_assert(RecipeOutputSlotFor(SEM_MATERIAL_ID) != 0u,
              "generated channel vocabulary must expose a non-distance MaterialId output slot");

// SpliceProceduralRecipesIntoSource — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11.
//
// Textually inserts, right after BodyInstanceRayMarch.comp's
// "// VIXEN_UBER_RECIPE_SPLICE_MARKER" line:
//   1. #define VIXEN_UBER_RECIPE_SPLICED (gates the recipeId>=2 branch in main(), so an
//      unspliced source — zero registrations — never references an undeclared identifier)
//   2. one kernel-service-emitted sdfRecipe_<id>(vec3) per registered recipe
//   3. a generated evalRecipeField(uint recipeId, vec3 p) switch dispatching to those functions
//   4. a generated getRecipeBoundSphere(uint recipeId, out vec3 center, out float radius,
//      out float relaxation) switch — each case's bound/relaxation are baked as GLSL float
//      literals directly in the switch body (compile-time constants per case, per Task 11's
//      "no new binding" scope line — registration already forces a recompile, so an SSBO here
//      buys nothing in v1; that one-new-binding budget stays reserved for M6's occupancy grid)
//   5. (M6 Task 13, only when outConcatenatedOccupancyGrid != nullptr) a generated
//      getRecipeOccupancyGrid(uint recipeId, out uint gridOffset, out uint gridDim,
//      out vec3 gridAabbMin, out float gridCellSize) switch — offset/dim/aabbMin/cellSize
//      are baked as GLSL literals per case (same "no metadata SSBO" convention as bound
//      sphere above); the actual dim^3 float values for EVERY registered recipe's grid are
//      concatenated (Ids() ascending order, gridOffset = running float-element offset) into
//      *outConcatenatedOccupancyGrid — the caller uploads that ONE blob to the new SSBO
//      binding this milestone reserves. A recipe with occupancyGridDim==0 (ungridded —
//      DeriveOccupancyGrid declined the program) gets gridDim=0u in its switch case and
//      contributes nothing to the blob; the shader treats gridDim==0 as "no grid available,
//      skip the occupancy fast-path for this instance" (never a hard error).
//   6. (R631, only when outConcatenatedGlyphInstances != nullptr) the registry's one
//      content-deduplicated typed glyph blob is copied into the runtime SSBO payload.
//      GlyphOutline operands already carry its global indices; no outline coordinates or
//      contour buckets are embedded in shader source.
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
    const RecipeRegistry& registry,
    std::vector<float>* outConcatenatedOccupancyGrid = nullptr,
    std::vector<Yeroket::Sdf::Kernels::GlyphOutlineInstance>* outConcatenatedGlyphInstances = nullptr,
    std::vector<YkRecipeGpuTapeInstruction>* outConcatenatedRecipeTape = nullptr,
    bool forceMaterialVariant = false)
{
    if (outConcatenatedOccupancyGrid) outConcatenatedOccupancyGrid->clear();
    if (outConcatenatedGlyphInstances) outConcatenatedGlyphInstances->clear();
    if (outConcatenatedRecipeTape) outConcatenatedRecipeTape->clear();
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

    // Bounds and grid metadata obey the same binary32 transport contract as operands.
    const auto f = RecipeGlslFloatLiteral;

    const bool materialVariant = forceMaterialVariant || std::any_of(ids.begin(), ids.end(), [&](uint32_t id) {
        return RecipeRequiresMaterialVariant(registry.Get(id));
    });
    std::vector<uint32_t> tapeBases;
    tapeBases.reserve(ids.size());
    uint32_t tapeOffset = 0u;
    for (uint32_t id : ids) {
        const auto* entry = registry.Get(id);
        tapeBases.push_back(tapeOffset);
        if (!materialVariant || !entry) continue;
        const auto tape = RecipeGpuTape(entry->bytecode);
        if (tape.status != YK_RECIPE_GPU_OK || tape.instructions.size() != entry->bytecode.size()) {
            throw std::runtime_error("SpliceProceduralRecipesIntoSource: recipe " + std::to_string(id) +
                " tape lowering refused (" + (tape.diagnostic.empty() ? "no diagnostic" : tape.diagnostic) + ")");
        }
        if (outConcatenatedRecipeTape) {
            outConcatenatedRecipeTape->insert(outConcatenatedRecipeTape->end(),
                tape.instructions.begin(), tape.instructions.end());
        }
        tapeOffset += static_cast<uint32_t>(tape.instructions.size());
    }

    std::ostringstream out;
    out << "\n#define VIXEN_UBER_RECIPE_SPLICED 1\n";
    if (materialVariant) {
        out << "#define VIXEN_UBER_RECIPE_MATERIAL 1\n"
               "struct RecipeTapeInstruction { uint opCode; float data[32]; };\n"
               "layout(std430, binding = 53) readonly buffer RecipeTapeBuffer { RecipeTapeInstruction recipeTape[]; };\n";
    }

    // Recipe-Diversity-Stress-Scene Inc6 M2: a recipe containing DeclarePosition (M1's
    // spatial-contract opcode) MUST be emitted with emitDeclaredPositionOutParam=true —
    // The kernel service's DeclarePosition lowering writes
    // `declaredPos = vec3(...)`, which only compiles when the function signature actually
    // declares that out-param (asserted in Debug, a raw GLSL compile error in Release either
    // way). Every OTHER recipe (the overwhelming majority — every pre-Inc6 demo, and every
    // Inc6-diversity-demo recipe that happens not to use DeclarePosition) keeps the plain
    // 3-arg signature unchanged, so this is genuinely per-recipe opt-in, not a blanket
    // signature change. The declared position itself is never read back by evalRecipeField's
    // own callers (traceUberRecipeBody/occupancy sampling already get an instance's placement
    // from RecipeEntry::boundCenter, authored separately at registration) — the out-param
    // exists solely so DeclarePosition's assignment has somewhere to go; a throwaway local at
    // the call site (below) is all production code needs.
    auto usesDeclarePosition = [](const RecipeRegistry::RecipeEntry* entry) {
        for (const auto& instr : entry->bytecode) {
            if (static_cast<SdfOpCode>(instr.opCode) == SdfOpCode::DeclarePosition) return true;
        }
        return false;
    };

    for (uint32_t id : ids) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        // Register() already guarantees a non-null, non-empty bytecode for every id in Ids() —
        // defensive only, mirrors the assert-not-throw discipline the CPU emitter itself uses.
        if (!entry) continue;
        const bool declaredPosition = usesDeclarePosition(entry);
        std::string field = RecipeGpuField(entry->bytecode, id, declaredPosition,
                                           entry->pathInstances, /*glyphInstanceBase=*/0,
                                           /*boundedGlyph=*/true,
                                           /*readInstructionOperands=*/materialVariant,
                                           /*carryMaterialId=*/materialVariant,
                                           /*materialDefault=*/entry->defaultMaterialId);
        if (field.empty()) {
            for (std::size_t instructionIndex = 0; instructionIndex < entry->bytecode.size(); ++instructionIndex) {
                const auto op = static_cast<SdfOpCode>(entry->bytecode[instructionIndex].opCode);
                if (op == SdfOpCode::SampleSurface || op == SdfOpCode::Instance) {
                    throw std::runtime_error("SpliceProceduralRecipesIntoSource: recipe " + std::to_string(id) +
                        " refused at instruction " + std::to_string(instructionIndex) + " (" +
                        std::string(SdfOpCodeName(op)) + "): GLSL field consumer cannot expand set operands");
                }
            }
            throw std::runtime_error("SpliceProceduralRecipesIntoSource: recipe GPU service returned no field");
        }
        out << field << "\n";
    }

    if (outConcatenatedGlyphInstances) {
        const auto blob = registry.GlyphOutlineBlob();
        outConcatenatedGlyphInstances->assign(blob.begin(), blob.end());
    }

    // params (Recipe-Parameterization M2 Task 6): threaded through as a plain function
    // argument, following the TraceWorld.glsl legacy-path precedent (BodyInstance.recipeParams
    // read out of the SSBO once at the call site, passed down as an argument — not re-indexed
    // from the SSBO inside deeply-nested functions). Every case takes it uniformly even though
    // only ReadParam/ReadParamFloat3-using recipes reference it.
    out << "float evalRecipeField(uint recipeId, vec3 p, float params[6]"
        << (materialVariant ? ", out uint materialId" : "") << ") {\n"
           "  vec3 unusedDeclaredPos;\n"
           "  switch (recipeId) {\n";
    for (uint32_t id : ids) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        if (!entry) continue;
        const auto idIt = std::find(ids.begin(), ids.end(), id);
        const uint32_t tapeBase = tapeBases[static_cast<std::size_t>(idIt - ids.begin())];
        out << "    case " << id << "u: return sdfRecipe_" << id << "(p, params";
        if (materialVariant) out << ", " << tapeBase << "u";
        if (usesDeclarePosition(entry)) {
            out << ", unusedDeclaredPos";
        }
        if (materialVariant) out << ", materialId";
        out << ");\n";
    }
    out << "    default: return 0.0;\n"
           "  }\n"
           "}\n";
    if (materialVariant) {
        out << "float evalRecipeField(uint recipeId, vec3 p, float params[6]) {\n"
               "  uint unusedMaterial;\n"
               "  return evalRecipeField(recipeId, p, params, unusedMaterial);\n"
               "}\n";
    }

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

    // M6 Task 13: occupancy-grid lookup switch + concatenated blob (see header comment's
    // point 5). Emitted unconditionally (even when outConcatenatedOccupancyGrid is null and
    // no caller will ever upload the blob) so the shader's getRecipeOccupancyGrid symbol
    // always exists once ANY recipe is registered — matching getRecipeBoundSphere's own
    // "declared whenever the splice fires at all" discipline; a caller that doesn't pass the
    // out-param simply never gets non-zero gridDim cases (every case falls through to the
    // default gridDim=0u), which the shader already treats as "no grid, skip nothing extra."
    out << "void getRecipeOccupancyGrid(uint recipeId, out uint gridOffset, out uint gridDim, "
           "out vec3 gridAabbMin, out float gridCellSize) {\n"
           "  switch (recipeId) {\n";
    uint32_t runningOffset = 0;
    for (uint32_t id : ids) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(id);
        if (!entry) continue;
        if (entry->occupancyGridDim == 0 || entry->occupancyGridValues.empty()) continue;
        out << "    case " << id << "u: gridOffset = " << runningOffset << "u; gridDim = "
            << entry->occupancyGridDim << "u; gridAabbMin = vec3("
            << f(entry->occupancyGridAabbMin.x) << ", " << f(entry->occupancyGridAabbMin.y) << ", "
            << f(entry->occupancyGridAabbMin.z) << "); gridCellSize = "
            << f(entry->occupancyGridCellSize) << "; return;\n";
        if (outConcatenatedOccupancyGrid) {
            outConcatenatedOccupancyGrid->insert(outConcatenatedOccupancyGrid->end(),
                entry->occupancyGridValues.begin(), entry->occupancyGridValues.end());
        }
        runningOffset += static_cast<uint32_t>(entry->occupancyGridValues.size());
    }
    out << "    default: gridOffset = 0u; gridDim = 0u; gridAabbMin = vec3(0.0); "
           "gridCellSize = 0.0; return;\n"
           "  }\n"
           "}\n";

    return rawSource.substr(0, insertPos) + out.str() + rawSource.substr(insertPos);
}

} // namespace Vixen::SVO::Recipe
