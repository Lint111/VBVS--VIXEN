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

enum class ViewKind : uint8_t { Int, Float, Bool, String, ArrayOfStruct };

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

struct ViewValue {
    enum class Tag : uint8_t { Int, Float, Bool, String } tag;
    int         i = 0;
    float       f = 0.0f;
    bool        b = false;
    std::string s;
    static ViewValue I(int v)          { ViewValue r; r.tag = Tag::Int;    r.i = v; return r; }
    static ViewValue F(float v)        { ViewValue r; r.tag = Tag::Float;  r.f = v; return r; }
    static ViewValue B(bool v)         { ViewValue r; r.tag = Tag::Bool;   r.b = v; return r; }
    static ViewValue S(std::string v)  { ViewValue r; r.tag = Tag::String; r.s = std::move(v); return r; }
};

inline bool KindAcceptsValue(ViewKind k, const ViewValue& v) {
    switch (k) {
        case ViewKind::Int:    return v.tag == ViewValue::Tag::Int;
        case ViewKind::Float:  return v.tag == ViewValue::Tag::Float;
        case ViewKind::Bool:   return v.tag == ViewValue::Tag::Bool;
        case ViewKind::String: return v.tag == ViewValue::Tag::String;
        case ViewKind::ArrayOfStruct: return false;
    }
    return false;
}

}  // namespace Vixen::RenderGraph
