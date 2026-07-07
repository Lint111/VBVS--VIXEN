#include "Ui/ViewWireReader.h"
#include <RmlUi/Core/Log.h>
#include <cstring>
#include <string>

namespace Vixen::RenderGraph {

namespace {

// Little-endian cursor over the wire; every read bounds-checks and sets ok=false on overrun.
struct Cursor {
    const std::byte* p;
    size_t n;
    size_t at = 0;
    bool ok = true;

    bool Need(size_t k) { if (at + k > n) { ok = false; } return ok; }
    uint8_t U8()  { if (!Need(1)) return 0; return static_cast<uint8_t>(p[at++]); }
    uint32_t U32() { if (!Need(4)) return 0;
        uint32_t v = static_cast<uint8_t>(p[at]) | (static_cast<uint8_t>(p[at+1])<<8)
                   | (static_cast<uint8_t>(p[at+2])<<16) | (static_cast<uint8_t>(p[at+3])<<24);
        at += 4; return v; }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    float F32() { uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    bool Bool() { return U8() != 0; }
    std::string Str() {
        uint32_t len = U32();
        if (!ok || !Need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len; return s;
    }
};

}  // namespace

bool ViewWireReader::Apply(std::span<const std::byte> wire, ViewStore& store) {
    Cursor c{ wire.data(), wire.size() };

    // Header: 'U','T','V','A' + version u32 + top-field-count u32.
    if (!c.Need(12)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: truncated header"); return false; }
    if (c.U8()!='U' || c.U8()!='T' || c.U8()!='V' || c.U8()!='A') {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: bad magic (expected UTVA)"); return false;
    }
    uint32_t wireVer = c.U32();
    uint32_t fieldCount = c.U32();

    // Version guard (spec §5.3) — hard boundary error, store untouched.
    if (wireVer != store.Version()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReader: schema version mismatch (wire=0x%08X store=0x%08X) — skipping",
            wireVer, store.Version());
        return false;
    }

    const auto& fields = store.Blob().fields;
    if (fieldCount != fields.size()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReader: top-field count mismatch (wire=%u blob=%zu)", fieldCount, fields.size());
        return false;
    }

    // Decode body in declared order, driving ViewStore's validated setters.
    for (const auto& f : fields) {
        switch (f.kind) {
            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::I(v)); break; }
            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::F(v)); break; }
            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::B(v)); break; }
            case ViewKind::String: { std::string v = c.Str(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::S(std::move(v))); break; }
            case ViewKind::ArrayOfStruct: {
                uint32_t rows = c.U32();
                if (!c.ok) break;
                auto h = store.ResizeArray(f.name, rows);
                for (uint32_t r = 0; r < rows && c.ok; ++r) {
                    for (const auto& ef : f.elem) {
                        switch (ef.kind) {
                            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::I(v)); break; }
                            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::F(v)); break; }
                            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::B(v)); break; }
                            case ViewKind::String: { std::string v = c.Str(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::S(std::move(v))); break; }
                            default: c.ok = false; break;   // nested arrays unsupported in the kind catalogue
                        }
                        if (!c.ok) break;
                    }
                }
                break;
            }
        }
        if (!c.ok) break;
    }

    if (!c.ok) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: malformed body (overran buffer)");
        return false;
    }
    if (c.at != c.n) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReader: %zu trailing bytes", c.n - c.at);
        return false;
    }
    return true;
}

}  // namespace Vixen::RenderGraph
