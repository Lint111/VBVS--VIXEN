// ============================================================================
// RecipeEditBindings.glsl - the material-capable recipe tape (R896)
// ============================================================================
// Declared statically (never printed by the runtime splice) so the merged SDI, the generated
// binding allocation and the C++ wire plan all see it. Present only in the material-capable
// variant: UberShaderSplice.h defines VIXEN_UBER_RECIPE_MATERIAL at the top of the source when
// the registry (or the editor override) needs the tape, and the sdi-variants.json axis of the
// same name gates the member, so its provider (BodyOctreeSceneNode RECIPE_TAPE_BUFFER) is wired
// by name through the SDI exactly like every other scene buffer.
//
// Included before VIXEN_UBER_RECIPE_SPLICE_MARKER: the spliced recipe fields read recipeTape[].
#ifndef VIXEN_RECIPE_EDIT_BINDINGS_GLSL
#define VIXEN_RECIPE_EDIT_BINDINGS_GLSL
#ifdef VIXEN_UBER_RECIPE_MATERIAL
struct RecipeTapeInstruction { uint opCode; float data[32]; };
layout(std430, binding = VXB_RecipeTapeBuffer) readonly buffer RecipeTapeBuffer { RecipeTapeInstruction recipeTape[]; };
// Set by the last successful procedural field evaluation (SdfRecipes.glsl). TraceWorld and the
// ray-query twin read it only after a certified hit, so the material id follows the winning
// surface sample. Declared with the variant, not with the splice, so the material-capable
// interface compiles (and reflects) with or without registered recipes.
uint g_lastRecipeMaterialId = 0u;
#endif
#endif
