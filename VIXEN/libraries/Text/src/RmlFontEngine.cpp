#include "Text/RmlFontEngine.h"

#include "Text/Abi.h"
#include "Text/GlyphAtlas.h"

#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vixen::Text {

namespace {

constexpr const char* kBundledLatoFamily = "LatoLatin";
constexpr FT_Int32 kSizedGlyphLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;

struct FontFace {
    FT_Face face = nullptr;
    std::string path;
    Rml::String family;
    Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
    Rml::Style::FontWeight weight = Rml::Style::FontWeight::Normal;
    bool fallback = false;
    ~FontFace() {
        if (face)
            FT_Done_Face(face);
    }
};

struct FontHandle {
    FontFace* font = nullptr;
    int size = 16;
    Rml::FontMetrics metrics{};
    GlyphAtlas atlas;
};

float MissingGlyphAdvance(int fontSize) {
    return static_cast<float>(std::max(fontSize / 3, 1) + 2);
}

FontHandle* FromHandle(Rml::FontFaceHandle handle) {
    return reinterpret_cast<FontHandle*>(handle);
}

int RoundWidth(float width) {
    return std::max(0, static_cast<int>(std::lround(width)));
}

FT_UInt GlyphIndex(FT_Face face, Rml::Character character) {
    return FT_Get_Char_Index(face, static_cast<FT_ULong>(character));
}

bool SetFontSize(FT_Face face, int size) {
    if (!face || size <= 0)
        return false;
    return FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(size) * 64, 0, 0) == 0;
}

float MeasureWidth(FontHandle& handle, Rml::StringView string, const Rml::TextShapingContext& context,
                   Rml::Character priorCharacter) {
    if (!handle.font || !handle.font->face)
        return 0.0f;

    FT_Face face = handle.font->face;
    if (!SetFontSize(face, handle.size))
        return 0.0f;

    int advance = 0;
    FT_UInt previousGlyph = priorCharacter == Rml::Character::Null ? 0 : GlyphIndex(face, priorCharacter);
    const bool kerningEnabled = context.font_kerning != Rml::Style::FontKerning::None && FT_HAS_KERNING(face);

    for (auto it = Rml::StringIteratorU8(string); it; ++it) {
        const FT_UInt glyphIndex = GlyphIndex(face, *it);
        if (kerningEnabled && previousGlyph != 0 && glyphIndex != 0) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
                advance += static_cast<int>(kerning.x >> 6);
        }
        if (glyphIndex == 0)
            advance += static_cast<int>(MissingGlyphAdvance(handle.size));
        else if (FT_Load_Glyph(face, glyphIndex, kSizedGlyphLoadFlags) == 0)
            advance += static_cast<int>(face->glyph->metrics.horiAdvance >> 6);
        advance += static_cast<int>(context.letter_spacing);
        previousGlyph = glyphIndex;
    }
    return static_cast<float>(std::max(advance, 0));
}

bool IsBundledFacePath(const Rml::String& fileName) {
    std::error_code error;
    const std::filesystem::path path = std::filesystem::weakly_canonical(fileName, error);
    if (error)
        return false;
    const std::string extension = path.extension().string();
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
        return false;
    return path.generic_string().find("/assets/ui/") != std::string::npos;
}

} // namespace

struct RmlFontEngine::Impl {
    FT_Library library = nullptr;
    std::vector<std::unique_ptr<FontFace>> faces;
    std::vector<std::unique_ptr<FontHandle>> handles;
    std::unordered_map<std::uint64_t, FontHandle*> handleLookup;
    bool initialized = false;

