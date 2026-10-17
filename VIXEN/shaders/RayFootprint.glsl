#ifndef RAY_FOOTPRINT_GLSL
#define RAY_FOOTPRINT_GLSL
// Traversal's query footprint defaults to the scene push constant. The primary
// sampling pass selects a native footprint for its separate picking query.
#ifndef VIXEN_QUERY_RAY_SIZE_COEF
#define VIXEN_QUERY_RAY_SIZE_COEF pc.raySizeCoef
#endif
#endif
