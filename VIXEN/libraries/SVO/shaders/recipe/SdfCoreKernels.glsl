// Mechanically translated (NOT source-generated) from the kernel-framework-emitted
// SdfCoreKernels.g.hlsl (vendored .g.* stays VERBATIM — see that file's own header). This
// is a derived artifact, hand-translated function-for-function; test_sdf_core_glsl_name_set
// drift-guards its SdfCore_* name set against the HLSL core's so a kernel-side core update
// (regenerated .g.hlsl) fails loudly here instead of silently drifting.
//
// Translation rules applied uniformly:
//   float3/float2/float4 -> vec3/vec2/vec4
//   lerp(a,b,t)           -> mix(a,b,t)
//   saturate(x)            -> clamp(x, 0.0, 1.0)
//   frac(x)                -> fract(x)
//   fmod(a,b)               -> mod(a,b) [safe here: every fmod call site's first operand is
//                              already non-negative (abs(...)), so HLSL's truncated-toward-zero
//                              fmod and GLSL's floored mod agree]
//   ternary (cond ? a : b)  -> unchanged (valid in both languages)
// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 7.

#ifndef SDF_CORE_KERNELS_GLSL
#define SDF_CORE_KERNELS_GLSL

float SdfCore_Sphere(vec3 p, vec3 center, float radius) {
    return length(p - center) - radius;
}

float SdfCore_Union(float a, float b) {
    return min(a, b);
}

float SdfCore_Box(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float SdfCore_SmoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

vec3 SdfCore_MirrorX(vec3 p) {
    return vec3(abs(p.x), p.y, p.z);
}

vec3 SdfCore_MirrorY(vec3 p) {
    return vec3(p.x, abs(p.y), p.z);
}

vec3 SdfCore_MirrorZ(vec3 p) {
    return vec3(p.x, p.y, abs(p.z));
}

vec3 SdfCore_Elongate(vec3 p, vec3 h) {
    return p - clamp(p, -h, h);
}

vec3 SdfCore_Revolution(vec3 p, vec3 center, float offset) {
    vec3 pp = p - center;
    vec2 q = vec2(length(vec2(pp.x, pp.z)) - offset, pp.y);
    return vec3(q.x, q.y, 0.0) + center;
}

vec3 SdfCore_Twist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    vec2 q = vec2(c * p.x - s * p.z, s * p.x + c * p.z);
    return vec3(q.x, p.y, q.y);
}

vec3 SdfCore_Bend(vec3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    vec2 q = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    return vec3(q.x, q.y, p.z);
}

vec3 SdfCore_RepeatInfinite(vec3 p, vec3 spacing) {
    return mod(abs(p) + spacing * 0.5, spacing) - spacing * 0.5;
}

vec3 SdfCore_RepeatLimited(vec3 p, float spacing, vec3 limit) {
    return p - spacing * clamp(round(p / spacing), -limit, limit);
}

vec3 SdfCore_Transform(vec3 p, vec3 translation, vec4 invRotXYZW, vec3 invScale) {
    vec3 v = p - translation;
    vec3 qv = vec3(invRotXYZW.x, invRotXYZW.y, invRotXYZW.z);
    float qw = invRotXYZW.w;
    vec3 t = 2.0 * cross(qv, v);
    vec3 rotated = v + qw * t + cross(qv, t);
    return rotated * invScale;
}

float SdfCore_Intersect(float a, float b) {
    return max(a, b);
}

float SdfCore_Subtract(float a, float b) {
    return max(a, -b);
}

float SdfCore_Xor(float a, float b) {
    return max(min(a, b), -max(a, b));
}

float SdfCore_SmoothIntersect(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

float SdfCore_SmoothSubtract(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (b + a) / k, 0.0, 1.0);
    return mix(a, -b, h) + k * h * (1.0 - h);
}