    FontHandle* GetOrCreateHandle(FontFace* face, int size) {
        const std::uint64_t key = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(face)) << 1) ^
                                  static_cast<std::uint32_t>(std::max(size, 1));
        if (auto it = handleLookup.find(key); it != handleLookup.end())
            return it->second;

        auto handle = std::make_unique<FontHandle>();
        handle->font = face;
        handle->size = std::max(size, 1);
        FT_Face ftFace = face->face;
        handle->metrics.size = handle->size;
        handle->metrics.has_ellipsis = FT_Get_Char_Index(ftFace, 0x2026) != 0;
        if (SetFontSize(ftFace, handle->size)) {
            const FT_Size_Metrics& sizeMetrics = ftFace->size->metrics;
            handle->metrics.ascent = static_cast<float>(sizeMetrics.ascender) / 64.0f;
            handle->metrics.descent = static_cast<float>(-sizeMetrics.descender) / 64.0f;
            handle->metrics.line_spacing = static_cast<float>(sizeMetrics.height) / 64.0f;
            handle->metrics.underline_position = std::max(
                0.0f, static_cast<float>(FT_MulFix(-ftFace->underline_position, sizeMetrics.y_scale)) / 64.0f);
            handle->metrics.underline_thickness = std::max(
                1.0f, static_cast<float>(FT_MulFix(ftFace->underline_thickness, sizeMetrics.y_scale)) / 64.0f);
            const FT_UInt xGlyph = FT_Get_Char_Index(ftFace, static_cast<FT_ULong>('x'));
            if (xGlyph != 0 && FT_Load_Glyph(ftFace, xGlyph, kSizedGlyphLoadFlags) == 0)
                handle->metrics.x_height = static_cast<float>(ftFace->glyph->metrics.height) / 64.0f;
            else
                handle->metrics.x_height = 0.5f * handle->metrics.line_spacing;
        }

        FontHandle* result = handle.get();
        handles.push_back(std::move(handle));
        handleLookup.emplace(key, result);
        return result;
    }

    FontFace* FindFace(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight) const {
        FontFace* fallback = nullptr;
        for (const auto& candidate : faces) {
            if (candidate->style == style &&
                Rml::StringUtilities::StringCompareCaseInsensitive(candidate->family, family)) {
                if (candidate->weight == weight || weight == Rml::Style::FontWeight::Auto)
                    return candidate.get();
                if (!fallback)
                    fallback = candidate.get();
            }
            if (candidate->fallback && !fallback)
                fallback = candidate.get();
        }
        return fallback;
    }
};

RmlFontEngine::RmlFontEngine() : impl_(std::make_unique<Impl>()) {}

RmlFontEngine::~RmlFontEngine() = default;

void RmlFontEngine::Initialize() {
    if (impl_->initialized)
        return;
    impl_->initialized = FT_Init_FreeType(&impl_->library) == 0;
}

void RmlFontEngine::Shutdown() {
    ReleaseFontResources();
    impl_->faces.clear();
    if (impl_->library) {
        FT_Done_FreeType(impl_->library);
        impl_->library = nullptr;
    }
    impl_->initialized = false;
}

bool RmlFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace,
                                 Rml::Style::FontWeight weight) {
    return LoadFontFace(fileName, faceIndex, kBundledLatoFamily, Rml::Style::FontStyle::Normal, weight, fallbackFace);
}

bool RmlFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                                 Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) {
    if (!impl_->initialized || !impl_->library || faceIndex < 0 || !IsBundledFacePath(fileName))
        return false;

    const std::string path(fileName);
    for (const auto& loaded : impl_->faces) {
        if (loaded->path == path && loaded->family == family && loaded->style == style && loaded->weight == weight)
            return true;
    }

    auto loaded = std::make_unique<FontFace>();
    loaded->path = path;
    loaded->family = family;
    loaded->style = style;
    loaded->weight = weight;
    loaded->fallback = fallbackFace;
    if (FT_New_Face(impl_->library, path.c_str(), faceIndex, &loaded->face) != 0)
        return false;
    impl_->faces.push_back(std::move(loaded));
    return true;
}

bool RmlFontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
                                 Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) {
    (void)data;
    (void)faceIndex;
    (void)family;
    (void)style;
    (void)weight;
    (void)fallbackFace;
    return false; // This lane accepts packaged files only; in-memory/system fonts are not font sources.
}

Rml::FontFaceHandle RmlFontEngine::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                                     Rml::Style::FontWeight weight, int size) {
    FontFace* face = impl_->FindFace(family, style, weight);
    return face ? reinterpret_cast<Rml::FontFaceHandle>(impl_->GetOrCreateHandle(face, size)) : Rml::FontFaceHandle{};
}

Rml::FontEffectsHandle RmlFontEngine::PrepareFontEffects(Rml::FontFaceHandle handle, const Rml::FontEffectList& effects) {
    (void)handle;
    (void)effects;
    return 0;
}

