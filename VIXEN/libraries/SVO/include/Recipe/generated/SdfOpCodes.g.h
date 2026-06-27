
#pragma once
#include <cstdint>
// <provenance: generated from SDFOpCode — do not edit by hand>
namespace Vixen::SVO::Recipe {
enum class SdfOpCode : uint8_t {
    Sphere               = 0,
    Box                  = 1,
    BoxRounded           = 2,
    Capsule              = 3,
    Cylinder             = 4,
    Plane                = 5,
    Torus                = 6,
    Union                = 24,
    SmoothUnion          = 25,
    Subtract             = 26,
    SmoothSubtract       = 27,
    Intersect            = 28,
    SmoothIntersect      = 29,
    Xor                  = 30,
    SmoothMax            = 31,
    SmoothUnionCubic     = 32,
    SmoothSubtractCubic  = 33,
    SmoothIntersectCubic = 34,
    Round                = 35,
    Onion                = 36,
    MirrorX              = 41,
    RestorePos           = 97,
};
}  // namespace Vixen::SVO::Recipe