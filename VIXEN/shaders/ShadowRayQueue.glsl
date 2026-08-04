#ifndef SHADOW_RAY_QUEUE_GLSL
#define SHADOW_RAY_QUEUE_GLSL
// ============================================================================
// W1 (wavefront epoch): the shadow-ray queue's shared record layout.
// ============================================================================
// THIS file owns the request layout — the traversal wave (ShadowRayTrace.comp,
// the consumer) and every gather pass that emits requests (first customer:
// ProbeGather.comp) include it, so producer and consumer cannot drift. Buffer
// DECLARATIONS stay per-shader (access qualifiers differ: emitters declare the
// request buffer writeonly, the wave readonly), only the struct is shared.
//
// 32 B per request under std430 (vec3+float pairs pack tight, no padding).
// Result records are a bare uint per slot (1 = visible — unoccluded or
// invalid slot — 0 = occluded), declared per-shader; no struct needed.
// ============================================================================

struct ShadowRayRequest {
    vec3 origin; float tmin;
    vec3 dir;    float tmax;   // tmax <= tmin => invalid slot (no ray emitted)
};

#endif // SHADOW_RAY_QUEUE_GLSL