const Rml::FontMetrics& RmlFontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
    static const Rml::FontMetrics empty{};
    FontHandle* fontHandle = FromHandle(handle);
    return fontHandle ? fontHandle->metrics : empty;
}

int RmlFontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                                  const Rml::TextShapingContext& shapingContext, Rml::Character priorCharacter) {
    FontHandle* fontHandle = FromHandle(handle);
    return fontHandle ? RoundWidth(MeasureWidth(*fontHandle, string, shapingContext, priorCharacter)) : 0;
}

int RmlFontEngine::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                                  Rml::FontEffectsHandle effectsHandle, Rml::StringView string,
                                  Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                                  const Rml::TextShapingContext& shapingContext, Rml::TexturedMeshList& meshList) {
    (void)effectsHandle;
    (void)opacity;
    FontHandle* handle = FromHandle(faceHandle);
    if (!handle || !handle->font || !handle->font->face)
        return 0;

    FT_Face face = handle->font->face;
    GlyphAtlas& atlas = handle->atlas;
    if (!SetFontSize(face, handle->size))
        return 0;
    const bool kerningEnabled = shapingContext.font_kerning != Rml::Style::FontKerning::None && FT_HAS_KERNING(face);
    float penX = position.x;
    FT_UInt previousGlyph = 0;
    bool hasGeometry = false;
    Rml::TexturedMesh textMesh;

    for (auto it = Rml::StringIteratorU8(string); it; ++it) {
        const FT_UInt glyphIndex = GlyphIndex(face, *it);
        if (kerningEnabled && previousGlyph != 0 && glyphIndex != 0) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
                penX += static_cast<float>(kerning.x >> 6);
        }

        float glyphAdvance = 0.0f;
        bool glyphAdded = false;
        if (glyphIndex == 0) {
            glyphAdvance = MissingGlyphAdvance(handle->size);
            glyphAdded = atlas.AddReplacementGlyph(face, handle->size);
        } else if (FT_Load_Glyph(face, glyphIndex, kSizedGlyphLoadFlags) == 0) {
            glyphAdvance = static_cast<float>(face->glyph->metrics.horiAdvance >> 6);
            glyphAdded = atlas.AddGlyph(face, glyphIndex);
        }

        if (glyphAdded) {
            if (const AtlasGlyph* glyph = atlas.FindGlyph(glyphIndex); glyph && glyph->width > 0.0f && glyph->height > 0.0f) {
                hasGeometry = true;
                const float fontSize = static_cast<float>(handle->size);
                const float padding = glyph->padding * fontSize;
                const float leftBearing = std::floor((glyph->bearingX + glyph->padding) * fontSize);
                const float topBearing = std::ceil((glyph->bearingY - glyph->padding) * fontSize);
                const float x0 = penX + leftBearing - padding;
                const float y0 = position.y - topBearing - padding;
                const float x1 = x0 + glyph->width * fontSize;
                const float y1 = y0 + glyph->height * fontSize;
                const int firstVertex = static_cast<int>(textMesh.mesh.vertices.size());
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y0), colour, Rml::Vector2f(glyph->u0, glyph->v0)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y0), colour, Rml::Vector2f(glyph->u1, glyph->v0)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y1), colour, Rml::Vector2f(glyph->u1, glyph->v1)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y1), colour, Rml::Vector2f(glyph->u0, glyph->v1)});
                const int indices[6] = {firstVertex, firstVertex + 1, firstVertex + 2,
                                        firstVertex, firstVertex + 2, firstVertex + 3};
                textMesh.mesh.indices.insert(textMesh.mesh.indices.end(), std::begin(indices), std::end(indices));
            }
        }
        penX += glyphAdvance;
        penX += shapingContext.letter_spacing;
        previousGlyph = glyphIndex;
    }

    if (hasGeometry) {
        textMesh.texture = atlas.GetTexture(renderManager);
        meshList.push_back(std::move(textMesh));
    }
    return RoundWidth(penX - position.x);
}

int RmlFontEngine::GetVersion(Rml::FontFaceHandle handle) {
    FontHandle* fontHandle = FromHandle(handle);
    return fontHandle && fontHandle->font ? static_cast<int>(fontHandle->atlas.Version()) : 0;
}

void RmlFontEngine::ReleaseFontResources() {
    impl_->handles.clear();
    impl_->handleLookup.clear();
}

} // namespace Vixen::Text
