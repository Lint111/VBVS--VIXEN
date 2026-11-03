// BodyInstanceRecord.glsl — the ONE GLSL declaration of the cold per-body material record (R680.4).
// std430 SSBO element, 48 bytes — byte-for-byte identical to C++ Vixen::SVO::BodyInstanceMaterialGpu.
// Bindings differ per shader namespace, so each includer declares its own
// `layout(std430, binding = N) readonly buffer BodyInstanceBuffer { BodyInstance bodyInstances[]; }`.
// libraries/SVO/include/Recipe/SpecializedRecipeShaderGlsl.h keeps an inline copy: that source is
// compiled at runtime without an include resolver (see the note there).
#ifndef VIXEN_BODY_INSTANCE_RECORD_GLSL
#define VIXEN_BODY_INSTANCE_RECORD_GLSL
struct BodyInstance {
    vec3  color;             // 0..11
    uint  octreeIndex;       // 12
    uint  providerKind;      // 16  (0 = Stored/ESVO, 1 = Procedural)
    uint  recipeId;          // 20
    float recipeParams[6];   // 24..47
};
#endif
