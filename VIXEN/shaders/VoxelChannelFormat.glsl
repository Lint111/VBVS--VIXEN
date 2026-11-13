#ifndef VOXEL_CHANNEL_FORMAT_GLSL
#define VOXEL_CHANNEL_FORMAT_GLSL
// The pinned runtime-vocabulary emitter writes unsigned literal suffixes into
// a preprocessor `#if` (`#define MAT_COUNT 0u`), which glslang rejects before
// shader compilation. Keep the generated artifact and its codegen check intact,
// while this consumer shim exposes the same semantic ids to GLSL until the
// framework emitter gains a preprocessor-safe literal mode.
#define SEM_SDF 0u
#define SEM_COLOR 1u
#define SEM_ROUGHNESS 2u
#define SEM_NORMAL 3u
#define SEM_METALLIC 4u
#define SEM_EMISSION 5u
#define SEM_DENSITY 6u
#define SEM_MATERIAL_ID 7u
#define SEM_AO 8u
#define SEM_THICKNESS 9u
#define SEM_VECTOR_FIELD 10u
#define SEM_CUSTOM0 11u
#define SEM_CUSTOM1 12u
#define SEM_CUSTOM2 13u
#define SEM_CUSTOM3 14u
#define SEM_EDGE_SMOOTHNESS 15u
#define SEM_EMISSION_COLOR 16u
#define SEM_COUNT 17u
#define FK_NONE 0u
#define FK_DISTANCE 1u
#define FK_DENSITY 2u
#define VX_VOXELS_PER_BRICK 512u
#endif