float SdfCore_SmoothMax(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothUnionCubic(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothIntersectCubic(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothSubtractCubic(float a, float b, float k) {
    float h = max(k - abs(-b - a), 0.0) / k;
    return max(a, -b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_Round(float d, float r) {
    return d - r;
}

float SdfCore_Onion(float d, float r) {
    return abs(d) - r;
}

float SdfCore_BoxRounded(vec3 p, vec3 halfExtents, float roundRadius) {
    vec3 q = abs(p) - halfExtents + roundRadius;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - roundRadius;
}

float SdfCore_Capsule(vec3 p, float height, float radius) {
    vec3 localP = p;
    localP.y -= clamp(localP.y, -height, height);
    return length(localP) - radius;
}

float SdfCore_Cylinder(vec3 p, float height, float radius) {
    vec2 d = vec2(length(vec2(p.x, p.z)) - radius, abs(p.y) - height);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float SdfCore_Plane(vec3 p, vec3 normal, float distance) {
    return dot(p, normal) + distance;
}

float SdfCore_Torus(vec3 p, float majorRadius, float minorRadius) {
    vec2 q = vec2(length(vec2(p.x, p.z)) - majorRadius, p.y);
    return length(q) - minorRadius;
}

float SdfCore_Ellipsoid(vec3 p, vec3 radii) {
    vec3 safeRadii = max(radii, 0.0001);
    float k0 = length(p / safeRadii);
    float k1 = length(p / (safeRadii * safeRadii));
    return k0 * (k0 - 1.0) / max(k1, 0.0001);
}

float SdfCore_HollowCylinder(vec3 p, float halfLen, float outerR, float wall) {
    vec2 d = vec2(length(vec2(p.x, p.z)) - outerR, abs(p.y) - halfLen);
    float cyl = min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
    return abs(cyl) - wall;
}

float SdfCore_TaperedCylinder(vec3 p, float height, float r1, float r2) {
    vec2 q = vec2(length(vec2(p.x, p.z)), p.y);
    vec2 k1 = vec2(r2, height);
    vec2 k2 = vec2(r2 - r1, 2.0 * height);
    vec2 ca = vec2(q.x - min(q.x, ((q.y < 0.0) ? r1 : r2)), abs(q.y) - height);
    vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
    float s = ((cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0);
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

float SdfCore_Cone(vec3 p, vec2 angle, float height) {
    vec2 q = height * vec2(angle.x / angle.y, -1.0);
    vec2 w = vec2(length(vec2(p.x, p.z)), p.y);
    vec2 a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
    vec2 b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
    float k = sign(q.y);
    float d = min(dot(a, a), dot(b, b));
    float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

float SdfCore_CappedTorus(vec3 p, vec2 sc, float majorRadius, float minorRadius) {
    vec3 localP = p;
    localP.x = abs(localP.x);
    float k = ((sc.y * localP.x > sc.x * localP.z) ? dot(vec2(localP.x, localP.z), sc) : length(vec2(localP.x, localP.z)));
    return sqrt(dot(localP, localP) + majorRadius * majorRadius - 2.0 * majorRadius * k) - minorRadius;
}

float SdfCore_Link(vec3 p, float halfLength, float majorRadius, float minorRadius) {
    vec3 q = vec3(p.x, max(abs(p.y) - halfLength, 0.0), p.z);
    return length(vec2(length(vec2(q.x, q.y)) - majorRadius, q.z)) - minorRadius;
}

float SdfCore_TriangularPrism(vec3 p, vec2 h) {
    vec3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}

float SdfCore_HexPrism(vec3 p, vec2 h) {
    float k0 = 0.8660254;
    float kz = 0.57735;
    vec3 q = abs(p);
    float dotVal = min(dot(vec2(-k0, 0.5), vec2(q.x, q.z)), 0.0);
    q.x -= 2.0 * dotVal * (-k0);
    q.z -= 2.0 * dotVal * 0.5;
    vec2 d = vec2(length(vec2(q.x, q.z) - vec2(clamp(q.x, -kz * h.x, kz * h.x), h.x)) * sign(q.z - h.x), q.y - h.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float SdfCore_Pyramid(vec3 p, float height) {
    float m2 = height * height + 0.25;
    vec3 q = vec3(abs(p.x), p.y, abs(p.z));
    q = ((q.z > q.x) ? vec3(q.z, q.y, q.x) : q);
    q.x -= 0.5;
    q.z -= 0.5;
    vec3 a = vec3(q.z, height * q.y - 0.5 * q.x, height * q.x + 0.5 * q.y);
    float s = max(-a.x, 0.0);
    float t = clamp((a.y - 0.5 * q.z) / (m2 + 0.25), 0.0, 1.0);
    float da = m2 * (a.x + s) * (a.x + s) + a.y * a.y;
    float db = m2 * (a.x + 0.5 * t) * (a.x + 0.5 * t) + (a.y - m2 * t) * (a.y - m2 * t);
    float d2 = ((min(a.y, -a.x * m2 - a.y * 0.5) > 0.0) ? 0.0 : min(da, db));
    return sqrt((d2 + a.z * a.z) / m2) * sign(max(a.z, -q.y));
}

float SdfCore_Segment(vec3 p, vec3 a, vec3 b, float radius) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - radius;
}

float SdfCore_FakeRoundCone(vec3 p, float r1, float r2, float height) {
    vec2 q = vec2(length(vec2(p.x, p.z)), p.y);
    float h = clamp(q.y / height, 0.0, 1.0);
    float r = mix(r1, r2, h);
    return length(vec2(q.x, q.y - height * h)) - r;
}

float SdfCore_RoundCone(vec3 p, float r1, float r2, float height) {
    vec2 q = vec2(length(vec2(p.x, p.z)), p.y);
    float b = (r1 - r2) / height;
    float a = sqrt(1.0 - b * b);
    float k = dot(q, vec2(-b, a));
    float regionA = length(q) - r1;
    float regionB = length(q - vec2(0.0, height)) - r2;
    float regionC = dot(q, vec2(a, b)) - r1;
    float d = ((k < 0.0) ? regionA : regionC);
    d = ((k > a * height) ? regionB : d);
    return d;
}

float SdfCore_MathSin(float x, float frequency, float phase, float amplitude) {
    return sin(x * frequency + phase) * amplitude;
}

float SdfCore_MathCos(float x, float frequency, float phase, float amplitude) {
    return cos(x * frequency + phase) * amplitude;
}

float SdfCore_MathSmoothstep(float x, float edge0, float edge1) {
    return smoothstep(edge0, edge1, x);
}

float SdfCore_MathRemap(float x, float inMin, float inMax, float outMin, float outMax) {
    return outMin + (x - inMin) / max(inMax - inMin, 1e-8) * (outMax - outMin);
}

float SdfCore_MathClamp(float x, float lo, float hi) {
    return clamp(x, lo, hi);
}

float SdfCore_MathAbs(float x) {
    return abs(x);
}

float SdfCore_MathFrac(float x) {
    return fract(x);
}

float SdfCore_MathPow(float x, float power) {
    return pow(abs(x), power) * sign(x);
}

float SdfCore_MathSqrt(float x) {
    return sqrt(abs(x));
}

float SdfCore_MathNegate(float x) {
    return -x;
}

float SdfCore_MathStep(float x, float edge) {
    return step(edge, x);
}

float SdfCore_MathSign(float x) {
    return sign(x);
}

float SdfCore_MathSaturate(float x) {
    return clamp(x, 0.0, 1.0);
}

float SdfCore_MathExp(float x) {
    return exp(x);
}

float SdfCore_MathLog(float x) {
    return log(max(x, 1e-30));
}

float SdfCore_MathLog2(float x) {
    return log2(max(x, 1e-30));
}

float SdfCore_MathAdd(float a, float b) {
    return a + b;
}

float SdfCore_MathSub(float a, float b) {
    return a - b;
}

float SdfCore_MathMul(float a, float b) {
    return a * b;
}

float SdfCore_MathDiv(float a, float b) {
    return (b != 0.0 ? a / b : 0.0);
}

float SdfCore_MathMin(float a, float b) {
    return min(a, b);
}

float SdfCore_MathMax(float a, float b) {
    return max(a, b);
}

float SdfCore_MathLerp(float a, float b, float t) {
    return mix(a, b, t);
}

float SdfCore_Select(float cond, float a, float b, float thr) {
    return (cond > thr ? a : b);
}

float SdfCore_Displacement(float sdf, float disp, float scale) {
    return sdf + disp * scale;
}

vec3 SdfCore_Float3Add(vec3 a, vec3 b) {
    return a + b;
}

vec3 SdfCore_Float3Sub(vec3 a, vec3 b) {
    return a - b;
}

vec3 SdfCore_Float3MulComponentWise(vec3 a, vec3 b) {
    return a * b;
}

vec3 SdfCore_Float3Min(vec3 a, vec3 b) {
    return min(a, b);
}

vec3 SdfCore_Float3Max(vec3 a, vec3 b) {
    return max(a, b);
}

vec3 SdfCore_Float3ScalarMul(vec3 v, float s) {
    return v * s;
}

float SdfCore_Float3Dot(vec3 a, vec3 b) {
    return dot(a, b);
}

vec3 SdfCore_Float3Normalize(vec3 v) {
    float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
    float invLen = (lenSq < 1e-14 ? 0.0 : 1.0 / sqrt(lenSq));
    return v * invLen;
}

#endif // SDF_CORE_KERNELS_GLSL
