#include "Ui/ViewWireReaderSoa.h"
#include <RmlUi/Core/Log.h>
#include <cstring>
#include <string>
#include <vector>

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
    uint64_t U64() { if (!Need(8)) return 0;
        uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(static_cast<uint8_t>(p[at+i])) << (8*i));
        at += 8; return v; }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    float F32() { uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    bool Bool() { return U8() != 0; }
    std::string Str() {
        uint32_t len = U32();
        if (!ok || !Need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len; return s;
    }

    // Reads a String COLUMN: (rowCount+1) u32 offsets (offsets[0]==0, monotonically increasing)
    // followed by the blob of just this column's concatenated UTF8 bytes. Returns one string per
    // row, sliced out of the column's own blob by consecutive offset pairs.
    std::vector<std::string> StrColumn(uint32_t rowCount) {
        std::vector<uint32_t> offsets(rowCount + 1);
        for (uint32_t i = 0; i <= rowCount; ++i) offsets[i] = U32();
        if (!ok) return {};
        uint32_t blobLen = offsets[rowCount];
        if (!Need(blobLen)) return {};
        const char* blobStart = reinterpret_cast<const char*>(p + at);
        std::vector<std::string> out(rowCount);
        for (uint32_t i = 0; i < rowCount; ++i) {
            if (offsets[i] > offsets[i + 1] || offsets[i + 1] > blobLen) { ok = false; return {}; }
            out[i].assign(blobStart + offsets[i], offsets[i + 1] - offsets[i]);
        }
        at += blobLen;
        return out;
    }
};

}  // namespace

bool ViewWireReaderSoa::Apply(std::span<const std::byte> wire, ViewStore& store) {
    Cursor c{ wire.data(), wire.size() };

    // Header: 'U','T','V','A' + version u32 + top-field-count u32 (same header as AoS; the SoA
    // shape is scoped to how ArrayOfStruct bodies encode, not a different magic).
    if (!c.Need(12)) { Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReaderSoa: truncated header"); return false; }
    if (c.U8()!='U' || c.U8()!='T' || c.U8()!='V' || c.U8()!='A') {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReaderSoa: bad magic (expected UTVA)"); return false;
    }
    uint32_t wireVer = c.U32();
    uint32_t fieldCount = c.U32();

    if (wireVer != store.Version()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReaderSoa: schema version mismatch (wire=0x%08X store=0x%08X) — skipping",
            wireVer, store.Version());
        return false;
    }

    const auto& fields = store.Blob().fields;
    if (fieldCount != fields.size()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ViewWireReaderSoa: top-field count mismatch (wire=%u blob=%zu)", fieldCount, fields.size());
        return false;
    }

    for (const auto& f : fields) {
        switch (f.kind) {
            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::I(v)); break; }
            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::F(v)); break; }
            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::B(v)); break; }
            case ViewKind::String: { std::string v = c.Str(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::S(std::move(v))); break; }
            case ViewKind::U64:    { uint64_t v = c.U64(); if (!c.ok) break; store.SetScalar(f.name, ViewValue::U64(v)); break; }
            case ViewKind::Vector: {
                // 3 consecutive F32s (x,y,z), declared field order -- the wire's own field-loop
                // structure carries no special Vector framing beyond "read 3 floats instead of 1".
                Vec3f v; v.x = c.F32(); if (!c.ok) break; v.y = c.F32(); if (!c.ok) break; v.z = c.F32(); if (!c.ok) break;
                store.SetScalar(f.name, ViewValue::Vec(v));
                break;
            }
            case ViewKind::SubjectRef: {
                uint8_t kind = c.U8(); if (!c.ok) break;
                uint64_t instance = c.U64(); if (!c.ok) break;
                store.SetScalar(f.name, ViewValue::Subject(SubjectRef{kind, instance}));
                break;
            }
            case ViewKind::ArrayOfStruct: {
                uint32_t rows = c.U32();
                if (!c.ok) break;
                auto h = store.ResizeArray(f.name, rows);
                for (const auto& ef : f.elem) {
                    if (!c.ok) break;
                    if (ef.kind == ViewKind::String) {
                        auto col = c.StrColumn(rows);
                        if (!c.ok) break;
                        for (uint32_t r = 0; r < rows; ++r) h.Set(r, ef.name, ViewValue::S(std::move(col[r])));
                        continue;
                    }
                    for (uint32_t r = 0; r < rows && c.ok; ++r) {
                        switch (ef.kind) {
                            case ViewKind::Int:    { int v = c.I32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::I(v)); break; }
                            case ViewKind::Float:  { float v = c.F32(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::F(v)); break; }
                            case ViewKind::Bool:   { bool v = c.Bool(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::B(v)); break; }
                            // U64 column: rowCount contiguous 8-byte little-endian values, same
                            // fixed-size per-row read shape as Int/Float above (row-deltas step-7b's
                            // stable RowId key column).
                            case ViewKind::U64:    { uint64_t v = c.U64(); if (!c.ok) break; h.Set(r, ef.name, ViewValue::U64(v)); break; }
                            case ViewKind::Vector: {
                                Vec3f v; v.x = c.F32(); if (!c.ok) break; v.y = c.F32(); if (!c.ok) break; v.z = c.F32(); if (!c.ok) break;
                                h.Set(r, ef.name, ViewValue::Vec(v));
                                break;
                            }
                            case ViewKind::SubjectRef: {
                                uint8_t kind = c.U8(); if (!c.ok) break;
                                uint64_t instance = c.U64(); if (!c.ok) break;
                                h.Set(r, ef.name, ViewValue::Subject(SubjectRef{kind, instance}));
                                break;
                            }
                            default: c.ok = false; break;   // nested arrays unsupported in the kind catalogue
                        }
                    }
                }
                break;
            }
            default: c.ok = false; break;   // unrecognized top-level ViewKind
        }
        if (!c.ok) break;
    }

    if (!c.ok) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReaderSoa: malformed body (overran buffer)");
        return false;
    }
    if (c.at != c.n) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "ViewWireReaderSoa: %zu trailing bytes", c.n - c.at);
        return false;
    }
    return true;
}

}  // namespace Vixen::RenderGraph
