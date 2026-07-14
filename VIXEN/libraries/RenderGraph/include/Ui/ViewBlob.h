#pragma once
// The reflection-blob contract for the renderer-agnostic view path (Inc-2b). A ViewBlob describes
// a [View] in the finite kind catalogue; a BlobView builds a live RmlUi data model from it and a
// ViewStore holds the typed data. Delivered as a constexpr header (Generated/Hud.blob.g.h) or a
// runtime .viewblob file (ViewBlobFile) — both produce this struct. No byte offsets/strides.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Vixen::RenderGraph {

// Vector: a flat 3-float field (e.g. a Float3-marker-typed scalar such as Bodies.Position) --
// a genuine distinct kind (design A, View Contract Inc-5b Milestone 2.4), NOT 3 synthetic Float
// sub-fields (which would make fieldCount untruthful). Carries exactly 3 floats (x,y,z); a future
// vector shape (Float2, Float4) would need its own ViewKind rather than overloading this one, since
// ViewValue::Vec below is fixed at 3 components.
enum class ViewKind : uint8_t { Int, Float, Bool, String, ArrayOfStruct, Vector };

struct ViewFieldDesc {
    std::string_view name;
    ViewKind         kind;
    std::span<const ViewFieldDesc> elem;   // element scalar fields; empty unless ArrayOfStruct
};

struct ViewBlob {
    std::string_view               model;
    std::span<const ViewFieldDesc> fields;   // top-level fields, declared order
    uint32_t                       version;  // C#-authored schema hash; engine reads/compares only
};

// A Vector payload is exactly 3 floats (x,y,z) -- matches the Float3 marker's shape; not a
// std::array/glm type since ViewBlob.h stays dependency-free (no math library) and every other
// ViewValue payload here is likewise a bare primitive.
struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct ViewValue {
    enum class Tag : uint8_t { Int, Float, Bool, String, Vector } tag;
    int         i = 0;
    float       f = 0.0f;
    bool        b = false;
    std::string s;
    Vec3f       vec;
    static ViewValue I(int v)          { ViewValue r; r.tag = Tag::Int;    r.i = v; return r; }
    static ViewValue F(float v)        { ViewValue r; r.tag = Tag::Float;  r.f = v; return r; }
    static ViewValue B(bool v)         { ViewValue r; r.tag = Tag::Bool;   r.b = v; return r; }
    static ViewValue S(std::string v)  { ViewValue r; r.tag = Tag::String; r.s = std::move(v); return r; }
    static ViewValue Vec(Vec3f v)      { ViewValue r; r.tag = Tag::Vector; r.vec = v; return r; }
};

inline bool KindAcceptsValue(ViewKind k, const ViewValue& v) {
    switch (k) {
        case ViewKind::Int:    return v.tag == ViewValue::Tag::Int;
        case ViewKind::Float:  return v.tag == ViewValue::Tag::Float;
        case ViewKind::Bool:   return v.tag == ViewValue::Tag::Bool;
        case ViewKind::String: return v.tag == ViewValue::Tag::String;
        case ViewKind::Vector: return v.tag == ViewValue::Tag::Vector;
        case ViewKind::ArrayOfStruct: return false;
    }
    return false;
}

}  // namespace Vixen::RenderGraph
