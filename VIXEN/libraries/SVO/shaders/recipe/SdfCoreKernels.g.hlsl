// GENERATED from SdfCoreKernels.cs by the kernel-framework HLSL emitter.
// Do not edit; regenerate via --recipe-kernels-cpp.
#ifndef SDF_CORE_KERNELS_G_HLSL
#define SDF_CORE_KERNELS_G_HLSL

float SdfCore_Sphere(float3 p, float3 center, float radius) {
    return length(p - center) - radius;
}

float SdfCore_Union(float a, float b) {
    return min(a, b);
}

float SdfCore_Box(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float SdfCore_SmoothUnion(float a, float b, float k) {
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

float3 SdfCore_MirrorX(float3 p) {
    return float3(abs(p.x), p.y, p.z);
}

float3 SdfCore_MirrorY(float3 p) {
    return float3(p.x, abs(p.y), p.z);
}

float3 SdfCore_MirrorZ(float3 p) {
    return float3(p.x, p.y, abs(p.z));
}

float3 SdfCore_Elongate(float3 p, float3 h) {
    return p - clamp(p, -h, h);
}

float3 SdfCore_Revolution(float3 p, float3 center, float offset) {
    float3 pp = p - center;
    float2 q = float2(length(float2(pp.x, pp.z)) - offset, pp.y);
    return float3(q.x, q.y, 0) + center;
}

float3 SdfCore_Twist(float3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    float2 q = float2(c * p.x - s * p.z, s * p.x + c * p.z);
    return float3(q.x, p.y, q.y);
}

float3 SdfCore_Bend(float3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    float2 q = float2(c * p.x - s * p.y, s * p.x + c * p.y);
    return float3(q.x, q.y, p.z);
}

float3 SdfCore_RepeatInfinite(float3 p, float3 spacing) {
    return fmod(abs(p) + spacing * 0.5, spacing) - spacing * 0.5;
}

float3 SdfCore_RepeatLimited(float3 p, float spacing, float3 limit) {
    return p - spacing * clamp(round(p / spacing), -limit, limit);
}

float3 SdfCore_Transform(float3 p, float3 translation, float4 invRotXYZW, float3 invScale) {
    float3 v = p - translation;
    float3 qv = float3(invRotXYZW.x, invRotXYZW.y, invRotXYZW.z);
    float qw = invRotXYZW.w;
    float3 t = 2.0 * cross(qv, v);
    float3 rotated = v + qw * t + cross(qv, t);
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
    float h = saturate(0.5 - 0.5 * (b - a) / k);
    return lerp(b, a, h) + k * h * (1.0 - h);
}

float SdfCore_SmoothSubtract(float a, float b, float k) {
    float h = saturate(0.5 - 0.5 * (b + a) / k);
    return lerp(a, -b, h) + k * h * (1.0 - h);
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

float SdfCore_BoxRounded(float3 p, float3 halfExtents, float roundRadius) {
    float3 q = abs(p) - halfExtents + roundRadius;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - roundRadius;
}

float SdfCore_Capsule(float3 p, float height, float radius) {
    float3 localP = p;
    localP.y -= clamp(localP.y, -height, height);
    return length(localP) - radius;
}

float SdfCore_Cylinder(float3 p, float height, float radius) {
    float2 d = float2(length(float2(p.x, p.z)) - radius, abs(p.y) - height);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float SdfCore_Plane(float3 p, float3 normal, float distance) {
    return dot(p, normal) + distance;
}

float SdfCore_Torus(float3 p, float majorRadius, float minorRadius) {
    float2 q = float2(length(float2(p.x, p.z)) - majorRadius, p.y);
    return length(q) - minorRadius;
}

float SdfCore_Ellipsoid(float3 p, float3 radii) {
    float3 safeRadii = max(radii, 0.0001);
    float k0 = length(p / safeRadii);
    float k1 = length(p / (safeRadii * safeRadii));
    return k0 * (k0 - 1.0) / max(k1, 0.0001);
}

float SdfCore_HollowCylinder(float3 p, float halfLen, float outerR, float wall) {
    float2 d = float2(length(float2(p.x, p.z)) - outerR, abs(p.y) - halfLen);
    float cyl = min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
    return abs(cyl) - wall;
}

float SdfCore_TaperedCylinder(float3 p, float height, float r1, float r2) {
    float2 q = float2(length(float2(p.x, p.z)), p.y);
    float2 k1 = float2(r2, height);
    float2 k2 = float2(r2 - r1, 2.0 * height);
    float2 ca = float2(q.x - min(q.x, ((q.y < 0.0) ? r1 : r2)), abs(q.y) - height);
    float2 cb = q - k1 + k2 * saturate(dot(k1 - q, k2) / dot(k2, k2));
    float s = ((cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0);
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

float SdfCore_Cone(float3 p, float2 angle, float height) {
    float2 q = height * float2(angle.x / angle.y, -1.0);
    float2 w = float2(length(float2(p.x, p.z)), p.y);
    float2 a = w - q * saturate(dot(w, q) / dot(q, q));
    float2 b = w - q * float2(saturate(w.x / q.x), 1.0);
    float k = sign(q.y);
    float d = min(dot(a, a), dot(b, b));
    float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

float SdfCore_CappedTorus(float3 p, float2 sc, float majorRadius, float minorRadius) {
    float3 localP = p;
    localP.x = abs(localP.x);
    float k = ((sc.y * localP.x > sc.x * localP.z) ? dot(float2(localP.x, localP.z), sc) : length(float2(localP.x, localP.z)));
    return sqrt(dot(localP, localP) + majorRadius * majorRadius - 2.0 * majorRadius * k) - minorRadius;
}

float SdfCore_Link(float3 p, float halfLength, float majorRadius, float minorRadius) {
    float3 q = float3(p.x, max(abs(p.y) - halfLength, 0.0), p.z);
    return length(float2(length(float2(q.x, q.y)) - majorRadius, q.z)) - minorRadius;
}

float SdfCore_TriangularPrism(float3 p, float2 h) {
    float3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}

float SdfCore_HexPrism(float3 p, float2 h) {
    float k0 = 0.8660254;
    float kz = 0.57735;
    float3 q = abs(p);
    float dotVal = min(dot(float2(-k0, 0.5), float2(q.x, q.z)), 0.0);
    q.x -= 2.0 * dotVal * (-k0);
    q.z -= 2.0 * dotVal * 0.5;
    float2 d = float2(length(float2(q.x, q.z) - float2(clamp(q.x, -kz * h.x, kz * h.x), h.x)) * sign(q.z - h.x), q.y - h.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float SdfCore_Pyramid(float3 p, float height) {
    float m2 = height * height + 0.25;
    float3 q = float3(abs(p.x), p.y, abs(p.z));
    q = ((q.z > q.x) ? float3(q.z, q.y, q.x) : q);
    q.x -= 0.5;
    q.z -= 0.5;
    float3 a = float3(q.z, height * q.y - 0.5 * q.x, height * q.x + 0.5 * q.y);
    float s = max(-a.x, 0.0);
    float t = saturate((a.y - 0.5 * q.z) / (m2 + 0.25));
    float da = m2 * (a.x + s) * (a.x + s) + a.y * a.y;
    float db = m2 * (a.x + 0.5 * t) * (a.x + 0.5 * t) + (a.y - m2 * t) * (a.y - m2 * t);
    float d2 = ((min(a.y, -a.x * m2 - a.y * 0.5) > 0.0) ? 0.0 : min(da, db));
    return sqrt((d2 + a.z * a.z) / m2) * sign(max(a.z, -q.y));
}

float SdfCore_Segment(float3 p, float3 a, float3 b, float radius) {
    float3 pa = p - a;
    float3 ba = b - a;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h) - radius;
}

float SdfCore_FakeRoundCone(float3 p, float r1, float r2, float height) {
    float2 q = float2(length(float2(p.x, p.z)), p.y);
    float h = saturate(q.y / height);
    float r = lerp(r1, r2, h);
    return length(float2(q.x, q.y - height * h)) - r;
}

float SdfCore_RoundCone(float3 p, float r1, float r2, float height) {
    float2 q = float2(length(float2(p.x, p.z)), p.y);
    float b = (r1 - r2) / height;
    float a = sqrt(1.0 - b * b);
    float k = dot(q, float2(-b, a));
    float regionA = length(q) - r1;
    float regionB = length(q - float2(0.0, height)) - r2;
    float regionC = dot(q, float2(a, b)) - r1;
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
    return frac(x);
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
    return saturate(x);
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
    return lerp(a, b, t);
}

float SdfCore_Select(float cond, float a, float b, float thr) {
    return (cond > thr ? a : b);
}

float SdfCore_Displacement(float sdf, float disp, float scale) {
    return sdf + disp * scale;
}

float3 SdfCore_Float3Add(float3 a, float3 b) {
    return a + b;
}

float3 SdfCore_Float3Sub(float3 a, float3 b) {
    return a - b;
}

float3 SdfCore_Float3MulComponentWise(float3 a, float3 b) {
    return a * b;
}

float3 SdfCore_Float3Min(float3 a, float3 b) {
    return min(a, b);
}

float3 SdfCore_Float3Max(float3 a, float3 b) {
    return max(a, b);
}

float3 SdfCore_Float3ScalarMul(float3 v, float s) {
    return v * s;
}

float SdfCore_Float3Dot(float3 a, float3 b) {
    return dot(a, b);
}

float3 SdfCore_Float3Normalize(float3 v) {
    float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
    float invLen = (lenSq < 1e-14 ? 0.0 : 1.0 / sqrt(lenSq));
    return v * invLen;
}

uint SdfCore_Hash32(uint valueBits) {
    uint x = valueBits;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint SdfCore_Hash32Combine(uint stateBits, uint valueBits) {
    uint x = (stateBits ^ valueBits) + 0x9e3779b9u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint SdfCore_U32Add(uint a, uint b) {
    return a + b;
}

uint SdfCore_U32Sub(uint a, uint b) {
    return a - b;
}

uint SdfCore_U32Mul(uint a, uint b) {
    return a * b;
}

uint SdfCore_U32Min(uint a, uint b) {
    return min(a, b);
}

uint SdfCore_U32Max(uint a, uint b) {
    return max(a, b);
}

uint SdfCore_U32Equal(uint a, uint b) {
    return (a == b ? 1u : 0u);
}

uint SdfCore_U32NotEqual(uint a, uint b) {
    return (a != b ? 1u : 0u);
}

uint SdfCore_U32Less(uint a, uint b) {
    return (a < b ? 1u : 0u);
}

uint SdfCore_U32LessEqual(uint a, uint b) {
    return (a <= b ? 1u : 0u);
}

uint SdfCore_U32Greater(uint a, uint b) {
    return (a > b ? 1u : 0u);
}

uint SdfCore_U32GreaterEqual(uint a, uint b) {
    return (a >= b ? 1u : 0u);
}

int SdfCore_Q16Add(int a, int b) {
    if (b > 0 && a > 2147483647 - b)
    {
        return 2147483647;
    }
    if (b < 0 && a < -2147483647 - 1 - b)
    {
        return -2147483647 - 1;
    }
    return a + b;
}

int SdfCore_Q16Sub(int a, int b) {
    if (b < 0 && a > 2147483647 + b)
    {
        return 2147483647;
    }
    if (b > 0 && a < -2147483647 - 1 + b)
    {
        return -2147483647 - 1;
    }
    return a - b;
}

int SdfCore_Q16Mul(int a, int b) {
    bool negative = (a < 0) != (b < 0);
    uint aMagnitude = (a < 0 ? (uint)(-(a + 1)) + 1u : (uint)a);
    uint bMagnitude = (b < 0 ? (uint)(-(b + 1)) + 1u : (uint)b);
    uint aLow = aMagnitude & 0xffffu;
    uint aHigh = aMagnitude >> 16;
    uint bLow = bMagnitude & 0xffffu;
    uint bHigh = bMagnitude >> 16;
    uint lowProduct = aLow * bLow;
    uint middle = aHigh * bLow + (lowProduct >> 16);
    uint middleLow = middle & 0xffffu;
    uint middleHigh = middle >> 16;
    middle = aLow * bHigh + middleLow;
    uint productLow = (middle << 16) | (lowProduct & 0xffffu);
    uint productHigh = aHigh * bHigh + middleHigh + (middle >> 16);
    uint remainder = productLow & 0xffffu;
    uint roundedLow = (productHigh << 16) | (productLow >> 16);
    uint roundedHigh = productHigh >> 16;
    if (remainder > 0x8000u || (remainder == 0x8000u && (roundedLow & 1u) != 0u))
    {
        roundedLow++;
        if (roundedLow == 0u)
        {
            roundedHigh++;
        }
    }
    uint limit = (negative ? 0x80000000u : 0x7fffffffu);
    if (roundedHigh != 0u || roundedLow > limit)
    {
        return (negative ? -2147483647 - 1 : 2147483647);
    }
    if (!negative)
    {
        return (int)roundedLow;
    }
    if (roundedLow == 0x80000000u)
    {
        return -2147483647 - 1;
    }
    return -(int)roundedLow;
}

int SdfCore_Q16Min(int a, int b) {
    return min(a, b);
}

int SdfCore_Q16Max(int a, int b) {
    return max(a, b);
}

uint SdfCore_Q16Equal(int a, int b) {
    return (a == b ? 1u : 0u);
}

uint SdfCore_Q16NotEqual(int a, int b) {
    return (a != b ? 1u : 0u);
}

uint SdfCore_Q16Less(int a, int b) {
    return (a < b ? 1u : 0u);
}

uint SdfCore_Q16LessEqual(int a, int b) {
    return (a <= b ? 1u : 0u);
}

uint SdfCore_Q16Greater(int a, int b) {
    return (a > b ? 1u : 0u);
}

uint SdfCore_Q16GreaterEqual(int a, int b) {
    return (a >= b ? 1u : 0u);
}

int SdfCore_FloatToQ16(float value) {
    uint bits = asuint(value);
    bool negative = (bits & 0x80000000u) != 0u;
    uint exponent = (bits >> 23) & 0xffu;
    uint mantissa = bits & 0x7fffffu;
    if (exponent == 0xffu)
    {
        if (mantissa != 0u)
        {
            return 0;
        }
        return (negative ? -2147483647 - 1 : 2147483647);
    }
    if (exponent == 0u)
    {
        return 0;
    }
    mantissa |= 0x800000u;
    if (exponent >= 142u)
    {
        return (negative ? -2147483647 - 1 : 2147483647);
    }
    int shift = (int)exponent - 134;
    uint magnitude = 0;
    if (shift >= 0)
    {
        magnitude = mantissa << (uint)shift;
    }
    else
    {
        uint rightShift = (uint)(-shift);
        if (rightShift > 24u)
        {
            magnitude = 0u;
        }
        else
        {
            uint quotient = mantissa >> rightShift;
            uint remainder = mantissa & ((1u << rightShift) - 1u);
            uint halfway = 1u << (rightShift - 1u);
            if (remainder > halfway || (remainder == halfway && (quotient & 1u) != 0u))
            {
                quotient++;
            }
            magnitude = quotient;
        }
    }
    if (!negative)
    {
        return (int)magnitude;
    }
    if (magnitude == 0x80000000u)
    {
        return -2147483647 - 1;
    }
    return -(int)magnitude;
}

float SdfCore_Q16ToFloat(int value) {
    return (float)value * (1.0 / 65536.0);
}

int SdfCore_U32ToQ16(uint value) {
    if (value > 32767u)
    {
        return 2147483647;
    }
    return (int)(value << 16);
}

uint SdfCore_Q16ToU32(int value) {
    if (value < 0)
    {
        return 0u;
    }
    uint whole = (uint)value >> 16;
    uint remainder = (uint)value & 0xffffu;
    if (remainder > 0x8000u || (remainder == 0x8000u && (whole & 1u) != 0u))
    {
        whole++;
    }
    if (whole > 0xffffffffu)
    {
        return 0xffffffffu;
    }
    return whole;
}

float SdfCore_U32ToFloat(uint value) {
    return (float)value;
}


#endif // SDF_CORE_KERNELS_G_HLSL
